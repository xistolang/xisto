/* Xisto runtime: xisto:profiler, a sampling CPU profiler. Appended after xisto_rt.h when a program imports
   xisto:profiler (dd-trace's profiling does, for Datadog's Profiles tab).

   Sampling: setitimer(ITIMER_PROF) sends SIGPROF every 1/hz seconds of the process's CPU time (all threads: the
   signal goes to the thread that was running, so busy workers are sampled too). The handler walks the frame
   pointers of the interrupted thread (the program is built with them; see "stack traces" in xisto_rt.h) and
   counts the stack in a lock-free table: no malloc, no locks. Each word of the walk is read through the kernel
   (vm_read_overwrite on macOS, process_vm_readv on Linux) unless it is on a page already read, so a frame
   pointer that libc used as a plain register cannot crash the program.

   Two tables: collect() switches the handlers to the other one, waits for the handlers still writing the old
   one (a few microseconds), then turns it into a pprof profile (profile.proto, encoded here) and clears it.

   Symbols: a return address is the call site right before it (the xs_sites labels stack traces use, with the
   frames the C compiler inlined). The interrupted instruction is in the function the executable's symbol table
   (Mach-O nlist or ELF .symtab) puts around it; when a call-site label follows it in that function, the code is
   that call's: the caller's own code before the call, or, for a callee that is always inlined (xs_prof_inltab),
   the callee's body; so inlined functions keep their frames. Else the function is named by the compiler's table
   (xs_prof_fntab: C name, TypeScript name, file, line), else by its C name (runtime code), else by dladdr
   (system libraries). Not on Windows (start() returns false).

   Time: a sample stands for the process's CPU time while sampling (getrusage) divided by the samples taken, not
   for 1/hz: macOS checks the timer on its 100 Hz clock, so it delivers fewer signals than asked (99 Hz gives about
   60 a second there), and the CPU time of each function would be short by as much. */
#if !defined(_WIN32)
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <sys/ucontext.h>
#else
#include <elf.h>
#include <link.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <ucontext.h>
#endif
#endif

/* the program's functions by C name, and the call sites of functions always inlined (emit.c, for a program that
   imports xisto:profiler) */
typedef struct xs_pfn { const char *cname, *name, *file; int32_t line; } xs_pfn;
typedef struct xs_pinl { const xs_site *site; const char *name, *file; int32_t line; } xs_pinl;
extern const xs_pfn xs_prof_fntab[];
extern const int64_t xs_prof_nfn;
extern const xs_pinl xs_prof_inltab[];
extern const int64_t xs_prof_ninl;

#define XP_MAXF 64           /* frames kept per sample (the innermost ones) */
#define XP_TAB 16384         /* distinct stacks per table (a power of two) */
#define XP_WORDS (1u << 19)  /* frame words per table (4 MB, touched only as used) */
#define XP_PROBES 64
#define XP_LR 1u             /* bit 0 of a frame: the link register (arm64), kept or not when symbolized */

typedef struct { uint64_t hash; uint64_t count; uint32_t off; uint16_t n; uint8_t ready; uint8_t pad; } xp_ent;
typedef struct { xp_ent *tab; uintptr_t *words; uint32_t used; uint64_t dropped, samples; } xp_tabs;
static xp_tabs xp_t[2];
static int xp_active;          /* the table the handlers write */
static int xp_inflight[2];     /* handlers writing each table */
static int xp_on;              /* sampling */
static int xp_owner;           /* start() succeeded once: the process has one profiler */
static int xp_safe_read;       /* the kernel reads the frames (else only the interrupted pc is kept) */
static int64_t xp_hz, xp_period_ns, xp_t0_ns;
static int64_t xp_last_samples; /* samples in the last collect() (tests) */
static int64_t xp_cpu_mark, xp_cpu_acc; /* the process's CPU time when sampling (re)started; CPU time sampled since */
#if !defined(__APPLE__) && !defined(_WIN32)
static pid_t xp_pid;
#endif

