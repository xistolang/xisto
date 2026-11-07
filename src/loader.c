/* xisto run --dev on macOS: the program runs inside this process, loaded the way a JIT loads code.

   macOS checks every newly created executable file the first time it runs (~150 ms, even for an empty
   program), and a dlopen'ed library just the same. A dev loop pays that on every edit; Bun does not, because
   it never makes a file it executes: its code is generated into memory. Here too: the program is linked as a
   dylib, its segments are copied into anonymous memory at their relative addresses, its fixups applied (its
   pointers to itself moved to where it now is, its imports looked up in the C library with dlsym), its text
   made read+execute, and its main called. Release builds are ordinary executables.

   Supported: Mach-O 64 dylibs with chained fixups (what Apple's linker makes for macOS 12+), pointer formats
   DYLD_CHAINED_PTR_64 / _64_OFFSET, imports with or without addends, initializers (pointers or offsets).
   Anything else (thread-local variables, other formats) returns -1 and the caller runs an executable. */
#include "xisto.h" /* stdio, stdlib, string */

#if defined(__APPLE__)
#include <dlfcn.h>
#include <libkern/OSCacheControl.h>
#include <mach-o/fixup-chains.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <sys/mman.h>
#include <unistd.h>

#define DECLINE(why) do { if (getenv("XISTO_LOADER_DEBUG")) fprintf(stderr, "xisto: loader: %s\n", why); return -1; } while (0)
typedef struct { uint64_t vmaddr, vmsize, fileoff, filesize; int prot; char name[17]; } LSeg;

static uint64_t lsym(void *lib, const char *name, bool weak) {
    if (name[0] == '_') name++;
    void *p = lib ? dlsym(lib, name) : NULL;
    if (!p) p = dlsym(RTLD_DEFAULT, name);
    if (!p && !weak) {
        if (getenv("XISTO_LOADER_DEBUG")) fprintf(stderr, "xisto: loader: no symbol %s\n", name);
        return UINT64_MAX;
    }
    return (uint64_t)(uintptr_t)p;
}

