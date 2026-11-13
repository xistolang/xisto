/* Xisto runtime. Embedded verbatim at the top of every generated C file. */
#if !defined(__APPLE__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* memmem, arc4random on glibc */
#endif
/* Per-thread state is only needed by servers (XISTO_WORKERS); elsewhere a plain global is much cheaper:
   on macOS every XS_TLS access is a call through the TLV thunk. */
#ifdef XS_THREADS
#define XS_TLS _Thread_local
#else
#define XS_TLS
#endif
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
/* ---- Windows (xisto build --target=windows-x64): what the C library there lacks ----
   memmem; open_memstream (the console.log layout writes containers into memory streams and reads them back
   after each flush): a temporary file whose bytes are copied out on fflush and fclose. */
static void *xs_memmem(const void *h, size_t hn, const void *n, size_t nn) {
    if (nn == 0) return (void *)h;
    for (const char *p = h, *e = (const char *)h + hn; (size_t)(e - p) >= nn; p++)
        if (*p == *(const char *)n && memcmp(p, n, nn) == 0) return (void *)p;
    return NULL;
}
#define memmem xs_memmem
typedef struct { FILE *f; char **buf; size_t *size; } xs_wms;
static xs_wms xs_wmss[256];
static int xs_nwms;
static FILE *xs_open_memstream(char **buf, size_t *size) {
    FILE *f = tmpfile();
    if (!f || xs_nwms == 256) { fputs("xisto: cannot open a memory stream\n", stderr); exit(1); }
    xs_wmss[xs_nwms++] = (xs_wms){f, buf, size};
    *buf = NULL;
    *size = 0;
    return f;
}
static void xs_wms_sync(xs_wms *m) {
    fflush(m->f);
    long end = ftell(m->f);
    if (end < 0) end = 0;
    *m->buf = realloc(*m->buf, (size_t)end + 1);
    fseek(m->f, 0, SEEK_SET);
    size_t got = fread(*m->buf, 1, (size_t)end, m->f);
    (*m->buf)[got] = 0;
    *m->size = got;
    fseek(m->f, 0, SEEK_END);
}
static int xs_wfflush(FILE *f) {
    for (int i = xs_nwms - 1; i >= 0; i--) if (xs_wmss[i].f == f) { xs_wms_sync(&xs_wmss[i]); return 0; }
    return fflush(f);
}
static int xs_wfclose(FILE *f) {
    for (int i = xs_nwms - 1; i >= 0; i--)
        if (xs_wmss[i].f == f) { xs_wms_sync(&xs_wmss[i]); xs_wmss[i] = xs_wmss[--xs_nwms]; break; }
    return fclose(f);
}
#define open_memstream xs_open_memstream
#define fflush xs_wfflush
#define fclose xs_wfclose
#define ftello ftell
#endif

/* ---- console.log output: Node's util.inspect layout ----
   Printers write to XS_OUT: stdout, or the buffer of the container being laid out. A container (object, array,
   Map, typed array) opens a buffer, prints its entries into it, then decides as util.inspect does (depth 2,
   breakLength 80, compact 3): one line if it fits, else one entry per line indented; arrays of more than six
   entries in aligned columns; deeper than two levels "[Name]"; a cycle "[Circular *n]", with "<ref *n>" on the
   object it points back to (written after the children, which is why entries go to a buffer first). */
/* code that runs once (literal setup, a main that only wires things up): optimizing it buys nothing and was a
   third of a release build of the Conduit API (312 ms of -O2 on the decorator registrations alone) */
#if defined(__clang__)
#define XS_RUN_ONCE __attribute__((optnone, noinline, cold))
#else
#define XS_RUN_ONCE __attribute__((cold, optimize("O0")))
#endif
static XS_TLS FILE *xs_out_f;
#define XS_OUT (xs_out_f ? xs_out_f : stdout)
typedef struct xs_insp { FILE *f, *parent; char *buf; size_t size; size_t *off; int n, cap; int saved_level; void *obj; int recurse; } xs_insp;
static XS_TLS int xs_insp_level;   /* ctx.indentationLvl */
static XS_TLS int xs_insp_rt;      /* recurseTimes of the container being printed */
static XS_TLS int xs_insp_cur;     /* ctx.currentDepth */
static XS_TLS void **xs_insp_seen;
static XS_TLS int xs_insp_nseen, xs_insp_seencap;
static XS_TLS void **xs_circ_obj;
static XS_TLS int xs_ncirc, xs_circcap;
static int xs_circ_id(void *o, bool add) {
    for (int i = 0; i < xs_ncirc; i++) if (xs_circ_obj[i] == o) return i + 1;
    if (!add) return 0;
    if (xs_ncirc == xs_circcap) { xs_circcap = xs_circcap ? xs_circcap * 2 : 8; xs_circ_obj = realloc(xs_circ_obj, sizeof(void *) * (size_t)xs_circcap); }
    xs_circ_obj[xs_ncirc++] = o;
    return xs_ncirc;
}
/* false: the container is not expanded (a cycle or too deep) and its marker is already written */
/* empty: what an empty container prints ("[]", "Name {}"), at any depth, as Node; NULL if it has entries */
static __attribute__((noinline)) bool xs_insp_begin(xs_insp *fr, void *obj, const char *name, const char *empty) {
    for (int i = 0; obj && i < xs_insp_nseen; i++)
        if (xs_insp_seen[i] == obj) { fprintf(XS_OUT, "[Circular *%d]", xs_circ_id(obj, true)); return false; }
    if (empty) { fputs(empty, XS_OUT); return false; }
    if (xs_insp_rt > 2) { fprintf(XS_OUT, "[%s]", name); return false; }
    if (xs_insp_nseen == 0) xs_ncirc = 0; /* a new top-level value */
    if (xs_insp_nseen == xs_insp_seencap) { xs_insp_seencap = xs_insp_seencap ? xs_insp_seencap * 2 : 16; xs_insp_seen = realloc(xs_insp_seen, sizeof(void *) * (size_t)xs_insp_seencap); }
    xs_insp_seen[xs_insp_nseen++] = obj;
    fr->obj = obj;
    fr->recurse = ++xs_insp_rt;
    xs_insp_cur = xs_insp_rt;
    fr->parent = xs_out_f;
    fr->buf = NULL;
    fr->size = 0;
    fr->f = open_memstream(&fr->buf, &fr->size);
    xs_out_f = fr->f;
    fr->off = NULL;
    fr->n = fr->cap = 0;
    fr->saved_level = xs_insp_level;
    xs_insp_level += 2;
    return true;
}
/* the next entry starts here */
static __attribute__((noinline)) void xs_insp_entry(xs_insp *fr) {
    fflush(fr->f);
    if (fr->n == fr->cap) { fr->cap = fr->cap ? fr->cap * 2 : 8; fr->off = realloc(fr->off, sizeof(size_t) * (size_t)(fr->cap + 1)); }
    fr->off[fr->n++] = (size_t)ftello(fr->f);
}
static size_t xs_cp_len(const char *p, size_t n) { size_t k = 0; for (size_t i = 0; i < n; i++) if ((p[i] & 0xC0) != 0x80) k++; return k; }
static void xs_pad(FILE *o, size_t n) { for (size_t i = 0; i < n; i++) fputc(' ', o); }
/* open: "Name {", "{", "[", "Map(2) {", "Uint8Array(3) ["; close: "}" or "]"; array: entries are elements
   (grouped when more than six); numeric: all of them numbers (right-aligned in columns); more: the last entry is
   "... n more items" */
static __attribute__((noinline)) void xs_insp_end(xs_insp *fr, const char *open, const char *close, bool array, bool numeric, bool more) {
    fflush(fr->f);
    size_t total = (size_t)ftello(fr->f);
    xs_out_f = fr->parent;
    xs_insp_level = fr->saved_level;
    xs_insp_nseen--;
    xs_insp_rt--;
    FILE *o = XS_OUT;
    int n = fr->n;
    const char **ent = malloc(sizeof(char *) * (size_t)(n ? n : 1));
    size_t *len = malloc(sizeof(size_t) * (size_t)(n ? n : 1));
    for (int i = 0; i < n; i++) {
        size_t a = fr->off[i], b = i + 1 < n ? fr->off[i + 1] : total;
        ent[i] = fr->buf + a;
        len[i] = b - a;
    }
    char base[32] = "";
    int id = xs_circ_id(fr->obj, false);
    if (id) snprintf(base, sizeof base, "<ref *%d>", id);
    size_t baselen = strlen(base);
    if (n == 0) {
        if (baselen) { fputs(base, o); fputc(' ', o); }
        fputs(open, o);
        fputs(close, o);
    } else {
        /* the entries of the output (rows, when grouped) */
        int on = n;
        char **out = NULL;
        size_t *olen = NULL;
        bool grouped = false;
        if (array && n > 6) {
            int outputLength = more ? n - 1 : n;
            size_t totalLength = 0, maxLength = 0, sep = 2;
            size_t *dataLen = malloc(sizeof(size_t) * (size_t)n);
            for (int i = 0; i < outputLength; i++) {
                dataLen[i] = xs_cp_len(ent[i], len[i]);
                totalLength += dataLen[i] + sep;
                if (maxLength < dataLen[i]) maxLength = dataLen[i];
            }
            size_t actualMax = maxLength + sep;
            if (actualMax * 3 + (size_t)xs_insp_level < 80 && ((double)totalLength / (double)actualMax > 5 || maxLength <= 6)) {
                double averageBias = sqrt((double)actualMax - (double)totalLength / (double)n);
                double biasedMax = fmax((double)actualMax - 3 - averageBias, 1);
                double c1 = round(sqrt(2.5 * biasedMax * (double)outputLength) / biasedMax);
                double c2 = floor((80.0 - (double)xs_insp_level) / (double)actualMax);
                int columns = (int)fmin(fmin(c1, c2), fmin(3 * 4, 15));
                if (columns > 1) {
                    size_t *maxLineLength = malloc(sizeof(size_t) * (size_t)columns);
                    for (int i = 0; i < columns; i++) {
                        size_t lineLength = 0;
                        for (int j = i; j < n; j += columns) if (j < outputLength && dataLen[j] > lineLength) lineLength = dataLen[j];
                        maxLineLength[i] = lineLength + sep;
                    }
                    int rows = (outputLength + columns - 1) / columns + (more ? 1 : 0);
                    out = malloc(sizeof(char *) * (size_t)rows);
                    olen = malloc(sizeof(size_t) * (size_t)rows);
                    int r = 0;
                    for (int i = 0; i < outputLength; i += columns) {
                        int max = i + columns < outputLength ? i + columns : outputLength;
                        char *line = NULL;
                        size_t llen = 0;
                        FILE *lf = open_memstream(&line, &llen);
                        int j = i;
                        for (; j < max - 1; j++) {
                            size_t w = dataLen[j] + 2, pad = maxLineLength[j - i] > w ? maxLineLength[j - i] - w : 0;
                            if (numeric) { xs_pad(lf, pad); fwrite(ent[j], 1, len[j], lf); fputs(", ", lf); }
                            else { fwrite(ent[j], 1, len[j], lf); fputs(", ", lf); xs_pad(lf, pad); }
                        }
                        if (numeric) {
                            size_t w = dataLen[j], target = maxLineLength[j - i] - sep;
                            xs_pad(lf, target > w ? target - w : 0);
                        }
                        fwrite(ent[j], 1, len[j], lf);
                        fclose(lf);
                        out[r] = line;
                        olen[r++] = llen;
                    }
                    if (more) {
                        out[r] = malloc(len[n - 1] + 1);
                        memcpy(out[r], ent[n - 1], len[n - 1]);
                        olen[r++] = len[n - 1];
                    }
                    on = r;
                    grouped = true;
                    free(maxLineLength);
                }
            }
            free(dataLen);
        }
        const char **e = grouped ? (const char **)out : ent;
        size_t *el = grouped ? olen : len;
        bool single = false;
        if (xs_insp_cur - fr->recurse < 3 && on == n) {
            size_t start = (size_t)on + (size_t)xs_insp_level + strlen(open) + baselen + 10;
            size_t totalLength = (size_t)on + start;
            bool below = totalLength + (size_t)on <= 80;
            for (int i = 0; i < on && below; i++) {
                totalLength += xs_cp_len(e[i], el[i]);
                if (totalLength > 80) below = false;
            }
            bool nl = false;
            for (int i = 0; i < on && !nl; i++) if (memchr(e[i], '\n', el[i])) nl = true;
            single = below && !nl;
        }
        if (baselen) { fputs(base, o); fputc(' ', o); }
        fputs(open, o);
        if (single) {
            for (int i = 0; i < on; i++) { fputs(i ? ", " : " ", o); fwrite(e[i], 1, el[i], o); }
            fputc(' ', o);
        } else {
            for (int i = 0; i < on; i++) {
                fputs(i ? ",\n" : "\n", o);
                xs_pad(o, (size_t)xs_insp_level + 2);
                fwrite(e[i], 1, el[i], o);
            }
            fputc('\n', o);
            xs_pad(o, (size_t)xs_insp_level);
        }
        fputs(close, o);
        if (grouped) { for (int i = 0; i < on; i++) free(out[i]); free(out); free(olen); }
    }
    free(ent);
    free(len);
    fclose(fr->f);
    free(fr->buf);
    free(fr->off);
}

#include <setjmp.h>
#if defined(_WIN32) && defined(__aarch64__) /* arm64: mingw's own pair, which unwinds no SEH frames */
#define _setjmp(b) __mingw_setjmp((b))
#define _longjmp(b, v) __mingw_longjmp((b), (v))
#elif defined(_WIN32) /* the C library's own: every register saved; a NULL frame means longjmp unwinds no SEH frames */
#define _setjmp(b) _setjmp((b), NULL)
#define _longjmp(b, v) longjmp((b), (v))
#endif
#define XS_AI __attribute__((always_inline)) static inline
/* T | null of a primitive: a sentinel value. int: INT64_MIN; boolean: 2 in an int8_t; number: a NaN with a
   payload no arithmetic produces (computed NaNs are the quiet 0x7FF8...). */
#define XS_NULL_INT INT64_MIN
#define XS_NULL_BOOL ((int8_t)2)
#define XS_NULL_NUM_BITS 0x7FF4000000000001ull
static inline double xs_bool_to_number(bool b) { return b ? 1.0 : 0.0; } /* Number(true) */
static inline double xs_null_num(void) { union { uint64_t u; double d; } x = { XS_NULL_NUM_BITS }; return x.d; }
static inline bool xs_isnull_num(double d) { union { double d; uint64_t u; } x = { d }; return x.u == XS_NULL_NUM_BITS; }
/* user functions: one source frame = one machine frame, for stack traces (see "stack traces") */
/* runtime functions that can fail or call back into user code (tools/noinline-scan.py keeps the list) */
#define XS_FI static /* user functions that cannot fail: never a frame, free to inline */
#define XS_FII __attribute__((always_inline)) static inline /* ... and small enough to inline everywhere (emit.c small_ctor) */
#ifdef XS_NO_TRACE /* XISTO_NO_TRACE=1: the same program without stack-trace support, to measure its cost */
#define XS_FA static
#define XS_FN static
#define XS_NOINLINE static
#else
#define XS_FN __attribute__((noinline)) static
#define XS_NOINLINE __attribute__((noinline)) static
/* called from one place: inlined there; its sites carry that call (xs_site.inl) */
#define XS_FA __attribute__((always_inline)) static inline
#endif

/* A recovery point lets a long-running host (the HTTP server) turn a runtime error inside a
   request handler into an error response instead of exiting. Locals of the abandoned frames leak. */
static XS_TLS jmp_buf *xs_recover;
static XS_TLS const char *xs_recover_msg;

/* try/catch: a stack of jump buffers; the thrown Error waits in xs_thrown until the catch block takes it */
static XS_TLS jmp_buf **xs_try_stack; /* 256 slots; each coroutine has its own */
static XS_TLS int xs_try_depth;
static XS_TLS void *xs_thrown;
/* node:async_hooks: the context AsyncLocalStorage reads, an owned reference to the innermost frame (a class of the
   xisto:async_hooks module, or NULL). A coroutine takes the context it was created in and swaps it in and out as it
   runs (xisto_net.h, xs_coro_resume): awaits, Promise.all tasks and timers keep their caller's context */
static XS_TLS void *xs_als_cur;
static void *xs_Error_new(void *msg);
static void xs_throw(void *err);
#ifdef XS_DEBUG
static void xs_dbg_exception(const char *name, const char *message, bool caught); /* runtime/xisto_dbg.h */
#endif

/* the handler stack grows on demand (its capacity in the slot before the first: a coroutine that never
   nests deeply keeps a few bytes, not 2 KB) */
static __attribute__((noinline)) void xs_try_grow(void) {
    int cap = xs_try_stack ? (int)(intptr_t)xs_try_stack[-1] : 0;
    if (cap >= 512) { fflush(stdout); fprintf(stderr, "runtime error: try blocks nested too deep\n"); exit(70); }
    int nc = cap ? cap * 4 : 8;
    if (nc > 512) nc = 512;
    jmp_buf **raw = realloc(xs_try_stack ? xs_try_stack - 1 : NULL, sizeof(jmp_buf *) * (size_t)(nc + 1));
    if (!raw) { fprintf(stderr, "runtime error: out of memory\n"); exit(70); }
    raw[0] = (jmp_buf *)(intptr_t)nc;
    xs_try_stack = raw + 1;
}
/* ---- the cleanup stack: what an exception must release on its way out ----
   An exception is a longjmp: it skips the releases at the end of every scope between the throw and the catch,
   in the throwing function and in every function between (a for-of's iterable, a local array, a string).
   In programs that can catch (a try, or a server, which turns a handler's error into a response), each
   reference-counted local registers the address of its slot here when it is declared and leaves when its scope
   ends. A throw releases, while those frames are still alive, everything registered after the catch's mark. */
static XS_TLS void ***xs_cln;
static XS_TLS size_t xs_cln_n, xs_cln_cap;
static __attribute__((noinline)) void xs_cln_grow(void) {
    xs_cln_cap = xs_cln_cap ? xs_cln_cap * 2 : 256;
    xs_cln = realloc(xs_cln, sizeof(void **) * xs_cln_cap);
    if (!xs_cln) { fprintf(stderr, "runtime error: out of memory\n"); exit(70); }
}
static inline __attribute__((always_inline)) void xs_cln_push(void **slot) {
    if (__builtin_expect(xs_cln_n == xs_cln_cap, 0)) xs_cln_grow();
    xs_cln[xs_cln_n++] = slot;
}
static void xs_release_(void *p);
static __attribute__((noinline)) void xs_cln_unwind(size_t mark) {
    while (xs_cln_n > mark) {
        void **slot = xs_cln[--xs_cln_n];
        void *v = *slot;
        *slot = NULL;
        xs_release_(v);
    }
}
static XS_TLS size_t xs_recover_cln; /* the mark of the server's recovery point (the handler's start) */
/* each handler takes two slots: its jump buffer and the cleanup mark at the try */
static inline void xs_try_push(jmp_buf *b) {
    if (!xs_try_stack || 2 * xs_try_depth + 2 > (int)(intptr_t)xs_try_stack[-1]) xs_try_grow();
    xs_try_stack[2 * xs_try_depth] = b;
    xs_try_stack[2 * xs_try_depth + 1] = (jmp_buf *)(uintptr_t)xs_cln_n;
    xs_try_depth++;
}
static inline void xs_try_pop(void) { xs_try_depth--; }
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define XS_ASAN 1
#endif
#endif
#ifdef XS_ASAN /* a throw is a longjmp: on a task's own stack AddressSanitizer does not clear the frames it skipped */
static void (*xs_asan_after_jump)(void);
static void *xs_caught(void) { if (xs_asan_after_jump) xs_asan_after_jump(); void *e = xs_thrown; xs_thrown = NULL; return e; }
#else
static void *xs_caught(void) { void *e = xs_thrown; xs_thrown = NULL; return e; } /* owned by the catch block */
#endif

/* ---- stack traces, free until an error happens ----
   The compiler describes every call site with an xs_site (function, file, line, column) and puts a label
   right after each call instruction, recorded in the xs_sites section as {address, site}. Nothing runs on
   the happy path. When an error is created, the runtime walks the frame-pointer chain (the arm64 ABI keeps
   it; elsewhere the compiler is given -fno-omit-frame-pointer) and maps each return address to the label
   that follows it. User functions are never inlined into each other, so each source frame is a machine
   frame; runtime functions that can fail are not inlined either, so a failure inside them returns to a
   labelled call. Failures inline in user code (integer overflow, array index) carry their site directly. */
/* inl: for a function the C compiler inlined into its only caller, that call (printed right after) */
typedef struct xs_site { const char *fn, *file; const struct xs_site *inl; int32_t line, col; } xs_site;
_Static_assert(sizeof(xs_site) == 32, "the compiler emits site addresses as table + 32 * index");
typedef struct { uintptr_t addr; const xs_site *site; } xs_srec;
#if defined(__APPLE__)
/* in __DATA_CONST, which every program already has (the GOT), so the table does not add a 16 KB page */
#define XS_SITES_SEG "__DATA_CONST"
extern const xs_srec xs_srec_begin[] __asm("section$start$" XS_SITES_SEG "$__xs_sites");
extern const xs_srec xs_srec_end[] __asm("section$end$" XS_SITES_SEG "$__xs_sites");
#elif defined(_WIN32)
/* PE/COFF has no __start_/__stop_ symbols: the linker merges xs_sites$a, xs_sites$m and xs_sites$z in that
   order, so the entries ($m) sit between two markers (zero padding the linker may add has addr 0, never a
   match). Without this the table read as empty and a trace on Windows had only its first frame. */
__attribute__((section("xs_sites$a"), used)) static xs_srec xs_srec_a_[1] = {{0, 0}};
__attribute__((section("xs_sites$z"), used)) static xs_srec xs_srec_z_[1] = {{0, 0}};
#define xs_srec_begin ((const xs_srec *)(xs_srec_a_ + 1))
#define xs_srec_end ((const xs_srec *)xs_srec_z_)
#else
extern const xs_srec __start_xs_sites[] __attribute__((weak));
extern const xs_srec __stop_xs_sites[] __attribute__((weak));
#define xs_srec_begin __start_xs_sites
#define xs_srec_end __stop_xs_sites
#endif
/* XS_POST(n) right after a call in generated C; n indexes xs_sitetab. %= keeps the label unique when the
   C compiler duplicates code. The asm is volatile with a memory clobber: it stays after the call and
   prevents a tail call, so the return address is in the caller, just before the label. */
#if defined(__APPLE__)
#define XS_SEC_PUSH ".pushsection " XS_SITES_SEG ",__xs_sites,regular,no_dead_strip\n\t"
#define XS_L "L"
#define XS_TAB "_xs_sitetab"
#elif defined(_WIN32)
#define XS_SEC_PUSH ".pushsection xs_sites$m,\"dw\"\n\t"
#define XS_L ".L"
#define XS_TAB "xs_sitetab"
#else
#define XS_SEC_PUSH ".pushsection xs_sites,\"aw\"\n\t"
#define XS_L ".L"
#define XS_TAB "xs_sitetab"
#endif
#ifdef XS_NO_TRACE
#define XS_POST(n) ((void)0)
#else
#define XS_POST(n) __asm__ volatile(XS_L "xq%=:\n\t" XS_SEC_PUSH ".p2align 3\n\t.quad " XS_L "xq%=\n\t.quad " XS_TAB "+" #n "*32\n\t.popsection" ::: "memory")
#endif
#define XS_S(n) (&xs_sitetab[n])
#define XS_TIGHT 16 /* at most a couple of register moves between a return address and its label */
#define XS_MAX_FRAMES 40
typedef struct { const xs_site *top; int n; uintptr_t ra[XS_MAX_FRAMES]; } xs_trace;
static volatile int xs_never; /* read on error paths so the compiler cannot treat them as noreturn */

#if defined(_WIN32)
/* Windows: LLVM's x64 frame pointer points into the frame (for SEH unwinding), not at the saved one, so the
   chain below cannot be walked there; the system walks the stack from the unwind tables (.pdata) instead */
__declspec(dllimport) unsigned short __stdcall RtlCaptureStackBackTrace(unsigned long skip, unsigned long n, void **frames, unsigned long *hash);
#endif
__attribute__((noinline)) static void xs_trace_capture(xs_trace *t, const xs_site *top) {
    t->top = top;
    t->n = 0;
#if defined(_WIN32)
    t->n = RtlCaptureStackBackTrace(1, XS_MAX_FRAMES, (void **)t->ra, NULL); /* (1: from the caller's return address on) */
    return;
#endif
    uintptr_t *fp = (uintptr_t *)__builtin_frame_address(0);
    while (fp && t->n < XS_MAX_FRAMES) {
        uintptr_t *next = (uintptr_t *)fp[0], ra = fp[1];
        if (!ra) break;
        t->ra[t->n++] = ra;
        /* the chain ends at a zero frame pointer (thread and coroutine entry); anything else odd stops the walk */
        if (next <= fp || (uintptr_t)next - (uintptr_t)fp > ((uintptr_t)8 << 20) || ((uintptr_t)next & 7)) break;
        fp = next;
    }
}

static xs_srec *xs_labels;   /* published once with release order; xs_labels[-1].addr holds the count */
static int xs_srec_cmp(const void *a, const void *b) {
    const xs_srec *x = a, *y = b;
    return x->addr < y->addr ? -1 : x->addr > y->addr;
}
/* sorted once, at the first error that is reported */
static void xs_labels_build(void) {
    if (__atomic_load_n(&xs_labels, __ATOMIC_ACQUIRE)) return;
    const xs_srec *b = xs_srec_begin, *e = xs_srec_end;
    size_t n = b && e > b ? (size_t)(e - b) : 0;
    xs_srec *ls = malloc((n + 1) * sizeof *ls);
    if (!ls) return;
    ls[0].addr = n;
    ls++;
    if (n) memcpy(ls, b, n * sizeof *ls);
    qsort(ls, n, sizeof *ls, xs_srec_cmp);
    xs_srec *expect = NULL;
    if (!__atomic_compare_exchange_n(&xs_labels, &expect, ls, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) free(ls - 1);
}
/* the call site a return address belongs to, or NULL (runtime code) */
static const xs_site *xs_site_of(uintptr_t ra) {
    const xs_srec *ls = __atomic_load_n(&xs_labels, __ATOMIC_ACQUIRE);
    if (!ls) return NULL;
    size_t n = (size_t)ls[-1].addr, lo = 0, hi = n;
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (ls[mid].addr < ra) lo = mid + 1; else hi = mid; }
    if (lo < n && ls[lo].addr - ra <= XS_TIGHT) return ls[lo].site;
    return NULL;
}
static size_t xs_site_line(char *out, size_t cap, const xs_site *s) {
    if (cap == 0) return 0;
    int n = s->fn && s->fn[0] ? snprintf(out, cap, "    at %s (%s:%d:%d)\n", s->fn, s->file, s->line, s->col)
                              : snprintf(out, cap, "    at %s:%d:%d\n", s->file, s->line, s->col);
    return n < 0 ? 0 : (size_t)n < cap ? (size_t)n : cap - 1;
}
/* "    at f (file.ts:3:10)\n" per frame, innermost first, as in Node. With a top site (an inline failure),
   ra[0] is in xs_panic_at and ra[1] is the failing function itself, already described by the top site. */
static size_t xs_trace_format(const xs_trace *t, char *out, size_t cap) {
    size_t len = 0;
    if (cap) out[0] = 0;
    xs_labels_build();
    for (const xs_site *s = t->top; s; s = s->inl) len += xs_site_line(out + len, cap - len, s);
    for (int i = t->top ? 2 : 0; i < t->n; i++)
        for (const xs_site *s = xs_site_of(t->ra[i]); s; s = s->inl) len += xs_site_line(out + len, cap - len, s);
    return len;
}
static void xs_trace_print(const xs_trace *t, FILE *f) {
    char buf[4096];
    xs_trace_format(t, buf, sizeof buf);
    fputs(buf, f);
}

static XS_TLS const char *xs_panic_msg_pending;
static XS_TLS bool xs_panic_pending;
static XS_TLS xs_trace xs_recover_trace; /* where the error that reached the HTTP handler's recovery point came from */
static XS_TLS bool xs_recover_client;    /* that error came from reading JSON (a bad request body: 400, not 500) */
static XS_TLS xs_trace xs_panic_trace;
__attribute__((noinline)) static void xs_panic_at(const char *msg, const xs_site *site);
/* from inline code: the barrier keeps the call from becoming a tail call, which would drop the frame; after it
   the path ends (xs_panic_at throws or exits), which the compiler is told here, at the check, and not on
   xs_panic_at itself (see docs/poc8-stacktrace.md): otherwise every failed check "may return" having changed
   any memory, and a loop reloads every array pointer after each index check (nbody: 0.73 s) */
#define XS_FAIL(msg, site) do { xs_panic_at(msg, site); __asm__ volatile(""); __builtin_unreachable(); } while (0)
#define XS_ISAFE_MAX INT64_C(9007199254740991)
/* In range: -2^53 <= r <= 2^53 - 1 (-2^53 is exact in a double too, so Node gives the same number). Inputs are in
   range, so r = a +- b is within +-2^54. The tests are plain comparisons: in a loop the compiler keeps the 64-bit
   constant in a register and each test is one compare-and-branch (a shift test was 3-4 instructions: +9% in
   fannkuch on the Xeon). */
#define XS_IOUT(r) ((uint64_t)(r) + (UINT64_C(1) << 53) > (UINT64_C(1) << 54) - 1)
#define XS_IOUT_HI(r) ((r) > XS_ISAFE_MAX)        /* only the top can be crossed (a non-negative or + constant >= 0) */
#define XS_IOUT_LO(r) ((r) < -XS_ISAFE_MAX - 1)   /* only the bottom (- constant >= 0) */
/* a sum of two values >= 0 is below 2^54: past the range iff bit 53 is set — a bit test, no 64-bit constant, which
   matters in a recursive function (binarytrees' itemCheck: the constant cost two callee-saved registers per call) */
#define XS_IOUT_NN(r) (((r) & (INT64_C(1) << 53)) != 0)
/* with a constant operand only one side can be crossed: one test (as the 64-bit overflow test was) */
#define XS_IOUT_ADD(r, b) (__builtin_constant_p(b) ? ((b) >= 0 ? XS_IOUT_HI(r) : XS_IOUT_LO(r)) : XS_IOUT(r))
#define XS_IADD_C_OUT(a, b) ((b) >= 0 ? (a) > XS_ISAFE_MAX - (b) : (a) < -XS_ISAFE_MAX - 1 - (b))
#define XS_IOVF_MSG "integer overflow: the result left JavaScript's safe integer range (+-2^53 - 1); use number or bigint"
__attribute__((noinline)) static void xs_panic_at(const char *msg, const xs_site *site) {
    xs_trace_capture(&xs_panic_trace, site);
#ifdef XS_DEBUG
    if (xs_try_depth == 0) xs_dbg_exception("Error", msg, false); /* with a try, xs_throw reports it */
#endif
    if (xs_try_depth > 0) { /* runtime errors are catchable, as in JavaScript */
        xs_panic_msg_pending = msg;
        xs_panic_pending = true;
        xs_throw(xs_Error_new(NULL));
    } else if (xs_recover) {
        xs_recover_trace = xs_panic_trace;
        xs_recover_client = strncmp(msg, "JSON", 4) == 0; /* only the runtime's JSON reader words panics so */
        xs_recover_msg = msg;
        xs_cln_unwind(xs_recover_cln);
        _longjmp(*xs_recover, 1);
    }
    fflush(stdout);
    fprintf(stderr, "runtime error: %s\n", msg);
    xs_trace_print(&xs_panic_trace, stderr);
    if (!xs_never) exit(70);
}
static void xs_panic(const char *msg) { xs_panic_at(msg, NULL); }

/* integer to decimal without printf: the hot path of JSON serialization */
static size_t xs_itoa(int64_t v, char *out /* >= 21 bytes */) {
    char tmp[21];
    size_t n = 0;
    uint64_t u = v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    size_t k = 0;
    if (v < 0) out[k++] = '-';
    while (n) out[k++] = tmp[--n];
    out[k] = 0;
    return k;
}

static void xs_panicf(const char *fmt, const char *a) {
    static char buf[512];
    snprintf(buf, sizeof buf, fmt, a);
    xs_panic(buf);
}

/* ---- reference counting ----
   Every heap value starts with xs_hdr. The compiler inserts retain/release;
   values created and not stored go to the autorelease pool, drained per statement. */

/* what a heap value is: how it is freed and printed. One per allocation site (static), held by the header as a
   22-bit offset (below). Identity checks compare type->free, which every site of a type shares. */
typedef void (*xs_visit_cb)(void **slot, void *arg);
typedef struct __attribute__((aligned(64))) xs_type {
    void (*free)(void *);
    void (*print)(void *);
    /* only the types that can be part of a cycle (the compiler sees every type): calls cb on each slot holding a
       reference. Objects of these types carry XS_CYC and are looked at by the cycle collector. */
    void (*visit)(void *, xs_visit_cb cb, void *arg);
    /* region classes (the compiler's region.c): drop releases what the object holds outside its region, when
       the region is rewound; rref takes away its references to other objects of the region (XISTO_REGION_CHECK) */
    void (*drop)(void *);
    void (*rref)(void *);
    /* the program's classes when it uses `any` (src/checker.c, dynamic path): their fields by name, read and
       written through x.f / x[k] on an any; NULL otherwise */
    const struct xs_dyn_cls *dyn;
} xs_type;
/* hdr.cls: the size class in the low bits, then the flags */
#define XS_CLS_MASK 31    /* size class of the block (8-byte steps), 0 = not pooled */
#define XS_STACK (1 << 5) /* lives in its creator's stack frame (stack allocation, src/stackalloc.c) */
#define XS_DEAD (1 << 6)  /* a cycle suspect whose count reached zero: its block waits among the suspects for the
                             collector, which gives it back (the header has no room for its position there) */
#define XS_CYC (1 << 7)   /* may be part of a cycle */
#define XS_BUF (1 << 8)   /* among the cycle collector's suspects */
#define XS_RGN (1 << 9)   /* an object of a dual region class made in its region (src/region.c): its count is
                             kept, and reaching zero does nothing; the region's rewind frees it */
/* Every heap value starts with this 8-byte header: a node of two references is 24 bytes, not 32 (the size
   classes go in 8-byte steps), and binary-trees moves a quarter less memory. cls holds the size class and the
   flags in its low 10 bits and the type in the other 22: not a pointer but the descriptor's address in
   64-byte units (xs_type is 64-byte aligned), taken modulo 2^22. Reading it back goes through one anchor
   descriptor: the 22-bit difference to the anchor, signed, reaches 128 MB each way. Every descriptor sits in
   one section (xs_types), and the program checks once, at start, that the section fits in that reach; so
   writing a header is a constant or (the compiler folds XS_TIX of a static descriptor into an address
   computation), with no subtraction and no check per allocation. */
typedef struct xs_hdr {
    int32_t rc;
    uint32_t cls;
} xs_hdr;
_Static_assert(sizeof(xs_hdr) == 8, "xs_hdr is 8 bytes");
#define XS_TIX(t) ((uint32_t)((uintptr_t)(t) << 4)) /* the type's part of hdr.cls (bits 10..31) */
#if defined(__APPLE__)
#define XS_TYPE_SEC __attribute__((section("__DATA_CONST,__xs_types")))
extern const char xs_types_begin[] __asm("section$start$__DATA_CONST$__xs_types");
extern const char xs_types_end[] __asm("section$end$__DATA_CONST$__xs_types");
#elif defined(_WIN32) /* (markers merged around the descriptors, as the site table's) */
#define XS_TYPE_SEC __attribute__((section("xs_types$m")))
__attribute__((section("xs_types$a"), used, aligned(64))) static const char xs_types_a_[64] = {0};
__attribute__((section("xs_types$z"), used, aligned(64))) static const char xs_types_z_[64] = {0};
#define xs_types_begin xs_types_a_
#define xs_types_end xs_types_z_
#else
#define XS_TYPE_SEC __attribute__((section("xs_types")))
extern const char __start_xs_types[] __attribute__((weak));
extern const char __stop_xs_types[] __attribute__((weak));
#define xs_types_begin __start_xs_types
#define xs_types_end __stop_xs_types
#endif
/* the anchor: the type of the static closures of top-level functions used as values (never freed; their
   headers are written where they are taken, xs_cl_static, since XS_TIX is not a constant initializer) */
static void xs_print_closure(void *p);
static const xs_type xs_immortal_closure_type XS_TYPE_SEC = {NULL, xs_print_closure};
/* (the anchor's address goes through an empty asm: the C compiler, seeing a pointer made from the anchor's,
   would take it to stay inside the anchor and read the anchor's own fields) */
static inline __attribute__((always_inline)) const xs_type *xs_type_of(const void *p) {
    uintptr_t base = (uintptr_t)&xs_immortal_closure_type;
    __asm__("" : "+r"(base));
    int32_t d = (int32_t)(((const xs_hdr *)p)->cls - XS_TIX(base)) >> 10; /* (the low 10 bits never borrow) */
    return (const xs_type *)(base + (uintptr_t)((intptr_t)d << 6));
}
static inline __attribute__((always_inline)) void xs_set_type(xs_hdr *h, const xs_type *t) {
    h->cls = (h->cls & 0x3ff) | XS_TIX(t);
}
/* at start: the descriptors within the header's reach (an executable with 2M types; never, but checked) */
static void xs_types_check(void) {
    const char *b = xs_types_begin, *e = xs_types_end; /* (weak on ELF: null without the section) */
    if (b && e && (size_t)(e - b) > ((size_t)1 << 27)) {
        fprintf(stderr, "xisto: the program's type descriptors do not fit in 128 MB; this is a bug in xisto\n");
        exit(70);
    }
}
#define XS_IMMORTAL (INT32_MAX / 2)

static XS_TLS int64_t xs_live;   /* allocations alive (literals excluded); per thread when workers > 1 */
static XS_TLS int64_t xs_allocs; /* total allocations */
/* Small objects (up to 248 bytes) come from per-thread free lists by 8-byte size class: allocation-heavy
   code (an object or a string per loop iteration) otherwise spends most of its time in malloc and free.
   The step is 8 because the header is 8 bytes and a 24-byte node then takes a class of its own: on the
   reference machine (Linux x86, Xeon E5) binary-trees ran 1.22x faster than main with 8-byte classes and
   1.00x with 16-byte ones; on the M4 and an Intel Mac 8 also won. The Graviton2 alone preferred 16 (one node
   in three straddles a cache line at 24). XS_OBJ_SHIFT=4 (XISTO_CC="cc -DXS_OBJ_SHIFT=4") gives 16-byte
   classes, to measure on a machine. */
#ifndef XS_OBJ_SHIFT
#define XS_OBJ_SHIFT 3
#endif
#define XS_OBJ_STEP ((size_t)1 << XS_OBJ_SHIFT)
#define XS_SMALL_CLASSES ((256 >> XS_OBJ_SHIFT) - 1)
#define XS_SMALL_MAX (XS_SMALL_CLASSES << XS_OBJ_SHIFT)
#ifdef XS_HEAP_CHECK /* every object from malloc: a guarded malloc (DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib) sees each one */
#define XS_SMALL_LIMIT 0
#else
#define XS_SMALL_LIMIT XS_SMALL_MAX
#endif
typedef struct xs_free_blk { struct xs_free_blk *next; } xs_free_blk;
static XS_TLS xs_free_blk *xs_small[XS_SMALL_CLASSES + 1];
static XS_TLS int xs_small_n[XS_SMALL_CLASSES + 1];

/* Small objects (<= XS_SMALL_MAX bytes) are carved from 256 KB slabs and, once freed, wait on their size class's list
   for the next allocation; they never go back to malloc. Splitting a 22 MB text into 2.7M strings spent
   most of its time in malloc and free; a slab carve is a pointer bump. */
static XS_TLS char *xs_slab_p, *xs_slab_end;
static __attribute__((noinline)) void *xs_slab_new(size_t sz) {
    size_t chunk = 256 * 1024;
    xs_slab_p = malloc(chunk);
    if (!xs_slab_p) xs_panic("out of memory");
    xs_slab_end = xs_slab_p + chunk;
    void *h = xs_slab_p;
    xs_slab_p += sz;
    return h;
}
static inline __attribute__((always_inline)) void *xs_slab_carve(int k) {
    size_t sz = (size_t)k << XS_OBJ_SHIFT;
    if (xs_slab_p && xs_slab_p + sz <= xs_slab_end) { void *h = xs_slab_p; xs_slab_p += sz; return h; }
    return xs_slab_new(sz);
}
static void *xs_obj_alloc_slow(size_t size, int k) {
    if (k) { void *h = xs_slab_carve(k); memset(h, 0, size); return h; }
    void *h = calloc(1, size);
    if (!h) xs_panic("out of memory");
    return h;
}
static void *xs_obj_alloc_slow_nz(size_t size, int k) { /* (the caller writes every byte that matters) */
    if (k) return xs_slab_carve(k);
    void *h = malloc(size);
    if (!h) xs_panic("out of memory");
    return h;
}
/* inlined at every allocation site: the size is a constant there, so the clearing is a few stores */
static XS_TLS bool xs_gc_due; /* enough suspects: the cycle collector runs at the next allocation */
static XS_TLS size_t xs_sus_n; /* cycle suspects waiting (alive) */
static void xs_gc_step(void);
/* zero: clear the object (fields start as 0/NULL); false when the constructor writes every field first
   (emit.c ctor_sets_every_field: binarytrees on the Xeon, -5% with the region's variant) */
static inline __attribute__((always_inline)) void *xs_obj_alloc_tz(size_t size, const xs_type *type, bool zero) {
    if (__builtin_expect(xs_gc_due, 0)) xs_gc_step();
    xs_hdr *h;
    int k = size <= XS_SMALL_LIMIT ? (int)((size + XS_OBJ_STEP - 1) >> XS_OBJ_SHIFT) : 0;
    if (k && xs_small[k]) {
        h = (xs_hdr *)xs_small[k];
        xs_small[k] = xs_small[k]->next;
        xs_small_n[k]--;
        if (zero) memset(h, 0, size);
    } else {
        h = zero ? xs_obj_alloc_slow(size, k) : xs_obj_alloc_slow_nz(size, k);
    }
    h->rc = 1;
    h->cls = (uint32_t)k | (type->visit ? XS_CYC : 0) | XS_TIX(type);
    xs_live++;
    xs_allocs++;
    return h;
}
#define xs_obj_alloc_t(size, type) xs_obj_alloc_tz((size), (type), true)
#define xs_obj_alloc_t_nz(size, type) xs_obj_alloc_tz((size), (type), false)

/* an object in its creator's frame (src/stackalloc.c), declared = {0}: a header like any other, so retains, releases and the
   cleanup stack work unchanged; when its count reaches zero its fields are released and the memory stays */
static inline __attribute__((always_inline)) void *xs_obj_on_stack(void *mem, size_t size, const xs_type *type) {
    xs_hdr *h = mem; /* cleared where it is declared */
    (void)size;
    h->rc = 1;
    h->cls = XS_STACK | XS_TIX(type);
    xs_live++;
    return h;
}

/* the same without clearing: for objects whose every byte is written next (string data) */
static inline __attribute__((always_inline)) void *xs_obj_alloc_raw(size_t size, const xs_type *type) {
    xs_hdr *h;
    int k = size <= XS_SMALL_LIMIT ? (int)((size + XS_OBJ_STEP - 1) >> XS_OBJ_SHIFT) : 0;
    if (k && xs_small[k]) {
        h = (xs_hdr *)xs_small[k];
        xs_small[k] = xs_small[k]->next;
        xs_small_n[k]--;
    } else if (k) {
        h = xs_slab_carve(k);
    } else {
        h = malloc(size);
        if (!h) xs_panic("out of memory");
    }
    h->rc = 1;
    h->cls = (uint32_t)k | (type->visit ? XS_CYC : 0) | XS_TIX(type);
    xs_live++;
    xs_allocs++;
    return h;
}

/* xs_obj_alloc(size, free, print): the type is a constant made where it is called */
#define xs_obj_alloc(size, free_fn, print_fn) xs_obj_alloc_t((size), ({ static const xs_type xs_t_ XS_TYPE_SEC = {(free_fn), (print_fn)}; &xs_t_; }))

static inline __attribute__((always_inline)) void xs_obj_free_block(void *p, uint32_t k);
static inline __attribute__((always_inline)) void xs_obj_free(void *p) {
    xs_live--;
    uint32_t k = ((xs_hdr *)p)->cls;
    if (__builtin_expect(k & (XS_BUF | XS_STACK), 0)) {
        if (k & XS_STACK) return; /* its frame owns the memory: only the fields were released */
        ((xs_hdr *)p)->cls |= XS_DEAD; /* a suspect: the collector gives the block back (xs_gc_step) */
        xs_sus_n--;
        return;
    }
    xs_obj_free_block(p, k);
}
/* a block back where it came from (k: its size class) */
/* -DXS_HEAP_CHECK (debugging the runtime or a package's C, never in a normal build): a freed block is marked and
   kept out of reuse, and the first retain or release of it stops the program with the xisto stack, where the
   bad reference is (a use after free otherwise shows up later, as a broken free list in another allocation) */
#define XS_FREED_RC ((int32_t)-0x5EADBEEF)
/* (a trap, not xs_panic: it stops inside the function with the bad reference, where a debugger shows it, and keeps
   retain/release free of failing calls for tools/noinline-scan.py) */
#define XS_HEAP_TRAP(what) (fprintf(stderr, "xisto heap check: %s\n", what), __builtin_trap())
static inline __attribute__((always_inline)) void xs_obj_free_block(void *p, uint32_t k) {
#ifdef XS_HEAP_CHECK
    if (((xs_hdr *)p)->rc == XS_FREED_RC) XS_HEAP_TRAP("an object freed twice");
    ((xs_hdr *)p)->rc = XS_FREED_RC;
    (void)k;
    if (getenv("XISTO_HEAP_CHECK_FREE")) free(p); /* (with guard malloc: the freed page faults at the bad access) */
    return;
#endif
    k &= XS_CLS_MASK;
    if (k) { /* from a slab: back on its class's list */
        xs_free_blk *b = p;
        b->next = xs_small[k];
        xs_small[k] = b;
        xs_small_n[k]++;
        return;
    }
    free(p);
}

/* An object several threads hold (a string a worker was sent, xs_str_share): its count lives above
   XS_SHARED_RC (XS_SHARED_RC + the holders) and changes atomically; every other count is a thread's own and
   changes with plain arithmetic. The test is one compare on the count the operation loads anyway. */
#define XS_SHARED_RC 0x60000000
XS_NOINLINE void xs_shared_release(xs_hdr *h);
static inline void *xs_retain_(void *p) {
#ifdef XS_HEAP_CHECK
    if (p && ((xs_hdr *)p)->rc == XS_FREED_RC) XS_HEAP_TRAP("retain of an object that was freed");
#endif
    if (p) {
        xs_hdr *h = p;
        if (__builtin_expect(h->rc >= XS_SHARED_RC, 0)) __atomic_add_fetch(&h->rc, 1, __ATOMIC_RELAXED);
        else h->rc++;
    }
    return p;
}

static void xs_gc_suspect(void *p);
static inline void xs_release_(void *p) {
    if (!p) return;
    xs_hdr *h = p;
#ifdef XS_HEAP_CHECK
    if (h->rc == XS_FREED_RC) XS_HEAP_TRAP("release of an object that was freed");
#endif
    if (__builtin_expect(h->rc >= XS_SHARED_RC, 0)) { xs_shared_release(h); return; }
    if (--h->rc == 0) xs_type_of(h)->free(p);
    else if (__builtin_expect((h->cls & (XS_CYC | XS_BUF)) == XS_CYC, 0)) xs_gc_suspect(p); /* may now hang on a cycle only */
}

/* the last holder of a shared object frees it: only strings of malloc's own are shared (xs_str_share), which hold
   nothing and which no thread counts among its allocations */
XS_NOINLINE void xs_shared_release(xs_hdr *h) {
    if (__atomic_sub_fetch(&h->rc, 1, __ATOMIC_ACQ_REL) == XS_SHARED_RC) free(h);
}
/* a release whose type the compiler knows (a field of a class without subclasses); only strings are ever
   shared, so only a string's release looks for a shared count (the test of the free function folds away): the free function is
   called directly, not through the type, so freeing a tree is a direct recursion the C compiler can work on */
#ifdef XS_HEAP_CHECK
#define XS_HEAP_CHECK_REL(h_) if ((h_)->rc == XS_FREED_RC) XS_HEAP_TRAP("release of an object that was freed");
#else
#define XS_HEAP_CHECK_REL(h_)
#endif
static void xs_str_free(void *p);
#define xs_release_as(x, free_fn) do { xs_hdr *h_ = (xs_hdr *)(x); \
    if (h_) { XS_HEAP_CHECK_REL(h_) if ((void *)(free_fn) == (void *)xs_str_free && __builtin_expect(h_->rc >= XS_SHARED_RC, 0)) xs_shared_release(h_); else if (--h_->rc == 0) free_fn(h_); \
              else if (__builtin_expect((h_->cls & (XS_CYC | XS_BUF)) == XS_CYC, 0)) xs_gc_suspect(h_); } } while (0)
/* the argument appears once in the expansion: a chain of calls nests these macros, and a copy for __typeof__ doubled
   the text at every level (a 20-call query builder chain became ~1M copies and took clang past 10 GB, 11/11) */
#define xs_retain(x) ({ __auto_type xs_rtv_ = (x); (__typeof__(xs_rtv_))xs_retain_((void *)xs_rtv_); })
#define xs_release(x) xs_release_((void *)(x))
/* node:async_hooks: read and replace the current context (xs_als_cur, above) */
XS_NOINLINE void *xs_als_current(void) { return xs_als_cur ? xs_retain_(xs_als_cur) : NULL; }
XS_NOINLINE void xs_als_set(void *frame) {
    void *old = xs_als_cur;
    xs_als_cur = frame ? xs_retain_(frame) : NULL;
    if (old) xs_release_(old);
}

/* Stores an owned reference into a slot, releasing the previous value. */
static inline void *xs_assign_(void **slot, void *val) {
    void *old = *slot;
    *slot = val;
    xs_release_(old);
    return val;
}
#define xs_assign(slot, val) ((__typeof__(slot))xs_assign_((void **)&(slot), (void *)(val)))
/* the same when the old value's free function is known (xs_release_as) */
#define xs_assign_as(slot, val, free_fn) ({ __typeof__(slot) av_ = (val); __typeof__(slot) *as_ = &(slot); \
    void *ao_ = (void *)*as_; *as_ = av_; xs_release_as(ao_, free_fn); av_; })

static XS_TLS void **xs_ar_pool;
static XS_TLS size_t xs_ar_len, xs_ar_cap;

static inline size_t xs_ar_mark(void) { return xs_ar_len; }

static void *xs_ar_(void *p) {
    if (xs_ar_len == xs_ar_cap) {
        xs_ar_cap = xs_ar_cap ? xs_ar_cap * 2 : 64;
        xs_ar_pool = realloc(xs_ar_pool, xs_ar_cap * sizeof(void *));
        if (!xs_ar_pool) xs_panic("out of memory");
    }
    xs_ar_pool[xs_ar_len++] = p;
    return p;
}
#define xs_ar(x) ({ __auto_type xs_arv_ = (x); (__typeof__(xs_arv_))xs_ar_((void *)xs_arv_); }) /* (once: see xs_retain) */

static inline void xs_ar_drain(size_t mark) {
    while (xs_ar_len > mark) xs_release_(xs_ar_pool[--xs_ar_len]);
}

/* An array length is below 2^40 (arrays are created under that limit; growing past it would take terabytes):
   said to the compiler, an index under a length times a small constant cannot overflow, and the checks of
   `a[i] = i * 31 + 7` in a counted loop go away (the loop then vectorizes). */
static inline __attribute__((always_inline)) int64_t xs_len_bound(int64_t n) {
#if defined(__clang__)
    __builtin_assume(n >= 0 && n <= ((int64_t)1 << 40));
#else
    if (n < 0 || n > ((int64_t)1 << 40)) __builtin_unreachable();
#endif
    return n;
}

#if defined(__linux__)
#include <sys/mman.h>
#endif
/* Linux: the 2 MB-aligned inside of a block of 64 MB or more is offered huge pages before it is touched
   (MADV_HUGEPAGE, honoured when THP is "madvise" or "always"), as the shared blocks are (xs_shblk_new): the
   first touches fault 2 MB at a time. The block stays malloc's, freed as any other. Typed arrays (revcomp.ts
   reads 254 MB of stdin into one; Graviton2: the reads' sys time halves), the buffer of readFileSync(0) and
   strings of that size (knucleotide.ts: 250 MB read, a 125 MB slice; Xeon: 337 -> see historia). XISTO_NO_U8_HUGE=1 turns it off. */
static inline void xs_big_hint(void *data, int64_t n) {
#if defined(__linux__) && defined(MADV_HUGEPAGE)
    if (n < ((int64_t)64 << 20)) return;
    static int off = -1;
    if (off < 0) off = getenv("XISTO_NO_U8_HUGE") != NULL;
    if (off) return;
    uintptr_t lo = ((uintptr_t)data + ((uintptr_t)2 << 20) - 1) & ~(((uintptr_t)2 << 20) - 1);
    uintptr_t hi = ((uintptr_t)data + (uintptr_t)n) & ~(((uintptr_t)2 << 20) - 1);
    if (hi > lo) madvise((void *)lo, hi - lo, MADV_HUGEPAGE);
#else
    (void)data, (void)n;
#endif
}
#define xs_u8_huge_hint xs_big_hint
/* the first touch of fresh pages is cheap here (huge pages offered and honoured: THP "madvise" or "always"), so a
   reader may go on into fresh memory instead of reusing the front (xisto:io's freshPagesCheap; macOS: no) */
static bool xs_fresh_pages_cheap(void) {
#if defined(__linux__) && defined(MADV_HUGEPAGE)
    static int v = -1;
    if (v < 0) {
        v = 0;
        if (!getenv("XISTO_NO_U8_HUGE")) {
            FILE *f = fopen("/sys/kernel/mm/transparent_hugepage/enabled", "r");
            char t[128] = {0};
            if (f) { if (fgets(t, sizeof t, f)) v = strstr(t, "[always]") || strstr(t, "[madvise]"); fclose(f); }
        }
    }
    return v > 0;
#else
    return false;
#endif
}

/* ---- strings: immutable, UTF-8 ---- */

typedef struct xs_str {
    xs_hdr hdr;
    int64_t len;   /* bytes */
    int32_t ulen;  /* code points, -1 until first asked (or when more than 2^31) */
    char data[];   /* always NUL-terminated; allocated from offsetof(xs_str, data): no padding after ulen */
} xs_str;

static void xs_str_free(void *p) { xs_obj_free(p); }
/* A string another thread is sent (postMessage) goes as itself, not as a copy, when it is big (64 KB or more) and a
   block of malloc's own: strings never change, so the threads can read the same bytes. From then on its count is
   shared (XS_SHARED_RC + the holders, changed atomically by both threads, xs_retain_/xs_release_) and no thread
   counts it among its allocations; the last release frees it. Below 64 KB, from a slab, a region or a frame,
   or a literal: false, and the caller copies as before. The thread calling this is the only one that holds s. */
static bool xs_str_share(xs_str *s) {
    if (s->len < ((int64_t)64 << 10) || s->hdr.rc < 1) return false;
    if (s->hdr.rc >= XS_SHARED_RC) { __atomic_add_fetch(&s->hdr.rc, 1, __ATOMIC_RELAXED); return true; } /* (the message's own) */
    if (s->hdr.rc >= XS_IMMORTAL / 2 || (s->hdr.cls & (XS_CLS_MASK | XS_STACK | XS_RGN | XS_CYC | XS_BUF | XS_DEAD))) return false;
    if (xs_type_of(s)->free != xs_str_free || getenv("XISTO_NO_STR_SHARE")) return false;
    xs_live--; /* (no thread's allocation any more) */
    __atomic_store_n(&s->hdr.rc, XS_SHARED_RC + s->hdr.rc + 1, __ATOMIC_RELEASE); /* this thread's holds, and the message's */
    return true;
}
static void xs_print_str(xs_str *s);
static void xs_str_print_(void *p) { xs_print_str(p); }

static const xs_type xs_str_type XS_TYPE_SEC = {xs_str_free, xs_str_print_};
/* a string from a buffer built with offsetof(xs_str, data) bytes reserved in front and room for the NUL:
   the buffer becomes the string, nothing is copied (replace() over 22 MB copied it twice) */
static xs_str *xs_str_adopt(char *buf, size_t len) {
    xs_str *s = (xs_str *)buf;
    if (offsetof(xs_str, data) + len + 1 <= XS_SMALL_MAX) { /* small: the allocator's own block */
        xs_str *r = xs_obj_alloc_raw(offsetof(xs_str, data) + len + 1, &xs_str_type);
        memcpy(r->data, s->data, len);
        r->data[len] = 0;
        r->len = (int64_t)len;
        r->ulen = -1;
        free(buf);
        return r;
    }
    s->hdr.rc = 1;
    s->hdr.cls = 0;
    xs_set_type(&s->hdr, &xs_str_type);
    s->len = (int64_t)len;
    s->ulen = -1;
    s->data[len] = 0;
    xs_live++;
    xs_allocs++;
    return s;
}
/* inlined where a string is made in a hot path (short templates, xs_cat_n): objects.ts 9.3 -> 7.5 ns per
   iteration; everywhere else the out-of-line xs_str_alloc keeps the code small */
static inline __attribute__((always_inline)) xs_str *xs_str_alloc_i(int64_t len) {
    xs_str *s = xs_obj_alloc_raw(offsetof(xs_str, data) + (size_t)len + 1, &xs_str_type);
    s->len = len;
    s->ulen = -1;
    return s;
}
static xs_str *xs_str_alloc(int64_t len) {
    xs_str *s = xs_str_alloc_i(len);
    xs_big_hint(s->data, len); /* (64 MB or more: huge pages before the first touch) */
    return s;
}

static char *xs_strndup_(const char *s, size_t n) {
    char *d = malloc(n + 1);
    if (!d) xs_panic("out of memory");
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

static xs_str *xs_str_new(const char *bytes, size_t len) {
    xs_str *s = xs_str_alloc((int64_t)len);
    memcpy(s->data, bytes, len);
    s->data[len] = 0;
    return s;
}

/* Literals live for the whole program and are never counted. */
static xs_str *xs_str_immortal(const char *bytes, size_t len) {
    xs_str *s = malloc(offsetof(xs_str, data) + len + 1);
    if (!s) xs_panic("out of memory");
    s->hdr.rc = XS_IMMORTAL;
    s->hdr.cls = 0;
    static const xs_type immortal_str XS_TYPE_SEC = {xs_str_free, xs_str_print_};
    xs_set_type(&s->hdr, &immortal_str);
    s->len = (int64_t)len;
    s->ulen = -1;
    if (len <= INT32_MAX) { /* literals: their length in UTF-16 units counted once, here (templates add them up) */
        int32_t u = 0;
        for (size_t i = 0; i < len; i++) u += ((unsigned char)bytes[i] & 0xC0) != 0x80;
        s->ulen = u;
    }
    memcpy(s->data, bytes, len);
    s->data[len] = 0;
    return s;
}

/* string.length counts Unicode code points (JS counts UTF-16 units; they agree outside emoji and other astral characters). */
/* a query parameter for node:sqlite and pg: kind 0 null, 1 int, 2 number, 3 string, 4 boolean */
typedef struct { int kind; int64_t i; double d; xs_str *s; } xs_sqarg;
/* truthiness of a string (xisto allows it for strings and T | null only): not empty */
static inline bool xs_str_truthy(xs_str *s) { return s && s->len > 0; }
static bool xs_str_eq(xs_str *a, xs_str *b);
static inline bool xs_str_eq_null(xs_str *a, xs_str *b) { return a && b && xs_str_eq(a, b); }
/* how many bytes of d[0, n) from the start are ASCII: eight words at a time, so an ASCII text of 125 MB is
   checked at memory speed instead of byte by byte */
static int64_t xs_ascii_prefix(const unsigned char *d, int64_t n) {
    int64_t i = 0;
    while (i + 64 <= n) {
        uint64_t acc = 0;
        for (int k = 0; k < 8; k++) { uint64_t w; memcpy(&w, d + i + 8 * k, 8); acc |= w; }
        if (acc & 0x8080808080808080ULL) break;
        i += 64;
    }
    while (i < n && d[i] < 0x80) i++;
    return i;
}
static bool xs_ulen_par(const unsigned char *d, int64_t n, int64_t *count);
static bool xs_copy_par(unsigned char *dst, const unsigned char *src, int64_t n);
static int64_t xs_str_len(xs_str *s) {
    if (s->ulen < 0) {
        int64_t n;
        if (!xs_ulen_par((const unsigned char *)s->data, s->len, &n)) { /* (a long string: counted over the threads) */
            n = xs_ascii_prefix((const unsigned char *)s->data, s->len);
            for (int64_t i = n; i < s->len; i++)
                if ((s->data[i] & 0xC0) != 0x80) n++;
        }
        if (n > INT32_MAX) return n; /* too long to cache in the header: counted each time */
        s->ulen = (int32_t)n;
    }
    return s->ulen;
}

/* Byte offset of the code point at index i (clamped to the end). */
static int64_t xs_str_byte_at(xs_str *s, int64_t i) {
    if (xs_str_len(s) == s->len) return i < s->len ? i : s->len; /* ASCII */
    int64_t n = 0;
    for (int64_t b = 0; b < s->len; b++) {
        if ((s->data[b] & 0xC0) != 0x80) {
            if (n == i) return b;
            n++;
        }
    }
    return s->len;
}

static int64_t xs_str_cp_at_byte(xs_str *s, int64_t b) {
    if (xs_str_len(s) == s->len) return b;
    int64_t n = 0;
    for (int64_t i = 0; i < b; i++)
        if ((s->data[i] & 0xC0) != 0x80) n++;
    return n;
}

/* the length of a + b when a's is known: b's is counted (b is copied anyway), so `s += t; s.length` does not count
   the whole of s again after every append */
static inline int32_t xs_str_ulen_sum(xs_str *a, xs_str *b) {
    if (a->ulen < 0 || a->len + b->len > INT32_MAX) return -1;
    return a->ulen + (int32_t)xs_str_len(b);
}
static xs_str *xs_str_concat(xs_str *a, xs_str *b) {
    xs_str *s = xs_str_alloc(a->len + b->len);
    memcpy(s->data, a->data, (size_t)a->len);
    memcpy(s->data + a->len, b->data, (size_t)b->len);
    s->data[s->len] = 0;
    s->ulen = xs_str_ulen_sum(a, b);
    return s;
}
/* the usable size of a malloc block (0: unknown, then a string is never grown in place) */
#if defined(__APPLE__)
#include <malloc/malloc.h>
#define xs_msize(p) malloc_size(p)
#elif defined(__linux__) || defined(_WIN32)
#include <malloc.h>
#if defined(_WIN32)
#define xs_msize(p) _msize(p)
#else
#define xs_msize(p) malloc_usable_size(p)
#endif
#else
#define xs_msize(p) ((size_t)0)
#endif
/* s += t (the emitter, when computing t calls no user code): a string held only by the variable (count 1, a malloc
   block of its own: not a literal, nor from a slab, a region or a frame) takes t at the end of its block, which grows
   by half when full; one that cannot (shared, small) is copied into such a block once it is past the small sizes.
   A buffer built from short pieces is then linear instead of a copy of the whole per piece (PolyBench's dump). With
   count 1 no other reference holds the old value, so changing it in place is not seen (strings never change). */
static xs_str *xs_str_append_to(xs_str **slot, xs_str *b) {
    xs_str *a = *slot;
    int64_t n = a->len + b->len;
    size_t need = offsetof(xs_str, data) + (size_t)n + 1;
    if (a != b && a->hdr.rc == 1 && !(a->hdr.cls & (XS_CLS_MASK | XS_STACK | XS_RGN | XS_CYC | XS_BUF | XS_DEAD)) &&
        xs_type_of(a)->free == xs_str_free && need > XS_SMALL_MAX) {
        size_t cap = xs_msize(a);
        if (cap) {
            if (need > cap) {
                xs_str *r = realloc(a, need + need / 2);
                if (!r) xs_panic("out of memory");
                a = *slot = r;
            }
            a->ulen = xs_str_ulen_sum(a, b);
            memcpy(a->data + a->len, b->data, (size_t)b->len);
            a->len = n;
            a->data[n] = 0;
            return a;
        }
    }
    xs_str *s;
    if (need > XS_SMALL_MAX && a != b) { /* room for the next appends */
        s = xs_obj_alloc_raw(need + need / 2, &xs_str_type);
        s->len = n;
        memcpy(s->data, a->data, (size_t)a->len);
        memcpy(s->data + a->len, b->data, (size_t)b->len);
        s->data[n] = 0;
        s->ulen = xs_str_ulen_sum(a, b);
    } else s = xs_str_concat(a, b);
    xs_assign(*slot, s);
    return s;
}

static inline bool xs_str_eq(xs_str *a, xs_str *b) {
    return a->len == b->len && memcmp(a->data, b->data, (size_t)a->len) == 0;
}

static bool xs_str_starts_with(xs_str *s, xs_str *p) {
    return p->len <= s->len && memcmp(s->data, p->data, (size_t)p->len) == 0;
}

static bool xs_str_ends_with(xs_str *s, xs_str *p) {
    return p->len <= s->len && memcmp(s->data + s->len - p->len, p->data, (size_t)p->len) == 0;
}

/* Substring search. memchr (vectorized in libc) finds candidates for the first byte, memcmp confirms.
   macOS memmem is a plain byte loop and was 60% of a log-filtering stream; glibc's is fast too, but this
   is never slower. When candidates are dense (first byte everywhere), fall back to memmem so the
   worst case stays what libc gives. UTF-8 is self-synchronizing, so a byte match is a character match. */
static const char *xs_find(const char *h, size_t hn, const char *n, size_t nn) {
    if (nn == 0) return h;
    if (nn > hn) return NULL;
    const char *p = h, *last = h + (hn - nn); /* last possible start */
    size_t misses = 0;
    for (;;) {
        p = memchr(p, (unsigned char)n[0], (size_t)(last - p) + 1);
        if (!p) return NULL;
        if (memcmp(p + 1, n + 1, nn - 1) == 0) return p;
        if (p == last) return NULL;
        p++;
        if (++misses > 32 && (size_t)(p - h) < misses * 16) return memmem(p, (size_t)(h + hn - p), n, nn);
    }
}

static bool xs_find_par(const char *h, size_t hn, const char *nd, size_t nn, const char **hit);
static int64_t xs_str_index_of(xs_str *s, xs_str *p, int64_t from) {
    int64_t n = xs_str_len(s);
    if (from < 0) from = 0;
    if (from > n) from = n;
    if (p->len == 0) return from;
    int64_t start = xs_str_byte_at(s, from);
    if (p->len > s->len - start) return -1;
    const char *hit;
    if (!xs_find_par(s->data + start, (size_t)(s->len - start), p->data, (size_t)p->len, &hit)) /* (a long text: searched over the threads) */
        hit = xs_find(s->data + start, (size_t)(s->len - start), p->data, (size_t)p->len);
    if (!hit) return -1;
    return xs_str_cp_at_byte(s, (int64_t)(hit - s->data));
}

static bool xs_str_includes(xs_str *s, xs_str *p) { return xs_find(s->data, (size_t)s->len, p->data, (size_t)p->len) != NULL; }

/* JS slice semantics on code points; XS_SLICE_END from the compiler means "to the end". */
#define XS_SLICE_END INT64_MIN
static xs_str *xs_str_slice(xs_str *s, int64_t start, int64_t end) {
    int64_t n = xs_str_len(s);
    if (end == XS_SLICE_END) end = n;
    if (start < 0) start += n;
    if (end < 0) end += n;
    if (start < 0) start = 0;
    if (end > n) end = n;
    if (start >= end) return xs_str_new("", 0);
    int64_t a = xs_str_byte_at(s, start), b = xs_str_byte_at(s, end);
    xs_str *r = xs_str_new(s->data + a, (size_t)(b - a));
    if (s->ulen == s->len) r->ulen = (int32_t)(b - a); /* a piece of an ASCII string: its length is known */
    return r;
}

/* JS substring: both ends clamped to [0, length] (no counting from the end), swapped when start > end */
static xs_str *xs_str_substring(xs_str *s, int64_t start, int64_t end) {
    int64_t n = xs_str_len(s);
    if (end == XS_SLICE_END || end > n) end = n;
    if (end < 0) end = 0;
    if (start < 0) start = 0;
    if (start > n) start = n;
    if (start > end) { int64_t t = start; start = end; end = t; }
    return xs_str_slice(s, start, end);
}

static xs_str *xs_str_trim(xs_str *s) {
    int64_t a = 0, b = s->len;
    while (a < b && isspace((unsigned char)s->data[a])) a++;
    while (b > a && isspace((unsigned char)s->data[b - 1])) b--;
    return xs_str_new(s->data + a, (size_t)(b - a));
}

/* case mapping by code point, as JavaScript's toUpperCase / toLowerCase for the Latin-1 and Latin Extended-A
   blocks, Greek and Cyrillic (the scripts of the languages the programs here meet); other code points are
   left as they are. The mappings that change length (ß -> SS, İ -> i̇, ŉ -> ʼN) and Greek's final sigma are in
   xs_str_case. */
static uint32_t xs_cp_upper(uint32_t c) {
    if (c < 0x80) return c >= 'a' && c <= 'z' ? c - 32 : c;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c == 0xB5) return 0x39C;
    if (c >= 0x100 && c <= 0x17F) {
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c & 1 ? c : c - 1;
        if (c == 0x131) return 'I';
        if (c == 0x17F) return 'S';
        if (c <= 0x12F || (c >= 0x132 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return c & 1 ? c - 1 : c;
        return c;
    }
    if (c >= 0x3B1 && c <= 0x3CB) return c == 0x3C2 ? 0x3A3 : c - 32;
    switch (c) {
    case 0x3AC: return 0x386; case 0x3AD: return 0x388; case 0x3AE: return 0x389; case 0x3AF: return 0x38A;
    case 0x3CC: return 0x38C; case 0x3CD: return 0x38E; case 0x3CE: return 0x38F;
    }
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}
static uint32_t xs_cp_lower(uint32_t c) {
    if (c < 0x80) return c >= 'A' && c <= 'Z' ? c + 32 : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x100 && c <= 0x17F) {
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c & 1 ? c + 1 : c;
        if (c == 0x178) return 0xFF;
        if (c <= 0x12F || (c >= 0x132 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return c & 1 ? c : c + 1;
        return c;
    }
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;
    switch (c) {
    case 0x386: return 0x3AC; case 0x388: return 0x3AD; case 0x389: return 0x3AE; case 0x38A: return 0x3AF;
    case 0x38C: return 0x3CC; case 0x38E: return 0x3CD; case 0x38F: return 0x3CE;
    }
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}
/* one code point at p (well-formed UTF-8, as every xs_str is); *n gets its length in bytes */
static uint32_t xs_u8_next(const unsigned char *p, int *n) {
    if (p[0] < 0x80) { *n = 1; return p[0]; }
    if (p[0] < 0xE0) { *n = 2; return (uint32_t)(p[0] & 0x1F) << 6 | (p[1] & 0x3F); }
    if (p[0] < 0xF0) { *n = 3; return (uint32_t)(p[0] & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 | (p[2] & 0x3F); }
    *n = 4;
    return (uint32_t)(p[0] & 0x07) << 18 | (uint32_t)(p[1] & 0x3F) << 12 | (uint32_t)(p[2] & 0x3F) << 6 | (p[3] & 0x3F);
}
static int xs_u8_put(char *o, uint32_t c) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) { o[0] = (char)(0xE0 | c >> 12); o[1] = (char)(0x80 | (c >> 6 & 0x3F)); o[2] = (char)(0x80 | (c & 0x3F)); return 3; }
    o[0] = (char)(0xF0 | c >> 18); o[1] = (char)(0x80 | (c >> 12 & 0x3F)); o[2] = (char)(0x80 | (c >> 6 & 0x3F)); o[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}
static bool xs_cp_cased(uint32_t c) { return xs_cp_upper(c) != c || xs_cp_lower(c) != c; }
XS_NOINLINE xs_str *xs_str_case(xs_str *s, bool up) {
    const unsigned char *d = (const unsigned char *)s->data;
    int64_t n = s->len, i = xs_ascii_prefix(d, n);
    if (i == n) { /* ASCII: byte by byte */
        xs_str *r = xs_str_alloc(n);
        for (int64_t k = 0; k < n; k++) {
            unsigned char c = d[k];
            r->data[k] = (char)(up ? (c >= 'a' && c <= 'z' ? c - 32 : c) : (c >= 'A' && c <= 'Z' ? c + 32 : c));
        }
        r->data[n] = 0;
        r->ulen = n <= INT32_MAX ? (int32_t)n : -1;
        return r;
    }
    char *buf = malloc((size_t)n * 2 + 8), *o = buf; /* no mapping here grows a code point by more than 2x */
    if (!buf) xs_panic("out of memory");
    uint32_t prev = 0;
    for (int64_t k = 0; k < n;) {
        int w;
        uint32_t c = xs_u8_next(d + k, &w);
        if (up && c == 0xDF) { *o++ = 'S'; *o++ = 'S'; }
        else if (up && c == 0x149) { o += xs_u8_put(o, 0x2BC); *o++ = 'N'; }
        else if (!up && c == 0x130) { *o++ = 'i'; o += xs_u8_put(o, 0x307); }
        else if (!up && c == 0x3A3) { /* final sigma: after a letter, and not before one */
            bool before = false;
            if (k + w < n) { int w2; before = xs_cp_cased(xs_u8_next(d + k + w, &w2)); }
            o += xs_u8_put(o, prev && xs_cp_cased(prev) && !before ? 0x3C2 : 0x3C3);
        } else o += xs_u8_put(o, up ? xs_cp_upper(c) : xs_cp_lower(c));
        prev = c;
        k += w;
    }
    xs_str *r = xs_str_new(buf, (size_t)(o - buf));
    free(buf);
    return r;
}
/* s.charCodeAt(i) / s.codePointAt(i): the code point at index i, NaN past the end */
static double xs_str_code_at(xs_str *s, int64_t i) {
    if (i < 0 || i >= xs_str_len(s)) return NAN;
    int w;
    return (double)xs_u8_next((const unsigned char *)s->data + xs_str_byte_at(s, i), &w);
}
/* String.fromCharCode(...) and String.fromCodePoint(...): fromCharCode takes each value modulo 2^16 (a lone
   surrogate becomes U+FFFD, as UTF-8 cannot hold it; a high-low pair in a row is the code point it encodes);
   fromCodePoint throws a RangeError for what is not a code point */
XS_NOINLINE xs_str *xs_str_from_codes(int n, const double *v, bool points) {
    char *buf = malloc((size_t)n * 4 + 1), *o = buf;
    if (!buf) xs_panic("out of memory");
    for (int i = 0; i < n; i++) {
        double d = v[i];
        uint32_t c;
        if (points) {
            if (!(d >= 0 && d <= 0x10FFFF) || d != floor(d)) {
                free(buf);
                char m[96];
                snprintf(m, sizeof m, "RangeError: Invalid code point %.17g", d);
                xs_panic(m);
            }
            c = (uint32_t)d;
        } else c = d == d && !isinf(d) ? (uint32_t)(int64_t)fmod(trunc(d), 65536.0) & 0xFFFF : 0;
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n) { /* a surrogate pair, given as two values */
            double e = v[i + 1];
            uint32_t lo = points ? (uint32_t)e : e == e && !isinf(e) ? (uint32_t)(int64_t)fmod(trunc(e), 65536.0) & 0xFFFF : 0;
            if (lo >= 0xDC00 && lo <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i++; }
        }
        if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
        o += xs_u8_put(o, c);
    }
    xs_str *r = xs_str_new(buf, (size_t)(o - buf));
    free(buf);
    return r;
}
XS_NOINLINE xs_str *xs_str_from_char_codes(int n, const double *v) { return xs_str_from_codes(n, v, false); }
XS_NOINLINE xs_str *xs_str_from_code_points(int n, const double *v) { return xs_str_from_codes(n, v, true); }
static xs_str *xs_str_upper(xs_str *s) { return xs_str_case(s, true); }
/* toUpperCase / toLowerCase of a temporary only the caller holds (rc 1): an ASCII text is mapped in its own buffer
   and comes back retained (the caller's release leaves it alive); otherwise as a new string */
XS_NOINLINE xs_str *xs_str_case_mv(xs_str *s, bool up) {
    if (s->hdr.rc != 1 || s->len <= XS_SMALL_MAX) return xs_str_case(s, up);
    unsigned char *d = (unsigned char *)s->data;
    int64_t n = s->len;
    if (xs_ascii_prefix(d, n) != n) return xs_str_case(s, up);
    if (up) { for (int64_t k = 0; k < n; k++) d[k] = (unsigned char)(d[k] >= 'a' && d[k] <= 'z' ? d[k] - 32 : d[k]); }
    else { for (int64_t k = 0; k < n; k++) d[k] = (unsigned char)(d[k] >= 'A' && d[k] <= 'Z' ? d[k] + 32 : d[k]); }
    s->ulen = n <= INT32_MAX ? (int32_t)n : -1;
    return xs_retain(s);
}
static xs_str *xs_str_upper_mv(xs_str *s) { return xs_str_case_mv(s, true); }
static xs_str *xs_str_lower_mv(xs_str *s) { return xs_str_case_mv(s, false); }
static xs_str *xs_str_lower(xs_str *s) { return xs_str_case(s, false); }

static inline xs_str *xs_retain_str(xs_str *s) { return xs_retain(s); }
XS_NOINLINE void xs_assert(bool cond, xs_str *msg) {
    if (cond) return;
    static XS_TLS char buf[512];
    snprintf(buf, sizeof buf, "assertion failed: %s", msg->data);
    xs_panic(buf);
}

static int64_t xs_str_last_index_of(xs_str *s, xs_str *p) {
    if (p->len > s->len) return -1;
    for (int64_t i = s->len - p->len; i >= 0; i--)
        if (memcmp(s->data + i, p->data, (size_t)p->len) == 0) return xs_str_cp_at_byte(s, i);
    return -1;
}
static xs_str *xs_str_trim_start(xs_str *s) {
    int64_t a = 0;
    while (a < s->len && isspace((unsigned char)s->data[a])) a++;
    return xs_str_new(s->data + a, (size_t)(s->len - a));
}
static xs_str *xs_str_trim_end(xs_str *s) {
    int64_t b = s->len;
    while (b > 0 && isspace((unsigned char)s->data[b - 1])) b--;
    return xs_str_new(s->data, (size_t)b);
}
static xs_str *xs_str_slice(xs_str *s, int64_t start, int64_t end);
static xs_str *xs_str_char_at(xs_str *s, int64_t i) {
    if (i < 0 || i >= xs_str_len(s)) return xs_str_new("", 0);
    return xs_str_slice(s, i, i + 1);
}
XS_NOINLINE xs_str *xs_str_repeat(xs_str *s, int64_t n) {
    if (n < 0) xs_panic("Invalid count value: repeat count must be non-negative");
    if (n && s->len > ((int64_t)1 << 32) / n) xs_panic("Invalid string length");
    xs_str *r = xs_str_alloc(s->len * n);
    for (int64_t i = 0; i < n; i++) memcpy(r->data + i * s->len, s->data, (size_t)s->len);
    r->data[r->len] = 0;
    return r;
}
XS_NOINLINE xs_str *xs_str_pad(xs_str *s, int64_t target, xs_str *fill, bool start) {
    int64_t len = xs_str_len(s);
    if (target <= len || fill->len == 0) return xs_retain(s);
    if (target > ((int64_t)1 << 30)) xs_panic("Invalid string length");
    int64_t need = target - len, flen = xs_str_len(fill);
    /* fill is cut on code points; byte length of the padding */
    int64_t full = need / flen, rest = need % flen;
    int64_t restb = xs_str_byte_at(fill, rest);
    int64_t padb = full * fill->len + restb;
    xs_str *r = xs_str_alloc(s->len + padb);
    char *p = r->data + (start ? 0 : s->len);
    for (int64_t i = 0; i < full; i++) { memcpy(p, fill->data, (size_t)fill->len); p += fill->len; }
    memcpy(p, fill->data, (size_t)restb);
    memcpy(r->data + (start ? padb : 0), s->data, (size_t)s->len);
    r->data[r->len] = 0;
    return r;
}
XS_NOINLINE xs_str *xs_str_pad_start(xs_str *s, int64_t n, xs_str *f) { return xs_str_pad(s, n, f, true); }
XS_NOINLINE xs_str *xs_str_pad_end(xs_str *s, int64_t n, xs_str *f) { return xs_str_pad(s, n, f, false); }
/* inplace: s is a temporary only the caller holds (rc 1, about to be released): a long text that does not grow is
   rewritten in its own buffer, and s comes back retained (the caller's release leaves it alive) */
XS_NOINLINE xs_str *xs_str_replace_impl2(xs_str *s, xs_str *a, xs_str *b, bool all, bool inplace) {
    if (a->len == 0) { /* JS: "" matches at 0 (replace) or between every code unit (replaceAll); support the first */
        if (all) xs_panic("replaceAll with an empty pattern is not supported");
        xs_str *r = xs_str_alloc(s->len + b->len);
        memcpy(r->data, b->data, (size_t)b->len); memcpy(r->data + b->len, s->data, (size_t)s->len); r->data[r->len] = 0;
        return r;
    }
    int64_t count = 0;
    const char *p = s->data, *end = s->data + s->len;
    if (all && b->len <= a->len && s->len > XS_SMALL_MAX) { /* a long text that does not grow: one pass into a buffer
        of its size (the result is at most as long; finding every match twice cost as much as the copy) */
        inplace = inplace && s->hdr.rc == 1 && b != s && a != s;
        xs_str *r = inplace ? s : xs_str_alloc(s->len);
        int32_t ulen0 = s->ulen;
        char *o = r->data;
        const char *hit;
        if (a->len == 1 && b->len == 0) { /* one character removed (the newlines of a text): eight bytes at a time,
            a word without it copied whole (one call per match cost as much as the copy) */
            const uint64_t ones = 0x0101010101010101ULL, c8 = ones * (unsigned char)a->data[0];
            while (p + 8 <= end) {
                uint64_t w;
                memcpy(&w, p, 8);
                uint64_t x = w ^ c8;
                if (!((x - ones) & ~x & 0x8080808080808080ULL)) { memcpy(o, &w, 8); o += 8; p += 8; continue; } /* (w: the bytes already read, so o may trail p in the same buffer) */
                for (int k = 0; k < 8; k++, p++) { if (*p != a->data[0]) *o++ = *p; else count++; }
            }
            for (; p < end; p++) { if (*p != a->data[0]) *o++ = *p; else count++; }
            if (count == 0) { if (r != s) xs_release(r); return xs_retain(s); }
            r->len = o - r->data;
            r->data[r->len] = 0;
            r->ulen = ulen0 >= 0 && ulen0 - count <= INT32_MAX ? (int32_t)(ulen0 - count) : -1;
            return r == s ? xs_retain(s) : r;
        }
        while ((hit = xs_find(p, (size_t)(end - p), a->data, (size_t)a->len))) {
            memmove(o, p, (size_t)(hit - p)); o += hit - p;
            memcpy(o, b->data, (size_t)b->len); o += b->len;
            p = hit + a->len;
            count++;
        }
        if (count == 0) { if (r != s) xs_release(r); return xs_retain(s); }
        memmove(o, p, (size_t)(end - p)); o += end - p;
        r->len = o - r->data;
        r->data[r->len] = 0;
        r->ulen = -1;
        if (ulen0 >= 0) {
            int64_t u = ulen0 + count * (xs_str_len(b) - xs_str_len(a));
            if (u >= 0 && u <= INT32_MAX) r->ulen = (int32_t)u;
        }
        return r == s ? xs_retain(s) : r;
    }
    while ((p = xs_find(p, (size_t)(end - p), a->data, (size_t)a->len))) { count++; p += a->len; if (!all) break; }
    if (count == 0) return xs_retain(s);
    xs_str *r = xs_str_alloc(s->len + count * (b->len - a->len));
    char *o = r->data;
    p = s->data;
    for (int64_t k = 0; k < count; k++) {
        const char *hit = xs_find(p, (size_t)(end - p), a->data, (size_t)a->len);
        memcpy(o, p, (size_t)(hit - p)); o += hit - p;
        memcpy(o, b->data, (size_t)b->len); o += b->len;
        p = hit + a->len;
    }
    memcpy(o, p, (size_t)(end - p));
    r->data[r->len] = 0;
    if (s->ulen >= 0) { /* the length in code points follows from the parts' (no recount of a long result) */
        int64_t u = s->ulen + count * (xs_str_len(b) - xs_str_len(a));
        if (u >= 0 && u <= INT32_MAX) r->ulen = (int32_t)u;
    }
    return r;
}
XS_NOINLINE xs_str *xs_str_replace_impl(xs_str *s, xs_str *a, xs_str *b, bool all) { return xs_str_replace_impl2(s, a, b, all, false); }
XS_NOINLINE xs_str *xs_str_replace_all_mv(xs_str *s, xs_str *a, xs_str *b) { return xs_str_replace_impl2(s, a, b, true, true); }

/* the fused chain's pass over [p, q): each byte case-mapped (mode 1 upper, 2 lower), the ones equal to ch dropped
   (compared before or after the mapping, as the calls are nested; ch 256: none); writes to o and gives the bytes
   written, or, with o NULL, only counts them (the parallel split's first phase) */
static int64_t xs_chain_pass(const unsigned char *p, const unsigned char *q, unsigned char *o, int mode, int ch, bool case_first) {
    unsigned char *o0 = o;
    int64_t kept = 0;
    unsigned char lo = mode == 1 ? 'a' : 'A', hi = mode == 1 ? 'z' : 'Z';
    int delta = mode == 1 ? -32 : 32;
    typedef unsigned char v16 __attribute__((vector_size(16), aligned(1)));
    v16 vlo = (v16){0} + lo, vhi = (v16){0} + hi, vd = (v16){0} + (unsigned char)delta, vc = (v16){0} + (unsigned char)(ch & 0xff);
    while (p + 16 <= q) {
        v16 x;
        memcpy(&x, p, 16);
        v16 m = mode ? (v16)((x >= vlo) & (x <= vhi)) : (v16){0};
        v16 y = x + (m & vd);
        v16 hit = ch < 256 ? (v16)((case_first ? y : x) == vc) : (v16){0};
        uint64_t h[2];
        memcpy(h, &hit, 16);
        if (!(h[0] | h[1])) {
            if (o) { memcpy(o, &y, 16); o += 16; } else kept += 16;
            p += 16;
            continue;
        }
        if (!o) { kept += 16 - (__builtin_popcountll(h[0]) + __builtin_popcountll(h[1])) / 8; p += 16; continue; }
        unsigned char yb[16], xb[16];
        memcpy(yb, &y, 16); memcpy(xb, &x, 16);
        for (int k = 0; k < 16; k++) if ((case_first ? yb[k] : xb[k]) != ch) *o++ = yb[k];
        p += 16;
    }
    for (; p < q; p++) {
        unsigned char x = *p, y = mode && x >= lo && x <= hi ? (unsigned char)(x + delta) : x;
        if ((case_first ? y : x) != ch) { if (o) *o++ = y; else kept++; }
    }
    return o ? (int64_t)(o - o0) : kept;
}
static bool xs_str_chain_par(const unsigned char *p, int64_t len, unsigned char *o, int mode, int ch, bool case_first, int64_t *written);

/* s.slice(a, b), then .replaceAll(one ASCII character, "") and/or .toUpperCase()/.toLowerCase() (the compiler's
   fused chain; pat NULL: no replaceAll; mode 0 none, 1 upper, 2 lower; case_first: the case call is inside the
   replaceAll). An ASCII s: one pass over the source range into a new string, sixteen bytes at a time (k-nucleotide's
   125 MB: three passes and a copy before, one now). Anything else: the calls one after another, as written. */
XS_NOINLINE xs_str *xs_str_chain_fused(xs_str *s, int64_t start, int64_t end, xs_str *pat, int mode, bool case_first) {
    int64_t n = xs_str_len(s);
    if (s->ulen != s->len || (pat && (pat->len != 1 || (unsigned char)pat->data[0] >= 0x80)) || getenv("XISTO_NO_CHAIN_FUSE")) {
        xs_str *t = xs_str_slice(s, start, end), *u;
        if (mode && case_first) { u = mode == 1 ? xs_str_upper(t) : xs_str_lower(t); xs_release_as(t, xs_str_free); t = u; }
        if (pat) { xs_str *e = xs_str_new("", 0); u = xs_str_replace_impl(t, pat, e, true); xs_release_as(e, xs_str_free); xs_release_as(t, xs_str_free); t = u; }
        if (mode && !case_first) { u = mode == 1 ? xs_str_upper(t) : xs_str_lower(t); xs_release_as(t, xs_str_free); t = u; }
        return t;
    }
    if (end == XS_SLICE_END) end = n;
    if (start < 0) start += n;
    if (end < 0) end += n;
    if (start < 0) start = 0;
    if (end > n) end = n;
    if (start >= end) return xs_str_new("", 0);
    const unsigned char *p = (const unsigned char *)s->data + start, *q = (const unsigned char *)s->data + end;
    xs_str *r = xs_str_alloc(end - start);
    unsigned char *o = (unsigned char *)r->data;
    int ch = pat ? (unsigned char)pat->data[0] : 256;
    int64_t written;
    if (!xs_str_chain_par(p, q - p, o, mode, ch, case_first, &written)) written = xs_chain_pass(p, q, o, mode, ch, case_first);
    o += written;
    r->len = (int64_t)(o - (unsigned char *)r->data);
    r->data[r->len] = 0;
    r->ulen = r->len <= INT32_MAX ? (int32_t)r->len : -1;
    return r;
}
XS_NOINLINE xs_str *xs_str_replace(xs_str *s, xs_str *a, xs_str *b) { return xs_str_replace_impl(s, a, b, false); }
XS_NOINLINE xs_str *xs_str_replace_all(xs_str *s, xs_str *a, xs_str *b) { return xs_str_replace_impl(s, a, b, true); }

#include <stdarg.h>
/* template literal: concatenates n borrowed strings into a new one */
XS_NOINLINE xs_str *xs_tpl(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int64_t total = 0;
    xs_str *parts[64];
    if (n > 64) xs_panic("template literal with more than 64 parts");
    for (int i = 0; i < n; i++) { parts[i] = va_arg(ap, xs_str *); total += parts[i]->len; }
    va_end(ap);
    xs_str *r = xs_str_alloc(total);
    char *p = r->data;
    for (int i = 0; i < n; i++) { memcpy(p, parts[i]->data, (size_t)parts[i]->len); p += parts[i]->len; }
    *p = 0;
    return r;
}

/* ---- arrays: fixed length, bounds-checked ---- */

#define XS_INLINE_ELEMS 4 /* small arrays keep their elements inside the object */
#define XS_ARRAY(T, name)                                                              \
    typedef struct name { xs_hdr hdr; size_t len; T *data; size_t cap; } name;        \
    static void name##_free(void *p) { name *a_ = p; if ((void *)a_->data != (void *)(a_ + 1)) free(a_->data); xs_obj_free(p); } \
    static void xs_print_##name(void *p);                                              \
    XS_NOINLINE name *name##_new(int64_t n, T fill) {                                       \
        if (n < 0 || n > ((int64_t)1 << 40)) xs_panic("invalid array length");         \
        /* up to XS_INLINE_ELEMS elements live inside the object: no second allocation */ \
        bool in_ = n <= XS_INLINE_ELEMS;                                               \
        name *a = in_ ? xs_obj_alloc(sizeof(name) + XS_INLINE_ELEMS * sizeof(T), name##_free, xs_print_##name) \
                      : xs_obj_alloc(sizeof(name), name##_free, xs_print_##name);      \
        a->len = (size_t)n;                                                            \
        a->cap = in_ ? XS_INLINE_ELEMS : (size_t)n;                                    \
        a->data = in_ ? (T *)(a + 1) : malloc(a->cap * sizeof(T));                     \
        if (!a->data) xs_panic("out of memory");                                       \
        for (size_t i = 0; i < a->len; i++) a->data[i] = fill;                         \
        return a;                                                                      \
    }                                                                                  \
    static name *name##_lit(size_t n, const T *items) {                                \
        name *a = name##_new((int64_t)n, 0);                                           \
        memcpy(a->data, items, n * sizeof(T));                                         \
        return a;                                                                      \
    }                                                                                  \
    XS_AI T *xs_at_##name##_s(name *a, int64_t i, const xs_site *s) {                \
        if ((uint64_t)i >= a->len) XS_FAIL("array index out of bounds", s);        \
        return &a->data[i];                                                            \
    }                                                                                  \
    static inline T *xs_at_##name(name *a, int64_t i) { return xs_at_##name##_s(a, i, NULL); } \
    static int64_t name##_push(name *a, T v) {                                         \
        if (a->len == a->cap) {                                                        \
            a->cap *= 2;                                                               \
            if ((void *)a->data == (void *)(a + 1)) {                                  \
                T *d_ = malloc(a->cap * sizeof(T));                                    \
                if (!d_) xs_panic("out of memory");                                    \
                memcpy(d_, a->data, a->len * sizeof(T));                               \
                a->data = d_;                                                          \
            } else {                                                                   \
                a->data = realloc(a->data, a->cap * sizeof(T));                        \
                if (!a->data) xs_panic("out of memory");                               \
            }                                                                          \
        }                                                                              \
        a->data[a->len++] = v;                                                         \
        return (int64_t)a->len;                                                        \
    }                                                                                  \
    XS_NOINLINE T name##_pop(name *a) {                                                     \
        if (a->len == 0) xs_panic("pop from an empty array");                          \
        return a->data[--a->len];                                                      \
    }                                                                                  \
    XS_NOINLINE int64_t name##_unshift(name *a, T v) {                                      \
        name##_push(a, v);                                                             \
        memmove(a->data + 1, a->data, (a->len - 1) * sizeof(T));                       \
        a->data[0] = v;                                                                \
        return (int64_t)a->len;                                                        \
    }                                                                                  \
    XS_NOINLINE T name##_shift(name *a) {                                                   \
        if (a->len == 0) xs_panic("shift from an empty array");                        \
        T v = a->data[0];                                                              \
        memmove(a->data, a->data + 1, (a->len - 1) * sizeof(T));                       \
        a->len--;                                                                      \
        return v;                                                                      \
    }                                                                                  \
    XS_NOINLINE void name##_splice1(name *a, int64_t i) {                                   \
        if ((uint64_t)i >= a->len) xs_panic("splice index out of bounds");             \
        memmove(a->data + i, a->data + i + 1, (a->len - (size_t)i - 1) * sizeof(T));   \
        a->len--;                                                                      \
    }

XS_ARRAY(double, xs_arr_num)
XS_ARRAY(int64_t, xs_arr_int)
XS_ARRAY(bool, xs_arr_bool)
#define xs_at_num xs_at_xs_arr_num
#define xs_at_int xs_at_xs_arr_int
#define xs_at_bool xs_at_xs_arr_bool
#define xs_at_num_s xs_at_xs_arr_num_s
#define xs_at_int_s xs_at_xs_arr_int_s
#define xs_at_bool_s xs_at_xs_arr_bool_s

/* Arrays of strings and objects: the array owns one reference per element. */
typedef struct xs_arr_ref { xs_hdr hdr; size_t len; void **data; size_t cap; void (*print_elem)(void *); } xs_arr_ref;
/* an array of region objects (the compiler's region.c) is itself in the region: made and grown there */
static const xs_type xs_arr_ref_rtype XS_TYPE_SEC;
static void *xs_region_bytes_cur(size_t n);
static void xs_arr_ref_free(void *p) {
    xs_arr_ref *a = p;
    for (size_t i = 0; i < a->len; i++) xs_release_(a->data[i]);
    if ((void *)a->data != (void *)(a + 1)) free(a->data);
    xs_obj_free(p);
}
static void xs_print_xs_arr_ref(void *p);
static xs_arr_ref *xs_arr_ref_new(size_t cap, void (*print_elem)(void *)) {
    bool in_ = cap <= XS_INLINE_ELEMS;
    xs_arr_ref *a = in_ ? xs_obj_alloc(sizeof *a + XS_INLINE_ELEMS * sizeof(void *), xs_arr_ref_free, xs_print_xs_arr_ref)
                        : xs_obj_alloc(sizeof *a, xs_arr_ref_free, xs_print_xs_arr_ref);
    a->cap = in_ ? XS_INLINE_ELEMS : cap;
    a->data = in_ ? (void **)(a + 1) : malloc(a->cap * sizeof(void *));
    if (!a->data) xs_panic("out of memory");
    a->print_elem = print_elem;
    return a;
}
XS_AI void **xs_at_ref_s(xs_arr_ref *a, int64_t i, const xs_site *s) {
    if ((uint64_t)i >= a->len) XS_FAIL("array index out of bounds", s);
    return &a->data[i];
}
static inline void **xs_at_ref(xs_arr_ref *a, int64_t i) { return xs_at_ref_s(a, i, NULL); }
/* takes an owned reference; the growth is out of line, the common case (room left) a store and an add */
XS_NOINLINE void xs_arr_ref_grow(xs_arr_ref *a) {
    {
        a->cap *= 2;
        if (xs_type_of(a) == &xs_arr_ref_rtype) { /* in a region: the old storage goes with it */
            void **d = xs_region_bytes_cur(a->cap * sizeof(void *));
            memcpy(d, a->data, a->len * sizeof(void *));
            a->data = d;
        } else if ((void *)a->data == (void *)(a + 1)) {
            void **d = malloc(a->cap * sizeof(void *));
            if (!d) xs_panic("out of memory");
            memcpy(d, a->data, a->len * sizeof(void *));
            a->data = d;
        } else {
            a->data = realloc(a->data, a->cap * sizeof(void *));
            if (!a->data) xs_panic("out of memory");
        }
    }
}
static inline __attribute__((always_inline)) int64_t xs_arr_ref_push(xs_arr_ref *a, void *v) {
    if (__builtin_expect(a->len == a->cap, 0)) xs_arr_ref_grow(a);
    a->data[a->len++] = v;
    return (int64_t)a->len;
}
/* returns an owned reference */
XS_NOINLINE void *xs_arr_ref_pop(xs_arr_ref *a) {
    if (a->len == 0) xs_panic("pop from an empty array");
    return a->data[--a->len];
}
/* takes an owned reference */
XS_NOINLINE int64_t xs_arr_ref_unshift(xs_arr_ref *a, void *v) {
    xs_arr_ref_push(a, v);
    memmove(a->data + 1, a->data, (a->len - 1) * sizeof(void *));
    a->data[0] = v;
    return (int64_t)a->len;
}
/* returns an owned reference */
XS_NOINLINE void *xs_arr_ref_shift(xs_arr_ref *a) {
    if (a->len == 0) xs_panic("shift from an empty array");
    void *v = a->data[0];
    memmove(a->data, a->data + 1, (a->len - 1) * sizeof(void *));
    a->len--;
    return v;
}
XS_NOINLINE void xs_arr_ref_splice1(xs_arr_ref *a, int64_t i) {
    if ((uint64_t)i >= a->len) xs_panic("splice index out of bounds");
    void *old = a->data[i];
    memmove(a->data + i, a->data + i + 1, (a->len - (size_t)i - 1) * sizeof(void *));
    a->len--;
    xs_release_(old);
}
static void xs_print_str_quoted(xs_str *s);
static void xs_print_str_quoted_(void *p) { xs_print_str_quoted(p); }

static xs_arr_ref *xs_str_split(xs_str *s, xs_str *sep) {
    xs_arr_ref *a = xs_arr_ref_new(8, xs_print_str_quoted_);
    if (sep->len == 0) { /* one element per code point */
        for (int64_t i = 0; i < s->len;) {
            int64_t j = i + 1;
            while (j < s->len && (s->data[j] & 0xC0) == 0x80) j++;
            xs_arr_ref_push(a, xs_str_new(s->data + i, (size_t)(j - i)));
            i = j;
        }
        return a;
    }
    const char *p = s->data, *end = s->data + s->len;
    for (;;) {
        const char *hit = xs_find(p, (size_t)(end - p), sep->data, (size_t)sep->len);
        if (!hit) { xs_arr_ref_push(a, xs_str_new(p, (size_t)(end - p))); break; }
        xs_arr_ref_push(a, xs_str_new(p, (size_t)(hit - p)));
        p = hit + sep->len;
    }
    return a;
}
static void xs_print_obj(void *p);
#include <stdarg.h>
static xs_arr_ref *xs_arr_ref_lit(void (*print_elem)(void *), int n, ...) { /* takes owned references */
    xs_arr_ref *a = xs_arr_ref_new((size_t)n, print_elem);
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) xs_arr_ref_push(a, va_arg(ap, void *));
    va_end(ap);
    return a;
}
XS_NOINLINE xs_arr_ref *xs_arr_ref_fill(xs_arr_ref *a, int64_t n, void *v) { /* v is borrowed */
    if (n < 0 || n > ((int64_t)1 << 40)) xs_panic("invalid array length");
    for (int64_t i = 0; i < n; i++) xs_arr_ref_push(a, xs_retain_(v));
    return a;
}
static inline double xs_to_number_id(double x) { return x; }

/* ---- Error ---- */

/* f___thrown: the object a throw carried (an object of a class that extends Error, or another value): instanceof and
   casts on a caught Error read it. f_cause: new Error(message, { cause }), as in JS (null when not given). */
typedef struct xs_Error { xs_hdr hdr; xs_str *f_message; xs_str *f_name; void *f___thrown; xs_trace *trace; xs_str *stack; void *f_cause; } xs_Error;
static void xs_Error_free(void *p) {
    xs_Error *e = p;
    xs_release(e->f_message); xs_release(e->f_name); xs_release(e->stack); xs_release(e->f___thrown); xs_release(e->f_cause);
    free(e->trace);
    xs_obj_free(p);
}
static void xs_print_any_quoted(void *p);
/* console.log(err): its first line; a cause as Node prints it below the stack ("{\n  [cause]: ...\n}") */
static void xs_Error_print(void *p) {
    xs_Error *e = p;
    xs_print_str(e->f_name); fputs(": ", XS_OUT); xs_print_str(e->f_message);
    if (e->f_cause) { fputs(" {\n  [cause]: ", XS_OUT); xs_print_any_quoted(e->f_cause); fputs("\n}", XS_OUT); }
}
__attribute__((noinline)) static void *xs_Error_new(void *msg) {
    xs_Error *e = xs_obj_alloc(sizeof *e, xs_Error_free, xs_Error_print);
    const char *nm = "Error";
    size_t nml = 5;
    if (msg) e->f_message = xs_retain((xs_str *)msg);
    else if (xs_panic_pending) { /* "URIError: URI malformed" from the runtime: an Error of that name with that message, as JavaScript's */
        const char *m = xs_panic_msg_pending;
        static const char *const names[] = {"TypeError", "RangeError", "URIError", "SyntaxError", "ReferenceError", "EvalError"};
        for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
            size_t n = strlen(names[i]);
            if (strncmp(m, names[i], n) == 0 && m[n] == ':' && m[n + 1] == ' ') { nm = names[i]; nml = n; m += n + 2; break; }
        }
        e->f_message = xs_str_new(m, strlen(m));
    }
    else e->f_message = xs_str_new("", 0);
    e->f_name = xs_str_new(nm, nml);
    /* like V8, the stack is captured where the Error is created; it is turned into text only if read */
    xs_trace t, *src = &t;
    if (xs_panic_pending) { src = &xs_panic_trace; xs_panic_pending = false; }
    else xs_trace_capture(&t, NULL);
    size_t sz = offsetof(xs_trace, ra) + (size_t)src->n * sizeof(uintptr_t);
    if ((e->trace = malloc(sz))) memcpy(e->trace, src, sz);
    return e;
}
/* a method an object literal standing for a class does not give, called: TypeError "<name> is not a function" */
XS_NOINLINE void *xs_type_error_not_fn(xs_str *name) {
    xs_Error *e = xs_Error_new(NULL);
    xs_release(e->f_name);
    e->f_name = xs_str_new("TypeError", 9);
    xs_release(e->f_message);
    size_t n = (size_t)name->len;
    char *b = malloc(n + 20);
    memcpy(b, name->data, n);
    memcpy(b + n, " is not a function", 18);
    e->f_message = xs_str_new(b, n + 18);
    free(b);
    return e;
}
/* new Error(message, { cause }): the cause is kept as err.cause (both arguments borrowed) */
XS_NOINLINE void *xs_Error_new_cause(void *msg, void *cause) {
    xs_Error *e = xs_Error_new(msg);
    e->f_cause = xs_retain(cause);
    return e;
}
/* an Error from a C message (native packages): the Error is the message's only owner (xs_Error_new retains) */
XS_NOINLINE void *xs_Error_from(const char *msg) {
    xs_str *s = xs_str_new(msg, strlen(msg));
    void *e = xs_Error_new(s);
    xs_release(s);
    return e;
}
/* throw obj (not an Error): an Error named after the class, with the object as its cause */
XS_NOINLINE void *xs_Error_wrap(xs_str *name, void *cause) {
    xs_Error *e = xs_Error_new(name);
    e->f___thrown = xs_retain(cause); /* the argument is borrowed */
    return e;
}
/* throw v, v unknown: a caught Error (what a catch gives) is thrown again as it is; another value travels as the
   cause of an Error, as throw obj does (the argument is borrowed) */
XS_NOINLINE void *xs_Error_of_unknown(void *v) {
    if (v && xs_type_of(v)->free == xs_Error_free) return xs_retain(v);
    xs_str *name = xs_str_new("Error", 5);
    void *e = xs_Error_wrap(name, v);
    xs_release(name);
    return e;
}
/* throw obj, obj of a class that extends Error: its first fields are message and name, laid out as xs_Error's */
XS_NOINLINE void *xs_Error_wrap_err(void *cause) {
    struct { xs_hdr hdr; xs_str *message; xs_str *name; } *o = cause;
    xs_Error *e = xs_Error_new(o->message);
    xs_release(e->f_name);
    e->f_name = xs_retain(o->name);
    e->f___thrown = xs_retain(cause); /* the argument is borrowed */
    return e;
}
/* String(err): "name: message", or the one that is not empty (an Error, or an object of a class that extends Error:
   both start with message and name) */
/* rows[i] as T | undefined: the element, or null past the end (JavaScript reads undefined there); borrowed */
static inline void *xs_arr_ref_at_or_null(xs_arr_ref *a, int64_t i) { return a && i >= 0 && (size_t)i < a->len ? a->data[i] : NULL; }
/* encodeURIComponent / encodeURI: the UTF-8 bytes of what is not kept, as %XX (uppercase), as JavaScript writes them */
XS_NOINLINE xs_str *xs_uri_encode(xs_str *s, const char *keep) {
    static const char hex[] = "0123456789ABCDEF";
    char *out = malloc((size_t)s->len * 3 + 1);
    size_t n = 0;
    for (int64_t i = 0; i < s->len; i++) {
        unsigned char ch = (unsigned char)s->data[i];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || (ch && strchr(keep, ch))) out[n++] = (char)ch;
        else { out[n++] = '%'; out[n++] = hex[ch >> 4]; out[n++] = hex[ch & 15]; }
    }
    xs_str *r = xs_str_new(out, n);
    free(out);
    return r;
}
XS_NOINLINE xs_str *xs_encode_uri_component(xs_str *s) { return xs_uri_encode(s, "-_.!~*'()"); }
XS_NOINLINE xs_str *xs_encode_uri(xs_str *s) { return xs_uri_encode(s, "-_.!~*'();/?:@&=+$,#"); }
/* decodeURIComponent / decodeURI: %XX sequences back to bytes (decodeURI keeps the escapes of ; / ? : @ & = + $ , #);
   a malformed one is URIError: URI malformed, as in JavaScript */
XS_NOINLINE xs_str *xs_uri_decode(xs_str *s, const char *reserved) {
    char *out = malloc((size_t)s->len + 1);
    size_t n = 0;
    for (int64_t i = 0; i < s->len; i++) {
        char ch = s->data[i];
        if (ch != '%') { out[n++] = ch; continue; }
        int v = -1;
        if (i + 2 < s->len) {
            int h = s->data[i + 1], l = s->data[i + 2];
            int hv = isdigit(h) ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
            int lv = isdigit(l) ? l - '0' : (l >= 'a' && l <= 'f') ? l - 'a' + 10 : (l >= 'A' && l <= 'F') ? l - 'A' + 10 : -1;
            if (hv >= 0 && lv >= 0) v = hv * 16 + lv;
        }
        if (v < 0) { free(out); xs_panic("URIError: URI malformed"); }
        if (v < 128 && v && strchr(reserved, v)) { out[n++] = '%'; out[n++] = s->data[i + 1]; out[n++] = s->data[i + 2]; }
        else out[n++] = (char)v;
        i += 2;
    }
    xs_str *r = xs_str_new(out, n);
    free(out);
    return r;
}
XS_NOINLINE xs_str *xs_decode_uri_component(xs_str *s) { return xs_uri_decode(s, ""); }
XS_NOINLINE xs_str *xs_decode_uri(xs_str *s) { return xs_uri_decode(s, ";/?:@&=+$,#"); }
XS_NOINLINE xs_str *xs_Error_to_string(void *v) {
    struct { xs_hdr hdr; xs_str *message; xs_str *name; } *o = v;
    if (!o->message->len) return xs_retain(o->name);
    if (!o->name->len) return xs_retain(o->message);
    xs_str *sep = xs_str_new(": ", 2), *a = xs_str_concat(o->name, sep), *r = xs_str_concat(a, o->message);
    xs_release(sep); xs_release(a);
    return r;
}
/* the object a caught Error was thrown as (an object of a class that extends Error), or NULL */
static inline void *xs_Error_thrown(void *v) {
    return v && xs_type_of(v)->free == xs_Error_free ? ((xs_Error *)v)->f___thrown : NULL;
}
static inline bool xs_is_a(void *v, void (*free_fn)(void *)) { return v && free_fn && xs_type_of(v)->free == free_fn; }
/* e.stack: "Error: message" and one "    at ..." line per frame, as in Node; borrowed from the Error */
static xs_str *xs_Error_stack(xs_Error *e) {
    if (!e->stack) {
        char buf[4096];
        int n = snprintf(buf, sizeof buf, "%s: %s\n", e->f_name->data, e->f_message->data);
        size_t len = n < 0 ? 0 : (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1;
        if (e->trace) len += xs_trace_format(e->trace, buf + len, sizeof buf - len);
        if (len && buf[len - 1] == '\n') len--;
        e->stack = xs_str_new(buf, len);
    }
    return e->stack;
}
/* takes an owned Error */
static void xs_throw(void *err) {
#ifdef XS_DEBUG
    xs_dbg_exception(((xs_Error *)err)->f_name->data, ((xs_Error *)err)->f_message->data, xs_try_depth > 0);
#endif
    if (xs_try_depth > 0) {
        xs_release(xs_thrown);
        xs_thrown = err;
        --xs_try_depth;
        xs_cln_unwind((size_t)(uintptr_t)xs_try_stack[2 * xs_try_depth + 1]);
        _longjmp(*xs_try_stack[2 * xs_try_depth], 1);
    }
    xs_Error *e = err;
    if (xs_recover) { /* inside an HTTP handler: becomes an error response */
        static XS_TLS char buf[512];
        snprintf(buf, sizeof buf, "%s", e->f_message->data);
        xs_recover_client = false; /* a thrown Error is the program's, whatever its message says */
        if (e->trace) memcpy(&xs_recover_trace, e->trace, offsetof(xs_trace, ra) + (size_t)e->trace->n * sizeof(uintptr_t));
        else xs_recover_trace.n = 0, xs_recover_trace.top = NULL;
        xs_release(err);
        xs_recover_msg = buf;
        xs_cln_unwind(xs_recover_cln);
        _longjmp(*xs_recover, 1);
    }
    fflush(stdout);
    fprintf(stderr, "uncaught Error: %s\n", e->f_message->data);
    if (e->trace) xs_trace_print(e->trace, stderr);
    if (!xs_never) exit(1);
}

/* x % y on numbers: fmod, but whole numbers below 2^53 (counters, an LCG like fasta's) take the integer
   remainder, exact for them and much cheaper than the libm call. The sign is the dividend's, -0 included */
static inline __attribute__((always_inline)) double xs_fmod(double x, double y) {
    if (__builtin_expect(fabs(x) < 9007199254740992.0 && fabs(y) < 9007199254740992.0 && y != 0 &&
                         x == (double)(int64_t)x && y == (double)(int64_t)y, 1)) {
        int64_t r = (int64_t)x % (int64_t)y;
        return r == 0 ? (signbit(x) ? -0.0 : 0.0) : (double)r;
    }
    return fmod(x, y);
}

/* ---- Map and Set: a Swiss table, string or int keys, values of any kind, insertion order ---- */

typedef union { double d; int64_t i; void *p; } xs_val;
#define XS_VNUM(x) ((xs_val){.d = (x)})
#define XS_VINT(x) ((xs_val){.i = (x)})
#define XS_VBOOL(x) ((xs_val){.i = (x) ? 1 : 0})
#define XS_VPTR(x) ((xs_val){.p = (x)})

/* The table is a Swiss table (Abseil, Rust's hashbrown): a control byte per slot, read 16 at a time, holds 7 bits
   of the key's hash (full), 0x80 (empty) or 0xFE (deleted); the slots hold only the key and the value (16 bytes).
   A lookup compares the control bytes of a group with SIMD and looks at a slot only where its 7 bits match, so a
   hit reads one slot and a miss usually none, and a probe seldom goes past the first group (load at most 7/8;
   7/16 after a rebuild); an int key's slot line is prefetched with the group. A map has string keys or
   int keys, never both: one field holds either. JS iteration is in insertion order: order[] holds the slots in
   that order. Deleted slots stay deleted until the table is rebuilt (reusing one would put a live entry under a
   stale position of order[]), so a lookup stops at the first group with an empty slot, and an insert takes that
   empty slot. */
typedef struct { union { xs_str *skey; int64_t ikey; }; xs_val val; } xs_map_entry;
typedef struct xs_map {
    xs_hdr hdr;
    uint8_t *ctrl;        /* cap + 16 control bytes: the last 16 repeat the first 16 (a group read never wraps) */
    xs_map_entry *slots;  /* cap */
    uint32_t *order;      /* slots in insertion order (JS iteration order); deleted ones are skipped */
    size_t norder, order_cap;
    size_t cap, count, used; /* used: live and deleted slots (what fills the table) */
    bool is_map, str_keys;
    bool record;  /* a Record<string, V> (an object used as a dictionary): printed as { a: 1 }, written to JSON as one */
    bool compact; /* a Map<int, int> whose keys and values all fit in 32 bits so far: 8-byte slots (key low, value
                     high), read only by the counting idiom (xs_map_add_i) and put; anything else widens it first */
    int val_kind; /* 0 num, 1 int, 2 bool, 3 heap pointer */
    bool ukeys;   /* a Set<unknown>: string keys made from the elements (SameValueZero), each entry's value the element */
    void (*print_key)(void *);
    void (*print_val)(void *);
    xs_str *lk;   /* the last string key hashed, held, and its hash: has(k), get(k), set(k, ...) in a row hash once */
    uint64_t lh;
    int64_t lik;  /* int keys: the last key looked up (where lidx points) */
    size_t lidx;  /* + 1: the slot of the last key (its entry, or the empty slot it would take); 0 after a grow,
                     delete or clear, which can move or empty slots */
    xs_map_entry *lhit; /* int keys: the entry of lik, NULL when it is not in the map or lidx is 0 */
} xs_map;

#define XS_MAP_EMPTY 0x80
#define XS_MAP_DEL 0xFE
#if defined(__aarch64__) && defined(__ARM_NEON)
#define XS_MAP_GW 8 /* slots in a group: 8 on NEON (no movemask: cmeq.8b + fmov + and, as hashbrown does there) */
#else
#define XS_MAP_GW 16 /* slots in a group */
#endif
#define XS_MAP_FULL(m, i) ((m)->ctrl[i] < 0x80)
/* a group's bytes equal to b, as a mask: bit (slot << XS_MAP_GSH) set for each; XS_MAP_GBIT gives the lowest slot */
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
typedef uint64_t xs_map_gmask;
#define XS_MAP_GSH 3 /* a byte per slot, its bit 7 set where the control byte matches */
static inline xs_map_gmask xs_map_gmatch(const uint8_t *g, uint8_t b) {
    uint8x8_t eq = vceq_u8(vld1_u8(g), vdup_n_u8(b));
    return vget_lane_u64(vreinterpret_u64_u8(eq), 0) & 0x8080808080808080ull;
}
#elif defined(__SSE2__)
#include <emmintrin.h>
typedef uint32_t xs_map_gmask;
#define XS_MAP_GSH 0
static inline xs_map_gmask xs_map_gmatch(const uint8_t *g, uint8_t b) {
    return (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)g), _mm_set1_epi8((char)b)));
}
#else
typedef uint32_t xs_map_gmask;
#define XS_MAP_GSH 0
static inline xs_map_gmask xs_map_gmatch(const uint8_t *g, uint8_t b) {
    uint32_t mm = 0;
    for (int j = 0; j < XS_MAP_GW; j++) mm |= (uint32_t)(g[j] == b) << j;
    return mm;
}
#endif
#define XS_MAP_GBIT(mm) ((size_t)__builtin_ctzll((uint64_t)(mm)) >> XS_MAP_GSH)
#define XS_MAP_H2(h) ((uint8_t)((h) >> 57)) /* the control byte of a full slot: the hash's top 7 bits */

static uint64_t xs_hash_bytes(const char *p, size_t n) { /* 8 bytes a step, then mixed (splitmix64's constants) */
    uint64_t h = 0x9E3779B97F4A7C15ull ^ (uint64_t)n;
    while (n >= 8) {
        uint64_t w;
        memcpy(&w, p, 8);
        h = (h ^ w) * 0xbf58476d1ce4e5b9ull;
        h ^= h >> 31;
        p += 8;
        n -= 8;
    }
    if (n) {
        uint64_t w = 0;
        memcpy(&w, p, n);
        h = (h ^ w) * 0x94d049bb133111ebull;
        h ^= h >> 29;
    }
    h ^= h >> 32;
    h *= 0xd6e8feb86659fd93ull;
    h ^= h >> 32;
    return h | 1;
}
/* (the top bits, the control byte, are the product's best; the low bits, the slot, get the high ones mixed in) */
static inline uint64_t xs_hash_int(int64_t v) {
    uint64_t h = (uint64_t)v * 11400714819323198485ull;
    return h ^ (h >> 29);
}

static void xs_map_free(void *p) {
    xs_map *m = p;
    if (m->str_keys || m->val_kind == 3)
        for (size_t i = 0; i < m->cap; i++) {
            if (!XS_MAP_FULL(m, i)) continue;
            if (m->str_keys) xs_release_(m->slots[i].skey);
            if (m->val_kind == 3) xs_release_(m->slots[i].val.p);
        }
    if (m->lk) xs_release_(m->lk);
    free(m->ctrl);
    free(m->slots);
    free(m->order);
    xs_obj_free(p);
}

/* iterates the live entries in insertion order; e points at each (key and value in place) */
static size_t xs_map_pf_min = 4096; /* slots: below, the table is in the L1/L2 and the prefetch is only an instruction */
static void xs_map_widen(struct xs_map *m);
#define XS_MAP_WIDE(m) ((m)->compact ? xs_map_widen(m) : (void)0)
#define XS_MAP_EACH(m, e) for (size_t oi_ = (XS_MAP_WIDE(m), 0); oi_ < (m)->norder; oi_++) if (XS_MAP_FULL(m, (m)->order[oi_]) && ((e = &(m)->slots[(m)->order[oi_]]), 1))

static void xs_print_num(double v);
static void xs_print_int(int64_t v);
static void xs_print_bool(bool b);
static void xs_print_num_v(void *p) { xs_print_num(*(double *)p); }
static void xs_print_int_v(void *p) { xs_print_int(*(int64_t *)p); }
static void xs_print_bool_v(void *p) { xs_print_bool(*(int64_t *)p != 0); }

/* Node style: Map(2) { 'a' => 1, 'b' => 2 }  Set(2) { 1, 2 }; a record: { a: 1, 'b-c': 2 } */
static void xs_print_key_ident(xs_str *k) { /* an object key as Node shows it: bare when it is an identifier, else quoted */
    bool ident = k->len > 0 && !(k->data[0] >= '0' && k->data[0] <= '9');
    for (int64_t i = 0; i < k->len && ident; i++) {
        char c = k->data[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || (unsigned char)c >= 0x80)) ident = false;
    }
    if (ident) fwrite(k->data, 1, (size_t)k->len, XS_OUT); else xs_print_str_quoted_(k);
}
static void xs_print_map(void *p) {
    xs_map *m = p;
    xs_insp fr;
    if (m->record) {
        if (!xs_insp_begin(&fr, p, "Object", m->count ? NULL : "{}")) return;
        xs_map_entry *e;
        XS_MAP_EACH(m, e) {
            xs_insp_entry(&fr);
            xs_print_key_ident(e->skey);
            fputs(": ", XS_OUT);
            if (m->val_kind == 3) { if (e->val.p) m->print_val(e->val.p); else fputs("null", XS_OUT); }
            else m->print_val(&e->val);
        }
        xs_insp_end(&fr, "{", "}", false, false, false);
        return;
    }
    if (!xs_insp_begin(&fr, p, m->is_map ? "Map" : "Set", m->count ? NULL : m->is_map ? "Map(0) {}" : "Set(0) {}")) return;
    size_t shown = 0;
    xs_map_entry *e;
    XS_MAP_EACH(m, e) {
        xs_insp_entry(&fr);
        if (shown++ == 100) { fprintf(XS_OUT, "... %zu more item%s", m->count - 100, m->count - 100 == 1 ? "" : "s"); break; }
        if (m->ukeys) { if (e->val.p) m->print_val(e->val.p); else fputs("null", XS_OUT); }
        else if (m->str_keys) m->print_key(e->skey); else m->print_key(&e->ikey);
        if (m->is_map) {
            fputs(" => ", XS_OUT);
            if (m->val_kind == 3) { if (e->val.p) m->print_val(e->val.p); else fputs("null", XS_OUT); }
            else m->print_val(&e->val);
        }
    }
    char open[48];
    snprintf(open, sizeof open, "%s(%zu) {", m->is_map ? "Map" : "Set", m->count);
    xs_insp_end(&fr, open, "}", false, false, m->count > 100);
}

/* an empty table of cap slots (a power of two, 16 or more) */
static void xs_map_alloc(xs_map *m, size_t cap) {
    if (cap > (size_t)UINT32_MAX) xs_panic("out of memory"); /* (order[] holds 32-bit slots) */
    m->cap = cap;
    m->ctrl = malloc(cap + XS_MAP_GW);
    m->slots = malloc(cap * (m->compact ? sizeof(uint64_t) : sizeof(xs_map_entry))); /* (only a full slot is ever read) */
    if (!m->ctrl || !m->slots) xs_panic("out of memory");
    memset(m->ctrl, XS_MAP_EMPTY, cap + XS_MAP_GW);
    m->used = 0;
}

static inline xs_map *xs_map_rec(xs_map *m) { m->record = true; return m; } /* (a Record<string, V>: see xs_map.record) */
static xs_map *xs_map_new(int is_map, int str_keys, int val_kind, void (*print_key)(void *), void (*print_val)(void *)) {
    xs_map *m = xs_obj_alloc(sizeof *m, xs_map_free, xs_print_map);
    m->is_map = is_map != 0;
    m->str_keys = str_keys != 0;
    m->val_kind = val_kind;
    m->print_key = print_key;
    m->print_val = print_val;
    m->lk = NULL;
    m->lidx = 0;
    m->lhit = NULL;
    static int no_compact = -1;
    if (no_compact < 0) {
        no_compact = getenv("XISTO_NO_MAP_COMPACT") != NULL;
        const char *pf = getenv("XISTO_MAP_PF_MIN"); /* measuring knob: the table size from which a probe prefetches its slot */
        if (pf) xs_map_pf_min = (size_t)strtoull(pf, NULL, 10);
    }
    /* compact tables pay where the cache does: on the Graviton2 (1 MB L2) k-nucleotide's k = 12 frame 3,0-4,2 -> 2,0 s;
       on the Xeon the extra checks cost 7-13% and the table already fits, so x86 keeps 16-byte slots
       (XISTO_MAP_COMPACT=1 forces them, XISTO_NO_MAP_COMPACT=1 turns them off) */
#if defined(__aarch64__)
    bool compact_default = true;
#else
    bool compact_default = false;
#endif
    static int force_compact = -1;
    if (force_compact < 0) force_compact = getenv("XISTO_MAP_COMPACT") != NULL;
    m->compact = m->is_map && !m->str_keys && val_kind == 1 && !no_compact && (compact_default || force_compact);
    xs_map_alloc(m, 16);
    return m;
}

static inline void xs_map_set_ctrl(xs_map *m, size_t i, uint8_t c) {
    m->ctrl[i] = c;
    if (i < XS_MAP_GW) m->ctrl[m->cap + i] = c; /* (the mirror) */
}

/* Looks an int key up and leaves the answer in the last-key cache: lik = k, lidx = its slot + 1 (its own, or
   the empty slot it would take), lhit = its entry or NULL. Groups are probed in triangular steps, which visit
   every group of a power-of-two table. */
static inline xs_map_entry *xs_map_probe_i(xs_map *m, int64_t k) {
    XS_MAP_WIDE(m);
    uint64_t h = xs_hash_int(k);
    size_t mask = m->cap - 1, pos = (size_t)h & mask, stride = 0;
    uint8_t h2 = XS_MAP_H2(h);
    const uint8_t *ctrl = m->ctrl;
    xs_map_entry *slots = m->slots;
    __builtin_prefetch(&slots[pos]); /* (a key mostly sits at pos or right after it: its line comes with the group's) */
    m->lik = k;
    for (;;) {
        const uint8_t *g = ctrl + pos;
        for (xs_map_gmask mm = xs_map_gmatch(g, h2); mm; mm &= mm - 1) {
            size_t i = (pos + XS_MAP_GBIT(mm)) & mask;
            if (__builtin_expect(slots[i].ikey == k, 1)) { m->lidx = i + 1; return m->lhit = &slots[i]; }
        }
        xs_map_gmask em = xs_map_gmatch(g, XS_MAP_EMPTY);
        if (__builtin_expect(em != 0, 1)) { m->lidx = ((pos + XS_MAP_GBIT(em)) & mask) + 1; return m->lhit = NULL; }
        stride += XS_MAP_GW;
        pos = (pos + stride) & mask;
    }
}
/* The slot of a string key: its own when it is in the map, else the empty slot it would take (the caller looks
   at its control byte) */
static size_t xs_map_find_slot_s(xs_map *m, xs_str *k, uint64_t h) {
    size_t mask = m->cap - 1, pos = (size_t)h & mask, stride = 0;
    uint8_t h2 = XS_MAP_H2(h);
    for (;;) {
        const uint8_t *g = m->ctrl + pos;
        for (xs_map_gmask mm = xs_map_gmatch(g, h2); mm; mm &= mm - 1) {
            size_t i = (pos + XS_MAP_GBIT(mm)) & mask;
            xs_str *sk = m->slots[i].skey;
            if (sk == k || xs_str_eq(sk, k)) return i;
        }
        xs_map_gmask em = xs_map_gmatch(g, XS_MAP_EMPTY);
        if (__builtin_expect(em != 0, 1)) return (pos + XS_MAP_GBIT(em)) & mask;
        stride += XS_MAP_GW;
        pos = (pos + stride) & mask;
    }
}
/* the empty slot a new key takes (a rebuild: no key to compare) */
static size_t xs_map_free_slot(xs_map *m, uint64_t h) {
    size_t mask = m->cap - 1, pos = (size_t)h & mask, stride = 0;
    for (;;) {
        xs_map_gmask em = xs_map_gmatch(m->ctrl + pos, XS_MAP_EMPTY);
        if (em) return (pos + XS_MAP_GBIT(em)) & mask;
        stride += XS_MAP_GW;
        pos = (pos + stride) & mask;
    }
}

static void xs_map_order_push(xs_map *m, size_t idx) {
    if (m->norder == m->order_cap) {
        m->order_cap = m->order_cap ? m->order_cap * 2 : 16;
        m->order = realloc(m->order, m->order_cap * sizeof(uint32_t));
        if (!m->order) xs_panic("out of memory");
    }
    m->order[m->norder++] = (uint32_t)idx;
}

/* rebuilt in insertion order, twice the size when more than 7/16 of it is live (else the same size: deleted
   slots cleaned); a string key is hashed again (the table keeps only 7 bits of it) */
static void xs_map_grow(xs_map *m) {
    m->lidx = 0;
    m->lhit = NULL;
    if (m->compact) { /* the same, 8-byte slots */
        uint8_t *oc = m->ctrl;
        uint64_t *os = (uint64_t *)m->slots;
        size_t cap = m->cap;
        if (m->count * 16 > cap * 7) cap *= 2;
        xs_map_alloc(m, cap);
        uint64_t *ns = (uint64_t *)m->slots;
        size_t j = 0;
        for (size_t oi = 0; oi < m->norder; oi++) {
            size_t s = m->order[oi];
            if (oc[s] >= 0x80) continue;
            uint64_t h = xs_hash_int((int64_t)(int32_t)(uint32_t)os[s]);
            size_t i = xs_map_free_slot(m, h);
            xs_map_set_ctrl(m, i, XS_MAP_H2(h));
            ns[i] = os[s];
            m->order[j++] = (uint32_t)i;
        }
        m->norder = m->used = j;
        free(oc);
        free(os);
        return;
    }
    uint8_t *old_ctrl = m->ctrl;
    xs_map_entry *old = m->slots;
    size_t cap = m->cap;
    if (m->count * 16 > cap * 7) cap *= 2;
    xs_map_alloc(m, cap);
    size_t j = 0;
    for (size_t oi = 0; oi < m->norder; oi++) { /* reinsert in insertion order; order[] is rewritten in place */
        size_t s = m->order[oi];
        if (old_ctrl[s] >= 0x80) continue;
        xs_map_entry *o = &old[s];
        uint64_t h = m->str_keys ? xs_hash_bytes(o->skey->data, (size_t)o->skey->len) : xs_hash_int(o->ikey);
        size_t i = xs_map_free_slot(m, h);
        xs_map_set_ctrl(m, i, XS_MAP_H2(h));
        m->slots[i] = *o;
        m->order[j++] = (uint32_t)i;
    }
    m->norder = m->used = j;
    free(old_ctrl);
    free(old);
}

static uint64_t xs_map_hash_s(xs_map *m, xs_str *k) {
    if (k == m->lk) return m->lh;
    uint64_t h = xs_hash_bytes(k->data, (size_t)k->len);
    if (!(k->hdr.cls & XS_STACK)) { /* (a string in a frame cannot be held past it) */
        if (m->lk) xs_release_(m->lk);
        m->lk = xs_retain(k);
        m->lh = h;
        m->lidx = 0;
    }
    return h;
}
/* int keys: has(k), get(k), set(k, ...) in a row probe once (the counting idiom: 125M of them per frame in
   k-nucleotide); lidx is cleared where the table changes shape (grow, delete, clear) */
static inline size_t xs_map_find_i(xs_map *m, int64_t k) {
    if (!(m->lidx && m->lik == k)) xs_map_probe_i(m, k);
    return m->lidx - 1;
}
/* the same for a string key, through the cached slot of the last key */
static size_t xs_map_find_s(xs_map *m, xs_str *k, uint64_t h) {
    if (k == m->lk && m->lidx) return m->lidx - 1;
    size_t i = xs_map_find_slot_s(m, k, h);
    if (k == m->lk) m->lidx = i + 1;
    return i;
}
/* value is owned when val_kind == 3 */
static size_t xs_map_cfind(xs_map *m, int32_t k, uint64_t h);
static xs_map *xs_map_put(xs_map *m, xs_str *skey, int64_t ikey, xs_val val) {
    if (m->compact) {
        if ((int32_t)ikey == ikey && (int32_t)val.i == val.i) {
            if ((m->used + 1) * 8 > m->cap * 7) xs_map_grow(m);
            uint64_t h = xs_hash_int(ikey);
            size_t i = xs_map_cfind(m, (int32_t)ikey, h);
            uint64_t *cs = (uint64_t *)m->slots;
            uint64_t w = (uint64_t)(uint32_t)(int32_t)ikey | ((uint64_t)(uint32_t)(int32_t)val.i << 32);
            if (XS_MAP_FULL(m, i)) { cs[i] = w; return m; }
            m->used++;
            xs_map_set_ctrl(m, i, XS_MAP_H2(h));
            xs_map_order_push(m, i);
            cs[i] = w;
            m->count++;
            return m;
        }
        xs_map_widen(m);
    }
    if ((m->used + 1) * 8 > m->cap * 7) xs_map_grow(m);
    uint64_t h = m->str_keys ? xs_map_hash_s(m, skey) : xs_hash_int(ikey);
    size_t i = m->str_keys ? xs_map_find_s(m, skey, h) : xs_map_find_i(m, ikey);
    xs_map_entry *e = &m->slots[i];
    if (XS_MAP_FULL(m, i)) {
        if (m->val_kind == 3) xs_release_(e->val.p);
        e->val = val;
        return m;
    }
    m->used++;
    xs_map_set_ctrl(m, i, XS_MAP_H2(h));
    xs_map_order_push(m, i);
    if (m->str_keys) e->skey = xs_retain(skey);
    else { e->ikey = ikey; m->lhit = e; } /* (the same field; lik and lidx are this key's already) */
    e->val = val;
    m->count++;
    return m;
}
static xs_map *xs_map_set_s(xs_map *m, xs_str *k, xs_val v) { return xs_map_put(m, k, 0, v); }
static inline __attribute__((always_inline)) xs_map *xs_map_set_i(xs_map *m, int64_t k, xs_val v) {
    if (m->lhit && m->lik == k && m->val_kind != 3) { /* the key just looked up, there, a value that is no
                                                          reference: in place */
        m->lhit->val = v;
        return m;
    }
    return xs_map_put(m, NULL, k, v);
}

static xs_map_entry *xs_map_lookup(xs_map *m, xs_str *skey, int64_t ikey) {
    XS_MAP_WIDE(m);
    if (!m->str_keys) return m->lidx && m->lik == ikey ? m->lhit : xs_map_probe_i(m, ikey);
    size_t i = xs_map_find_s(m, skey, xs_map_hash_s(m, skey));
    return XS_MAP_FULL(m, i) ? &m->slots[i] : NULL;
}
static bool xs_map_has_s(xs_map *m, xs_str *k) { return xs_map_lookup(m, k, 0) != NULL; }
/* int keys, inlined where they are used: the last key looked up answers at once (has, get, set of one key in a
   row: the counting idiom), else one probe; nothing asks whether the keys are strings. has() probes always:
   it is what starts such a row, and a branch on whether its key repeats the last one would be a coin toss
   where few keys alternate (k-nucleotide's frame 1) */
static inline __attribute__((always_inline)) xs_map_entry *xs_map_lookup_i(xs_map *m, int64_t k) {
    return m->lidx && m->lik == k ? m->lhit : xs_map_probe_i(m, k);
}
static inline __attribute__((always_inline)) bool xs_map_has_i(xs_map *m, int64_t k) { return xs_map_probe_i(m, k) != NULL; }
/* m.set(k, m.get(k) + d) / m.set(k, d) (emit.c emit_map_add): one probe that does not touch the last-key cache (its
   three stores, and the reloads of cap/ctrl/slots they force on the compiler: on the Neoverse N1 the loop had 61
   instructions per symbol against the Rust table's 28), the insert out of line so m stays in a register */
/* the slot of a key in a compact table: its own, or the empty one it would take */
static size_t xs_map_cfind(xs_map *m, int32_t k, uint64_t h) {
    size_t mask = m->cap - 1, pos = (size_t)h & mask, stride = 0;
    uint8_t h2 = XS_MAP_H2(h);
    const uint64_t *cs = (const uint64_t *)m->slots;
    for (;;) {
        const uint8_t *g = m->ctrl + pos;
        for (xs_map_gmask mm = xs_map_gmatch(g, h2); mm; mm &= mm - 1) {
            size_t i = (pos + XS_MAP_GBIT(mm)) & mask;
            if ((int32_t)(uint32_t)cs[i] == k) return i;
        }
        xs_map_gmask em = xs_map_gmatch(g, XS_MAP_EMPTY);
        if (em) return (pos + XS_MAP_GBIT(em)) & mask;
        stride += XS_MAP_GW;
        pos = (pos + stride) & mask;
    }
}
/* a compact table to 16-byte slots, in place (the same slots and order): before anything but the counting idiom */
static __attribute__((noinline)) void xs_map_widen(xs_map *m) {
    if (!m->compact) return;
    uint64_t *cs = (uint64_t *)m->slots;
    xs_map_entry *ws = malloc(m->cap * sizeof(xs_map_entry));
    if (!ws) xs_panic("out of memory");
    for (size_t i = 0; i < m->cap; i++)
        if (XS_MAP_FULL(m, i)) { ws[i].ikey = (int64_t)(int32_t)(uint32_t)cs[i]; ws[i].val.i = (int64_t)(int32_t)(uint32_t)(cs[i] >> 32); }
    free(cs);
    m->slots = ws;
    m->compact = false;
    m->lidx = 0;
    m->lhit = NULL;
}
/* the insert of the counting idiom, and every case the inline path leaves: a key or a sum past 32 bits widens */
static __attribute__((noinline)) void xs_map_add_slow_i(xs_map *m, int64_t k, int64_t d, const xs_site *s) {
    if (m->compact && (int32_t)k == k) {
        size_t i = xs_map_cfind(m, (int32_t)k, xs_hash_int(k));
        if (!XS_MAP_FULL(m, i)) { if ((int32_t)d == d) { xs_map_put(m, NULL, k, XS_VINT(d)); return; } }
    }
    XS_MAP_WIDE(m);
    xs_map_entry *e = xs_map_lookup(m, NULL, k);
    if (e) { { if (__builtin_expect(__builtin_constant_p(d) ? XS_IADD_C_OUT(e->val.i, d) : XS_IOUT((int64_t)((uint64_t)(e->val.i) + (uint64_t)d)), 0)) XS_FAIL(XS_IOVF_MSG, s); e->val.i += d; } }
    else xs_map_put(m, NULL, k, XS_VINT(d));
}
static inline __attribute__((always_inline)) xs_map_entry *xs_map_seek_i(xs_map *m, int64_t k) {
    uint64_t h = xs_hash_int(k);
    size_t mask = m->cap - 1, pos = (size_t)h & mask, stride = 0;
    uint8_t h2 = XS_MAP_H2(h);
    const uint8_t *ctrl = m->ctrl;
    xs_map_entry *slots = m->slots;
    __builtin_prefetch(&slots[pos]); /* (as probe_i: without it k-nucleotide k=12/18 on the N1 is 4,3 -> 5,2 s) */
    for (;;) {
        const uint8_t *g = ctrl + pos;
        for (xs_map_gmask mm = xs_map_gmatch(g, h2); mm; mm &= mm - 1) {
            size_t i = (pos + XS_MAP_GBIT(mm)) & mask;
            if (__builtin_expect(slots[i].ikey == k, 1)) return &slots[i];
        }
        if (__builtin_expect(xs_map_gmatch(g, XS_MAP_EMPTY) != 0, 1)) return NULL;
        stride += XS_MAP_GW;
        pos = (pos + stride) & mask;
    }
}
/* m.set(k, (m.get(k) ?? ...) + d): the compact table's slot holds the key in its low 32 bits and the value in the
   high ones; a sum that leaves 32 bits goes to the slow path, which widens the table and adds there (checked) */
static inline __attribute__((always_inline)) void xs_map_add_i(xs_map *m, int64_t k, int64_t d, const xs_site *s) {
    if (__builtin_expect(m->compact, 1)) {
        uint64_t h = xs_hash_int(k);
        size_t mask = m->cap - 1, pos = (size_t)h & mask, stride = 0;
        uint8_t h2 = XS_MAP_H2(h);
        const uint8_t *ctrl = m->ctrl;
        uint64_t *cs = (uint64_t *)m->slots;
        if (mask >= xs_map_pf_min) __builtin_prefetch(&cs[pos]);
        if (__builtin_expect((int32_t)k == k, 1))
            for (;;) {
                const uint8_t *g = ctrl + pos;
                for (xs_map_gmask mm = xs_map_gmatch(g, h2); mm; mm &= mm - 1) {
                    size_t i = (pos + XS_MAP_GBIT(mm)) & mask;
                    if (__builtin_expect((int32_t)(uint32_t)cs[i] == (int32_t)k, 1)) {
                        int64_t v = (int64_t)(int32_t)(uint32_t)(cs[i] >> 32) + d;
                        if (__builtin_expect((int32_t)v == v, 1)) { cs[i] = (uint32_t)cs[i] | ((uint64_t)(uint32_t)(int32_t)v << 32); return; }
                        goto slow;
                    }
                }
                if (__builtin_expect(xs_map_gmatch(g, XS_MAP_EMPTY) != 0, 1)) goto slow;
                stride += XS_MAP_GW;
                pos = (pos + stride) & mask;
            }
    } else {
        xs_map_entry *e = xs_map_seek_i(m, k);
        if (__builtin_expect(e != NULL, 1)) { { if (__builtin_expect(__builtin_constant_p(d) ? XS_IADD_C_OUT(e->val.i, d) : XS_IOUT((int64_t)((uint64_t)(e->val.i) + (uint64_t)d)), 0)) XS_FAIL(XS_IOVF_MSG, s); e->val.i += d; } return; }
    }
slow:
    xs_map_add_slow_i(m, k, d, s);
}
/* the add idiom in a loop that touches the map only through it (emit.c map_hoist): the table's fields held in
   locals across the loop, read again only after the slow path (which may grow or widen the table). Without this
   the C compiler reloads cap, ctrl and slots on every symbol: a store of the counter may alias them (k-nucleotide
   on the Xeon, with the fields of the sequence hoisted as well: 5,2 -> 3,9 cpu-s) */
typedef struct { size_t mask; const uint8_t *ctrl; void *slots; bool compact, pf; } xs_map_hc;
static inline __attribute__((always_inline)) xs_map_hc xs_map_hc_of(xs_map *m) {
    return (xs_map_hc){m->cap - 1, m->ctrl, m->slots, m->compact, m->cap - 1 >= xs_map_pf_min};
}
/* defer (a constant): the counting loop's adds of a constant d >= 0 test only the 64-bit overflow, and the loop is
   followed by xs_map_check_range (emit.c, loop_hoist): every add raises a value, so one that left the safe range
   is still out of it when the loop ends; nothing reads the map before (k-nucleotide, Xeon: the range test of the
   counter was 6% of the CPU) */
static inline __attribute__((always_inline)) void xs_map_add_hc_f(xs_map *m, xs_map_hc *c, int64_t k, int64_t d, const xs_site *s, int defer) {
    uint64_t h = xs_hash_int(k);
    size_t mask = c->mask, pos = (size_t)h & mask, stride = 0;
    uint8_t h2 = XS_MAP_H2(h);
    const uint8_t *ctrl = c->ctrl;
    if (__builtin_expect(c->compact, 1)) {
        uint64_t *cs = (uint64_t *)c->slots;
        if (c->pf) __builtin_prefetch(&cs[pos]);
        if (__builtin_expect((int32_t)k == k, 1))
            for (;;) {
                const uint8_t *g = ctrl + pos;
                for (xs_map_gmask mm = xs_map_gmatch(g, h2); mm; mm &= mm - 1) {
                    size_t i = (pos + XS_MAP_GBIT(mm)) & mask;
                    if (__builtin_expect((int32_t)(uint32_t)cs[i] == (int32_t)k, 1)) {
                        int64_t v = (int64_t)(int32_t)(uint32_t)(cs[i] >> 32) + d;
                        if (__builtin_expect((int32_t)v == v, 1)) { cs[i] = (uint32_t)cs[i] | ((uint64_t)(uint32_t)(int32_t)v << 32); return; }
                        goto slow;
                    }
                }
                if (__builtin_expect(xs_map_gmatch(g, XS_MAP_EMPTY) != 0, 1)) goto slow;
                stride += XS_MAP_GW;
                pos = (pos + stride) & mask;
            }
    } else {
        xs_map_entry *slots = c->slots;
        if (c->pf) __builtin_prefetch(&slots[pos]);
        for (;;) {
            const uint8_t *g = ctrl + pos;
            for (xs_map_gmask mm = xs_map_gmatch(g, h2); mm; mm &= mm - 1) {
                size_t i = (pos + XS_MAP_GBIT(mm)) & mask;
                if (__builtin_expect(slots[i].ikey == k, 1)) {
                    if (defer) { if (__builtin_expect(__builtin_add_overflow(slots[i].val.i, d, &slots[i].val.i), 0)) XS_FAIL(XS_IOVF_MSG, s); }
                    else { if (__builtin_expect(__builtin_constant_p(d) ? XS_IADD_C_OUT(slots[i].val.i, d) : XS_IOUT((int64_t)((uint64_t)(slots[i].val.i) + (uint64_t)d)), 0)) XS_FAIL(XS_IOVF_MSG, s); slots[i].val.i += d; }
                    return;
                }
            }
            if (__builtin_expect(xs_map_gmatch(g, XS_MAP_EMPTY) != 0, 1)) break;
            stride += XS_MAP_GW;
            pos = (pos + stride) & mask;
        }
    }
slow:
    xs_map_add_slow_i(m, k, d, s);
    *c = xs_map_hc_of(m);
}
#define xs_map_add_hc(m, c, k, d, s) xs_map_add_hc_f((m), (c), (k), (d), (s), 0)
#define xs_map_add_hcd(m, c, k, d, s) xs_map_add_hc_f((m), (c), (k), (d), (s), 1)
/* after a loop of deferred adds: every int value back in the safe range, else the error at the add's site */
static void xs_map_check_range(xs_map *m, const xs_site *s) {
    if (m->compact) return; /* (32-bit values: always in range) */
    for (size_t i = 0; i < m->cap; i++)
        if (XS_MAP_FULL(m, i) && __builtin_expect(XS_IOUT(m->slots[i].val.i), 0)) XS_FAIL(XS_IOVF_MSG, s);
}
/* after a loop of unchecked counters in an int[] whose bound passed 2^53 - 1 (emit.c, ctr_emit_for): every element
   back in the safe range, else the error at the first increment's site */
static void xs_arr_int_check_range(xs_arr_int *a, const xs_site *s) {
    for (int64_t i = 0; i < (int64_t)a->len; i++)
        if (__builtin_expect(XS_IOUT(a->data[i]), 0)) XS_FAIL(XS_IOVF_MSG, s);
}
static bool xs_map_delete_at(xs_map *m, xs_map_entry *e) {
    if (!e) return false;
    m->lidx = 0;
    m->lhit = NULL;
    if (m->str_keys) xs_release_(e->skey);
    if (m->val_kind == 3) xs_release_(e->val.p);
    xs_map_set_ctrl(m, (size_t)(e - m->slots), XS_MAP_DEL);
    m->count--;
    return true;
}
static bool xs_map_delete_s(xs_map *m, xs_str *k) { return xs_map_delete_at(m, xs_map_lookup(m, k, 0)); }
static bool xs_map_delete_i(xs_map *m, int64_t k) { return xs_map_delete_at(m, xs_map_lookup(m, NULL, k)); }
static void xs_map_clear(xs_map *m) {
    m->lidx = 0;
    m->lhit = NULL;
    if (m->compact) { memset(m->ctrl, XS_MAP_EMPTY, m->cap + XS_MAP_GW); m->used = m->norder = m->count = 0; return; }
    for (size_t i = 0; i < m->cap; i++) if (XS_MAP_FULL(m, i)) xs_map_delete_at(m, &m->slots[i]);
    memset(m->ctrl, XS_MAP_EMPTY, m->cap + XS_MAP_GW);
    m->used = 0;
    m->norder = 0;
}
XS_NOINLINE xs_val xs_map_need(xs_map *m, xs_str *skey, int64_t ikey) {
    xs_map_entry *e = xs_map_lookup(m, skey, ikey);
    if (!e) xs_panic("Map.get: key not found (check with has() first)");
    return e->val;
}
XS_NOINLINE double xs_map_get_num_s(xs_map *m, xs_str *k) { return xs_map_need(m, k, 0).d; }
XS_NOINLINE double xs_map_get_num_i(xs_map *m, int64_t k) { return xs_map_need(m, NULL, k).d; }
XS_NOINLINE int64_t xs_map_get_int_s(xs_map *m, xs_str *k) { return xs_map_need(m, k, 0).i; }
static inline __attribute__((always_inline)) int64_t xs_map_get_int_i(xs_map *m, int64_t k) {
    xs_map_entry *e = xs_map_lookup_i(m, k);
    if (__builtin_expect(!e, 0)) return xs_map_need(m, NULL, k).i; /* (it reports the missing key) */
    return e->val.i;
}
XS_NOINLINE bool xs_map_get_bool_s(xs_map *m, xs_str *k) { return xs_map_need(m, k, 0).i != 0; }
XS_NOINLINE bool xs_map_get_bool_i(xs_map *m, int64_t k) { return xs_map_need(m, NULL, k).i != 0; }
static void *xs_map_get_ptr_s(xs_map *m, xs_str *k) { xs_map_entry *e = xs_map_lookup(m, k, 0); return e ? e->val.p : NULL; }
static void *xs_map_get_ptr_i(xs_map *m, int64_t k) { xs_map_entry *e = xs_map_lookup(m, NULL, k); return e ? e->val.p : NULL; }

/* keys() / values() as new arrays, in insertion order */
static void *xs_map_keys(xs_map *m, void (*print_elem)(void *)) {
    xs_map_entry *e;
    if (m->str_keys) {
        xs_arr_ref *a = xs_arr_ref_new(m->count, print_elem ? print_elem : xs_print_str_quoted_);
        XS_MAP_EACH(m, e) xs_arr_ref_push(a, xs_retain(e->skey));
        return a;
    }
    xs_arr_int *a = xs_arr_int_new(0, 0);
    XS_MAP_EACH(m, e) xs_arr_int_push(a, e->ikey);
    return a;
}
static void *xs_map_values(xs_map *m, void (*print_elem)(void *)) {
    xs_map_entry *e;
    switch (m->val_kind) {
    case 0: { xs_arr_num *a = xs_arr_num_new(0, 0); XS_MAP_EACH(m, e) xs_arr_num_push(a, e->val.d); return a; }
    case 1: { xs_arr_int *a = xs_arr_int_new(0, 0); XS_MAP_EACH(m, e) xs_arr_int_push(a, e->val.i); return a; }
    case 2: { xs_arr_bool *a = xs_arr_bool_new(0, false); XS_MAP_EACH(m, e) xs_arr_bool_push(a, e->val.i != 0); return a; }
    default: { xs_arr_ref *a = xs_arr_ref_new(m->count, print_elem); XS_MAP_EACH(m, e) xs_arr_ref_push(a, xs_retain_(e->val.p)); return a; }
    }
}

typedef struct { char *data; size_t len, cap; } xs_sb;
static void xs_sb_put(xs_sb *b, const char *s, size_t n);
static void xs_sb_puts(xs_sb *b, const char *s);
static void xs_sb_int(xs_sb *b, int64_t v);
static void xs_sb_bool(xs_sb *b, bool v);
static xs_str *xs_sb_finish(xs_sb *b);
static void xs_num_to_str(double v, char *out);

/* ---- closures: a refcounted object whose first field after the header is the code pointer; captures follow ---- */

typedef struct xs_closure { xs_hdr hdr; void *fn; } xs_closure;
typedef struct xs_promise xs_promise; /* (runtime/xisto_net.h: a field may hold one in a program that never makes one) */
static inline double xs_num_or(double a, double b) { return a != 0 && a == a ? a : b; } /* a || b on numbers: a unless 0 or NaN */
static inline int64_t xs_int_or(int64_t a, int64_t b) { return a ? a : b; }
/* a top-level function taken as a value: its static closure (the compiler declares it {XS_IMMORTAL, 0}) gets
   the anchor type the first time it is taken (every thread writes the same bits) */
static inline __attribute__((always_inline)) xs_closure *xs_cl_static(xs_closure *c) {
    if (__builtin_expect(c->hdr.cls == 0, 0)) c->hdr.cls = XS_TIX(&xs_immortal_closure_type);
    return c;
}

/* ---- unknown: any value by reference. Primitives are boxed; `x as T` checks the object's type (its free
   function is unique per type) and fails with a TypeError, like a bad cast in a checked language. ---- */
typedef struct { xs_hdr hdr; int64_t v; } xs_box_i;
typedef struct { xs_hdr hdr; double v; } xs_box_d;
static void xs_box_int_print(void *p) { xs_print_int(((xs_box_i *)p)->v); }
static void xs_box_num_print(void *p) { xs_print_num(((xs_box_d *)p)->v); }
static void xs_box_bool_print(void *p) { fputs(((xs_box_i *)p)->v ? "true" : "false", XS_OUT); }
static void xs_box_int_free(void *p) { xs_obj_free(p); }
static void xs_box_num_free(void *p) { xs_obj_free(p); }
static void xs_box_bool_free(void *p) { xs_obj_free(p); }
static void *xs_box_int(int64_t v) { xs_box_i *b = xs_obj_alloc(sizeof *b, xs_box_int_free, xs_box_int_print); b->v = v; return b; }
static void *xs_box_num(double v) { xs_box_d *b = xs_obj_alloc(sizeof *b, xs_box_num_free, xs_box_num_print); b->v = v; return b; }
static void *xs_box_bool(bool v) { xs_box_i *b = xs_obj_alloc(sizeof *b, xs_box_bool_free, xs_box_bool_print); b->v = v; return b; }
static void *xs_box_int_or_null(int64_t v) { return v == XS_NULL_INT ? NULL : xs_box_int(v); }
static inline double xs_num_of_int_or_null(int64_t v) { return v == XS_NULL_INT ? xs_null_num() : (double)v; }
static void *xs_box_num_or_null(double v) { return xs_isnull_num(v) ? NULL : xs_box_num(v); }
static void *xs_box_bool_or_null(int8_t v) { return v == XS_NULL_BOOL ? NULL : xs_box_bool(v != 0); }
/* Set<unknown>: JavaScript's SameValueZero as a string key: strings and numbers by value (an int and a number of one
   value are one; NaN equals NaN; -0 is 0), booleans, null, and any other object by identity */
static void xs_str_free(void *p);
XS_NOINLINE xs_str *xs_uset_key(void *v) {
    char buf[64];
    if (!v) return xs_str_new("z", 1);
    void (*f)(void *) = xs_type_of(v)->free;
    if (f == xs_str_free) {
        xs_str *sv = v;
        size_t n = (size_t)sv->len;
        char *b = malloc(n + 2);
        b[0] = 's';
        memcpy(b + 1, sv->data, n);
        xs_str *r = xs_str_new(b, n + 1);
        free(b);
        return r;
    }
    if (f == xs_box_int_free || f == xs_box_num_free) {
        double d = f == xs_box_int_free ? (double)((xs_box_i *)v)->v : ((xs_box_d *)v)->v;
        if (d != d) return xs_str_new("nNaN", 4);
        if (d == 0) d = 0; /* (-0 is 0) */
        int n = snprintf(buf, sizeof buf, "n%.17g", d);
        return xs_str_new(buf, (size_t)n);
    }
    if (f == xs_box_bool_free) return xs_str_new(((xs_box_i *)v)->v ? "bt" : "bf", 2);
    int n = snprintf(buf, sizeof buf, "p%p", v);
    return xs_str_new(buf, (size_t)n);
}
static void xs_print_any_quoted(void *p);
static xs_map *xs_map_new(int is_map, int str_keys, int val_kind, void (*print_key)(void *), void (*print_val)(void *));
static xs_map *xs_map_put(xs_map *m, xs_str *skey, int64_t ikey, xs_val v);
static xs_map_entry *xs_map_lookup(xs_map *m, xs_str *skey, int64_t ikey);
static bool xs_map_delete_at(xs_map *m, xs_map_entry *e);
XS_NOINLINE xs_map *xs_uset_new(void) {
    xs_map *m = xs_map_new(0, 1, 3, xs_print_str_quoted_, xs_print_any_quoted);
    m->ukeys = true;
    return m;
}
XS_NOINLINE xs_map *xs_uset_add(xs_map *m, void *v) { /* (an element already there keeps its place, as JavaScript's) */
    xs_str *k = xs_uset_key(v);
    if (!xs_map_lookup(m, k, 0)) xs_map_put(m, k, 0, XS_VPTR(v ? xs_retain_(v) : NULL));
    xs_release(k);
    return m;
}
XS_NOINLINE bool xs_uset_has(xs_map *m, void *v) {
    xs_str *k = xs_uset_key(v);
    bool r = xs_map_lookup(m, k, 0) != NULL;
    xs_release(k);
    return r;
}
XS_NOINLINE bool xs_uset_delete(xs_map *m, void *v) {
    xs_str *k = xs_uset_key(v);
    bool r = xs_map_delete_at(m, xs_map_lookup(m, k, 0));
    xs_release(k);
    return r;
}
static void xs_print_closure(void *p);
XS_NOINLINE void xs_as_fail(const char *what) {
    static XS_TLS char buf[160];
    snprintf(buf, sizeof buf, "TypeError: the value is not %s", what);
    xs_panic(buf);
}
XS_NOINLINE void *xs_as_ref(void *v, void (*free_fn)(void *), int nullable, const char *what) {
    if (!v) { if (nullable) return NULL; xs_as_fail(what); }
    if (xs_type_of(v)->free != free_fn) xs_as_fail(what);
    return v;
}
/* timer.unref() / timer.ref(): accepted on a timer handle, no effect */
static inline void xs_timer_ref_noop(int64_t t) { (void)t; }
/* a number where a string method takes a position (s.slice(0, n / 2)): JavaScript's ToIntegerOrInfinity, NaN 0 */
XS_NOINLINE int64_t xs_num_to_index(double v) {
    if (v != v) return 0;
    if (v >= 9.2e18) return INT64_MAX;
    if (v <= -9.2e18) return INT64_MIN + 1;
    return (int64_t)v;
}
/* n ?? 'text' with n a number | null: String(n), or null for the default (JavaScript's number | string, read as text) */
static xs_str *xs_str_from_num(double v);
static xs_str *xs_str_from_int(int64_t v);
XS_NOINLINE xs_str *xs_str_of_num_or_null(double v) { return xs_isnull_num(v) ? NULL : xs_str_from_num(v); }
XS_NOINLINE xs_str *xs_str_of_int_or_null(int64_t v) { return v == XS_NULL_INT ? NULL : xs_str_from_int(v); }
/* unknown as T[] with T an object: the array and each element checked (an any of rows read by name, Records,
   is not a T[]: a TypeError, not a struct read from a Map) */
XS_NOINLINE void *xs_as_arr_of(void *v, void (*el_free)(void *), int el_nullable, int nullable, const char *what) {
    if (!v) { if (nullable) return NULL; xs_as_fail(what); }
    if (xs_type_of(v)->free != xs_arr_ref_free) xs_as_fail(what);
    xs_arr_ref *a = v;
    for (size_t i = 0; i < a->len; i++) {
        void *x = a->data[i];
        if (!x) { if (el_nullable) continue; xs_as_fail(what); }
        if (xs_type_of(x)->free != el_free) xs_as_fail(what);
    }
    return v;
}
/* x.toPrecision(p) as JavaScript: p significant digits; exponential when the exponent is < -6 or >= p */
static xs_str *xs_str_from_num(double v);
XS_NOINLINE xs_str *xs_num_to_precision(double x, int64_t p) {
    if (isnan(x) || isinf(x)) return xs_str_from_num(x);
    if (p < 1 || p > 100) xs_panic("RangeError: toPrecision() argument must be between 1 and 100");
    char buf[160], out[200];
    snprintf(buf, sizeof buf, "%.*e", (int)(p - 1), x); /* d.ddde±X, rounded */
    char *ep = strchr(buf, 'e');
    int e = atoi(ep + 1);
    *ep = 0;
    char digits[128];
    size_t nd = 0;
    bool neg = buf[0] == '-';
    for (char *c = buf + (neg ? 1 : 0); *c; c++) if (*c != '.') digits[nd++] = *c;
    digits[nd] = 0;
    size_t n = 0;
    if (neg) out[n++] = '-';
    if (x == 0) e = 0;
    if (e < -6 || e >= p) {
        out[n++] = digits[0];
        if (nd > 1) { out[n++] = '.'; memcpy(out + n, digits + 1, nd - 1); n += nd - 1; }
        n += (size_t)snprintf(out + n, sizeof out - n, "e%c%d", e < 0 ? '-' : '+', abs(e));
    } else if (e >= 0) {
        memcpy(out + n, digits, (size_t)e + 1); n += (size_t)e + 1;
        if ((size_t)e + 1 < nd) { out[n++] = '.'; memcpy(out + n, digits + e + 1, nd - (size_t)e - 1); n += nd - (size_t)e - 1; }
    } else {
        out[n++] = '0'; out[n++] = '.';
        for (int i = 0; i < -e - 1; i++) out[n++] = '0';
        memcpy(out + n, digits, nd); n += nd;
    }
    return xs_str_new(out, n);
}
/* x.toLocaleString(locale, { style, currency, minimumFractionDigits, maximumFractionDigits }) for 'pt-BR' and
   'en-US' (other locales as en-US): the shortest decimal of x rounded half away from zero at maxfd digits (as ICU),
   trailing zeros kept down to minfd, the integer part grouped by threes; currency 'BRL' / 'USD' / other codes */
XS_NOINLINE xs_str *xs_num_to_locale(double x, xs_str *locale, xs_str *currency, int64_t minfd, int64_t maxfd) {
    if (isnan(x)) return xs_str_new("NaN", 3);
    if (isinf(x)) return xs_str_new(x < 0 ? "-\xe2\x88\x9e" : "\xe2\x88\x9e", x < 0 ? 4 : 3);
    bool br = locale && locale->len >= 2 && memcmp(locale->data, "pt", 2) == 0;
    char gsep = br ? '.' : ',', dsep = br ? ',' : '.';
    if (maxfd < minfd) maxfd = minfd;
    char dec[400];
    if (fabs(x) >= 1e21 || (fabs(x) < 1e-6 && x != 0)) snprintf(dec, sizeof dec, "%.*f", (int)maxfd + 1, fabs(x));
    else { xs_str *sh = xs_str_from_num(fabs(x)); size_t l = (size_t)sh->len < sizeof dec - 1 ? (size_t)sh->len : sizeof dec - 1; memcpy(dec, sh->data, l); dec[l] = 0; xs_release_(sh); }
    char ip[400], fp[400];
    char *dot = strchr(dec, '.');
    size_t il = dot ? (size_t)(dot - dec) : strlen(dec);
    memcpy(ip + 1, dec, il); ip[0] = '0'; il++; ip[il] = 0; /* a leading 0 takes a carry */
    size_t fl = 0;
    if (dot) for (char *c = dot + 1; *c; c++) fp[fl++] = *c;
    fp[fl] = 0;
    if ((int64_t)fl > maxfd) { /* round half away from zero at maxfd */
        bool up = fp[maxfd] >= '5';
        fl = (size_t)maxfd;
        if (up) {
            int64_t k = (int64_t)fl - 1;
            for (; k >= 0; k--) { if (fp[k] == '9') fp[k] = '0'; else { fp[k]++; break; } }
            if (k < 0) {
                int64_t j = (int64_t)il - 1;
                for (; j >= 0; j--) { if (ip[j] == '9') ip[j] = '0'; else { ip[j]++; break; } }
            }
        }
    }
    while ((int64_t)fl > minfd && fp[fl - 1] == '0') fl--;
    while ((int64_t)fl < minfd) fp[fl++] = '0';
    const char *ipd = ip;
    while (*ipd == '0' && ipd[1]) ipd++;
    size_t ni = strlen(ipd);
    bool zero = true;
    for (size_t i = 0; i < ni; i++) if (ipd[i] != '0') zero = false;
    for (size_t i = 0; i < fl; i++) if (fp[i] != '0') zero = false;
    char out[1000];
    size_t n = 0;
    (void)zero;
    if (signbit(x)) out[n++] = '-'; /* (ICU keeps the sign of a value that rounds to 0, and of -0) */
    if (currency) {
        if (currency->len == 3 && memcmp(currency->data, "BRL", 3) == 0) { memcpy(out + n, "R$\xc2\xa0", 4); n += 4; }
        else if (currency->len == 3 && memcmp(currency->data, "USD", 3) == 0) { if (br) { memcpy(out + n, "US$\xc2\xa0", 6); n += 6; } else out[n++] = '$'; }
        else { memcpy(out + n, currency->data, (size_t)currency->len); n += (size_t)currency->len; memcpy(out + n, "\xc2\xa0", 2); n += 2; }
    }
    for (size_t i = 0; i < ni; i++) {
        out[n++] = ipd[i];
        size_t left = ni - 1 - i;
        if (left && left % 3 == 0) out[n++] = gsep;
    }
    if (fl) { out[n++] = dsep; memcpy(out + n, fp, fl); n += fl; }
    return xs_str_new(out, n);
}
/* x.toFixed(d) as JavaScript: the exact decimal value of the double rounded half up in magnitude (printf would
   round an exact tie to even), "-" when x < 0, String(x) from 1e21 on */
static xs_str *xs_str_from_num(double v);
XS_NOINLINE xs_str *xs_num_to_fixed(double x, int64_t d) {
    if (d < 0 || d > 100) xs_panic("RangeError: toFixed() digits argument must be between 0 and 100");
    if (isnan(x)) return xs_str_new("NaN", 3);
    if (fabs(x) >= 1e21) return xs_str_from_num(x);
    if (d <= 22 && fabs(x) < 9007199254740992.0) {
        /* |x| = m * 2^e exactly (m < 2^53); N = |x| * 10^d rounded half up is (m * 10^d) / 2^-e rounded, in 128-bit
           integers (m * 10^22 < 2^127): the same digits as the exact expansion below, without printing 1100 of them */
        static const uint64_t p10[20] = {1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull,
            100000000ull, 1000000000ull, 10000000000ull, 100000000000ull, 1000000000000ull, 10000000000000ull,
            100000000000000ull, 1000000000000000ull, 10000000000000000ull, 100000000000000000ull,
            1000000000000000000ull, 10000000000000000000ull};
        uint64_t bits;
        double ax = fabs(x);
        memcpy(&bits, &ax, 8);
        int be = (int)(bits >> 52);
        uint64_t m = bits & ((UINT64_C(1) << 52) - 1);
        int e = be ? be - 1075 : -1074;
        if (be) m |= UINT64_C(1) << 52;
        unsigned __int128 pd = d < 20 ? (unsigned __int128)p10[d] : (unsigned __int128)p10[19] * p10[d - 19];
        unsigned __int128 N;
        if (e >= 0) N = ((unsigned __int128)m << e) * pd; /* an integer below 2^53 */
        else {
            unsigned __int128 P = (unsigned __int128)m * pd;
            int k = -e;
            if (k >= 128) N = 0; /* P < 2^127 <= half of 2^k: below half */
            else {
                N = P >> k;
                if (((P >> (k - 1)) & 1) != 0) N++; /* the remainder is half or more: up (a tie goes up, as toFixed) */
            }
        }
        char dig[48];
        int nd = 0;
        do { dig[nd++] = (char)('0' + (int)(N % 10)); N /= 10; } while (N);
        while (nd < d + 1) dig[nd++] = '0';
        char out[64];
        size_t n = 0;
        if (x < 0) out[n++] = '-';
        for (int i = nd - 1; i >= 0; i--) {
            out[n++] = dig[i];
            if (i == d && d > 0) out[n++] = '.';
        }
        return xs_str_new(out, n);
    }
    static XS_TLS char exact[1200];
    snprintf(exact, sizeof exact, "%.1100f", fabs(x)); /* every digit of the double, exactly */
    char *dot = strchr(exact, '.');
    size_t ilen = (size_t)(dot - exact);
    char out[1300];
    size_t n = 0;
    if (x < 0) out[n++] = '-';
    size_t start = n;
    memcpy(out + n, exact, ilen);
    n += ilen;
    for (int64_t i = 0; i < d; i++) out[n++] = dot[1 + i];
    if (dot[1 + d] >= '5') { /* round up the magnitude, carrying left */
        size_t k = n;
        while (k > start) {
            k--;
            if (out[k] == '9') { out[k] = '0'; continue; }
            out[k]++;
            break;
        }
        if (k == start && out[start] == '0') { memmove(out + start + 1, out + start, n - start); out[start] = '1'; n++; }
    }
    if (d > 0) { memmove(out + n - d + 1, out + n - d, (size_t)d); out[n - d] = '.'; n++; }
    return xs_str_new(out, n);
}
XS_NOINLINE xs_str *xs_int_to_fixed(int64_t v, int64_t d) { return xs_num_to_fixed((double)v, d); }
/* base as Sub: the object's class is Sub or one of its subclasses (isa is generated per class) */
XS_NOINLINE void *xs_as_isa(void *v, bool (*isa)(void *), int nullable, const char *what) {
    if (!v) { if (nullable) return NULL; xs_as_fail(what); }
    if (!isa(v)) xs_as_fail(what);
    return v;
}
XS_NOINLINE void *xs_as_closure(void *v, int nullable) {
    if (!v) { if (nullable) return NULL; xs_as_fail("a function"); }
    if (xs_type_of(v)->print != xs_print_closure) xs_as_fail("a function");
    return v;
}
XS_NOINLINE int64_t xs_as_int(void *v) { if (!v || xs_type_of(v)->free != xs_box_int_free) xs_as_fail("int"); return ((xs_box_i *)v)->v; }
XS_NOINLINE double xs_as_num(void *v) {
    if (v && xs_type_of(v)->free == xs_box_int_free) return (double)((xs_box_i *)v)->v; /* an int is a number */
    if (!v || xs_type_of(v)->free != xs_box_num_free) xs_as_fail("number");
    return ((xs_box_d *)v)->v;
}
XS_NOINLINE bool xs_as_bool(void *v) { if (!v || xs_type_of(v)->free != xs_box_bool_free) xs_as_fail("boolean"); return ((xs_box_i *)v)->v != 0; }
XS_NOINLINE int64_t xs_as_int_or_null(void *v) { return v ? xs_as_int(v) : XS_NULL_INT; }
XS_NOINLINE double xs_as_num_or_null(void *v) { return v ? xs_as_num(v) : xs_null_num(); }
XS_NOINLINE int8_t xs_as_bool_or_null(void *v) { return v ? (int8_t)xs_as_bool(v) : XS_NULL_BOOL; }
/* typeof: immortal strings, so the result needs no release */
static xs_str *xs_typeof_lit(const char *s) {
    static xs_str *made[8];
    static const char *names[8] = {"string", "number", "boolean", "object", "function", "undefined", "symbol", "bigint"};
    for (int i = 0; i < 8; i++)
        if (strcmp(s, names[i]) == 0) {
            if (!made[i]) made[i] = xs_str_immortal(names[i], strlen(names[i]));
            return made[i];
        }
    return xs_typeof_lit("object");
}
static void xs_print_closure(void *p);
/* ---- symbol: Symbol("desc"), a value equal only to itself (Nest's injection tokens); description as given ---- */
typedef struct xs_Symbol { xs_hdr hdr; xs_str *f_description; int64_t f___id; } xs_Symbol;
static void xs_Symbol_free(void *p) { xs_release(((xs_Symbol *)p)->f_description); xs_obj_free(p); }
static void xs_Symbol_print(void *p) {
    xs_Symbol *y = p;
    fputs("Symbol(", XS_OUT);
    if (y->f_description) xs_print_str(y->f_description);
    fputs(")", XS_OUT);
}
static int64_t xs_symbol_ids;
XS_NOINLINE xs_Symbol *xs_Symbol_new(xs_str *description) {
    xs_Symbol *y = xs_obj_alloc(sizeof *y, xs_Symbol_free, xs_Symbol_print);
    y->f_description = xs_retain(description);
    y->f___id = __atomic_add_fetch(&xs_symbol_ids, 1, __ATOMIC_RELAXED);
    return y;
}
static xs_str *xs_str_concat(xs_str *a, xs_str *b);
/* String(sym): "Symbol(desc)" */
XS_NOINLINE xs_str *xs_Symbol_to_string(xs_Symbol *y) {
    xs_str *d = y->f_description;
    size_t n = d ? (size_t)d->len : 0;
    xs_str *r = xs_str_new("Symbol(", 7);
    xs_str *t = xs_str_new(d ? d->data : "", n);
    xs_str *a = xs_str_concat(r, t);
    xs_release(r);
    xs_release(t);
    xs_str *close = xs_str_new(")", 1);
    xs_str *out = xs_str_concat(a, close);
    xs_release(a);
    xs_release(close);
    return out;
}
static void xs_big_free(void *p);
static void (*const xs_big_free_fn)(void *) = xs_big_free;
XS_NOINLINE xs_str *xs_typeof(void *v) {
    if (!v) return xs_typeof_lit("object"); /* typeof null */
    void (*f)(void *) = xs_type_of(v)->free;
    if (f == xs_str_free) return xs_typeof_lit("string");
    if (f == xs_box_int_free || f == xs_box_num_free) return xs_typeof_lit("number");
    if (f == xs_box_bool_free) return xs_typeof_lit("boolean");
    if (f == xs_Symbol_free) return xs_typeof_lit("symbol");
    if (f == xs_big_free_fn) return xs_typeof_lit("bigint");
    if (xs_type_of(v)->print == xs_print_closure) return xs_typeof_lit("function");
    return xs_typeof_lit("object");
}
static void xs_print_closure(void *p) { (void)p; fputs("[Function]", XS_OUT); }

/* Math.max(...xs) / Math.min(...xs): -Infinity / Infinity for an empty array, NaN when an element is */
XS_NOINLINE double xs_math_max_nums(xs_arr_num *a) { double m = -INFINITY; for (size_t i = 0; i < a->len; i++) { if (a->data[i] != a->data[i]) return NAN; if (a->data[i] > m) m = a->data[i]; } return m; }
XS_NOINLINE double xs_math_min_nums(xs_arr_num *a) { double m = INFINITY; for (size_t i = 0; i < a->len; i++) { if (a->data[i] != a->data[i]) return NAN; if (a->data[i] < m) m = a->data[i]; } return m; }
XS_NOINLINE double xs_math_max_ints(xs_arr_int *a) { double m = -INFINITY; for (size_t i = 0; i < a->len; i++) if ((double)a->data[i] > m) m = (double)a->data[i]; return m; }
XS_NOINLINE double xs_math_min_ints(xs_arr_int *a) { double m = INFINITY; for (size_t i = 0; i < a->len; i++) if ((double)a->data[i] < m) m = (double)a->data[i]; return m; }

/* ---- any: the dynamic path (MANIFESTO, "Ponto importante: any"). An any is an unknown (a reference, primitives
   boxed) the program reads and writes by name. An object of a class keeps its struct: x.f and x[k] go through
   the class's field table (xs_dyn_cls, generated per class when the program uses any), so it takes no memory
   of its own; a name the class does not have is a TypeError (JavaScript would read undefined, or add the
   field). An object made as an any ({} or { a: 1 } typed any) is a Record<string, unknown>. ---- */
enum { XD_NUM, XD_INT, XD_BOOL, XD_NNUM, XD_NINT, XD_NBOOL, XD_STR, XD_REF, XD_ANY };
typedef struct { const char *name; uint32_t len, off; uint8_t kind, nullable, optional; bool (*isa)(void *); const char *tname; } xs_dyn_field;
typedef struct xs_dyn_cls { const char *name; int32_t n; const xs_dyn_field *f; int32_t nm; const char *const *m; int32_t shape; } xs_dyn_cls; /* m: its methods' names; shape: an interface's (made from literals: an optional field left out is not there) */
/* a method read by name through an any (typeof x.toJSON === "function"): a function whose call is not supported yet */
XS_NOINLINE void xs_dyn_method_called(void) { xs_panic("TypeError: a method read through an any cannot be called (call it on a value of its class)"); }
static xs_closure xs_dyn_method_cl = {{XS_IMMORTAL, 0}, (void *)xs_dyn_method_called};
static void xs_Date_free(void *p);
static bool xs_dyn_has_method(const xs_dyn_cls *c, xs_str *k) {
    for (int32_t i = 0; i < c->nm; i++) if (strlen(c->m[i]) == (size_t)k->len && memcmp(c->m[i], k->data, (size_t)k->len) == 0) return true;
    return false;
}
static double xs_str_to_number(xs_str *s);
static xs_str *xs_str_from_int(int64_t v);
static const xs_dyn_field *xs_dyn_field_of(const xs_dyn_cls *c, xs_str *k) {
    for (int32_t i = 0; i < c->n; i++)
        if (c->f[i].len == (uint32_t)k->len && memcmp(c->f[i].name, k->data, (size_t)k->len) == 0) return &c->f[i];
    return NULL;
}
static const char *xs_dyn_what(void *v) {
    if (!v) return "null";
    const xs_type *t = xs_type_of(v);
    if (t->dyn) return t->dyn->name;
    if (t->free == xs_str_free) return "a string";
    if (t->free == xs_box_int_free || t->free == xs_box_num_free) return "a number";
    if (t->free == xs_box_bool_free) return "a boolean";
    if (t->free == xs_map_free) return "a Map";
    if (t->free == xs_arr_ref_free || t->free == xs_arr_num_free || t->free == xs_arr_int_free || t->free == xs_arr_bool_free) return "an array";
    if (t->print == xs_print_closure) return "a function";
    return "an object";
}
XS_NOINLINE void xs_dyn_fail_read(void *o, xs_str *k) {
    static XS_TLS char buf[300];
    if (!o) snprintf(buf, sizeof buf, "TypeError: Cannot read properties of null (reading '%.*s')", (int)(k->len > 100 ? 100 : k->len), k->data);
    else snprintf(buf, sizeof buf, "TypeError: %s has no field '%.*s' (an any reads the fields its class declares)", xs_dyn_what(o), (int)(k->len > 100 ? 100 : k->len), k->data);
    xs_panic(buf);
}
XS_NOINLINE void xs_dyn_fail_write(void *o, xs_str *k, const char *why) {
    static XS_TLS char buf[300];
    if (!o) snprintf(buf, sizeof buf, "TypeError: Cannot set properties of null (setting '%.*s')", (int)(k->len > 100 ? 100 : k->len), k->data);
    else snprintf(buf, sizeof buf, "TypeError: cannot set '%.*s' of %s: %s", (int)(k->len > 100 ? 100 : k->len), k->data, xs_dyn_what(o), why);
    xs_panic(buf);
}
/* a field's value as an any (owned) */
static void *xs_dyn_load(const xs_dyn_field *f, void *o) {
    char *p = (char *)o + f->off;
    switch (f->kind) {
    case XD_NUM: return xs_box_num(*(double *)p);
    case XD_INT: return xs_box_int(*(int64_t *)p);
    case XD_BOOL: return xs_box_bool(*(bool *)p);
    case XD_NNUM: return xs_box_num_or_null(*(double *)p);
    case XD_NINT: return xs_box_int_or_null(*(int64_t *)p);
    case XD_NBOOL: return xs_box_bool_or_null(*(int8_t *)p);
    default: return xs_retain_(*(void **)p);
    }
}
static void *xs_dyn_map_val(xs_map *m, xs_val v) {
    switch (m->val_kind) {
    case 0: return xs_box_num(v.d);
    case 1: return xs_box_int(v.i);
    case 2: return xs_box_bool(v.i != 0);
    default: return xs_retain_(v.p);
    }
}
XS_NOINLINE int64_t xs_dyn_as_int(void *v);
XS_NOINLINE double xs_dyn_as_num(void *v) { return xs_as_num(v); }
/* o.k / o[k] on an any: an owned any (null for a key a Record does not have, as JavaScript's undefined) */
XS_NOINLINE void *xs_dyn_get(void *o, xs_str *k) {
    if (!o) xs_dyn_fail_read(o, k);
    const xs_type *t = xs_type_of(o);
    if (t->dyn) {
        const xs_dyn_field *f = xs_dyn_field_of(t->dyn, k);
        if (!f) return xs_dyn_has_method(t->dyn, k) ? xs_cl_static(&xs_dyn_method_cl) : NULL; /* a field the class does not
                                declare reads as undefined, as in JavaScript (err?.status || 500); writing one is still an error */
        return xs_dyn_load(f, o);
    }
    if (t->free == xs_Date_free) { /* a Date's methods (typeof d.toJSON === "function"); it has no fields */
        static const char *const dm[] = {"toJSON", "toISOString", "getTime", "valueOf", "toString"};
        static const xs_dyn_cls dc = {"Date", 0, NULL, 5, dm};
        return xs_dyn_has_method(&dc, k) ? xs_cl_static(&xs_dyn_method_cl) : NULL;
    }
    if (t->free == xs_Error_free) { /* an Error seen as any: its own fields, then the object it was thrown as (err.status) */
        xs_Error *e = o;
        if (k->len == 7 && memcmp(k->data, "message", 7) == 0) return xs_retain_(e->f_message);
        if (k->len == 4 && memcmp(k->data, "name", 4) == 0) return xs_retain_(e->f_name);
        if (k->len == 5 && memcmp(k->data, "stack", 5) == 0) return xs_retain_(xs_Error_stack(e));
        if (k->len == 5 && memcmp(k->data, "cause", 5) == 0) return xs_retain_(e->f_cause);
        return e->f___thrown ? xs_dyn_get(e->f___thrown, k) : NULL;
    }
    if (t->free == xs_map_free) {
        xs_map *m = o;
        if (!m->str_keys) return NULL;
        xs_map_entry *e = xs_map_lookup(m, k, 0);
        return e ? xs_dyn_map_val(m, e->val) : NULL;
    }
    if (k->len == 6 && memcmp(k->data, "length", 6) == 0) {
        if (t->free == xs_str_free) return xs_box_int(((xs_str *)o)->len); /* (bytes: the ASCII text a row holds) */
        if (t->free == xs_arr_ref_free) return xs_box_int((int64_t)((xs_arr_ref *)o)->len);
        if (t->free == xs_arr_num_free) return xs_box_int((int64_t)((xs_arr_num *)o)->len);
        if (t->free == xs_arr_int_free) return xs_box_int((int64_t)((xs_arr_int *)o)->len);
        if (t->free == xs_arr_bool_free) return xs_box_int((int64_t)((xs_arr_bool *)o)->len);
    }
    return NULL; /* (undefined, as above) */
}
/* o?.k: null when o is */
XS_NOINLINE void *xs_dyn_get_opt(void *o, xs_str *k) { return o ? xs_dyn_get(o, k) : NULL; }
/* o[i] on an any: an array's element (null past its end, as undefined), or a Record's key i */
XS_NOINLINE void *xs_dyn_at(void *o, int64_t i) {
    if (!o) xs_panic("TypeError: Cannot read properties of null (reading an index)");
    const xs_type *t = xs_type_of(o);
    if (t->free == xs_arr_ref_free) { xs_arr_ref *a = o; return i >= 0 && (size_t)i < a->len ? xs_retain_(a->data[i]) : NULL; }
    if (t->free == xs_arr_num_free) { xs_arr_num *a = o; return i >= 0 && (size_t)i < a->len ? xs_box_num(a->data[i]) : NULL; }
    if (t->free == xs_arr_int_free) { xs_arr_int *a = o; return i >= 0 && (size_t)i < a->len ? xs_box_int(a->data[i]) : NULL; }
    if (t->free == xs_arr_bool_free) { xs_arr_bool *a = o; return i >= 0 && (size_t)i < a->len ? xs_box_bool(a->data[i]) : NULL; }
    xs_str *k = xs_str_from_int(i);
    void *r = xs_dyn_get(o, k);
    xs_release(k);
    return r;
}
/* o[k] with k an any: a string key or an index */
XS_NOINLINE void *xs_dyn_get_any(void *o, void *k) {
    if (k && xs_type_of(k)->free == xs_str_free) return xs_dyn_get(o, k);
    return xs_dyn_at(o, xs_dyn_as_int(k));
}
/* the value v (an any, borrowed) stored in a field: checked against the field's type */
XS_NOINLINE void xs_dyn_store(const xs_dyn_field *f, void *o, xs_str *k, void *v) {
    char *p = (char *)o + f->off;
    switch (f->kind) {
    case XD_NUM: if (!v) xs_dyn_fail_write(o, k, "the field is a number, not null"); *(double *)p = xs_as_num(v); return;
    case XD_INT: if (!v) xs_dyn_fail_write(o, k, "the field is an int, not null"); *(int64_t *)p = xs_dyn_as_int(v); return;
    case XD_BOOL: if (!v) xs_dyn_fail_write(o, k, "the field is a boolean, not null"); *(bool *)p = xs_as_bool(v); return;
    case XD_NNUM: *(double *)p = v ? xs_as_num(v) : xs_null_num(); return;
    case XD_NINT: *(int64_t *)p = v ? xs_dyn_as_int(v) : XS_NULL_INT; return;
    case XD_NBOOL: *(int8_t *)p = v ? (int8_t)xs_as_bool(v) : XS_NULL_BOOL; return;
    case XD_STR:
        if (!v && !f->nullable) xs_dyn_fail_write(o, k, "the field is a string, not null");
        if (v && xs_type_of(v)->free != xs_str_free) xs_dyn_fail_write(o, k, "the field is a string");
        break;
    case XD_REF:
        if (!v && !f->nullable) xs_dyn_fail_write(o, k, "the field cannot be null");
        if (v && f->isa && !f->isa(v)) xs_dyn_fail_write(o, k, f->tname ? f->tname : "another type");
        if (v && !f->isa) xs_dyn_fail_write(o, k, "its type is not checked at run time; assign it through a typed value");
        break;
    default: break;
    }
    void *old = *(void **)p;
    *(void **)p = xs_retain_(v);
    xs_release_(old);
}
/* o.k = v / o[k] = v on an any (v an any, borrowed) */
XS_NOINLINE void xs_dyn_set(void *o, xs_str *k, void *v) {
    if (!o) xs_dyn_fail_write(o, k, "");
    const xs_type *t = xs_type_of(o);
    if (t->dyn) {
        const xs_dyn_field *f = xs_dyn_field_of(t->dyn, k);
        if (!f) xs_dyn_fail_write(o, k, "its class has no such field (an any writes the fields its class declares)");
        xs_dyn_store(f, o, k, v);
        return;
    }
    if (t->free == xs_map_free && ((xs_map *)o)->str_keys) {
        xs_map *m = o;
        xs_val x;
        switch (m->val_kind) {
        case 0: x.d = xs_as_num(v); break;
        case 1: x.i = xs_dyn_as_int(v); break;
        case 2: x.i = xs_as_bool(v); break;
        default: x.p = xs_retain_(v);
        }
        xs_map_set_s(m, k, x);
        return;
    }
    xs_dyn_fail_write(o, k, "it has no fields");
}
/* x as Record<string, V> from an unknown: a Record is itself; an object of a class becomes a Record of its fields (a copy:
   Object.entries(data as Record<string, unknown>) reads it, as JavaScript reads any object's own properties; writing
   to the copy does not change the object). Borrowed, as a cast's value is: the copy lives until the arena drains. */
XS_NOINLINE void *xs_as_record(void *v, int nullable, const char *what) {
    if (!v) { if (nullable) return NULL; xs_as_fail(what); }
    const xs_type *t = xs_type_of(v);
    if (t->free == xs_map_free && ((xs_map *)v)->str_keys) return v;
    if (!t->dyn) xs_as_fail(what);
    xs_map *m = xs_map_rec(xs_map_new(1, 1, 3, xs_print_str_quoted_, xs_print_any_quoted));
    for (int32_t i = 0; i < t->dyn->n; i++) {
        const xs_dyn_field *f = &t->dyn->f[i];
        xs_str *k = xs_str_new(f->name, f->len);
        xs_val x;
        x.p = xs_dyn_load(f, v);
        if (t->dyn->shape && f->optional && !x.p) { xs_release_(k); continue; } /* (no key of the literal it was made from) */
        xs_map_set_s(m, k, x);
        xs_release_(k);
    }
    return xs_ar_(m);
}
/* x as number[] (int[], boolean[]) from an unknown: an array of that kind is itself; an array of unknowns (JSON.parse,
   an any) is copied with each element checked. Borrowed, as a cast's value is (the copy lives until the arena drains). */
XS_NOINLINE void *xs_as_arr_prim(void *v, int kind, int nullable, const char *what) {
    if (!v) { if (nullable) return NULL; xs_as_fail(what); }
    void (*f)(void *) = xs_type_of(v)->free;
    if ((kind == 0 && f == xs_arr_num_free) || (kind == 1 && f == xs_arr_int_free) || (kind == 2 && f == xs_arr_bool_free)) return v;
    if (f != xs_arr_ref_free) xs_as_fail(what);
    xs_arr_ref *a = v;
    if (kind == 0) {
        xs_arr_num *r = xs_arr_num_new(0, 0);
        for (size_t i = 0; i < a->len; i++) xs_arr_num_push(r, xs_as_num(a->data[i]));
        return xs_ar_(r);
    }
    if (kind == 1) {
        xs_arr_int *r = xs_arr_int_new(0, 0);
        for (size_t i = 0; i < a->len; i++) xs_arr_int_push(r, xs_dyn_as_int(a->data[i]));
        return xs_ar_(r);
    }
    xs_arr_bool *r = xs_arr_bool_new(0, false);
    for (size_t i = 0; i < a->len; i++) xs_arr_bool_push(r, xs_as_bool(a->data[i]));
    return xs_ar_(r);
}
XS_NOINLINE void xs_dyn_set_any(void *o, void *k, void *v) {
    if (k && xs_type_of(k)->free == xs_str_free) { xs_dyn_set(o, k, v); return; }
    int64_t i = xs_dyn_as_int(k);
    if (o && xs_type_of(o)->free == xs_arr_ref_free) {
        xs_arr_ref *a = o;
        if (i < 0 || (size_t)i >= a->len) xs_panic("RangeError: index out of the array (xisto arrays do not grow by index)");
        void *old = a->data[i];
        a->data[i] = xs_retain_(v);
        xs_release_(old);
        return;
    }
    xs_str *ks = xs_str_from_int(i);
    xs_dyn_set(o, ks, v);
    xs_release(ks);
}
/* k in o: the class has the field (an optional one only when it is given: left out, it is not there); a Record's key */
XS_NOINLINE bool xs_dyn_has(void *o, xs_str *k) {
    if (!o) xs_panic("TypeError: Cannot use 'in' operator to search for a key in null");
    const xs_type *t = xs_type_of(o);
    if (t->dyn) {
        const xs_dyn_field *f = xs_dyn_field_of(t->dyn, k);
        if (!f) return false;
        if (!f->optional) return true;
        void *v = xs_dyn_load(f, o);
        bool has = v != NULL;
        xs_release(v);
        return has;
    }
    if (t->free == xs_map_free) return ((xs_map *)o)->str_keys && xs_map_lookup(o, k, 0) != NULL;
    if (t->free == xs_Error_free) { /* 'cause' in err: an Error has message and stack; cause when it was given */
        if (k->len == 5 && memcmp(k->data, "cause", 5) == 0) return ((xs_Error *)o)->f_cause != NULL;
        return (k->len == 7 && memcmp(k->data, "message", 7) == 0) || (k->len == 5 && memcmp(k->data, "stack", 5) == 0);
    }
    return false;
}
/* 'k' in u, u a union of objects (held as an unknown): the member it holds has k */
XS_NOINLINE bool xs_union_has(void *o, xs_str *k) { return o && xs_dyn_has(o, k); }
XS_NOINLINE int64_t xs_dyn_as_int(void *v) {
    if (v && xs_type_of(v)->free == xs_box_num_free) { /* a number with no fraction (JavaScript has only numbers) */
        double d = ((xs_box_d *)v)->v;
        if (d == (double)(int64_t)d && fabs(d) <= 9007199254740991.0) return (int64_t)d;
        xs_as_fail("int");
    }
    return xs_as_int(v);
}
XS_NOINLINE int64_t xs_dyn_as_int_or_null(void *v) { return v ? xs_dyn_as_int(v) : XS_NULL_INT; }
/* if (x), !x, x && y on an any: JavaScript's truthiness */
XS_NOINLINE bool xs_dyn_truthy(void *v) {
    if (!v) return false;
    const xs_type *t = xs_type_of(v);
    if (t->free == xs_box_num_free) { double d = ((xs_box_d *)v)->v; return d != 0 && d == d; }
    if (t->free == xs_box_int_free || t->free == xs_box_bool_free) return ((xs_box_i *)v)->v != 0;
    if (t->free == xs_str_free) return ((xs_str *)v)->len > 0;
    return true;
}
/* a === b with an any on one side (both anys, borrowed): the same number, string or boolean, or the same object */
XS_NOINLINE bool xs_dyn_eq(void *a, void *b) {
    if (!a || !b) return a == b;
    if (a == b) return true;
    const xs_type *ta = xs_type_of(a), *tb = xs_type_of(b);
    bool na = ta->free == xs_box_num_free || ta->free == xs_box_int_free, nb = tb->free == xs_box_num_free || tb->free == xs_box_int_free;
    if (na || nb) return na && nb && xs_as_num(a) == xs_as_num(b);
    if (ta->free == xs_str_free && tb->free == xs_str_free) {
        xs_str *x = a, *y = b;
        return x->len == y->len && memcmp(x->data, y->data, (size_t)x->len) == 0;
    }
    if (ta->free == xs_box_bool_free && tb->free == xs_box_bool_free) return ((xs_box_i *)a)->v == ((xs_box_i *)b)->v;
    return false;
}
/* Number(x) / arithmetic on an any: as JavaScript converts (null is 0, a string is parsed, an object is NaN) */
XS_NOINLINE double xs_dyn_num(void *v) {
    if (!v) return 0;
    const xs_type *t = xs_type_of(v);
    if (t->free == xs_box_num_free) return ((xs_box_d *)v)->v;
    if (t->free == xs_box_int_free || t->free == xs_box_bool_free) return (double)((xs_box_i *)v)->v;
    if (t->free == xs_str_free) return xs_str_to_number(v);
    return NAN;
}
/* String(x) / `${x}` on an any (owned) */
static xs_str *xs_dyn_lit(const char *s) { return xs_str_new(s, strlen(s)); }
XS_NOINLINE xs_str *xs_dyn_str(void *v) {
    if (!v) return xs_dyn_lit("null");
    const xs_type *t = xs_type_of(v);
    if (t->free == xs_str_free) return xs_retain_(v);
    if (t->free == xs_box_num_free) return xs_str_from_num(((xs_box_d *)v)->v);
    if (t->free == xs_box_int_free) return xs_str_from_int(((xs_box_i *)v)->v);
    if (t->free == xs_box_bool_free) return xs_dyn_lit(((xs_box_i *)v)->v ? "true" : "false");
    if (t->free == xs_Symbol_free) return xs_Symbol_to_string(v);
    if (t->free == xs_Error_free) { /* Error.prototype.toString: "name: message" (the name alone when the message is empty) */
        xs_Error *e = v;
        int64_t nl = e->f_name ? e->f_name->len : 0, ml = e->f_message ? e->f_message->len : 0;
        if (ml == 0) return e->f_name ? xs_retain_(e->f_name) : xs_dyn_lit("");
        if (nl == 0) return xs_retain_(e->f_message);
        char *buf = malloc((size_t)(nl + ml + 2));
        memcpy(buf, e->f_name->data, (size_t)nl); buf[nl] = ':'; buf[nl + 1] = ' ';
        memcpy(buf + nl + 2, e->f_message->data, (size_t)ml);
        xs_str *r = xs_str_new(buf, (size_t)(nl + ml + 2));
        free(buf);
        return r;
    }
    return xs_dyn_lit("[object Object]");
}
/* a + b with an any on one side (borrowed): a string when either is one, else a number */
XS_NOINLINE void *xs_dyn_add(void *a, void *b) {
    bool sa = a && xs_type_of(a)->free == xs_str_free, sb = b && xs_type_of(b)->free == xs_str_free;
    if (sa || sb) {
        xs_str *x = xs_dyn_str(a), *y = xs_dyn_str(b);
        xs_str *r = xs_str_concat(x, y);
        xs_release(x);
        xs_release(y);
        return r;
    }
    bool ia = a && xs_type_of(a)->free == xs_box_int_free, ib = b && xs_type_of(b)->free == xs_box_int_free;
    if (ia && ib) {
        int64_t r;
        if (!__builtin_add_overflow(((xs_box_i *)a)->v, ((xs_box_i *)b)->v, &r) && r <= 9007199254740991LL && r >= -9007199254740991LL) return xs_box_int(r);
    }
    return xs_box_num(xs_dyn_num(a) + xs_dyn_num(b));
}

/* ---- array methods driven by callbacks: one instance per element kind ---- */

#define XS_ARR_CALLBACKS(name, T, PUSH_OWNED)                                                              \
    XS_NOINLINE void xs_arr_forEach_##name(name *a, xs_closure *cl, int arity) {                                \
        for (size_t i = 0; i < a->len; i++) {                                                             \
            if (arity == 2) ((void (*)(void *, T, int64_t))cl->fn)(cl, a->data[i], (int64_t)i);           \
            else ((void (*)(void *, T))cl->fn)(cl, a->data[i]);                                           \
        }                                                                                                 \
    }                                                                                                     \
    XS_NOINLINE bool xs_arr_test_##name(xs_closure *cl, int arity, T v, size_t i) {                             \
        return arity == 2 ? ((bool (*)(void *, T, int64_t))cl->fn)(cl, v, (int64_t)i) : ((bool (*)(void *, T))cl->fn)(cl, v); \
    }                                                                                                     \
    XS_NOINLINE name *xs_arr_filter_##name(name *a, xs_closure *cl, int arity) {                                \
        name *r = name##_new(0, 0);                                                                       \
        for (size_t i = 0; i < a->len; i++) if (xs_arr_test_##name(cl, arity, a->data[i], i)) name##_push(r, PUSH_OWNED(a->data[i])); \
        return r;                                                                                         \
    }                                                                                                     \
    XS_NOINLINE int64_t xs_arr_findIndex_##name(name *a, xs_closure *cl, int arity) {                           \
        for (size_t i = 0; i < a->len; i++) if (xs_arr_test_##name(cl, arity, a->data[i], i)) return (int64_t)i; \
        return -1;                                                                                        \
    }                                                                                                     \
    XS_NOINLINE bool xs_arr_some_##name(name *a, xs_closure *cl, int arity) { return xs_arr_findIndex_##name(a, cl, arity) >= 0; } \
    XS_NOINLINE bool xs_arr_every_##name(name *a, xs_closure *cl, int arity) {                                  \
        for (size_t i = 0; i < a->len; i++) if (!xs_arr_test_##name(cl, arity, a->data[i], i)) return false; \
        return true;                                                                                      \
    }                                                                                                     \
    XS_NOINLINE name *xs_arr_sort_##name(name *a, xs_closure *cl, int int_cmp) { /* stable merge sort */        \
        if (a->len < 2) return a;                                                                         \
        T *tmp = malloc(a->len * sizeof(T));                                                              \
        for (size_t w = 1; w < a->len; w *= 2) {                                                          \
            for (size_t lo = 0; lo < a->len; lo += 2 * w) {                                               \
                size_t mid = lo + w < a->len ? lo + w : a->len, hi = lo + 2 * w < a->len ? lo + 2 * w : a->len; \
                size_t i = lo, j = mid, k = lo;                                                           \
                while (i < mid && j < hi) {                                                               \
                    double c = int_cmp ? (double)((int64_t (*)(void *, T, T))cl->fn)(cl, a->data[i], a->data[j]) \
                                       : ((double (*)(void *, T, T))cl->fn)(cl, a->data[i], a->data[j]);  \
                    tmp[k++] = c <= 0 ? a->data[i++] : a->data[j++];                                       \
                }                                                                                         \
                while (i < mid) tmp[k++] = a->data[i++];                                                  \
                while (j < hi) tmp[k++] = a->data[j++];                                                   \
            }                                                                                             \
            memcpy(a->data, tmp, a->len * sizeof(T));                                                     \
        }                                                                                                 \
        free(tmp);                                                                                        \
        return a;                                                                                         \
    }                                                                                                     \
    static name *xs_arr_reverse_##name(name *a) {                                                          \
        for (size_t i = 0, j = a->len ? a->len - 1 : 0; i < j; i++, j--) { T t = a->data[i]; a->data[i] = a->data[j]; a->data[j] = t; } \
        return a;                                                                                         \
    }                                                                                                     \
    static name *xs_arr_slice_##name(name *a, int64_t start, int64_t end) {                                \
        int64_t n = (int64_t)a->len;                                                                      \
        if (start < 0) start += n; if (end < 0) end += n; if (start < 0) start = 0; if (end > n) end = n; \
        name *r = name##_new(0, 0);                                                                       \
        for (int64_t i = start; i < end; i++) name##_push(r, PUSH_OWNED(a->data[i]));                     \
        return r;                                                                                         \
    }

#define XS_ID(x) (x)
XS_ARR_CALLBACKS(xs_arr_num, double, XS_ID)
XS_ARR_CALLBACKS(xs_arr_int, int64_t, XS_ID)
XS_ARR_CALLBACKS(xs_arr_bool, bool, XS_ID)
/* reference arrays: xs_arr_ref_new takes (cap, printer) and pushes take ownership */
XS_NOINLINE void xs_arr_forEach_xs_arr_ref(xs_arr_ref *a, xs_closure *cl, int arity) {
    for (size_t i = 0; i < a->len; i++) {
        if (arity == 2) ((void (*)(void *, void *, int64_t))cl->fn)(cl, a->data[i], (int64_t)i);
        else ((void (*)(void *, void *))cl->fn)(cl, a->data[i]);
    }
}
XS_NOINLINE bool xs_arr_test_xs_arr_ref(xs_closure *cl, int arity, void *v, size_t i) {
    return arity == 2 ? ((bool (*)(void *, void *, int64_t))cl->fn)(cl, v, (int64_t)i) : ((bool (*)(void *, void *))cl->fn)(cl, v);
}
XS_NOINLINE xs_arr_ref *xs_arr_filter_xs_arr_ref(xs_arr_ref *a, xs_closure *cl, int arity) {
    xs_arr_ref *r = xs_arr_ref_new(a->len, a->print_elem);
    for (size_t i = 0; i < a->len; i++) if (xs_arr_test_xs_arr_ref(cl, arity, a->data[i], i)) xs_arr_ref_push(r, xs_retain_(a->data[i]));
    return r;
}
XS_NOINLINE int64_t xs_arr_findIndex_xs_arr_ref(xs_arr_ref *a, xs_closure *cl, int arity) {
    for (size_t i = 0; i < a->len; i++) if (xs_arr_test_xs_arr_ref(cl, arity, a->data[i], i)) return (int64_t)i;
    return -1;
}
XS_NOINLINE void *xs_arr_find_xs_arr_ref(xs_arr_ref *a, xs_closure *cl, int arity) { /* borrowed */
    int64_t i = xs_arr_findIndex_xs_arr_ref(a, cl, arity);
    return i < 0 ? NULL : a->data[i];
}
XS_NOINLINE bool xs_arr_some_xs_arr_ref(xs_arr_ref *a, xs_closure *cl, int arity) { return xs_arr_findIndex_xs_arr_ref(a, cl, arity) >= 0; }
XS_NOINLINE bool xs_arr_every_xs_arr_ref(xs_arr_ref *a, xs_closure *cl, int arity) {
    for (size_t i = 0; i < a->len; i++) if (!xs_arr_test_xs_arr_ref(cl, arity, a->data[i], i)) return false;
    return true;
}
XS_NOINLINE xs_arr_ref *xs_arr_sort_xs_arr_ref(xs_arr_ref *a, xs_closure *cl, int int_cmp) {
    if (a->len < 2) return a;
    void **tmp = malloc(a->len * sizeof(void *));
    for (size_t w = 1; w < a->len; w *= 2) {
        for (size_t lo = 0; lo < a->len; lo += 2 * w) {
            size_t mid = lo + w < a->len ? lo + w : a->len, hi = lo + 2 * w < a->len ? lo + 2 * w : a->len;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) {
                double c = int_cmp ? (double)((int64_t (*)(void *, void *, void *))cl->fn)(cl, a->data[i], a->data[j])
                                   : ((double (*)(void *, void *, void *))cl->fn)(cl, a->data[i], a->data[j]);
                tmp[k++] = c <= 0 ? a->data[i++] : a->data[j++];
            }
            while (i < mid) tmp[k++] = a->data[i++];
            while (j < hi) tmp[k++] = a->data[j++];
        }
        memcpy(a->data, tmp, a->len * sizeof(void *));
    }
    free(tmp);
    return a;
}
static xs_arr_ref *xs_arr_reverse_xs_arr_ref(xs_arr_ref *a) {
    for (size_t i = 0, j = a->len ? a->len - 1 : 0; i < j; i++, j--) { void *t = a->data[i]; a->data[i] = a->data[j]; a->data[j] = t; }
    return a;
}
static xs_arr_ref *xs_arr_slice_xs_arr_ref(xs_arr_ref *a, int64_t start, int64_t end) {
    int64_t n = (int64_t)a->len;
    if (start < 0) start += n; if (end < 0) end += n; if (start < 0) start = 0; if (end > n) end = n;
    xs_arr_ref *r = xs_arr_ref_new(end > start ? (size_t)(end - start) : 0, a->print_elem);
    for (int64_t i = start; i < end; i++) xs_arr_ref_push(r, xs_retain_(a->data[i]));
    return r;
}
#define xs_arr_forEach_num xs_arr_forEach_xs_arr_num
#define xs_arr_forEach_int xs_arr_forEach_xs_arr_int
#define xs_arr_forEach_bool xs_arr_forEach_xs_arr_bool
#define xs_arr_forEach_ref xs_arr_forEach_xs_arr_ref
#define xs_arr_filter_num xs_arr_filter_xs_arr_num
#define xs_arr_filter_int xs_arr_filter_xs_arr_int
#define xs_arr_filter_bool xs_arr_filter_xs_arr_bool
#define xs_arr_filter_ref xs_arr_filter_xs_arr_ref
#define xs_arr_findIndex_num xs_arr_findIndex_xs_arr_num
#define xs_arr_findIndex_int xs_arr_findIndex_xs_arr_int
#define xs_arr_findIndex_bool xs_arr_findIndex_xs_arr_bool
#define xs_arr_findIndex_ref xs_arr_findIndex_xs_arr_ref
#define xs_arr_find_ref xs_arr_find_xs_arr_ref
#define xs_arr_some_num xs_arr_some_xs_arr_num
#define xs_arr_some_int xs_arr_some_xs_arr_int
#define xs_arr_some_bool xs_arr_some_xs_arr_bool
#define xs_arr_some_ref xs_arr_some_xs_arr_ref
#define xs_arr_every_num xs_arr_every_xs_arr_num
#define xs_arr_every_int xs_arr_every_xs_arr_int
#define xs_arr_every_bool xs_arr_every_xs_arr_bool
#define xs_arr_every_ref xs_arr_every_xs_arr_ref
#define xs_arr_sort_num xs_arr_sort_xs_arr_num
#define xs_arr_sort_int xs_arr_sort_xs_arr_int
#define xs_arr_sort_bool xs_arr_sort_xs_arr_bool
#define xs_arr_sort_ref xs_arr_sort_xs_arr_ref
#define xs_arr_reverse_num xs_arr_reverse_xs_arr_num
#define xs_arr_reverse_int xs_arr_reverse_xs_arr_int
#define xs_arr_reverse_bool xs_arr_reverse_xs_arr_bool
#define xs_arr_reverse_ref xs_arr_reverse_xs_arr_ref
#define xs_arr_slice_num xs_arr_slice_xs_arr_num
#define xs_arr_slice_int xs_arr_slice_xs_arr_int
#define xs_arr_slice_bool xs_arr_slice_xs_arr_bool
#define xs_arr_slice_ref xs_arr_slice_xs_arr_ref

/* map: from any kind to any kind */
#define XS_ARR_MAP(from, FT, to, TT, NEW, PUSH, fsfx, tsfx)                                                \
    XS_NOINLINE to *xs_arr_map_##fsfx##_##tsfx(from *a, xs_closure *cl, int arity, void (*pr)(void *)) {        \
        to *r = NEW;                                                                                       \
        for (size_t i = 0; i < a->len; i++) {                                                             \
            TT v = arity == 2 ? ((TT (*)(void *, FT, int64_t))cl->fn)(cl, a->data[i], (int64_t)i) : ((TT (*)(void *, FT))cl->fn)(cl, a->data[i]); \
            PUSH(r, v);                                                                                    \
        }                                                                                                 \
        (void)pr;                                                                                         \
        return r;                                                                                         \
    }
#define XS_MAP_ALL_TO(from, FT, fsfx)                                                                      \
    XS_ARR_MAP(from, FT, xs_arr_num, double, xs_arr_num_new(0, 0), xs_arr_num_push, fsfx, num)             \
    XS_ARR_MAP(from, FT, xs_arr_int, int64_t, xs_arr_int_new(0, 0), xs_arr_int_push, fsfx, int)            \
    XS_ARR_MAP(from, FT, xs_arr_bool, bool, xs_arr_bool_new(0, false), xs_arr_bool_push, fsfx, bool)       \
    XS_ARR_MAP(from, FT, xs_arr_ref, void *, xs_arr_ref_new(a->len, pr), xs_arr_ref_push, fsfx, ref)
XS_MAP_ALL_TO(xs_arr_num, double, num)
XS_MAP_ALL_TO(xs_arr_int, int64_t, int)
XS_MAP_ALL_TO(xs_arr_bool, bool, bool)
XS_MAP_ALL_TO(xs_arr_ref, void *, ref)
/* (`bool` is a macro of <stdbool.h>: the names pasted with it came out as _Bool; the emitter writes bool) */
#define xs_arr_map_bool_num xs_arr_map__Bool_num
#define xs_arr_map_bool_int xs_arr_map__Bool_int
#define xs_arr_map_bool_bool xs_arr_map__Bool_bool
#define xs_arr_map_bool_ref xs_arr_map__Bool_ref

/* indexOf / join by value */
static int64_t xs_arr_index_of_num(xs_arr_num *a, double v) {
    if (xs_isnull_num(v)) { /* null in a (number | null)[]: the sentinel, by its bits */
        for (size_t i = 0; i < a->len; i++) if (xs_isnull_num(a->data[i])) return (int64_t)i;
        return -1;
    }
    for (size_t i = 0; i < a->len; i++) if (a->data[i] == v) return (int64_t)i;
    return -1;
}
static int64_t xs_arr_index_of_int(xs_arr_int *a, int64_t v) { for (size_t i = 0; i < a->len; i++) if (a->data[i] == v) return (int64_t)i; return -1; }
static int64_t xs_arr_index_of_bool(xs_arr_bool *a, bool v) { for (size_t i = 0; i < a->len; i++) if (a->data[i] == v) return (int64_t)i; return -1; }
/* strings by content; in a (string | null)[] null matches null */
/* (unknown[]).includes(v) / indexOf(v): === as JavaScript's (strings and boxed numbers by value, objects by identity) */
XS_NOINLINE int64_t xs_arr_index_of_ref(xs_arr_ref *a, void *v) {
    for (size_t i = 0; i < a->len; i++) if (a->data[i] == v || xs_dyn_eq(a->data[i], v)) return (int64_t)i;
    return -1;
}
static int64_t xs_arr_index_of_str(xs_arr_ref *a, xs_str *v) {
    for (size_t i = 0; i < a->len; i++) {
        xs_str *x = a->data[i];
        if (x == v || (x && v && xs_str_eq(x, v))) return (int64_t)i;
    }
    return -1;
}
static xs_str *xs_arr_join_generic(size_t len, xs_str *sep, void (*item)(xs_sb *, void *, size_t), void *ctx) {
    xs_sb b = {0};
    for (size_t i = 0; i < len; i++) {
        if (i) { if (sep) xs_sb_put(&b, sep->data, (size_t)sep->len); else xs_sb_put(&b, ",", 1); }
        item(&b, ctx, i);
    }
    return xs_sb_finish(&b);
}
static void xs_join_num(xs_sb *b, void *ctx, size_t i) { char buf[32]; xs_num_to_str(((xs_arr_num *)ctx)->data[i], buf); if (((xs_arr_num *)ctx)->data[i] == 0) { buf[0] = '0'; buf[1] = 0; } xs_sb_puts(b, buf); }
static void xs_join_int(xs_sb *b, void *ctx, size_t i) { xs_sb_int(b, ((xs_arr_int *)ctx)->data[i]); }
static void xs_join_bool(xs_sb *b, void *ctx, size_t i) { xs_sb_bool(b, ((xs_arr_bool *)ctx)->data[i]); }
static void xs_join_str(xs_sb *b, void *ctx, size_t i) { xs_str *s = ((xs_arr_ref *)ctx)->data[i]; if (s) xs_sb_put(b, s->data, (size_t)s->len); } /* null joins as "" */
static xs_str *xs_arr_join_num(xs_arr_num *a, xs_str *sep) { return xs_arr_join_generic(a->len, sep, xs_join_num, a); }
static xs_str *xs_arr_join_int(xs_arr_int *a, xs_str *sep) { return xs_arr_join_generic(a->len, sep, xs_join_int, a); }
static xs_str *xs_arr_join_bool(xs_arr_bool *a, xs_str *sep) { return xs_arr_join_generic(a->len, sep, xs_join_bool, a); }
static xs_str *xs_arr_join_str(xs_arr_ref *a, xs_str *sep) { return xs_arr_join_generic(a->len, sep, xs_join_str, a); }
/* an unknown[] / any[] (or another array of references): each value as String(x), null as "" (JavaScript's join) */
static void xs_join_ref(xs_sb *b, void *ctx, size_t i) {
    void *v = ((xs_arr_ref *)ctx)->data[i];
    if (!v) return;
    xs_str *s = xs_dyn_str(v);
    xs_sb_put(b, s->data, (size_t)s->len);
    xs_release(s);
}
static xs_str *xs_arr_join_ref(xs_arr_ref *a, xs_str *sep) { return xs_arr_join_generic(a->len, sep, xs_join_ref, a); }
/* (number | null)[] and (int | null)[]: a null slot joins as "", as in JavaScript */
static void xs_join_num_n(xs_sb *b, void *ctx, size_t i) { if (!xs_isnull_num(((xs_arr_num *)ctx)->data[i])) xs_join_num(b, ctx, i); }
static void xs_join_int_n(xs_sb *b, void *ctx, size_t i) { if (((xs_arr_int *)ctx)->data[i] != XS_NULL_INT) xs_join_int(b, ctx, i); }
static xs_str *xs_arr_join_num_n(xs_arr_num *a, xs_str *sep) { return xs_arr_join_generic(a->len, sep, xs_join_num_n, a); }
static xs_str *xs_arr_join_int_n(xs_arr_int *a, xs_str *sep) { return xs_arr_join_generic(a->len, sep, xs_join_int_n, a); }

/* ---- int: an integer JavaScript represents exactly, |v| <= 2^53 - 1 (Number.MAX_SAFE_INTEGER); leaving that range
   is a runtime error (XB-075): there Node would round the result and print another number. Every int source keeps
   the range (literals in the lexer, conversions in xs_to_int_s, the operations below), so a sum or difference of two
   ints cannot overflow 64 bits and needs only the range test; a product is tested for 64-bit overflow too. */

/* The _s forms take the source position of the operation (the compiler passes &xs_sitetab[n]); it is only
   read on the error path. */
/* with a constant operand the test is on the other operand, before the operation: a + b leaves the range iff a
   passes MAX - b (one compare against a constant the compiler hoists; testing the result made clang rebuild the
   constant inside fannkuch's flip loop: +7% on the Xeon) */
XS_AI int64_t xs_iadd_s(int64_t a, int64_t b, const xs_site *s) {
    if (__builtin_constant_p(a) && !__builtin_constant_p(b)) { int64_t t = a; a = b; b = t; }
    if (__builtin_constant_p(b)) { if (__builtin_expect(XS_IADD_C_OUT(a, b), 0)) XS_FAIL(XS_IOVF_MSG, s); return a + b; }
    int64_t r = (int64_t)((uint64_t)a + (uint64_t)b);
    if (__builtin_expect(XS_IOUT(r), 0)) XS_FAIL(XS_IOVF_MSG, s);
    return r;
}
XS_AI int64_t xs_isub_s(int64_t a, int64_t b, const xs_site *s) {
    if (__builtin_constant_p(b)) { if (__builtin_expect(XS_IADD_C_OUT(a, -(b)), 0)) XS_FAIL(XS_IOVF_MSG, s); return a - b; }
    int64_t r = (int64_t)((uint64_t)a - (uint64_t)b);
    if (__builtin_expect(XS_IOUT(r), 0)) XS_FAIL(XS_IOVF_MSG, s);
    return r;
}
XS_AI int64_t xs_imul_s(int64_t a, int64_t b, const xs_site *s) {
    if (__builtin_constant_p(a) && !__builtin_constant_p(b)) { int64_t t = a; a = b; b = t; }
    if (__builtin_constant_p(b) && b > -1024 && b < 1024) { /* |a| <= 2^53, |b| < 2^10: the product fits 64 bits */
        int64_t r = a * b;
        if (__builtin_expect(XS_IOUT(r), 0)) XS_FAIL(XS_IOVF_MSG, s);
        return r;
    }
    int64_t r;
    if (__builtin_expect(__builtin_mul_overflow(a, b, &r) | XS_IOUT(r), 0)) XS_FAIL(XS_IOVF_MSG, s);
    return r;
}
XS_AI int64_t xs_ineg_s(int64_t a, const xs_site *s) { int64_t r = -a; if (__builtin_expect(XS_IOUT_HI(r), 0)) XS_FAIL(XS_IOVF_MSG, s); return r; } /* (-(-2^53)) */
/* both operands >= 0 (the emitter proved it): only the top of the range can be crossed */
/* only the 64-bit overflow: the caller tests the range once on the final value (emit.c, spine: a sum of terms >= 0
   rises, so a partial sum out of the range leaves the total out of it too) */
XS_AI int64_t xs_iadd_w_s(int64_t a, int64_t b, const xs_site *s) { int64_t r; if (__builtin_expect(__builtin_add_overflow(a, b, &r), 0)) XS_FAIL(XS_IOVF_MSG, s); return r; }
XS_AI int64_t xs_iadd_nn_s(int64_t a, int64_t b, const xs_site *s) { int64_t r = a + b; if (__builtin_expect(XS_IOUT_NN(r), 0)) XS_FAIL(XS_IOVF_MSG, s); return r; }
XS_AI int64_t xs_imul_nn_s(int64_t a, int64_t b, const xs_site *s) {
    if (__builtin_constant_p(a) && !__builtin_constant_p(b)) { int64_t t = a; a = b; b = t; }
    if (__builtin_constant_p(b) && b >= 0 && b < 1024) { int64_t r = a * b; if (__builtin_expect(XS_IOUT_HI(r), 0)) XS_FAIL(XS_IOVF_MSG, s); return r; }
    int64_t r;
    if (__builtin_expect(__builtin_mul_overflow(a, b, &r) | XS_IOUT_HI(r), 0)) XS_FAIL(XS_IOVF_MSG, s);
    return r;
}
XS_AI int64_t xs_imod_s(int64_t a, int64_t b, const xs_site *s) {
    if (b == 0) XS_FAIL("integer modulo by zero", s);
    if (b == -1) return 0;
    return a % b;
}
XS_AI double xs_idiv_s(int64_t a, int64_t b, const xs_site *s) { (void)s; return (double)a / (double)b; }
XS_AI int64_t xs_iadd_to_s(int64_t *p, int64_t b, const xs_site *s) { return *p = xs_iadd_s(*p, b, s); }
/* a loop counter whose range test waits for the loop's end (emit.c, dctr): it only rises, one by one */
XS_AI void xs_ichk_hi_s(int64_t v, const xs_site *s) { if (__builtin_expect(XS_IOUT_HI(v), 0)) XS_FAIL(XS_IOVF_MSG, s); }
XS_AI int64_t xs_isub_to_s(int64_t *p, int64_t b, const xs_site *s) { return *p = xs_isub_s(*p, b, s); }
XS_AI int64_t xs_imul_to_s(int64_t *p, int64_t b, const xs_site *s) { return *p = xs_imul_s(*p, b, s); }
XS_AI int64_t xs_ipostinc_s(int64_t *p, const xs_site *s) { int64_t old = *p; *p = xs_iadd_s(old, 1, s); return old; }
XS_AI int64_t xs_ipostdec_s(int64_t *p, const xs_site *s) { int64_t old = *p; *p = xs_isub_s(old, 1, s); return old; }
static inline int64_t xs_iadd(int64_t a, int64_t b) { return xs_iadd_s(a, b, NULL); }
static inline int64_t xs_isub(int64_t a, int64_t b) { return xs_isub_s(a, b, NULL); }
static inline int64_t xs_imul(int64_t a, int64_t b) { return xs_imul_s(a, b, NULL); }
static inline int64_t xs_ineg(int64_t a) { return xs_ineg_s(a, NULL); }
static inline int64_t xs_imod(int64_t a, int64_t b) { return xs_imod_s(a, b, NULL); }
static inline double xs_idiv(int64_t a, int64_t b) { return (double)a / (double)b; } /* int / int is number, as in JS */
/* jam (XB-083): an integer computed in a double; *bad notes a value outside ±(2^53 − 1) (the int would have failed) */
static inline double xs_jam_ck(bool *bad, double r) { *bad |= !(__builtin_fabs(r) <= 9007199254740991.0); return r; }
static inline int64_t xs_iadd_to(int64_t *p, int64_t b) { return *p = xs_iadd(*p, b); }
static inline int64_t xs_isub_to(int64_t *p, int64_t b) { return *p = xs_isub(*p, b); }
static inline int64_t xs_imul_to(int64_t *p, int64_t b) { return *p = xs_imul(*p, b); }
static inline int64_t xs_ipostinc(int64_t *p) { int64_t old = *p; *p = xs_iadd(old, 1); return old; }
static inline int64_t xs_ipostdec(int64_t *p) { int64_t old = *p; *p = xs_isub(old, 1); return old; }
static inline int64_t xs_iabs(int64_t a) { return a < 0 ? xs_ineg(a) : a; }
static inline int64_t xs_imin(int64_t a, int64_t b) { return a < b ? a : b; }
static inline int64_t xs_imax(int64_t a, int64_t b) { return a > b ? a : b; }
static inline double xs_to_number(int64_t a) { return (double)a; }
static bool xs_is_finite(double x) { return isfinite(x); }
static inline bool xs_is_nan(double x) { return x != x; }
static inline bool xs_is_integer(double x) { return x == x && !isinf(x) && x == floor(x); }
/* Math.cbrt as V8 has it (FreeBSD's s_cbrt.c, fdlibm): glibc's cbrt is not correctly rounded (cbrt(27) is
   3.0000000000000004 there), macOS's and V8's give 3. A polynomial to 23 bits, then one Newton step. */
static double xs_cbrt(double x) {
    union { double d; uint64_t u; } v = { x };
    uint32_t hx = (uint32_t)(v.u >> 32), lo = (uint32_t)v.u, sign = hx & 0x80000000u;
    hx &= 0x7fffffffu;
    if (hx >= 0x7ff00000u) return x + x; /* NaN, Infinity */
    union { double d; uint64_t u; } t;
    if (hx < 0x00100000u) { /* zero or subnormal */
        if ((hx | lo) == 0) return x;
        t.u = (uint64_t)0x43500000u << 32; /* 2^54 */
        t.d *= x;
        uint32_t high = (uint32_t)(t.u >> 32);
        t.u = (uint64_t)(sign | ((high & 0x7fffffffu) / 3 + 696219795u)) << 32;
    } else {
        t.u = (uint64_t)(sign | (hx / 3 + 715094163u)) << 32;
    }
    double r = (t.d * t.d) * (t.d / x);
    t.d = t.d * ((1.87595182427177009643 + r * (-1.88497979543377169875 + r * 1.621429720105354466140)) +
                 ((r * r) * r) * (-0.758397934778766047437 + r * 0.145996192886612446982));
    t.u = (t.u + 0x80000000u) & 0xffffffffc0000000ull; /* rounded away from zero to 23 bits */
    double s2 = t.d * t.d;
    r = x / s2;
    double w = t.d + t.d;
    r = (r - t.d) / (w + r);
    return t.d + t.d * r;
}
/* Math.min / Math.max of two numbers as JavaScript has them: NaN if either is, and -0 below 0 */
static inline double xs_fmin(double a, double b) {
    if (a != a || b != b) return NAN;
    if (a == b) return signbit(a) ? a : b;
    return a < b ? a : b;
}
static inline double xs_fmax(double a, double b) {
    if (a != a || b != b) return NAN;
    if (a == b) return signbit(a) ? b : a;
    return a > b ? a : b;
}
static inline bool xs_is_safe_integer(double x) { return xs_is_integer(x) && fabs(x) <= 9007199254740991.0; }

XS_AI int64_t xs_to_int_s(double x, const xs_site *s) {
    if (!(x >= -9007199254740991.0 && x <= 9007199254740991.0)) XS_FAIL("value out of int range: an int is an integer of JavaScript's safe range (+-2^53 - 1); use number or bigint", s);
    return (int64_t)x;
}
static inline int64_t xs_to_int(double x) { return xs_to_int_s(x, NULL); }
static inline int64_t xs_floor_int(double x) { return xs_to_int(floor(x)); }
static inline int64_t xs_ceil_int(double x) { return xs_to_int(ceil(x)); }
static inline int64_t xs_trunc_int(double x) { return xs_to_int(trunc(x)); }
static int64_t xs_trunc_int_or_null(double d) { return xs_isnull_num(d) ? XS_NULL_INT : xs_trunc_int(d); } /* number | null where int | null goes */
static inline int64_t xs_round_int(double x) {
    double r = floor(x);
    if (x - r >= 0.5) r += 1;
    return xs_to_int(r);
}
XS_AI int64_t xs_floor_int_s(double x, const xs_site *s) { return xs_to_int_s(floor(x), s); }
XS_AI int64_t xs_ceil_int_s(double x, const xs_site *s) { return xs_to_int_s(ceil(x), s); }
XS_AI int64_t xs_trunc_int_s(double x, const xs_site *s) { return xs_to_int_s(trunc(x), s); }
XS_AI int64_t xs_round_int_s(double x, const xs_site *s) {
    double r = floor(x);
    if (x - r >= 0.5) r += 1;
    return xs_to_int_s(r, s);
}
/* XB-087: int operations as doubles where the value is read as a number, so a -0 stays (0 * -3, -4 % 2): the operands
   are integers of the safe range, so the double result is exact while it is inside the range, and past it is 2^53 or
   more: the same test, the same error as the int's */
#define XS_DOUT(r) (!(__builtin_fabs(r) <= 9007199254740991.0))
XS_AI double xs_dmul_s(double a, double b, const xs_site *s) { double r = a * b; if (__builtin_expect(XS_DOUT(r), 0)) XS_FAIL(XS_IOVF_MSG, s); return r; }
XS_AI double xs_dadd_s(double a, double b, const xs_site *s) { double r = a + b; if (__builtin_expect(XS_DOUT(r), 0)) XS_FAIL(XS_IOVF_MSG, s); return r; }
XS_AI double xs_dsub_s(double a, double b, const xs_site *s) { double r = a - b; if (__builtin_expect(XS_DOUT(r), 0)) XS_FAIL(XS_IOVF_MSG, s); return r; }
XS_AI double xs_dmod_s(double a, double b, const xs_site *s) { if (b == 0) XS_FAIL("integer modulo by zero", s); return fmod(a, b); }
/* XB-087: the same, as the double JavaScript gives (Math.trunc(-0.5) is -0), range-checked like the int */
XS_AI double xs_trunc_d_s(double x, const xs_site *s) { double r = trunc(x); (void)xs_to_int_s(r, s); return r; }
XS_AI double xs_floor_d_s(double x, const xs_site *s) { double r = floor(x); (void)xs_to_int_s(r, s); return r; }
XS_AI double xs_ceil_d_s(double x, const xs_site *s) { double r = ceil(x); (void)xs_to_int_s(r, s); return r; }
XS_AI double xs_round_d_s(double x, const xs_site *s) {
    double r = floor(x);
    if (x - r >= 0.5) r += 1;
    if (r == 0 && (x < 0 || __builtin_signbit(x))) r = -0.0;
    (void)xs_to_int_s(r, s);
    return r;
}
XS_AI int64_t xs_iabs_s(int64_t a, const xs_site *s) { return a < 0 ? xs_ineg_s(a, s) : a; }

/* ---- number formatting: ECMAScript Number::toString ---- */

static void xs_num_to_str(double v, char *out /* >= 32 bytes */) {
    if (v != v) { strcpy(out, "NaN"); return; }
    if (v == 0) { strcpy(out, signbit(v) ? "-0" : "0"); return; } /* console.log shows -0 */
    if (isinf(v)) { strcpy(out, v > 0 ? "Infinity" : "-Infinity"); return; }
    char *o = out;
    if (v < 0) { *o++ = '-'; v = -v; }
    char tmp[40];
    /* the shortest digit string that round-trips. When v's interval (the reals that round to it) is symmetric,
       rounding to more digits can only come closer to v, so "p digits round-trip" holds from some p on: a binary
       search over 1..17 finds the least (4 or 5 tries instead of up to 17). At a power of two (zero mantissa) the
       interval below is half as wide as above, and there the digits are tried one by one, as before. */
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    if (bits & 0xFFFFFFFFFFFFFull) {
        int lo = 1, hi = 17;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            snprintf(tmp, sizeof tmp, "%.*e", mid - 1, v);
            if (strtod(tmp, NULL) == v) hi = mid; else lo = mid + 1;
        }
        snprintf(tmp, sizeof tmp, "%.*e", lo - 1, v);
    } else {
        for (int prec = 1; prec <= 17; prec++) {
            snprintf(tmp, sizeof tmp, "%.*e", prec - 1, v);
            if (strtod(tmp, NULL) == v) break;
            /* a tie: printf rounds it to the even digit, which can fall below the narrow lower half of a power
               of two's interval; the digit one above, inside it, is the one JavaScript writes (2^-24:
               5.960464477539063e-8) */
            char up[40];
            strcpy(up, tmp);
            char *e = strchr(up, 'e');
            int d = (int)(e - up) - 1;
            while (d >= 0 && (up[d] == '9' || up[d] == '.')) { if (up[d] == '9') up[d] = '0'; d--; }
            if (d >= 0) {
                up[d]++;
                if (strtod(up, NULL) == v) { strcpy(tmp, up); break; }
            }
        }
    }
    char digits[24];
    int k = 0;
    char *p = tmp;
    while (*p && *p != 'e') {
        if (*p != '.') digits[k++] = *p;
        p++;
    }
    while (k > 1 && digits[k - 1] == '0') k--;
    digits[k] = 0;
    int n = atoi(p + 1) + 1;
    if (k <= n && n <= 21) {
        memcpy(o, digits, (size_t)k); o += k;
        for (int i = 0; i < n - k; i++) *o++ = '0';
        *o = 0;
    } else if (0 < n && n <= 21) {
        memcpy(o, digits, (size_t)n); o += n;
        *o++ = '.';
        memcpy(o, digits + n, (size_t)(k - n)); o += k - n;
        *o = 0;
    } else if (-6 < n && n <= 0) {
        *o++ = '0'; *o++ = '.';
        for (int i = 0; i < -n; i++) *o++ = '0';
        memcpy(o, digits, (size_t)k); o += k;
        *o = 0;
    } else {
        int e = n - 1;
        *o++ = digits[0];
        if (k > 1) { *o++ = '.'; memcpy(o, digits + 1, (size_t)(k - 1)); o += k - 1; }
        *o++ = 'e';
        *o++ = e < 0 ? '-' : '+';
        int ae = e < 0 ? -e : e; /* at most 324: three digits */
        if (ae >= 100) *o++ = (char)('0' + ae / 100);
        if (ae >= 10) *o++ = (char)('0' + ae / 10 % 10);
        *o++ = (char)('0' + ae % 10);
        *o = 0;
    }
}

static xs_str *xs_str_from_num(double v) {
    char buf[32];
    xs_num_to_str(v, buf);
    if (v == 0) buf[0] = '0', buf[1] = 0; /* String(-0) is "0" */
    return xs_str_new(buf, strlen(buf));
}

/* n.toString(radix), 2..36, as JavaScript writes integers */
XS_NOINLINE xs_str *xs_int_to_radix(int64_t v, int64_t radix) {
    if (radix < 2 || radix > 36) xs_panic("RangeError: toString() radix must be between 2 and 36");
    char buf[72];
    int n = 0;
    uint64_t u = v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    do { int d = (int)(u % (uint64_t)radix); buf[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (uint64_t)radix; } while (u);
    if (v < 0) buf[n++] = '-';
    char out[72];
    for (int i = 0; i < n; i++) out[i] = buf[n - 1 - i];
    return xs_str_new(out, (size_t)n);
}
static xs_str *xs_str_from_int(int64_t v) {
    char buf[24];
    size_t n = xs_itoa(v, buf);
    return xs_str_new(buf, n);
}

/* a + b + String(i) + ... and template literals: one allocation for the whole text, numbers written in place */
typedef struct { int k; union { xs_str *s; int64_t i; double d; bool b; }; } xs_part; /* k: 0 string, 1 int, 2 number, 3 boolean */
static inline int xs_udigits(uint64_t u) {
    int n = 1;
    while (u >= 10000) { u /= 10000; n += 4; }
    return n + (u >= 10) + (u >= 100) + (u >= 1000);
}
/* the digits of u, ending at end (two at a time) */
static inline void xs_uwrite(char *end, uint64_t u) {
    static const char d2[201] = "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
                                "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
                                "8081828384858687888990919293949596979899";
    while (u >= 100) { unsigned q = (unsigned)(u % 100); u /= 100; end -= 2; end[0] = d2[2 * q]; end[1] = d2[2 * q + 1]; }
    if (u >= 10) { end -= 2; end[0] = d2[2 * u]; end[1] = d2[2 * u + 1]; }
    else *--end = (char)('0' + u);
}
/* a string in a buffer of its creator's frame (src/stackalloc.c: it dies with a stack object): counted like any
   other, XS_STACK leaves the memory alone when the count reaches zero */
static inline __attribute__((always_inline)) xs_str *xs_str_on_stack(void *buf, int64_t len) {
    xs_str *s = buf;
    s->hdr.rc = 1;
    s->hdr.cls = XS_STACK;
    xs_set_type(&s->hdr, &xs_str_type);
    s->len = len;
    s->ulen = -1;
    xs_live++;
    return s;
}
static inline __attribute__((always_inline)) xs_str *xs_cat_into(void *sbuf, size_t scap, int n, const xs_part *p);
/* xs_cat_into: inlined where the parts are known (short templates): the kinds fold away and the loops unroll;
   with a buffer that holds the result, the string lives there */
static inline __attribute__((always_inline)) xs_str *xs_cat_into(void *sbuf, size_t scap, int n, const xs_part *p) {
    /* numbers (doubles) are written into a buffer first; integers and booleans go straight into the result */
    char dbuf[4][32], (*num)[32] = dbuf;
    int ndbl = 0;
    for (int i = 0; i < n; i++) ndbl += p[i].k == 2;
    if (ndbl > 4) num = malloc(sizeof(char[32]) * (size_t)ndbl);
    int64_t total = 0, ulen = 0; /* ulen < 0 once a string part's length in code points is not known */
    for (int i = 0, d = 0; i < n; i++) {
        int64_t l;
        switch (p[i].k) {
        case 0: total += p[i].s->len; if (ulen >= 0) ulen = p[i].s->ulen >= 0 ? ulen + p[i].s->ulen : -1; continue;
        case 1: { int64_t v = p[i].i; l = xs_udigits(v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v) + (v < 0); break; }
        case 2: if (p[i].d == 0) { num[d][0] = '0'; num[d][1] = 0; l = 1; } else { xs_num_to_str(p[i].d, num[d]); l = (int64_t)strlen(num[d]); } d++; break;
        default: l = p[i].b ? 4 : 5; break;
        }
        total += l;
        if (ulen >= 0) ulen += l; /* digits, signs, letters: one each */
    }
    xs_str *r = sbuf && offsetof(xs_str, data) + (size_t)total + 1 <= scap ? xs_str_on_stack(sbuf, total) : xs_str_alloc_i(total);
    char *o = r->data;
    for (int i = 0, d = 0; i < n; i++) {
        const char *src;
        size_t l;
        switch (p[i].k) {
        case 0: src = p[i].s->data; l = (size_t)p[i].s->len; break;
        case 1: {
            int64_t v = p[i].i;
            uint64_t u = v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
            if (v < 0) *o++ = '-';
            o += xs_udigits(u);
            xs_uwrite(o, u);
            continue;
        }
        case 2: src = num[d++]; l = strlen(src); break;
        default: src = p[i].b ? "true" : "false"; l = p[i].b ? 4 : 5; break;
        }
        if (l <= 16) for (size_t k = 0; k < l; k++) o[k] = src[k]; /* short pieces: no call */
        else memcpy(o, src, l);
        o += l;
    }
    *o = 0;
    if (ulen >= 0 && ulen <= INT32_MAX) r->ulen = (int32_t)ulen;
    if (num != dbuf) free(num);
    return r;
}
static inline __attribute__((always_inline)) xs_str *xs_cat_n(int n, const xs_part *p) { return xs_cat_into(NULL, 0, n, p); }
XS_NOINLINE xs_str *xs_cat(int n, const xs_part *p) { return xs_cat_n(n, p); }

/* parseInt(s, radix): leading space, a sign, 0x for radix 16 (or 0), the longest run of digits; NaN when none */
XS_NOINLINE double xs_parse_int(xs_str *s, int64_t radix) {
    const char *p = s->data, *e = s->data + s->len;
    while (p < e && (*p == ' ' || (*p >= 9 && *p <= 13) || ((unsigned char)p[0] == 0xC2 && p + 1 < e && (unsigned char)p[1] == 0xA0))) p += (unsigned char)*p == 0xC2 ? 2 : 1;
    bool neg = false;
    if (p < e && (*p == '+' || *p == '-')) neg = *p++ == '-';
    if (radix != 0 && (radix < 2 || radix > 36)) return NAN;
    if ((radix == 0 || radix == 16) && e - p >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { p += 2; radix = 16; }
    if (radix == 0) radix = 10;
    const char *d0 = p;
    double v = 0;
    while (p < e) {
        int c = *p, k = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 99;
        if (k >= radix) break;
        v = v * (double)radix + k;
        p++;
    }
    if (p == d0) return NAN;
    if (radix == 10 && p - d0 > 15) { /* exact rounding of long decimal runs */
        char buf[400];
        size_t n = (size_t)(p - d0) < sizeof buf - 1 ? (size_t)(p - d0) : sizeof buf - 1;
        memcpy(buf, d0, n);
        buf[n] = 0;
        v = strtod(buf, NULL);
    }
    return neg ? -v : v;
}
/* parseFloat(s): the longest prefix that is a decimal literal (or Infinity); NaN when none */
XS_NOINLINE double xs_parse_float(xs_str *s) {
    const char *p = s->data, *e = s->data + s->len;
    while (p < e && (*p == ' ' || (*p >= 9 && *p <= 13))) p++;
    const char *st = p;
    if (p < e && (*p == '+' || *p == '-')) p++;
    if (e - p >= 8 && memcmp(p, "Infinity", 8) == 0) return *st == '-' ? -INFINITY : INFINITY;
    const char *ds = p;
    while (p < e && *p >= '0' && *p <= '9') p++;
    if (p < e && *p == '.') { p++; while (p < e && *p >= '0' && *p <= '9') p++; }
    if (p == ds || (p == ds + 1 && *ds == '.')) return NAN;
    if (p < e && (*p == 'e' || *p == 'E')) {
        const char *q = p + 1;
        if (q < e && (*q == '+' || *q == '-')) q++;
        if (q < e && *q >= '0' && *q <= '9') { while (q < e && *q >= '0' && *q <= '9') q++; p = q; }
    }
    char buf[512];
    size_t n = (size_t)(p - st) < sizeof buf - 1 ? (size_t)(p - st) : sizeof buf - 1;
    memcpy(buf, st, n);
    buf[n] = 0;
    return strtod(buf, NULL);
}
static xs_str *xs_str_from_bool(bool b) { return b ? xs_str_new("true", 4) : xs_str_new("false", 5); }

static double xs_str_to_number(xs_str *s) {
    const char *p = s->data, *end = s->data + s->len;
    while (p < end && isspace((unsigned char)*p)) p++;
    while (end > p && isspace((unsigned char)end[-1])) end--;
    if (p == end) return 0;
    char *stop;
    double v = strtod(p, &stop);
    if (stop != end) return NAN;
    return v;
}

/* ---- JSON ---- */

typedef enum { XJ_NULL, XJ_BOOL, XJ_NUM, XJ_STR, XJ_ARR, XJ_OBJ } xs_jkind;
typedef struct xs_jv {
    xs_jkind kind;
    double num;
    bool b;
    char *str; size_t slen;
    struct xs_jv **items; size_t n;   /* array items, or object values */
    char **keys;                      /* object keys */
} xs_jv;

typedef struct { const char *p, *end, *start; int depth; } xs_jp;
static XS_TLS bool xs_json_lenient; /* SQLite rows: booleans are stored as 0/1 */

/* Every node and string of one parse comes from an arena, so a failure halfway (which longjmps out
   through xs_panic) frees everything in one go instead of leaking the partial tree. */
typedef struct xs_jarena { struct xs_jarena *next; size_t used, cap; char data[]; } xs_jarena;
static XS_TLS xs_jarena *xs_jarena_cur;

static void *xs_jalloc(size_t n) {
    n = (n + 15) & ~(size_t)15;
    xs_jarena *a = xs_jarena_cur;
    if (!a || a->used + n > a->cap) {
        size_t cap = n > 65536 ? n : 65536;
        xs_jarena *b = malloc(sizeof *b + cap);
        if (!b) xs_panic("out of memory");
        b->next = a;
        b->used = 0;
        b->cap = cap;
        xs_jarena_cur = b;
        a = b;
    }
    void *p = a->data + a->used;
    a->used += n;
    memset(p, 0, n);
    return p;
}

/* keeps one block for the next use: row-by-row conversions reuse it instead of malloc/free per row */
static void xs_jarena_reset(void) {
    if (!xs_jarena_cur) return;
    while (xs_jarena_cur->next) {
        xs_jarena *n = xs_jarena_cur->next;
        free(xs_jarena_cur);
        xs_jarena_cur = n;
    }
    xs_jarena_cur->used = 0;
}

static void xs_jarena_free(void) {
    while (xs_jarena_cur) {
        xs_jarena *n = xs_jarena_cur->next;
        free(xs_jarena_cur);
        xs_jarena_cur = n;
    }
}

XS_NOINLINE void xs_json_fail(xs_jp *j, const char *what) {
    static XS_TLS char buf[256];
    snprintf(buf, sizeof buf, "JSON: %s at offset %ld", what, (long)(j->p - j->start));
    xs_jarena_free();
    xs_panic(buf);
}

static void xs_json_ws(xs_jp *j) { while (j->p < j->end && isspace((unsigned char)*j->p)) j->p++; }

static xs_jv *xs_json_value(xs_jp *j);

static xs_jv *xs_jv_new(xs_jkind k) {
    xs_jv *v = xs_jalloc(sizeof *v);
    v->kind = k;
    return v;
}

/* frees the whole parse (the tree is arena-allocated) */
static void xs_jv_free(xs_jv *v) { (void)v; xs_jarena_free(); }

/* grow an arena array: copy into a bigger arena block (no realloc in arenas) */
static void *xs_jgrow(void *old, size_t old_n, size_t new_n, size_t elem) {
    void *p = xs_jalloc(new_n * elem);
    if (old) memcpy(p, old, old_n * elem);
    return p;
}

XS_NOINLINE char *xs_json_string(xs_jp *j, size_t *outlen) {
    if (j->p >= j->end || *j->p != '"') xs_json_fail(j, "expected a string");
    j->p++;
    size_t cap = 32, n = 0;
    char *out = xs_jalloc(cap);
    for (;;) {
        if (j->p >= j->end) xs_json_fail(j, "unterminated string");
        unsigned char c = (unsigned char)*j->p++;
        if (c == '"') break;
        if (n + 4 >= cap) { out = xs_jgrow(out, n, cap * 2, 1); cap *= 2; }
        if (c == '\\') {
            if (j->p >= j->end) xs_json_fail(j, "bad escape");
            char e = *j->p++;
            switch (e) {
            case 'n': out[n++] = '\n'; break;
            case 't': out[n++] = '\t'; break;
            case 'r': out[n++] = '\r'; break;
            case 'b': out[n++] = '\b'; break;
            case 'f': out[n++] = '\f'; break;
            case '/': out[n++] = '/'; break;
            case '\\': out[n++] = '\\'; break;
            case '"': out[n++] = '"'; break;
            case 'u': {
                if (j->end - j->p < 4) xs_json_fail(j, "bad \\u escape");
                unsigned cp = 0;
                for (int k = 0; k < 4; k++) {
                    char h = *j->p++;
                    int hv = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
                    if (hv < 0) xs_json_fail(j, "bad \\u escape");
                    cp = cp * 16 + (unsigned)hv;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF && j->end - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u') {
                    char hex4[5] = {j->p[2], j->p[3], j->p[4], j->p[5], 0};
                    unsigned lo = (unsigned)strtoul(hex4, NULL, 16);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); j->p += 6; }
                }
                if (cp < 0x80) out[n++] = (char)cp;
                else if (cp < 0x800) { out[n++] = (char)(0xC0 | (cp >> 6)); out[n++] = (char)(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { out[n++] = (char)(0xE0 | (cp >> 12)); out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[n++] = (char)(0x80 | (cp & 0x3F)); }
                else { out[n++] = (char)(0xF0 | (cp >> 18)); out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[n++] = (char)(0x80 | (cp & 0x3F)); }
                break;
            }
            default: xs_json_fail(j, "bad escape");
            }
        } else {
            out[n++] = (char)c;
        }
    }
    out[n] = 0;
    *outlen = n;
    return out;
}

XS_NOINLINE xs_jv *xs_json_value(xs_jp *j) {
    xs_json_ws(j);
    if (j->p >= j->end) xs_json_fail(j, "unexpected end");
    if (++j->depth > 200) xs_json_fail(j, "too deep");
    char c = *j->p;
    xs_jv *v;
    if (c == '{') {
        j->p++;
        v = xs_jv_new(XJ_OBJ);
        size_t cap = 0;
        xs_json_ws(j);
        if (j->p < j->end && *j->p == '}') { j->p++; j->depth--; return v; }
        for (;;) {
            xs_json_ws(j);
            size_t klen;
            char *key = xs_json_string(j, &klen);
            xs_json_ws(j);
            if (j->p >= j->end || *j->p != ':') xs_json_fail(j, "expected ':'");
            j->p++;
            xs_jv *item = xs_json_value(j);
            if (v->n == cap) { size_t nc = cap ? cap * 2 : 4; v->items = xs_jgrow(v->items, v->n, nc, sizeof(void *)); v->keys = xs_jgrow(v->keys, v->n, nc, sizeof(char *)); cap = nc; }
            v->keys[v->n] = key;
            v->items[v->n++] = item;
            xs_json_ws(j);
            if (j->p < j->end && *j->p == ',') { j->p++; continue; }
            if (j->p < j->end && *j->p == '}') { j->p++; break; }
            xs_json_fail(j, "expected ',' or '}'");
        }
    } else if (c == '[') {
        j->p++;
        v = xs_jv_new(XJ_ARR);
        size_t cap = 0;
        xs_json_ws(j);
        if (j->p < j->end && *j->p == ']') { j->p++; j->depth--; return v; }
        for (;;) {
            xs_jv *item = xs_json_value(j);
            if (v->n == cap) { size_t nc = cap ? cap * 2 : 4; v->items = xs_jgrow(v->items, v->n, nc, sizeof(void *)); cap = nc; }
            v->items[v->n++] = item;
            xs_json_ws(j);
            if (j->p < j->end && *j->p == ',') { j->p++; continue; }
            if (j->p < j->end && *j->p == ']') { j->p++; break; }
            xs_json_fail(j, "expected ',' or ']'");
        }
    } else if (c == '"') {
        v = xs_jv_new(XJ_STR);
        v->str = xs_json_string(j, &v->slen);
    } else if (c == 't' && j->end - j->p >= 4 && memcmp(j->p, "true", 4) == 0) {
        v = xs_jv_new(XJ_BOOL); v->b = true; j->p += 4;
    } else if (c == 'f' && j->end - j->p >= 5 && memcmp(j->p, "false", 5) == 0) {
        v = xs_jv_new(XJ_BOOL); j->p += 5;
    } else if (c == 'n' && j->end - j->p >= 4 && memcmp(j->p, "null", 4) == 0) {
        v = xs_jv_new(XJ_NULL); j->p += 4;
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        v = xs_jv_new(XJ_NUM);
        char *stop;
        v->num = strtod(j->p, &stop);
        if (stop == j->p) xs_json_fail(j, "bad number");
        j->p = stop;
    } else {
        xs_json_fail(j, "unexpected character");
        return NULL;
    }
    j->depth--;
    return v;
}

XS_NOINLINE xs_jv *xs_json_parse(xs_str *text) {
    xs_jarena_free(); /* a previous parse abandoned by a throw */
    xs_json_lenient = false;
    xs_jp j = {text->data, text->data + text->len, text->data, 0};
    xs_jv *v = xs_json_value(&j);
    xs_json_ws(&j);
    if (j.p != j.end) xs_json_fail(&j, "trailing characters");
    return v;
}

/* Typed readers used by generated code. Missing or mistyped fields are runtime errors with the field name. */
XS_NOINLINE xs_jv *xs_json_field(xs_jv *obj, const char *name) {
    if (obj->kind != XJ_OBJ) xs_panic("JSON: expected an object");
    for (size_t i = 0; i < obj->n; i++)
        if (strcmp(obj->keys[i], name) == 0) return obj->items[i];
    xs_panicf("JSON: missing field '%s'", name);
    return NULL;
}
XS_NOINLINE xs_jv *xs_json_field_opt(xs_jv *obj, const char *name) {
    if (obj->kind != XJ_OBJ) xs_panic("JSON: expected an object");
    for (size_t i = 0; i < obj->n; i++)
        if (strcmp(obj->keys[i], name) == 0) return obj->items[i];
    return NULL;
}
XS_NOINLINE double xs_json_num(xs_jv *v, const char *where) { if (v->kind != XJ_NUM) xs_panicf("JSON: '%s' must be a number", where); return v->num; }
XS_NOINLINE int64_t xs_json_int(xs_jv *v, const char *where) {
    if (v->kind != XJ_NUM || v->num != floor(v->num)) xs_panicf("JSON: '%s' must be an integer", where);
    return xs_to_int(v->num);
}
XS_NOINLINE bool xs_json_bool(xs_jv *v, const char *where) {
    if (xs_json_lenient && v->kind == XJ_NUM) return v->num != 0;
    if (v->kind != XJ_BOOL) xs_panicf("JSON: '%s' must be true or false", where);
    return v->b;
}
XS_NOINLINE xs_str *xs_json_str(xs_jv *v, const char *where) { if (v->kind != XJ_STR) xs_panicf("JSON: '%s' must be a string", where); return xs_str_new(v->str, v->slen); }
XS_NOINLINE xs_jv *xs_json_arr(xs_jv *v, const char *where) { if (v->kind != XJ_ARR) xs_panicf("JSON: '%s' must be an array", where); return v; }
XS_NOINLINE xs_jv *xs_json_obj(xs_jv *v, const char *where) { if (v->kind != XJ_OBJ) xs_panicf("JSON: '%s' must be an object", where); return v; }
/* Array.isArray(v) on an unknown: whether it holds an array (of any element type) */
XS_NOINLINE bool xs_is_array(void *v) {
    if (!v) return false;
    void (*f)(void *) = xs_type_of(v)->free;
    return f == xs_arr_ref_free || f == xs_arr_num_free || f == xs_arr_int_free || f == xs_arr_bool_free;
}
/* a < b on strings: by their characters (UTF-8 bytes order as code points do) */
/* a.localeCompare(b): the order of ICU's root collation for Latin text, as Node gives it by default: first the
   letters without accents or case (spaces and punctuation < digits < letters), then the accents (none first), then
   the case (lower first). Other scripts compare by code point after Latin (a gap: ICU orders them too). */
static uint32_t xs_lc_next(const unsigned char **p, const unsigned char *end) {
    const unsigned char *s = *p;
    uint32_t c = *s++;
    if (c >= 0xC0 && s < end) {
        int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        c &= n == 3 ? 0x07 : n == 2 ? 0x0F : 0x1F;
        while (n-- && s < end) c = (c << 6) | (*s++ & 0x3F);
    }
    *p = s;
    return c;
}
static void xs_lc_key(uint32_t c, uint32_t *pri, int *sec, int *ter) {
    static const char *acc[2] = { /* Latin-1 letters 0xC0..0xFF: base letter and accent (1 acute, 2 grave, 3 circumflex, 4 ring, 5 diaeresis, 6 tilde, 7 cedilla, 8 stroke) */
        "a2a1a3a6a5a4..c7e2e1e3e5i2i1i3i5..n6o2o1o3o6o5..o8u2u1u3u5y1....",
        "a2a1a3a6a5a4..c7e2e1e3e5i2i1i3i5..n6o2o1o3o6o5..o8u2u1u3u5y1..y5" };
    *sec = 0; *ter = 0;
    if (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7) {
        int up = c < 0xE0, i = (int)(c - (up ? 0xC0 : 0xE0));
        const char *e = acc[up ? 0 : 1] + 2 * i;
        if (e[0] != '.') { *pri = 2000u + (uint32_t)(e[0] - 'a'); *sec = e[1] - '0'; *ter = up; return; }
    }
    if (c >= 'a' && c <= 'z') { *pri = 2000u + (c - 'a'); return; }
    if (c >= 'A' && c <= 'Z') { *pri = 2000u + (c - 'A'); *ter = 1; return; }
    if (c >= '0' && c <= '9') { *pri = 1000u + (c - '0'); return; }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { *pri = 10u + c; return; }
    if (c < 0x80) { *pri = 100u + c; return; }
    *pri = 10000u + c;
}
static int xs_lc_level(xs_str *a, xs_str *b, int level) {
    const unsigned char *p = (const unsigned char *)a->data, *pe = p + a->len, *q = (const unsigned char *)b->data, *qe = q + b->len;
    for (;;) {
        if (p >= pe || q >= qe) return p >= pe && q >= qe ? 0 : p >= pe ? -1 : 1;
        uint32_t x = xs_lc_next(&p, pe), y = xs_lc_next(&q, qe), px, py;
        int sx, sy, tx, ty;
        xs_lc_key(x, &px, &sx, &tx);
        xs_lc_key(y, &py, &sy, &ty);
        long dx = level == 0 ? (long)px : level == 1 ? sx : tx, dy = level == 0 ? (long)py : level == 1 ? sy : ty;
        if (dx != dy) return dx < dy ? -1 : 1;
    }
}
XS_NOINLINE int64_t xs_str_locale_compare(xs_str *a, xs_str *b, ...) {
    for (int l = 0; l < 3; l++) { int c = xs_lc_level(a, b, l); if (c) return c; }
    return 0;
}
/* s.normalize(form): the text as it is (source text and the database's are NFC already; a gap for NFD input) */
XS_NOINLINE xs_str *xs_str_normalize(xs_str *s, ...) { return xs_retain(s); }
XS_NOINLINE int64_t xs_str_order(xs_str *a, xs_str *b) {
    size_t n = a->len < b->len ? (size_t)a->len : (size_t)b->len;
    int c = memcmp(a->data, b->data, n);
    if (c) return c < 0 ? -1 : 1;
    return a->len < b->len ? -1 : a->len > b->len ? 1 : 0;
}
/* JSON as an any (the dynamic path): an object is a Record<string, unknown>, an array an unknown[], numbers and
   booleans boxed (what raw query rows are in TypeORM's query<T = any>) */
static void xs_print_any_quoted(void *p);
static void *xs_jv_any(xs_jv *v) {
    switch (v->kind) {
    case XJ_NULL: return NULL;
    case XJ_BOOL: return xs_box_bool(v->b);
    case XJ_NUM: return xs_box_num(v->num);
    case XJ_STR: return xs_str_new(v->str, v->slen);
    case XJ_ARR: {
        xs_arr_ref *a = xs_arr_ref_new(v->n, xs_print_any_quoted);
        for (size_t i = 0; i < v->n; i++) xs_arr_ref_push(a, xs_jv_any(v->items[i]));
        return a;
    }
    default: {
        xs_map *m = xs_map_rec(xs_map_new(1, 1, 3, xs_print_str_quoted_, xs_print_any_quoted));
        for (size_t i = 0; i < v->n; i++) {
            xs_str *k = xs_str_new(v->keys[i], strlen(v->keys[i]));
            xs_map_set_s(m, k, XS_VPTR(xs_jv_any(v->items[i])));
            xs_release(k);
        }
        return m;
    }
    }
}
XS_NOINLINE void *xs_json_any(xs_str *text) {
    if (!text) return NULL;
    xs_jv *v = xs_json_parse(text);
    void *r = xs_jv_any(v);
    xs_jarena_reset();
    return r;
}

/* stringify helpers: generated code appends into an xs_sb */
static void xs_sb_put(xs_sb *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 512;
        while (cap < b->len + n + 1) cap *= 2;
        b->data = realloc(b->data, cap);
        if (!b->data) xs_panic("out of memory");
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}
static void xs_sb_puts(xs_sb *b, const char *s) { xs_sb_put(b, s, strlen(s)); }
static void xs_sb_num(xs_sb *b, double v) {
    char buf[32];
    if (v != v || isinf(v)) { xs_sb_puts(b, "null"); return; } /* JSON.stringify(NaN) is null */
    xs_num_to_str(v, buf);
    if (v == 0) { buf[0] = '0'; buf[1] = 0; }
    xs_sb_puts(b, buf);
}
static void xs_sb_int(xs_sb *b, int64_t v) { char buf[24]; size_t n = xs_itoa(v, buf); xs_sb_put(b, buf, n); }
static void xs_sb_bool(xs_sb *b, bool v) { xs_sb_puts(b, v ? "true" : "false"); }
static void xs_sb_str(xs_sb *b, xs_str *s) {
    xs_sb_put(b, "\"", 1);
    for (int64_t i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->data[i];
        char esc[8];
        switch (c) {
        case '"': xs_sb_put(b, "\\\"", 2); break;
        case '\\': xs_sb_put(b, "\\\\", 2); break;
        case '\n': xs_sb_put(b, "\\n", 2); break;
        case '\r': xs_sb_put(b, "\\r", 2); break;
        case '\t': xs_sb_put(b, "\\t", 2); break;
        case '\b': xs_sb_put(b, "\\b", 2); break;
        case '\f': xs_sb_put(b, "\\f", 2); break;
        default:
            if (c < 0x20) { snprintf(esc, sizeof esc, "\\u%04x", c); xs_sb_puts(b, esc); }
            else xs_sb_put(b, (const char *)&c, 1);
        }
    }
    xs_sb_put(b, "\"", 1);
}
/* a parsed JSON value written back as text: an `object` element of an array or a field read from JSON keeps
   its value as JSON text (xisto's `object`); strings re-escaped, numbers as JavaScript writes them */
static void xs_json_text_into(xs_sb *b, const xs_jv *v) {
    switch (v->kind) {
    case XJ_NULL: xs_sb_put(b, "null", 4); break;
    case XJ_BOOL: if (v->b) xs_sb_put(b, "true", 4); else xs_sb_put(b, "false", 5); break;
    case XJ_NUM: xs_sb_num(b, v->num); break;
    case XJ_STR: { xs_str *t = xs_str_new(v->str, v->slen); xs_sb_str(b, t); xs_release(t); break; }
    case XJ_ARR:
        xs_sb_put(b, "[", 1);
        for (size_t k = 0; k < v->n; k++) { if (k) xs_sb_put(b, ",", 1); xs_json_text_into(b, v->items[k]); }
        xs_sb_put(b, "]", 1);
        break;
    case XJ_OBJ:
        xs_sb_put(b, "{", 1);
        for (size_t k = 0; k < v->n; k++) {
            if (k) xs_sb_put(b, ",", 1);
            xs_str *key = xs_str_new(v->keys[k], strlen(v->keys[k]));
            xs_sb_str(b, key);
            xs_release(key);
            xs_sb_put(b, ":", 1);
            xs_json_text_into(b, v->items[k]);
        }
        xs_sb_put(b, "}", 1);
        break;
    }
}
static void xs_jv_write(xs_sb *b, xs_jv *v); /* (below) */
XS_NOINLINE xs_str *xs_json_text(const xs_jv *v) { /* (the value's JSON text, as JSON.stringify writes it) */
    xs_sb b = {0};
    xs_jv_write(&b, (xs_jv *)v);
    return xs_sb_finish(&b);
}
static xs_str *xs_sb_finish(xs_sb *b) {
    xs_str *s = xs_str_new(b->data ? b->data : "", b->len);
    free(b->data);
    return s;
}

/* a parsed JSON value back as its text, with JSON's own number spelling (packages/typeorm's native code) */
static void xs_sb_jstr(xs_sb *b, const char *p, size_t n) {
    xs_sb_put(b, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        char esc[8];
        switch (c) {
        case '"': xs_sb_put(b, "\\\"", 2); break;
        case '\\': xs_sb_put(b, "\\\\", 2); break;
        case '\n': xs_sb_put(b, "\\n", 2); break;
        case '\r': xs_sb_put(b, "\\r", 2); break;
        case '\t': xs_sb_put(b, "\\t", 2); break;
        case '\b': xs_sb_put(b, "\\b", 2); break;
        case '\f': xs_sb_put(b, "\\f", 2); break;
        default:
            if (c < 0x20) { snprintf(esc, sizeof esc, "\\u%04x", c); xs_sb_puts(b, esc); }
            else xs_sb_put(b, (const char *)&c, 1);
        }
    }
    xs_sb_put(b, "\"", 1);
}
static void xs_jv_write(xs_sb *b, xs_jv *v) {
    switch (v->kind) {
    case XJ_NULL: xs_sb_puts(b, "null"); break;
    case XJ_BOOL: xs_sb_bool(b, v->b); break;
    case XJ_NUM: xs_sb_num(b, v->num); break;
    case XJ_STR: xs_sb_jstr(b, v->str, v->slen); break;
    case XJ_ARR:
        xs_sb_put(b, "[", 1);
        for (size_t i = 0; i < v->n; i++) { if (i) xs_sb_put(b, ",", 1); xs_jv_write(b, v->items[i]); }
        xs_sb_put(b, "]", 1);
        break;
    case XJ_OBJ:
        xs_sb_put(b, "{", 1);
        for (size_t i = 0; i < v->n; i++) {
            if (i) xs_sb_put(b, ",", 1);
            xs_sb_jstr(b, v->keys[i], strlen(v->keys[i]));
            xs_sb_put(b, ":", 1);
            xs_jv_write(b, v->items[i]);
        }
        xs_sb_put(b, "}", 1);
        break;
    }
}

/* ---- console.log ---- */

static void xs_print_num(double v) {
    char buf[32];
    xs_num_to_str(v, buf);
    fputs(buf, XS_OUT);
}

static void xs_print_int(int64_t v) { char buf[24]; size_t n = xs_itoa(v, buf); fwrite(buf, 1, n, XS_OUT); }
static void xs_print_bool(bool b) { fputs(b ? "true" : "false", XS_OUT); }
static void xs_print_str(xs_str *s) { fwrite(s->data, 1, (size_t)s->len, XS_OUT); }
static void xs_print_str_quoted(xs_str *s) { /* as Node prints strings inside objects */
    fputc('\'', XS_OUT);
    for (int64_t i = 0; i < s->len; i++) {
        char c = s->data[i];
        if (c == '\'') fputs("\\'", XS_OUT);
        else if (c == '\n') fputs("\\n", XS_OUT);
        else fputc(c, XS_OUT);
    }
    fputc('\'', XS_OUT);
}
#ifdef XS_DEBUG
/* > 0 while the debugger renders a value: the nesting it may still print. A cycle (parent <-> child) would
   otherwise recurse until the stack overflows; console.log itself is unchanged. */
static XS_TLS int xs_dbg_print_budget;
static void xs_print_obj(void *p) {
    if (xs_dbg_print_budget == 1) { fputs("[Object]", XS_OUT); return; }
    if (xs_dbg_print_budget) xs_dbg_print_budget--;
    xs_type_of(p)->print(p);
    if (xs_dbg_print_budget) xs_dbg_print_budget++;
}
#else
static void xs_print_obj(void *p) { xs_type_of(p)->print(p); }
#endif
static void xs_print_obj_or_null(void *p) { if (p) xs_print_obj(p); else fputs("null", XS_OUT); }
static void xs_print_str_quoted_or_null_(void *p) { if (p) xs_print_str_quoted_(p); else fputs("null", XS_OUT); }
/* a union or unknown value inside an array or an object: strings quoted, as Node prints them there */
static void xs_print_any_quoted(void *p) { if (p && xs_type_of(p)->free == xs_str_free) xs_print_str_quoted_(p); else xs_print_obj_or_null(p); }
static void xs_print_str_or_null(xs_str *s) { if (s) xs_print_str(s); else fputs("null", XS_OUT); }
static void xs_print_xs_arr_ref(void *p) {
    xs_arr_ref *a = p;
    xs_insp fr;
    if (!xs_insp_begin(&fr, p, "Array", a->len ? NULL : "[]")) return;
    for (size_t i = 0; i < a->len && i < 100; i++) {
        xs_insp_entry(&fr);
        a->print_elem(a->data[i]);
    }
    if (a->len > 100) { xs_insp_entry(&fr); fprintf(XS_OUT, "... %zu more item%s", a->len - 100, a->len - 101 ? "s" : ""); }
    xs_insp_end(&fr, "[", "]", true, false, a->len > 100);
}
#define xs_print_arr_ref xs_print_xs_arr_ref
static void xs_print_sep(void) { fputc(' ', XS_OUT); }
static void xs_print_nl(void) { fputc('\n', XS_OUT); }

#define XS_PRINT_ARRAY(name, printer, numeric)                        \
    static void xs_print_##name(void *p) {                            \
        name *a = p;                                                  \
        xs_insp fr;                                                   \
        if (!xs_insp_begin(&fr, p, "Array", a->len ? NULL : "[]")) return; \
        for (size_t i = 0; i < a->len && i < 100; i++) {              \
            xs_insp_entry(&fr);                                       \
            printer(a->data[i]);                                      \
        }                                                             \
        if (a->len > 100) { xs_insp_entry(&fr); fprintf(XS_OUT, "... %zu more item%s", a->len - 100, a->len - 101 ? "s" : ""); } \
        xs_insp_end(&fr, "[", "]", true, numeric, a->len > 100);       \
    }
XS_PRINT_ARRAY(xs_arr_num, xs_print_num, true)
XS_PRINT_ARRAY(xs_arr_int, xs_print_int, true)
XS_PRINT_ARRAY(xs_arr_bool, xs_print_bool, false)
#define xs_print_arr_num xs_print_xs_arr_num
#define xs_print_arr_int xs_print_xs_arr_int
#define xs_print_arr_bool xs_print_xs_arr_bool
/* (number | null)[] / (int | null)[]: the null sentinel prints as null */
static void xs_print_num_or_null_(double d) { if (xs_isnull_num(d)) fputs("null", XS_OUT); else xs_print_num(d); }
static void xs_print_int_or_null_(int64_t v) { if (v == XS_NULL_INT) fputs("null", XS_OUT); else xs_print_int(v); }
#define XS_PRINT_ARRAY_N(fname, st, printer)                          \
    static void fname(void *p) {                                      \
        st *a = p;                                                    \
        xs_insp fr;                                                   \
        if (!xs_insp_begin(&fr, p, "Array", a->len ? NULL : "[]")) return; \
        for (size_t i = 0; i < a->len && i < 100; i++) {              \
            xs_insp_entry(&fr);                                       \
            printer(a->data[i]);                                      \
        }                                                             \
        if (a->len > 100) { xs_insp_entry(&fr); fprintf(XS_OUT, "... %zu more item%s", a->len - 100, a->len - 101 ? "s" : ""); } \
        xs_insp_end(&fr, "[", "]", true, true, a->len > 100);         \
    }
XS_PRINT_ARRAY_N(xs_print_arr_num_n, xs_arr_num, xs_print_num_or_null_)
XS_PRINT_ARRAY_N(xs_print_arr_int_n, xs_arr_int, xs_print_int_or_null_)

/* ---- internal indicators ----
   Built with --profile, every function counts calls and time; the table goes to stderr at exit.
   XISTO_RC_STATS=1 reports allocations. Both exist to find bottlenecks before the bench does. */

#if !defined(_WIN32)
#include <sys/resource.h>
#endif
#include <time.h>

static inline uint64_t xs_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#if !defined(_WIN32)
#include <unistd.h>
#endif
/* Intl.DateTimeFormat with a timeZone (xisto:intl): the wall clock of a zone of the system TZ database at an instant,
   as [year, month 1-12, day, hour, minute, second, weekday]; a zone the system does not know is a RangeError, as in Node */
XS_NOINLINE xs_arr_int *xs_tz_fields(double ms, xs_str *zone) {
    char z[160];
    size_t n = zone->len < sizeof z - 1 ? (size_t)zone->len : sizeof z - 1;
    memcpy(z, zone->data, n);
    z[n] = 0;
    time_t t = (time_t)floor(ms / 1000.0);
    struct tm tm;
#if defined(_WIN32)
    gmtime_s(&tm, &t); /* (no TZ database: UTC) */
#else
    bool utc = strcmp(z, "UTC") == 0 || strcmp(z, "Etc/UTC") == 0 || strcmp(z, "GMT") == 0;
    if (!utc) {
        char path[256];
        bool known = strstr(z, "..") == NULL && z[0] != '/';
        if (known) {
            snprintf(path, sizeof path, "/usr/share/zoneinfo/%s", z);
            known = access(path, R_OK) == 0;
            if (!known) { snprintf(path, sizeof path, "/var/db/timezone/zoneinfo/%s", z); known = access(path, R_OK) == 0; }
        }
        if (!known) {
            static XS_TLS char msg[220];
            snprintf(msg, sizeof msg, "RangeError: Invalid time zone specified: %s", z);
            xs_panic(msg);
        }
    }
    if (utc) gmtime_r(&t, &tm);
    else {
        const char *old = getenv("TZ");
        char *saved = old ? strdup(old) : NULL;
        setenv("TZ", z, 1);
        tzset();
        localtime_r(&t, &tm);
        if (saved) { setenv("TZ", saved, 1); free(saved); } else unsetenv("TZ");
        tzset();
    }
#endif
    xs_arr_int *a = xs_arr_int_new(0, 0);
    xs_arr_int_push(a, tm.tm_year + 1900);
    xs_arr_int_push(a, tm.tm_mon + 1);
    xs_arr_int_push(a, tm.tm_mday);
    xs_arr_int_push(a, tm.tm_hour);
    xs_arr_int_push(a, tm.tm_min);
    xs_arr_int_push(a, tm.tm_sec);
    xs_arr_int_push(a, tm.tm_wday);
    return a;
}

typedef struct xs_prof { const char *name; uint64_t calls, ns, self_ns; struct xs_prof *next; } xs_prof;
static xs_prof *xs_prof_list;
static uint64_t xs_prof_child_ns; /* time spent in callees of the running function, for self time */

static inline void xs_prof_enter(xs_prof *p, uint64_t *t0, uint64_t *child_mark) {
    if (!p->next && xs_prof_list != p) { p->next = xs_prof_list ? xs_prof_list : p; xs_prof_list = p; }
    *t0 = xs_now();
    *child_mark = xs_prof_child_ns;
}

static inline void xs_prof_leave(xs_prof *p, uint64_t t0, uint64_t child_mark) {
    uint64_t dt = xs_now() - t0;
    uint64_t children = xs_prof_child_ns - child_mark;
    p->calls++;
    p->ns += dt;
    p->self_ns += dt > children ? dt - children : 0;
    xs_prof_child_ns = child_mark + dt;
}

static int64_t xs_max_rss_bytes(void) {
#if defined(_WIN32)
    return 0; /* not measured there yet */
#else
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
#if defined(__APPLE__)
    return (int64_t)ru.ru_maxrss; /* bytes on macOS */
#else
    return (int64_t)ru.ru_maxrss * 1024; /* kilobytes on Linux */
#endif
#endif
}

static void xs_prof_report(void) {
    if (!xs_prof_list) return;
    /* collect and sort by self time */
    int n = 0;
    for (xs_prof *p = xs_prof_list; p; p = p->next == p ? NULL : p->next) n++;
    xs_prof **v = malloc(sizeof(xs_prof *) * (size_t)n);
    int i = 0;
    for (xs_prof *p = xs_prof_list; p; p = p->next == p ? NULL : p->next) v[i++] = p;
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (v[b]->self_ns > v[a]->self_ns) { xs_prof *t = v[a]; v[a] = v[b]; v[b] = t; }
    fprintf(stderr, "\nxisto profile (sorted by self time)\n%-28s %12s %12s %12s %10s\n", "function", "calls", "self ms", "total ms", "ns/call");
    for (int k = 0; k < n; k++) {
        xs_prof *p = v[k];
        fprintf(stderr, "%-28s %12llu %12.3f %12.3f %10.0f\n", p->name, (unsigned long long)p->calls, p->self_ns / 1e6, p->ns / 1e6, p->calls ? (double)p->ns / (double)p->calls : 0);
    }
    fprintf(stderr, "peak rss %.1f MB, %lld allocations\n", xs_max_rss_bytes() / 1048576.0, (long long)xs_allocs);
    free(v);
}

/* ---- streams: pull-based, lazy, one element in flight between stages ----
   pull(st, out) fills *out with the next element (owned if it is a heap value) and returns false at the end. */

typedef struct xs_stream xs_stream;
typedef bool (*xs_pull_fn)(xs_stream *st, xs_val *out);
struct xs_stream {
    xs_hdr hdr;
    xs_pull_fn pull;
    xs_stream *src;        /* upstream stage, retained */
    xs_closure *fn;        /* callback, retained */
    int elem_kind;         /* 0 num, 1 int, 2 bool, 3 heap pointer (of the elements this stage produces) */
    int src_kind;
    void (*print_elem)(void *);
    int64_t n, counter;    /* batch size / take limit */
    /* file source */
    FILE *file;
    char *buf; size_t buf_len, buf_cap, buf_pos;
    bool eof;
    bool done;
    void *ext;             /* a generator (runtime/xisto_net.h): its coroutine and arguments */
    void (*ext_free)(void *);
};

static void xs_stream_free(void *p) {
    xs_stream *st = p;
    if (st->ext_free) st->ext_free(st->ext);
    xs_release(st->src);
    xs_release(st->fn);
    if (st->file) fclose(st->file);
    free(st->buf);
    xs_obj_free(p);
}
static void xs_print_stream(void *p) { (void)p; fputs("Stream {}", XS_OUT); }

static xs_stream *xs_stream_new(xs_pull_fn pull, xs_stream *src, xs_closure *fn, int elem_kind) {
    xs_stream *st = xs_obj_alloc(sizeof *st, xs_stream_free, xs_print_stream);
    st->pull = pull;
    st->src = xs_retain(src);
    st->fn = xs_retain(fn);
    st->elem_kind = elem_kind;
    st->src_kind = src ? src->elem_kind : 3;
    return st;
}

/* a stream that has ended lets go of what it holds at once (its file, the stages above it): one kept in a
   variable would otherwise keep its file open until the end (Windows then refuses to delete the file) */
static __attribute__((noinline)) void xs_stream_end(xs_stream *st) {
    st->done = true;
    if (st->file) { fclose(st->file); st->file = NULL; }
    free(st->buf);
    st->buf = NULL;
    st->buf_len = st->buf_cap = st->buf_pos = 0;
    if (st->ext_free) { st->ext_free(st->ext); st->ext_free = NULL; st->ext = NULL; }
    xs_stream *src = st->src;
    st->src = NULL;
    xs_release(src);
}
static inline bool xs_stream_pull(xs_stream *st, xs_val *out) {
    if (st->done) return false;
    if (st->pull(st, out)) return true;
    xs_stream_end(st);
    return false;
}

static inline void xs_val_release(int kind, xs_val v) { if (kind == 3) xs_release_(v.p); }

/* file sources: 64 KB reads (override with -DXS_STREAM_BUF=... to experiment) */
#ifndef XS_STREAM_BUF
#define XS_STREAM_BUF (64 * 1024)
#endif
static bool xs_stream_fill(xs_stream *st) {
    if (st->eof) return false;
    if (st->buf_pos > 0) { /* keep the unread tail */
        memmove(st->buf, st->buf + st->buf_pos, st->buf_len - st->buf_pos);
        st->buf_len -= st->buf_pos;
        st->buf_pos = 0;
    }
    if (st->buf_len == st->buf_cap) { st->buf_cap *= 2; st->buf = realloc(st->buf, st->buf_cap); if (!st->buf) xs_panic("out of memory"); }
    size_t n = fread(st->buf + st->buf_len, 1, st->buf_cap - st->buf_len, st->file);
    if (n == 0) { st->eof = true; return false; }
    st->buf_len += n;
    return true;
}

static bool xs_pull_lines(xs_stream *st, xs_val *out) {
    for (;;) {
        char *start = st->buf + st->buf_pos;
        char *nl = memchr(start, '\n', st->buf_len - st->buf_pos);
        if (nl) {
            size_t len = (size_t)(nl - start);
            if (len && start[len - 1] == '\r') len--;
            out->p = xs_str_new(start, len);
            st->buf_pos = (size_t)(nl - st->buf) + 1;
            return true;
        }
        if (!xs_stream_fill(st)) { /* last line without newline */
            size_t len = st->buf_len - st->buf_pos;
            if (len == 0) return false;
            out->p = xs_str_new(st->buf + st->buf_pos, len);
            st->buf_pos = st->buf_len;
            return true;
        }
    }
}

static bool xs_pull_chunks(xs_stream *st, xs_val *out) {
    while (st->buf_len - st->buf_pos < (size_t)st->n && !st->eof) xs_stream_fill(st);
    size_t avail = st->buf_len - st->buf_pos;
    if (avail == 0) return false;
    size_t len = avail < (size_t)st->n ? avail : (size_t)st->n;
    out->p = xs_str_new(st->buf + st->buf_pos, len);
    st->buf_pos += len;
    return true;
}

XS_NOINLINE xs_stream *xs_stream_open(xs_str *path, xs_pull_fn pull, int64_t n) {
    xs_stream *st = xs_stream_new(pull, NULL, NULL, 3);
    st->file = fopen(path->data, "rb");
    if (!st->file) xs_panicf("ENOENT: no such file or directory, open '%s'", path->data);
    st->buf_cap = XS_STREAM_BUF > (size_t)n * 2 ? XS_STREAM_BUF : (size_t)n * 2;
    st->buf = malloc(st->buf_cap);
    st->n = n;
    st->print_elem = xs_print_str_quoted_;
    return st;
}
XS_NOINLINE xs_stream *xs_stream_lines(xs_str *path) { return xs_stream_open(path, xs_pull_lines, 0); }
XS_NOINLINE xs_stream *xs_stream_chunks(xs_str *path, int64_t bytes) {
    if (bytes <= 0) xs_panic("chunks: size must be positive");
    return xs_stream_open(path, xs_pull_chunks, bytes);
}

/* stages */
XS_NOINLINE xs_val xs_call1(xs_closure *fn, int in_kind, int out_kind, xs_val v) {
    xs_val r = {0};
    #define CALL(IT, OT, FIELD_IN, FIELD_OUT) r.FIELD_OUT = ((OT (*)(void *, IT))fn->fn)(fn, v.FIELD_IN)
    switch (in_kind * 4 + out_kind) {
    case 0: CALL(double, double, d, d); break;   case 1: CALL(double, int64_t, d, i); break;   case 2: r.i = ((bool (*)(void *, double))fn->fn)(fn, v.d); break;   case 3: CALL(double, void *, d, p); break;
    case 4: CALL(int64_t, double, i, d); break;  case 5: CALL(int64_t, int64_t, i, i); break;  case 6: r.i = ((bool (*)(void *, int64_t))fn->fn)(fn, v.i); break;  case 7: CALL(int64_t, void *, i, p); break;
    case 8: r.d = ((double (*)(void *, bool))fn->fn)(fn, v.i != 0); break; case 9: r.i = ((int64_t (*)(void *, bool))fn->fn)(fn, v.i != 0); break; case 10: r.i = ((bool (*)(void *, bool))fn->fn)(fn, v.i != 0); break; case 11: r.p = ((void *(*)(void *, bool))fn->fn)(fn, v.i != 0); break;
    case 12: CALL(void *, double, p, d); break;  case 13: CALL(void *, int64_t, p, i); break;  case 14: r.i = ((bool (*)(void *, void *))fn->fn)(fn, v.p); break;  case 15: CALL(void *, void *, p, p); break;
    }
    #undef CALL
    return r;
}

XS_NOINLINE bool xs_pull_map(xs_stream *st, xs_val *out) {
    xs_val v;
    if (!xs_stream_pull(st->src, &v)) return false;
    *out = xs_call1(st->fn, st->src_kind, st->elem_kind, v);
    xs_val_release(st->src_kind, v);
    return true;
}
static xs_stream *xs_stream_map(xs_stream *src, xs_closure *fn, int out_kind, void (*print_elem)(void *)) {
    xs_stream *st = xs_stream_new(xs_pull_map, src, fn, out_kind);
    st->print_elem = print_elem;
    return st;
}

XS_NOINLINE bool xs_pull_filter(xs_stream *st, xs_val *out) {
    xs_val v;
    while (xs_stream_pull(st->src, &v)) {
        if (xs_call1(st->fn, st->src_kind, 2, v).i) { *out = v; return true; }
        xs_val_release(st->src_kind, v);
    }
    return false;
}
static xs_stream *xs_stream_filter(xs_stream *src, xs_closure *fn) {
    xs_stream *st = xs_stream_new(xs_pull_filter, src, fn, src->elem_kind);
    st->print_elem = src->print_elem;
    return st;
}

static bool xs_pull_take(xs_stream *st, xs_val *out) {
    if (st->counter >= st->n) return false;
    if (!xs_stream_pull(st->src, out)) return false;
    st->counter++;
    return true;
}
static xs_stream *xs_stream_take(xs_stream *src, int64_t n) {
    xs_stream *st = xs_stream_new(xs_pull_take, src, NULL, src->elem_kind);
    st->n = n;
    st->print_elem = src->print_elem;
    return st;
}

static void *xs_array_new_for_kind(int kind, size_t cap, void (*print_elem)(void *)) {
    switch (kind) {
    case 0: return xs_arr_num_new(0, 0);
    case 1: return xs_arr_int_new(0, 0);
    case 2: return xs_arr_bool_new(0, false);
    default: return xs_arr_ref_new(cap, print_elem);
    }
}
static void xs_array_push_val(int kind, void *arr, xs_val v) { /* takes ownership of heap values */
    switch (kind) {
    case 0: xs_arr_num_push(arr, v.d); break;
    case 1: xs_arr_int_push(arr, v.i); break;
    case 2: xs_arr_bool_push(arr, v.i != 0); break;
    default: xs_arr_ref_push(arr, v.p); break;
    }
}

static bool xs_pull_batch(xs_stream *st, xs_val *out) {
    void *arr = NULL;
    xs_val v;
    int64_t count = 0;
    while (count < st->n && xs_stream_pull(st->src, &v)) {
        if (!arr) arr = xs_array_new_for_kind(st->src_kind, (size_t)st->n, st->print_elem);
        xs_array_push_val(st->src_kind, arr, v);
        count++;
    }
    if (!arr) return false;
    out->p = arr;
    return true;
}
XS_NOINLINE xs_stream *xs_stream_batch(xs_stream *src, int64_t n, void (*print_elem)(void *)) {
    if (n <= 0) xs_panic("batch: size must be positive");
    xs_stream *st = xs_stream_new(xs_pull_batch, src, NULL, 3);
    st->n = n;
    st->print_elem = print_elem; /* printer of the inner elements */
    return st;
}

/* terminals */
XS_NOINLINE void xs_stream_forEach(xs_stream *st, xs_closure *fn) {
    xs_val v;
    while (xs_stream_pull(st, &v)) {
        switch (st->elem_kind) {
        case 0: ((void (*)(void *, double))fn->fn)(fn, v.d); break;
        case 1: ((void (*)(void *, int64_t))fn->fn)(fn, v.i); break;
        case 2: ((void (*)(void *, bool))fn->fn)(fn, v.i != 0); break;
        default: ((void (*)(void *, void *))fn->fn)(fn, v.p); break;
        }
        xs_val_release(st->elem_kind, v);
    }
}
static int64_t xs_stream_count(xs_stream *st) {
    xs_val v;
    int64_t n = 0;
    while (xs_stream_pull(st, &v)) { xs_val_release(st->elem_kind, v); n++; }
    return n;
}
static void *xs_stream_to_array(xs_stream *st, void (*print_elem)(void *)) {
    /* the partial array sits in the autorelease pool while the stream runs: a throw from a stage (a generator,
       a callback) lands in a catch that drains the pool, so it is released there */
    void *arr = xs_ar_(xs_array_new_for_kind(st->elem_kind, 16, print_elem));
    xs_val v;
    while (xs_stream_pull(st, &v)) xs_array_push_val(st->elem_kind, arr, v);
    return xs_retain(arr); /* owned by the caller; the pool's reference goes at the statement's end */
}
XS_NOINLINE void xs_stream_write_lines(xs_stream *st, xs_str *path) {
    FILE *f = fopen(path->data, "wb");
    if (!f) xs_panicf("EACCES: cannot open '%s' for writing", path->data);
    xs_val v;
    while (xs_stream_pull(st, &v)) {
        xs_str *s = v.p;
        fwrite(s->data, 1, (size_t)s->len, f);
        fputc('\n', f);
        xs_release(s);
    }
    fclose(f);
}
#define XS_REDUCE(name, T, FIELD, KIND)                                                   \
    XS_NOINLINE T xs_stream_reduce_##name(xs_stream *st, xs_closure *fn, T init) {             \
        xs_val acc = {0}; acc.FIELD = init;                                               \
        xs_val v;                                                                         \
        while (xs_stream_pull(st, &v)) {                                                  \
            xs_val next;                                                                  \
            switch (st->elem_kind) {                                                      \
            case 0: next.FIELD = ((T (*)(void *, T, double))fn->fn)(fn, acc.FIELD, v.d); break;   \
            case 1: next.FIELD = ((T (*)(void *, T, int64_t))fn->fn)(fn, acc.FIELD, v.i); break;  \
            case 2: next.FIELD = ((T (*)(void *, T, bool))fn->fn)(fn, acc.FIELD, v.i != 0); break; \
            default: next.FIELD = ((T (*)(void *, T, void *))fn->fn)(fn, acc.FIELD, v.p); break;  \
            }                                                                             \
            if (KIND == 3) xs_release_(acc.p);                                            \
            acc = next;                                                                   \
            xs_val_release(st->elem_kind, v);                                             \
        }                                                                                 \
        return acc.FIELD;                                                                 \
    }
XS_REDUCE(num, double, d, 0)
XS_REDUCE(int, int64_t, i, 1)
XS_NOINLINE bool xs_stream_reduce_bool(xs_stream *st, xs_closure *fn, bool init) {
    bool acc = init; xs_val v;
    while (xs_stream_pull(st, &v)) {
        switch (st->elem_kind) {
        case 0: acc = ((bool (*)(void *, bool, double))fn->fn)(fn, acc, v.d); break;
        case 1: acc = ((bool (*)(void *, bool, int64_t))fn->fn)(fn, acc, v.i); break;
        case 2: acc = ((bool (*)(void *, bool, bool))fn->fn)(fn, acc, v.i != 0); break;
        default: acc = ((bool (*)(void *, bool, void *))fn->fn)(fn, acc, v.p); break;
        }
        xs_val_release(st->elem_kind, v);
    }
    return acc;
}
XS_REDUCE(ptr, void *, p, 3)

/* ---- std: time, random, process, files ---- */

#include <sys/time.h>
#include <unistd.h>

static double xs_date_now(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)(tv.tv_usec / 1000);
}
/* a ** b on ints: exact, an error on overflow (as + - * on ints) or for a negative exponent */
XS_NOINLINE int64_t xs_ipow_s(int64_t a, int64_t b, const xs_site *s) {
    if (b < 0) XS_FAIL("negative exponent of an int: write Number(a) ** b", s);
    int64_t r = 1;
    while (b) {
        if (b & 1) { if (__builtin_mul_overflow(r, a, &r) || XS_IOUT(r)) XS_FAIL(XS_IOVF_MSG, s); }
        b >>= 1;
        if (b && (__builtin_mul_overflow(a, a, &a) || XS_IOUT(a))) XS_FAIL(XS_IOVF_MSG, s);
    }
    return r;
}
static inline double xs_sign(double x) { return x > 0 ? 1 : x < 0 ? -1 : x; } /* NaN and -0 stay */
static inline double xs_fround(double x) { return (double)(float)x; }
/* ToInt32 of JS: a number to the 32-bit integer the bitwise operators work on */
static inline int32_t xs_toi32(double d) {
    if (!isfinite(d)) return 0;
    double t = trunc(d);
    if (fabs(t) < 2147483648.0) return (int32_t)t;
    double m = fmod(t, 4294967296.0);
    if (m < 0) m += 4294967296.0;
    return (int32_t)(uint32_t)m;
}
#if defined(__linux__) && (!defined(__GLIBC__) || __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 36))
/* musl (xisto build --target=linux-*) and glibc before 2.36 (Amazon Linux 2023, RHEL 9): no arc4random */
#include <sys/random.h>
XS_NOINLINE uint32_t xs_arc4random(void) {
    static XS_TLS uint32_t pool[256];
    static XS_TLS int left;
    if (left == 0) {
        if (getrandom(pool, sizeof pool, 0) != (ssize_t)sizeof pool) xs_panic("getrandom failed");
        left = 256;
    }
    return pool[--left];
}
#define arc4random xs_arc4random
#endif
#if defined(_WIN32)
errno_t __cdecl rand_s(unsigned int *); /* the C library's (RtlGenRandom) */
static uint32_t xs_win_random(void) { unsigned int v = 0; rand_s(&v); return v; }
#define arc4random xs_win_random
#endif
static double xs_random(void) { return (double)arc4random() / 4294967296.0; }

/* ---- Date: a time value (ms since 1970 UTC, NaN when invalid), as in JS; local time from the system (TZ) ---- */
typedef struct xs_Date { xs_hdr hdr; double t; } xs_Date;
#define XS_MS_DAY 86400000.0
static int64_t xs_days_from_civil(int64_t y, int64_t m, int64_t d) { /* m 1..12 (H. Hinnant) */
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static void xs_civil_from_days(int64_t z, int64_t *y, int64_t *m, int64_t *d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yoe + era * 400 + (*m <= 2);
}
/* MakeDay / MakeTime / MakeDate of the spec: fields may overflow (month 13, day 0) */
static double xs_make_date(double y, double mon, double d, double h, double mi, double s, double ms) {
    if (!isfinite(y) || !isfinite(mon) || !isfinite(d) || !isfinite(h) || !isfinite(mi) || !isfinite(s) || !isfinite(ms)) return NAN;
    y = trunc(y); mon = trunc(mon); d = trunc(d); h = trunc(h); mi = trunc(mi); s = trunc(s); ms = trunc(ms);
    double ym = y + floor(mon / 12);
    double mn = fmod(mon, 12);
    if (mn < 0) mn += 12;
    if (fabs(ym) > 400000) return NAN;
    double day = (double)xs_days_from_civil((int64_t)ym, (int64_t)mn + 1, 1) + d - 1;
    return day * XS_MS_DAY + ((h * 60 + mi) * 60 + s) * 1000 + ms;
}
static double xs_time_clip(double t) { return !isfinite(t) || fabs(t) > 8.64e15 ? NAN : trunc(t) + 0.0; }
/* the local offset (ms east of UTC) at UTC time t. localtime_r takes a lock and reads the zone rules (it was
   90% of a loop of getters), so offsets are cached per UTC day: when the day starts and ends with the same
   offset, it holds all day; the day of a transition asks the system every time. */
static double xs_tz_offset_sys(double t) {
    time_t secs = (time_t)floor(t / 1000);
    struct tm tm;
#if defined(_WIN32)
    if (localtime_s(&tm, &secs) != 0) return 0;
    return (double)(_mkgmtime(&tm) - secs) * 1000; /* the local wall clock read as UTC, minus the instant */
#else
    if (!localtime_r(&secs, &tm)) return 0;
    return (double)tm.tm_gmtoff * 1000;
#endif
}
static double xs_tz_offset(double t) {
    if (!isfinite(t)) return 0;
    static XS_TLS struct { int64_t day[2048]; int32_t off[2048]; bool init; } c; /* off: INT32_MIN on a transition day */
    if (!c.init) { for (int i = 0; i < 2048; i++) c.day[i] = INT64_MIN; c.init = true; }
    int64_t day = (int64_t)floor(t / XS_MS_DAY);
    int slot = (int)(day & 2047);
    if (c.day[slot] != day) {
        double a = xs_tz_offset_sys((double)day * XS_MS_DAY), b = xs_tz_offset_sys((double)(day + 1) * XS_MS_DAY - 1000);
        c.day[slot] = day;
        c.off[slot] = a == b ? (int32_t)a : INT32_MIN;
    }
    return c.off[slot] != INT32_MIN ? (double)c.off[slot] : xs_tz_offset_sys(t);
}
/* local wall time -> UTC, as the spec does: the offset in force before a transition wins (a repeated hour
   is its first occurrence; a skipped one, 02:30 when clocks jump to 03:00, is read with the old offset) */
static double xs_local_to_utc(double local) {
    if (!isfinite(local)) return NAN;
    double before = xs_tz_offset(local - XS_MS_DAY), after = xs_tz_offset(local + XS_MS_DAY);
    if (xs_tz_offset(local - before) == before) return local - before;
    if (xs_tz_offset(local - after) == after) return local - after;
    return local - before;
}
typedef struct { int64_t y, mon, d, h, mi, s, ms, wd; } xs_dfields;
static bool xs_date_fields_(double t, bool local, xs_dfields *f);
/* d.getFullYear() + d.getMonth() + d.getDate() ...: the last date taken apart is reused */
static bool xs_date_fields(double t, bool local, xs_dfields *f) {
    static XS_TLS struct { double t; bool local, ok; xs_dfields f; } last = {NAN, false, false, {0}};
    if (t == last.t && local == last.local) { *f = last.f; return last.ok; }
    bool ok = xs_date_fields_(t, local, f);
    last.t = t;
    last.local = local;
    last.ok = ok;
    last.f = *f;
    return ok;
}
static bool xs_date_fields_(double t, bool local, xs_dfields *f) {
    if (isnan(t)) return false;
    if (local) t += xs_tz_offset(t);
    double days = floor(t / XS_MS_DAY);
    double msd = t - days * XS_MS_DAY;
    xs_civil_from_days((int64_t)days, &f->y, &f->mon, &f->d);
    f->mon -= 1;
    int64_t m = (int64_t)msd;
    f->ms = m % 1000; m /= 1000;
    f->s = m % 60; m /= 60;
    f->mi = m % 60;
    f->h = m / 60;
    f->wd = (((int64_t)days % 7) + 11) % 7; /* 1970-01-01 was a Thursday */
    return true;
}

static void xs_Date_free(void *p) { xs_obj_free(p); }
static void xs_Date_print(void *p);
static xs_Date *xs_date_make(double t) {
    xs_Date *d = xs_obj_alloc(sizeof *d, xs_Date_free, xs_Date_print);
    d->t = xs_time_clip(t);
    return d;
}
XS_NOINLINE xs_Date *xs_Date_new_now(void) { return xs_date_make(xs_date_now()); }
XS_NOINLINE xs_Date *xs_Date_new_ms(double t) { return xs_date_make(t); }
XS_NOINLINE xs_Date *xs_Date_new_copy(xs_Date *o) { return xs_date_make(o->t); }
/* new Date(y, m, d = 1, h = 0, mi = 0, s = 0, ms = 0), in local time; years 0..99 mean 1900..1999 */
XS_NOINLINE xs_Date *xs_Date_new_parts(double y, double mon, double d, double h, double mi, double s, double ms) {
    if (isfinite(y) && trunc(y) >= 0 && trunc(y) <= 99) y = 1900 + trunc(y);
    return xs_date_make(xs_local_to_utc(xs_make_date(y, mon, d, h, mi, s, ms)));
}
/* Date.UTC(y, m = 0, d = 1, ...) */
XS_NOINLINE double xs_Date_UTC(double y, double mon, double d, double h, double mi, double s, double ms) {
    if (isfinite(y) && trunc(y) >= 0 && trunc(y) <= 99) y = 1900 + trunc(y);
    return xs_time_clip(xs_make_date(y, mon, d, h, mi, s, ms));
}

/* ---- parsing: ISO 8601 (date only: UTC; date and time without offset: local), then the legacy forms
   engines accept: "Fri, 06 Nov 2026 10:00:00 GMT", "Nov 6, 2026 10:00", "2026/11/06 10:00", "11/06/2026" ---- */
static int xs_dnum(const char **p, const char *e, int min, int max, int64_t *v) {
    int n = 0;
    *v = 0;
    while (*p < e && **p >= '0' && **p <= '9' && n < max) { *v = *v * 10 + (**p - '0'); (*p)++; n++; }
    return n >= min ? n : -1;
}
static double xs_parse_iso(const char *p, const char *e) {
    int64_t y, mon = 1, d = 1, h = 0, mi = 0, s = 0;
    double ms = 0;
    int sign = 1;
    if (p < e && (*p == '+' || *p == '-')) { sign = *p == '-' ? -1 : 1; p++; if (xs_dnum(&p, e, 6, 6, &y) < 0) return NAN; if (sign < 0 && y == 0) return NAN; }
    else if (xs_dnum(&p, e, 4, 4, &y) < 0) return NAN;
    y *= sign;
    bool has_time = false, utc = true;
    if (p < e && *p == '-') {
        p++;
        if (xs_dnum(&p, e, 2, 2, &mon) < 0 || mon < 1 || mon > 12) return NAN;
        if (p < e && *p == '-') { p++; if (xs_dnum(&p, e, 2, 2, &d) < 0 || d < 1 || d > 31) return NAN; }
    }
    if (p < e && (*p == 'T' || *p == 't' || *p == ' ')) {
        p++;
        has_time = true;
        utc = false;
        if (xs_dnum(&p, e, 2, 2, &h) < 0 || p >= e || *p != ':') return NAN;
        p++;
        if (xs_dnum(&p, e, 2, 2, &mi) < 0) return NAN;
        if (p < e && *p == ':') {
            p++;
            if (xs_dnum(&p, e, 2, 2, &s) < 0) return NAN;
            if (p < e && (*p == '.' || *p == ',')) {
                p++;
                double scale = 100;
                int nd = 0;
                while (p < e && *p >= '0' && *p <= '9') { if (nd < 3) ms += (*p - '0') * scale; scale /= 10; p++; nd++; }
                if (!nd) return NAN;
            }
        }
        if (h > 24 || mi > 59 || s > 59 || (h == 24 && (mi || s || ms))) return NAN;
        if (p < e && (*p == 'Z' || *p == 'z')) { p++; utc = true; }
        else if (p < e && (*p == '+' || *p == '-')) {
            int sg = *p == '-' ? -1 : 1;
            p++;
            int64_t oh, om = 0;
            if (xs_dnum(&p, e, 2, 2, &oh) < 0) return NAN;
            if (p < e && *p == ':') p++;
            if (p < e && xs_dnum(&p, e, 2, 2, &om) < 0) return NAN;
            if (p != e) return NAN;
            double t = xs_make_date((double)y, (double)(mon - 1), (double)d, (double)h, (double)mi, (double)s, ms);
            return xs_time_clip(t - sg * (double)(oh * 60 + om) * 60000);
        }
    }
    if (p != e) return NAN;
    double t = xs_make_date((double)y, (double)(mon - 1), (double)d, (double)h, (double)mi, (double)s, ms);
    (void)has_time;
    return xs_time_clip(utc ? t : xs_local_to_utc(t));
}
static const char *xs_month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *xs_day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static double xs_parse_legacy(const char *p, const char *e) {
    int64_t nums[8];
    int nn = 0;
    int64_t mon = -1, h = -1, mi = 0, s = 0;
    bool pm = false, am = false, utc = false, slash = false;
    double off = 0;
    bool has_off = false;
    while (p < e) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            int64_t v;
            const char *st = p;
            xs_dnum(&p, e, 1, 9, &v);
            if (p < e && *p == ':' && h < 0) { /* a time: h:mm[:ss] */
                h = v;
                p++;
                if (xs_dnum(&p, e, 1, 2, &mi) < 0) return NAN;
                if (p < e && *p == ':') { p++; if (xs_dnum(&p, e, 1, 2, &s) < 0) return NAN; }
                continue;
            }
            if (nn == 8) return NAN;
            (void)st;
            nums[nn++] = v;
            if (p < e && *p == '/') { slash = true; p++; }
            continue;
        }
        if ((c == '+' || c == '-') && p + 1 < e && p[1] >= '0' && p[1] <= '9' && (utc || h >= 0)) { /* GMT+0300, +03:00 */
            int sg = c == '-' ? -1 : 1;
            p++;
            int64_t v;
            int nd = xs_dnum(&p, e, 1, 4, &v);
            int64_t oh = nd <= 2 ? v : v / 100, om = nd <= 2 ? 0 : v % 100;
            if (p < e && *p == ':') { p++; xs_dnum(&p, e, 1, 2, &om); }
            off = sg * (double)(oh * 60 + om) * 60000;
            has_off = true;
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            const char *st = p;
            while (p < e && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) p++;
            size_t n = (size_t)(p - st);
            if ((n == 3 && strncasecmp(st, "GMT", 3) == 0) || (n == 3 && strncasecmp(st, "UTC", 3) == 0) || (n == 1 && (*st == 'Z' || *st == 'z'))) { utc = true; continue; }
            if (n == 2 && strncasecmp(st, "AM", 2) == 0) { am = true; continue; }
            if (n == 2 && strncasecmp(st, "PM", 2) == 0) { pm = true; continue; }
            bool known = false;
            for (int k = 0; k < 12 && n >= 3; k++) if (strncasecmp(st, xs_month_names[k], 3) == 0) { mon = k; known = true; }
            for (int k = 0; k < 7 && n >= 3 && !known; k++) if (strncasecmp(st, xs_day_names[k], 3) == 0) known = true;
            if (!known) return NAN;
            continue;
        }
        if (c == '(') { while (p < e && *p != ')') p++; if (p < e) p++; continue; } /* (Brasilia Standard Time) */
        if (c == ' ' || c == ',' || c == '.' || c == '-' || c == '/') { p++; continue; }
        return NAN;
    }
    int64_t y, m, d;
    if (mon >= 0) { /* Nov 6 2026 / 6 Nov 2026 */
        if (nn < 2) return NAN;
        m = mon;
        if (nums[0] > 31) { y = nums[0]; d = nums[1]; } else { d = nums[0]; y = nums[1]; }
    } else {
        if (nn < 3) return NAN;
        if (nums[0] > 31) { y = nums[0]; m = nums[1] - 1; d = nums[2]; } /* 2026/11/06 */
        else { m = nums[0] - 1; d = nums[1]; y = nums[2]; }             /* 11/06/2026 */
    }
    (void)slash;
    if (y >= 0 && y <= 49) y += 2000;
    else if (y >= 50 && y <= 99) y += 1900;
    if (h < 0) h = 0;
    if (pm && h < 12) h += 12;
    if (am && h == 12) h = 0;
    if (m < 0 || m > 11 || d < 1 || d > 31 || h > 24 || mi > 59 || s > 59) return NAN;
    double t = xs_make_date((double)y, (double)m, (double)d, (double)h, (double)mi, (double)s, 0);
    if (has_off) return xs_time_clip(t - off);
    return xs_time_clip(utc ? t : xs_local_to_utc(t));
}
XS_NOINLINE double xs_Date_parse(xs_str *s) {
    const char *p = s->data, *e = s->data + s->len;
    while (p < e && *p == ' ') p++;
    while (e > p && e[-1] == ' ') e--;
    double t = xs_parse_iso(p, e);
    if (isnan(t)) t = xs_parse_legacy(p, e);
    return t;
}
XS_NOINLINE xs_Date *xs_Date_new_str(xs_str *s) { return xs_date_make(xs_Date_parse(s)); }
/* new Date(v) with v an any: a Date is copied, a string parsed, anything else converted to a number (null is 0) */
XS_NOINLINE xs_Date *xs_Date_new_dyn(void *v) {
    if (v && xs_type_of(v)->free == xs_Date_free) return xs_date_make(((xs_Date *)v)->t);
    if (v && xs_type_of(v)->free == xs_str_free) return xs_Date_new_str(v);
    return xs_date_make(xs_dyn_num(v));
}

/* ---- formatting ---- */
static inline char *xs_dig(char *o, int64_t v, int w) { for (int i = w - 1; i >= 0; i--) { o[i] = (char)('0' + v % 10); v /= 10; } return o + w; }
static int xs_iso_into(double t, char *b) {
    xs_dfields f;
    xs_date_fields(t, false, &f);
    char *o = b;
    if (f.y >= 0 && f.y <= 9999) o = xs_dig(o, f.y, 4);
    else { *o++ = f.y < 0 ? '-' : '+'; o = xs_dig(o, f.y < 0 ? -f.y : f.y, 6); }
    *o++ = '-'; o = xs_dig(o, f.mon + 1, 2);
    *o++ = '-'; o = xs_dig(o, f.d, 2);
    *o++ = 'T'; o = xs_dig(o, f.h, 2);
    *o++ = ':'; o = xs_dig(o, f.mi, 2);
    *o++ = ':'; o = xs_dig(o, f.s, 2);
    *o++ = '.'; o = xs_dig(o, f.ms, 3);
    *o++ = 'Z';
    *o = 0;
    return (int)(o - b);
}
XS_NOINLINE void xr_date_range_error(void) {
    xs_Error *e = xs_Error_new(xs_str_new("Invalid time value", 18));
    xs_release(e->f_message);
    xs_release(e->f_name);
    e->f_name = xs_str_new("RangeError", 10);
    xs_throw(e);
}
XS_NOINLINE xs_str *xs_Date_toISOString(xs_Date *d) {
    if (isnan(d->t)) xr_date_range_error();
    char b[40];
    int n = xs_iso_into(d->t, b);
    return xs_str_new(b, (size_t)n);
}
static void xs_Date_print(void *p) {
    xs_Date *d = p;
    if (isnan(d->t)) { fputs("Invalid Date", XS_OUT); return; }
    char b[40];
    fwrite(b, 1, (size_t)xs_iso_into(d->t, b), XS_OUT);
}
static xs_str *xs_date_fmt(xs_Date *d, int kind) { /* 0 toString, 1 toDateString, 2 toTimeString, 3 toUTCString */
    if (isnan(d->t)) return xs_str_new("Invalid Date", 12);
    xs_dfields f;
    xs_date_fields(d->t, kind != 3, &f);
    char b[160], ys[16];
    if (f.y >= 0) snprintf(ys, sizeof ys, "%04lld", (long long)f.y); else snprintf(ys, sizeof ys, "-%06lld", (long long)-f.y);
    int n = 0;
    if (kind == 3) {
        n = snprintf(b, sizeof b, "%s, %02lld %s %s %02lld:%02lld:%02lld GMT", xs_day_names[f.wd], (long long)f.d, xs_month_names[f.mon], ys, (long long)f.h, (long long)f.mi, (long long)f.s);
        return xs_str_new(b, (size_t)n);
    }
    if (kind != 2) n += snprintf(b + n, sizeof b - (size_t)n, "%s %s %02lld %s", xs_day_names[f.wd], xs_month_names[f.mon], (long long)f.d, ys);
    if (kind != 1) {
        double off = xs_tz_offset(d->t) / 60000;
        int o = (int)off, ao = o < 0 ? -o : o;
        time_t secs = (time_t)floor(d->t / 1000);
        struct tm tm;
#if defined(_WIN32)
        localtime_s(&tm, &secs);
        char zone[64];
        size_t zl = 0;
        _get_tzname(&zl, zone, sizeof zone, tm.tm_isdst > 0); /* the system's own name: no ICU data */
        const char *zn = zone;
#else
        localtime_r(&secs, &tm);
        const char *zn = tm.tm_zone ? tm.tm_zone : "";
#endif
        n += snprintf(b + n, sizeof b - (size_t)n, "%s%02lld:%02lld:%02lld GMT%c%02d%02d (%s)", kind == 0 ? " " : "", (long long)f.h, (long long)f.mi, (long long)f.s,
                      o < 0 ? '-' : '+', ao / 60, ao % 60, zn);
    }
    return xs_str_new(b, (size_t)n);
}
XS_NOINLINE xs_str *xs_Date_toString(xs_Date *d) { return xs_date_fmt(d, 0); }
XS_NOINLINE xs_str *xs_Date_toDateString(xs_Date *d) { return xs_date_fmt(d, 1); }
XS_NOINLINE xs_str *xs_Date_toTimeString(xs_Date *d) { return xs_date_fmt(d, 2); }
XS_NOINLINE xs_str *xs_Date_toUTCString(xs_Date *d) { return xs_date_fmt(d, 3); }
/* toLocale*String() without arguments: en-US, as Node prints by default ("11/6/2026, 3:01:00 PM") */
static xs_str *xs_date_locale(xs_Date *d, int kind) { /* 0 both, 1 date, 2 time */
    if (isnan(d->t)) return xs_str_new("Invalid Date", 12);
    xs_dfields f;
    xs_date_fields(d->t, true, &f);
    char b[80];
    int n = 0;
    if (kind != 2) n += snprintf(b + n, sizeof b, "%lld/%lld/%lld", (long long)f.mon + 1, (long long)f.d, (long long)f.y);
    if (kind == 0) n += snprintf(b + n, sizeof b - (size_t)n, ", ");
    if (kind != 1) {
        int64_t h12 = f.h % 12 == 0 ? 12 : f.h % 12;
        n += snprintf(b + n, sizeof b - (size_t)n, "%lld:%02lld:%02lld %s", (long long)h12, (long long)f.mi, (long long)f.s, f.h < 12 ? "AM" : "PM");
    }
    return xs_str_new(b, (size_t)n);
}
XS_NOINLINE xs_str *xs_Date_toLocaleString(xs_Date *d) { return xs_date_locale(d, 0); }
XS_NOINLINE xs_str *xs_Date_toLocaleDateString(xs_Date *d) { return xs_date_locale(d, 1); }
XS_NOINLINE xs_str *xs_Date_toLocaleTimeString(xs_Date *d) { return xs_date_locale(d, 2); }

/* ---- getters: ints (0 for an invalid date, where JS has NaN) ---- */
static inline double xs_Date_getTime(xs_Date *d) { return d->t; }
#define XS_DATE_GET(name, field, local) \
    static int64_t xs_Date_##name(xs_Date *d) { xs_dfields f; return xs_date_fields(d->t, local, &f) ? f.field : 0; }
XS_DATE_GET(getFullYear, y, true) XS_DATE_GET(getMonth, mon, true) XS_DATE_GET(getDate, d, true) XS_DATE_GET(getDay, wd, true)
XS_DATE_GET(getHours, h, true) XS_DATE_GET(getMinutes, mi, true) XS_DATE_GET(getSeconds, s, true) XS_DATE_GET(getMilliseconds, ms, true)
XS_DATE_GET(getUTCFullYear, y, false) XS_DATE_GET(getUTCMonth, mon, false) XS_DATE_GET(getUTCDate, d, false) XS_DATE_GET(getUTCDay, wd, false)
XS_DATE_GET(getUTCHours, h, false) XS_DATE_GET(getUTCMinutes, mi, false) XS_DATE_GET(getUTCSeconds, s, false) XS_DATE_GET(getUTCMilliseconds, ms, false)
static int64_t xs_Date_getTimezoneOffset(xs_Date *d) { return isnan(d->t) ? 0 : (int64_t)(-xs_tz_offset(d->t) / 60000); }

/* ---- setters: set(d, which, n, a, b, c, e): `n` arguments given, the others keep the date's fields ---- */
XS_NOINLINE double xs_Date_set(xs_Date *d, int64_t which, int64_t n, double a, double b, double c, double e) {
    /* which: 0 FullYear(y, m, d), 1 Month(m, d), 2 Date(d), 3 Hours(h, mi, s, ms), 4 Minutes(mi, s, ms),
       5 Seconds(s, ms), 6 Milliseconds(ms); + 8: the UTC versions */
    bool local = which < 8;
    int w = (int)(which & 7);
    xs_dfields f;
    if (!xs_date_fields(d->t, local, &f)) {
        if (w != 0) return d->t; /* only setFullYear revives an invalid date */
        f = (xs_dfields){1970, 0, 1, 0, 0, 0, 0, 0};
    }
    double y = (double)f.y, mon = (double)f.mon, dd = (double)f.d, h = (double)f.h, mi = (double)f.mi, s = (double)f.s, ms = (double)f.ms;
    double v[4] = {a, b, c, e};
    double *slots[7] = {&y, &mon, &dd, &h, &mi, &s, &ms};
    int first = w == 0 ? 0 : w == 1 ? 1 : w == 2 ? 2 : w;
    int maxn = w == 0 ? 3 : w == 1 ? 2 : w == 2 ? 1 : w == 3 ? 4 : w == 4 ? 3 : w == 5 ? 2 : 1;
    for (int i = 0; i < n && i < maxn; i++) *slots[first + i] = v[i];
    double t = xs_make_date(y, mon, dd, h, mi, s, ms);
    d->t = xs_time_clip(local ? xs_local_to_utc(t) : t);
    return d->t;
}
XS_NOINLINE double xs_Date_setTime(xs_Date *d, double t) { d->t = xs_time_clip(t); return d->t; }
/* JSON: a Date is written as its ISO text (null when invalid, as toJSON gives); a typed read takes that text */
static void xs_sb_date(xs_sb *b, xs_Date *d) {
    if (isnan(d->t)) { xs_sb_put(b, "null", 4); return; }
    char t[40];
    int n = xs_iso_into(d->t, t);
    xs_sb_put(b, "\"", 1);
    xs_sb_put(b, t, (size_t)n);
    xs_sb_put(b, "\"", 1);
}
XS_NOINLINE xs_Date *xs_json_date(xs_jv *v, const char *where) {
    if (v->kind != XJ_STR) xs_panicf("JSON: '%s' must be a date (ISO text)", where);
    xs_str *s = xs_str_new(v->str, v->slen);
    xs_Date *d = xs_Date_new_str(s);
    xs_release(s);
    return d;
}

static xs_arr_ref *xs_argv;
static xs_map *xs_env;
static xs_arr_ref *xs_process_argv(void) { return xs_argv; }
static xs_map *xs_process_env(void) { return xs_env; }
static int64_t xs_process_pid(void) { return (int64_t)getpid(); } /* process.pid */
/* a key of a Map<number, V> / an element of a Set<number>: held as an int, so it must be a whole number */
XS_NOINLINE int64_t xs_num_key(double d) {
    if (d != d || d != trunc(d) || fabs(d) > 9007199254740991.0) {
        char b[40];
        snprintf(b, sizeof b, "%.17g", d);
        xs_panicf("a Map<number> key or Set<number> element must be a whole number (xisto), got %s", b);
    }
    return (int64_t)d;
}
XS_NOINLINE int64_t xs_num_key_find(double d) { /* m.get(n): a number that is not whole is a key no entry has */
    if (d != d || d != trunc(d) || fabs(d) > 9007199254740991.0) return INT64_MIN;
    return (int64_t)d;
}
XS_NOINLINE xs_arr_int *xs_num_keys(xs_arr_num *a) { /* new Set<number>(xs): the numbers as keys (the argument is borrowed) */
    xs_arr_int *r = xs_arr_int_new(0, 0);
    for (size_t i = 0; i < a->len; i++) xs_arr_int_push(r, xs_num_key(a->data[i]));
    return r;
}
static xs_str *xs_process_cwd(void) { /* process.cwd() */
    char b[4096];
#if defined(_WIN32)
    if (!_getcwd(b, (int)sizeof b)) return xs_str_new("", 0);
#else
    if (!getcwd(b, sizeof b)) return xs_str_new("", 0);
#endif
    return xs_str_new(b, strlen(b));
}
static void xs_process_exit(int64_t code) { fflush(stdout); exit((int)code); }
static int xs_exit_code_; /* process.exitCode: the status when the program ends on its own */
static int64_t xs_process_set_exit_code(int64_t code) { xs_exit_code_ = (int)code; return code; }
/* process.stdout.write / process.stderr.write: through the same FILE as console.log, so the order is kept */
static inline void xs_out_drain(void);
static bool xs_stdout_write_str(xs_str *s) { xs_out_drain(); fwrite(s->data, 1, (size_t)s->len, XS_OUT); return true; }
static bool xs_stderr_write_str(xs_str *s) { xs_out_drain(); fwrite(s->data, 1, (size_t)s->len, stderr); return true; }

#if !defined(_WIN32) /* there, the C library's environ is a macro over a function */
extern char **environ __attribute__((visibility("default"))); /* the C library's: not one of the runtime's hidden symbols */
#endif
static void xs_std_init(int argc, char **argv) {
    xs_argv = xs_arr_ref_new((size_t)(argc + 1), xs_print_str_quoted_);
    /* like Node: [runtime, script, ...args]; the executable stands for both */
    xs_arr_ref_push(xs_argv, xs_str_new(argc ? argv[0] : "", argc ? strlen(argv[0]) : 0));
    for (int i = 0; i < argc; i++) xs_arr_ref_push(xs_argv, xs_str_new(argv[i], strlen(argv[i])));
    xs_env = xs_map_new(1, 1, 3, xs_print_str_quoted_, xs_print_str_quoted_);
    for (char **e = environ; e && *e; e++) {
        const char *eq = strchr(*e, '=');
        if (!eq) continue;
        xs_str *k = xs_str_new(*e, (size_t)(eq - *e));
        xs_map_set_s(xs_env, k, XS_VPTR(xs_str_new(eq + 1, strlen(eq + 1))));
        xs_release(k);
    }
    xs_argv->hdr.rc = XS_IMMORTAL; /* process-lifetime objects: not counted as allocations or leaks */
    xs_env->hdr.rc = XS_IMMORTAL;
    xs_live = 0;
    xs_allocs = 0;
}

/* all of a stream that cannot seek (a pipe, the terminal, /dev/stdin), in a malloc'd buffer with `front` bytes
   kept free at the start (a string's header) and one more at the end (its NUL) */
#if !defined(_WIN32)
#include <sys/stat.h>
#endif
static char *xs_read_stream(FILE *f, size_t front, size_t *len) {
    size_t cap = front + 65536, n = 0;
#if !defined(_WIN32)
    { /* a regular file (stdin redirected from one): its size, so the buffer is made once instead of doubling */
        struct stat st;
        if (fstat(fileno(f), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
            off_t at = ftello(f);
            if (at >= 0 && st.st_size > at) cap = front + (size_t)(st.st_size - at) + 2;
        }
    }
#endif
    char *buf = malloc(cap);
    if (!buf) xs_panic("out of memory");
    xs_big_hint(buf, (int64_t)cap);
    for (;;) {
        if (front + n + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); if (!buf) xs_panic("out of memory"); xs_big_hint(buf, (int64_t)cap); }
        size_t got = fread(buf + front + n, 1, cap - front - n - 1, f);
        n += got;
        if (got == 0) break;
    }
    *len = n;
    return buf;
}
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
static void xs_stdin_binary(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY); /* the bytes as they are, \r\n too, as Node reads them */
#endif
}
/* readFileSync(0, "utf8") and readFileSync(0): all of standard input */
XS_NOINLINE xs_str *xs_fs_read_stdin(void) {
    xs_stdin_binary();
    size_t n;
    char *buf = xs_read_stream(stdin, offsetof(xs_str, data), &n);
    buf[offsetof(xs_str, data) + n] = 0;
    return xs_str_adopt(buf, n);
}
XS_NOINLINE xs_str *xs_fs_read(xs_str *path) {
    FILE *f = fopen(path->data, "rb");
    if (!f) xs_panicf("ENOENT: no such file or directory, open '%s'", path->data);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { /* not a regular file (/dev/stdin on a pipe): read until the end */
        size_t m;
        char *buf = xs_read_stream(f, offsetof(xs_str, data), &m);
        fclose(f);
        buf[offsetof(xs_str, data) + m] = 0;
        return xs_str_adopt(buf, m);
    }
    fseek(f, 0, SEEK_SET);
    xs_str *s = xs_str_alloc(n > 0 ? n : 0);
    if (n > 0 && fread(s->data, 1, (size_t)n, f) != (size_t)n) { fclose(f); xs_panicf("EIO: cannot read '%s'", path->data); }
    s->data[s->len] = 0;
    fclose(f);
    return s;
}
XS_NOINLINE void xs_fs_write_mode(xs_str *path, xs_str *data, const char *mode) {
    FILE *f = fopen(path->data, mode);
    if (!f) xs_panicf("EACCES: cannot open '%s' for writing", path->data);
    fwrite(data->data, 1, (size_t)data->len, f);
    fclose(f);
}
XS_NOINLINE void xs_fs_write(xs_str *path, xs_str *data) { xs_fs_write_mode(path, data, "wb"); }
XS_NOINLINE void xs_fs_append(xs_str *path, xs_str *data) { xs_fs_write_mode(path, data, "ab"); }
static bool xs_fs_exists(xs_str *path) { return access(path->data, F_OK) == 0; }

/* ---- bytes: Uint8Array (and Buffer, the same object printed the Node way), TextEncoder/Decoder, base64/hex ----
   An array owns its bytes, or is a view (subarray) on another one's, which it keeps alive. Indexing is bounds
   checked like arrays; a write keeps the low 8 bits, as in JS. */
typedef struct xs_Uint8Array { xs_hdr hdr; int64_t f_length; uint8_t *data; void *owner; } xs_Uint8Array;
/* 16 bytes in a register (GCC vector extension); the shuffle is one instruction on SSSE3 and NEON, a loop
   elsewhere */
typedef uint8_t xs_u8x16 __attribute__((vector_size(16)));
static inline xs_u8x16 xs_u8x16_splat(uint8_t v) { return (xs_u8x16){v, v, v, v, v, v, v, v, v, v, v, v, v, v, v, v}; }
static inline xs_u8x16 xs_u8x16_iota(void) { return (xs_u8x16){0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}; }
static inline xs_u8x16 xs_u8x16_load(const uint8_t *p) { xs_u8x16 v; memcpy(&v, p, 16); return v; }
static inline void xs_u8x16_store(uint8_t *p, xs_u8x16 v) { memcpy(p, &v, 16); }
#if defined(__SSSE3__)
#include <tmmintrin.h>
static inline xs_u8x16 xs_u8x16_shuffle(xs_u8x16 v, xs_u8x16 m) { return (xs_u8x16)_mm_shuffle_epi8((__m128i)v, (__m128i)m); }
#elif defined(__ARM_NEON)
#include <arm_neon.h>
static inline xs_u8x16 xs_u8x16_shuffle(xs_u8x16 v, xs_u8x16 m) { return (xs_u8x16)vqtbl1q_u8((uint8x16_t)v, (uint8x16_t)m); }
#else
static inline xs_u8x16 xs_u8x16_shuffle(xs_u8x16 v, xs_u8x16 m) { xs_u8x16 r; for (int i = 0; i < 16; i++) r[i] = (m[i] & 0x80) ? 0 : v[m[i] & 15]; return r; }
#endif
/* A local Uint8Array(N <= 16) that a loop only indexes (emit.c u8reg) lives in one of these for the loop's
   duration, loaded before and stored after: reads, writes, the reversal and the copy idioms never touch
   memory, so no byte store ever sits before a 16-byte load (a store that narrow is never forwarded to a load
   that wide: Intel and Apple alike, ~14 cycles per flip of fannkuch-redux). The reversal's mask comes from a
   table (one load, off the critical path), as the Rust fannkuch-redux reads its MASK[k]. */
static xs_u8x16 xs_rev_mask[16][16]; /* [lo][hi]: i outside [lo, hi], lo + hi - i inside (xs_init fills it) */
static xs_u8x16 xs_shl_mask[16][16]; /* [lo][hi]: i outside [lo, hi], i + 1 inside: a[j] = a[j + 1] for lo <= j <= hi */
static xs_u8x16 xs_lane_mask[16];    /* [i]: 0xff at lane i (a set: one load instead of splat + compare) */
static xs_u8x16 xs_rot_mask[16][16]; /* [lo][hi]: lanes lo..hi rotated left by one (lo takes lo + 1, ..., hi takes lo) */
static void xs_rev_mask_init(void) {
    for (int lo = 0; lo < 16; lo++)
        for (int hi = 0; hi < 16; hi++)
            for (int i = 0; i < 16; i++) {
                xs_rev_mask[lo][hi][i] = (uint8_t)(lo <= i && i <= hi ? lo + hi - i : i);
                xs_shl_mask[lo][hi][i] = (uint8_t)(lo <= i && i <= hi ? (i + 1) & 15 : i);
            }
    for (int l = 0; l < 16; l++) for (int i = 0; i < 16; i++) xs_lane_mask[l][i] = (uint8_t)(i == l ? 0xff : 0);
    for (int lo = 0; lo < 16; lo++)
        for (int hi = 0; hi < 16; hi++)
            for (int i = 0; i < 16; i++) xs_rot_mask[lo][hi][i] = (uint8_t)(lo <= i && i <= hi ? (i == hi ? lo : i + 1) : i);
}
/* n, the register's length: a Uint8Array's, or an int[]'s with XS_V16_ARR set (emit.c u8reg, int arrays proved to
   hold bytes): the same checks, each with the message of the array the program wrote */
#define XS_V16_ARR (INT64_C(1) << 32)
#define XS_V16_LEN(n) ((n) & INT64_C(0xffffffff))
#define XS_V16_OOB(n) (((n) & XS_V16_ARR) ? "array index out of bounds" : "index out of bounds")
static inline xs_u8x16 xs_v16_load(const uint8_t *d, int64_t n) { if (n >= 16) return xs_u8x16_load(d); xs_u8x16 v = xs_u8x16_splat(0); memcpy(&v, d, (size_t)n); return v; }
static inline void xs_v16_store(uint8_t *d, xs_u8x16 v, int64_t n) { if (n >= 16) xs_u8x16_store(d, v); else memcpy(d, &v, (size_t)n); }
/* an int[] of n <= 16 elements, each a byte (proved), in a register and back */
static inline xs_u8x16 xs_v16_load_i64(const int64_t *d, int64_t n) { xs_u8x16 v = xs_u8x16_splat(0); for (int64_t i = 0; i < n; i++) v[i] = (uint8_t)d[i]; return v; }
static inline void xs_v16_store_i64(int64_t *d, xs_u8x16 v, int64_t n) { for (int64_t i = 0; i < n; i++) d[i] = (int64_t)v[i]; }
static inline int64_t xs_v16_get_nc(xs_u8x16 v, int64_t i) { return (int64_t)xs_u8x16_shuffle(v, xs_u8x16_splat((uint8_t)i))[0]; }
static inline int64_t xs_v16_get(xs_u8x16 v, int64_t i, int64_t n, const xs_site *s) {
    if ((uint64_t)i >= (uint64_t)XS_V16_LEN(n)) XS_FAIL(XS_V16_OOB(n), s);
    return xs_v16_get_nc(v, i);
}
static inline xs_u8x16 xs_v16_set_nc(xs_u8x16 v, int64_t i, int64_t b) { /* (i < 16: checked, or proved) */
    xs_u8x16 m = xs_lane_mask[i & 15];
    return (v & ~m) | (xs_u8x16_splat((uint8_t)b) & m);
}
static inline xs_u8x16 xs_v16_set(xs_u8x16 v, int64_t i, int64_t b, int64_t n, const xs_site *s) {
    if ((uint64_t)i >= (uint64_t)XS_V16_LEN(n)) XS_FAIL(XS_V16_OOB(n), s);
    return xs_v16_set_nc(v, i, b);
}
/* the swap loop (xs_u8_rev) on a register value */
static inline xs_u8x16 xs_v16_rev(xs_u8x16 v, int64_t lo, int64_t hi, int64_t n, const xs_site *s) {
    if (lo >= hi) return v;
    if (lo < 0 || (uint64_t)hi >= (uint64_t)XS_V16_LEN(n)) XS_FAIL(XS_V16_OOB(n), s);
    return xs_u8x16_shuffle(v, xs_rev_mask[lo][hi]);
}
/* the element xs_v16_rev(v, lo, hi) brings to lo, from v before the shuffle (0 when the reversal fails: it
   throws first) */
static inline int64_t xs_v16_rev_head(xs_u8x16 v, int64_t lo, int64_t hi, int64_t n) {
    int64_t at = lo < hi ? hi : lo;
    return (uint64_t)at < (uint64_t)XS_V16_LEN(n) && at >= 0 ? xs_v16_get_nc(v, at) : 0;
}
/* the same, when the compiler proved 0 <= lo and hi below the length (an index value of the array: ix_prove) */
static inline xs_u8x16 xs_v16_rev_nc(xs_u8x16 v, int64_t lo, int64_t hi) {
    if (lo >= hi) return v;
    return xs_u8x16_shuffle(v, xs_rev_mask[lo][hi]);
}
static inline int64_t xs_v16_rev_head_nc(xs_u8x16 v, int64_t lo, int64_t hi, int64_t n) {
    (void)n;
    return xs_v16_get_nc(v, lo < hi ? hi : lo);
}
/* a[lo] right after the reversal: the element above, with a[lo]'s own check */
static inline int64_t xs_v16_head_after(int64_t h, int64_t lo, int64_t n, const xs_site *s) {
    if ((uint64_t)lo >= (uint64_t)XS_V16_LEN(n)) XS_FAIL(XS_V16_OOB(n), s);
    return h;
}
static inline xs_u8x16 xs_v16_range(int64_t lo, int64_t hi) { /* lanes lo <= i < hi */
    xs_u8x16 idx = xs_u8x16_iota();
    return (idx >= xs_u8x16_splat((uint8_t)lo)) & (idx < xs_u8x16_splat((uint8_t)hi));
}
/* `for (j = lo; j < hi; j++) dst[j] = src[j]` (xs_u8_copy_range) with one side or both in a register (the read
   of src checked first, as the loop reads before it stores) */
static inline xs_u8x16 xs_v16_copy_rr(xs_u8x16 dst, xs_u8x16 src, int64_t lo, int64_t hi, int64_t ndst, int64_t nsrc, const xs_site *s) {
    if (lo >= hi) return dst;
    if (lo < 0 || (uint64_t)(hi - 1) >= (uint64_t)XS_V16_LEN(nsrc)) XS_FAIL(XS_V16_OOB(nsrc), s);
    if ((uint64_t)(hi - 1) >= (uint64_t)XS_V16_LEN(ndst)) XS_FAIL(XS_V16_OOB(ndst), s);
    xs_u8x16 m = xs_v16_range(lo, hi);
    return (dst & ~m) | (src & m);
}
static inline xs_u8x16 xs_v16_copy_in(xs_u8x16 dst, xs_Uint8Array *src, int64_t lo, int64_t hi, int64_t ndst, const xs_site *s) {
    if (lo >= hi) return dst;
    if (lo < 0 || (uint64_t)(hi - 1) >= (uint64_t)src->f_length) XS_FAIL("index out of bounds", s);
    if ((uint64_t)(hi - 1) >= (uint64_t)XS_V16_LEN(ndst)) XS_FAIL(XS_V16_OOB(ndst), s);
    xs_u8x16 m = xs_v16_range(lo, hi);
    return (dst & ~m) | (xs_v16_load(src->data, src->f_length) & m);
}
static inline void xs_v16_copy_out(xs_Uint8Array *dst, xs_u8x16 src, int64_t lo, int64_t hi, int64_t nsrc, const xs_site *s) {
    if (lo >= hi) return;
    if (lo < 0 || (uint64_t)(hi - 1) >= (uint64_t)XS_V16_LEN(nsrc)) XS_FAIL(XS_V16_OOB(nsrc), s);
    if ((uint64_t)(hi - 1) >= (uint64_t)dst->f_length) XS_FAIL("index out of bounds", s);
    memcpy(dst->data + lo, (const uint8_t *)&src + lo, (size_t)(hi - lo));
}

#include <errno.h>
/* a big block goes to the file with write(2), after what stdio holds: through stdio's small buffer, 254 MB to
   /dev/null took 140 ms (macOS copies it piece by piece); written straight it is a single call */
static void xs_write_big(FILE *f, const void *p, size_t n) {
#if !defined(_WIN32)
    if (n >= 65536 && !xs_out_f) {
        fflush(f);
        int fd = fileno(f);
        const char *q = p;
        while (n > 0) {
            ssize_t w = write(fd, q, n);
            if (w < 0) { if (errno == EINTR) continue; fwrite(q, 1, n, f); return; } /* (let stdio report it) */
            q += w;
            n -= (size_t)w;
        }
        return;
    }
#endif
    fwrite(p, 1, n, f);
}
/* stdin still being read (XB-091): set by readSync on fd 0 while its end has not been reached (a file: the offset
   short of its size; anything else: no read of 0 bytes yet). Writing behind pays only then: the program has
   reading left to overlap with the write, or a write still going out ahead of it; after the input, with nothing
   pending (or with no input), a write at the call costs no copy. */
static int xs_stdin_reading;
/* ---- stdout written behind (XB-091): a big block for stdout (1 MB or more, stdout a pipe or a file, not a
   terminal), while stdin is still being read, is copied into a ring of 64 MB and written by a thread of its own,
   so the program goes on (reading and computing the next part) while the reader of the pipe takes it. The copy is made at the call, so what is
   written is what the bytes were then, as Node's synchronous write; every other way to stdout waits for the ring
   first (console.*, process.stdout.write of a string or a small block: xs_out_drain), and so does the exit
   (atexit), so the order is the program's. XISTO_NO_WRITE_BEHIND=1: written at the call, as before. */
#if !defined(_WIN32)
#include <pthread.h>
static struct {
    pthread_mutex_t mu;
    pthread_cond_t more, less;
    char *buf;
    size_t head, tail, used; /* the bytes to write: buf[head..] for used bytes, wrapping at XS_AW_CAP */
    int state;               /* 0: not decided, 1: on, -1: off */
} xs_aw = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER, NULL, 0, 0, 0, 0};
static volatile size_t xs_aw_pending; /* (read without the lock: only a hint that xs_out_drain has work) */
/* a lent write (writeStdoutLent): the bytes of a part read in place, written by the thread straight from the part,
   with no copy into the ring. The compiler lends a part only where the program writes none of its bytes after the
   call (src/inplace.c: the last statement of the loop); the only other writer of those bytes is the parts' reader,
   through Uint8Array.set and readSync, and both wait (xs_lent_wait) until the thread has written past what they
   overwrite. So what goes out is what the bytes were at the call, as Node's synchronous write. The part is
   retained while lent and released by the program's thread (refcounts are not atomic). */
typedef struct {
    xs_Uint8Array *b;   /* the part, retained */
    const uint8_t *p;
    size_t n, done;     /* done: written so far (under xs_aw.mu) */
    size_t ready;       /* bytes from p that are final: the thread writes no further (n once the write is called) */
    int active;         /* being written */
} xs_lent_slot;
/* writeStdoutLentBegin (src/inplace.c, before `reverse(part, ...)` that comes just before `writeStdoutLent(part)`):
   the part is lent before the reverse, with nothing final yet; the reverse over several threads publishes the front
   of the part as its outer pairs are done (xs_lent_publish), so the thread writes the start of the output while
   the rest is still turned; writeStdoutLent then makes all of it final. A part begun while another is going out
   waits in `next`, so the reverse overlaps the write of the part before. Bytes are written only once final: what
   goes out is what the program's write would write. */
static struct {
    xs_lent_slot cur, next; /* next: begun, waiting for cur to go out */
    xs_Uint8Array *spent[2]; /* written, still to release (by the program's thread) */
} xs_lent;
#define XS_AW_CAP ((size_t)64 << 20) /* (a ring that holds a big part whole frees the program to read the next while the last is written: 32 MB 0,150 s, 64 MB 0,133 s on 250 MB, -5 0,126) */
#define XS_AW_STEP ((size_t)1 << 20) /* each copy and each write: the ring frees and fills a step at a time, so neither side waits for a whole ring */
static void *xs_aw_main(void *arg) {
    (void)arg;
    int fd = fileno(stdout);
    for (;;) {
        pthread_mutex_lock(&xs_aw.mu);
        for (;;) {
            if (!xs_lent.cur.active && xs_lent.next.active) { xs_lent.cur = xs_lent.next; xs_lent.next.active = 0; xs_lent.next.b = NULL; }
            if (xs_aw.used > 0 || (xs_lent.cur.active && xs_lent.cur.done < xs_lent.cur.ready)) break;
            pthread_cond_wait(&xs_aw.more, &xs_aw.mu);
        }
        if (xs_lent.cur.active) { /* (the ring is empty while a lent write goes out: xs_stdout_write_lent waits for it) */
            size_t k = xs_lent.cur.ready - xs_lent.cur.done < XS_AW_STEP ? xs_lent.cur.ready - xs_lent.cur.done : XS_AW_STEP;
            const uint8_t *lq = xs_lent.cur.p + xs_lent.cur.done;
            pthread_mutex_unlock(&xs_aw.mu);
            size_t lleft = k;
            while (lleft > 0) {
                ssize_t w = write(fd, lq, lleft);
                if (w < 0 && errno == EINTR) continue;
                if (w <= 0) break;
                lq += w;
                lleft -= (size_t)w;
            }
            pthread_mutex_lock(&xs_aw.mu);
            xs_lent.cur.done += k;
            if (xs_lent.cur.done >= xs_lent.cur.n) {
                xs_lent.cur.active = 0;
                xs_lent.spent[xs_lent.spent[0] ? 1 : 0] = xs_lent.cur.b;
                xs_lent.cur.b = NULL;
            }
            xs_aw_pending = xs_aw.used + (xs_lent.cur.active ? xs_lent.cur.n - xs_lent.cur.done : 0) + (xs_lent.next.active ? xs_lent.next.n : 0);
            pthread_cond_broadcast(&xs_aw.less);
            pthread_mutex_unlock(&xs_aw.mu);
            continue;
        }
        size_t span = xs_aw.used < XS_AW_CAP - xs_aw.head ? xs_aw.used : XS_AW_CAP - xs_aw.head;
        if (span > XS_AW_STEP) span = XS_AW_STEP;
        const char *q = xs_aw.buf + xs_aw.head;
        pthread_mutex_unlock(&xs_aw.mu);
        size_t left = span;
        while (left > 0) { /* (a failed write drops the rest of the span, as a closed pipe: the reader is gone) */
            ssize_t w = write(fd, q, left);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) break;
            q += w;
            left -= (size_t)w;
        }
        pthread_mutex_lock(&xs_aw.mu);
        xs_aw.head = (xs_aw.head + span) % XS_AW_CAP;
        xs_aw.used -= span;
        xs_aw_pending = xs_aw.used;
        pthread_cond_broadcast(&xs_aw.less);
        pthread_mutex_unlock(&xs_aw.mu);
    }
    return NULL;
}
static void xs_lent_release(void) { /* (the program's thread, under xs_aw.mu) */
    for (int i = 0; i < 2; i++) if (xs_lent.spent[i]) { xs_release(xs_lent.spent[i]); xs_lent.spent[i] = NULL; }
}
#define XS_LENT_BUSY() (xs_lent.cur.active || xs_lent.next.active)
static void xs_lent_cancel_open(void) { /* (under xs_aw.mu) a begun part never called to be written (an exception
    between the two calls): what is not out yet is dropped, as the program never wrote it */
    if (xs_lent.cur.active && xs_lent.cur.ready < xs_lent.cur.n) xs_lent.cur.n = xs_lent.cur.ready > xs_lent.cur.done ? xs_lent.cur.ready : xs_lent.cur.done;
    if (xs_lent.cur.active && xs_lent.cur.done >= xs_lent.cur.n) { xs_lent.cur.active = 0; xs_lent.spent[xs_lent.spent[0] ? 1 : 0] = xs_lent.cur.b; xs_lent.cur.b = NULL; }
    if (xs_lent.next.active && xs_lent.next.ready < xs_lent.next.n) { xs_lent.next.n = xs_lent.next.ready; if (xs_lent.next.n == 0) { xs_lent.next.active = 0; xs_lent.spent[xs_lent.spent[0] ? 1 : 0] = xs_lent.next.b; xs_lent.next.b = NULL; } }
    pthread_cond_broadcast(&xs_aw.more);
}
static void xs_out_drain_slow(void) {
    pthread_mutex_lock(&xs_aw.mu);
    xs_lent_cancel_open();
    while (xs_aw.used > 0 || XS_LENT_BUSY()) pthread_cond_wait(&xs_aw.less, &xs_aw.mu);
    xs_lent_release();
    pthread_mutex_unlock(&xs_aw.mu);
}
/* before the bytes [p, p + n) are overwritten: wait until a lent write has gone past them */
static void xs_lent_wait(const uint8_t *p, size_t n) {
    if (!XS_LENT_BUSY() && !xs_lent.spent[0]) return; /* (a hint read without the lock: set only by this thread, cleared by the writer) */
    pthread_mutex_lock(&xs_aw.mu);
    for (;;) {
        bool c = xs_lent.cur.active && p < xs_lent.cur.p + xs_lent.cur.n && xs_lent.cur.p + xs_lent.cur.done < p + n;
        bool x = xs_lent.next.active && p < xs_lent.next.p + xs_lent.next.n && xs_lent.next.p < p + n;
        if (!c && !x) break;
        pthread_cond_wait(&xs_aw.less, &xs_aw.mu);
    }
    xs_lent_release();
    pthread_mutex_unlock(&xs_aw.mu);
}
static inline void xs_out_drain(void) { if (xs_aw_pending || xs_lent.spent[0] || XS_LENT_BUSY()) xs_out_drain_slow(); }
static void xs_aw_atexit(void) { xs_out_drain(); }
static bool xs_aw_on(void) {
    if (xs_aw.state == 0) {
        struct stat st;
        const char *off = getenv("XISTO_NO_WRITE_BEHIND");
        bool ok = !(off && *off && strcmp(off, "0") != 0) && fstat(fileno(stdout), &st) == 0 && !isatty(fileno(stdout)) &&
                  (S_ISFIFO(st.st_mode) || S_ISREG(st.st_mode) || S_ISSOCK(st.st_mode));
        pthread_t th;
        if (ok) ok = (xs_aw.buf = malloc(XS_AW_CAP)) != NULL;
        if (ok) {
            pthread_attr_t at;
            pthread_attr_init(&at);
            pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
            ok = pthread_create(&th, &at, xs_aw_main, NULL) == 0;
            pthread_attr_destroy(&at);
        }
        if (ok) atexit(xs_aw_atexit);
        xs_aw.state = ok ? 1 : -1;
    }
    return xs_aw.state > 0;
}
static void xs_aw_put(const uint8_t *p, size_t n) {
    fflush(stdout); /* what stdio holds came before */
    if (XS_LENT_BUSY() || xs_lent.spent[0]) { /* a lent write goes first */
        pthread_mutex_lock(&xs_aw.mu);
        xs_lent_cancel_open();
        while (XS_LENT_BUSY()) pthread_cond_wait(&xs_aw.less, &xs_aw.mu);
        xs_lent_release();
        pthread_mutex_unlock(&xs_aw.mu);
    }
    while (n > 0) {
        pthread_mutex_lock(&xs_aw.mu);
        while (xs_aw.used == XS_AW_CAP) pthread_cond_wait(&xs_aw.less, &xs_aw.mu);
        size_t room = XS_AW_CAP - xs_aw.used, upto = XS_AW_CAP - xs_aw.tail;
        size_t k = n < room ? n : room;
        if (k > upto) k = upto;
        if (k > XS_AW_STEP) k = XS_AW_STEP;
        char *dst = xs_aw.buf + xs_aw.tail;
        pthread_mutex_unlock(&xs_aw.mu);
        memcpy(dst, p, k); /* (the free part of the ring: the writer never reads it) */
        pthread_mutex_lock(&xs_aw.mu);
        xs_aw.tail = (xs_aw.tail + k) % XS_AW_CAP;
        xs_aw.used += k;
        xs_aw_pending = xs_aw.used;
        pthread_cond_signal(&xs_aw.more);
        pthread_mutex_unlock(&xs_aw.mu);
        p += k;
        n -= k;
    }
}
/* Linux: stdout a pipe and a block of 256 KB or more for it: the pipe's buffer raised to 1 MB, once (F_SETPIPE_SZ, up
   to /proc/sys/fs/pipe-max-size, 1 MB by default). With the default 64 KB a big write goes out 64 KB at a time,
   each one waking the reader and then the writer again; with two cores busy computing, those switches cost more
   than the bytes (revcomp-9 on the Graviton2: 0,57 -> 0,31 s). XISTO_NO_PIPE_GROW=1 leaves it. */
#if defined(__linux__)
#include <fcntl.h>
#endif
static void xs_out_pipe_grow(size_t n) {
#if defined(__linux__) && defined(F_SETPIPE_SZ)
    static int done = 0;
    if (done || n < ((size_t)1 << 18)) return;
    done = 1;
    if (getenv("XISTO_NO_PIPE_GROW")) return;
    struct stat st;
    int fd = fileno(stdout);
    if (fstat(fd, &st) == 0 && S_ISFIFO(st.st_mode) && fcntl(fd, F_GETPIPE_SZ) < (1 << 20)) fcntl(fd, F_SETPIPE_SZ, 1 << 20);
#else
    (void)n;
#endif
}
static bool xs_stdout_write_bytes(xs_Uint8Array *b) {
    size_t n = (size_t)b->f_length;
    xs_out_pipe_grow(n);
    if (n >= ((size_t)1 << 20) && (xs_stdin_reading || xs_aw_pending) && !xs_out_f && xs_aw_on()) { xs_aw_put(b->data, n); return true; } /* (behind a write still going out: its turn comes then anyway) */
    xs_out_drain();
    xs_write_big(XS_OUT, b->data, n);
    return true;
}
/* writeStdoutLent (xisto:io; only the compiler calls it, src/inplace.c): as xs_stdout_write_bytes, but a block
   written behind goes out straight from the part instead of a copy in the ring (see xs_lent).
   XISTO_NO_LENT_WRITE=1: the copy, as writeStdout. */
static bool xs_stdout_write_lent(xs_Uint8Array *b) {
    static int off = -1;
    if (off < 0) { const char *e = getenv("XISTO_NO_LENT_WRITE"); off = e && *e && strcmp(e, "0") != 0; }
    size_t n = (size_t)b->f_length;
    xs_out_pipe_grow(n);
    if (XS_LENT_BUSY()) { /* begun (writeStdoutLentBegin): now all of it is final */
        pthread_mutex_lock(&xs_aw.mu);
        xs_lent_slot *s = xs_lent.next.active && xs_lent.next.b == b ? &xs_lent.next : xs_lent.cur.active && xs_lent.cur.b == b ? &xs_lent.cur : NULL;
        if (s && s->p == b->data && s->n == n) {
            s->ready = n;
            pthread_cond_signal(&xs_aw.more);
            pthread_mutex_unlock(&xs_aw.mu);
            return true;
        }
        pthread_mutex_unlock(&xs_aw.mu);
    }
    if (off || !(n >= ((size_t)1 << 20) && (xs_stdin_reading || xs_aw_pending) && !xs_out_f && xs_aw_on())) return xs_stdout_write_bytes(b);
    fflush(stdout);
    pthread_mutex_lock(&xs_aw.mu);
    xs_lent_cancel_open();
    while (xs_aw.used > 0 || XS_LENT_BUSY()) pthread_cond_wait(&xs_aw.less, &xs_aw.mu); /* (in order: what went before goes first) */
    xs_lent_release();
    xs_lent.cur = (xs_lent_slot){xs_retain(b), b->data, n, 0, n, 1};
    xs_aw_pending = n;
    pthread_cond_signal(&xs_aw.more);
    pthread_mutex_unlock(&xs_aw.mu);
    return true;
}
/* writeStdoutLentBegin (xisto:io; only the compiler calls it): the part is lent with nothing final yet, behind the
   one going out if there is one (next); XISTO_NO_LENT_EARLY=1: nothing (writeStdoutLent then writes it) */
static void xs_stdout_lent_begin(xs_Uint8Array *b) {
    static int off = -1;
    if (off < 0) { const char *e = getenv("XISTO_NO_LENT_EARLY"), *f = getenv("XISTO_NO_LENT_WRITE"); off = (e && *e && strcmp(e, "0") != 0) || (f && *f && strcmp(f, "0") != 0); }
    size_t n = (size_t)b->f_length;
    if (off || !(n >= ((size_t)1 << 20) && (xs_stdin_reading || xs_aw_pending) && !xs_out_f && xs_aw_on())) return;
    fflush(stdout);
    pthread_mutex_lock(&xs_aw.mu);
    xs_lent_slot *s = NULL;
    if (xs_aw.used == 0 && !xs_lent.cur.active && !xs_lent.next.active) s = &xs_lent.cur;
    else if (xs_aw.used == 0 && xs_lent.cur.active && xs_lent.cur.ready == xs_lent.cur.n && !xs_lent.next.active) s = &xs_lent.next;
    if (s) {
        xs_lent_release();
        *s = (xs_lent_slot){xs_retain(b), b->data, n, 0, 0, 1};
        xs_aw_pending += n;
    }
    pthread_mutex_unlock(&xs_aw.mu);
}
/* the bytes before upto (inside a begun part) are final: the thread may write them */
static void xs_lent_publish(const uint8_t *upto) {
    pthread_mutex_lock(&xs_aw.mu);
    xs_lent_slot *ss[2] = {&xs_lent.cur, &xs_lent.next};
    for (int i = 0; i < 2; i++) {
        xs_lent_slot *s = ss[i];
        if (s->active && s->ready < s->n && upto >= s->p && upto <= s->p + s->n && (size_t)(upto - s->p) > s->ready) s->ready = (size_t)(upto - s->p);
    }
    pthread_cond_signal(&xs_aw.more);
    pthread_mutex_unlock(&xs_aw.mu);
}
/* a reverse over [p, p + n) inside a begun part not yet final: it may publish (a hint read without the lock) */
static bool xs_lent_open(const uint8_t *p, size_t n) {
    const xs_lent_slot *ss[2] = {&xs_lent.cur, &xs_lent.next};
    for (int i = 0; i < 2; i++) if (ss[i]->active && ss[i]->ready < ss[i]->n && p >= ss[i]->p && p + n <= ss[i]->p + ss[i]->n) return true;
    return false;
}
#else
static inline void xs_out_drain(void) {}
static inline void xs_lent_wait(const uint8_t *p, size_t n) { (void)p; (void)n; }
static bool xs_stdout_write_bytes(xs_Uint8Array *b) { xs_write_big(XS_OUT, b->data, (size_t)b->f_length); return true; }
static bool xs_stdout_write_lent(xs_Uint8Array *b) { return xs_stdout_write_bytes(b); }
static inline void xs_stdout_lent_begin(xs_Uint8Array *b) { (void)b; }
static inline void xs_lent_publish(const uint8_t *upto) { (void)upto; }
static inline bool xs_lent_open(const uint8_t *p, size_t n) { (void)p; (void)n; return false; }
#endif
static bool xs_stderr_write_bytes(xs_Uint8Array *b) { xs_out_drain(); xs_write_big(stderr, b->data, (size_t)b->f_length); return true; }
static uint32_t xs_utf8_cp(const unsigned char *s, size_t n, size_t *len) { /* one code point (U+FFFD for a bad byte) */
    unsigned c = s[0];
    if (c < 0x80) { *len = 1; return c; }
    int k = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (k == 1 || (size_t)k > n) { *len = 1; return 0xFFFD; }
    uint32_t cp = c & (0xFFu >> (k + 1));
    for (int i = 1; i < k; i++) cp = (cp << 6) | (s[i] & 0x3Fu);
    *len = (size_t)k;
    return cp;
}
static void xs_u8_free(void *p) {
    xs_Uint8Array *b = p;
    if (b->owner) xs_release(b->owner); else free(b->data);
    xs_obj_free(p);
}
static void xs_u8_print(void *p) {
    xs_Uint8Array *b = p;
    xs_insp fr;
    if (!xs_insp_begin(&fr, p, "Uint8Array", b->f_length ? NULL : "Uint8Array(0) []")) return;
    int64_t n = b->f_length < 100 ? b->f_length : 100;
    for (int64_t i = 0; i < n; i++) { xs_insp_entry(&fr); fprintf(XS_OUT, "%d", b->data[i]); }
    if (b->f_length > 100) { xs_insp_entry(&fr); fprintf(XS_OUT, "... %lld more item%s", (long long)(b->f_length - 100), b->f_length - 100 == 1 ? "" : "s"); }
    char open[48];
    snprintf(open, sizeof open, "Uint8Array(%lld) [", (long long)b->f_length);
    xs_insp_end(&fr, open, "]", true, true, b->f_length > 100);
}
static void xs_buf_print(void *p) {
    xs_Uint8Array *b = p;
    fputs("<Buffer", XS_OUT);
    int64_t n = b->f_length < 50 ? b->f_length : 50;
    for (int64_t i = 0; i < n; i++) fprintf(XS_OUT, " %02x", b->data[i]);
    if (b->f_length > 50) fprintf(XS_OUT, " ... %lld more byte%s", (long long)(b->f_length - 50), b->f_length - 50 == 1 ? "" : "s");
    fputc('>', XS_OUT);
}
static inline bool xs_is_buffer(xs_Uint8Array *b) { return xs_type_of(b)->print == xs_buf_print; }
#if defined(__linux__)
#include <sys/mman.h>
#endif
XS_NOINLINE xs_Uint8Array *xs_u8_raw(int64_t n, bool buffer, bool zero) {
    if (n < 0 || n > ((int64_t)1 << 40)) xs_panic("RangeError: Invalid typed array length");
    xs_Uint8Array *b = buffer ? xs_obj_alloc(sizeof *b, xs_u8_free, xs_buf_print) : xs_obj_alloc(sizeof *b, xs_u8_free, xs_u8_print);
    b->f_length = n;
    b->data = zero ? calloc((size_t)(n ? n : 1), 1) : malloc((size_t)(n ? n : 1));
    if (!b->data) xs_panic("out of memory");
    xs_u8_huge_hint(b->data, n);
    return b;
}
XS_NOINLINE xs_Uint8Array *xs_u8_new(int64_t n) { return xs_u8_raw(n, false, true); }
XS_NOINLINE xs_Uint8Array *xs_u8_from_ints(xs_arr_int *a) {
    xs_Uint8Array *b = xs_u8_raw((int64_t)a->len, false, false);
    for (size_t i = 0; i < a->len; i++) b->data[i] = (uint8_t)a->data[i];
    return b;
}
XS_NOINLINE xs_Uint8Array *xs_u8_from_nums(xs_arr_num *a) {
    xs_Uint8Array *b = xs_u8_raw((int64_t)a->len, false, false);
    for (size_t i = 0; i < a->len; i++) { double v = a->data[i]; b->data[i] = isfinite(v) ? (uint8_t)(int64_t)fmod(trunc(v), 256) : 0; }
    return b;
}
XS_NOINLINE xs_Uint8Array *xs_u8_copy_kind(xs_Uint8Array *s, bool buffer) {
    xs_Uint8Array *b = xs_u8_raw(s->f_length, buffer, false);
    memcpy(b->data, s->data, (size_t)s->f_length);
    return b;
}
XS_NOINLINE xs_Uint8Array *xs_u8_copy(xs_Uint8Array *s) { return xs_u8_copy_kind(s, false); }
/* b[i] is read and written as a byte type that is not a character type: a write through uint8_t may alias
   anything (C's char rule), so the compiler reloaded b->length and b->data after every byte stored; through
   _BitInt(8) it keeps them in registers (a fill loop went from 54 to 39 ms per 64 MB) */
#if (defined(__clang__) && __clang_major__ >= 14) || (!defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 14)
__extension__ typedef unsigned _BitInt(8) xs_byte;
#else
typedef uint8_t xs_byte;
#endif
/* the length, with the bound every Uint8Array has (xs_u8_raw): an index below it times a small constant
   cannot overflow, so the compiler drops those checks in `for (i = 0; i < b.length; i++) b[i] = i * 31` */
static inline int64_t xs_u8_len(xs_Uint8Array *b) {
    int64_t n = b->f_length;
#if defined(__clang__)
    __builtin_assume(n >= 0 && n <= ((int64_t)1 << 40));
#else
    if (n < 0 || n > ((int64_t)1 << 40)) __builtin_unreachable();
#endif
    return n;
}
static inline xs_byte *xs_u8_ptr(xs_Uint8Array *b, int64_t i, const xs_site *s) {
    if ((uint64_t)i >= (uint64_t)b->f_length) XS_FAIL("index out of bounds", s);
    return (xs_byte *)&b->data[i];
}
static inline int64_t xs_u8_get(xs_Uint8Array *b, int64_t i, const xs_site *s) { return (int64_t)*xs_u8_ptr(b, i, s); }
/* The swap loop `for (i = lo; i < (hi+lo+1) >> 1; i++) { t = a[i]; a[i] = a[hi+lo-i]; a[hi+lo-i] = t; }` (emit.c
   emit_u8_idiom): a[lo..hi] reversed in place. The indices are checked once; a range inside the first 16 bytes of
   a buffer of 16 or more is one 16-byte shuffle (SSSE3 pshufb / NEON tbl), the way the Rust fannkuch-redux keeps
   its permutation in one register. fannkuch-redux on the Xeon: see historia.md. */
/* The two-cursor table trade of reverse-complement (emit.c emit_u8_idiom):
     for (t = 0; t < run; t++) { x = buf[a+t]; buf[a+t] = T[buf[b-t]]; buf[b-t] = T[x]; }
   T a Uint8Array(256) the loop does not write. The setup, once per function (T is invariant there), decides
   whether T is "letters by 5 bits" (T[c] == L[c & 0x1f] for A..Z and a..z, T[c] == c for every other byte) and
   keeps L in two vectors; then a step takes 16 bytes of each side, reverses them with a shuffle, translates
   them (L by a 32-entry lookup; a non-letter stays itself, by a blend on an unsigned compare, so the result is
   T[c] bit for bit for any byte) and stores them on the other side, both loads before both stores (blocks that
   overlap in the middle of the run are exact; a block past the run is stored under a lane < rem mask). Any
   other table, a buffer too close to its ends, or XISTO_NO_U8_TRADE=1 at build time: the scalar loop. */
typedef struct { const uint8_t *T; bool simd;
#if defined(__SSSE3__) || defined(__ARM_NEON)
    xs_u8x16 L0, L1, M0, M1; /* L: the letters' values by their 5 low bits; M: 0xff where a letter is mapped */
#endif
} xs_u8_tr_ctx;
/* "letters by 5 bits": a letter either keeps itself in both cases, or both cases go to one value L[c & 0x1f]
   (M marks those); every other byte keeps itself. The complement table is one (16 letter pairs mapped, the 10
   others identity); a table that moves a digit, or maps 'e' and 'E' apart, is not. */
static inline bool xs_u8_tr_is5(const uint8_t *T, uint8_t *L, uint8_t *M) {
    for (int i = 0; i < 32; i++) { L[i] = 0; M[i] = 0; }
    for (int i = 0; i < 26; i++) {
        int up = 'A' + i, lo = 'a' + i, k = up & 0x1f;
        if (T[up] == up && T[lo] == lo) continue;
        if (T[lo] != T[up]) return false;
        L[k] = T[up];
        M[k] = 0xff;
    }
    for (int c = 0; c < 256; c++) {
        bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (!letter && T[c] != c) return false;
    }
    return true;
}
static inline xs_u8_tr_ctx xs_u8_tr_setup(xs_Uint8Array *T) {
    xs_u8_tr_ctx c;
    c.T = T->data;
    c.simd = false;
#if defined(__SSSE3__) || defined(__ARM_NEON)
    uint8_t L[32], M[32];
    if (T->f_length >= 256 && !getenv("XISTO_NO_U8_TRADE") && xs_u8_tr_is5(T->data, L, M)) {
        c.simd = true;
#if defined(__SSSE3__)
        /* x86: pshufb runs on one port only (p5 on Haswell/Broadwell), so M is folded into L: an unmapped letter's
           entry carries bit 7 (0x80 | idx) and the lookup's sign says "not mapped"; the mapped values must then be
           below 0x80 (they are letters in the tables this is for; any other table takes the scalar loop) */
        for (int k = 0; k < 32; k++) {
            if (!M[k]) L[k] = (uint8_t)(0x80 | k);
            else if (L[k] & 0x80) c.simd = false;
        }
#endif
        c.L0 = xs_u8x16_load(L);
        c.L1 = xs_u8x16_load(L + 16);
        c.M0 = xs_u8x16_load(M);
        c.M1 = xs_u8x16_load(M + 16);
    }
#endif
    return c;
}
#if defined(__SSSE3__) || defined(__ARM_NEON)
/* 16 bytes through the table: letter ? L[c & 0x1f] : c */
/* 32 entries by a 5-bit index: one tbl on NEON; two pshufb on x86 (idx + 0x70: bit 7 set for idx >= 16, so
   the first gives 0 there; idx - 0x10 wraps negative for idx < 16, so the second gives 0 there) */
static inline __attribute__((always_inline)) xs_u8x16 xs_u8_tr_lut32(xs_u8x16 t0, xs_u8x16 t1, xs_u8x16 idx) {
#if defined(__SSSE3__)
    return xs_u8x16_shuffle(t0, idx + xs_u8x16_splat(0x70)) | xs_u8x16_shuffle(t1, idx - xs_u8x16_splat(0x10));
#else
    uint8x16x2_t tab = {{(uint8x16_t)t0, (uint8x16_t)t1}};
    return (xs_u8x16)vqtbl2q_u8(tab, (uint8x16_t)idx);
#endif
}
typedef int8_t xs_i8x16 __attribute__((vector_size(16)));
static inline __attribute__((always_inline)) xs_u8x16 xs_u8_tr_vec(const xs_u8_tr_ctx *c, xs_u8x16 v) {
    xs_u8x16 idx = v & xs_u8x16_splat(0x1f);
    xs_u8x16 t = xs_u8_tr_lut32(c->L0, c->L1, idx);
    xs_u8x16 m = (v | xs_u8x16_splat(0x20)) - xs_u8x16_splat('a');
    xs_u8x16 letter = (xs_u8x16)(m <= xs_u8x16_splat(25)); /* (unsigned compare) */
#if defined(__SSSE3__)
    xs_u8x16 unmapped = (xs_u8x16)((xs_i8x16)t < (xs_i8x16)xs_u8x16_splat(0)); /* bit 7 of the folded entry: one pcmpgtb */
    xs_u8x16 take = letter & ~unmapped;
    return (take & t) | (~take & v);
#else
    letter &= xs_u8_tr_lut32(c->M0, c->M1, idx); /* and mapped */
    return (letter & t) | (~letter & v);
#endif
}
#endif
static inline void xs_u8_trade(const xs_u8_tr_ctx *c, xs_Uint8Array *buf, int64_t a, int64_t b, int64_t run, const xs_site *s) {
    if (run <= 0) return;
    int64_t len = buf->f_length;
    if (a < 0 || b >= len || a + run - 1 >= len || b - run + 1 < 0) {
        xs_u8_get(buf, a < 0 ? a : b >= len ? b : a + run - 1 >= len ? a + run - 1 : b - run + 1, s); /* (the panic, with the index) */
        return;
    }
    uint8_t *d = buf->data;
    const uint8_t *T = c->T;
#if defined(__SSSE3__) || defined(__ARM_NEON)
    if (c->simd) {
        const xs_u8x16 rev = xs_u8x16_splat(15) - xs_u8x16_iota();
        if (run >= 16) {
            /* The run's two regions, [a, a+run) and [b-run+1, b], are walked in blocks of 16 from their outer ends;
               the last block of each is aligned to the region's inner end, so it overlaps the one before it when
               run % 16 != 0. Those two final blocks are loaded first, before any store (every block is then
               computed from the original bytes; the stores write the same final value where they overlap), and
               stored last. No mask, no block outside its region: nothing to keep, nothing past the buffer. */
            const int64_t fa = a + run - 16, fb = b - run + 1;
            xs_u8x16 Af = xs_u8x16_load(d + fa), Rf = xs_u8x16_load(d + fb);
            xs_u8x16 nAf = xs_u8_tr_vec(c, xs_u8x16_shuffle(Rf, rev)), nRf = xs_u8_tr_vec(c, xs_u8x16_shuffle(Af, rev));
            int64_t rem = run, aa = a, bb = b;
            while (rem >= 16) {
                xs_u8x16 A = xs_u8x16_load(d + aa), R = xs_u8x16_load(d + bb - 15);
                xs_u8x16_store(d + aa, xs_u8_tr_vec(c, xs_u8x16_shuffle(R, rev)));
                xs_u8x16_store(d + bb - 15, xs_u8_tr_vec(c, xs_u8x16_shuffle(A, rev)));
                aa += 16; bb -= 16; rem -= 16;
            }
            xs_u8x16_store(d + fa, nAf);
            xs_u8x16_store(d + fb, nRf);
            return;
        }
        if (a + 16 <= len && b - 15 >= 0) { /* a short run: one masked pair; blocks past the run keep their bytes */
            xs_u8x16 A = xs_u8x16_load(d + a), R = xs_u8x16_load(d + b - 15);
            xs_u8x16 lane = xs_u8x16_iota();
            xs_u8x16 inA = (xs_u8x16)(lane < xs_u8x16_splat((uint8_t)run));          /* lanes 0..run-1 of the a block */
            xs_u8x16 inR = (xs_u8x16)(lane >= xs_u8x16_splat((uint8_t)(16 - run))); /* lanes 16-run..15 of the b block */
            xs_u8x16 nA = xs_u8_tr_vec(c, xs_u8x16_shuffle(R, rev)), nR = xs_u8_tr_vec(c, xs_u8x16_shuffle(A, rev));
            xs_u8x16_store(d + a, (inA & nA) | (~inA & A));
            /* the b block's bytes outside the run may have just been written by the a block (the two meet in the
               middle of a sequence): kept as they are now, not as they were loaded */
            xs_u8x16_store(d + b - 15, (inR & nR) | (~inR & xs_u8x16_load(d + b - 15)));
            return;
        }
    }
#endif
    for (int64_t t = 0; t < run; t++) {
        uint8_t x = d[a + t];
        d[a + t] = T[d[b - t]];
        d[b - t] = T[x];
    }
}
/* ---- reverse of xisto:bytes on several threads (XB-092): reverseAround / reverseMapped of a big range. Both trade
   the k-th byte that is not `keep` from the front with the k-th from the back, mapped through the table (keep < 0:
   every byte). Those pairs are independent, so the pairs are cut into tasks by rank: a first pass counts the
   bytes that are not `keep` in blocks of the range (each thread its blocks), so each task finds the positions of
   its first front and back rank and trades its pairs in runs between two `keep` bytes, as the one-thread loop
   does (xs_u8_trade's 16-byte blocks; a run below 16 bytes in plain bytes: the masked short path would rewrite
   bytes of the neighbouring task). Exact: the same pairs, the same table. Left to the one-thread loop: a range
   below 1 MB, fewer than 4 cores, a table that maps another byte to `keep` (the loop would then see the new `keep` bytes),
   and XISTO_NO_REV_PAR=1. Its own threads (no runtime state is touched: raw bytes only), made at the first use. */
/* memchr, 64 bytes per step on NEON for Linux (glibc's arm64 memchr is the generic one on the Graviton2, Amazon
   Linux: __memchr_generic; readStdinParts scans 254 MB for '>' with it); libc's elsewhere (vectorized there) */
static inline const uint8_t *xs_memchr(const uint8_t *p, int c, size_t n) {
#if defined(__ARM_NEON) && defined(__linux__)
    const uint8_t *e = p + n;
    const uint8x16_t cv = vdupq_n_u8((uint8_t)c);
    while (p + 64 <= e) {
        uint8x16_t a = vceqq_u8(vld1q_u8(p), cv), b = vceqq_u8(vld1q_u8(p + 16), cv);
        uint8x16_t x = vceqq_u8(vld1q_u8(p + 32), cv), y = vceqq_u8(vld1q_u8(p + 48), cv);
        if (vmaxvq_u8(vorrq_u8(vorrq_u8(a, b), vorrq_u8(x, y)))) {
            uint8x16_t q[4] = {a, b, x, y};
            for (int k = 0; k < 4; k++) {
                uint64_t m = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(q[k]), 4)), 0);
                if (m) return p + 16 * k + (__builtin_ctzll(m) >> 2);
            }
        }
        p += 64;
    }
    return memchr(p, c, (size_t)(e - p));
#else
    return memchr(p, c, n);
#endif
}
#if !defined(_WIN32)
#include <pthread.h>
#include <stdatomic.h>
typedef struct {
    uint8_t *d; xs_u8_tr_ctx c; int keep;
    const uint8_t *src, *map; /* phase 2: d[i] = map[src[i]] over [start, end) in ntask pieces */
    int64_t start, end, m, half, blk, nblk, ntask;
    int64_t *pre; /* bytes that are not keep before block b (nblk + 1 entries) */
    int64_t fw; /* > 0: every line has fw bytes, then keep (the last one fw or fewer), proved by the count: positions by arithmetic */
    atomic_int fw_bad; /* phase 0: a block whose keeps are not where fw puts them */
    const unsigned char *csrc; unsigned char *cout; int cmode, cch; bool ccase; int64_t *ckept; /* phases 3, 4: the fused chain in ntask pieces */
    unsigned char *gdst; int64_t *gcount; /* phase 5: code points of csrc in ntask pieces into gcount; 6: csrc copied to gdst */
    const char *gneedle; size_t gnn; atomic_int_fast64_t gfirst; /* phase 7: the first piece with a hit of gneedle in csrc (gcount: its offset or -1) */
    int phase;
    atomic_int_fast64_t next;
    bool publish; atomic_uchar *tdone; atomic_int_fast64_t cont; /* phase 1 in a begun part: the tasks done in order, so the front up to there is final */
} xs_rap_job;
/* the shared threads run one job at a time: a caller that finds them taken does its work on its own thread */
static atomic_int xs_rap_owner;
static bool xs_rap_acquire(void) { int expect = 0; return atomic_compare_exchange_strong(&xs_rap_owner, &expect, 1); }
static void xs_rap_release(void) { atomic_store(&xs_rap_owner, 0); }
static pthread_mutex_t xs_rap_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t xs_rap_wake = PTHREAD_COND_INITIALIZER, xs_rap_done = PTHREAD_COND_INITIALIZER;
static xs_rap_job *xs_rap_cur;
static uint64_t xs_rap_gen;
static int xs_rap_nthreads = -1, xs_rap_busy;
/* 16 bytes compared at once: a bit mask of the equal lanes, XS_EQ_SHIFT bits per lane (NEON: 4, through a narrowing
   shift; x86: 1, pmovmskb), so the first is ctz >> shift and the last (63 - clz) >> shift */
#if defined(__ARM_NEON)
#define XS_EQ_SHIFT 2
static inline uint64_t xs_u8x16_eqmask(xs_u8x16 v, xs_u8x16 c) {
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(vceqq_u8((uint8x16_t)v, (uint8x16_t)c)), 4)), 0);
}
#elif defined(__SSSE3__)
#define XS_EQ_SHIFT 0
static inline uint64_t xs_u8x16_eqmask(xs_u8x16 v, xs_u8x16 c) { return (uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8((__m128i)v, (__m128i)c)); }
#endif
static int64_t xs_rap_count(const uint8_t *p, int64_t n, int keep) {
    int64_t k = 0, i = 0;
    const uint8_t c = (uint8_t)keep;
#if defined(__SSSE3__) || defined(__ARM_NEON)
    const xs_u8x16 cv = xs_u8x16_splat(c);
    while (i + 16 <= n) { /* 16 at a time into byte counters, 255 blocks at most before they are summed */
        xs_u8x16 acc = xs_u8x16_splat(0);
        int64_t stop = i + (int64_t)16 * 255 < n ? i + (int64_t)16 * 255 : n - 15;
        for (; i < stop; i += 16) acc -= (xs_u8x16)(xs_u8x16_load(p + i) == cv); /* (an equal lane is -1) */
        for (int l = 0; l < 16; l++) k += acc[l];
    }
#endif
    for (; i < n; i++) k += p[i] == c; /* (a memchr per kept byte cost more than the whole block) */
    return n - k;
}
/* the position of the byte of rank r (0-based, among those that are not keep) */
static int64_t xs_rap_pos(const xs_rap_job *j, int64_t r) {
    if (j->keep < 0) return j->start + r;
    if (j->fw > 0) return j->start + r + r / j->fw; /* (fw bytes, then a keep) */
    int64_t lo = 0, hi = j->nblk - 1; /* the last block b with pre[b] <= r */
    while (lo < hi) { int64_t mid = (lo + hi + 1) >> 1; if (j->pre[mid] <= r) lo = mid; else hi = mid - 1; }
    int64_t need = r - j->pre[lo];
    const uint8_t *p = j->d + j->start + lo * j->blk, *e = j->d + (j->start + (lo + 1) * j->blk < j->end ? j->start + (lo + 1) * j->blk : j->end);
    for (;;) {
        const uint8_t *q = xs_memchr(p, j->keep, (size_t)(e - p));
        int64_t seg = (q ? q : e) - p;
        if (need < seg) return (p - j->d) + need;
        need -= seg;
        p = q + 1;
    }
}
/* trades run pairs from a up and b down; masked (the 16-byte short path, which rewrites bytes next to the run)
   only when those bytes are the task's own: inside [a, fend] and [bstart, b] */
static void xs_rap_trade(const xs_rap_job *j, int64_t a, int64_t b, int64_t run, int64_t fend, int64_t bstart) {
    uint8_t *d = j->d;
#if defined(__SSSE3__) || defined(__ARM_NEON)
    if (j->c.simd && (run >= 16 || (a + 15 <= fend && b - 15 >= bstart))) {
        xs_Uint8Array fake;
        memset(&fake, 0, sizeof fake);
        fake.data = d; fake.f_length = j->end;
        xs_u8_trade(&j->c, &fake, a, b, run, NULL);
        return;
    }
#endif
    const uint8_t *T = j->c.T;
    for (int64_t t = 0; t < run; t++) { uint8_t x = d[a + t]; d[a + t] = T[d[b - t]]; d[b - t] = T[x]; }
}
static int64_t xs_rap_next_keep(const xs_rap_job *j, int64_t i) {
    if (j->keep < 0) return j->end;
    if (j->fw > 0) { int64_t q = j->start + (i - j->start) / (j->fw + 1) * (j->fw + 1) + j->fw; return q < j->end ? q : j->end; }
    const uint8_t *d = j->d;
    int64_t x = i;
#if defined(__SSSE3__) || defined(__ARM_NEON)
    const xs_u8x16 cv = xs_u8x16_splat((uint8_t)j->keep);
    for (; x + 16 <= j->end; x += 16) { /* 16 at a time: stretches are short (lines), a memchr call per line costs more than the scan */
        uint64_t m = xs_u8x16_eqmask(xs_u8x16_load(d + x), cv);
        if (m) return x + (__builtin_ctzll(m) >> XS_EQ_SHIFT);
    }
#endif
    for (int k = 0; k < 16 && x + 8 <= j->end; k++) { /* 8 at a time first: stretches are short (lines), a memchr call per line costs more than the scan */
        uint64_t w;
        memcpy(&w, d + x, 8);
        uint64_t v = w ^ (0x0101010101010101ull * (uint8_t)j->keep);
        uint64_t z = (v - 0x0101010101010101ull) & ~v & 0x8080808080808080ull;
        if (z) { /* the first keep byte among them (the lowest flagged byte is exact) */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            return x + (__builtin_ctzll(z) >> 3);
#else
            break;
#endif
        }
        x += 8;
    }
    const uint8_t *q = xs_memchr(d + x, j->keep, (size_t)(j->end - x));
    return q ? q - d : j->end;
}
static int64_t xs_rap_prev_keep(const xs_rap_job *j, int64_t k) { /* the position after the keep before k (or start) */
    if (j->keep < 0) return j->start;
    if (j->fw > 0) {
        int64_t o = (k - j->start) % (j->fw + 1);
        return o == j->fw ? k + 1 : k - o; /* (k a keep itself: the position after it, as the scan) */
    }
    const uint8_t *d = j->d;
    int64_t x = k;
#if defined(__SSSE3__) || defined(__ARM_NEON)
    const xs_u8x16 cv = xs_u8x16_splat((uint8_t)j->keep);
    while (x - 15 >= j->start) { /* 16 at a time, d[x - 15..x]: the last keep among them */
        uint64_t m = xs_u8x16_eqmask(xs_u8x16_load(d + x - 15), cv);
        if (m) return x - 15 + ((63 - __builtin_clzll(m)) >> XS_EQ_SHIFT) + 1;
        x -= 16;
    }
#endif
    while (x - 8 >= j->start) { /* 8 at a time: a keep byte among them? */
        uint64_t w;
        memcpy(&w, d + x - 7, 8);
        uint64_t v = w ^ (0x0101010101010101ull * (uint8_t)j->keep);
        if ((v - 0x0101010101010101ull) & ~v & 0x8080808080808080ull) break;
        x -= 8;
    }
    while (x >= j->start && d[x] != j->keep) x--;
    return x + 1;
}
static void xs_rap_trade_task(xs_rap_job *j, int64_t t);
static void xs_rap_task(xs_rap_job *j, int64_t t) {
    if (j->phase == 0) { /* count block t */
        int64_t s = j->start + t * j->blk, e = s + j->blk < j->end ? s + j->blk : j->end;
        int64_t c = xs_rap_count(j->d + s, e - s, j->keep);
        j->pre[t + 1] = c;
        if (j->fw > 0 && !atomic_load(&j->fw_bad)) { /* the keeps the fixed width puts in [s, e): as many as counted, each where it says */
            int64_t w1 = j->fw + 1, a = s - j->start - j->fw, b = e - j->start - j->fw; /* keep k at start + fw + k * w1 */
            int64_t k0 = a <= 0 ? 0 : (a + w1 - 1) / w1, k1 = b <= 0 ? 0 : (b + w1 - 1) / w1; /* the k with s <= p_k < e */
            bool ok = (e - s) - c == k1 - k0;
            for (int64_t k = k0; ok && k < k1; k++) ok = j->d[j->start + j->fw + k * w1] == (uint8_t)j->keep;
            if (!ok) atomic_store(&j->fw_bad, 1);
        }
        return;
    }
    xs_rap_trade_task(j, t);
    if (j->publish) { /* the tasks done in order from the outside: the front before the next one's first pair is final */
        atomic_store(&j->tdone[t], 1);
        int_fast64_t c = atomic_load(&j->cont);
        while (c < j->ntask && atomic_load(&j->tdone[c])) {
            if (atomic_compare_exchange_weak(&j->cont, &c, c + 1)) {
                c++;
                xs_lent_publish(j->d + xs_rap_pos(j, j->half * c / j->ntask));
            }
        }
    }
}
static void xs_rap_trade_task(xs_rap_job *j, int64_t t) {
    int64_t r0 = j->half * t / j->ntask, r1 = j->half * (t + 1) / j->ntask, left = r1 - r0;
    if (left <= 0) return;
    int64_t i = xs_rap_pos(j, r0), k = xs_rap_pos(j, j->m - 1 - r0);
    int64_t fend = xs_rap_pos(j, r1 - 1), bstart = xs_rap_pos(j, j->m - r1); /* this task's own bytes */
    const uint8_t *d = j->d;
    int64_t iend = xs_rap_next_keep(j, i), kstart = xs_rap_prev_keep(j, k);
    while (left > 0) {
        int64_t run = iend - i;
        if (k - kstart + 1 < run) run = k - kstart + 1;
        if (left < run) run = left;
        xs_rap_trade(j, i, k, run, fend, bstart);
        i += run; k -= run; left -= run;
        if (left > 0) {
            if (i == iend) { while (d[i] == j->keep) i++; iend = xs_rap_next_keep(j, i); }
            if (k < kstart) { while (d[k] == j->keep) k--; kstart = xs_rap_prev_keep(j, k); }
        }
    }
}
/* one thread: the two cursors of reverseAround (xisto:bytes), each inside its stretch between two keep bytes; a run
   in which neither leaves its stretch is one trade (the same pairs as the TS loop: the k-th byte that is not keep
   from the front with the k-th from the back) */
static void xs_rap_seq(xs_rap_job *j) {
    const uint8_t *d = j->d;
    int64_t i = j->start, k = j->end - 1;
    while (k >= j->start && d[k] == j->keep) k--;
    int64_t iend = xs_rap_next_keep(j, i), kstart = xs_rap_prev_keep(j, k);
    while (i < k) {
        if (i == iend) { i++; iend = xs_rap_next_keep(j, i); continue; }
        if (k < kstart) { k--; kstart = xs_rap_prev_keep(j, k); continue; }
        int64_t run = iend - i;
        if (k - kstart + 1 < run) run = k - kstart + 1;
        int64_t meet = (k - i + 1) >> 1;
        if (meet < run) run = meet;
        if (run == 0) break;
        xs_rap_trade(j, i, k, run, k - 16, i + 16); /* (masked only when the two 16-byte blocks are apart) */
        i += run; k -= run;
    }
    if (i == k && d[i] != j->keep) j->d[i] = j->c.T[d[i]]; /* the middle byte */
}
static void xs_map_task(xs_rap_job *j, int64_t t) {
    int64_t n = j->end - j->start, a = j->start + n * t / j->ntask, b = j->start + n * (t + 1) / j->ntask;
    uint8_t *restrict d = j->d;
    const uint8_t *restrict src = j->src, *restrict T = j->map;
    if (d == src) { uint8_t *p = d; for (int64_t i = a; i < b; i++) p[i] = T[p[i]]; }
    else for (int64_t i = a; i < b; i++) d[i] = T[src[i]];
}
static void xs_gen_task(xs_rap_job *j, int64_t t) {
    int64_t n = j->end - j->start, a = j->start + n * t / j->ntask, b = j->start + n * (t + 1) / j->ntask;
    const unsigned char *d = j->csrc;
    if (j->phase == 6) { memcpy(j->gdst + a, d + a, (size_t)(b - a)); return; }
    if (j->phase == 7) { /* the hits that start in [a, b); a piece after one that has a hit is not searched */
        j->gcount[t] = -1;
        if (t > atomic_load(&j->gfirst)) return;
        int64_t e = b + (int64_t)j->gnn - 1 < j->end ? b + (int64_t)j->gnn - 1 : j->end;
        const char *h = e - a >= (int64_t)j->gnn ? xs_find((const char *)d + a, (size_t)(e - a), j->gneedle, j->gnn) : NULL;
        if (!h) return;
        j->gcount[t] = (int64_t)(h - (const char *)d);
        int_fast64_t cur = atomic_load(&j->gfirst);
        while (t < cur && !atomic_compare_exchange_weak(&j->gfirst, &cur, t)) {}
        return;
    }
    int64_t c = 0;
    for (int64_t i = a; i < b; i++) c += (d[i] & 0xC0) != 0x80; /* (vectorized: no branch) */
    j->gcount[t] = c;
}
static void xs_chain_task(xs_rap_job *j, int64_t t) {
    int64_t n = j->end - j->start, a = j->start + n * t / j->ntask, b = j->start + n * (t + 1) / j->ntask;
    if (j->phase == 3) j->ckept[t + 1] = xs_chain_pass(j->csrc + a, j->csrc + b, NULL, j->cmode, j->cch, j->ccase);
    else xs_chain_pass(j->csrc + a, j->csrc + b, j->cout + j->ckept[t], j->cmode, j->cch, j->ccase);
}
static void xs_rap_work(xs_rap_job *j) {
    int64_t n = j->phase == 0 ? j->nblk : j->ntask;
    for (;;) {
        int64_t t = atomic_fetch_add(&j->next, 1);
        if (t >= n) break;
        if (j->phase == 2) xs_map_task(j, t);
        else if (j->phase == 3 || j->phase == 4) xs_chain_task(j, t);
        else if (j->phase >= 5 && j->phase <= 7) xs_gen_task(j, t);
        else xs_rap_task(j, t);
    }
}
static void *xs_rap_worker(void *arg) {
    (void)arg;
    uint64_t seen = 0;
    for (;;) {
        pthread_mutex_lock(&xs_rap_mu);
        while (xs_rap_gen == seen) pthread_cond_wait(&xs_rap_wake, &xs_rap_mu);
        seen = xs_rap_gen;
        xs_rap_job *j = xs_rap_cur;
        pthread_mutex_unlock(&xs_rap_mu);
        xs_rap_work(j);
        pthread_mutex_lock(&xs_rap_mu);
        if (--xs_rap_busy == 0) pthread_cond_signal(&xs_rap_done);
        pthread_mutex_unlock(&xs_rap_mu);
    }
    return NULL;
}
static void xs_rap_run(xs_rap_job *j, int phase) {
    j->phase = phase;
    atomic_store(&j->next, 0);
    pthread_mutex_lock(&xs_rap_mu);
    xs_rap_cur = j;
    xs_rap_busy = xs_rap_nthreads;
    xs_rap_gen++;
    pthread_cond_broadcast(&xs_rap_wake);
    pthread_mutex_unlock(&xs_rap_mu);
    xs_rap_work(j);
    pthread_mutex_lock(&xs_rap_mu);
    while (xs_rap_busy > 0) pthread_cond_wait(&xs_rap_done, &xs_rap_mu);
    pthread_mutex_unlock(&xs_rap_mu);
}
static void xs_rap_start_threads(void) {
    if (xs_rap_nthreads >= 0) return;
    const char *s = getenv("XISTO_THREADS");
    long t = s && *s ? strtol(s, NULL, 10) : sysconf(_SC_NPROCESSORS_ONLN);
    if (t > 64) t = 64;
    xs_rap_nthreads = 0;
    for (long k = 1; k < t; k++) {
        pthread_t th;
        if (pthread_create(&th, NULL, xs_rap_worker, NULL) == 0) { pthread_detach(th); xs_rap_nthreads++; }
    }
}
/* `for (i = lo; i < hi; i++) D[i] = T[S[i]]` on byte arrays, split over threads: every element on its own, so the
   pieces in any order give the loop's bytes, when the ranges are in bounds (no error to raise), T has 256 entries
   (every byte indexes it) and nothing overlaps but D and S being the same bytes (in place). true: done here;
   false: the caller runs its loop (a small range, one core, anything else) */
XS_NOINLINE bool xs_u8_map_par(xs_Uint8Array *D, xs_Uint8Array *S, xs_Uint8Array *T, int64_t lo, int64_t hi) {
    static int off = -1;
    if (off < 0) off = getenv("XISTO_NO_U8_MAP_PAR") != NULL;
    if (off || hi - lo < ((int64_t)1 << 21) || lo < 0 || hi > D->f_length || hi > S->f_length || T->f_length < 256) return false;
    uintptr_t d0 = (uintptr_t)(D->data + lo), d1 = (uintptr_t)(D->data + hi), s0 = (uintptr_t)(S->data + lo), s1 = (uintptr_t)(S->data + hi);
    uintptr_t t0 = (uintptr_t)T->data, t1 = t0 + 256;
    if (d0 != s0 && d0 < s1 && s0 < d1) return false; /* D and S overlapping at an offset: the loop's order matters */
    if (t0 < d1 && d0 < t1) return false;           /* the table among the bytes written */
    if (!xs_rap_acquire()) return false; /* (the threads busy with another caller's job: this one loops) */
    xs_rap_start_threads();
    if (xs_rap_nthreads < 1) { xs_rap_release(); return false; }
    xs_rap_job j;
    memset(&j, 0, sizeof j);
    j.d = D->data; j.src = S->data; j.map = T->data; j.start = lo; j.end = hi;
    j.ntask = (int64_t)(xs_rap_nthreads + 1) * 4;
    xs_rap_run(&j, 2);
    xs_rap_release();
    return true;
}
/* true: done here; false: the caller runs its own loop */
XS_NOINLINE bool xs_u8_rev_par(xs_Uint8Array *buf, xs_Uint8Array *table, int64_t keep, int64_t start, int64_t end) {
    int64_t n = end - start;
    if (n < 4096 || start < 0 || end > buf->f_length || table->f_length != 256 || keep > 255) return false;
    static int off = -1;
    if (off < 0) off = getenv("XISTO_NO_REV_PAR") != NULL;
    if (off) return false;
    if (keep >= 0) for (int b = 0; b < 256; b++) if (b != keep && table->data[b] == keep) return false;
    static int seq_off = -1;
    if (seq_off < 0) seq_off = getenv("XISTO_NO_REV_SEQ") != NULL;
    if (n < ((int64_t)1 << 20)) { /* small: one thread, here when there are stretches (no threads started) */
        if (keep < 0 || seq_off) return false;
        xs_rap_job j;
        memset(&j, 0, sizeof j);
        j.d = buf->data; j.c = xs_u8_tr_setup(table); j.keep = (int)keep; j.start = start; j.end = end;
        xs_rap_seq(&j);
        return true;
    }
    if (xs_rap_nthreads < 0) {
        const char *s = getenv("XISTO_THREADS");
        long t = s && *s ? strtol(s, NULL, 10) : sysconf(_SC_NPROCESSORS_ONLN);
        if (t > 64) t = 64;
        xs_rap_nthreads = 0;
        for (long k = 1; k < t; k++) {
            pthread_t th;
            if (pthread_create(&th, NULL, xs_rap_worker, NULL) == 0) { pthread_detach(th); xs_rap_nthreads++; }
        }
    }
    static int min_threads = -1; /* the fewest extra threads that split the pairs (XISTO_REV_PAR_MIN): one, since the count
                                    and the stretch scans go 16 bytes at a time (Graviton2, 2 cores, 254 MB: 114 -> 73 ms;
                                    with a memchr per kept byte the count ate the gain: 31 ms either way on the M4) */
    if (min_threads < 0) { const char *m = getenv("XISTO_REV_PAR_MIN"); min_threads = m && *m ? (int)strtol(m, NULL, 10) : 1; }
    if (xs_rap_nthreads < min_threads) { /* one thread, here */
        if (keep < 0 || seq_off) return false;
        xs_rap_job j;
        memset(&j, 0, sizeof j);
        j.d = buf->data; j.c = xs_u8_tr_setup(table); j.keep = (int)keep; j.start = start; j.end = end;
        xs_rap_seq(&j);
        return true;
    }
    if (!xs_rap_acquire()) { /* the threads busy with another caller's job (XB-096): one thread, here */
        if (keep < 0 || seq_off) return false;
        xs_rap_job j;
        memset(&j, 0, sizeof j);
        j.d = buf->data; j.c = xs_u8_tr_setup(table); j.keep = (int)keep; j.start = start; j.end = end;
        xs_rap_seq(&j);
        return true;
    }
    xs_rap_job j;
    memset(&j, 0, sizeof j);
    j.d = buf->data; j.c = xs_u8_tr_setup(table); j.keep = (int)keep; j.start = start; j.end = end;
    int threads = xs_rap_nthreads + 1;
    if (keep >= 0) {
        static int fixed_off = -1; /* lines of one width (the first line's), checked by the count: no scan for the keeps */
        if (fixed_off < 0) fixed_off = getenv("XISTO_NO_REV_FIXED") != NULL;
        if (!fixed_off) {
            const uint8_t *f = xs_memchr(j.d + start, (int)keep, (size_t)n);
            j.fw = f && f - (j.d + start) >= 1 ? f - (j.d + start) : 0;
        }
        j.blk = (int64_t)1 << 18;
        j.nblk = (n + j.blk - 1) / j.blk;
        j.pre = malloc(sizeof(int64_t) * (size_t)(j.nblk + 1));
        if (!j.pre) { xs_rap_release(); return false; }
        j.pre[0] = 0;
        xs_rap_run(&j, 0);
        for (int64_t b = 0; b < j.nblk; b++) j.pre[b + 1] += j.pre[b];
        j.m = j.pre[j.nblk];
        if (atomic_load(&j.fw_bad)) j.fw = 0;
    } else j.m = n;
    j.half = j.m >> 1;
    j.ntask = (int64_t)threads * 8;
    if (j.half > 0 && xs_lent_open(j.d + start, (size_t)n)) { /* a begun part (writeStdoutLentBegin): the front goes out as it is done */
        if (j.ntask < 64) j.ntask = 64; /* (smaller steps where there are few threads: the first bytes are final sooner) */
        j.tdone = calloc((size_t)j.ntask, sizeof *j.tdone);
        if (j.tdone) { j.publish = true; xs_lent_publish(j.d + start); }
    }
    if (j.half > 0) xs_rap_run(&j, 1);
    free(j.tdone);
    if (j.m & 1) { int64_t p = xs_rap_pos(&j, j.half); j.d[p] = j.c.T[j.d[p]]; }
    free(j.pre);
    xs_rap_release();
    return true;
}
/* the fused string chain (xs_str_chain_fused) over threads: each piece first counts the bytes it keeps, then writes
   them where the pieces before it end (the same bytes as one pass). From 4 MB; false: the caller makes one pass */
/* a long string's length in code points (xs_str_len) and a long copy (TextEncoder.encode) over the threads: pieces
   counted or copied on their own, the same count and bytes as one pass. From 16 MB; false: the caller does it */
static bool xs_ulen_par(const unsigned char *d, int64_t n, int64_t *count) {
    static int off = -1;
    if (off < 0) off = getenv("XISTO_NO_GEN_PAR") != NULL;
    if (off || n < ((int64_t)1 << 24)) return false;
    if (!xs_rap_acquire()) return false;
    xs_rap_start_threads();
    if (xs_rap_nthreads < 1) { xs_rap_release(); return false; }
    xs_rap_job j;
    memset(&j, 0, sizeof j);
    j.csrc = d; j.start = 0; j.end = n;
    j.ntask = (int64_t)(xs_rap_nthreads + 1) * 4;
    j.gcount = malloc(sizeof(int64_t) * (size_t)j.ntask);
    if (!j.gcount) { xs_rap_release(); return false; }
    xs_rap_run(&j, 5);
    int64_t c = 0;
    for (int64_t t = 0; t < j.ntask; t++) c += j.gcount[t];
    free(j.gcount);
    xs_rap_release();
    *count = c;
    return true;
}
/* the first occurrence of nd in h over the threads (indexOf of a long string, of a byte in a long Uint8Array): the
   pieces searched in order, each its own first hit, none after the first piece with one; the earliest hit is the
   one a single search finds. From 16 MB; false: the caller searches */
static bool xs_find_par(const char *h, size_t hn, const char *nd, size_t nn, const char **hit) {
    static int off = -1;
    if (off < 0) off = getenv("XISTO_NO_GEN_PAR") != NULL;
    if (off || hn < ((size_t)1 << 24) || nn == 0 || nn > hn) return false;
    /* the first megabyte on this thread first: a search that hits near its start (indexOf("\n", from) walking a long
       text, line by line) must not wake every thread for a hit 60 bytes away (knucleotide-2: 4 s -> 64 s, 25 s of
       system time, before this). Every match starting in the first megabyte is found here, so the threads search
       only what follows */
    const size_t pre = (size_t)1 << 20;
    const char *near = memmem(h, pre + nn - 1, nd, nn);
    if (near) { *hit = near; return true; }
    h += pre; hn -= pre;
    if (!xs_rap_acquire()) return false;
    xs_rap_start_threads();
    if (xs_rap_nthreads < 1) { xs_rap_release(); return false; }
    xs_rap_job j;
    memset(&j, 0, sizeof j);
    j.csrc = (const unsigned char *)h; j.start = 0; j.end = (int64_t)hn;
    j.gneedle = nd; j.gnn = nn;
    j.ntask = (int64_t)(xs_rap_nthreads + 1) * 4;
    atomic_store(&j.gfirst, j.ntask);
    j.gcount = malloc(sizeof(int64_t) * (size_t)j.ntask);
    if (!j.gcount) { xs_rap_release(); return false; }
    xs_rap_run(&j, 7);
    int64_t f = atomic_load(&j.gfirst);
    *hit = f < j.ntask ? h + j.gcount[f] : NULL;
    free(j.gcount);
    xs_rap_release();
    return true;
}
static bool xs_copy_par(unsigned char *dst, const unsigned char *src, int64_t n) {
    static int off = -1;
    if (off < 0) off = getenv("XISTO_NO_GEN_PAR") != NULL;
    if (off || n < ((int64_t)1 << 24)) return false;
    if (!xs_rap_acquire()) return false;
    xs_rap_start_threads();
    if (xs_rap_nthreads < 1) { xs_rap_release(); return false; }
    xs_rap_job j;
    memset(&j, 0, sizeof j);
    j.csrc = src; j.gdst = dst; j.start = 0; j.end = n;
    j.ntask = (int64_t)(xs_rap_nthreads + 1) * 4;
    xs_rap_run(&j, 6);
    xs_rap_release();
    return true;
}
static bool xs_str_chain_par(const unsigned char *p, int64_t len, unsigned char *o, int mode, int ch, bool case_first, int64_t *written) {
    static int off = -1;
    if (off < 0) off = getenv("XISTO_NO_CHAIN_PAR") != NULL;
    if (off || len < ((int64_t)1 << 22)) return false;
    if (!xs_rap_acquire()) return false;
    xs_rap_start_threads();
    if (xs_rap_nthreads < 1) { xs_rap_release(); return false; }
    xs_rap_job j;
    memset(&j, 0, sizeof j);
    j.csrc = p; j.cout = o; j.cmode = mode; j.cch = ch; j.ccase = case_first;
    j.start = 0; j.end = len;
    j.ntask = (int64_t)(xs_rap_nthreads + 1) * 4;
    j.ckept = malloc(sizeof(int64_t) * (size_t)(j.ntask + 1));
    if (!j.ckept) { xs_rap_release(); return false; }
    j.ckept[0] = 0;
    xs_rap_run(&j, 3);
    for (int64_t t = 0; t < j.ntask; t++) j.ckept[t + 1] += j.ckept[t];
    xs_rap_run(&j, 4);
    *written = j.ckept[j.ntask];
    free(j.ckept);
    xs_rap_release();
    return true;
}
#else
static bool xs_u8_rev_par(xs_Uint8Array *buf, xs_Uint8Array *table, int64_t keep, int64_t start, int64_t end) { (void)buf, (void)table, (void)keep, (void)start, (void)end; return false; }
static bool xs_u8_map_par(xs_Uint8Array *D, xs_Uint8Array *S, xs_Uint8Array *T, int64_t lo, int64_t hi) { (void)D, (void)S, (void)T, (void)lo, (void)hi; return false; }
static bool xs_str_chain_par(const unsigned char *p, int64_t len, unsigned char *o, int mode, int ch, bool case_first, int64_t *written) { (void)p, (void)len, (void)o, (void)mode, (void)ch, (void)case_first, (void)written; return false; }
static bool xs_ulen_par(const unsigned char *d, int64_t n, int64_t *count) { (void)d, (void)n, (void)count; return false; }
static bool xs_copy_par(unsigned char *dst, const unsigned char *src, int64_t n) { (void)dst, (void)src, (void)n; return false; }
static bool xs_find_par(const char *h, size_t hn, const char *nd, size_t nn, const char **hit) { (void)h, (void)hn, (void)nd, (void)nn, (void)hit; return false; }
#endif
static inline void xs_u8_rev(xs_Uint8Array *a, int64_t lo, int64_t hi, const xs_site *s) {
    if (lo >= hi) return; /* (the loop runs no iteration: nothing read, nothing checked) */
    if (lo < 0 || (uint64_t)hi >= (uint64_t)a->f_length) XS_FAIL("index out of bounds", s);
    uint8_t *d = a->data;
#if defined(__SSSE3__) || defined(__ARM_NEON)
    if (hi < 16 && a->f_length >= 16) {
        /* mask[i] = i outside [lo, hi], lo + hi - i inside */
        xs_u8x16 idx = xs_u8x16_iota(), rev = xs_u8x16_splat((uint8_t)(lo + hi)) - idx;
        xs_u8x16 in = (idx >= xs_u8x16_splat((uint8_t)lo)) & (idx <= xs_u8x16_splat((uint8_t)hi));
        xs_u8x16 mask = (in & rev) | (~in & idx);
        xs_u8x16 v = xs_u8x16_load(d);
        xs_u8x16_store(d, xs_u8x16_shuffle(v, mask));
        return;
    }
#endif
    for (int64_t i = lo, j = hi; i < j; i++, j--) { uint8_t t = d[i]; d[i] = d[j]; d[j] = t; }
}
/* `for (j = lo; j < hi; j++) dst[j] = src[j]` on two byte arrays: one check each side, then a move */
static inline void xs_u8_copy_range(xs_Uint8Array *dst, xs_Uint8Array *src, int64_t lo, int64_t hi, const xs_site *s) {
    if (lo >= hi) return;
    if (lo < 0 || (uint64_t)(hi - 1) >= (uint64_t)src->f_length || (uint64_t)(hi - 1) >= (uint64_t)dst->f_length) XS_FAIL("index out of bounds", s);
    memmove(dst->data + lo, src->data + lo, (size_t)(hi - lo));
}

/* `for (j = lo; j <= hi; j++) a[j] = a[j + 1]` on a byte array (fannkuch-redux's next permutation): one check,
   then a move; in a register (emit.c u8reg) it is one shuffle */
static inline void xs_u8_shl(xs_Uint8Array *a, int64_t lo, int64_t hi, const xs_site *s) {
    if (lo > hi) return;
    if (lo < 0 || (uint64_t)(hi + 1) >= (uint64_t)a->f_length) XS_FAIL("index out of bounds", s);
    memmove(a->data + lo, a->data + lo + 1, (size_t)(hi - lo + 1));
}
static inline xs_u8x16 xs_v16_shl(xs_u8x16 v, int64_t lo, int64_t hi, int64_t n, const xs_site *s) {
    if (lo > hi) return v;
    if (lo < 0 || (uint64_t)(hi + 1) >= (uint64_t)XS_V16_LEN(n)) XS_FAIL(XS_V16_OOB(n), s);
    return xs_u8x16_shuffle(v, xs_shl_mask[lo][hi]);
}
/* the shift followed by `a[hi + 1] = x` where x is known to hold a[lo] (emit.c u8_rot_ok): a rotation of
   a[lo..hi + 1] to the left, one shuffle (the Rust fannkuch-redux's advance_array) */
static inline xs_u8x16 xs_v16_rot(xs_u8x16 v, int64_t lo, int64_t hi, int64_t x, int64_t n, const xs_site *s) {
    if (lo > hi) { if ((uint64_t)(hi + 1) >= (uint64_t)XS_V16_LEN(n)) XS_FAIL(XS_V16_OOB(n), s); return xs_v16_set_nc(v, hi + 1, x); } /* (the loop ran nothing) */
    if (lo < 0 || (uint64_t)(hi + 1) >= (uint64_t)XS_V16_LEN(n)) XS_FAIL(XS_V16_OOB(n), s);
    return xs_u8x16_shuffle(v, xs_rot_mask[lo][hi + 1]);
}
/* the shift followed by `a[hi + 1] = b` (fannkuch-redux's next permutation): the shuffle, then the lane */
static inline xs_u8x16 xs_v16_shl_put(xs_u8x16 v, int64_t lo, int64_t hi, int64_t b, int64_t n, const xs_site *s) {
    if (lo > hi) { if ((uint64_t)(hi + 1) >= (uint64_t)XS_V16_LEN(n)) XS_FAIL(XS_V16_OOB(n), s); return xs_v16_set_nc(v, hi + 1, b); }
    if (lo < 0 || (uint64_t)(hi + 1) >= (uint64_t)XS_V16_LEN(n)) XS_FAIL(XS_V16_OOB(n), s);
    return xs_v16_set_nc(xs_u8x16_shuffle(v, xs_shl_mask[lo][hi]), hi + 1, b);
}
static inline int64_t xs_u8_set(xs_Uint8Array *b, int64_t i, int64_t v, const xs_site *s) { *xs_u8_ptr(b, i, s) = (xs_byte)v; return v; }
/* b[i] where the compiler proved i within b (bounds-check elimination in emit.c): no check */
static inline xs_byte *xs_u8_at_nc(xs_Uint8Array *b, int64_t i) { return (xs_byte *)&b->data[i]; }
static inline int64_t xs_u8_set_nc(xs_Uint8Array *b, int64_t i, int64_t v) { *xs_u8_at_nc(b, i) = (xs_byte)v; return v; }

/* relative index as slice() reads it: negative from the end, clamped; INT64_MIN: "not given" -> dflt */
static int64_t xs_u8_rel(int64_t i, int64_t len, int64_t dflt) {
    if (i == INT64_MIN) return dflt;
    if (i < 0) { i += len; return i < 0 ? 0 : i; }
    return i > len ? len : i;
}
XS_NOINLINE xs_Uint8Array *xs_u8_slice(xs_Uint8Array *b, int64_t start, int64_t end) { /* a copy (a Buffer's slice is a view, as in Node) */
    int64_t s = xs_u8_rel(start, b->f_length, 0), e = xs_u8_rel(end, b->f_length, b->f_length);
    if (xs_is_buffer(b)) {
        xs_Uint8Array *v = xs_obj_alloc(sizeof *v, xs_u8_free, xs_buf_print);
        v->f_length = e > s ? e - s : 0;
        v->data = b->data + s;
        v->owner = xs_retain(b->owner ? b->owner : b);
        return v;
    }
    xs_Uint8Array *r = xs_u8_raw(e > s ? e - s : 0, false, false);
    if (e > s) memcpy(r->data, b->data + s, (size_t)(e - s));
    return r;
}
XS_NOINLINE xs_Uint8Array *xs_u8_subarray(xs_Uint8Array *b, int64_t start, int64_t end) { /* a view on the same bytes */
    int64_t s = xs_u8_rel(start, b->f_length, 0), e = xs_u8_rel(end, b->f_length, b->f_length);
    xs_Uint8Array *v = xs_is_buffer(b) ? xs_obj_alloc(sizeof *v, xs_u8_free, xs_buf_print) : xs_obj_alloc(sizeof *v, xs_u8_free, xs_u8_print);
    v->f_length = e > s ? e - s : 0;
    v->data = b->data + s;
    v->owner = xs_retain(b->owner ? b->owner : b);
    return v;
}
XS_NOINLINE void xs_u8_set_from(xs_Uint8Array *dst, xs_Uint8Array *src, int64_t off) {
    if (off < 0 || off + src->f_length > dst->f_length) xs_panic("RangeError: offset is out of bounds");
    xs_lent_wait(dst->data + off, (size_t)src->f_length);
    memmove(dst->data + off, src->data, (size_t)src->f_length);
}
XS_NOINLINE void xs_u8_set_ints(xs_Uint8Array *dst, xs_arr_int *src, int64_t off) {
    if (off < 0 || off + (int64_t)src->len > dst->f_length) xs_panic("RangeError: offset is out of bounds");
    for (size_t i = 0; i < src->len; i++) dst->data[off + (int64_t)i] = (uint8_t)src->data[i];
}
XS_NOINLINE xs_Uint8Array *xs_u8_fill(xs_Uint8Array *b, int64_t v, int64_t start, int64_t end) {
    int64_t s = xs_u8_rel(start, b->f_length, 0), e = xs_u8_rel(end, b->f_length, b->f_length);
    if (e > s) memset(b->data + s, (int)(uint8_t)v, (size_t)(e - s));
    return xs_retain(b); /* the array itself (a method result is owned) */
}
XS_NOINLINE int64_t xs_u8_index_of(xs_Uint8Array *b, int64_t v, int64_t from) {
    if (v < 0 || v > 255) return -1;
    int64_t s = xs_u8_rel(from, b->f_length, 0);
    if (s >= b->f_length) return -1;
    const char *hp;
    unsigned char c = (unsigned char)v;
    const uint8_t *h = xs_find_par((const char *)b->data + s, (size_t)(b->f_length - s), (const char *)&c, 1, &hp) ? (const uint8_t *)hp
                                                                                                            : xs_memchr(b->data + s, (int)v, (size_t)(b->f_length - s));
    return h ? (int64_t)(h - b->data) : -1;
}
XS_NOINLINE bool xs_u8_includes(xs_Uint8Array *b, int64_t v) { return xs_u8_index_of(b, v, 0) >= 0; }
/* JS: from defaults to length - 1; a negative from counts from the end; past the end it is the last index */
XS_NOINLINE int64_t xs_u8_last_index_of(xs_Uint8Array *b, int64_t v, int64_t from) {
    if (v < 0 || v > 255) return -1;
    int64_t k = from == INT64_MIN ? b->f_length - 1 : from < 0 ? b->f_length + from : from >= b->f_length ? b->f_length - 1 : from;
    const uint8_t *d = b->data;
#if defined(__aarch64__) && defined(__ARM_NEON)
    const uint8x16_t pv = vdupq_n_u8((uint8_t)v);
    while (k >= 15) { /* 16 bytes a step, d[k-15..k]; the mask has 4 bits per byte (shrn), the highest is the answer */
        uint8x16_t eq = vceqq_u8(vld1q_u8(d + k - 15), pv);
        uint64_t m = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
        if (m) return k - 15 + (63 - __builtin_clzll(m)) / 4;
        k -= 16;
    }
#elif defined(__SSE2__)
    const __m128i pv = _mm_set1_epi8((char)v);
    while (k >= 15) { /* 16 bytes a step, d[k-15..k]: bit i of the mask is byte k-15+i, the highest is the answer */
        uint32_t m = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(d + k - 15)), pv));
        if (m) return k - 15 + (31 - __builtin_clz(m));
        k -= 16;
    }
#endif
    const uint64_t lows = 0x7f7f7f7f7f7f7f7full, pat = 0x0101010101010101ull * (uint64_t)v;
    while (k >= 7) { /* 8 bytes a step, d[k-7..k] (little-endian load: byte k is the top one) */
        uint64_t w;
        memcpy(&w, d + k - 7, 8);
        uint64_t x = w ^ pat, z = ~(((x & lows) + lows) | x | lows); /* exactly the high bit of each zero byte of x */
        if (z) return k - 7 + (63 - __builtin_clzll(z)) / 8;
        k -= 8;
    }
    for (; k >= 0; k--) if (d[k] == (uint8_t)v) return k;
    return -1;
}
XS_NOINLINE int64_t xs_buf_index_of_str(xs_Uint8Array *b, xs_str *p, int64_t from) {
    int64_t s = xs_u8_rel(from, b->f_length, 0);
    if (s > b->f_length) return -1;
    if (p->len == 0) return s;
    const char *h = xs_find((const char *)b->data + s, (size_t)(b->f_length - s), p->data, (size_t)p->len);
    return h ? (int64_t)((const uint8_t *)h - b->data) : -1;
}
XS_NOINLINE bool xs_buf_includes_str(xs_Uint8Array *b, xs_str *p, int64_t from) { return xs_buf_index_of_str(b, p, from) >= 0; }
XS_NOINLINE xs_Uint8Array *xs_u8_reverse(xs_Uint8Array *b) {
    for (int64_t i = 0, j = b->f_length - 1; i < j; i++, j--) { uint8_t t = b->data[i]; b->data[i] = b->data[j]; b->data[j] = t; }
    return xs_retain(b);
}
XS_NOINLINE bool xs_u8_equals(xs_Uint8Array *a, xs_Uint8Array *b) { return a->f_length == b->f_length && memcmp(a->data, b->data, (size_t)a->f_length) == 0; }
XS_NOINLINE xs_str *xs_u8_join(xs_Uint8Array *b, xs_str *sep) {
    xs_sb sb = {0};
    char t[4];
    for (int64_t i = 0; i < b->f_length; i++) {
        if (i) xs_sb_put(&sb, sep->data, (size_t)sep->len);
        int v = b->data[i], n = 0;
        if (v >= 100) t[n++] = (char)('0' + v / 100);
        if (v >= 10) t[n++] = (char)('0' + v / 10 % 10);
        t[n++] = (char)('0' + v % 10);
        xs_sb_put(&sb, t, (size_t)n);
    }
    xs_str *r = xs_str_new(sb.data ? sb.data : "", sb.len);
    free(sb.data);
    return r;
}
static xs_arr_int *xs_u8_to_ints(xs_Uint8Array *b) { /* Array.from(bytes) */
    xs_arr_int *a = xs_arr_int_new(b->f_length, 0);
    for (int64_t i = 0; i < b->f_length; i++) a->data[i] = b->data[i];
    return a;
}

/* ---- shared memory: SharedArrayBuffer and Float64Array ----
   Threads have heaps of their own and counts that are not atomic, so memory that two threads see is a block
   apart (xs_shblk: plain malloc, its own count atomic), and each thread holds it through objects of its own
   heap. A SharedArrayBuffer is such a block; a Float64Array is a view on one (a block of its own for
   `new Float64Array(n)`, which nothing else sees). postMessage of a SharedArrayBuffer, or of a Float64Array on
   one, gives the receiver a view on the same block, as in Node; a Float64Array on a block of its own goes as a
   copy. What one thread wrote is seen by another after a message between them (the queue's mutex orders the
   two): there are no Atomics. */
typedef struct xs_shblk { int64_t refs; int64_t n; } xs_shblk; /* n bytes follow, 16-byte aligned (malloc's) */
static inline char *xs_shblk_data(xs_shblk *b) { return (char *)(b + 1); }
/* Linux: a block of 64 MB or more is mapped (not malloc'ed) and offered huge pages before it is touched
   (MADV_HUGEPAGE, honoured when THP is "madvise" or "always"): the first touches then fault 2 MB at a time, ~20x
   fewer faults, and the loops over it miss the TLB less. Measured on the Xeon for 254 MB: calloc + touch 0,141 s
   of sys, MAP_POPULATE 0,080, huge pages 0,053 (pre-populating the huge pages costs more than touching them as
   the bytes arrive). revcomp-4/5 read 254 MB of stdin straight into such a block. */
#if defined(__linux__)
#include <sys/mman.h>
#define XS_SHBLK_MAP_MIN ((int64_t)64 << 20)
static inline bool xs_shblk_mapped(xs_shblk *b) { return b->n >= XS_SHBLK_MAP_MIN; }
#endif
XS_NOINLINE xs_shblk *xs_shblk_new(int64_t n, bool zero) {
    xs_shblk *b;
#if defined(__linux__)
    if (n >= XS_SHBLK_MAP_MIN) {
        size_t sz = sizeof *b + (size_t)n;
        void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) xs_panic("out of memory");
#ifdef MADV_HUGEPAGE
        madvise(p, sz, MADV_HUGEPAGE); /* (before any touch: the faults come in 2 MB) */
#endif
        b = p; /* (zero already) */
        b->refs = 1;
        b->n = n;
        return b;
    }
#endif
    b = zero ? calloc(1, sizeof *b + (size_t)n) : malloc(sizeof *b + (size_t)n);
    if (!b) xs_panic("out of memory");
    b->refs = 1;
    b->n = n;
    return b;
}
static inline void xs_shblk_ref(xs_shblk *b) { __atomic_add_fetch(&b->refs, 1, __ATOMIC_RELAXED); }
static inline void xs_shblk_free(xs_shblk *b) {
#if defined(__linux__)
    if (xs_shblk_mapped(b)) { munmap(b, sizeof *b + (size_t)b->n); return; }
#endif
    free(b);
}
static inline void xs_shblk_unref(xs_shblk *b) { if (b && __atomic_sub_fetch(&b->refs, 1, __ATOMIC_ACQ_REL) == 0) xs_shblk_free(b); }

typedef struct xs_SharedArrayBuffer { xs_hdr hdr; int64_t f_byteLength; xs_shblk *blk; } xs_SharedArrayBuffer;
static void xs_sab_free(void *p) { xs_shblk_unref(((xs_SharedArrayBuffer *)p)->blk); xs_obj_free(p); }
static void xs_sab_print(void *p) { /* SharedArrayBuffer { [Uint8Contents]: <00 01>, [byteLength]: 2 } */
    xs_SharedArrayBuffer *s = p;
    const uint8_t *d = (const uint8_t *)xs_shblk_data(s->blk);
    xs_insp fr;
    if (!xs_insp_begin(&fr, p, "SharedArrayBuffer", NULL)) return;
    xs_insp_entry(&fr);
    fputs("[Uint8Contents]: <", XS_OUT);
    int64_t n = s->f_byteLength < 100 ? s->f_byteLength : 100; /* (Node shows up to 100 bytes) */
    for (int64_t i = 0; i < n; i++) fprintf(XS_OUT, i ? " %02x" : "%02x", d[i]);
    if (s->f_byteLength > 100) fprintf(XS_OUT, " ... %lld more byte%s", (long long)(s->f_byteLength - 100), s->f_byteLength - 100 == 1 ? "" : "s");
    fputc('>', XS_OUT);
    xs_insp_entry(&fr);
    fprintf(XS_OUT, "[byteLength]: %lld", (long long)s->f_byteLength);
    xs_insp_end(&fr, "SharedArrayBuffer {", "}", false, false, false);
}
/* a buffer on blk, whose reference it takes over */
XS_NOINLINE xs_SharedArrayBuffer *xs_sab_wrap(xs_shblk *blk) {
    xs_SharedArrayBuffer *s = xs_obj_alloc(sizeof *s, xs_sab_free, xs_sab_print);
    s->f_byteLength = blk->n;
    s->blk = blk;
    return s;
}
XS_NOINLINE xs_SharedArrayBuffer *xs_sab_new(int64_t n) {
    if (n < 0 || n > ((int64_t)1 << 40)) xs_panic("RangeError: Invalid array buffer length");
    return xs_sab_wrap(xs_shblk_new(n, true));
}
/* new Uint8Array(sab, byteOffset?, length?): a view on the buffer's bytes (INT64_MIN: not given). It holds the
   buffer as a subarray holds its array (owner), so a message carries it as the block, not as a copy. */
XS_NOINLINE xs_Uint8Array *xs_u8_on(xs_SharedArrayBuffer *s, int64_t off, int64_t n) {
    if (off == INT64_MIN) off = 0;
    if (off < 0 || off > s->f_byteLength) xs_panic("RangeError: start offset is outside the bounds of the buffer");
    if (n == INT64_MIN) n = s->f_byteLength - off;
    else if (n < 0 || n > s->f_byteLength - off) xs_panic("RangeError: Invalid typed array length");
    xs_Uint8Array *b = xs_obj_alloc(sizeof *b, xs_u8_free, xs_u8_print);
    b->f_length = n;
    b->data = (uint8_t *)xs_shblk_data(s->blk) + off;
    b->owner = xs_retain(s);
    return b;
}
/* the shared block under a Uint8Array on a SharedArrayBuffer (or a subarray of one); NULL for bytes of its own */
static inline xs_shblk *xs_u8_shared(xs_Uint8Array *b) {
    return b->owner && xs_type_of(b->owner)->free == xs_sab_free ? ((xs_SharedArrayBuffer *)b->owner)->blk : NULL;
}
static inline int64_t xs_u8_byte_offset(xs_Uint8Array *b) {
    xs_shblk *k = xs_u8_shared(b);
    if (k) return (int64_t)((char *)b->data - xs_shblk_data(k));
    return b->owner ? (int64_t)(b->data - ((xs_Uint8Array *)b->owner)->data) : 0;
}

/* the length is also `len`, the name the loop versioning of emit.c (bce_version_guard) reads arrays' by */
typedef struct xs_Float64Array { xs_hdr hdr; union { int64_t f_length; int64_t len; }; double *data; xs_shblk *blk; bool shared; } xs_Float64Array;
static void xs_f64_free(void *p) { xs_shblk_unref(((xs_Float64Array *)p)->blk); xs_obj_free(p); }
static void xs_f64_print(void *p) {
    xs_Float64Array *a = p;
    xs_insp fr;
    if (!xs_insp_begin(&fr, p, "Float64Array", a->f_length ? NULL : "Float64Array(0) []")) return;
    int64_t n = a->f_length < 100 ? a->f_length : 100;
    for (int64_t i = 0; i < n; i++) { xs_insp_entry(&fr); xs_print_num(a->data[i]); }
    if (a->f_length > 100) { xs_insp_entry(&fr); fprintf(XS_OUT, "... %lld more item%s", (long long)(a->f_length - 100), a->f_length - 100 == 1 ? "" : "s"); }
    char open[48];
    snprintf(open, sizeof open, "Float64Array(%lld) [", (long long)a->f_length);
    xs_insp_end(&fr, open, "]", true, true, a->f_length > 100);
}
/* a view of n doubles from the off-th one of blk, whose reference it takes over */
XS_NOINLINE xs_Float64Array *xs_f64_wrap(xs_shblk *blk, int64_t off, int64_t n, bool shared) {
    xs_Float64Array *a = xs_obj_alloc(sizeof *a, xs_f64_free, xs_f64_print);
    a->f_length = n;
    a->data = (double *)xs_shblk_data(blk) + off;
    a->blk = blk;
    a->shared = shared;
    return a;
}
XS_NOINLINE xs_Float64Array *xs_f64_new(int64_t n) {
    if (n < 0 || n > ((int64_t)1 << 37)) xs_panic("RangeError: Invalid typed array length");
    return xs_f64_wrap(xs_shblk_new(n * 8, true), 0, n, false);
}
XS_NOINLINE xs_Float64Array *xs_f64_from_nums(xs_arr_num *s) {
    xs_Float64Array *a = xs_f64_wrap(xs_shblk_new((int64_t)s->len * 8, false), 0, (int64_t)s->len, false);
    if (s->len) memcpy(a->data, s->data, s->len * sizeof(double));
    return a;
}
XS_NOINLINE xs_Float64Array *xs_f64_from_ints(xs_arr_int *s) {
    xs_Float64Array *a = xs_f64_wrap(xs_shblk_new((int64_t)s->len * 8, false), 0, (int64_t)s->len, false);
    for (size_t i = 0; i < s->len; i++) a->data[i] = (double)s->data[i];
    return a;
}
XS_NOINLINE xs_Float64Array *xs_f64_copy(xs_Float64Array *s) {
    xs_Float64Array *a = xs_f64_wrap(xs_shblk_new(s->f_length * 8, false), 0, s->f_length, false);
    if (s->f_length) memcpy(a->data, s->data, (size_t)s->f_length * sizeof(double));
    return a;
}
/* new Float64Array(sab, byteOffset?, length?): a view on the buffer (INT64_MIN: not given) */
XS_NOINLINE xs_Float64Array *xs_f64_on(xs_SharedArrayBuffer *s, int64_t off, int64_t n) {
    if (off == INT64_MIN) off = 0;
    if (off < 0 || off % 8) xs_panic("RangeError: start offset of Float64Array should be a multiple of 8");
    if (off > s->f_byteLength) xs_panic("RangeError: start offset is outside the bounds of the buffer");
    if (n == INT64_MIN) {
        if ((s->f_byteLength - off) % 8) xs_panic("RangeError: byte length of Float64Array should be a multiple of 8");
        n = (s->f_byteLength - off) / 8;
    } else if (n < 0 || n > (s->f_byteLength - off) / 8) xs_panic("RangeError: Invalid typed array length");
    xs_shblk_ref(s->blk);
    return xs_f64_wrap(s->blk, off / 8, n, true);
}
/* the length, with the bound every Float64Array has: as xs_u8_len, it lets the compiler drop overflow checks */
static inline int64_t xs_f64_len(xs_Float64Array *a) {
    int64_t n = a->f_length;
#if defined(__clang__)
    __builtin_assume(n >= 0 && n <= ((int64_t)1 << 37));
#else
    if (n < 0 || n > ((int64_t)1 << 37)) __builtin_unreachable();
#endif
    return n;
}
/* ---- Int32Array and Atomics (as in Node) ----
   An Int32Array is a view of 32-bit integers on a SharedArrayBuffer (or on memory of its own); the program reaches
   its elements through Atomics, which is what it is for here: threads that meet in shared memory without a message
   (a barrier between a thread and a worker). Atomics.wait sleeps until Atomics.notify on the same element (or the
   timeout), as Node's: "ok", "not-equal" (the element was not the value) or "timed-out". The waiters sit in one list
   under one lock (a wait is a sleep, so the lock is not where the time goes), each with its element's address. */
typedef struct xs_Int32Array { xs_hdr hdr; union { int64_t f_length; int64_t len; }; int32_t *data; xs_shblk *blk; bool shared; } xs_Int32Array;
static void xs_i32_free(void *p) { xs_shblk_unref(((xs_Int32Array *)p)->blk); xs_obj_free(p); }
static void xs_i32_print(void *p) {
    xs_Int32Array *a = p;
    xs_insp fr;
    if (!xs_insp_begin(&fr, p, "Int32Array", a->f_length ? NULL : "Int32Array(0) []")) return;
    int64_t n = a->f_length < 100 ? a->f_length : 100;
    for (int64_t i = 0; i < n; i++) { xs_insp_entry(&fr); fprintf(XS_OUT, "%d", __atomic_load_n(&a->data[i], __ATOMIC_RELAXED)); }
    if (a->f_length > 100) { xs_insp_entry(&fr); fprintf(XS_OUT, "... %lld more item%s", (long long)(a->f_length - 100), a->f_length - 100 == 1 ? "" : "s"); }
    char open[48];
    snprintf(open, sizeof open, "Int32Array(%lld) [", (long long)a->f_length);
    xs_insp_end(&fr, open, "]", true, true, a->f_length > 100);
}
static xs_Int32Array *xs_i32_wrap(xs_shblk *blk, int64_t off, int64_t n, bool shared) {
    xs_Int32Array *a = xs_obj_alloc(sizeof *a, xs_i32_free, xs_i32_print);
    a->f_length = n;
    a->data = (int32_t *)xs_shblk_data(blk) + off;
    a->blk = blk;
    a->shared = shared;
    return a;
}
XS_NOINLINE xs_Int32Array *xs_i32_new(int64_t n) {
    if (n < 0 || n > ((int64_t)1 << 37)) xs_panic("RangeError: Invalid typed array length");
    return xs_i32_wrap(xs_shblk_new(n * 4, true), 0, n, false);
}
/* new Int32Array(sab, byteOffset?, length?): a view on the buffer (INT64_MIN: not given) */
XS_NOINLINE xs_Int32Array *xs_i32_on(xs_SharedArrayBuffer *s, int64_t off, int64_t n) {
    if (off == INT64_MIN) off = 0;
    if (off < 0 || off % 4) xs_panic("RangeError: start offset of Int32Array should be a multiple of 4");
    if (off > s->f_byteLength) xs_panic("RangeError: start offset is outside the bounds of the buffer");
    if (n == INT64_MIN) {
        if ((s->f_byteLength - off) % 4) xs_panic("RangeError: byte length of Int32Array should be a multiple of 4");
        n = (s->f_byteLength - off) / 4;
    } else if (n < 0 || n > (s->f_byteLength - off) / 4) xs_panic("RangeError: Invalid typed array length");
    xs_shblk_ref(s->blk);
    return xs_i32_wrap(s->blk, off / 4, n, true);
}
XS_NOINLINE int32_t *xs_atomics_at(xs_Int32Array *a, int64_t i) {
    if ((uint64_t)i >= (uint64_t)a->f_length) xs_panic("RangeError: Invalid atomic access index");
    return &a->data[i];
}
XS_NOINLINE int64_t xs_atomics_load(xs_Int32Array *a, int64_t i) { return __atomic_load_n(xs_atomics_at(a, i), __ATOMIC_SEQ_CST); }
XS_NOINLINE int64_t xs_atomics_store(xs_Int32Array *a, int64_t i, int64_t v) { __atomic_store_n(xs_atomics_at(a, i), (int32_t)(uint32_t)v, __ATOMIC_SEQ_CST); return v; }
XS_NOINLINE int64_t xs_atomics_add(xs_Int32Array *a, int64_t i, int64_t v) { return (int32_t)__atomic_fetch_add((uint32_t *)xs_atomics_at(a, i), (uint32_t)v, __ATOMIC_SEQ_CST); }
XS_NOINLINE int64_t xs_atomics_sub(xs_Int32Array *a, int64_t i, int64_t v) { return (int32_t)__atomic_fetch_sub((uint32_t *)xs_atomics_at(a, i), (uint32_t)v, __ATOMIC_SEQ_CST); }
XS_NOINLINE int64_t xs_atomics_exchange(xs_Int32Array *a, int64_t i, int64_t v) { return __atomic_exchange_n(xs_atomics_at(a, i), (int32_t)(uint32_t)v, __ATOMIC_SEQ_CST); }
XS_NOINLINE int64_t xs_atomics_cmpxchg(xs_Int32Array *a, int64_t i, int64_t expected, int64_t v) {
    int32_t e = (int32_t)(uint32_t)expected;
    __atomic_compare_exchange_n(xs_atomics_at(a, i), &e, (int32_t)(uint32_t)v, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return e; /* (the element as it was: the expected value when the exchange happened) */
}
#if !defined(_WIN32)
#include <pthread.h>
typedef struct xs_awaiter { struct xs_awaiter *next; int32_t *addr; bool woken; } xs_awaiter;
static pthread_mutex_t xs_aw_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t xs_aw_cv = PTHREAD_COND_INITIALIZER;
static xs_awaiter *xs_aw_list;
/* Atomics.wait(a, i, value, timeout ms = Infinity) */
XS_NOINLINE xs_str *xs_atomics_wait(xs_Int32Array *a, int64_t i, int64_t value, double timeout) {
    int32_t *p = xs_atomics_at(a, i);
    if (!a->shared) xs_panic("TypeError: Atomics.wait needs an Int32Array on a SharedArrayBuffer");
    pthread_mutex_lock(&xs_aw_mu);
    if (__atomic_load_n(p, __ATOMIC_SEQ_CST) != (int32_t)(uint32_t)value) { pthread_mutex_unlock(&xs_aw_mu); return xs_str_new("not-equal", 9); }
    xs_awaiter me = {xs_aw_list, p, false};
    xs_aw_list = &me;
    bool forever = !(timeout < INFINITY), timed = false;
    struct timespec until;
    if (!forever) {
        if (!(timeout > 0)) timeout = 0; /* (NaN, negative: no wait) */
        clock_gettime(CLOCK_REALTIME, &until);
        double s = (double)until.tv_sec + until.tv_nsec / 1e9 + timeout / 1000.0;
        until.tv_sec = (time_t)s;
        until.tv_nsec = (long)((s - (double)until.tv_sec) * 1e9);
    }
    while (!me.woken) {
        if (forever) pthread_cond_wait(&xs_aw_cv, &xs_aw_mu);
        else if (pthread_cond_timedwait(&xs_aw_cv, &xs_aw_mu, &until) != 0 && !me.woken) { timed = true; break; }
    }
    for (xs_awaiter **w = &xs_aw_list; *w; w = &(*w)->next) if (*w == &me) { *w = me.next; break; }
    pthread_mutex_unlock(&xs_aw_mu);
    return timed ? xs_str_new("timed-out", 9) : xs_str_new("ok", 2);
}
/* Atomics.notify(a, i, count = all): how many waiters it woke, the longest waiting first (as Node) */
XS_NOINLINE int64_t xs_atomics_notify(xs_Int32Array *a, int64_t i, double count) {
    int32_t *p = xs_atomics_at(a, i);
    if (!a->shared) return 0;
    int64_t want = !(count < INFINITY) ? INT64_MAX : count > 0 ? (int64_t)count : 0, n = 0;
    pthread_mutex_lock(&xs_aw_mu);
    xs_awaiter *order[64]; int no = 0; /* (the list is newest first: the oldest are at its end) */
    for (xs_awaiter *w = xs_aw_list; w; w = w->next) if (w->addr == p && !w->woken && no < 64) order[no++] = w;
    for (int k = no - 1; k >= 0 && n < want; k--) { order[k]->woken = true; n++; }
    if (n) pthread_cond_broadcast(&xs_aw_cv);
    pthread_mutex_unlock(&xs_aw_mu);
    return n;
}
#endif
/* a Float64Array that lives in its function's frame (emit.c f64_local_n: fixed small length, only ever indexed, never
   escaping): no heap, no count; the C compiler sees a plain double[N] and keeps it in registers (n-body) */
static inline __attribute__((always_inline)) xs_Float64Array *xs_f64_on_frame(xs_Float64Array *a, double *data, int64_t n) {
    a->hdr.rc = 1;
    a->hdr.cls = XS_STACK;
    a->f_length = n;
    a->data = data;
    a->blk = NULL;
    a->shared = false;
    return a;
}
static inline double *xs_f64_at_s(xs_Float64Array *a, int64_t i, const xs_site *s) {
    if ((uint64_t)i >= (uint64_t)a->f_length) XS_FAIL("index out of bounds", s);
    return &a->data[i];
}
static inline int64_t xs_f64_byte_length(xs_Float64Array *a) { return a->f_length * 8; }
static inline int64_t xs_f64_byte_offset(xs_Float64Array *a) { return (int64_t)((char *)a->data - xs_shblk_data(a->blk)); }
XS_NOINLINE xs_Float64Array *xs_f64_fill(xs_Float64Array *a, double v, int64_t start, int64_t end) {
    int64_t s = xs_u8_rel(start, a->f_length, 0), e = xs_u8_rel(end, a->f_length, a->f_length);
    for (int64_t i = s; i < e; i++) a->data[i] = v;
    return xs_retain(a);
}
XS_NOINLINE void xs_f64_set_check(int64_t n, int64_t off, int64_t len) {
    if (off < 0 || off > len || n > len - off) xs_panic("RangeError: offset is out of bounds");
}
XS_NOINLINE void xs_f64_set_from(xs_Float64Array *d, xs_Float64Array *s, int64_t off) {
    xs_f64_set_check(s->f_length, off, d->f_length);
    if (s->f_length) memmove(d->data + off, s->data, (size_t)s->f_length * sizeof(double)); /* (views on one block may overlap) */
}
XS_NOINLINE void xs_f64_set_nums(xs_Float64Array *d, xs_arr_num *s, int64_t off) {
    xs_f64_set_check((int64_t)s->len, off, d->f_length);
    if (s->len) memcpy(d->data + off, s->data, s->len * sizeof(double));
}
XS_NOINLINE xs_Float64Array *xs_f64_subarray(xs_Float64Array *a, int64_t start, int64_t end) { /* a view on the same block */
    int64_t s = xs_u8_rel(start, a->f_length, 0), e = xs_u8_rel(end, a->f_length, a->f_length);
    xs_shblk_ref(a->blk);
    return xs_f64_wrap(a->blk, (a->data - (double *)xs_shblk_data(a->blk)) + s, e > s ? e - s : 0, a->shared);
}
XS_NOINLINE xs_Float64Array *xs_f64_slice(xs_Float64Array *a, int64_t start, int64_t end) { /* a copy, of its own */
    int64_t s = xs_u8_rel(start, a->f_length, 0), e = xs_u8_rel(end, a->f_length, a->f_length), n = e > s ? e - s : 0;
    xs_Float64Array *r = xs_f64_wrap(xs_shblk_new(n * 8, false), 0, n, false);
    if (n) memcpy(r->data, a->data + s, (size_t)n * sizeof(double));
    return r;
}
XS_NOINLINE xs_str *xs_f64_join(xs_Float64Array *a, xs_str *sep) {
    xs_sb sb = {0};
    char t[32];
    for (int64_t i = 0; i < a->f_length; i++) {
        if (i) xs_sb_put(&sb, sep->data, (size_t)sep->len);
        if (a->data[i] == 0) xs_sb_put(&sb, "0", 1); /* (-0 joins as 0) */
        else { xs_num_to_str(a->data[i], t); xs_sb_put(&sb, t, strlen(t)); }
    }
    xs_str *r = xs_str_new(sb.data ? sb.data : "", sb.len);
    free(sb.data);
    return r;
}
XS_NOINLINE xs_str *xs_f64_to_string(xs_Float64Array *a) {
    xs_str *c = xs_str_new(",", 1), *r = xs_f64_join(a, c);
    xs_release(c);
    return r;
}

/* ---- Float32Array (as in Node) ----
   4 bytes per element, a float of C. An element read is the float as a double (exact); a write rounds the double to
   the nearest float, ties to even (C's conversion in the default rounding mode, as JavaScript's), so a value too large
   becomes an Infinity, a tiny one a subnormal or a zero of its sign, and NaN, the Infinities and -0 stay. The program's
   arithmetic stays in double: only the reads and the writes convert (no fused operations: -ffp-contract=off). */
static inline __attribute__((always_inline)) double xs_f32_num(float x) { return (double)x; } /* an element read: exact */
typedef struct xs_Float32Array { xs_hdr hdr; union { int64_t f_length; int64_t len; }; float *data; xs_shblk *blk; bool shared; } xs_Float32Array;
static void xs_f32_free(void *p) { xs_shblk_unref(((xs_Float32Array *)p)->blk); xs_obj_free(p); }
static void xs_f32_print(void *p) {
    xs_Float32Array *a = p;
    xs_insp fr;
    if (!xs_insp_begin(&fr, p, "Float32Array", a->f_length ? NULL : "Float32Array(0) []")) return;
    int64_t n = a->f_length < 100 ? a->f_length : 100;
    for (int64_t i = 0; i < n; i++) { xs_insp_entry(&fr); xs_print_num((double)a->data[i]); }
    if (a->f_length > 100) { xs_insp_entry(&fr); fprintf(XS_OUT, "... %lld more item%s", (long long)(a->f_length - 100), a->f_length - 100 == 1 ? "" : "s"); }
    char open[48];
    snprintf(open, sizeof open, "Float32Array(%lld) [", (long long)a->f_length);
    xs_insp_end(&fr, open, "]", true, true, a->f_length > 100);
}
/* a view of n floats from the off-th one of blk, whose reference it takes over */
XS_NOINLINE xs_Float32Array *xs_f32_wrap(xs_shblk *blk, int64_t off, int64_t n, bool shared) {
    xs_Float32Array *a = xs_obj_alloc(sizeof *a, xs_f32_free, xs_f32_print);
    a->f_length = n;
    a->data = (float *)xs_shblk_data(blk) + off;
    a->blk = blk;
    a->shared = shared;
    return a;
}
XS_NOINLINE xs_Float32Array *xs_f32_new(int64_t n) {
    if (n < 0 || n > ((int64_t)1 << 38)) xs_panic("RangeError: Invalid typed array length");
    return xs_f32_wrap(xs_shblk_new(n * 4, true), 0, n, false);
}
XS_NOINLINE xs_Float32Array *xs_f32_from_nums(xs_arr_num *s) {
    xs_Float32Array *a = xs_f32_wrap(xs_shblk_new((int64_t)s->len * 4, false), 0, (int64_t)s->len, false);
    for (size_t i = 0; i < s->len; i++) a->data[i] = (float)s->data[i];
    return a;
}
XS_NOINLINE xs_Float32Array *xs_f32_from_ints(xs_arr_int *s) {
    xs_Float32Array *a = xs_f32_wrap(xs_shblk_new((int64_t)s->len * 4, false), 0, (int64_t)s->len, false);
    for (size_t i = 0; i < s->len; i++) a->data[i] = (float)(double)s->data[i]; /* (through the double the int is as a number) */
    return a;
}
XS_NOINLINE xs_Float32Array *xs_f32_copy(xs_Float32Array *s) {
    xs_Float32Array *a = xs_f32_wrap(xs_shblk_new(s->f_length * 4, false), 0, s->f_length, false);
    if (s->f_length) memcpy(a->data, s->data, (size_t)s->f_length * sizeof(float));
    return a;
}
XS_NOINLINE xs_Float32Array *xs_f32_from_f64(xs_Float64Array *s) {
    xs_Float32Array *a = xs_f32_wrap(xs_shblk_new(s->f_length * 4, false), 0, s->f_length, false);
    for (int64_t i = 0; i < s->f_length; i++) a->data[i] = (float)s->data[i];
    return a;
}
XS_NOINLINE xs_Float64Array *xs_f64_from_f32(xs_Float32Array *s) {
    xs_Float64Array *a = xs_f64_wrap(xs_shblk_new(s->f_length * 8, false), 0, s->f_length, false);
    for (int64_t i = 0; i < s->f_length; i++) a->data[i] = (double)s->data[i];
    return a;
}
/* new Float32Array(sab, byteOffset?, length?): a view on the buffer (INT64_MIN: not given) */
XS_NOINLINE xs_Float32Array *xs_f32_on(xs_SharedArrayBuffer *s, int64_t off, int64_t n) {
    if (off == INT64_MIN) off = 0;
    if (off < 0 || off % 4) xs_panic("RangeError: start offset of Float32Array should be a multiple of 4");
    if (off > s->f_byteLength) xs_panic("RangeError: start offset is outside the bounds of the buffer");
    if (n == INT64_MIN) {
        if ((s->f_byteLength - off) % 4) xs_panic("RangeError: byte length of Float32Array should be a multiple of 4");
        n = (s->f_byteLength - off) / 4;
    } else if (n < 0 || n > (s->f_byteLength - off) / 4) xs_panic("RangeError: Invalid typed array length");
    xs_shblk_ref(s->blk);
    return xs_f32_wrap(s->blk, off / 4, n, true);
}
/* the length, with the bound every Float32Array has (as xs_f64_len) */
static inline int64_t xs_f32_len(xs_Float32Array *a) {
    int64_t n = a->f_length;
#if defined(__clang__)
    __builtin_assume(n >= 0 && n <= ((int64_t)1 << 38));
#else
    if (n < 0 || n > ((int64_t)1 << 38)) __builtin_unreachable();
#endif
    return n;
}
static inline float *xs_f32_at_s(xs_Float32Array *a, int64_t i, const xs_site *s) {
    if ((uint64_t)i >= (uint64_t)a->f_length) XS_FAIL("index out of bounds", s);
    return &a->data[i];
}
static inline int64_t xs_f32_byte_length(xs_Float32Array *a) { return a->f_length * 4; }
static inline int64_t xs_f32_byte_offset(xs_Float32Array *a) { return (int64_t)((char *)a->data - xs_shblk_data(a->blk)); }
XS_NOINLINE xs_Float32Array *xs_f32_fill(xs_Float32Array *a, double v, int64_t start, int64_t end) {
    int64_t s = xs_u8_rel(start, a->f_length, 0), e = xs_u8_rel(end, a->f_length, a->f_length);
    float f = (float)v;
    for (int64_t i = s; i < e; i++) a->data[i] = f;
    return xs_retain(a);
}
XS_NOINLINE void xs_f32_set_from(xs_Float32Array *d, xs_Float32Array *s, int64_t off) {
    xs_f64_set_check(s->f_length, off, d->f_length);
    if (s->f_length) memmove(d->data + off, s->data, (size_t)s->f_length * sizeof(float)); /* (views on one block may overlap) */
}
XS_NOINLINE void xs_f32_set_nums(xs_Float32Array *d, xs_arr_num *s, int64_t off) {
    xs_f64_set_check((int64_t)s->len, off, d->f_length);
    for (size_t i = 0; i < s->len; i++) d->data[off + (int64_t)i] = (float)s->data[i];
}
XS_NOINLINE void xs_f32_set_f64(xs_Float32Array *d, xs_Float64Array *s, int64_t off) {
    xs_f64_set_check(s->f_length, off, d->f_length);
    if (!s->f_length) return;
    if (s->blk == d->blk) { /* (views on one block: the source read whole before any write, as the spec's clone) */
        float *t = malloc((size_t)s->f_length * sizeof(float));
        if (!t) xs_panic("out of memory");
        for (int64_t i = 0; i < s->f_length; i++) t[i] = (float)s->data[i];
        memcpy(d->data + off, t, (size_t)s->f_length * sizeof(float));
        free(t);
        return;
    }
    for (int64_t i = 0; i < s->f_length; i++) d->data[off + i] = (float)s->data[i];
}
XS_NOINLINE void xs_f64_set_f32(xs_Float64Array *d, xs_Float32Array *s, int64_t off) {
    xs_f64_set_check(s->f_length, off, d->f_length);
    if (!s->f_length) return;
    if (s->blk == d->blk) {
        double *t = malloc((size_t)s->f_length * sizeof(double));
        if (!t) xs_panic("out of memory");
        for (int64_t i = 0; i < s->f_length; i++) t[i] = (double)s->data[i];
        memcpy(d->data + off, t, (size_t)s->f_length * sizeof(double));
        free(t);
        return;
    }
    for (int64_t i = 0; i < s->f_length; i++) d->data[off + i] = (double)s->data[i];
}
XS_NOINLINE xs_Float32Array *xs_f32_subarray(xs_Float32Array *a, int64_t start, int64_t end) { /* a view on the same block */
    int64_t s = xs_u8_rel(start, a->f_length, 0), e = xs_u8_rel(end, a->f_length, a->f_length);
    xs_shblk_ref(a->blk);
    return xs_f32_wrap(a->blk, (a->data - (float *)xs_shblk_data(a->blk)) + s, e > s ? e - s : 0, a->shared);
}
XS_NOINLINE xs_Float32Array *xs_f32_slice(xs_Float32Array *a, int64_t start, int64_t end) { /* a copy, of its own */
    int64_t s = xs_u8_rel(start, a->f_length, 0), e = xs_u8_rel(end, a->f_length, a->f_length), n = e > s ? e - s : 0;
    xs_Float32Array *r = xs_f32_wrap(xs_shblk_new(n * 4, false), 0, n, false);
    if (n) memcpy(r->data, a->data + s, (size_t)n * sizeof(float));
    return r;
}
XS_NOINLINE xs_str *xs_f32_join(xs_Float32Array *a, xs_str *sep) {
    xs_sb sb = {0};
    char t[32];
    for (int64_t i = 0; i < a->f_length; i++) {
        if (i) xs_sb_put(&sb, sep->data, (size_t)sep->len);
        double v = (double)a->data[i];
        if (v == 0) xs_sb_put(&sb, "0", 1); /* (-0 joins as 0) */
        else { xs_num_to_str(v, t); xs_sb_put(&sb, t, strlen(t)); }
    }
    xs_str *r = xs_str_new(sb.data ? sb.data : "", sb.len);
    free(sb.data);
    return r;
}
XS_NOINLINE xs_str *xs_f32_to_string(xs_Float32Array *a) {
    xs_str *c = xs_str_new(",", 1), *r = xs_f32_join(a, c);
    xs_release(c);
    return r;
}

/* ---- Int32Array elements (as in Node): i[k] is an int; a write is ToInt32 of the value (an int wrapped modulo 2^32,
   a number truncated first, NaN and the Infinities 0) ---- */
static inline __attribute__((always_inline)) int32_t xs_toint32_i(int64_t v) { return (int32_t)(uint32_t)(uint64_t)v; }
static inline int32_t xs_toint32_d(double d) {
    if (d > -9.2e18 && d < 9.2e18) return (int32_t)(uint32_t)(uint64_t)(int64_t)d; /* (truncates toward 0; NaN fails both tests) */
    if (!isfinite(d)) return 0;
    double m = fmod(trunc(d), 4294967296.0); /* exact: |d| >= 2^63 is an integer */
    if (m < 0) m += 4294967296.0;
    return (int32_t)(uint32_t)m;
}
/* the length, with the bound every Int32Array has (as xs_f64_len) */
static inline int64_t xs_i32_len(xs_Int32Array *a) {
    int64_t n = a->f_length;
#if defined(__clang__)
    __builtin_assume(n >= 0 && n <= ((int64_t)1 << 38));
#else
    if (n < 0 || n > ((int64_t)1 << 38)) __builtin_unreachable();
#endif
    return n;
}
static inline int32_t *xs_i32_at_s(xs_Int32Array *a, int64_t i, const xs_site *s) {
    if ((uint64_t)i >= (uint64_t)a->f_length) XS_FAIL("index out of bounds", s);
    return &a->data[i];
}
static inline int64_t xs_i32_byte_length(xs_Int32Array *a) { return a->f_length * 4; }
static inline int64_t xs_i32_byte_offset(xs_Int32Array *a) { return (int64_t)((char *)a->data - xs_shblk_data(a->blk)); }
XS_NOINLINE xs_Int32Array *xs_i32_from_ints(xs_arr_int *s) {
    xs_Int32Array *a = xs_i32_wrap(xs_shblk_new((int64_t)s->len * 4, false), 0, (int64_t)s->len, false);
    for (size_t i = 0; i < s->len; i++) a->data[i] = xs_toint32_i(s->data[i]);
    return a;
}
XS_NOINLINE xs_Int32Array *xs_i32_from_nums(xs_arr_num *s) {
    xs_Int32Array *a = xs_i32_wrap(xs_shblk_new((int64_t)s->len * 4, false), 0, (int64_t)s->len, false);
    for (size_t i = 0; i < s->len; i++) a->data[i] = xs_toint32_d(s->data[i]);
    return a;
}
XS_NOINLINE xs_Int32Array *xs_i32_copy(xs_Int32Array *s) {
    xs_Int32Array *a = xs_i32_wrap(xs_shblk_new(s->f_length * 4, false), 0, s->f_length, false);
    if (s->f_length) memcpy(a->data, s->data, (size_t)s->f_length * sizeof(int32_t));
    return a;
}
XS_NOINLINE xs_Int32Array *xs_i32_fill(xs_Int32Array *a, double v, int64_t start, int64_t end) {
    int64_t s = xs_u8_rel(start, a->f_length, 0), e = xs_u8_rel(end, a->f_length, a->f_length);
    int32_t x = xs_toint32_d(v);
    for (int64_t i = s; i < e; i++) a->data[i] = x;
    return xs_retain(a);
}
XS_NOINLINE void xs_i32_set_from(xs_Int32Array *d, xs_Int32Array *s, int64_t off) {
    xs_f64_set_check(s->f_length, off, d->f_length);
    if (s->f_length) memmove(d->data + off, s->data, (size_t)s->f_length * sizeof(int32_t)); /* (views on one block may overlap) */
}
XS_NOINLINE void xs_i32_set_ints(xs_Int32Array *d, xs_arr_int *s, int64_t off) {
    xs_f64_set_check((int64_t)s->len, off, d->f_length);
    for (size_t i = 0; i < s->len; i++) d->data[off + (int64_t)i] = xs_toint32_i(s->data[i]);
}
XS_NOINLINE void xs_i32_set_nums(xs_Int32Array *d, xs_arr_num *s, int64_t off) {
    xs_f64_set_check((int64_t)s->len, off, d->f_length);
    for (size_t i = 0; i < s->len; i++) d->data[off + (int64_t)i] = xs_toint32_d(s->data[i]);
}
XS_NOINLINE xs_Int32Array *xs_i32_subarray(xs_Int32Array *a, int64_t start, int64_t end) { /* a view on the same block */
    int64_t s = xs_u8_rel(start, a->f_length, 0), e = xs_u8_rel(end, a->f_length, a->f_length);
    xs_shblk_ref(a->blk);
    return xs_i32_wrap(a->blk, (a->data - (int32_t *)xs_shblk_data(a->blk)) + s, e > s ? e - s : 0, a->shared);
}
XS_NOINLINE xs_Int32Array *xs_i32_slice(xs_Int32Array *a, int64_t start, int64_t end) { /* a copy, of its own */
    int64_t s = xs_u8_rel(start, a->f_length, 0), e = xs_u8_rel(end, a->f_length, a->f_length), n = e > s ? e - s : 0;
    xs_Int32Array *r = xs_i32_wrap(xs_shblk_new(n * 4, false), 0, n, false);
    if (n) memcpy(r->data, a->data + s, (size_t)n * sizeof(int32_t));
    return r;
}
XS_NOINLINE xs_str *xs_i32_join(xs_Int32Array *a, xs_str *sep) {
    xs_sb sb = {0};
    char t[16];
    for (int64_t i = 0; i < a->f_length; i++) {
        if (i) xs_sb_put(&sb, sep->data, (size_t)sep->len);
        int k = snprintf(t, sizeof t, "%d", a->data[i]);
        xs_sb_put(&sb, t, (size_t)k);
    }
    xs_str *r = xs_str_new(sb.data ? sb.data : "", sb.len);
    free(sb.data);
    return r;
}
XS_NOINLINE xs_str *xs_i32_to_string(xs_Int32Array *a) {
    xs_str *c = xs_str_new(",", 1), *r = xs_i32_join(a, c);
    xs_release(c);
    return r;
}

/* ---- text ---- */
XS_NOINLINE xs_Uint8Array *xs_text_encode(xs_str *s) { /* new TextEncoder().encode(s): the UTF-8 bytes */
    xs_Uint8Array *b = xs_u8_raw(s->len, false, false);
    if (!xs_copy_par(b->data, (const unsigned char *)s->data, s->len)) memcpy(b->data, s->data, (size_t)s->len);
    return b;
}
/* UTF-8 bytes to a string; ill-formed sequences become U+FFFD, as TextDecoder and Buffer do */
static xs_str *xs_utf8_decode(const uint8_t *p, int64_t n) {
    int64_t i = 0;
    while (i < n && p[i] < 0x80) i++;
    if (i == n) return xs_str_new((const char *)p, (size_t)n); /* ASCII: as is */
    xs_sb sb = {0};
    xs_sb_put(&sb, (const char *)p, (size_t)i);
    while (i < n) {
        uint8_t c = p[i];
        if (c < 0x80) { int64_t j = i; while (j < n && p[j] < 0x80) j++; xs_sb_put(&sb, (const char *)p + i, (size_t)(j - i)); i = j; continue; }
        int k = c >= 0xF0 && c <= 0xF4 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 && c < 0xE0 ? 2 : 0;
        bool ok = k > 0 && i + k <= n;
        for (int j = 1; ok && j < k; j++) if ((p[i + j] & 0xC0) != 0x80) ok = false;
        if (ok && k == 3) { uint32_t cp = ((c & 0x0Fu) << 12) | ((p[i + 1] & 0x3Fu) << 6) | (p[i + 2] & 0x3Fu); if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) ok = false; }
        if (ok && k == 4) { uint32_t cp = ((c & 0x07u) << 18) | ((p[i + 1] & 0x3Fu) << 12) | ((p[i + 2] & 0x3Fu) << 6) | (p[i + 3] & 0x3Fu); if (cp < 0x10000 || cp > 0x10FFFF) ok = false; }
        if (ok) { xs_sb_put(&sb, (const char *)p + i, (size_t)k); i += k; continue; }
        xs_sb_put(&sb, "\xEF\xBF\xBD", 3);
        i++;
        while (i < n && (p[i] & 0xC0) == 0x80 && k > 1) { i++; k--; } /* the maximal subpart is one replacement */
    }
    xs_str *r = xs_str_new(sb.data, sb.len);
    free(sb.data);
    return r;
}
XS_NOINLINE xs_str *xs_text_decode(xs_Uint8Array *b) { return xs_utf8_decode(b->data, b->f_length); }
typedef struct xs_TextEncoder { xs_hdr hdr; } xs_TextEncoder;
static void xs_TextEncoder_print(void *p) { (void)p; fputs("TextEncoder { encoding: 'utf-8' }", XS_OUT); }
static void xs_TextDecoder_print(void *p) { (void)p; fputs("TextDecoder { encoding: 'utf-8' }", XS_OUT); }
static void xs_TextEncoder_free(void *p) { xs_obj_free(p); }
XS_NOINLINE xs_TextEncoder *xs_TextEncoder_new(void) { return xs_obj_alloc(sizeof(xs_TextEncoder), xs_TextEncoder_free, xs_TextEncoder_print); }
XS_NOINLINE xs_TextEncoder *xs_TextDecoder_new(void) { return xs_obj_alloc(sizeof(xs_TextEncoder), xs_TextEncoder_free, xs_TextDecoder_print); }
XS_NOINLINE xs_Uint8Array *xs_TextEncoder_encode(xs_TextEncoder *t, xs_str *s) { (void)t; return xs_text_encode(s); }
static xs_str *xs_TextDecoder_decode(xs_TextEncoder *t, xs_Uint8Array *b) { (void)t; return xs_text_decode(b); }

/* ---- base64 / hex ---- */
static const char xs_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char xs_b64url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
/* arm64: base64 and hex 16 bytes at a time (NEON table lookups); the scalar code does the rest and every
   error, padding and whitespace case. Other targets use the scalar code only. */
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#define XS_NEON 1
/* hex characters to nibbles, 16 pairs at a time; stops before the first block with a non-hex character */
static int64_t xs_hex_dec_neon(const char *s, int64_t pairs, uint8_t *out) {
    int64_t k = 0;
    const uint8x16_t c0 = vdupq_n_u8('0'), ca = vdupq_n_u8('a'), lower = vdupq_n_u8(0x20), ten = vdupq_n_u8(10), six = vdupq_n_u8(6);
    for (; k + 16 <= pairs; k += 16) {
        uint8x16x2_t c = vld2q_u8((const uint8_t *)s + 2 * k);
        uint8x16_t n[2], bad = vdupq_n_u8(0);
        for (int h = 0; h < 2; h++) {
            uint8x16_t d = vsubq_u8(c.val[h], c0), l = vsubq_u8(vorrq_u8(c.val[h], lower), ca);
            uint8x16_t isd = vcltq_u8(d, ten), isl = vcltq_u8(l, six);
            n[h] = vbslq_u8(isd, d, vaddq_u8(l, ten));
            bad = vorrq_u8(bad, vmvnq_u8(vorrq_u8(isd, isl)));
        }
        if (vmaxvq_u8(bad)) break;
        vst1q_u8(out + k, vorrq_u8(vshlq_n_u8(n[0], 4), n[1]));
    }
    return k;
}
#endif
/* x86-64: the base ISA has no byte shuffle, so SSSE3 is checked once at run time (cpuid) and these functions
   are compiled for it alone (Muła's and aklomp/base64's algorithms): 12 bytes -> 16 characters, 16 characters
   -> 12 bytes while every character is in the standard alphabet. Everything else stays scalar. */
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#include <tmmintrin.h>
#define XS_X86_B64 1
static int xs_ssse3_ = -1;
static bool xs_has_ssse3(void) {
    if (xs_ssse3_ < 0) { unsigned a, b, c, d; xs_ssse3_ = __get_cpuid(1, &a, &b, &c, &d) && (c & bit_SSSE3) ? 1 : 0; }
    return xs_ssse3_ == 1;
}
/* encodes 12 bytes per round from in (reading 16, so while 16 remain); returns the bytes consumed */
__attribute__((target("ssse3"))) static int64_t xs_b64_enc_ssse3(const uint8_t *in, int64_t n, char *o, bool url) {
    const __m128i shuf = _mm_set_epi8(10, 11, 9, 10, 7, 8, 6, 7, 4, 5, 3, 4, 1, 2, 0, 1);
    const __m128i lut = url ? _mm_setr_epi8(65, 71, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -17, 32, 0, 0)
                            : _mm_setr_epi8(65, 71, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -19, -16, 0, 0);
    int64_t i = 0;
    for (; i + 16 <= n; i += 12, o += 16) {
        __m128i v = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(in + i)), shuf);
        __m128i t0 = _mm_mulhi_epu16(_mm_and_si128(v, _mm_set1_epi32(0x0fc0fc00)), _mm_set1_epi32(0x04000040));
        __m128i t1 = _mm_mullo_epi16(_mm_and_si128(v, _mm_set1_epi32(0x003f03f0)), _mm_set1_epi32(0x01000010));
        __m128i idx = _mm_or_si128(t0, t1); /* sixteen 6-bit values */
        __m128i r = _mm_subs_epu8(idx, _mm_set1_epi8(51));
        r = _mm_sub_epi8(r, _mm_cmpgt_epi8(idx, _mm_set1_epi8(25)));
        _mm_storeu_si128((__m128i *)o, _mm_add_epi8(idx, _mm_shuffle_epi8(lut, r)));
    }
    return i;
}
/* decodes 16 characters per round while all are in the standard alphabet and 16 more follow (the store writes
   16 bytes for 12); returns the characters consumed */
__attribute__((target("ssse3"))) static int64_t xs_b64_dec_ssse3(const char *s, int64_t n, uint8_t *out) {
    const __m128i lut_lo = _mm_setr_epi8(0x15, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x13, 0x1A, 0x1B, 0x1B, 0x1B, 0x1A);
    const __m128i lut_hi = _mm_setr_epi8(0x10, 0x10, 0x01, 0x02, 0x04, 0x08, 0x04, 0x08, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10);
    const __m128i lut_roll = _mm_setr_epi8(0, 16, 19, 4, -65, -65, -71, -71, 0, 0, 0, 0, 0, 0, 0, 0);
    const __m128i m2f = _mm_set1_epi8(0x2f), zero = _mm_setzero_si128();
    const __m128i pack = _mm_setr_epi8(2, 1, 0, 6, 5, 4, 10, 9, 8, 14, 13, 12, -1, -1, -1, -1);
    int64_t i = 0, o = 0;
    for (; i + 32 <= n; i += 16, o += 12) {
        __m128i in = _mm_loadu_si128((const __m128i *)(s + i));
        __m128i hn = _mm_and_si128(_mm_srli_epi32(in, 4), m2f), ln = _mm_and_si128(in, m2f);
        __m128i lo = _mm_shuffle_epi8(lut_lo, ln), hi = _mm_shuffle_epi8(lut_hi, hn);
        if (_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_and_si128(lo, hi), zero)) != 0xFFFF) break; /* not all in the alphabet */
        __m128i v = _mm_add_epi8(in, _mm_shuffle_epi8(lut_roll, _mm_add_epi8(_mm_cmpeq_epi8(in, m2f), hn)));
        __m128i ab = _mm_maddubs_epi16(v, _mm_set1_epi32(0x01400140));
        __m128i abc = _mm_madd_epi16(ab, _mm_set1_epi32(0x00011000));
        _mm_storeu_si128((__m128i *)(out + o), _mm_shuffle_epi8(abc, pack));
    }
    return i;
}
#endif
static xs_str *xs_b64_enc_(const uint8_t *p, int64_t n, bool url, bool pad) {
    const char *t = url ? xs_b64url : xs_b64;
    int64_t full = n / 3, rem = n % 3;
    int64_t out = full * 4 + (rem ? (pad ? 4 : rem + 1) : 0);
    xs_str *s = xs_str_alloc(out);
    char *o = s->data;
    /* 12 bits -> two characters per lookup */
    static uint16_t pairs_std[4096], pairs_url[4096];
    uint16_t *pt = url ? pairs_url : pairs_std;
    if (!pt[1]) for (int k = 0; k < 4096; k++) { char c2[2] = {t[k >> 6], t[k & 63]}; memcpy(&pt[k], c2, 2); }
#ifdef XS_X86_B64
    if (full >= 6 && xs_has_ssse3()) { /* whole groups of 3 only: the remainder stays for the code below */
        int64_t used = xs_b64_enc_ssse3(p, full * 3, o, url);
        p += used; o += used / 3 * 4; full -= used / 3;
    }
#endif
#ifdef XS_NEON
    { /* 48 bytes -> 64 characters per round */
        const uint8x16x4_t lut = vld1q_u8_x4((const uint8_t *)t);
        const uint8x16_t m63 = vdupq_n_u8(63);
        for (; full >= 16; full -= 16, p += 48, o += 64) {
            uint8x16x3_t in = vld3q_u8(p);
            uint8x16x4_t r;
            r.val[0] = vqtbl4q_u8(lut, vshrq_n_u8(in.val[0], 2));
            r.val[1] = vqtbl4q_u8(lut, vandq_u8(vorrq_u8(vshlq_n_u8(in.val[0], 4), vshrq_n_u8(in.val[1], 4)), m63));
            r.val[2] = vqtbl4q_u8(lut, vandq_u8(vorrq_u8(vshlq_n_u8(in.val[1], 2), vshrq_n_u8(in.val[2], 6)), m63));
            r.val[3] = vqtbl4q_u8(lut, vandq_u8(in.val[2], m63));
            vst4q_u8((uint8_t *)o, r);
        }
    }
#endif
    for (int64_t i = 0; i < full; i++, p += 3) {
        uint32_t v = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        uint32_t w = (uint32_t)pt[v >> 12] | (uint32_t)pt[v & 0xFFF] << 16;
        memcpy(o, &w, 4);
        o += 4;
    }
    if (rem) {
        uint32_t v = (uint32_t)p[0] << 16 | (rem == 2 ? (uint32_t)p[1] << 8 : 0);
        *o++ = t[v >> 18];
        *o++ = t[(v >> 12) & 63];
        if (rem == 2) *o++ = t[(v >> 6) & 63];
        if (pad) { if (rem == 1) *o++ = '='; *o++ = '='; }
    }
    *o = 0;
    return s;
}
/* base64 or base64url, with or without padding, ASCII whitespace skipped (as Node's Buffer.from does);
   strict: any other character is an error (Uint8Array.fromBase64); else it is skipped and only = stops (Buffer) */
static int64_t xs_b64_dec_(const char *s, int64_t n, uint8_t *out, bool strict) {
    static int8_t rev[256];
    static bool init;
    if (!init) {
        memset(rev, -1, sizeof rev);
        for (int i = 0; i < 64; i++) rev[(unsigned char)xs_b64[i]] = (int8_t)i;
        rev['-'] = 62; rev['_'] = 63;
        init = true;
    }
    uint32_t acc = 0;
    int bits = 0;
    int64_t o = 0;
    int64_t i = 0;
    /* fast path: four valid characters make three bytes, two lookups of a character pair (12 bits each);
       whitespace, '=' and errors go the slow way below */
    static int16_t *pair;
    if (!pair) {
        int16_t *m = malloc(sizeof(int16_t) * 65536);
        for (int a = 0; a < 256; a++)
            for (int b = 0; b < 256; b++) {
                unsigned char k[2] = {(unsigned char)a, (unsigned char)b};
                uint16_t key;
                memcpy(&key, k, 2);
                m[key] = (int16_t)(rev[a] < 0 || rev[b] < 0 ? -1 : rev[a] << 6 | rev[b]);
            }
        pair = m;
    }
#ifdef XS_X86_B64
    if (n >= 32 && xs_has_ssse3()) { int64_t used = xs_b64_dec_ssse3(s, n, out); i += used; o += used / 4 * 3; }
#endif
#ifdef XS_NEON
    { /* 64 characters -> 48 bytes per round, while every character is in the alphabet */
        static uint8_t tab[128];
        static bool tab_init;
        if (!tab_init) { for (int c = 0; c < 128; c++) tab[c] = rev[c] < 0 ? 0xFF : (uint8_t)rev[c]; tab_init = true; }
        const uint8x16x4_t t0 = vld1q_u8_x4(tab), t1 = vld1q_u8_x4(tab + 64);
        const uint8x16_t k64 = vdupq_n_u8(64), kff = vdupq_n_u8(0xFF), k128 = vdupq_n_u8(128);
        for (; i + 64 <= n; i += 64, o += 48) {
            uint8x16x4_t in = vld4q_u8((const uint8_t *)s + i);
            uint8x16_t v[4], bad = vdupq_n_u8(0);
            for (int k = 0; k < 4; k++) {
                uint8x16_t c = in.val[k];
                v[k] = vorrq_u8(vqtbl4q_u8(t0, c), vqtbl4q_u8(t1, vsubq_u8(c, k64))); /* each table answers 0 off its range */
                bad = vorrq_u8(bad, vorrq_u8(vceqq_u8(v[k], kff), vcgeq_u8(c, k128)));
            }
            if (vmaxvq_u8(bad)) break;
            uint8x16x3_t r;
            r.val[0] = vorrq_u8(vshlq_n_u8(v[0], 2), vshrq_n_u8(v[1], 4));
            r.val[1] = vorrq_u8(vshlq_n_u8(v[1], 4), vshrq_n_u8(v[2], 2));
            r.val[2] = vorrq_u8(vshlq_n_u8(v[2], 6), v[3]);
            vst3q_u8(out + o, r);
        }
    }
#endif
    for (; i + 4 <= n; i += 4) {
        uint16_t k0, k1;
        memcpy(&k0, s + i, 2);
        memcpy(&k1, s + i + 2, 2);
        int hi = pair[k0], lo = pair[k1];
        if ((hi | lo) < 0) break;
        uint32_t v = (uint32_t)hi << 12 | (uint32_t)lo;
        out[o] = (uint8_t)(v >> 16);
        out[o + 1] = (uint8_t)(v >> 8);
        out[o + 2] = (uint8_t)v;
        o += 3;
    }
    for (; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') continue;
        if (c == '=') break;
        int v = rev[c];
        if (v < 0) { if (strict) return -1; continue; } /* Buffer skips what is not base64, as Node does */
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (uint8_t)(acc >> bits); }
    }
    return o;
}
static int8_t xs_hex_rev[256];
static void xs_hex_init(void) {
    if (xs_hex_rev['0'] == 0 && xs_hex_rev['1'] == 1) return;
    memset(xs_hex_rev, -1, sizeof xs_hex_rev);
    for (int i = 0; i < 10; i++) xs_hex_rev['0' + i] = (int8_t)i;
    for (int i = 0; i < 6; i++) { xs_hex_rev['a' + i] = (int8_t)(10 + i); xs_hex_rev['A' + i] = (int8_t)(10 + i); }
}
static inline int xs_hexv(int c) { return xs_hex_rev[(unsigned char)c]; }
static xs_str *xs_hex_encode(const uint8_t *p, int64_t n) {
    static uint16_t pairs[256];
    if (!pairs[1]) for (int i = 0; i < 256; i++) { char t[2] = {"0123456789abcdef"[i >> 4], "0123456789abcdef"[i & 15]}; memcpy(&pairs[i], t, 2); }
    xs_str *s = xs_str_alloc(n * 2);
    char *o = s->data;
    int64_t i = 0;
#ifdef XS_NEON
    { /* 16 bytes -> 32 characters per round */
        const uint8x16_t lut = vld1q_u8((const uint8_t *)"0123456789abcdef"), m15 = vdupq_n_u8(15);
        for (; i + 16 <= n; i += 16, o += 32) {
            uint8x16_t v = vld1q_u8(p + i);
            uint8x16x2_t r = {{vqtbl1q_u8(lut, vshrq_n_u8(v, 4)), vqtbl1q_u8(lut, vandq_u8(v, m15))}};
            vst2q_u8((uint8_t *)o, r);
        }
    }
#endif
    for (; i + 4 <= n; i += 4, o += 8) { /* four bytes, one 8-byte store */
        uint64_t w = (uint64_t)pairs[p[i]] | (uint64_t)pairs[p[i + 1]] << 16 | (uint64_t)pairs[p[i + 2]] << 32 | (uint64_t)pairs[p[i + 3]] << 48;
        memcpy(o, &w, 8);
    }
    for (; i < n; i++, o += 2) memcpy(o, &pairs[p[i]], 2);
    *o = 0;
    return s;
}
/* two hex characters at once: 64K entries, -1 when either is not hex */
static int16_t *xs_hex_pair_tab(void) {
    static int16_t *t;
    if (t) return t;
    xs_hex_init();
    int16_t *m = malloc(sizeof(int16_t) * 65536);
    for (int a = 0; a < 256; a++)
        for (int b = 0; b < 256; b++) {
            int h = xs_hex_rev[a], l = xs_hex_rev[b];
            unsigned char k[2] = {(unsigned char)a, (unsigned char)b};
            uint16_t key;
            memcpy(&key, k, 2);
            m[key] = (int16_t)(h < 0 || l < 0 ? -1 : h << 4 | l);
        }
    t = m;
    return t;
}
XS_NOINLINE xs_str *xs_u8_to_base64(xs_Uint8Array *b) { return xs_b64_enc_(b->data, b->f_length, false, true); }
XS_NOINLINE xs_str *xs_u8_to_hex(xs_Uint8Array *b) { return xs_hex_encode(b->data, b->f_length); }
XS_NOINLINE xs_Uint8Array *xs_u8_from_base64(xs_str *s) { /* Uint8Array.fromBase64: strict */
    xs_Uint8Array *b = xs_u8_raw(s->len / 4 * 3 + 3, false, false);
    int64_t n = xs_b64_dec_(s->data, s->len, b->data, true);
    if (n < 0) { xs_release(b); xs_panic("SyntaxError: The input is not a valid base64 string"); }
    b->f_length = n;
    return b;
}
XS_NOINLINE xs_Uint8Array *xs_u8_from_hex(xs_str *s) {
    xs_hex_init();
    if (s->len % 2) xs_panic("SyntaxError: Input string must contain hex characters in even length");
    xs_Uint8Array *b = xs_u8_raw(s->len / 2, false, false);
    int64_t i0 = 0;
#ifdef XS_NEON
    i0 = xs_hex_dec_neon(s->data, s->len / 2, b->data);
#endif
    for (int64_t i = i0; i < s->len / 2; i++) {
        int h = xs_hexv((unsigned char)s->data[2 * i]), l = xs_hexv((unsigned char)s->data[2 * i + 1]);
        if (h < 0 || l < 0) { xs_release(b); xs_panic("SyntaxError: Input string must contain hex characters in even length"); }
        b->data[i] = (uint8_t)(h << 4 | l);
    }
    return b;
}

/* ---- Buffer (node:buffer, a global) ---- */
XS_NOINLINE int xs_buf_enc(xs_str *e) { /* 0 utf8, 1 base64, 2 base64url, 3 hex, 4 latin1/binary, 5 ascii */
    if (!e) return 0;
    char t[16];
    size_t n = (size_t)e->len < sizeof t - 1 ? (size_t)e->len : sizeof t - 1;
    for (size_t i = 0; i < n; i++) t[i] = (char)tolower((unsigned char)e->data[i]);
    t[n] = 0;
    if (!strcmp(t, "utf8") || !strcmp(t, "utf-8")) return 0;
    if (!strcmp(t, "base64")) return 1;
    if (!strcmp(t, "base64url")) return 2;
    if (!strcmp(t, "hex")) return 3;
    if (!strcmp(t, "latin1") || !strcmp(t, "binary")) return 4;
    if (!strcmp(t, "ascii")) return 5;
    if (!strcmp(t, "xisto:raw")) return 6; /* the bytes as they are, in a string (packages: a Buffer body over http) */
    xs_panicf("TypeError: Unknown encoding: %s", e->data);
    return 0;
}
XS_NOINLINE xs_Uint8Array *xs_buf_from_str(xs_str *s, xs_str *enc) {
    int k = xs_buf_enc(enc);
    if (k == 0) { xs_Uint8Array *b = xs_u8_raw(s->len, true, false); memcpy(b->data, s->data, (size_t)s->len); return b; }
    if (k == 1 || k == 2) {
        xs_Uint8Array *b = xs_u8_raw(s->len / 4 * 3 + 3, true, false);
        b->f_length = xs_b64_dec_(s->data, s->len, b->data, false);
        return b;
    }
    if (k == 3) { /* stops at the first pair that is not hex, as Node does */
        const int16_t *pt = xs_hex_pair_tab();
        xs_Uint8Array *b = xs_u8_raw(s->len / 2, true, false);
        int64_t i = 0;
#ifdef XS_NEON
        i = xs_hex_dec_neon(s->data, s->len / 2, b->data);
#endif
        for (; i < s->len / 2; i++) {
            uint16_t key;
            memcpy(&key, s->data + 2 * i, 2);
            int v = pt[key];
            if (v < 0) break;
            b->data[i] = (uint8_t)v;
        }
        b->f_length = i;
        return b;
    }
    /* latin1 / ascii: one byte per code point (the low 8 bits) */
    xs_Uint8Array *b = xs_u8_raw(xs_str_len(s), true, false);
    int64_t o = 0;
    for (int64_t i = 0; i < s->len;) {
        size_t l;
        uint32_t c = xs_utf8_cp((const unsigned char *)s->data + i, (size_t)(s->len - i), &l);
        b->data[o++] = (uint8_t)c;
        i += (int64_t)l;
    }
    b->f_length = o;
    return b;
}
XS_NOINLINE xs_Uint8Array *xs_buf_from_bytes(xs_Uint8Array *s) { return xs_u8_copy_kind(s, true); }
XS_NOINLINE xs_Uint8Array *xs_buf_from_ints(xs_arr_int *a) {
    xs_Uint8Array *b = xs_u8_raw((int64_t)a->len, true, false);
    for (size_t i = 0; i < a->len; i++) b->data[i] = (uint8_t)a->data[i];
    return b;
}
XS_NOINLINE xs_Uint8Array *xs_buf_alloc(int64_t n, int64_t fill) {
    xs_Uint8Array *b = xs_u8_raw(n, true, fill == 0);
    if (fill) memset(b->data, (int)(uint8_t)fill, (size_t)n);
    return b;
}
XS_NOINLINE xs_Uint8Array *xs_buf_concat(xs_arr_ref *list) {
    int64_t total = 0;
    for (size_t i = 0; i < list->len; i++) total += ((xs_Uint8Array *)list->data[i])->f_length;
    xs_Uint8Array *b = xs_u8_raw(total, true, false);
    int64_t o = 0;
    for (size_t i = 0; i < list->len; i++) {
        xs_Uint8Array *p = list->data[i];
        memcpy(b->data + o, p->data, (size_t)p->f_length);
        o += p->f_length;
    }
    return b;
}
static int64_t xs_buf_byte_length(xs_str *s) { return s->len; }
/* buf.toString(encoding = "utf8", start = 0, end = length) */
XS_NOINLINE xs_str *xs_buf_to_string(xs_Uint8Array *b, xs_str *enc, int64_t start, int64_t end) {
    int64_t s = start < 0 ? 0 : start > b->f_length ? b->f_length : start;
    int64_t e = end == INT64_MIN || end > b->f_length ? b->f_length : end < s ? s : end;
    const uint8_t *p = b->data + s;
    int64_t n = e - s;
    switch (xs_buf_enc(enc)) {
    case 0: return xs_utf8_decode(p, n);
    case 1: return xs_b64_enc_(p, n, false, true);
    case 2: return xs_b64_enc_(p, n, true, false);
    case 3: return xs_hex_encode(p, n);
    case 6: return xs_str_new((const char *)p, (size_t)n);
    default: { /* latin1 / ascii: each byte a code point (ascii drops the high bit) */
        bool ascii = xs_buf_enc(enc) == 5;
        xs_sb sb = {0};
        for (int64_t i = 0; i < n; i++) {
            uint8_t c = ascii ? (p[i] & 0x7F) : p[i];
            if (c < 0x80) { char ch = (char)c; xs_sb_put(&sb, &ch, 1); }
            else { char t[2] = {(char)(0xC0 | (c >> 6)), (char)(0x80 | (c & 0x3F))}; xs_sb_put(&sb, t, 2); }
        }
        xs_str *r = xs_str_new(sb.data ? sb.data : "", sb.len);
        free(sb.data);
        return r;
    }
    }
}
/* the Uint8Array printed by toString: "1,2,3" */
XS_NOINLINE xs_str *xs_u8_to_string(xs_Uint8Array *b) {
    if (xs_is_buffer(b)) return xs_buf_to_string(b, NULL, 0, INT64_MIN);
    xs_str *comma = xs_str_new(",", 1);
    xs_str *r = xs_u8_join(b, comma);
    xs_release(comma);
    return r;
}
/* btoa / atob: strings of code points 0..255 <-> base64 */
XS_NOINLINE xs_str *xs_btoa(xs_str *s) {
    xs_Uint8Array *b = xs_buf_from_str(s, NULL);
    int64_t o = 0;
    for (int64_t i = 0; i < s->len;) {
        size_t l;
        uint32_t c = xs_utf8_cp((const unsigned char *)s->data + i, (size_t)(s->len - i), &l);
        if (c > 255) { xs_release(b); xs_panic("InvalidCharacterError: Invalid character"); }
        b->data[o++] = (uint8_t)c;
        i += (int64_t)l;
    }
    xs_str *r = xs_b64_enc_(b->data, o, false, true);
    xs_release(b);
    return r;
}
XS_NOINLINE xs_str *xs_atob(xs_str *s) {
    uint8_t *tmp = malloc((size_t)(s->len / 4 * 3 + 4));
    int64_t n = xs_b64_dec_(s->data, s->len, tmp, true);
    if (n < 0) { free(tmp); xs_panic("InvalidCharacterError: The string to be decoded is not correctly encoded."); }
    xs_sb sb = {0};
    for (int64_t i = 0; i < n; i++) {
        uint8_t c = tmp[i];
        if (c < 0x80) { char ch = (char)c; xs_sb_put(&sb, &ch, 1); }
        else { char t[2] = {(char)(0xC0 | (c >> 6)), (char)(0x80 | (c & 0x3F))}; xs_sb_put(&sb, t, 2); }
    }
    free(tmp);
    xs_str *r = xs_str_new(sb.data ? sb.data : "", sb.len);
    free(sb.data);
    return r;
}
XS_NOINLINE xs_Uint8Array *xs_fs_read_stdin_bytes(void) {
    xs_stdin_binary();
    size_t n;
    char *buf = xs_read_stream(stdin, 0, &n);
    xs_Uint8Array *b = xs_u8_raw(0, true, false);
    free(b->data);
    b->data = (uint8_t *)buf;
    b->f_length = (int64_t)n;
    return b;
}
/* readFileSync(path) without an encoding: the bytes (a Buffer); writeFileSync(path, bytes) */
XS_NOINLINE xs_Uint8Array *xs_fs_read_bytes(xs_str *path) {
    FILE *f = fopen(path->data, "rb");
    if (!f) xs_panicf("ENOENT: no such file or directory, open '%s'", path->data);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { /* not a regular file: read until the end */
        size_t m;
        char *buf = xs_read_stream(f, 0, &m);
        fclose(f);
        xs_Uint8Array *b = xs_u8_raw(0, true, false);
        free(b->data);
        b->data = (uint8_t *)buf;
        b->f_length = (int64_t)m;
        return b;
    }
    fseek(f, 0, SEEK_SET);
    xs_Uint8Array *b = xs_u8_raw(n > 0 ? n : 0, true, false);
    if (n > 0 && fread(b->data, 1, (size_t)n, f) != (size_t)n) { fclose(f); xs_panicf("EIO: cannot read '%s'", path->data); }
    fclose(f);
    return b;
}
XS_NOINLINE void xs_fs_write_bytes(xs_str *path, xs_Uint8Array *b) {
    FILE *f = fopen(path->data, "wb");
    if (!f) xs_panicf("EACCES: cannot open '%s' for writing", path->data);
    fwrite(b->data, 1, (size_t)b->f_length, f);
    fclose(f);
}

/* node:fs on file descriptors (checker: check_fs_fd_call): openSync, closeSync, fstatSync, readSync */
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct xs_Stats { xs_hdr hdr; int64_t f_size; int64_t mode; } xs_Stats;
static void xs_Stats_print(void *p) { fprintf(XS_OUT, "Stats { size: %lld }", (long long)((xs_Stats *)p)->f_size); }
static void xs_Stats_free(void *p) { xs_obj_free(p); }
XS_NOINLINE xs_Stats *xs_fs_fstat(int64_t fd) {
    struct stat st;
    if (fstat((int)fd, &st) != 0) { char m_[96]; snprintf(m_, sizeof m_, "EBADF: bad file descriptor, fstat (%lld)", (long long)fd); xs_panic(m_); }
    xs_Stats *s = xs_obj_alloc(sizeof *s, xs_Stats_free, xs_Stats_print);
    s->f_size = (int64_t)st.st_size;
    s->mode = (int64_t)st.st_mode;
    return s;
}
static bool xs_Stats_isFile(xs_Stats *s) { return S_ISREG((mode_t)s->mode); }
static bool xs_Stats_isDirectory(xs_Stats *s) { return S_ISDIR((mode_t)s->mode); }
XS_NOINLINE int64_t xs_fs_open(xs_str *path, xs_str *flags) {
    int fl = strcmp(flags->data, "w") == 0 ? O_WRONLY | O_CREAT | O_TRUNC : strcmp(flags->data, "a") == 0 ? O_WRONLY | O_CREAT | O_APPEND : strcmp(flags->data, "r+") == 0 ? O_RDWR : O_RDONLY;
    int fd = open(path->data, fl, 0666);
    if (fd < 0) xs_panicf("ENOENT: no such file or directory, open '%s'", path->data);
    return fd;
}
XS_NOINLINE void xs_fs_close(int64_t fd) {
    if (close((int)fd) != 0) { char m_[96]; snprintf(m_, sizeof m_, "EBADF: bad file descriptor, close (%lld)", (long long)fd); xs_panic(m_); }
}
/* readSync(fd, buffer, offset, length, position): bytes read (0 at the end), straight into the buffer's bytes;
   length -1: to the buffer's end; position -1: where the descriptor is (and it moves), else that position (it
   does not move), like Node */
XS_NOINLINE int64_t xs_fs_read_sync(int64_t fd, xs_Uint8Array *b, int64_t off, int64_t len, int64_t pos) {
    char m_[160];
    if (off < 0 || off > b->f_length) { snprintf(m_, sizeof m_, "RangeError: The value of \"offset\" is out of range. It must be >= 0 && <= %lld. Received %lld", (long long)b->f_length, (long long)off); xs_panic(m_); }
    if (len < 0) len = b->f_length - off;
    if (len > b->f_length - off) { snprintf(m_, sizeof m_, "RangeError: The value of \"length\" is out of range. It must be >= 0 && <= %lld. Received %lld", (long long)(b->f_length - off), (long long)len); xs_panic(m_); }
    if (len == 0) return 0;
    if (fd == 0) xs_stdin_binary();
    xs_lent_wait(b->data + off, (size_t)len);
    ssize_t r;
#ifdef _WIN32
    if (pos >= 0) { off_t cur = lseek((int)fd, 0, SEEK_CUR); lseek((int)fd, (off_t)pos, SEEK_SET); r = read((int)fd, b->data + off, (size_t)len); lseek((int)fd, cur, SEEK_SET); }
    else r = read((int)fd, b->data + off, (size_t)len);
#else
    r = pos >= 0 ? pread((int)fd, b->data + off, (size_t)len, (off_t)pos) : read((int)fd, b->data + off, (size_t)len);
#endif
    if (r < 0) { snprintf(m_, sizeof m_, "EIO: read failed on file descriptor %lld", (long long)fd); xs_panic(m_); }
#ifndef _WIN32
    if (fd == 0 && pos < 0) { /* (XB-091: is there input left to read?) */
        struct stat st;
        if (r == 0) xs_stdin_reading = 0;
        else if (fstat(0, &st) == 0 && S_ISREG(st.st_mode)) xs_stdin_reading = lseek(0, 0, SEEK_CUR) < st.st_size;
        else xs_stdin_reading = 1;
    }
#endif
    return (int64_t)r;
}

/* execSync from node:child_process: runs the command through /bin/sh and returns its standard output */
XS_NOINLINE xs_str *xs_exec_sync(xs_str *cmd) {
    fflush(stdout);
    FILE *p = popen(cmd->data, "r");
    if (!p) xs_panicf("Command failed: %s", cmd->data);
    xs_sb b = {0};
    char tmp[4096];
    size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, p)) > 0) xs_sb_put(&b, tmp, n);
    int st = pclose(p);
    if (st != 0) { free(b.data); xs_panicf("Command failed: %s", cmd->data); }
    return xs_sb_finish(&b);
}
XS_NOINLINE void xs_fs_unlink(xs_str *path) { if (unlink(path->data) != 0) xs_panicf("ENOENT: no such file or directory, unlink '%s'", path->data); }

/* BigInt: arbitrary-precision integers, immutable values as in JavaScript.
   Sign and magnitude, 64-bit limbs (least significant first) in the object itself; products and the
   division steps through unsigned __int128. Division is Knuth's algorithm D (TAOCP vol. 2, 4.3.1).
   (here, after the strings and the allocator they use) */

typedef struct xs_big { xs_hdr hdr; int32_t neg; int32_t n; uint64_t d[]; } xs_big;
typedef unsigned __int128 xs_u128;

static void xs_big_free(void *p) { xs_obj_free(p); }
static void xs_big_print(void *p);
static const xs_type xs_big_type XS_TYPE_SEC = {xs_big_free, xs_big_print};

XS_NOINLINE xs_big *xs_big_alloc(int64_t n) { /* n limbs, not initialised; at least one */
    if (n > ((int64_t)1 << 30)) xs_panic("RangeError: Maximum BigInt size exceeded");
    xs_big *b = xs_obj_alloc_raw(sizeof(xs_big) + 8 * (size_t)(n ? n : 1), &xs_big_type);
    b->neg = 0;
    b->n = (int32_t)n;
    return b;
}
static xs_big *xs_big_norm(xs_big *b) { /* no leading zero limbs; zero is never negative */
    while (b->n > 0 && b->d[b->n - 1] == 0) b->n--;
    if (b->n == 0) b->neg = 0;
    return b;
}

XS_NOINLINE xs_big *xs_big_from_int(int64_t v) {
    xs_big *b = xs_big_alloc(1);
    b->neg = v < 0;
    b->d[0] = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    return xs_big_norm(b);
}
XS_NOINLINE xs_big *xs_big_from_num(double v) {
    if (!isfinite(v) || trunc(v) != v) {
        xs_panicf("RangeError: The number %s cannot be converted to a BigInt because it is not an integer", xs_str_from_num(v)->data);
    }
    int e;
    double m = frexp(fabs(v), &e); /* |v| = m * 2^e, 0.5 <= m < 1 */
    if (e <= 63) return xs_big_from_int((int64_t)v);
    uint64_t top = (uint64_t)ldexp(m, 64); /* the 53 significant bits, at the top of a limb */
    int shift = e - 64, limbs = shift / 64 + 2, bits = shift % 64;
    xs_big *b = xs_big_alloc(limbs);
    memset(b->d, 0, 8 * (size_t)limbs);
    b->d[shift / 64] = top << bits;
    if (bits) b->d[shift / 64 + 1] = top >> (64 - bits);
    b->neg = v < 0;
    return xs_big_norm(b);
}

/* |a| <=> |b| */
static int xs_big_mag_cmp(const xs_big *a, const xs_big *b) {
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int32_t i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}
static int xs_big_cmp(const xs_big *a, const xs_big *b) {
    if (a->neg != b->neg) return a->neg ? -1 : 1;
    int c = xs_big_mag_cmp(a, b);
    return a->neg ? -c : c;
}
static bool xs_big_lt(xs_big *a, xs_big *b) { return xs_big_cmp(a, b) < 0; }
static bool xs_big_le(xs_big *a, xs_big *b) { return xs_big_cmp(a, b) <= 0; }
static bool xs_big_gt(xs_big *a, xs_big *b) { return xs_big_cmp(a, b) > 0; }
static bool xs_big_ge(xs_big *a, xs_big *b) { return xs_big_cmp(a, b) >= 0; }
static bool xs_big_eq(xs_big *a, xs_big *b) { return xs_big_cmp(a, b) == 0; }
static bool xs_big_ne(xs_big *a, xs_big *b) { return xs_big_cmp(a, b) != 0; }

/* Limb-vector primitives (as GMP's mpn layer): r may be the same array as x (in place), never partly overlapping.
   The carry chains go through __builtin_addcll / __builtin_subcll where the compiler has them: clang turns a
   run of them into one adcs / adc chain, about twice as fast as carries recomputed from comparisons (pidigits). */
#if defined(__has_builtin) && !defined(XB_NO_ADDC) /* (XB_NO_ADDC, XB_NO_ASM: to test the plain C paths) */
#if __has_builtin(__builtin_addcll) && __has_builtin(__builtin_subcll)
#define XB_HAVE_ADDC 1
#endif
#endif
/* aarch64 (Apple silicon, Graviton): add_n, sub_n, mul_1, addmul_1 and submul_1 in assembly. In add_n and
   sub_n the carry stays in the flags across the whole loop (C can't say that: the loop's own compare clobbers
   it, so every 4 limbs clang saved and restored it): about 0.28 ns per limb against 0.45 in C on an M4. */
#if defined(__aarch64__) && (defined(__clang__) || defined(__GNUC__)) && !defined(XB_NO_ASM)
#define XB_ASM_A64 1
#else
#define XB_ASM_A64 0
#endif
/* x86-64: add_n and sub_n, the carry kept in CF across the loop (lea, dec and jrcxz leave it alone); mul_1
   stays in C there (mul clobbers the flags, so the chain is add/adc per limb as the C compiles anyway) */
#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__)) && !defined(XB_NO_ASM)
#define XB_ASM_X64 1
#else
#define XB_ASM_X64 0
#endif
typedef unsigned long long xb_c; /* the carry type of __builtin_addcll */
static inline __attribute__((always_inline)) uint64_t xb_addc(uint64_t a, uint64_t b, xb_c ci, xb_c *co) {
#ifdef XB_HAVE_ADDC
    return __builtin_addcll(a, b, ci, co);
#else
    xs_u128 s = (xs_u128)a + b + ci;
    *co = (xb_c)(s >> 64);
    return (uint64_t)s;
#endif
}
static inline __attribute__((always_inline)) uint64_t xb_subc(uint64_t a, uint64_t b, xb_c ci, xb_c *co) {
#ifdef XB_HAVE_ADDC
    return __builtin_subcll(a, b, ci, co);
#else
    uint64_t t = a - b, r = t - ci;
    *co = (a < b) | (t < ci);
    return r;
#endif
}
/* r = x + y (n limbs each); returns the carry out */
static uint64_t xb_add_n(uint64_t *r, const uint64_t *x, const uint64_t *y, int64_t n) {
#if XB_ASM_A64
    uint64_t a0, a1, a2, a3, b0, b1, b2, b3, c;
    __asm__("cmn xzr, xzr\n\t"                 /* carry clear */
            "tbz %[n], #0, 1f\n\t"
            "ldr %[a0], [%[x]], #8\n\t"
            "ldr %[b0], [%[y]], #8\n\t"
            "adcs %[a0], %[a0], %[b0]\n\t"
            "str %[a0], [%[r]], #8\n"
            "1:\n\t"
            "tbz %[n], #1, 2f\n\t"
            "ldp %[a0], %[a1], [%[x]], #16\n\t"
            "ldp %[b0], %[b1], [%[y]], #16\n\t"
            "adcs %[a0], %[a0], %[b0]\n\t"
            "adcs %[a1], %[a1], %[b1]\n\t"
            "stp %[a0], %[a1], [%[r]], #16\n"
            "2:\n\t"
            "lsr %[n], %[n], #2\n\t"           /* (lsr, sub and cbnz leave the carry alone) */
            "cbz %[n], 4f\n"
            "3:\n\t"
            "ldp %[a0], %[a1], [%[x]], #16\n\t"
            "ldp %[b0], %[b1], [%[y]], #16\n\t"
            "ldp %[a2], %[a3], [%[x]], #16\n\t"
            "ldp %[b2], %[b3], [%[y]], #16\n\t"
            "adcs %[a0], %[a0], %[b0]\n\t"
            "adcs %[a1], %[a1], %[b1]\n\t"
            "adcs %[a2], %[a2], %[b2]\n\t"
            "adcs %[a3], %[a3], %[b3]\n\t"
            "stp %[a0], %[a1], [%[r]], #16\n\t"
            "stp %[a2], %[a3], [%[r]], #16\n\t"
            "sub %[n], %[n], #1\n\t"
            "cbnz %[n], 3b\n"
            "4:\n\t"
            "cset %[c], cs"
            : [r] "+r"(r), [x] "+r"(x), [y] "+r"(y), [n] "+r"(n), [c] "=r"(c),
              [a0] "=&r"(a0), [a1] "=&r"(a1), [a2] "=&r"(a2), [a3] "=&r"(a3),
              [b0] "=&r"(b0), [b1] "=&r"(b1), [b2] "=&r"(b2), [b3] "=&r"(b3)
            :
            : "cc", "memory");
    return c;
#elif XB_ASM_X64
    uint64_t a, c = 0, rem = (uint64_t)n & 3, cnt = (uint64_t)n >> 2;
    __asm__("xor %k[c], %k[c]\n\t"               /* (also clears the carry) */
            "test %[rem], %[rem]\n\t"
            "jz 2f\n"
            "1:\n\t"
            "mov (%[x]), %[a]\n\t"
            "adc (%[y]), %[a]\n\t"
            "mov %[a], (%[r])\n\t"
            "lea 8(%[x]), %[x]\n\t"                /* (lea and dec leave the carry alone) */
            "lea 8(%[y]), %[y]\n\t"
            "lea 8(%[r]), %[r]\n\t"
            "dec %[rem]\n\t"
            "jnz 1b\n"
            "2:\n\t"
            "jrcxz 4f\n"
            "3:\n\t"
            "mov (%[x]), %[a]\n\t"
            "adc (%[y]), %[a]\n\t"
            "mov %[a], (%[r])\n\t"
            "mov 8(%[x]), %[a]\n\t"
            "adc 8(%[y]), %[a]\n\t"
            "mov %[a], 8(%[r])\n\t"
            "mov 16(%[x]), %[a]\n\t"
            "adc 16(%[y]), %[a]\n\t"
            "mov %[a], 16(%[r])\n\t"
            "mov 24(%[x]), %[a]\n\t"
            "adc 24(%[y]), %[a]\n\t"
            "mov %[a], 24(%[r])\n\t"
            "lea 32(%[x]), %[x]\n\t"
            "lea 32(%[y]), %[y]\n\t"
            "lea 32(%[r]), %[r]\n\t"
            "dec %[cnt]\n\t"
            "jnz 3b\n"
            "4:\n\t"
            "setc %b[c]"
            : [r] "+r"(r), [x] "+r"(x), [y] "+r"(y), [rem] "+r"(rem), [cnt] "+c"(cnt), [c] "=&r"(c), [a] "=&r"(a)
            :
            : "cc", "memory");
    return c;
#else
    xb_c c = 0;
    int64_t i = 0;
    for (; i + 4 <= n; i += 4) {
        r[i] = xb_addc(x[i], y[i], c, &c);
        r[i + 1] = xb_addc(x[i + 1], y[i + 1], c, &c);
        r[i + 2] = xb_addc(x[i + 2], y[i + 2], c, &c);
        r[i + 3] = xb_addc(x[i + 3], y[i + 3], c, &c);
    }
    for (; i < n; i++) r[i] = xb_addc(x[i], y[i], c, &c);
    return c;
#endif
}
/* r = x - y (n limbs each); returns the borrow out */
static uint64_t xb_sub_n(uint64_t *r, const uint64_t *x, const uint64_t *y, int64_t n) {
#if XB_ASM_A64
    uint64_t a0, a1, a2, a3, b0, b1, b2, b3, c;
    __asm__("cmp xzr, xzr\n\t"                 /* no borrow (the carry set, on ARM) */
            "tbz %[n], #0, 1f\n\t"
            "ldr %[a0], [%[x]], #8\n\t"
            "ldr %[b0], [%[y]], #8\n\t"
            "sbcs %[a0], %[a0], %[b0]\n\t"
            "str %[a0], [%[r]], #8\n"
            "1:\n\t"
            "tbz %[n], #1, 2f\n\t"
            "ldp %[a0], %[a1], [%[x]], #16\n\t"
            "ldp %[b0], %[b1], [%[y]], #16\n\t"
            "sbcs %[a0], %[a0], %[b0]\n\t"
            "sbcs %[a1], %[a1], %[b1]\n\t"
            "stp %[a0], %[a1], [%[r]], #16\n"
            "2:\n\t"
            "lsr %[n], %[n], #2\n\t"
            "cbz %[n], 4f\n"
            "3:\n\t"
            "ldp %[a0], %[a1], [%[x]], #16\n\t"
            "ldp %[b0], %[b1], [%[y]], #16\n\t"
            "ldp %[a2], %[a3], [%[x]], #16\n\t"
            "ldp %[b2], %[b3], [%[y]], #16\n\t"
            "sbcs %[a0], %[a0], %[b0]\n\t"
            "sbcs %[a1], %[a1], %[b1]\n\t"
            "sbcs %[a2], %[a2], %[b2]\n\t"
            "sbcs %[a3], %[a3], %[b3]\n\t"
            "stp %[a0], %[a1], [%[r]], #16\n\t"
            "stp %[a2], %[a3], [%[r]], #16\n\t"
            "sub %[n], %[n], #1\n\t"
            "cbnz %[n], 3b\n"
            "4:\n\t"
            "cset %[c], cc"
            : [r] "+r"(r), [x] "+r"(x), [y] "+r"(y), [n] "+r"(n), [c] "=r"(c),
              [a0] "=&r"(a0), [a1] "=&r"(a1), [a2] "=&r"(a2), [a3] "=&r"(a3),
              [b0] "=&r"(b0), [b1] "=&r"(b1), [b2] "=&r"(b2), [b3] "=&r"(b3)
            :
            : "cc", "memory");
    return c;
#elif XB_ASM_X64
    uint64_t a, c = 0, rem = (uint64_t)n & 3, cnt = (uint64_t)n >> 2;
    __asm__("xor %k[c], %k[c]\n\t"               /* (also clears the carry) */
            "test %[rem], %[rem]\n\t"
            "jz 2f\n"
            "1:\n\t"
            "mov (%[x]), %[a]\n\t"
            "sbb (%[y]), %[a]\n\t"
            "mov %[a], (%[r])\n\t"
            "lea 8(%[x]), %[x]\n\t"                /* (lea and dec leave the carry alone) */
            "lea 8(%[y]), %[y]\n\t"
            "lea 8(%[r]), %[r]\n\t"
            "dec %[rem]\n\t"
            "jnz 1b\n"
            "2:\n\t"
            "jrcxz 4f\n"
            "3:\n\t"
            "mov (%[x]), %[a]\n\t"
            "sbb (%[y]), %[a]\n\t"
            "mov %[a], (%[r])\n\t"
            "mov 8(%[x]), %[a]\n\t"
            "sbb 8(%[y]), %[a]\n\t"
            "mov %[a], 8(%[r])\n\t"
            "mov 16(%[x]), %[a]\n\t"
            "sbb 16(%[y]), %[a]\n\t"
            "mov %[a], 16(%[r])\n\t"
            "mov 24(%[x]), %[a]\n\t"
            "sbb 24(%[y]), %[a]\n\t"
            "mov %[a], 24(%[r])\n\t"
            "lea 32(%[x]), %[x]\n\t"
            "lea 32(%[y]), %[y]\n\t"
            "lea 32(%[r]), %[r]\n\t"
            "dec %[cnt]\n\t"
            "jnz 3b\n"
            "4:\n\t"
            "setc %b[c]"
            : [r] "+r"(r), [x] "+r"(x), [y] "+r"(y), [rem] "+r"(rem), [cnt] "+c"(cnt), [c] "=&r"(c), [a] "=&r"(a)
            :
            : "cc", "memory");
    return c;
#else
    xb_c c = 0;
    int64_t i = 0;
    for (; i + 4 <= n; i += 4) {
        r[i] = xb_subc(x[i], y[i], c, &c);
        r[i + 1] = xb_subc(x[i + 1], y[i + 1], c, &c);
        r[i + 2] = xb_subc(x[i + 2], y[i + 2], c, &c);
        r[i + 3] = xb_subc(x[i + 3], y[i + 3], c, &c);
    }
    for (; i < n; i++) r[i] = xb_subc(x[i], y[i], c, &c);
    return c;
#endif
}
/* r = x + c (n limbs, c a carry of 0 or 1); returns the carry out. Once the carry is gone the rest is a copy. */
static uint64_t xb_add_1(uint64_t *r, const uint64_t *x, int64_t n, uint64_t c) {
    int64_t i = 0;
    for (; i < n && c; i++) { r[i] = x[i] + 1; c = r[i] == 0; }
    if (r != x && i < n) memcpy(r + i, x + i, 8 * (size_t)(n - i));
    return c;
}
/* r = x - b (n limbs, b a borrow of 0 or 1); returns the borrow out */
static uint64_t xb_sub_1(uint64_t *r, const uint64_t *x, int64_t n, uint64_t b) {
    int64_t i = 0;
    for (; i < n && b; i++) { uint64_t v = x[i]; r[i] = v - 1; b = v == 0; }
    if (r != x && i < n) memcpy(r + i, x + i, 8 * (size_t)(n - i));
    return b;
}
/* r = x * m (n limbs); returns the high limb. The products are independent: only the additions of each
   product's high half into the next low half form a chain. */
static uint64_t xb_mul_1(uint64_t *r, const uint64_t *x, int64_t n, uint64_t m) {
#if XB_ASM_A64
    uint64_t a0, a1, a2, a3, h0, h1, h2, h3, h = 0;
    __asm__("cmn xzr, xzr\n\t"
            "tbz %[n], #0, 1f\n\t"
            "ldr %[a0], [%[x]], #8\n\t"
            "mul %[h0], %[a0], %[m]\n\t"
            "umulh %[a0], %[a0], %[m]\n\t"
            "adcs %[h0], %[h0], %[h]\n\t"
            "mov %[h], %[a0]\n\t"
            "str %[h0], [%[r]], #8\n"
            "1:\n\t"
            "tbz %[n], #1, 2f\n\t"
            "ldp %[a0], %[a1], [%[x]], #16\n\t"
            "mul %[h0], %[a0], %[m]\n\t"
            "umulh %[a0], %[a0], %[m]\n\t"
            "mul %[h1], %[a1], %[m]\n\t"
            "umulh %[a1], %[a1], %[m]\n\t"
            "adcs %[h0], %[h0], %[h]\n\t"
            "adcs %[h1], %[h1], %[a0]\n\t"
            "mov %[h], %[a1]\n\t"
            "stp %[h0], %[h1], [%[r]], #16\n"
            "2:\n\t"
            "lsr %[n], %[n], #2\n\t"
            "cbz %[n], 4f\n"
            "3:\n\t"
            "ldp %[a0], %[a1], [%[x]], #16\n\t"
            "ldp %[a2], %[a3], [%[x]], #16\n\t"
            "mul %[h0], %[a0], %[m]\n\t"
            "umulh %[a0], %[a0], %[m]\n\t"
            "mul %[h1], %[a1], %[m]\n\t"
            "umulh %[a1], %[a1], %[m]\n\t"
            "mul %[h2], %[a2], %[m]\n\t"
            "umulh %[a2], %[a2], %[m]\n\t"
            "mul %[h3], %[a3], %[m]\n\t"
            "umulh %[a3], %[a3], %[m]\n\t"
            "adcs %[h0], %[h0], %[h]\n\t"     /* low halves (in h0..h3) plus the high half below */
            "adcs %[h1], %[h1], %[a0]\n\t"
            "adcs %[h2], %[h2], %[a1]\n\t"
            "adcs %[h3], %[h3], %[a2]\n\t"
            "mov %[h], %[a3]\n\t"
            "stp %[h0], %[h1], [%[r]], #16\n\t"
            "stp %[h2], %[h3], [%[r]], #16\n\t"
            "sub %[n], %[n], #1\n\t"
            "cbnz %[n], 3b\n"
            "4:\n\t"
            "adc %[h], %[h], xzr"             /* (the high half of a product is at most 2^64 - 2) */
            : [r] "+r"(r), [x] "+r"(x), [n] "+r"(n), [h] "+r"(h),
              [a0] "=&r"(a0), [a1] "=&r"(a1), [a2] "=&r"(a2), [a3] "=&r"(a3),
              [h0] "=&r"(h0), [h1] "=&r"(h1), [h2] "=&r"(h2), [h3] "=&r"(h3)
            : [m] "r"(m)
            : "cc", "memory");
    return h;
#else
    uint64_t h = 0;
    int64_t i = 0;
    for (; i + 4 <= n; i += 4) { /* (the chain restarts every 4 limbs, the carry folded into the high half) */
        xs_u128 p0 = (xs_u128)x[i] * m, p1 = (xs_u128)x[i + 1] * m, p2 = (xs_u128)x[i + 2] * m, p3 = (xs_u128)x[i + 3] * m;
        xb_c c;
        r[i] = xb_addc((uint64_t)p0, h, 0, &c);
        r[i + 1] = xb_addc((uint64_t)p1, (uint64_t)(p0 >> 64), c, &c);
        r[i + 2] = xb_addc((uint64_t)p2, (uint64_t)(p1 >> 64), c, &c);
        r[i + 3] = xb_addc((uint64_t)p3, (uint64_t)(p2 >> 64), c, &c);
        h = (uint64_t)(p3 >> 64) + c;
    }
    for (; i < n; i++) { xs_u128 p = (xs_u128)x[i] * m + h; r[i] = (uint64_t)p; h = (uint64_t)(p >> 64); }
    return h;
#endif
}
/* r = a + x * m (n limbs; r may be a); returns the limb carried out */
static uint64_t xb_addmul_1(uint64_t *r, const uint64_t *a, const uint64_t *x, int64_t n, uint64_t m) {
    uint64_t h = 0;
#if XB_ASM_A64
    int64_t blocks = n >> 2;
    if (blocks) {
        uint64_t a0, a1, a2, a3, l0, l1, l2, l3, r0, r1, r2, r3;
        __asm__("1:\n\t"
                "ldp %[a0], %[a1], [%[x]], #16\n\t"
                "ldp %[a2], %[a3], [%[x]], #16\n\t"
                "ldp %[r0], %[r1], [%[a]], #16\n\t"
                "ldp %[r2], %[r3], [%[a]], #16\n\t"
                "mul %[l0], %[a0], %[m]\n\t"
                "umulh %[a0], %[a0], %[m]\n\t"
                "mul %[l1], %[a1], %[m]\n\t"
                "umulh %[a1], %[a1], %[m]\n\t"
                "mul %[l2], %[a2], %[m]\n\t"
                "umulh %[a2], %[a2], %[m]\n\t"
                "mul %[l3], %[a3], %[m]\n\t"
                "umulh %[a3], %[a3], %[m]\n\t"
                "adds %[l0], %[l0], %[h]\n\t"      /* x * m + h over the 4 limbs: l0..l3, a3 on top */
                "adcs %[l1], %[l1], %[a0]\n\t"
                "adcs %[l2], %[l2], %[a1]\n\t"
                "adcs %[l3], %[l3], %[a2]\n\t"
                "adc %[a3], %[a3], xzr\n\t"
                "adds %[r0], %[r0], %[l0]\n\t"     /* plus a */
                "adcs %[r1], %[r1], %[l1]\n\t"
                "adcs %[r2], %[r2], %[l2]\n\t"
                "adcs %[r3], %[r3], %[l3]\n\t"
                "adc %[h], %[a3], xzr\n\t"
                "stp %[r0], %[r1], [%[r]], #16\n\t"
                "stp %[r2], %[r3], [%[r]], #16\n\t"
                "subs %[b], %[b], #1\n\t"
                "b.ne 1b"
                : [r] "+r"(r), [a] "+r"(a), [x] "+r"(x), [b] "+r"(blocks), [h] "+r"(h),
                  [a0] "=&r"(a0), [a1] "=&r"(a1), [a2] "=&r"(a2), [a3] "=&r"(a3),
                  [l0] "=&r"(l0), [l1] "=&r"(l1), [l2] "=&r"(l2), [l3] "=&r"(l3),
                  [r0] "=&r"(r0), [r1] "=&r"(r1), [r2] "=&r"(r2), [r3] "=&r"(r3)
                : [m] "r"(m)
                : "cc", "memory");
        n &= 3;
    }
#else
    for (; n >= 4; n -= 4, r += 4, a += 4, x += 4) { /* (two carry chains, each restarting every 4 limbs) */
        xs_u128 p0 = (xs_u128)x[0] * m, p1 = (xs_u128)x[1] * m, p2 = (xs_u128)x[2] * m, p3 = (xs_u128)x[3] * m;
        xb_c c, d;
        uint64_t t0 = xb_addc((uint64_t)p0, h, 0, &c);
        uint64_t t1 = xb_addc((uint64_t)p1, (uint64_t)(p0 >> 64), c, &c);
        uint64_t t2 = xb_addc((uint64_t)p2, (uint64_t)(p1 >> 64), c, &c);
        uint64_t t3 = xb_addc((uint64_t)p3, (uint64_t)(p2 >> 64), c, &c);
        uint64_t h3 = (uint64_t)(p3 >> 64) + c;
        r[0] = xb_addc(a[0], t0, 0, &d);
        r[1] = xb_addc(a[1], t1, d, &d);
        r[2] = xb_addc(a[2], t2, d, &d);
        r[3] = xb_addc(a[3], t3, d, &d);
        h = h3 + d;
    }
#endif
    for (int64_t i = 0; i < n; i++) { xs_u128 p = (xs_u128)x[i] * m + a[i] + h; r[i] = (uint64_t)p; h = (uint64_t)(p >> 64); }
    return h;
}
/* r = a - x * m (n limbs; r may be a); returns the limb borrowed out (to come off the limb above) */
static uint64_t xb_submul_1(uint64_t *r, const uint64_t *a, const uint64_t *x, int64_t n, uint64_t m) {
    uint64_t h = 0;
#if XB_ASM_A64
    int64_t blocks = n >> 2;
    if (blocks) {
        uint64_t a0, a1, a2, a3, l0, l1, l2, l3, r0, r1, r2, r3;
        __asm__("1:\n\t"
                "ldp %[a0], %[a1], [%[x]], #16\n\t"
                "ldp %[a2], %[a3], [%[x]], #16\n\t"
                "ldp %[r0], %[r1], [%[a]], #16\n\t"
                "ldp %[r2], %[r3], [%[a]], #16\n\t"
                "mul %[l0], %[a0], %[m]\n\t"
                "umulh %[a0], %[a0], %[m]\n\t"
                "mul %[l1], %[a1], %[m]\n\t"
                "umulh %[a1], %[a1], %[m]\n\t"
                "mul %[l2], %[a2], %[m]\n\t"
                "umulh %[a2], %[a2], %[m]\n\t"
                "mul %[l3], %[a3], %[m]\n\t"
                "umulh %[a3], %[a3], %[m]\n\t"
                "adds %[l0], %[l0], %[h]\n\t"
                "adcs %[l1], %[l1], %[a0]\n\t"
                "adcs %[l2], %[l2], %[a1]\n\t"
                "adcs %[l3], %[l3], %[a2]\n\t"
                "adc %[a3], %[a3], xzr\n\t"
                "subs %[r0], %[r0], %[l0]\n\t"     /* a minus it */
                "sbcs %[r1], %[r1], %[l1]\n\t"
                "sbcs %[r2], %[r2], %[l2]\n\t"
                "sbcs %[r3], %[r3], %[l3]\n\t"
                "cinc %[h], %[a3], cc\n\t"          /* (carry clear: a borrow) */
                "stp %[r0], %[r1], [%[r]], #16\n\t"
                "stp %[r2], %[r3], [%[r]], #16\n\t"
                "subs %[b], %[b], #1\n\t"
                "b.ne 1b"
                : [r] "+r"(r), [a] "+r"(a), [x] "+r"(x), [b] "+r"(blocks), [h] "+r"(h),
                  [a0] "=&r"(a0), [a1] "=&r"(a1), [a2] "=&r"(a2), [a3] "=&r"(a3),
                  [l0] "=&r"(l0), [l1] "=&r"(l1), [l2] "=&r"(l2), [l3] "=&r"(l3),
                  [r0] "=&r"(r0), [r1] "=&r"(r1), [r2] "=&r"(r2), [r3] "=&r"(r3)
                : [m] "r"(m)
                : "cc", "memory");
        n &= 3;
    }
#else
    for (; n >= 4; n -= 4, r += 4, a += 4, x += 4) { /* (two carry chains, each restarting every 4 limbs) */
        xs_u128 p0 = (xs_u128)x[0] * m, p1 = (xs_u128)x[1] * m, p2 = (xs_u128)x[2] * m, p3 = (xs_u128)x[3] * m;
        xb_c c, d;
        uint64_t t0 = xb_addc((uint64_t)p0, h, 0, &c);
        uint64_t t1 = xb_addc((uint64_t)p1, (uint64_t)(p0 >> 64), c, &c);
        uint64_t t2 = xb_addc((uint64_t)p2, (uint64_t)(p1 >> 64), c, &c);
        uint64_t t3 = xb_addc((uint64_t)p3, (uint64_t)(p2 >> 64), c, &c);
        uint64_t h3 = (uint64_t)(p3 >> 64) + c;
        r[0] = xb_subc(a[0], t0, 0, &d);
        r[1] = xb_subc(a[1], t1, d, &d);
        r[2] = xb_subc(a[2], t2, d, &d);
        r[3] = xb_subc(a[3], t3, d, &d);
        h = h3 + d;
    }
#endif
    for (int64_t i = 0; i < n; i++) {
        xs_u128 p = (xs_u128)x[i] * m + h;
        uint64_t lo = (uint64_t)p, t = a[i];
        h = (uint64_t)(p >> 64) + (t < lo);
        r[i] = t - lo;
    }
    return h;
}
/* a + (bneg ? -|b| : |b|) with a's sign: the core of + and - */
XS_NOINLINE xs_big *xs_big_addsub(const xs_big *a, const xs_big *b, int bneg) {
    if (a->neg == bneg) { /* same signs: the magnitudes add */
        const xs_big *x = a->n >= b->n ? a : b, *y = x == a ? b : a;
        xs_big *r = xs_big_alloc(x->n + 1);
        uint64_t c = xb_add_n(r->d, x->d, y->d, y->n);
        r->d[x->n] = xb_add_1(r->d + y->n, x->d + y->n, x->n - y->n, c);
        r->neg = a->neg;
        return xs_big_norm(r);
    }
    int c = xs_big_mag_cmp(a, b); /* different signs: the smaller magnitude comes off the larger */
    if (c == 0) return xs_big_from_int(0);
    const xs_big *x = c > 0 ? a : b, *y = c > 0 ? b : a;
    xs_big *r = xs_big_alloc(x->n);
    uint64_t br = xb_sub_n(r->d, x->d, y->d, y->n);
    xb_sub_1(r->d + y->n, x->d + y->n, x->n - y->n, br);
    r->neg = c > 0 ? a->neg : bneg;
    return xs_big_norm(r);
}
XS_NOINLINE xs_big *xs_big_add(xs_big *a, xs_big *b) { return xs_big_addsub(a, b, b->neg); }
XS_NOINLINE xs_big *xs_big_sub(xs_big *a, xs_big *b) { return xs_big_addsub(a, b, b->n ? !b->neg : 0); }
XS_NOINLINE xs_big *xs_big_neg(xs_big *a) {
    xs_big *r = xs_big_alloc(a->n);
    memcpy(r->d, a->d, 8 * (size_t)a->n);
    r->neg = a->n ? !a->neg : 0;
    return r;
}

XS_NOINLINE xs_big *xs_big_mul(xs_big *a, xs_big *b) {
    if (a->n == 0 || b->n == 0) return xs_big_from_int(0);
    const xs_big *x = a->n >= b->n ? a : b, *y = x == a ? b : a; /* the longer one in the inner loop */
    xs_big *r = xs_big_alloc((int64_t)x->n + y->n);
    r->d[x->n] = xb_mul_1(r->d, x->d, x->n, y->d[0]); /* by one limb: the common case (acc * k) */
    for (int32_t j = 1; j < y->n; j++) r->d[j + x->n] = xb_addmul_1(r->d + j, r->d + j, x->d, x->n, y->d[j]);
    r->neg = a->neg != b->neg;
    return xs_big_norm(r);
}

/* a + b * c and a - b * c (the checker joins them: acc += num * 2n): with a one-limb factor, the product goes
   into a copy of a in the same pass that makes it (addmul_1 / submul_1), no product object in between */
XS_NOINLINE xs_big *xs_big_addmul_s(xs_big *a, xs_big *b, xs_big *c, int sub) {
    const xs_big *x = b->n >= c->n ? b : c, *y = x == b ? c : b;
    if (y->n != 1 || a->n == 0) { /* a longer factor, or nothing to add to: the two steps */
        xs_big *t = xs_big_mul(b, c), *r = sub ? xs_big_sub(a, t) : xs_big_add(a, t);
        xs_obj_free(t);
        return r;
    }
    int32_t xn = x->n, an = a->n, top = an > xn ? an : xn + 1, tn = an > xn ? an - xn - 1 : 0; /* tn: a's limbs above xn */
    xs_big *r = xs_big_alloc(top + 1);
    const uint64_t *src = a->d;
    if (an < xn) { /* a shorter than the product: zero-extended in r first */
        memcpy(r->d, a->d, 8 * (size_t)an);
        memset(r->d + an, 0, 8 * (size_t)(xn - an));
        src = r->d;
    }
    uint64_t ax = xn < an ? a->d[xn] : 0, m = y->d[0];
    int pneg = (b->neg != c->neg) ^ sub; /* the sign of what comes onto a */
    r->neg = a->neg;
    if (pneg == a->neg) { /* the magnitudes add */
        uint64_t h = xb_addmul_1(r->d, src, x->d, xn, m), t = ax + h;
        r->d[xn] = t;
        r->d[top] = xb_add_1(r->d + xn + 1, a->d + xn + 1, tn, t < h);
    } else { /* |a| - |x| * m: below zero, the top limbs hold it in two's complement and are negated */
        uint64_t h = xb_submul_1(r->d, src, x->d, xn, m);
        r->d[xn] = ax - h;
        r->d[top] = 0;
        if (xb_sub_1(r->d + xn + 1, a->d + xn + 1, tn, ax < h)) { /* (pidigits' acc is often negative: common) */
            int32_t i = 0; /* -r: the low zero limbs stay, the first nonzero one is negated, the rest inverted */
            while (r->d[i] == 0) i++; /* (r is not zero: it was below zero) */
            r->d[i] = (uint64_t)0 - r->d[i];
            for (i++; i < top; i++) r->d[i] = ~r->d[i];
            r->neg = !a->neg;
        }
    }
    return xs_big_norm(r);
}
XS_NOINLINE xs_big *xs_big_addmul(xs_big *a, xs_big *b, xs_big *c) { return xs_big_addmul_s(a, b, c, 0); }
XS_NOINLINE xs_big *xs_big_submul(xs_big *a, xs_big *b, xs_big *c) { return xs_big_addmul_s(a, b, c, 1); }

/* |a| / |b| and |a| % |b| (either may be NULL): Knuth's algorithm D */
static void xs_big_divmod_mag(const xs_big *a, const xs_big *b, xs_big **qo, xs_big **ro) {
    int32_t m = a->n, n = b->n;
    if (xs_big_mag_cmp(a, b) < 0) {
        if (qo) *qo = xs_big_from_int(0);
        if (ro) { xs_big *r = xs_big_alloc(m); memcpy(r->d, a->d, 8 * (size_t)m); *ro = xs_big_norm(r); }
        return;
    }
    if (n >= 2 && (m == n || (m == n + 1 && a->d[n] < b->d[n - 1]))) {
        /* the quotient fits one limb (pidigits' digit): estimated from the top limbs, as one step of algorithm D */
        int s = __builtin_clzll(b->d[n - 1]);
        #define XB_L(x, i) ((i) >= 0 && (i) < (x)->n ? (x)->d[i] : 0)
        #define XB_SH(x, i) (s ? (XB_L(x, i) << s) | (XB_L(x, (i) - 1) >> (64 - s)) : XB_L(x, i))
        uint64_t u2 = XB_SH(a, n), u1 = XB_SH(a, n - 1), u0 = XB_SH(a, n - 2);
        uint64_t v1 = XB_SH(b, n - 1), v0 = XB_SH(b, n - 2);
        #undef XB_SH
        xs_u128 num = ((xs_u128)u2 << 64) | u1;
        xs_u128 qhat = num / v1, rhat = num % v1;
        while (qhat >> 64 || qhat * v0 > ((rhat << 64) | u0)) { /* now q or q + 1 (Knuth, step D3) */
            qhat--;
            rhat += v1;
            if (rhat >> 64) break;
        }
        uint64_t qd = (uint64_t)qhat, top = XB_L(a, n);
        #undef XB_L
        bool known = false;
        if (!ro) {
            /* Without the remainder, the top two limbs of b usually settle it: with t = n - 2, a - qd * b =
               (A - qd * B) * 2^(64t) + (a's low t limbs - qd * b's low t limbs), the second term between
               -qd * 2^(64t) and 2^(64t). So D = A - qd * B (A, B: the limbs from t up) below zero means qd is one
               too large, and D >= qd means it is right (qd is never too small). Only 0 <= D < qd, which almost
               never happens, needs the whole product. */
            xs_u128 p0 = (xs_u128)qd * b->d[n - 2], p1 = (xs_u128)qd * b->d[n - 1] + (uint64_t)(p0 >> 64);
            xb_c c;
            uint64_t d0 = xb_subc(a->d[n - 2], (uint64_t)p0, 0, &c);
            uint64_t d1 = xb_subc(a->d[n - 1], (uint64_t)p1, c, &c);
            uint64_t d2 = xb_subc(top, (uint64_t)(p1 >> 64), c, &c);
            if (c) { qd--; known = true; }
            else if (d2 || d1 || d0 >= qd || n == 2) known = true;
        }
        if (!known) { /* r = a - qd * b over n limbs; what is borrowed past them comes off a's limb n */
            xs_big *r = xs_big_alloc(n);
            uint64_t h = xb_submul_1(r->d, a->d, b->d, n, qd);
            if (h > top) { /* below zero: qd was one too large; the remainder is r + b (the carry out cancels the borrow) */
                qd--;
                xb_add_n(r->d, r->d, b->d, n);
            }
            if (ro) *ro = xs_big_norm(r); else xs_obj_free(r);
        }
        if (qo) { xs_big *q = xs_big_alloc(1); q->d[0] = qd; *qo = xs_big_norm(q); }
        return;
    }
    xs_big *q = xs_big_alloc(m - n + 1);
    if (n == 1) { /* by one limb */
        uint64_t v = b->d[0], rem = 0;
        for (int32_t i = m - 1; i >= 0; i--) { xs_u128 cur = ((xs_u128)rem << 64) | a->d[i]; q->d[i] = (uint64_t)(cur / v); rem = (uint64_t)(cur % v); }
        q->n = m;
        if (ro) { xs_big *r = xs_big_alloc(1); r->d[0] = rem; *ro = xs_big_norm(r); }
    } else {
        int s = __builtin_clzll(b->d[n - 1]);
        uint64_t *vn = malloc(8 * (size_t)(n + m + 1)), *un = vn + n; /* the divisor and the dividend shifted left by s */
        if (!vn) xs_panic("out of memory");
        for (int32_t i = n - 1; i > 0; i--) vn[i] = (b->d[i] << s) | (s ? b->d[i - 1] >> (64 - s) : 0);
        vn[0] = b->d[0] << s;
        un[m] = s ? a->d[m - 1] >> (64 - s) : 0;
        for (int32_t i = m - 1; i > 0; i--) un[i] = (a->d[i] << s) | (s ? a->d[i - 1] >> (64 - s) : 0);
        un[0] = a->d[0] << s;
        for (int32_t j = m - n; j >= 0; j--) {
            xs_u128 num = ((xs_u128)un[j + n] << 64) | un[j + n - 1];
            xs_u128 qhat = num / vn[n - 1], rhat = num % vn[n - 1];
            while (qhat >> 64 || qhat * vn[n - 2] > ((rhat << 64) | un[j + n - 2])) {
                qhat--;
                rhat += vn[n - 1];
                if (rhat >> 64) break;
            }
            uint64_t h = xb_submul_1(un + j, un + j, vn, n, (uint64_t)qhat); /* un[j..j+n] -= qhat * vn */
            bool neg = un[j + n] < h;
            un[j + n] -= h;
            if (neg) { /* qhat was one too large (rare): add vn back */
                qhat--;
                un[j + n] += xb_add_n(un + j, un + j, vn, n);
            }
            q->d[j] = (uint64_t)qhat;
        }
        q->n = m - n + 1;
        if (ro) {
            xs_big *r = xs_big_alloc(n);
            for (int32_t i = 0; i < n; i++) r->d[i] = (un[i] >> s) | (s ? un[i + 1] << (64 - s) : 0);
            *ro = xs_big_norm(r);
        }
        free(vn);
    }
    xs_big_norm(q);
    if (qo) *qo = q; else xs_obj_free(q);
}
XS_NOINLINE xs_big *xs_big_div(xs_big *a, xs_big *b) { /* truncates toward zero, as in JS */
    if (b->n == 0) xs_panic("RangeError: Division by zero");
    xs_big *q;
    xs_big_divmod_mag(a, b, &q, NULL);
    q->neg = q->n ? a->neg != b->neg : 0;
    return q;
}
XS_NOINLINE xs_big *xs_big_mod(xs_big *a, xs_big *b) { /* the sign of the dividend, as in JS */
    if (b->n == 0) xs_panic("RangeError: Division by zero");
    xs_big *r;
    xs_big_divmod_mag(a, b, NULL, &r);
    r->neg = r->n ? a->neg : 0;
    return r;
}

XS_NOINLINE xs_big *xs_big_pow(xs_big *a, xs_big *e) { /* square and multiply */
    if (e->neg) xs_panic("RangeError: Exponent must be non-negative");
    if (e->n == 0) return xs_big_from_int(1);
    if (a->n == 0) return xs_big_from_int(0);
    if (a->n == 1 && a->d[0] == 1) return xs_big_from_int(a->neg && (e->d[0] & 1) ? -1 : 1); /* (-1) ** k at any size */
    if (e->n > 1 || e->d[0] > ((uint64_t)1 << 32)) xs_panic("RangeError: Maximum BigInt size exceeded");
    uint64_t k = e->d[0];
    xs_big *r = xs_big_from_int(1), *base = xs_big_mul(a, r);
    for (;;) {
        if (k & 1) { xs_big *t = xs_big_mul(r, base); xs_obj_free(r); r = t; }
        k >>= 1;
        if (!k) break;
        xs_big *t = xs_big_mul(base, base); xs_obj_free(base); base = t;
    }
    xs_obj_free(base);
    return r;
}

/* decimal digits: 19 at a time (10^19 fits in a limb) */
static char *xs_big_digits(const xs_big *a, size_t *len) {
    size_t cap = (size_t)a->n * 20 + 2;
    char *out = malloc(cap);
    if (!out) xs_panic("out of memory");
    if (a->n == 0) { out[0] = '0'; *len = 1; return out; }
    uint64_t *t = malloc(8 * (size_t)a->n);
    if (!t) xs_panic("out of memory");
    memcpy(t, a->d, 8 * (size_t)a->n);
    int32_t n = a->n;
    size_t k = cap; /* filled from the end */
    const uint64_t P = 10000000000000000000ULL;
    while (n > 0) {
        uint64_t rem = 0;
        for (int32_t i = n - 1; i >= 0; i--) { xs_u128 cur = ((xs_u128)rem << 64) | t[i]; t[i] = (uint64_t)(cur / P); rem = (uint64_t)(cur % P); }
        while (n > 0 && t[n - 1] == 0) n--;
        for (int i = 0; i < 19 && (n > 0 || rem); i++) { out[--k] = (char)('0' + rem % 10); rem /= 10; }
    }
    free(t);
    if (a->neg) out[--k] = '-';
    *len = cap - k;
    memmove(out, out + k, *len);
    return out;
}
XS_NOINLINE xs_str *xs_big_to_string(xs_big *a) {
    size_t n;
    char *d = xs_big_digits(a, &n);
    xs_str *s = xs_str_new(d, n);
    free(d);
    return s;
}
static void xs_big_print(void *p) { /* console.log(10n) prints 10n */
    size_t n;
    char *d = xs_big_digits(p, &n);
    fwrite(d, 1, n, XS_OUT);
    fputc('n', XS_OUT);
    free(d);
}
XS_NOINLINE double xs_big_to_number(xs_big *a) { /* rounded to the nearest double, as Number(b) */
    double v;
    if (a->n == 0) v = 0;
    else if (a->n == 1) v = (double)a->d[0];
    else if (a->n == 2) v = (double)(((xs_u128)a->d[1] << 64) | a->d[0]);
    else {
        size_t n;
        char *d = xs_big_digits(a, &n);
        d = realloc(d, n + 1);
        d[n] = 0;
        v = strtod(d, NULL);
        free(d);
        return v;
    }
    return a->neg ? -v : v;
}

/* BigInt("123"), BigInt("0x1f"), and literals too long for an int: decimal with an optional sign, or 0x/0o/0b */
XS_NOINLINE xs_big *xs_big_parse(const char *s, size_t len, bool *ok) {
    size_t i = 0, e = len;
    while (i < e && isspace((unsigned char)s[i])) i++;
    while (e > i && isspace((unsigned char)s[e - 1])) e--;
    int base = 10;
    bool neg = false;
    if (e - i > 2 && s[i] == '0' && strchr("xXoObB", s[i + 1])) { base = s[i + 1] == 'x' || s[i + 1] == 'X' ? 16 : s[i + 1] == 'o' || s[i + 1] == 'O' ? 8 : 2; i += 2; }
    else if (i < e && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; if (i == e) { *ok = false; return NULL; } }
    xs_big *r = xs_big_alloc((int64_t)(e - i) / 4 + 2); /* room: a digit adds at most 4 bits (hex), so len / 16 + 1 limbs would do */
    r->n = 0;
    for (; i < e; i++) {
        int c = (unsigned char)s[i], dv = isdigit(c) ? c - '0' : isxdigit(c) ? (tolower(c) - 'a' + 10) : 99;
        if (dv >= base) { xs_obj_free(r); *ok = false; return NULL; }
        uint64_t carry = (uint64_t)dv; /* r = r * base + dv */
        for (int32_t k = 0; k < r->n; k++) { xs_u128 p = (xs_u128)r->d[k] * (uint64_t)base + carry; r->d[k] = (uint64_t)p; carry = (uint64_t)(p >> 64); }
        if (carry) r->d[r->n++] = carry;
    }
    r->neg = neg;
    *ok = true;
    return xs_big_norm(r);
}
XS_NOINLINE xs_big *xs_big_from_str(xs_str *s) {
    bool ok;
    xs_big *r = xs_big_parse(s->data, (size_t)s->len, &ok);
    if (!ok) xs_panicf("SyntaxError: Cannot convert %s to a BigInt", s->data);
    return r;
}
/* JSON into a bigint (a row's NUMBER column typed bigint): the number, or its digits as text */
XS_NOINLINE xs_big *xs_json_big(xs_jv *v, const char *where) {
    if (v->kind == XJ_NUM) return xs_big_from_num(v->num);
    if (v->kind != XJ_STR) xs_panicf("JSON: '%s' must be a bigint (a number or its digits)", where);
    xs_str *s = xs_str_new(v->str, v->slen);
    xs_big *r = xs_big_from_str(s);
    xs_release(s);
    return r;
}
/* JSON into a Buffer: the hex text a BLOB column comes as, or what JSON.stringify writes for one,
   {"type":"Buffer","data":[1,2,3]} */
XS_NOINLINE xs_Uint8Array *xs_json_buf(xs_jv *v, const char *where) {
    if (v->kind == XJ_STR) {
        xs_str *s = xs_str_new(v->str, v->slen), *enc = xs_str_new("hex", 3);
        xs_Uint8Array *b = xs_buf_from_str(s, enc);
        xs_release(s);
        xs_release(enc);
        return b;
    }
    xs_jv *d = NULL;
    if (v->kind == XJ_OBJ)
        for (size_t i = 0; i < v->n; i++) if (strcmp(v->keys[i], "data") == 0) d = v->items[i];
    if (!d || d->kind != XJ_ARR) xs_panicf("JSON: '%s' must be bytes (hex text or {\"type\":\"Buffer\",\"data\":[...]})", where);
    xs_Uint8Array *b = xs_u8_raw((int64_t)d->n, true, false);
    for (size_t i = 0; i < d->n; i++) b->data[i] = d->items[i]->kind == XJ_NUM ? (uint8_t)(int64_t)d->items[i]->num : 0;
    return b;
}
static void xs_sb_buf(xs_sb *sb, xs_Uint8Array *b) { /* JSON.stringify of a Buffer, as Node writes it */
    xs_sb_put(sb, "{\"type\":\"Buffer\",\"data\":[", 25);
    for (int64_t i = 0; i < b->f_length; i++) { if (i) xs_sb_put(sb, ",", 1); xs_sb_int(sb, b->data[i]); }
    xs_sb_put(sb, "]}", 2);
}

/* ---- program start / end ---- */

static uint64_t xs_started;

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
static void xs_init(int argc, char **argv) {
#if defined(_WIN32) /* bytes as written, as Node does: no \r\n translation on the standard streams */
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    xs_started = xs_now();
    xs_types_check();
    xs_rev_mask_init();
    xs_std_init(argc, argv);
}
/* performance.now(): milliseconds since the program started, with the fraction (monotonic, as Node's) */
static double xs_performance_now(void) { return (double)(xs_now() - xs_started) / 1e6; }

static int xs_rc_counting_;
static bool xs_rc_counting(void) { /* XISTO_RC_STATS: release everything at exit and report what is left */
    if (!xs_rc_counting_) xs_rc_counting_ = getenv("XISTO_RC_STATS") ? 1 : 2;
    return xs_rc_counting_ == 1;
}

/* ---- cycle collector ----
   Reference counting frees everything except cycles (a parent and a child pointing at each other). The
   compiler marks the types that can form one (XS_CYC: a class whose fields can lead back to it, arrays and
   maps of those, closures that capture them); everything else (strings, DTOs, numbers) never comes here.
   An XS_CYC object whose count drops but stays above zero becomes a suspect. When there are enough, at the
   next allocation (never inside a free in progress) the subgraph of suspects is examined as CPython does:
   each object's count minus the references from inside the subgraph; what keeps a count above zero is held
   from outside, and so is everything it reaches; the rest is garbage cycles. Those are pinned, their fields
   emptied (releasing what they point to), then unpinned: they free like any object. */
static XS_TLS size_t xs_gc_threshold = 10000;
static XS_TLS bool xs_gc_running;
static XS_TLS bool xs_gc_want; /* xs_gc_due because of the live suspects: the collector runs (else a sweep) */
static XS_TLS int64_t xs_gc_runs, xs_gc_freed;
/* Suspects live in a vector. One that is freed by its count before the collector runs (XS_BUF set when it
   reaches zero) is not taken out: its fields are released as usual, the block stays in the vector marked
   XS_DEAD (no longer counted in xs_sus_n), and is given back later. So adding is a push and forgetting is a
   flag, with no position kept in the header (it has no room for one). The collector runs when it did before
   (enough live suspects); a vector grown to twice that with dead ones is only swept. */
static XS_TLS void **xs_susv;
static XS_TLS size_t xs_susv_len, xs_susv_cap;
static __attribute__((noinline)) void xs_gc_suspect(void *p) {
    if (xs_gc_running) return;
    xs_hdr *h = p;
    h->cls |= XS_BUF;
    if (xs_susv_len == xs_susv_cap) {
        xs_susv_cap = xs_susv_cap ? xs_susv_cap * 2 : 4096;
        xs_susv = realloc(xs_susv, sizeof(void *) * xs_susv_cap);
        if (!xs_susv) xs_panic("out of memory");
    }
    xs_susv[xs_susv_len++] = p;
    if (++xs_sus_n >= xs_gc_threshold) xs_gc_due = xs_gc_want = true;
    else if (xs_susv_len >= 2 * xs_gc_threshold) xs_gc_due = true;
}
static void xs_gc_collect(void);
/* (at an allocation, when due) the collector, or only a sweep of the dead suspects */
static __attribute__((noinline)) void xs_gc_step(void) {
    xs_gc_due = false;
    if (xs_gc_running) return;
    if (xs_gc_want) { xs_gc_collect(); return; }
    size_t n = 0;
    for (size_t i = 0; i < xs_susv_len; i++) {
        xs_hdr *h = xs_susv[i];
        if (h->cls & XS_DEAD) xs_obj_free_block(h, h->cls);
        else xs_susv[n++] = h;
    }
    xs_susv_len = n;
}

/* The examined subgraph. While the collector runs (nothing else does), an object of the subgraph carries XS_INS
   and its count field holds its index here: membership and lookup are a load, no hashing; the counts are
   put back before anything is freed. */
#define XS_INS XS_RGN /* (no object of a region class is cyclic) */
typedef struct { void **obj; int64_t *refs; int32_t *rc0; size_t n, cap; int32_t *edge; size_t *first; size_t ne, ecap, fcap; } xs_gcset;
static inline bool xs_gc_eligible(void *p) { /* a cyclic object alive, not being freed, not yet in the set */
    xs_hdr *h = p;
    return p && (h->cls & (XS_CYC | XS_INS)) == XS_CYC && h->rc > 0 && h->rc < XS_IMMORTAL;
}
static void xs_gcset_add(xs_gcset *g, void *p) {
    if (g->n == g->cap) {
        g->cap = g->cap ? g->cap * 2 : 1024;
        g->obj = realloc(g->obj, sizeof(void *) * g->cap);
        g->refs = realloc(g->refs, sizeof(int64_t) * g->cap);
        g->rc0 = realloc(g->rc0, sizeof(int32_t) * g->cap);
        if (!g->obj || !g->refs || !g->rc0) xs_panic("out of memory");
    }
    xs_hdr *h = p;
    g->obj[g->n] = p;
    g->refs[g->n] = h->rc;
    g->rc0[g->n] = h->rc;
    h->rc = (int32_t)g->n; /* the index, until the counts are restored */
    h->cls |= XS_INS;
    g->n++;
}
static inline int64_t xs_gc_index(void *p) { xs_hdr *h = p; return p && (h->cls & XS_INS) ? h->rc : -1; }
/* one visit per object: children join the set, and the edges inside it are written down (CSR) for the
   passes that follow, which then run over arrays with no indirect calls */
static void xs_gc_cb_gather(void **slot, void *arg) {
    xs_gcset *g = arg;
    void *c = *slot;
    if (!c) return;
    if (xs_gc_eligible(c)) xs_gcset_add(g, c);
    int64_t i = xs_gc_index(c);
    if (i < 0) return;
    if (g->ne == g->ecap) { g->ecap = g->ecap ? g->ecap * 2 : 4096; g->edge = realloc(g->edge, sizeof(int32_t) * g->ecap); if (!g->edge) xs_panic("out of memory"); }
    g->edge[g->ne++] = (int32_t)i;
}
static void xs_gc_cb_unref(void **slot, void *arg) {
    int64_t i = xs_gc_index(*slot);
    if (i >= 0) ((xs_gcset *)arg)->refs[i]--;
}
typedef struct { xs_gcset *g; size_t *stack; size_t sp, cap; } xs_gcmark;
static void xs_gc_cb_live(void **slot, void *arg) {
    xs_gcmark *m = arg;
    int64_t i = xs_gc_index(*slot);
    if (i >= 0 && m->g->refs[i] <= 0) { /* reached from outside: alive */
        m->g->refs[i] = 1;
        if (m->sp == m->cap) { m->cap = m->cap ? m->cap * 2 : 1024; m->stack = realloc(m->stack, sizeof(size_t) * m->cap); }
        m->stack[m->sp++] = (size_t)i;
    }
}
#define XS_GARB XS_INS /* (after the counts are restored, XS_INS's bit marks garbage) */
static void xs_gc_cb_cut(void **slot, void *arg) { /* a reference to other garbage: dropped, not counted down */
    (void)arg;
    if (*slot && (((xs_hdr *)*slot)->cls & XS_GARB)) *slot = NULL;
}
static XS_TLS xs_gcset xs_gcs; /* kept between runs: no reallocation every time */
static __attribute__((noinline)) void xs_gc_collect(void) {
    xs_gc_due = xs_gc_want = false;
    if (xs_gc_running) return;
    if (!xs_sus_n) { /* only dead ones */
        for (size_t i = 0; i < xs_susv_len; i++) xs_obj_free_block(xs_susv[i], ((xs_hdr *)xs_susv[i])->cls);
        xs_susv_len = 0;
        return;
    }
    xs_gc_running = true;
    xs_gcset *g = &xs_gcs;
    g->n = 0;
    /* 1. the suspects and every cyclic object they reach */
    for (size_t i = 0; i < xs_susv_len; i++) {
        xs_hdr *h = xs_susv[i];
        if (h->cls & XS_DEAD) { xs_obj_free_block(h, h->cls); continue; } /* freed while it waited here */
        h->cls &= ~XS_BUF;
        if (xs_gc_eligible(h)) xs_gcset_add(g, h);
    }
    xs_susv_len = 0;
    xs_sus_n = 0;
    g->ne = 0;
    for (size_t i = 0; i < g->n; i++) {
        if (g->n + 1 > g->fcap) { g->fcap = (g->n + 1) * 2; g->first = realloc(g->first, sizeof(size_t) * g->fcap); }
        g->first[i] = g->ne;
        xs_type_of(g->obj[i])->visit(g->obj[i], xs_gc_cb_gather, g);
    }
    if (g->n + 1 > g->fcap) { g->fcap = (g->n + 1) * 2; g->first = realloc(g->first, sizeof(size_t) * g->fcap); }
    g->first[g->n] = g->ne;
    /* 2. subtract the references from inside */
    for (size_t e = 0; e < g->ne; e++) g->refs[g->edge[e]]--;
    /* 3. held from outside, and what they reach, stays */
    static XS_TLS size_t *stack;
    static XS_TLS size_t scap;
    if (scap < g->n + 1) { scap = (g->n + 1) * 2; stack = realloc(stack, sizeof(size_t) * scap); }
    for (size_t i = 0; i < g->n; i++) {
        if (g->refs[i] <= 0) continue;
        g->refs[i] = 1;
        size_t sp = 0;
        stack[sp++] = i;
        while (sp) {
            size_t k = stack[--sp];
            for (size_t e = g->first[k]; e < g->first[k + 1]; e++) {
                int32_t c = g->edge[e];
                if (g->refs[c] <= 0) { g->refs[c] = 1; stack[sp++] = (size_t)c; }
            }
        }
    }
    /* counts back; the garbage keeps a mark */
    size_t ngarbage = 0, nalive = 0;
    for (size_t i = 0; i < g->n; i++) {
        xs_hdr *h = g->obj[i];
        h->rc = g->rc0[i];
        if (g->refs[i] <= 0) { g->obj[ngarbage++] = h; } /* XS_INS stays set: it is XS_GARB now */
        else { h->cls &= ~XS_INS; nalive++; }
    }
    /* 4. the garbage: references between garbage objects are cut (nothing outside points into it, so no count
       needs to drop), then each is freed by its own free function, which releases everything else it holds */
    for (size_t i = 0; i < ngarbage; i++) xs_type_of(g->obj[i])->visit(g->obj[i], xs_gc_cb_cut, NULL);
    xs_gc_running = false;
    for (size_t i = 0; i < ngarbage; i++) {
        xs_hdr *h = g->obj[i];
        h->cls &= ~(XS_GARB | XS_BUF);
        h->rc = 0;
        xs_type_of(h)->free(h);
    }
    xs_gc_runs++;
    xs_gc_freed += (int64_t)ngarbage;
    /* the next run waits for as many suspects as there were live objects to look at again: the work stays
       proportional, and garbage does not pile up while nothing big is alive */
    xs_gc_threshold = nalive > 10000 ? nalive : 10000;
}
/* visit functions of the runtime's own containers (used when the compiler marks them cyclic) */
static void xs_arr_ref_visit(void *p, xs_visit_cb cb, void *arg);
static void xs_map_visit(void *p, xs_visit_cb cb, void *arg);
/* an array / map that holds values of a cyclic type: the compiler marks it where it is made */
static inline void *xs_cyc_arr(void *a) {
    static const xs_type t XS_TYPE_SEC = {xs_arr_ref_free, xs_print_xs_arr_ref, xs_arr_ref_visit};
    xs_hdr *h = a;
    if (h && !(h->cls & XS_CYC) && xs_type_of(h)->free == xs_arr_ref_free) { xs_set_type(h, &t); h->cls |= XS_CYC; }
    return a;
}
static inline void *xs_cyc_map(void *m) {
    static const xs_type t XS_TYPE_SEC = {xs_map_free, xs_print_map, xs_map_visit};
    xs_hdr *h = m;
    if (h && !(h->cls & XS_CYC) && xs_type_of(h)->free == xs_map_free) { xs_set_type(h, &t); h->cls |= XS_CYC; }
    return m;
}
static void xs_arr_ref_visit(void *p, xs_visit_cb cb, void *arg) {
    xs_arr_ref *a = p;
    for (size_t i = 0; i < a->len; i++) cb(&a->data[i], arg);
}
static void xs_map_visit(void *p, xs_visit_cb cb, void *arg) {
    xs_map *m = p;
    if (m->val_kind != 3) return;
    for (size_t i = 0; i < m->cap; i++) if (XS_MAP_FULL(m, i)) cb(&m->slots[i].val.p, arg);
}
static void (*xs_exit_hook)(void); /* background work to finish first (Promise.race losers) */
static void xs_before_exit(void) {
    void (*h)(void) = xs_exit_hook;
    xs_exit_hook = NULL;
    if (h) h();
}
/* ---- regions (the compiler's region.c) ----
   The objects of a region class are made in one loop and cannot outlive an iteration of it: they come from an
   arena that is rewound when the next iteration starts (and after the loop). Their count still moves (code that
   handles them is ordinary code), but reaching zero frees nothing and they are never cycle suspects. Rewinding
   runs drop on the objects whose type holds references out of the region (strings, arrays, other objects). */
typedef struct xs_rchunk { struct xs_rchunk *next; size_t size; _Alignas(16) char data[]; } xs_rchunk;
typedef struct xs_region {
    xs_rchunk *first, *cur;
    char *ptr, *end;
    void **list;            /* objects to drop when rewound (every object under XISTO_REGION_CHECK) */
    size_t nlist, caplist;
    int64_t nobj;           /* XS_REGION_CHECK: objects made since the last rewind (counted into xs_allocs there, not one by one:
                               with workers each global counter is a per-thread variable, a call on macOS) */
    bool listed;
    bool on;                /* a dual region (region.c): an iteration of its loop is running */
    struct xs_region *next; /* every region that was entered, for the counts at exit */
} xs_region;
static XS_TLS xs_region *xs_regions;
#ifdef XS_REGION_CHECK /* XISTO_REGION_CHECK=1 at build time (main.c): prove at run time that nothing escaped */
static int xs_region_check = -1;
#define XS_REGION_CHECK_INIT() do { if (xs_region_check < 0) { const char *e_ = getenv("XISTO_REGION_CHECK"); xs_region_check = e_ && strcmp(e_, "1") == 0; } } while (0)
#else /* the checks fold away: no load and branch per allocation (binarytrees on the Xeon: -8% with the count below) */
#define xs_region_check 0
#define XS_REGION_CHECK_INIT() do { } while (0)
#endif
#define XS_RCHUNK (256 * 1024)
static __attribute__((noinline)) void *xs_region_more(xs_region *r, size_t size) {
    xs_rchunk *c = r->cur ? r->cur->next : r->first;
    while (c && c->size < size) c = c->next; /* a reused chunk too small for a big object: the next one */
    if (!c) {
        size_t sz = size > XS_RCHUNK ? size : XS_RCHUNK;
        c = malloc(sizeof(xs_rchunk) + sz);
        if (!c) xs_panic("out of memory");
        c->size = sz;
        c->next = NULL;
        if (!r->first) r->first = c;
        else { xs_rchunk *t = r->cur ? r->cur : r->first; while (t->next) t = t->next; t->next = c; }
    }
    r->cur = c;
    r->ptr = c->data;
    r->end = c->data + c->size;
    void *p = r->ptr;
    r->ptr += size;
    return p;
}
static __attribute__((noinline)) void xs_region_list(xs_region *r, void *p) {
    if (r->nlist == r->caplist) {
        r->caplist = r->caplist ? r->caplist * 2 : 1024;
        r->list = realloc(r->list, sizeof(void *) * r->caplist);
        if (!r->list) xs_panic("out of memory");
    }
    r->list[r->nlist++] = p;
}
static inline __attribute__((always_inline)) void *xs_region_alloc_f(xs_region *r, size_t size, const xs_type *type, uint32_t flags, bool zero) {
    size = (size + XS_OBJ_STEP - 1) & ~(XS_OBJ_STEP - 1);
    void *p;
    if (__builtin_expect(r->ptr && (size_t)(r->end - r->ptr) >= size, 1)) { p = r->ptr; r->ptr += size; }
    else p = xs_region_more(r, size);
    if (zero) memset(p, 0, size); /* (not when the constructor writes every field: binarytrees -5% on the Xeon) */
    xs_hdr *h = p;
    h->rc = 1;
    h->cls = flags | XS_TIX(type);
    if (type->drop || xs_region_check > 0) {
        if (__builtin_expect(r->nlist < r->caplist, 1)) r->list[r->nlist++] = p;
        else xs_region_list(r, p);
    }
#ifdef XS_REGION_CHECK
    r->nobj++;
#endif
    return p;
}
#define xs_region_alloc(r, size, type) xs_region_alloc_f((r), (size), (type), 0, true)
/* objects without a header (emit.c nohdr_compute): a bump in the region, nothing written; outside the region, an
   arena that is never given back (the emitter proved those objects are made a bounded number of times) */
static inline __attribute__((always_inline)) void *xs_region_alloc_nh(xs_region *r, size_t size) {
    size = (size + XS_OBJ_STEP - 1) & ~(XS_OBJ_STEP - 1);
    void *p;
    if (__builtin_expect(r->ptr && (size_t)(r->end - r->ptr) >= size, 1)) { p = r->ptr; r->ptr += size; }
    else p = xs_region_more(r, size);
    return p;
}
static XS_TLS char *xs_nh_p, *xs_nh_end;
static void *xs_nh_heap(size_t size) {
    size = (size + XS_OBJ_STEP - 1) & ~(XS_OBJ_STEP - 1);
    if (__builtin_expect(!xs_nh_p || (size_t)(xs_nh_end - xs_nh_p) < size, 0)) {
        size_t n = size > ((size_t)1 << 20) ? size : ((size_t)1 << 20);
        xs_nh_p = malloc(n);
        if (!xs_nh_p) xs_panic("out of memory");
        xs_nh_end = xs_nh_p + n;
    }
    void *p = xs_nh_p;
    xs_nh_p += size;
    return p;
}
/* ---- compressed references (emit.c cptr_compute) ----
   The objects of a header-less class whose fields refer only to its own objects come from memory below 32 GB,
   8-byte aligned: a reference is then the address shifted right by 3, in 32 bits (0 is null), and decoding it is a
   shift, no base to add. That memory is reserved with mmap at addresses asked for (from 16 GB up: macOS keeps the
   first 4 GB unmapped and its shared cache below 12 GB), in segments that are never given back, as the regions'
   chunks and xs_nh_heap's blocks never are. Windows and the sanitizers (which own the low addresses) keep full
   pointers: XS_CPREF is then the pointer itself. */
#if !defined(_WIN32) && UINTPTR_MAX > 0xffffffffu && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__) && !defined(XS_NO_CPTR)
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#define XS_CP_FULL 1
#endif
#endif
#else
#define XS_CP_FULL 1
#endif
#ifndef XS_CP_FULL
#include <sys/mman.h>
#include <pthread.h>
#define XS_CPREF(T) uint32_t
#define XS_CP_ENC(p) ((uint32_t)((uintptr_t)(p) >> 3))
#define XS_CP_DEC(T, v) ((T *)(uintptr_t)((uint64_t)(v) << 3))
#define XS_CP_LIMIT ((uintptr_t)32 << 30) /* what 32 bits of 8-byte steps reach */
#define XS_CP_SEG ((size_t)64 << 20)
static pthread_mutex_t xs_cp_mu = PTHREAD_MUTEX_INITIALIZER;
static char *xs_cp_p, *xs_cp_end; /* the segment being carved */
static uintptr_t xs_cp_hint = (uintptr_t)16 << 30;
/* n bytes (a multiple of 16) below XS_CP_LIMIT, never given back; any thread */
XS_NOINLINE void *xs_cp_alloc(size_t n) {
    pthread_mutex_lock(&xs_cp_mu);
    if (!xs_cp_p || (size_t)(xs_cp_end - xs_cp_p) < n) {
        size_t seg = n > XS_CP_SEG ? (n + XS_CP_SEG - 1) / XS_CP_SEG * XS_CP_SEG : XS_CP_SEG;
        char *got = NULL;
        for (int tries = 0; !got && tries < 1024; tries++) {
            if (xs_cp_hint + seg > XS_CP_LIMIT) xs_cp_hint = (uintptr_t)4 << 30; /* then from 4 GB up */
            void *m = mmap((void *)xs_cp_hint, seg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
            if (m == MAP_FAILED) break;
            if ((uintptr_t)m + seg <= XS_CP_LIMIT) got = m; /* (where asked, or anywhere else low enough) */
            else munmap(m, seg);
            xs_cp_hint = got ? (uintptr_t)got + seg : xs_cp_hint + seg;
        }
        if (!got) { pthread_mutex_unlock(&xs_cp_mu); xs_panic("out of memory (no room below 32 GB for compact objects; XISTO_NO_CPTR=1 builds without them)"); }
        xs_cp_p = got;
        xs_cp_end = got + seg;
    }
    void *r = xs_cp_p;
    xs_cp_p += n;
    pthread_mutex_unlock(&xs_cp_mu);
    return r;
}
/* chunks given back (xs_region_give_back_cp): their pages returned to the system at once, their addresses kept
   for the next chunks of the same size */
static void **xs_cp_spare;
static size_t xs_cp_nspare, xs_cp_cspare;
static void xs_cp_free_chunk(void *p, size_t n) {
#if defined(__linux__) && defined(MADV_DONTNEED)
    madvise(p, n, MADV_DONTNEED);
#else
    mmap(p, n, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0); /* (fresh pages in place: the old ones go) */
#endif
    if (n != XS_RCHUNK) return;
    pthread_mutex_lock(&xs_cp_mu);
    if (xs_cp_nspare == xs_cp_cspare) {
        size_t c = xs_cp_cspare ? xs_cp_cspare * 2 : 256;
        void **a = realloc(xs_cp_spare, sizeof(void *) * c);
        if (a) { xs_cp_spare = a; xs_cp_cspare = c; }
    }
    if (xs_cp_nspare < xs_cp_cspare) xs_cp_spare[xs_cp_nspare++] = p;
    pthread_mutex_unlock(&xs_cp_mu);
}
static void *xs_cp_chunk(size_t n) {
    if (n == XS_RCHUNK && xs_cp_nspare) {
        void *r = NULL;
        pthread_mutex_lock(&xs_cp_mu);
        if (xs_cp_nspare) r = xs_cp_spare[--xs_cp_nspare];
        pthread_mutex_unlock(&xs_cp_mu);
        if (r) return r;
    }
    return xs_cp_alloc(n);
}
#else
#define XS_CPREF(T) T *
#define XS_CP_ENC(p) (p)
#define XS_CP_DEC(T, v) ((T *)(v))
static void *xs_cp_alloc(size_t n) {
    void *r = malloc(n);
    if (!r) xs_panic("out of memory");
    return r;
}
#define xs_cp_chunk xs_cp_alloc
static void xs_cp_free_chunk(void *p, size_t n) { (void)n; free(p); }
#endif
/* xs_region_more from xs_cp_alloc: a chunk of XS_RCHUNK bytes with its header (no malloc header past it: whole pages) */
static __attribute__((noinline)) void *xs_region_more_cp(xs_region *r, size_t size) {
    xs_rchunk *c = r->cur ? r->cur->next : r->first;
    while (c && c->size < size) c = c->next;
    if (!c) {
        size_t sz = size + sizeof(xs_rchunk) > XS_RCHUNK ? (size + 15) & ~(size_t)15 : XS_RCHUNK - sizeof(xs_rchunk);
        c = xs_cp_chunk(sizeof(xs_rchunk) + sz);
        c->size = sz;
        c->next = NULL;
        if (!r->first) r->first = c;
        else { xs_rchunk *t = r->cur ? r->cur : r->first; while (t->next) t = t->next; t->next = c; }
    }
    r->cur = c;
    r->ptr = c->data;
    r->end = c->data + c->size;
    void *p = r->ptr;
    r->ptr += size;
    return p;
}
static inline __attribute__((always_inline)) void *xs_region_alloc_nh_cp(xs_region *r, size_t size) {
    size = (size + XS_OBJ_STEP - 1) & ~(XS_OBJ_STEP - 1);
    void *p;
    if (__builtin_expect(r->ptr && (size_t)(r->end - r->ptr) >= size, 1)) { p = r->ptr; r->ptr += size; }
    else p = xs_region_more_cp(r, size);
    return p;
}
static XS_TLS char *xs_nhc_p, *xs_nhc_end;
static void *xs_nh_heap_cp(size_t size) {
    size = (size + XS_OBJ_STEP - 1) & ~(XS_OBJ_STEP - 1);
    if (__builtin_expect(!xs_nhc_p || (size_t)(xs_nhc_end - xs_nhc_p) < size, 0)) {
        size_t n = size > ((size_t)1 << 20) ? (size + 15) & ~(size_t)15 : ((size_t)1 << 20);
        xs_nhc_p = xs_cp_alloc(n);
        xs_nhc_end = xs_nhc_p + n;
    }
    void *p = xs_nhc_p;
    xs_nhc_p += size;
    return p;
}
#define xs_region_alloc_nz(r, size, type) xs_region_alloc_f((r), (size), (type), 0, false)
/* a dual class's object (region.c: the class is made both inside the loop and outside it): in the region while
   one of its loop's iterations runs (on), on the heap otherwise */
#define xs_region_alloc_dual(r, size, type) xs_region_alloc_f((r), (size), (type), XS_RGN, true)
#define xs_region_alloc_dual_nz(r, size, type) xs_region_alloc_f((r), (size), (type), XS_RGN, false)
static __attribute__((noinline)) void xs_region_reset(xs_region *r) {
    if (xs_region_check > 0) {
        for (size_t i = 0; i < r->nlist; i++) { xs_hdr *h = r->list[i]; const xs_type *t = xs_type_of(h); if (t->drop) t->drop(h); }
        for (size_t i = 0; i < r->nlist; i++) { xs_hdr *h = r->list[i]; const xs_type *t = xs_type_of(h); if (t->rref) t->rref(h); }
        for (size_t i = 0; i < r->nlist; i++) {
            xs_hdr *h = r->list[i];
            if (h->rc != 0) {
                fflush(stdout);
                fprintf(stderr, "xisto: region check: an object of a region class outlived its iteration (count %d); this is a bug in xisto\n", h->rc);
                exit(70);
            }
        }
    } else {
        for (size_t i = 0; i < r->nlist; i++) { xs_hdr *h = r->list[i]; xs_type_of(h)->drop(h); }
    }
    r->nlist = 0;
    xs_allocs += r->nobj; /* (never in xs_live: they leave together, here) */
    r->nobj = 0;
    r->cur = r->first;
    r->ptr = r->first ? r->first->data : NULL;
    r->end = r->first ? r->first->data + r->first->size : NULL;
}
/* after a statement whose objects of the region's classes die with it (emit.c once_compute): the region rewound
   and its chunks given back to the system (a later iteration of its loop on this thread makes new ones) */
XS_NOINLINE void xs_region_give_back(xs_region *r) {
    xs_region_reset(r);
    for (xs_rchunk *c = r->first, *n; c; c = n) { n = c->next; free(c); }
    r->first = r->cur = NULL;
    r->ptr = r->end = NULL;
}
XS_NOINLINE void xs_region_give_back_cp(xs_region *r) {
    xs_region_reset(r);
    for (xs_rchunk *c = r->first, *n; c; c = n) { n = c->next; xs_cp_free_chunk(c, sizeof(xs_rchunk) + c->size); }
    r->first = r->cur = NULL;
    r->ptr = r->end = NULL;
}
static XS_TLS xs_region *xs_rg_cur; /* the region of the loop running (regions do not nest) */
static void xs_region_enter(xs_region *r) {
    XS_REGION_CHECK_INIT();
    if (!r->listed) { r->listed = true; r->next = xs_regions; xs_regions = r; }
    xs_rg_cur = r;
    xs_region_reset(r); /* what a loop left by an exception still held */
}
static void *xs_region_bytes_cur(size_t n) {
    xs_region *r = xs_rg_cur;
    n = (n + XS_OBJ_STEP - 1) & ~(XS_OBJ_STEP - 1);
    if (r->ptr && (size_t)(r->end - r->ptr) >= n) { void *p = r->ptr; r->ptr += n; return p; }
    return xs_region_more(r, n);
}
static void xs_arr_ref_rfree(void *p) { (void)p; }
static void xs_arr_ref_rref(void *p) { /* the elements are objects of the region */
    xs_arr_ref *a = p;
    for (size_t i = 0; i < a->len; i++) if (a->data[i]) ((xs_hdr *)a->data[i])->rc--;
}
static const xs_type xs_arr_ref_rtype XS_TYPE_SEC = {xs_arr_ref_rfree, xs_print_xs_arr_ref, NULL, NULL, xs_arr_ref_rref};
static inline __attribute__((always_inline)) xs_arr_ref *xs_arr_ref_rnew(xs_region *r, void (*print_elem)(void *)) { /* [] */
    xs_rg_cur = r;
    xs_arr_ref *a = xs_region_alloc(r, sizeof *a + XS_INLINE_ELEMS * sizeof(void *), &xs_arr_ref_rtype);
    a->cap = XS_INLINE_ELEMS;
    a->data = (void **)(a + 1);
    a->print_elem = print_elem;
    return a;
}
static xs_arr_ref *xs_arr_ref_rlit(xs_region *r, void (*print_elem)(void *), int n, ...) { /* takes owned references */
    xs_rg_cur = r;
    size_t cap = n <= XS_INLINE_ELEMS ? XS_INLINE_ELEMS : (size_t)n;
    xs_arr_ref *a = xs_region_alloc(r, sizeof *a + cap * sizeof(void *), &xs_arr_ref_rtype);
    a->cap = cap;
    a->data = (void **)(a + 1);
    a->print_elem = print_elem;
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) a->data[a->len++] = va_arg(ap, void *);
    va_end(ap);
    return a;
}

static void xs_exit(void) {
    xs_out_drain(); /* (a lent part is released before the count below) */
    xs_before_exit();
    if (xs_rc_counting()) { xs_ar_drain(0); for (xs_region *r = xs_regions; r; r = r->next) xs_region_reset(r); xs_gc_collect(); }
    fflush(stdout);
    if (getenv("XISTO_RC_STATS")) {
        fprintf(stderr, "xisto: %lld allocations, %lld still alive at exit\n", (long long)xs_allocs, (long long)xs_live);
        if (xs_gc_runs) fprintf(stderr, "xisto: cycle collector: %lld objects in cycles freed, %lld runs\n", (long long)xs_gc_freed, (long long)xs_gc_runs);
    }
    xs_prof_report();
    if (xs_exit_code_) exit(xs_exit_code_);
}

/* ---- xisto:parallel: a pool of threads for the calls of fn (emit.c emit_par_variant) ----
   One job at a time: main publishes it, wakes the pool and takes part; every thread takes chunks of indices
   from a shared counter and runs body(ctx, i), which stores fn(i) in a slot of its own. A thrown error is
   caught per chunk and kept if its index is the lowest so far (later chunks are then skipped: the loop would
   never reach them). Objects made on a worker belong to main afterwards: its allocation counts move with them.
   Runtime state is per thread (XS_THREADS); the program's globals are shared, and nothing writes them while a
   job runs (the compiler proved fn does not). */
#ifdef XS_PARALLEL
#include <pthread.h>
#include <stdatomic.h>
typedef struct {
    void (*body)(void *ctx, int64_t i);
    bool (*body8)(void *ctx, int64_t i); /* rows i..i+7 at once, or false: run them one by one (jam, XB-083) */
    void *ctx;
    int64_t n, chunk;
    _Atomic int64_t next;
    _Atomic int64_t err_i; /* the lowest failing index (n: none) */
    void *err;             /* its error (owned), under xs_pool_mu */
    _Atomic int64_t live, allocs; /* what the workers allocated and did not free */
} xs_pool_job;
static pthread_mutex_t xs_pool_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t xs_pool_wake = PTHREAD_COND_INITIALIZER, xs_pool_done = PTHREAD_COND_INITIALIZER;
static xs_pool_job *xs_pool_cur;
static uint64_t xs_pool_gen;
static int xs_pool_nthreads = -1; /* workers besides main (-1: pool not made yet) */
static int xs_pool_busy;          /* workers still on the current job */
static XS_TLS bool xs_pool_inside; /* this thread is running a job: a nested parallel call runs as the loop */

static void xs_pool_chunks(xs_pool_job *j) {
    bool was = xs_pool_inside;
    xs_pool_inside = true;
    for (;;) {
        int64_t s = atomic_fetch_add(&j->next, j->chunk);
        if (s >= j->n || s > atomic_load(&j->err_i)) break;
        int64_t e = s + j->chunk < j->n ? s + j->chunk : j->n;
        volatile int64_t i = s;
        jmp_buf tb;
        size_t am = xs_ar_mark();
        xs_try_push(&tb);
        if (_setjmp(tb) == 0) {
            if (j->body8) /* (body8 never throws: false sends the rows to body, which reports the error where it is) */
                while (i < e) {
                    if (i + 7 < e && j->body8(j->ctx, i)) i += 8;
                    else j->body(j->ctx, i++);
                }
            for (; i < e; i++) j->body(j->ctx, i);
            xs_try_pop();
        } else {
            void *err = xs_caught();
            xs_ar_drain(am);
            pthread_mutex_lock(&xs_pool_mu);
            if (i < atomic_load(&j->err_i)) { xs_release(j->err); j->err = err; atomic_store(&j->err_i, (int64_t)i); }
            else xs_release(err);
            pthread_mutex_unlock(&xs_pool_mu);
        }
    }
    xs_pool_inside = was;
}
static void *xs_pool_worker(void *arg) {
    (void)arg;
    uint64_t seen = 0;
    for (;;) {
        pthread_mutex_lock(&xs_pool_mu);
        while (xs_pool_gen == seen) pthread_cond_wait(&xs_pool_wake, &xs_pool_mu);
        seen = xs_pool_gen;
        xs_pool_job *j = xs_pool_cur;
        pthread_mutex_unlock(&xs_pool_mu);
        int64_t live0 = xs_live, allocs0 = xs_allocs;
        xs_pool_chunks(j);
        atomic_fetch_add(&j->live, xs_live - live0);
        atomic_fetch_add(&j->allocs, xs_allocs - allocs0);
        xs_live = live0, xs_allocs = allocs0;
        pthread_mutex_lock(&xs_pool_mu);
        if (--xs_pool_busy == 0) pthread_cond_signal(&xs_pool_done);
        pthread_mutex_unlock(&xs_pool_mu);
    }
    return NULL;
}
/* the number of threads a job runs on: XISTO_THREADS, else one per core */
static int xs_pool_threads(void) {
    if (xs_pool_nthreads < 0) {
        const char *s = getenv("XISTO_THREADS");
        long n = s && *s ? strtol(s, NULL, 10) : sysconf(_SC_NPROCESSORS_ONLN);
        if (n < 1) n = 1;
        if (n > 256) n = 256;
        xs_pool_nthreads = 0;
        for (long k = 1; k < n; k++) {
            pthread_t t;
            pthread_attr_t at;
            pthread_attr_init(&at);
            pthread_attr_setstacksize(&at, (size_t)64 << 20); /* deep recursion as on the main thread */
            if (pthread_create(&t, &at, xs_pool_worker, NULL) == 0) { pthread_detach(t); xs_pool_nthreads++; }
            pthread_attr_destroy(&at);
        }
    }
    return xs_pool_nthreads + 1;
}
/* worth a job: more than one index, more than one thread, and not already inside one */
static bool xs_pool_worth(int64_t n) { return n > 1 && !xs_pool_inside && xs_pool_threads() > 1; }
/* runs body(ctx, i) for every i in [0, n) on the pool; afterwards j->err_i < n tells the failing index */
static void xs_pool_run8(xs_pool_job *j, int64_t n, void (*body)(void *, int64_t), bool (*body8)(void *, int64_t), void *ctx) {
    int t = xs_pool_threads();
    j->body8 = body8, j->body = body, j->ctx = ctx, j->n = n, j->err = NULL;
    int64_t c = n / ((int64_t)t * 16);
    j->chunk = c < 1 ? 1 : c;
    if (body8) j->chunk = (j->chunk + 7) & ~(int64_t)7; /* whole groups of eight rows */
    atomic_store(&j->next, 0);
    atomic_store(&j->err_i, n);
    atomic_store(&j->live, 0);
    atomic_store(&j->allocs, 0);
    pthread_mutex_lock(&xs_pool_mu);
    xs_pool_cur = j;
    xs_pool_busy = xs_pool_nthreads;
    xs_pool_gen++;
    pthread_cond_broadcast(&xs_pool_wake);
    pthread_mutex_unlock(&xs_pool_mu);
    xs_pool_chunks(j);
    pthread_mutex_lock(&xs_pool_mu);
    while (xs_pool_busy > 0) pthread_cond_wait(&xs_pool_done, &xs_pool_mu);
    pthread_mutex_unlock(&xs_pool_mu);
    xs_live += atomic_load(&j->live);
    xs_allocs += atomic_load(&j->allocs);
}
static void xs_pool_run(xs_pool_job *j, int64_t n, void (*body)(void *, int64_t), void *ctx) {
    xs_pool_run8(j, n, body, NULL, ctx);
}
/* the replay closure: the module's own loop runs again on main, and its fn hands back the value already made for
   index i, or throws the error of the failing index there (so what happens before it happens as in the loop) */
typedef struct { xs_closure base; xs_pool_job *job; void *slots; size_t slot_size; void (*drop)(void *slot); } xs_pool_replay;
XS_NOINLINE void xs_pool_fail(xs_pool_replay *r, int64_t i) {
    xs_pool_job *j = r->job;
    if (r->drop) for (int64_t k = i + 1; k < j->n; k++) r->drop((char *)r->slots + (size_t)k * r->slot_size);
    free(r->slots);
    r->slots = NULL;
    void *err = j->err;
    j->err = NULL;
    xs_throw(err);
}
#endif