#if !defined(_WIN32)
static int64_t xp_wall_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/* the process's CPU time (user + system, every thread) */
static int64_t xp_cpu_ns(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0;
    return ((int64_t)ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000000000 + ((int64_t)ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) * 1000;
}

/* 16 bytes at a (a frame record: the saved frame pointer and the return address), or false */
static bool xp_read16(uintptr_t a, uintptr_t out[2]) {
#if defined(__APPLE__)
    vm_size_t got32 = 0;
    if (vm_read_overwrite(mach_task_self(), (vm_address_t)a, 16, (vm_address_t)out, &got32) != KERN_SUCCESS) return false;
    return got32 == 16;
#else
    struct iovec l = {out, 16}, r = {(void *)a, 16};
    return syscall(SYS_process_vm_readv, xp_pid, &l, 1, &r, 1, 0) == 16;
#endif
}

/* return addresses: pointer-authentication bits off (macOS: 47-bit addresses, arm64e system code signs them;
   Linux arm64: 48-bit addresses, PIE executables live near 0xaaaa00000000) */
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
#define XP_ADDR(x) ((uintptr_t)(x) & (uintptr_t)0x00007FFFFFFFFFFFull)
#elif defined(__aarch64__)
#define XP_ADDR(x) ((uintptr_t)(x) & (uintptr_t)0x0000FFFFFFFFFFFFull)
#else
#define XP_ADDR(x) ((uintptr_t)(x))
#endif

/* the interrupted thread's stack, innermost first: pc, [lr | XP_LR on arm64], then the frame-pointer chain */
static int xp_walk(void *ucv, uintptr_t *f) {
    uintptr_t pc = 0, fp = 0, sp = 0, lr = 0;
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    ucontext_t *uc = ucv;
    pc = (uintptr_t)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
    fp = (uintptr_t)__darwin_arm_thread_state64_get_fp(uc->uc_mcontext->__ss);
    lr = (uintptr_t)__darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss);
    sp = (uintptr_t)__darwin_arm_thread_state64_get_sp(uc->uc_mcontext->__ss);
#elif defined(__APPLE__) && defined(__x86_64__)
    ucontext_t *uc = ucv;
    pc = uc->uc_mcontext->__ss.__rip;
    fp = uc->uc_mcontext->__ss.__rbp;
    sp = uc->uc_mcontext->__ss.__rsp;
#elif defined(__linux__) && defined(__x86_64__)
    ucontext_t *uc = ucv;
    pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    fp = (uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
    sp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
#elif defined(__linux__) && defined(__aarch64__)
    ucontext_t *uc = ucv;
    pc = (uintptr_t)uc->uc_mcontext.pc;
    fp = (uintptr_t)uc->uc_mcontext.regs[29];
    lr = (uintptr_t)uc->uc_mcontext.regs[30];
    sp = (uintptr_t)uc->uc_mcontext.sp;
#else
    (void)ucv;
#endif
    int n = 0;
    pc = XP_ADDR(pc);
    if (!pc) return 0;
    f[n++] = pc & ~(uintptr_t)XP_LR;
    lr = XP_ADDR(lr);
    if (lr && lr != pc) f[n++] = lr | XP_LR;
    if (!xp_safe_read) return n;
    uintptr_t okpage = sp & ~(uintptr_t)4095, lo = sp; /* (the page at sp is the stack the kernel just wrote to) */
    while (fp && n < XP_MAXF) {
        if (fp < lo || fp - lo > ((uintptr_t)8 << 20) || (fp & 7)) break;
        uintptr_t w[2], pg = fp & ~(uintptr_t)4095;
        if (pg == okpage && ((fp + 15) & ~(uintptr_t)4095) == pg) { w[0] = ((uintptr_t *)fp)[0]; w[1] = ((uintptr_t *)fp)[1]; }
        else if (xp_read16(fp, w)) { if (((fp + 15) & ~(uintptr_t)4095) == pg) okpage = pg; }
        else break;
        uintptr_t ra = XP_ADDR(w[1]);
        if (!ra) break;
        f[n++] = ra & ~(uintptr_t)XP_LR;
        lo = fp + 16;
        fp = w[0];
    }
    return n;
}

static void xp_insert(xp_tabs *t, const uintptr_t *f, int n) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < n; i++) h = (h ^ (uint64_t)f[i]) * 1099511628211ull;
    h ^= h >> 29;
    if (!h) h = 1;
    __atomic_fetch_add(&t->samples, 1, __ATOMIC_RELAXED);
    for (uint32_t p = 0; p < XP_PROBES; p++) {
        xp_ent *e = &t->tab[(h + p) & (XP_TAB - 1)];
        uint64_t k = __atomic_load_n(&e->hash, __ATOMIC_ACQUIRE);
        if (k == 0) {
            uint64_t z = 0;
            if (__atomic_compare_exchange_n(&e->hash, &z, h, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                uint32_t off = __atomic_fetch_add(&t->used, (uint32_t)n, __ATOMIC_RELAXED);
                if (off + (uint32_t)n > XP_WORDS) { /* (the words are spent: this slot stays dead until the next collect) */
                    __atomic_store_n(&e->ready, 2, __ATOMIC_RELEASE);
                    __atomic_fetch_add(&t->dropped, 1, __ATOMIC_RELAXED);
                    return;
                }
                for (int i = 0; i < n; i++) t->words[off + (uint32_t)i] = f[i];
                e->off = off;
                e->n = (uint16_t)n;
                e->count = 1;
                __atomic_store_n(&e->ready, 1, __ATOMIC_RELEASE);
                return;
            }
            k = z;
        }
        if (k != h || __atomic_load_n(&e->ready, __ATOMIC_ACQUIRE) != 1 || e->n != n) continue;
        const uintptr_t *w = t->words + e->off;
        int i = 0;
        while (i < n && w[i] == f[i]) i++;
        if (i == n) { __atomic_fetch_add(&e->count, 1, __ATOMIC_RELAXED); return; }
    }
    __atomic_fetch_add(&t->dropped, 1, __ATOMIC_RELAXED);
}