int xs_run_in_process(const char *path, int argc, char **argv) {
    FILE *f = fopen(path, "rb");
    if (!f) DECLINE("case 1");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc((size_t)n);
    if (!file || fread(file, 1, (size_t)n, f) != (size_t)n) { fclose(f); DECLINE("case 2"); }
    fclose(f);
    struct mach_header_64 *mh = (struct mach_header_64 *)file;
    if (mh->magic != MH_MAGIC_64 || mh->filetype != MH_DYLIB) DECLINE("case 3");

    LSeg segs[16];
    int nseg = 0;
    const struct linkedit_data_command *fixups = NULL;
    const struct symtab_command *symtab = NULL;
    struct section_64 *inits[8];
    int ninits = 0;
    uint8_t *lc = file + sizeof *mh;
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        struct load_command *c = (struct load_command *)lc;
        if (c->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *s = (struct segment_command_64 *)c;
            if (strcmp(s->segname, "__LINKEDIT") != 0) {
                if (nseg == 16) DECLINE("case 4");
                LSeg *g = &segs[nseg++];
                g->vmaddr = s->vmaddr; g->vmsize = s->vmsize; g->fileoff = s->fileoff; g->filesize = s->filesize; g->prot = s->initprot;
                memcpy(g->name, s->segname, 16);
                g->name[16] = 0;
            }
            struct section_64 *sec = (struct section_64 *)(s + 1);
            for (uint32_t k = 0; k < s->nsects; k++) {
                uint32_t type = sec[k].flags & SECTION_TYPE;
                if (type == S_THREAD_LOCAL_VARIABLES || type == S_THREAD_LOCAL_REGULAR || type == S_THREAD_LOCAL_ZEROFILL) DECLINE("case 5");
                if (type == S_MOD_INIT_FUNC_POINTERS || type == S_INIT_FUNC_OFFSETS) { if (ninits == 8) DECLINE("case 6"); inits[ninits++] = &sec[k]; }
            }
        } else if (c->cmd == LC_DYLD_CHAINED_FIXUPS) fixups = (const struct linkedit_data_command *)c;
        else if (c->cmd == LC_DYLD_INFO || c->cmd == LC_DYLD_INFO_ONLY) {
            const struct dyld_info_command *d = (const struct dyld_info_command *)c;
            if (d->rebase_size || d->bind_size || d->lazy_bind_size || d->weak_bind_size) DECLINE("case 7"); /* the old format */
        } else if (c->cmd == LC_SYMTAB) symtab = (const struct symtab_command *)c;
        lc += c->cmdsize;
    }
    if (!nseg || !symtab) DECLINE("case 8");
    uint64_t lo = segs[0].vmaddr, hi = 0;
    for (int i = 0; i < nseg; i++) { if (segs[i].vmaddr < lo) lo = segs[i].vmaddr; if (segs[i].vmaddr + segs[i].vmsize > hi) hi = segs[i].vmaddr + segs[i].vmsize; }
    size_t size = (size_t)(hi - lo);
    uint8_t *base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) DECLINE("case 9");
    for (int i = 0; i < nseg; i++) memcpy(base + (segs[i].vmaddr - lo), file + segs[i].fileoff, (size_t)segs[i].filesize);
    #define AT(vm) (base + ((vm) - lo))

    void *libsys = dlopen("/usr/lib/libSystem.B.dylib", RTLD_NOLOAD | RTLD_LAZY);
    if (fixups) {
        const uint8_t *fx = file + fixups->dataoff;
        const struct dyld_chained_fixups_header *h = (const void *)fx;
        if (h->fixups_version != 0 || h->symbols_format != 0) DECLINE("case 10");
        uint64_t *imp = calloc(h->imports_count ? h->imports_count : 1, sizeof(uint64_t));
        int64_t *add = calloc(h->imports_count ? h->imports_count : 1, sizeof(int64_t));
        const char *names = (const char *)fx + h->symbols_offset;
        for (uint32_t i = 0; i < h->imports_count; i++) {
            const char *nm;
            bool weak;
            if (h->imports_format == DYLD_CHAINED_IMPORT) {
                const struct dyld_chained_import *im = (const void *)(fx + h->imports_offset);
                nm = names + im[i].name_offset; weak = im[i].weak_import;
            } else if (h->imports_format == DYLD_CHAINED_IMPORT_ADDEND) {
                const struct dyld_chained_import_addend *im = (const void *)(fx + h->imports_offset);
                nm = names + im[i].name_offset; weak = im[i].weak_import; add[i] = im[i].addend;
            } else if (h->imports_format == DYLD_CHAINED_IMPORT_ADDEND64) {
                const struct dyld_chained_import_addend64 *im = (const void *)(fx + h->imports_offset);
                nm = names + im[i].name_offset; weak = im[i].weak_import; add[i] = (int64_t)im[i].addend;
            } else DECLINE("case 11");
            imp[i] = lsym(libsys, nm, weak);
            if (imp[i] == UINT64_MAX) DECLINE("case 12");
        }
        const struct dyld_chained_starts_in_image *si = (const void *)(fx + h->starts_offset);
        for (uint32_t s = 0; s < si->seg_count; s++) {
            if (!si->seg_info_offset[s]) continue;
            const struct dyld_chained_starts_in_segment *ss = (const void *)((const uint8_t *)si + si->seg_info_offset[s]);
            uint16_t fmt = ss->pointer_format;
            if (fmt != DYLD_CHAINED_PTR_64 && fmt != DYLD_CHAINED_PTR_64_OFFSET) DECLINE("case 13");
            for (uint16_t pg = 0; pg < ss->page_count; pg++) {
                uint16_t start = ss->page_start[pg];
                if (start == DYLD_CHAINED_PTR_START_NONE) continue;
                if (start & DYLD_CHAINED_PTR_START_MULTI) DECLINE("case 14");
                uint8_t *p = base + ss->segment_offset + (uint64_t)pg * ss->page_size + start;
                for (;;) {
                    uint64_t v;
                    memcpy(&v, p, 8);
                    uint64_t next;
                    uint64_t out;
                    if (v >> 63) { /* bind */
                        struct dyld_chained_ptr_64_bind b;
                        memcpy(&b, &v, 8);
                        if (b.ordinal >= h->imports_count) DECLINE("case 15");
                        out = imp[b.ordinal] + (uint64_t)((int64_t)b.addend + add[b.ordinal]);
                        next = b.next;
                    } else { /* rebase */
                        struct dyld_chained_ptr_64_rebase r;
                        memcpy(&r, &v, 8);
                        uint64_t target = fmt == DYLD_CHAINED_PTR_64 ? r.target - lo : r.target;
                        out = (uint64_t)(uintptr_t)base + target + ((uint64_t)r.high8 << 56);
                        next = r.next;
                    }
                    memcpy(p, &out, 8);
                    if (!next) break;
                    p += next * 4;
                }
            }
        }
        free(imp);
        free(add);
    }
    /* protections as the file asks: text read+execute, constants read-only once fixed */
    for (int i = 0; i < nseg; i++) {
        int prot = 0;
        if (segs[i].prot & VM_PROT_READ) prot |= PROT_READ;
        if (segs[i].prot & VM_PROT_WRITE) prot |= PROT_WRITE;
        if (segs[i].prot & VM_PROT_EXECUTE) prot |= PROT_EXEC;
        if (prot & PROT_EXEC) sys_icache_invalidate(AT(segs[i].vmaddr), (size_t)segs[i].vmsize);
        if (segs[i].vmsize && mprotect(AT(segs[i].vmaddr), (size_t)segs[i].vmsize, prot) != 0) DECLINE("case 16");
    }
    /* main, from the symbol table */
    const struct nlist_64 *sym = (const void *)(file + symtab->symoff);
    const char *str = (const char *)file + symtab->stroff;
    int (*entry)(int, char **, char **) = NULL;
    for (uint32_t i = 0; i < symtab->nsyms; i++)
        if ((sym[i].n_type & N_TYPE) == N_SECT && strcmp(str + sym[i].n_un.n_strx, "_main") == 0) { entry = (void *)AT(sym[i].n_value); break; }
    if (!entry) DECLINE("case 17");
    extern char **environ;
    for (int i = 0; i < ninits; i++) {
        struct section_64 *s = inits[i];
        if ((s->flags & SECTION_TYPE) == S_MOD_INIT_FUNC_POINTERS) {
            void (**fp)(int, char **, char **) = (void *)AT(s->addr);
            for (uint64_t k = 0; k < s->size / 8; k++) fp[k](argc, argv, environ);
        } else {
            uint32_t *off = (void *)AT(s->addr);
            for (uint64_t k = 0; k < s->size / 4; k++) ((void (*)(int, char **, char **))(base + off[k]))(argc, argv, environ);
        }
    }
    free(file);
    fflush(stdout);
    exit(entry(argc, argv, environ));
}
#else
int xs_run_in_process(const char *path, int argc, char **argv) { (void)path; (void)argc; (void)argv; return -1; }
#endif