static void xp_handler(int sig, siginfo_t *si, void *ucv) {
    (void)sig;
    (void)si;
    if (!__atomic_load_n(&xp_on, __ATOMIC_RELAXED)) return;
    int saved = errno;
    uintptr_t f[XP_MAXF];
    int n = xp_walk(ucv, f);
    if (n > 0) {
        int b;
        for (;;) {
            b = __atomic_load_n(&xp_active, __ATOMIC_ACQUIRE);
            __atomic_fetch_add(&xp_inflight[b], 1, __ATOMIC_ACQ_REL);
            if (__atomic_load_n(&xp_active, __ATOMIC_ACQUIRE) == b) break;
            __atomic_fetch_sub(&xp_inflight[b], 1, __ATOMIC_ACQ_REL);
        }
        xp_insert(&xp_t[b], f, n);
        __atomic_fetch_sub(&xp_inflight[b], 1, __ATOMIC_ACQ_REL);
    }
    errno = saved;
}

static bool xp_timer(int64_t hz) {
    struct itimerval it;
    memset(&it, 0, sizeof it);
    if (hz > 0) {
        it.it_interval.tv_sec = (time_t)(1000000 / hz / 1000000);
        it.it_interval.tv_usec = (suseconds_t)(1000000 / hz % 1000000);
        it.it_value = it.it_interval;
    }
    return setitimer(ITIMER_PROF, &it, NULL) == 0;
}
#endif

/* start(hz): sampling on, hz times per second of CPU (1..1000); false when the process already has a profiler
   (another thread started it) or the system cannot */
XS_NOINLINE bool xs_cpuprof_start(int64_t hz) {
#if defined(_WIN32)
    (void)hz;
    return false;
#else
    if (hz < 1) hz = 1;
    if (hz > 1000) hz = 1000;
    int expect = 0;
    if (!__atomic_compare_exchange_n(&xp_owner, &expect, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        if (__atomic_load_n(&xp_on, __ATOMIC_ACQUIRE)) return false;
        /* (stopped by this owner: sampling again, into the same tables) */
        xp_hz = hz;
        xp_period_ns = 1000000000 / hz;
        xp_cpu_mark = xp_cpu_ns();
        __atomic_store_n(&xp_on, 1, __ATOMIC_RELEASE);
        return xp_timer(hz);
    }
    for (int i = 0; i < 2; i++) {
        xp_t[i].tab = mmap(NULL, sizeof(xp_ent) * XP_TAB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        xp_t[i].words = mmap(NULL, sizeof(uintptr_t) * XP_WORDS, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (xp_t[i].tab == MAP_FAILED || xp_t[i].words == MAP_FAILED) return false;
    }
    uintptr_t probe[2], here[2] = {1, 2};
#if !defined(__APPLE__)
    xp_pid = getpid();
#endif
    xp_safe_read = xp_read16((uintptr_t)here, probe) && probe[0] == 1 && probe[1] == 2;
    xp_hz = hz;
    xp_period_ns = 1000000000 / hz;
    xp_t0_ns = xp_wall_ns();
    xp_cpu_mark = xp_cpu_ns();
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = xp_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, NULL) != 0) return false;
    __atomic_store_n(&xp_on, 1, __ATOMIC_RELEASE);
    return xp_timer(hz);
#endif
}

/* stop(): no more samples (the ones taken stay for the next collect) */
XS_NOINLINE void xs_cpuprof_stop(void) {
#if !defined(_WIN32)
    if (!__atomic_load_n(&xp_on, __ATOMIC_ACQUIRE)) return;
    xp_timer(0);
    __atomic_store_n(&xp_on, 0, __ATOMIC_RELEASE);
    xp_cpu_acc += xp_cpu_ns() - xp_cpu_mark;
#endif
}

XS_NOINLINE int64_t xs_cpuprof_last_samples(void) { return xp_last_samples; }

#if !defined(_WIN32)
/* ---- symbols ---- */
typedef struct { uintptr_t addr; const char *name; } xp_sym;
static xp_sym *xp_syms;
static size_t xp_nsyms;
static uintptr_t xp_text_lo, xp_text_hi;
static bool xp_syms_done;
static const xs_pfn **xp_fns; /* xs_prof_fntab by C name */
static size_t xp_nfns;
static const xs_pinl **xp_inls; /* xs_prof_inltab by site */
static size_t xp_ninls;

static int xp_sym_cmp(const void *a, const void *b) {
    uintptr_t x = ((const xp_sym *)a)->addr, y = ((const xp_sym *)b)->addr;
    return x < y ? -1 : x > y;
}
static int xp_fn_cmp(const void *a, const void *b) {
    return strcmp((*(const xs_pfn *const *)a)->cname, (*(const xs_pfn *const *)b)->cname);
}
static int xp_inl_cmp(const void *a, const void *b) {
    uintptr_t x = (uintptr_t)(*(const xs_pinl *const *)a)->site, y = (uintptr_t)(*(const xs_pinl *const *)b)->site;
    return x < y ? -1 : x > y;
}
static void xp_sym_add(size_t *cap, uintptr_t addr, const char *name) {
    if (xp_nsyms == *cap) {
        *cap = *cap ? *cap * 2 : 4096;
        xp_syms = realloc(xp_syms, sizeof(xp_sym) * *cap);
        if (!xp_syms) xs_panic("out of memory");
    }
    xp_syms[xp_nsyms].addr = addr;
    xp_syms[xp_nsyms].name = name;
    xp_nsyms++;
}

#if !defined(__APPLE__)
static int xp_phdr_cb(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    *(uintptr_t *)data = (uintptr_t)info->dlpi_addr;
    return 1; /* (the first one is the executable) */
}
#endif

/* the executable's functions, sorted by address (once) */
static void xp_load_syms(void) {
    if (xp_syms_done) return;
    xp_syms_done = true;
    size_t cap = 0;
#if defined(__APPLE__)
    const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(0);
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    if (mh && mh->magic == MH_MAGIC_64) {
        const struct load_command *lc = (const struct load_command *)(mh + 1);
        const struct symtab_command *st = NULL;
        uintptr_t le_vm = 0, le_off = 0;
        bool le = false;
        for (uint32_t i = 0; i < mh->ncmds; i++) {
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
                if (strcmp(sg->segname, "__LINKEDIT") == 0) { le_vm = (uintptr_t)sg->vmaddr; le_off = (uintptr_t)sg->fileoff; le = true; }
                if (strcmp(sg->segname, "__TEXT") == 0) { xp_text_lo = (uintptr_t)sg->vmaddr + (uintptr_t)slide; xp_text_hi = xp_text_lo + (uintptr_t)sg->vmsize; }
            } else if (lc->cmd == LC_SYMTAB) st = (const struct symtab_command *)lc;
            lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
        }
        if (st && le) {
            uintptr_t base = le_vm + (uintptr_t)slide - le_off;
            const struct nlist_64 *nl = (const struct nlist_64 *)(base + st->symoff);
            const char *strs = (const char *)(base + st->stroff);
            for (uint32_t i = 0; i < st->nsyms; i++) {
                if ((nl[i].n_type & N_STAB) || (nl[i].n_type & N_TYPE) != N_SECT) continue;
                uintptr_t a = (uintptr_t)nl[i].n_value + (uintptr_t)slide;
                if (a < xp_text_lo || a >= xp_text_hi) continue;
                const char *nm = strs + nl[i].n_un.n_strx;
                xp_sym_add(&cap, a, nm[0] == '_' ? nm + 1 : nm);
            }
        }
    }
#else
    uintptr_t bias = 0;
    dl_iterate_phdr(xp_phdr_cb, &bias);
    int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    struct stat sb;
    if (fd >= 0 && fstat(fd, &sb) == 0 && sb.st_size > (off_t)sizeof(Elf64_Ehdr)) {
        const uint8_t *m = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0); /* (kept: the names point into it) */
        const Elf64_Ehdr *eh = (const Elf64_Ehdr *)m;
        if (m != MAP_FAILED && memcmp(eh->e_ident, ELFMAG, SELFMAG) == 0 && eh->e_ident[EI_CLASS] == ELFCLASS64 &&
            eh->e_shoff + (uint64_t)eh->e_shnum * sizeof(Elf64_Shdr) <= (uint64_t)sb.st_size) {
            const Elf64_Shdr *sh = (const Elf64_Shdr *)(m + eh->e_shoff);
            xp_text_lo = UINTPTR_MAX;
            for (int i = 0; i < eh->e_shnum; i++) {
                if (sh[i].sh_type != SHT_SYMTAB || sh[i].sh_link >= eh->e_shnum) continue;
                const Elf64_Sym *sy = (const Elf64_Sym *)(m + sh[i].sh_offset);
                const char *strs = (const char *)(m + sh[sh[i].sh_link].sh_offset);
                size_t n = sh[i].sh_size / sizeof(Elf64_Sym);
                for (size_t k = 0; k < n; k++) {
                    if (ELF64_ST_TYPE(sy[k].st_info) != STT_FUNC || !sy[k].st_value) continue;
                    uintptr_t a = (uintptr_t)sy[k].st_value + bias;
                    xp_sym_add(&cap, a, strs + sy[k].st_name);
                    if (a < xp_text_lo) xp_text_lo = a;
                    if (a + sy[k].st_size > xp_text_hi) xp_text_hi = a + (uintptr_t)sy[k].st_size;
                }
            }
            if (!xp_nsyms) xp_text_lo = 0;
        }
    }
    if (fd >= 0) close(fd);
#endif
    if (xp_nsyms) qsort(xp_syms, xp_nsyms, sizeof(xp_sym), xp_sym_cmp);
    xp_nfns = xs_prof_nfn > 0 ? (size_t)xs_prof_nfn : 0;
    xp_fns = malloc(sizeof(xs_pfn *) * (xp_nfns ? xp_nfns : 1));
    if (!xp_fns) xs_panic("out of memory");
    for (size_t i = 0; i < xp_nfns; i++) xp_fns[i] = &xs_prof_fntab[i];
    qsort(xp_fns, xp_nfns, sizeof(xs_pfn *), xp_fn_cmp);
    xp_ninls = xs_prof_ninl > 0 ? (size_t)xs_prof_ninl : 0;
    xp_inls = malloc(sizeof(xs_pinl *) * (xp_ninls ? xp_ninls : 1));
    if (!xp_inls) xs_panic("out of memory");
    for (size_t i = 0; i < xp_ninls; i++) xp_inls[i] = &xs_prof_inltab[i];
    qsort(xp_inls, xp_ninls, sizeof(xs_pinl *), xp_inl_cmp);
    xs_labels_build();
}
/* the executable's function around a, or NULL */
static const xp_sym *xp_sym_of(uintptr_t a) {
    if (!xp_nsyms || a < xp_text_lo || a >= xp_text_hi) return NULL;
    size_t lo = 0, hi = xp_nsyms;
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (xp_syms[mid].addr <= a) lo = mid + 1; else hi = mid; }
    return lo ? &xp_syms[lo - 1] : NULL;
}
/* the end of the function sy (the next symbol) */
static uintptr_t xp_sym_end(const xp_sym *sy) {
    size_t i = (size_t)(sy - xp_syms);
    return i + 1 < xp_nsyms ? xp_syms[i + 1].addr : xp_text_hi;
}
/* the program's function of C name cname (a name the C compiler suffixed, "f.cold.1", as its function) */
static const xs_pfn *xp_fn_named(const char *cname) {
    size_t len = strlen(cname);
    const char *dot = strchr(cname, '.');
    if (dot) len = (size_t)(dot - cname);
    size_t lo = 0, hi = xp_nfns;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        const char *m = xp_fns[mid]->cname;
        int c = strncmp(m, cname, len);
        if (c == 0 && m[len]) c = 1;
        if (c == 0) return xp_fns[mid];
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return NULL;
}
static const xs_pinl *xp_inl_of(const xs_site *s) {
    size_t lo = 0, hi = xp_ninls;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (xp_inls[mid]->site == s) return xp_inls[mid];
        if ((uintptr_t)xp_inls[mid]->site < (uintptr_t)s) lo = mid + 1; else hi = mid;
    }
    return NULL;
}
/* the first call-site label at or after a, below end, or NULL */
static const xs_srec *xp_label_after(uintptr_t a, uintptr_t end) {
    const xs_srec *ls = __atomic_load_n(&xs_labels, __ATOMIC_ACQUIRE);
    if (!ls) return NULL;
    size_t n = (size_t)ls[-1].addr, lo = 0, hi = n;
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (ls[mid].addr < a) lo = mid + 1; else hi = mid; }
    if (lo >= n || ls[lo].addr >= end) return NULL;
    /* labels at one address (a call that is the last thing of an inlined callee): the innermost call, whose chain
       of inlined callers is the longest */
    size_t best = lo;
    int depth = -1;
    for (size_t k = lo; k < n && ls[k].addr == ls[lo].addr; k++) {
        int d = 0;
        for (const xs_site *s = ls[k].site; s; s = s->inl) d++;
        if (d > depth) { depth = d; best = k; }
    }
    return &ls[best];
}

/* ---- pprof: profile.proto written by hand ---- */
typedef struct { uint8_t *p; size_t n, cap; } xp_pb;
static void xp_pb_put(xp_pb *b, const void *d, size_t n) {
    if (b->cap - b->n < n) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap - b->n < n) cap *= 2;
        b->p = realloc(b->p, cap);
        if (!b->p) xs_panic("out of memory");
        b->cap = cap;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}
static void xp_pb_varint(xp_pb *b, uint64_t v) {
    uint8_t t[10];
    int k = 0;
    do { t[k] = (uint8_t)(v & 0x7F); v >>= 7; if (v) t[k] |= 0x80; k++; } while (v);
    xp_pb_put(b, t, (size_t)k);
}
static void xp_pb_int(xp_pb *b, int field, uint64_t v) { xp_pb_varint(b, (uint64_t)field << 3); xp_pb_varint(b, v); }
static void xp_pb_bytes(xp_pb *b, int field, const void *d, size_t n) {
    xp_pb_varint(b, ((uint64_t)field << 3) | 2);
    xp_pb_varint(b, n);
    xp_pb_put(b, d, n);
}
static void xp_pb_msg(xp_pb *b, int field, xp_pb *m) { xp_pb_bytes(b, field, m->p, m->n); m->n = 0; }

/* strings: the string table, deduplicated */
typedef struct { const char **s; size_t n, cap; int64_t *slot; size_t nslot; } xp_strs;
static uint64_t xp_hash_str(const char *s) {
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull;
    return h;
}
static int64_t xp_str(xp_strs *t, const char *s) {
    if (!s) s = "";
    if (t->n * 2 >= t->nslot) { /* grow and rehash */
        size_t ns = t->nslot ? t->nslot * 2 : 1024;
        int64_t *sl = malloc(sizeof(int64_t) * ns);
        if (!sl) xs_panic("out of memory");
        for (size_t i = 0; i < ns; i++) sl[i] = -1;
        for (size_t i = 0; i < t->n; i++) {
            size_t j = xp_hash_str(t->s[i]) & (ns - 1);
            while (sl[j] >= 0) j = (j + 1) & (ns - 1);
            sl[j] = (int64_t)i;
        }
        free(t->slot);
        t->slot = sl;
        t->nslot = ns;
    }
    size_t j = xp_hash_str(s) & (t->nslot - 1);
    while (t->slot[j] >= 0) {
        if (strcmp(t->s[t->slot[j]], s) == 0) return t->slot[j];
        j = (j + 1) & (t->nslot - 1);
    }
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 1024;
        t->s = realloc(t->s, sizeof(char *) * t->cap);
        if (!t->s) xs_panic("out of memory");
    }
    t->s[t->n] = s;
    t->slot[j] = (int64_t)t->n;
    return (int64_t)t->n++;
}

/* a map from a 64-bit key to an id (>= 1) */
typedef struct { uint64_t *k; uint64_t *v; size_t n, cap; } xp_map;
static uint64_t *xp_map_slot(xp_map *m, uint64_t key, bool *fresh) {
    if ((m->n + 1) * 2 > m->cap) {
        size_t nc = m->cap ? m->cap * 2 : 1024;
        uint64_t *nk = calloc(nc, sizeof(uint64_t)), *nv = calloc(nc, sizeof(uint64_t));
        if (!nk || !nv) xs_panic("out of memory");
        for (size_t i = 0; i < m->cap; i++) {
            if (!m->v[i]) continue;
            size_t j = (m->k[i] * 0x9E3779B97F4A7C15ull >> 20) & (nc - 1);
            while (nv[j]) j = (j + 1) & (nc - 1);
            nk[j] = m->k[i];
            nv[j] = m->v[i];
        }
        free(m->k);
        free(m->v);
        m->k = nk;
        m->v = nv;
        m->cap = nc;
    }
    size_t j = (key * 0x9E3779B97F4A7C15ull >> 20) & (m->cap - 1);
    while (m->v[j]) {
        if (m->k[j] == key) { *fresh = false; return &m->v[j]; }
        j = (j + 1) & (m->cap - 1);
    }
    m->k[j] = key;
    m->n++;
    *fresh = true;
    return &m->v[j];
}

typedef struct {
    xp_strs strs;
    xp_map locs, fns;   /* address (x2, +1 for the interrupted pc) -> location id; name/file/line -> function id */
    xp_pb out, loc, fn, tmp;
    uint64_t nloc, nfn;
} xp_enc;

static uint64_t xp_function(xp_enc *e, const char *name, const char *file, int64_t line) {
    int64_t ni = xp_str(&e->strs, name), fi = xp_str(&e->strs, file);
    bool fresh;
    uint64_t key = (uint64_t)ni * 0x9E3779B97F4A7C15ull + (uint64_t)fi * 0xC2B2AE3D27D4EB4Full + (uint64_t)line * 0x165667B19E3779F9ull;
    uint64_t *slot = xp_map_slot(&e->fns, key, &fresh);
    if (!fresh) return *slot;
    *slot = ++e->nfn;
    xp_pb_int(&e->tmp, 1, *slot);
    xp_pb_int(&e->tmp, 2, (uint64_t)ni);
    xp_pb_int(&e->tmp, 3, (uint64_t)ni);
    xp_pb_int(&e->tmp, 4, (uint64_t)fi);
    if (line > 0) xp_pb_int(&e->tmp, 5, (uint64_t)line);
    xp_pb_msg(&e->fn, 5, &e->tmp);
    return *slot;
}
static void xp_line(xp_pb *lines, xp_pb *tmp, uint64_t fid, int64_t line) {
    xp_pb_int(tmp, 1, fid);
    if (line > 0) xp_pb_int(tmp, 2, (uint64_t)line);
    xp_pb_msg(lines, 4, tmp);
}
static const char *xp_site_name(const xs_site *s) { return s->fn && s->fn[0] ? s->fn : "(anonymous)"; }
/* the function around address a (the interrupted pc, or a return address with no call site) */
static uint64_t xp_func_of(xp_enc *e, uintptr_t a, int64_t *line) {
    *line = 0;
    const xp_sym *sy = xp_sym_of(a);
    if (sy) {
        const xs_pfn *pf = xp_fn_named(sy->name);
        if (pf) { *line = pf->line; return xp_function(e, pf->name, pf->file, pf->line); }
        if (strcmp(sy->name, "main") == 0 || strncmp(sy->name, "xs_init_m", 9) == 0) return xp_function(e, "(top level)", "", 0);
        return xp_function(e, sy->name, "", 0);
    }
    Dl_info di;
    if (dladdr((void *)a, &di) && di.dli_sname) {
        const char *base = di.dli_fname ? strrchr(di.dli_fname, '/') : NULL;
        return xp_function(e, di.dli_sname, base ? base + 1 : di.dli_fname ? di.dli_fname : "", 0);
    }
    return xp_function(e, "(unknown)", "", 0);
}
/* the location id of address a; leaf: a is the interrupted pc (else a return address) */
static uint64_t xp_location(xp_enc *e, uintptr_t a, bool leaf) {
    bool fresh;
    uint64_t *slot = xp_map_slot(&e->locs, ((uint64_t)a << 1) | (leaf ? 1 : 0), &fresh);
    if (!fresh) return *slot;
    uint64_t id = ++e->nloc;
    *slot = id;
    xp_pb lines = {0};
    const xs_site *s = leaf ? NULL : xs_site_of(a);
    if (leaf) { /* (the code before a call-site label in the same function is that call's) */
        const xp_sym *sy = xp_sym_of(a);
        const xs_srec *l = sy ? xp_label_after(a, xp_sym_end(sy)) : NULL;
        if (l) {
            s = l->site;
            const xs_pinl *in = xp_inl_of(s);
            if (in) xp_line(&lines, &e->tmp, xp_function(e, in->name, in->file, in->line), in->line);
        }
    }
    if (s) {
        for (; s; s = s->inl) { /* (the call, then the calls that the C compiler inlined it into) */
            uint64_t fid = xp_function(e, xp_site_name(s), s->file, 0);
            xp_line(&lines, &e->tmp, fid, s->line);
        }
    } else {
        int64_t line;
        uint64_t fid = xp_func_of(e, leaf ? a : a - 1, &line); /* (a call that ends its function returns past it) */
        xp_line(&lines, &e->tmp, fid, line);
    }
    xp_pb_int(&e->tmp, 1, id);
    xp_pb_int(&e->tmp, 3, a);
    xp_pb_put(&e->tmp, lines.p, lines.n);
    free(lines.p);
    xp_pb_msg(&e->loc, 4, &e->tmp);
    return id;
}
#endif

/* collect(): the samples since the last collect() (or start()) as a pprof profile (uncompressed protobuf) */
XS_NOINLINE xs_Uint8Array *xs_cpuprof_collect(void) {
#if defined(_WIN32)
    return xs_u8_new(0);
#else
    if (!__atomic_load_n(&xp_owner, __ATOMIC_ACQUIRE) || !xp_t[0].tab) return xs_u8_new(0);
    int old = __atomic_load_n(&xp_active, __ATOMIC_ACQUIRE);
    __atomic_store_n(&xp_active, 1 - old, __ATOMIC_RELEASE);
    while (__atomic_load_n(&xp_inflight[old], __ATOMIC_ACQUIRE)) sched_yield();
    int64_t now = xp_wall_ns(), t0 = xp_t0_ns;
    xp_t0_ns = now;
    if (__atomic_load_n(&xp_on, __ATOMIC_ACQUIRE)) {
        int64_t c = xp_cpu_ns();
        xp_cpu_acc += c - xp_cpu_mark;
        xp_cpu_mark = c;
    }
    int64_t cpu = xp_cpu_acc;
    xp_cpu_acc = 0;
    xp_tabs *t = &xp_t[old];
    uint64_t nsamples = t->dropped;
    for (size_t i = 0; i < XP_TAB; i++) if (t->tab[i].ready == 1) nsamples += t->tab[i].count;
    /* the CPU time of one sample (see "Time" above) */
    int64_t per = nsamples && cpu > 0 ? cpu / (int64_t)nsamples : xp_period_ns;
    xp_load_syms();
    xp_enc e;
    memset(&e, 0, sizeof e);
    xp_str(&e.strs, "");
    int64_t s_sample = xp_str(&e.strs, "sample"), s_count = xp_str(&e.strs, "count");
    int64_t s_cpu = xp_str(&e.strs, "cpu"), s_ns = xp_str(&e.strs, "nanoseconds");
    /* sample_type: sample/count, cpu/nanoseconds (dd-trace's names on Node) */
    xp_pb_int(&e.tmp, 1, (uint64_t)s_sample);
    xp_pb_int(&e.tmp, 2, (uint64_t)s_count);
    xp_pb_msg(&e.out, 1, &e.tmp);
    xp_pb_int(&e.tmp, 1, (uint64_t)s_cpu);
    xp_pb_int(&e.tmp, 2, (uint64_t)s_ns);
    xp_pb_msg(&e.out, 1, &e.tmp);
    xp_pb ids = {0}, vals = {0};
    uint64_t locs[XP_MAXF + 1];
    int64_t total = 0;
    for (size_t i = 0; i < XP_TAB; i++) {
        xp_ent *en = &t->tab[i];
        if (en->ready != 1 || !en->count) continue;
        const uintptr_t *w = t->words + en->off;
        int n = en->n, k = 0;
        for (int j = 0; j < n; j++) {
            uintptr_t a = w[j];
            if (a & XP_LR) { /* the link register: the caller of a leaf that has no frame of its own yet */
                a &= ~(uintptr_t)XP_LR;
                const xp_sym *sl = xp_sym_of(a - 1), *sp = xp_sym_of(w[0]);
                bool same_fn = sl && sp && sl->addr == sp->addr;
                if (same_fn || (j + 1 < n && w[j + 1] == a)) continue;
            }
            locs[k++] = xp_location(&e, a, j == 0);
        }
        for (int j = 0; j < k; j++) xp_pb_varint(&ids, locs[j]);
        xp_pb_varint(&vals, en->count);
        xp_pb_varint(&vals, en->count * (uint64_t)per);
        xp_pb_bytes(&e.tmp, 1, ids.p, ids.n);
        xp_pb_bytes(&e.tmp, 2, vals.p, vals.n);
        ids.n = vals.n = 0;
        xp_pb_msg(&e.out, 2, &e.tmp);
        total += (int64_t)en->count;
    }
    if (t->dropped) { /* samples the tables had no room for, as one frame */
        uint64_t fid = xp_function(&e, "(profiler buffer full)", "", 0);
        uint64_t id = ++e.nloc;
        xp_pb lines = {0};
        xp_line(&lines, &e.tmp, fid, 0);
        xp_pb_int(&e.tmp, 1, id);
        xp_pb_put(&e.tmp, lines.p, lines.n);
        free(lines.p);
        xp_pb_msg(&e.loc, 4, &e.tmp);
        xp_pb_varint(&ids, id);
        xp_pb_varint(&vals, t->dropped);
        xp_pb_varint(&vals, t->dropped * (uint64_t)per);
        xp_pb_bytes(&e.tmp, 1, ids.p, ids.n);
        xp_pb_bytes(&e.tmp, 2, vals.p, vals.n);
        ids.n = vals.n = 0;
        xp_pb_msg(&e.out, 2, &e.tmp);
        total += (int64_t)t->dropped;
    }
    xp_pb_put(&e.out, e.loc.p, e.loc.n);
    xp_pb_put(&e.out, e.fn.p, e.fn.n);
    for (size_t i = 0; i < e.strs.n; i++) xp_pb_bytes(&e.out, 6, e.strs.s[i], strlen(e.strs.s[i]));
    xp_pb_int(&e.out, 9, (uint64_t)t0);
    xp_pb_int(&e.out, 10, (uint64_t)(now - t0));
    xp_pb_int(&e.tmp, 1, (uint64_t)s_cpu);
    xp_pb_int(&e.tmp, 2, (uint64_t)s_ns);
    xp_pb_msg(&e.out, 11, &e.tmp);
    xp_pb_int(&e.out, 12, (uint64_t)per);
    xp_last_samples = total;
    /* the old table, empty for its next turn */
    memset(t->tab, 0, sizeof(xp_ent) * XP_TAB);
    t->used = 0;
    t->dropped = 0;
    t->samples = 0;
    xs_Uint8Array *r = xs_u8_raw((int64_t)e.out.n, false, false);
    if (e.out.n) memcpy(r->data, e.out.p, e.out.n);
    free(e.out.p);
    free(e.loc.p);
    free(e.fn.p);
    free(e.tmp.p);
    free(ids.p);
    free(vals.p);
    free(e.strs.s);
    free(e.strs.slot);
    free(e.locs.k);
    free(e.locs.v);
    free(e.fns.k);
    free(e.fns.v);
    return r;
#endif
}
