#include "xisto.h"
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include <time.h>
#include <limits.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/wait.h>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#if defined(__APPLE__)
#define XS_LINK_FLAGS "-Wl,-dead_strip -Wno-incompatible-pointer-types-discards-qualifiers"
/* clang: no hot/cold splitting (an outlined error path is an extra machine frame stack traces would misread) */
#define XS_CLANG_FLAGS "-mllvm -hot-cold-split=false -mno-outline" /* no machine outliner: it turned nbody's repeated index checks into calls in the hot loop (1.87 -> 0.73 s) */
#define XS_LINK_LIBS ""
#define XS_CXX_LIB "-lc++"
#else
/* frame pointers: stack traces walk them (the arm64 macOS ABI always keeps them). (No gcc-only warning flag
   here: gcc gets -w, and clang would warn about it, and that warning would reach stack traces and the debugger) */
/* -fno-math-errno: the macOS default; without it sqrt() keeps an errno path and a call, and the magnitudes of
   nbody-4 stop vectorizing on Linux (Graviton2 6,29 -> 4,90 s with the same C). Node's Math.sqrt(-1) is NaN
   either way; the runtime never reads errno after a math call. */
#define XS_LINK_FLAGS "-fno-omit-frame-pointer -fno-math-errno -ffunction-sections -fdata-sections -Wl,--gc-sections"
/* clang on Linux (preferred when installed): the same tuning as on macOS */
#define XS_CLANG_FLAGS "-mllvm -hot-cold-split=false -mno-outline -Wno-incompatible-pointer-types-discards-qualifiers"
#define XS_LINK_LIBS "-lpthread"
#define XS_CXX_LIB "-lstdc++"
#endif
/* the system and processor the caches are for: a project directory can be shared (a mount, a copied tree) */
#if defined(__APPLE__)
#define XS_HOST_OS "darwin"
#elif defined(__linux__)
#define XS_HOST_OS "linux"
#elif defined(_WIN32)
#define XS_HOST_OS "windows"
#else
#define XS_HOST_OS "unix"
#endif
#if defined(__aarch64__)
#define XS_HOST_ARCH "arm64"
#elif defined(__x86_64__)
#define XS_HOST_ARCH "x64"
#else
#define XS_HOST_ARCH "cpu"
#endif
#define XS_HOST XS_HOST_OS "-" XS_HOST_ARCH
#ifdef XS_CLANG_FLAGS
#define XS_CC_KEY XS_LINK_FLAGS " " XS_CLANG_FLAGS " -O3"
#else
#define XS_CC_KEY XS_LINK_FLAGS " " XS_CLANG_FLAGS " -O3"
#endif
static bool show_time;
static double now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }

extern const unsigned char xs_runtime_h[];
extern const unsigned char xs_runtime_net_h[];
extern const unsigned char xs_runtime_sqlite_h[];
extern const unsigned char xs_runtime_tls_h[];
extern const unsigned char xs_runtime_tls_awslc_h[];
extern const unsigned char xs_runtime_crypto_h[];
extern const unsigned char xs_runtime_regex_h[];
extern const unsigned char xs_runtime_regex_bt_h[];
static void regex_with_bt(Buf *b);
extern const unsigned char xs_runtime_dbg_json_h[];
extern const unsigned char xs_runtime_dbg_h[];
/* the runtime split in two (tools/rtsplit.py): what a program includes, and the library compiled once */
#define XS_RT_PAIR(n) extern const unsigned char xs_runtime_##n##_h_decl[], xs_runtime_##n##_h_impl[];
XS_RT_PAIR(rt) XS_RT_PAIR(net) XS_RT_PAIR(sqlite) XS_RT_PAIR(tls) XS_RT_PAIR(tls_awslc) XS_RT_PAIR(crypto) XS_RT_PAIR(regex)
static bool split_rt;     /* the runtime is a cached object, the program only includes its declarations */
static Buf rt_src;        /* the runtime's translation unit for this program's configuration */
int cmd_debug(int argc, char **argv); /* src/debug.c */

static char xisto_dir[1024]; /* directory of the xisto executable: qbe and the runtime object live there */

/* TLS backend, linked only when the program can use https (see url_scheme in the checker).
   AWS-LC (TLS 1.3, assembly crypto) is the default when it was built next to xisto (make: build/awslc);
   BearSSL (TLS 1.2, pure C, ~250 KB) is the fallback. --tls=bearssl|awslc or XISTO_TLS choose explicitly;
   XISTO_AWSLC points at another AWS-LC install (include/, lib/). */
static const char *tls_choice;
/* --target=linux-arm64|linux-x64: a static Linux executable (musl), built with `zig cc` from any host */
static const char *xs_target;
static bool build_dylib; /* xisto run --dev on macOS: the program as a library this process loads (loader.c) */
static const char *xs_cpu; /* --cpu=native (XISTO_CPU=native): code for this machine's processor, as Rust's
                             -C target-cpu=native: AVX2 on x86, the core's extensions on arm; the default stays
                             portable (an executable built for one CPU may not run on an older one) */
static bool in_process;  /* ... and so built without per-thread state: one event loop, plain globals */
int xs_run_in_process(const char *path, int argc, char **argv);
static const char *awslc_dir(void) {
    const char *d = getenv("XISTO_AWSLC");
    if (d && *d) return d;
    static char buf[1100];
    snprintf(buf, sizeof buf, "%s/awslc", xisto_dir);
    char lib[1200];
    snprintf(lib, sizeof lib, "%s/lib/libssl.a", buf);
    return access(lib, R_OK) == 0 ? buf : NULL;
}
static bool tls_awslc(void) {
    if (xs_target) return false; /* another system: BearSSL, compiled for it from source */
    const char *v = tls_choice ? tls_choice : getenv("XISTO_TLS");
    if (v && strcmp(v, "bearssl") == 0) return false;
    if (awslc_dir()) return true;
    if (v && strcmp(v, "awslc") == 0) {
        fprintf(stderr, "xisto: AWS-LC is not installed next to xisto (build it with `make awslc`, or set XISTO_AWSLC)\n");
        exit(2);
    }
    return false;
}

static bool link_tls;
static bool link_sqlite;

#ifndef XISTO_VERSION
#define XISTO_VERSION "dev"
#endif
static void usage(void) {
    fprintf(stderr,
            "usage:\n"
            "  xisto build [--dev] [--backend=c|qbe] [-o OUT] FILE.ts   compile to a native executable\n"
            "    --target=linux-arm64|linux-x64|windows-x64             an executable for another system, from any host (needs zig)\n"
            "  xisto emit [--backend=c|qbe] FILE.ts                     print the generated C or QBE IL\n"
            "  xisto run [--dev] [--backend=c|qbe] FILE.ts [ARGS...]    compile to a temp file and run it\n"
            "  xisto debug [--batch CMDS] FILE.ts [ARGS...]             run under the debugger (docs/debugger.md)\n"
            "  xisto dap                                                debug adapter for IDEs (DAP over stdin/stdout)\n"
            "  xisto clean [DIR]                                        remove the build cache (.xisto/cache) of a project\n"
            "  xisto fmt [--check] FILE.ts...                           format in place (--check: exit 1 if any file would change)\n"
            "  xisto check [--details] [--deny-warnings] [--tsc-checked] [--pkgs DIR] [PATH...]  what a project needs to change to compile with xisto\n"
            "                                                           (PATH: a folder, a tsconfig.json or .ts files; --details: every place)\n"
            "  xisto test [DIR]                                         build and run every *.test.ts under DIR\n"
            "  xisto add SOURCE[@REF] [--name NAME]                     add a package from a git URL or path (source, locked by commit)\n"
            "  xisto install                                            fetch every package at the commit in xisto.lock\n"
            "\n"
            "  --dev          fast compile, no optimization (release is the default)\n"
            "  --debug        a debuggable binary: -O0 -g, statement hooks and the debugger agent (never in release)\n"
            "  --json         report each error and warning as one JSON object per line on stderr (editors, AI agents)\n"
            "  --no-warnings  do not report unused variables and functions\n"
            "  --warnings     list each rule warning (undefined read as null, int mixed with number...); a build prints their count\n"
            "  --deny-warnings  rule warnings are errors (CI); also for xisto check\n"
            "  --profile      count calls and time per function; the table goes to stderr at exit (C backend)\n"
            "  --backend=qbe  generate QBE IL and machine code directly, without C\n"
            "  --cpu=native   code for this machine's processor (AVX2, ...), as Rust's target-cpu=native; XISTO_CPU=native\n"
            "  --node-compat  accept node:fs, node:sqlite... as names of xisto's own modules (xisto:fs...)\n"
            "  --tsc-checked  the code already passes tsc --strict: what tsc accepted compiles, checked at run time, with a\n"
            "                 warning (T | null where T goes XS2107, unknown where T goes XS2108, number where int goes XS2109);\n"
            "                 XISTO_TSC_CHECKED=1; also for xisto check\n"
            "  --tls=off      never link TLS: fetch URLs only known at run time must be http:// (TLS at the load balancer)\n"
            "  --tls=bearssl  use BearSSL (TLS 1.2, smaller binary) instead of AWS-LC (TLS 1.3, the default)\n"
            "\n"
            "TLS is linked only when the program can use https: an https:// URL, a fetch URL known only at run time,\n"
            "or listen(port, { cert, key }). A program that only calls http:// URLs carries no TLS code.\n");
    exit(2);
}

static bool use_qbe;

static void find_xisto_dir(const char *argv0) {
    char buf[1024];
    uint32_t n = sizeof buf;
#if defined(__APPLE__)
    if (_NSGetExecutablePath(buf, &n) != 0) strncpy(buf, argv0, sizeof buf - 1);
#elif defined(_WIN32)
    DWORD len = GetModuleFileNameA(NULL, buf, sizeof buf);
    if (len == 0 || len >= sizeof buf) strncpy(buf, argv0, sizeof buf - 1);
    for (char *c = buf; *c; c++) if (*c == '\\') *c = '/'; /* forward slashes everywhere */
    (void)n;
#else
    ssize_t len = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (len > 0) buf[len] = 0; else strncpy(buf, argv0, sizeof buf - 1);
    (void)n;
#endif
#if !defined(_WIN32)
    { /* through a symlink (bin/xisto -> build/xisto, Homebrew's links): the tools live next to the real file */
        char real[PATH_MAX];
        if (realpath(buf, real)) { strncpy(buf, real, sizeof buf - 1); buf[sizeof buf - 1] = 0; }
    }
#endif
    char *slash = strrchr(buf, '/');
    if (slash) *slash = 0;
    else strcpy(buf, ".");
    strncpy(xisto_dir, buf, sizeof xisto_dir - 1);
}

static char *shell_output(const char *cmd) {
#if defined(_WIN32)
    FILE *f = popen(xs_sprintf("\"%s\"", cmd), "r"); /* (cmd.exe drops the line's first quote: see xs_sh) */
#else
    FILE *f = popen(cmd, "r");
#endif
    if (!f) return NULL;
    Buf b = {0};
    char tmp[256];
    size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) {
        for (size_t i = 0; i < n; i++) if (tmp[i] != '\n') buf_putc(&b, tmp[i]);
    }
    pclose(f);
    return b.data;
}

static uint64_t fnv(const char *s, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ull; }
    return h;
}

static bool file_exists(const char *path) { return access(path, R_OK) == 0; }

/* Stack traces on the QBE path: the IL has a `dbgloc <site>` before every call, which qbe prints as
   `.loc <file> <site>`. This drops the .file/.loc lines and puts a label right after each call instruction,
   recorded in the xs_sites section with the address of the unit's site table entry (runtime/xisto_rt.h). */
static bool qbe_label_calls(const char *asmf, const char *unit) {
    char *src = xs_read_file(asmf);
    if (!src) return false;
#if defined(__APPLE__)
    const char *sec = ".pushsection __DATA_CONST,__xs_sites,regular,no_dead_strip", *lp = "L", *sp = "_";
#else
    const char *sec = ".pushsection xs_sites,\"aw\"", *lp = ".L", *sp = "";
#endif
    Buf o = {0};
    int site = 0, k = 0;
    for (char *line = src; *line;) {
        char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        const char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        if (strncmp(t, ".file ", 6) == 0) { /* drop */ }
        else if (strncmp(t, ".section \"", 10) == 0) { /* qbe quotes section names; Apple's as does not take them */
            const char *q = t + 10, *e = strchr(q, '"');
            buf_printf(&o, ".section %.*s\n", e ? (int)(e - q) : (int)strlen(q), q);
        }
        else if (strncmp(t, ".loc ", 5) == 0) { unsigned f, l; if (sscanf(t + 5, "%u %u", &f, &l) == 2) site = (int)l; }
        else {
            buf_printf(&o, "%.*s\n", (int)len, line);
            bool call = strncmp(t, "bl\t", 3) == 0 || strncmp(t, "bl ", 3) == 0 || strncmp(t, "blr\t", 4) == 0 || strncmp(t, "blr ", 4) == 0
                     || strncmp(t, "call", 4) == 0;
            if (call && site > 0)
                buf_printf(&o, "%sxq%d:\n\t%s\n\t.p2align 3\n\t.quad %sxq%d\n\t.quad %sxs_sitetab_%s+%d\n\t.popsection\n",
                           lp, k, sec, lp, k, sp, unit, site * 32), k++;
            if (call) site = 0; /* every labelled call has its own dbgloc; anything else stays unlabelled */
        }
        if (!end) break;
        line = end + 1;
    }
    FILE *f = fopen(asmf, "wb");
    if (!f) return false;
    fwrite(o.data ? o.data : "", 1, o.len, f);
    fclose(f);
    free(src);
    free(o.data);
    return true;
}

/* QBE backend with a build cache: each unit's object is kept in .xisto/cache/qbe/<hash of the IL>.o
   next to the entry file, so a rebuild only runs qbe+as for units whose generated code changed. */
static void build_qbe_cached(Program *p, const char *entry, const char *outfile) {
    double t0 = now_ms();
    int n;
    Unit *units = emit_program_qbe_units(p, &n);
    if (show_time) fprintf(stderr, "xisto: emit %.1f ms (%d units)\n", now_ms() - t0, n);
    t0 = now_ms();
    char dir[1024];
    strncpy(dir, entry, sizeof dir - 1);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0; else strcpy(dir, ".");
    char *cache = xs_sprintf("%s/.xisto/cache/qbe", dir);
    if (system(xs_sprintf("mkdir -p '%s'", cache)) != 0) { fprintf(stderr, "xisto: cannot create %s\n", cache); exit(1); }
    Buf objs = {0};
    int compiled = 0;
    for (int i = 0; i < n; i++) {
        /* the key covers the IL text and the backend version */
        uint64_t h = fnv(units[i].text.data, units[i].text.len) ^ fnv("qbe-1.2/v3", 10) ^ (fnv(XS_HOST, strlen(XS_HOST)) * 3); /* v3: call labels, 32-byte sites */
        char *obj = xs_sprintf("%s/%016llx.o", cache, (unsigned long long)h);
        if (!file_exists(obj)) {
            char *ssa = xs_sprintf("%s/%016llx.ssa", cache, (unsigned long long)h);
            char *asmf = xs_sprintf("%s/%016llx.s", cache, (unsigned long long)h);
            FILE *f = fopen(ssa, "wb");
            if (!f) { fprintf(stderr, "xisto: cannot write %s\n", ssa); exit(1); }
            fwrite(units[i].text.data, 1, units[i].text.len, f);
            fclose(f);
            char *tmpobj = xs_sprintf("%s.tmp", obj);
            if (system(xs_sprintf("'%s/qbe' -o '%s' '%s'", xisto_dir, asmf, ssa)) != 0 || !qbe_label_calls(asmf, units[i].name)
                || system(xs_sprintf("as -o '%s' '%s' && mv '%s' '%s'", tmpobj, asmf, tmpobj, obj)) != 0) {
                fprintf(stderr, "xisto: qbe failed on unit %s (this is a bug in xisto; IL kept at %s)\n", units[i].name, ssa);
                exit(1);
            }
            unlink(asmf);
            unlink(ssa);
            compiled++;
        }
        buf_printf(&objs, " '%s'", obj);
    }
    if (show_time) fprintf(stderr, "xisto: qbe+as %.1f ms (%d of %d units compiled, %d from cache)\n", now_ms() - t0, compiled, n, n - compiled);
    t0 = now_ms();
#if defined(__APPLE__)
    char *sdk = shell_output("xcrun --sdk macosx --show-sdk-path 2>/dev/null");
    char *sdkver = shell_output("xcrun --sdk macosx --show-sdk-version 2>/dev/null");
    char *osver = shell_output("sw_vers -productVersion 2>/dev/null");
    const char *rt = xs_debug ? "xisto_rt_dbg.o" : "xisto_rt.o"; /* --debug: the runtime with the debugger agent */
    char *cmd = xs_sprintf("ld -dead_strip -o '%s'%s '%s/%s' -lSystem -lm -syslibroot '%s' -arch arm64 -platform_version macos %s %s",
                           outfile, objs.data, xisto_dir, rt, sdk ? sdk : "", osver && *osver ? osver : "14.0", sdkver && *sdkver ? sdkver : "14.0");
#else
    const char *rt = xs_debug ? "xisto_rt_dbg.o" : "xisto_rt.o";
    char *cmd = xs_sprintf("cc -no-pie -Wl,--gc-sections -o '%s'%s '%s/%s' -lm -lpthread", outfile, objs.data, xisto_dir, rt);
#endif
    if (system(cmd) != 0 || access(outfile, X_OK) != 0) { fprintf(stderr, "xisto: the linker failed\n"); exit(1); }
    if (show_time) fprintf(stderr, "xisto: ld %.1f ms\n", now_ms() - t0);
}

/* the code is for an x86 processor with 4-lane double vectors (AVX2): --cpu=native on such a machine, or a named
   x86-64-v3/v4 or Haswell-or-later target; never for another system (--target). Distribute's flat pass
   (vsqrtpd/vdivpd over the pairs) goes with pad4's columns of 4 lanes: alone, reading x y z from three columns, it
   measured slower on the Xeon E5-2686 v4 (nbody.ts 3,10 -> 4,24 s, 09/11); XISTO_WIDE_F64=0/1 forces it */
static bool cpu_avx2_target(void);
static bool cpu_wide_f64(void) {
    const char *f = getenv("XISTO_WIDE_F64");
    if (f && *f) return strcmp(f, "0") != 0;
    return xs_pad4; /* only with the x y z columns of 4 lanes (pad4): alone it was slower (nbody.ts 3,10 -> 4,24 s) */
}
/* pad4 (columns.c, pad4.c): x y z columns of 4 lanes, on the same targets; XISTO_PAD4=0/1 forces it */
static bool cpu_pad4(void) {
    const char *f = getenv("XISTO_PAD4");
    if (f && *f) return strcmp(f, "0") != 0;
    if (getenv("XISTO_NO_PAD4")) return false;
    return cpu_avx2_target();
}
static bool cpu_avx2_target(void) {
#if defined(__x86_64__)
    if (!xs_cpu || xs_target) return false;
    if (strcmp(xs_cpu, "native") == 0) return __builtin_cpu_supports("avx2");
    static const char *wide[] = {"x86-64-v3", "x86-64-v4", "haswell", "broadwell", "skylake", "skylake-avx512", "cascadelake",
                                 "icelake-client", "icelake-server", "alderlake", "sapphirerapids", "znver1", "znver2", "znver3", "znver4"};
    for (size_t i = 0; i < sizeof wide / sizeof *wide; i++) if (strcmp(xs_cpu, wide[i]) == 0) return true;
#endif
    return false;
}

static Program *last_program;

static Buf compile_to_c(const char *path) {
    double t0 = now_ms();
    Program *p = parse_program(path);
    last_program = p;
    inplace_program(p); /* (before the checker: a callee renamed) */
    if (!use_qbe) {
        xs_pad4 = cpu_pad4();
        columns_program(p); /* (the qbe backend takes no typed-array parameter yet) */
        pad4_program(p);
    }
    if (!use_qbe) {
        xs_wide_f64 = cpu_wide_f64();
        distribute_program(p); /* (after columns: it only touches loops over columns) */
    }
    double t1 = now_ms();
    check_program(p);
    double t2 = now_ms();
    prune_program(p);
    double t3 = now_ms();
    /* functions nobody can reach and nobody else can import */
    for (int i = 0; i < p->nfuncs; i++) {
        Func *f = p->funcs[i];
        if (f->used || f->exported || f->mod->builtin || f->name[0] == '_' || f->template_of || (f->ntparams && f->ninstances)) continue;
        const char *pk = getenv("XISTO_PKGS"); /* (--pkgs DIR) */
        if (f->mod->file && (strncmp(f->mod->file, "xisto:", 6) == 0 || strstr(f->mod->file, "/.xisto/pkgs/") || (pk && *pk && strncmp(f->mod->file, pk, strlen(pk)) == 0)))
            continue; /* a package's helpers, unused by this program */
        xs_file_name = f->mod->file;
        xs_source = f->mod->src;
        xs_warn(f->pos, "remove it, or export it if another module should use it", "function '%s' is never used", f->name);
    }
    if (show_time) {
        int used = 0;
        for (int i = 0; i < p->nfuncs; i++) used += p->funcs[i]->used;
        fprintf(stderr, "xisto: parse %.1f ms, check %.1f ms, prune %.1f ms (%d modules, %d functions, %d reachable)\n",
                t1 - t0, t2 - t1, t3 - t2, p->nmodules, p->nfuncs, used);
    }
    Buf b = {0};
    double t4 = now_ms();
    if (use_qbe) {
        emit_program_qbe(p, &b);
    } else {
        if (getenv("XISTO_NO_TRACE")) buf_puts(&b, "#define XS_NO_TRACE 1\n"); /* measuring knob: no stack-trace support */
        if (getenv("XISTO_REGION_CHECK") && strcmp(getenv("XISTO_REGION_CHECK"), "1") == 0) buf_puts(&b, "#define XS_REGION_CHECK 1\n"); /* regions: run-time proof (xisto_rt.h) */
        if (xs_debug) buf_puts(&b, "#define XS_DEBUG 1\n");
        if (p->uses_workers) in_process = false; /* (worker threads: an executable of its own, built with XS_THREADS) */
        if (p->uses_parallel && !xs_debug) par_analyze(p); /* (the debugger, like the QBE backend, runs the plain loops) */
        if (p->par_native) in_process = false; /* (xisto:parallel: a thread pool, so per-thread runtime state) */
        if ((p->uses_net || p->par_native) && !in_process) buf_puts(&b, "#define XS_THREADS 1\n"); /* servers may run worker threads */
        if (p->par_native) buf_puts(&b, "#define XS_PARALLEL 1\n");
        if (p->uses_tls) { buf_puts(&b, "#define XS_WITH_TLS 1\n"); link_tls = true; }
        if (p->uses_regex) buf_puts(&b, "#define XS_RT_REGEX 1\n");
        rt_src.len = 0;
        if (split_rt && !p->uses_rebt) { /* the same configuration for the library */
            buf_puts(&rt_src, b.data ? b.data : "");
#define XS_RT_ADD(n) do { buf_puts(&b, (const char *)xs_runtime_##n##_h_decl); buf_puts(&rt_src, (const char *)xs_runtime_##n##_h_impl); } while (0)
            XS_RT_ADD(rt);
            if (p->uses_net || p->uses_gen) XS_RT_ADD(net);
            if (p->uses_tls) { if (tls_awslc()) XS_RT_ADD(tls_awslc); else XS_RT_ADD(tls); }
            if (p->uses_sqlite) { XS_RT_ADD(sqlite); link_sqlite = true; }
            if (p->uses_crypto) XS_RT_ADD(crypto);
            if (p->uses_regex) XS_RT_ADD(regex);
#undef XS_RT_ADD
        } else {
            buf_puts(&b, (const char *)xs_runtime_h);
            if (xs_debug) { buf_puts(&b, (const char *)xs_runtime_dbg_json_h); buf_puts(&b, (const char *)xs_runtime_dbg_h); }
            if (p->uses_net || p->uses_gen) buf_puts(&b, (const char *)xs_runtime_net_h);
            if (p->uses_tls) buf_puts(&b, (const char *)(tls_awslc() ? xs_runtime_tls_awslc_h : xs_runtime_tls_h));
            if (p->uses_sqlite) { buf_puts(&b, (const char *)xs_runtime_sqlite_h); link_sqlite = true; }
            if (p->uses_crypto) buf_puts(&b, (const char *)xs_runtime_crypto_h);
            if (p->uses_regex && p->uses_rebt) regex_with_bt(&b);
            else if (p->uses_regex) buf_puts(&b, (const char *)xs_runtime_regex_h);
        }
        for (int i = 0; i < p->nnative; i++) { /* native packages: their C joins the program, after the runtime it builds on */
            char *src = xs_read_file(p->native_files[i]);
            if (!src) { fprintf(stderr, "xisto: cannot read native source %s\n", p->native_files[i]); exit(1); }
            buf_printf(&b, "\n/* ---- native package code ---- */\n#line 1 \"%s\"\n", p->native_files[i]);
            buf_puts(&b, src);
            buf_puts(&b, "\n");
        }
        if (p->nnative) { /* the program's own code: C diagnostics point back at the generated file, not the last package */
            size_t nl = 1;
            for (size_t i = 0; i < b.len; i++) nl += b.data[i] == '\n';
            buf_printf(&b, "#line %zu \"generated.c\"\n", nl + 1);
        }
        emit_program(p, &b);
    }
    if (show_time && !use_qbe) fprintf(stderr, "xisto: tls %s\n", !p->uses_tls ? "not linked" : tls_awslc() ? "AWS-LC" : "BearSSL");
    if (show_time) fprintf(stderr, "xisto: emit %.1f ms (%zu KB of %s)\n", now_ms() - t4, b.len / 1024, use_qbe ? "QBE IL" : "C");
    return b;
}


/* the linear RegExp engine with the backtracking one (runtime/xisto_regex_bt.h) beside it, for a program that has a
   pattern only backtracking can match: four places of xisto_regex.h are joined to it as the text is copied (the header
   itself stays as it is, so every other program's C is unchanged): the two compiles try the other engine for what the
   linear one refuses as needing backtracking, and a search of a pattern compiled by it is its search */
static void regex_rep(char **text, const char *from, const char *to) {
    char *at = strstr(*text, from);
    if (!at || strstr(at + strlen(from), from)) { fprintf(stderr, "xisto: internal error: runtime/xisto_regex.h changed (%.60s)\n", from); exit(70); }
    size_t pre = (size_t)(at - *text), fl = strlen(from), tl = strlen(to), tot = strlen(*text);
    char *o = malloc(tot - fl + tl + 1);
    memcpy(o, *text, pre);
    memcpy(o + pre, to, tl);
    memcpy(o + pre + tl, at + fl, tot - pre - fl + 1);
    free(*text);
    *text = o;
}
static void regex_with_bt(Buf *b) {
    char *t = strdup((const char *)xs_runtime_regex_h);
    regex_rep(&t, "xr_re *re = xr_compile(src->data, (size_t)src->len, f, &err);", "xr_re *re = xr_compile_any(src->data, (size_t)src->len, f, &err);");
    regex_rep(&t, "*slot = xr_compile(src, (size_t)n, f, &err);", "*slot = xr_compile_any(src, (size_t)n, f, &err);");
    regex_rep(&t, "XS_NOINLINE xs_RegExp *xs_RegExp_new(xs_str *src, xs_str *flags) {",
              "static xr_re *xr_compile_any(const char *src, size_t n, int f, const char **err);\nXS_NOINLINE xs_RegExp *xs_RegExp_new(xs_str *src, xs_str *flags) {");
    regex_rep(&t, "static bool xr_match_at(xs_RegExp *r, xs_str *s, int64_t from, bool sticky, xr_m *m, bool want_caps) {\n    m->more = NULL;",
              "static bool xrb_match_at(xs_RegExp *r, xs_str *s, int64_t from, bool sticky, xr_m *m, bool want_caps);\n"
              "static bool xr_match_at(xs_RegExp *r, xs_str *s, int64_t from, bool sticky, xr_m *m, bool want_caps) {\n    m->more = NULL;\n"
              "    if (r->re->fwd.n < 0) return xrb_match_at(r, s, from, sticky, m, want_caps);");
    regex_rep(&t, "if (!lg) return xr_test(r->re, s->data, (size_t)s->len, 0, false);",
              "if (!lg && r->re->fwd.n < 0) { xr_m m0_; bool ok_ = xr_match_at(r, s, 0, false, &m0_, false); free(m0_.more); return ok_; }\n"
              "    if (!lg) return xr_test(r->re, s->data, (size_t)s->len, 0, false);");
    /* (the backtracking engine goes inside the header's guard: before its last #endif) */
    char *e = strstr(t, "#endif");
    char *last = NULL;
    for (char *q = e; q; q = strstr(q + 1, "#endif")) last = q;
    buf_printf(b, "%.*s", (int)(last - t), t);
    buf_puts(b, (const char *)xs_runtime_regex_bt_h);
    buf_puts(b, last);
    free(t);
}
static char *strip_ts(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t n = strlen(base);
    if (n > 3 && strcmp(base + n - 3, ".ts") == 0) n -= 3;
    return xs_strndup(base, n);
}

/* is the C compiler clang? (cc is gcc on most Linux systems): the flags only clang knows go to clang only,
   and gcc's warnings about the generated C, which say nothing to the person writing TypeScript, are off */
/* the C compiler: XISTO_CC; else clang when it is on PATH (on Linux cc is usually gcc, and the generated code was
   tuned with clang: objects 0.145 -> 0.043 s, nbody 1.76 -> 1.42 s, bytes 0.45 -> 0.28 s on the same machine);
   else cc */
static const char *host_cc(void) {
    static const char *cc;
    if (cc) return cc;
    cc = getenv("XISTO_CC");
#if !defined(__APPLE__) /* (Apple's cc is clang) */
    if (!cc && xs_on_path("clang")) cc = "clang";
#endif
    if (!cc) cc = "cc";
    return cc;
}
static bool cc_is_clang(const char *cc) {
    static char last[512];
    static int known = -1;
    if (known >= 0 && strcmp(last, cc) == 0) return known;
#if defined(__APPLE__)
    if (strcmp(cc, "cc") == 0) return true; /* Apple's cc is clang: no process to ask (it cost every build ~30 ms) */
#endif
    snprintf(last, sizeof last, "%s", cc);
    known = 0;
    FILE *p = popen(xs_sprintf("%s --version 2>/dev/null", cc), "r");
    if (p) {
        char buf[512];
        while (fgets(buf, sizeof buf, p)) if (strstr(buf, "clang")) known = 1;
        pclose(p);
    }
    return known;
}
static void run_cc_raw(const char *cfile, const char *outfile, bool dev);
/* the C path's build cache: the executable is kept in .xisto/cache/c/<hash of the C text and the command>
   next to the entry file; an unchanged program (a test run again, a server restarted) is not compiled again */
static const char *cc_entry;
static const char *cc_text;
static size_t cc_len;
static void run_cc(const char *cfile, const char *outfile, bool dev) {
    if (!cc_entry || !cc_text || getenv("XISTO_NO_CACHE")) { run_cc_raw(cfile, outfile, dev); return; }
    double t0 = now_ms();
    char dir[1024];
    strncpy(dir, cc_entry, sizeof dir - 1);
    dir[sizeof dir - 1] = 0;
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0; else strcpy(dir, ".");
    char *cache = xs_sprintf("%s/.xisto/cache/c", dir);
    const char *cc = host_cc();
    uint64_t h = fnv(cc_text, cc_len) ^ (fnv(cc, strlen(cc)) * 31) ^ (dev ? 0x9e3779b97f4a7c15ull : 0) ^ (fnv(XS_HOST, strlen(XS_HOST)) * 3) ^
                 (xs_debug ? 0x51afd7ed558ccdull : 0) ^ (link_tls ? 0x2545f4914f6cdd1dull : 0) ^ (link_sqlite ? 0x27d4eb2f165667c5ull : 0) ^
                 (xs_target ? fnv(xs_target, strlen(xs_target)) * 7 : 0) ^ (build_dylib ? 0x165667b19e3779f9ull : 0) ^
                 fnv(XS_CC_KEY, strlen(XS_CC_KEY)) * 11 ^ /* the compiler flags this xisto passes: a new set rebuilds */
                 (xs_cpu ? fnv(xs_cpu, strlen(xs_cpu)) * 13 : 0);
    char *hit = xs_sprintf("%s/%016llx", cache, (unsigned long long)h);
    if (xs_exists(hit) && xs_copy_file(hit, outfile)) {
        if (show_time) fprintf(stderr, "xisto: cc from cache %.1f ms\n", now_ms() - t0);
        return;
    }
    run_cc_raw(cfile, outfile, dev);
    char *ctmp = xs_sprintf("%s.%d.tmp", hit, xs_pid());
    if (!xs_mkdirs(cache) || !xs_copy_file(outfile, ctmp) || !xs_rename(ctmp, hit)) unlink(ctmp); /* the cache is optional */
}
/* The runtime library for this configuration: compiled once (-O2) into $HOME/.cache/xisto/rt/<hash>.o and
   linked into every program that has the same configuration; the program's unit only declares it. */
static char *runtime_object(const char *cc) {
    if (!split_rt || rt_src.len == 0) return NULL;
    const char *home = getenv("HOME");
    char *cache = xs_sprintf("%s/.cache/xisto/rt", home ? home : "/tmp");
    uint64_t h = fnv(rt_src.data, rt_src.len) ^ (fnv(cc, strlen(cc)) * 31) ^ (fnv(XS_HOST, strlen(XS_HOST)) * 3);
    char *obj = xs_sprintf("%s/%016llx.o", cache, (unsigned long long)h);
    if (access(obj, R_OK) == 0) return obj;
    double t0 = now_ms();
    if (system(xs_sprintf("mkdir -p '%s'", cache)) != 0) return NULL;
    char *src = xs_sprintf("%s/%016llx.c", cache, (unsigned long long)h);
    FILE *f = fopen(src, "wb");
    if (!f) return NULL;
    fwrite(rt_src.data, 1, rt_src.len, f);
    fclose(f);
    const char *extra = "";
#ifdef XS_CLANG_FLAGS
    if (cc_is_clang(cc)) extra = XS_CLANG_FLAGS;
#endif
    if (!cc_is_clang(cc)) extra = "-w";
    char *inc = xs_sprintf("-I'%s'", xisto_dir);
    if (link_tls && tls_awslc()) inc = xs_sprintf("%s -I'%s/include'", inc, awslc_dir());
    else if (link_tls) inc = xs_sprintf("%s -I'%s/bearssl-inc'", inc, xisto_dir);
    char *tmp = xs_sprintf("%s.%d.tmp", obj, (int)getpid());
    char *cmd = xs_sprintf("%s -std=c11 -O2 %s -ffp-contract=off -Wno-unused-value -Wno-unused-variable -Wno-unused-function -Wno-parentheses-equality "
                           "-Wno-ignored-qualifiers -Wno-return-type -Wno-unused-command-line-argument %s %s -c -o '%s' '%s'",
                           cc, extra, XS_LINK_FLAGS, inc, tmp, src);
    if (system(cmd) != 0 || rename(tmp, obj) != 0) {
        fprintf(stderr, "xisto: the runtime library did not compile (this is a bug in xisto; source kept at %s)\n", src);
        exit(1);
    }
    unlink(src);
    if (show_time) fprintf(stderr, "xisto: runtime library compiled once for this configuration: %.1f ms\n", now_ms() - t0);
    return obj;
}
/* --dev: the program's C in codegen units compiled at the same time, one per performance core (ccunits.c);
   each unit's object is cached by its text, so the units an edit leaves alone are not compiled again.
   Returns the objects to link, quoted, or NULL to compile the C file whole. */
static int perf_cores(void) {
    int n = 0;
    size_t len = sizeof n;
#ifdef __APPLE__ /* the performance cores: the efficiency ones would be the slowest unit */
    if (sysctlbyname("hw.perflevel0.physicalcpu", &n, &len, NULL, 0) != 0) n = 0;
#endif
    (void)len;
    if (n <= 0) n = xs_ncpus() / 2;
    return n < 1 ? 1 : n > 8 ? 8 : n;
}
static char *compile_units(const char *cc, const char *flags, const char *cfile) {
#if defined(_WIN32)
    (void)cc; (void)flags; (void)cfile;
    return NULL; /* (Windows: --dev builds the program whole; the units need fork) */
}
#else
    const char *env = getenv("XISTO_UNITS");
    int want = env ? atoi(env) : perf_cores();
    if (want < 2 || !cc_entry) return NULL;
    double t0 = now_ms();
    FILE *f = fopen(cfile, "rb");
    if (!f) return NULL;
    Buf src = {0};
    char chunk[65536];
    size_t k;
    while ((k = fread(chunk, 1, sizeof chunk, f)) > 0) for (size_t i = 0; i < k; i++) buf_putc(&src, chunk[i]);
    fclose(f);
    if (!src.data) return NULL;
    Buf units[12];
    if (want > 8) want = 8;
    int nu = cc_split_units(src.data, src.len, env ? -want : want, units);
    free(src.data);
    if (nu < 2) return NULL;
    char dir[1024];
    strncpy(dir, cc_entry, sizeof dir - 1);
    dir[sizeof dir - 1] = 0;
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0; else strcpy(dir, ".");
    char *cache = xs_sprintf("%s/.xisto/cache/u", dir);
    if (system(xs_sprintf("mkdir -p '%s'", cache)) != 0) return NULL;
    /* the runtime's declarations open every unit and change only with the configuration: clang reads them from
       a precompiled header kept in ~/.cache/xisto/pch (an edit's units: 82 -> 44 ms each). Only the runtime part:
       a prefix that changed with the program would mean a new header at every edit. XISTO_NO_PCH=1 turns it off. */
    char *pch = NULL;
    size_t pre_len = 0;
    if (cc_is_clang(cc) && !getenv("XISTO_NO_PCH")) {
        const char *um = strstr(units[0].data, "\n/* ---- native package code ---- */");
        if (!um) um = strstr(units[0].data, "/* ---- codegen unit: the program");
        if (um) pre_len = (size_t)(um - units[0].data) + (um[0] == '\n' ? 1 : 0);
        for (int u = 1; u < nu && pre_len; u++)
            if (units[u].len <= pre_len || memcmp(units[u].data, units[0].data, pre_len) != 0) pre_len = 0;
    }
    if (pre_len) {
        const char *home = getenv("HOME");
        char *pdir = xs_sprintf("%s/.cache/xisto/pch", home ? home : "/tmp");
        uint64_t h = fnv(units[0].data, pre_len) ^ (fnv(cc, strlen(cc)) * 31) ^ (fnv(flags, strlen(flags)) * 131) ^ (fnv(XS_HOST, strlen(XS_HOST)) * 3);
        pch = xs_sprintf("%s/%016llx.pch", pdir, (unsigned long long)h);
        if (access(pch, R_OK) != 0) {
            /* the header stays next to its PCH: clang checks the file the PCH was made from */
            char *hdr = xs_sprintf("%s/%016llx.h", pdir, (unsigned long long)h), *htmp = xs_sprintf("%s.%d.tmp", hdr, (int)getpid());
            char *tmp = xs_sprintf("%s.%d.tmp", pch, (int)getpid());
            FILE *hf = system(xs_sprintf("mkdir -p '%s'", pdir)) == 0 ? fopen(htmp, "wb") : NULL;
            if (hf) {
                fwrite(units[0].data, 1, pre_len, hf);
                fclose(hf);
                if (access(hdr, R_OK) != 0) rename(htmp, hdr); else unlink(htmp);
                char *cmd = xs_sprintf("%s -std=c11 %s -ffp-contract=off -w -I'%s' -x c-header -o '%s' '%s' 2>/dev/null", cc, flags, xisto_dir, tmp, hdr); /* no link flags: they make clang expect several outputs */
                if (link_tls && tls_awslc()) cmd = xs_sprintf("%s -I'%s/include'", cmd, awslc_dir());
                else if (link_tls) cmd = xs_sprintf("%s -I'%s/bearssl-inc'", cmd, xisto_dir);
                if (system(cmd) != 0 || rename(tmp, pch) != 0) pch = NULL;
            } else pch = NULL;
        }
        if (!pch) pre_len = 0;
    }
    char *objs = "";
    char *cmds[12];
    int which[12];
    char *outs[12], *tmps[12];
    int fresh = 0;
    for (int u = 0; u < nu; u++) {
        uint64_t h = fnv(units[u].data, units[u].len) ^ (fnv(cc, strlen(cc)) * 31) ^ (fnv(flags, strlen(flags)) * 131) ^ (fnv(XS_HOST, strlen(XS_HOST)) * 3);
        outs[u] = xs_sprintf("%s/%016llx.o", cache, (unsigned long long)h);
        bool twin = false; /* units with no functions of their own (a small program) are the same text */
        for (int v = 0; v < u; v++) twin = twin || strcmp(outs[v], outs[u]) == 0;
        if (twin) { free(units[u].data); continue; }
        objs = xs_sprintf("%s '%s'", objs, outs[u]);
        if (access(outs[u], R_OK) == 0) { free(units[u].data); continue; }
        char *ufile = xs_sprintf("%s.u%d.c", cfile, u);
        FILE *uf = fopen(ufile, "wb");
        if (!uf) return NULL;
        fwrite(units[u].data + pre_len, 1, units[u].len - pre_len, uf);
        fclose(uf);
        free(units[u].data);
        tmps[u] = xs_sprintf("%s.%d.%d.tmp", outs[u], (int)getpid(), u);
        char *cmd = xs_sprintf("%s -std=c11 %s -ffp-contract=off -Wno-unused-value -Wno-unused-variable -Wno-unused-function -Wno-parentheses-equality "
                               "-Wno-ignored-qualifiers -Wno-return-type -Wno-unused-command-line-argument %s -I'%s'%s -c -o '%s' '%s'",
                               cc, flags, XS_LINK_FLAGS, xisto_dir, pch ? xs_sprintf(" -include-pch '%s'", pch) : "", tmps[u], ufile);
        if (link_tls && tls_awslc()) cmd = xs_sprintf("%s -I'%s/include'", cmd, awslc_dir());
        else if (link_tls) cmd = xs_sprintf("%s -I'%s/bearssl-inc'", cmd, xisto_dir);
        cmds[fresh] = cmd;
        which[fresh++] = u;
    }
    int rcs[12];
    const char *jenv = getenv("XISTO_CC_JOBS"); /* units compiled at once (default: all; a heavy build, like one with sanitizers, wants fewer) */
    int jobs = jenv && atoi(jenv) > 0 ? atoi(jenv) : want;
    if (!xs_cc_run(cmds, fresh, jobs, rcs)) exit(1);
    bool ok = true;
    for (int i = 0; i < fresh; i++)
        if (rcs[i] != 0 || rename(tmps[which[i]], outs[which[i]]) != 0) ok = false;
    if (!ok) {
        fprintf(stderr, "xisto: a codegen unit did not compile (this is a bug in xisto; units kept next to %s; XISTO_UNITS=1 builds it whole)\n", cfile);
        exit(1);
    }
    if (show_time) fprintf(stderr, "xisto: %d codegen units (%d compiled, %d from cache) %.1f ms\n", nu, fresh, nu - fresh, now_ms() - t0);
    return objs;
}
#endif
/* --target: the whole program in one unit, compiled and linked by zig's clang against musl, statically.
   SQLite and BearSSL (TLS) are compiled once per target from their source (~/.cache/xisto/target/<triple>/). */
static void run_cc_target(const char *cfile, const char *outfile, bool dev) {
    double t0 = now_ms();
    const char *zig = getenv("XISTO_ZIG");
    if (!zig) zig = "zig";
    if (!xs_on_path(zig)) {
#if defined(_WIN32)
        fprintf(stderr, "xisto: on Windows xisto compiles through zig: install it (https://ziglang.org/download, or winget install zig.zig) and put it on PATH, or set XISTO_ZIG\n");
#else
        fprintf(stderr, "xisto: --target needs zig as the cross compiler (macOS: brew install zig; or set XISTO_ZIG)\n");
#endif
        exit(1);
    }
    char *cc = xs_sprintf("%s cc -target %s", xs_q(zig), xs_target);
    char *tdir = xs_sprintf("%s/.cache/xisto/target/%s", xs_home(), xs_target);
    char *tls_extra = "";
    if (link_tls) { /* BearSSL from its source, once per target, its files compiled in parallel */
        char *lib = xs_sprintf("%s/libbearssl.a", tdir);
        char *src = xs_sprintf("%s/../third_party/bearssl", xisto_dir);
        if (access(lib, R_OK) != 0) {
            if (access(xs_sprintf("%s/src", src), R_OK) != 0) { fprintf(stderr, "xisto: --target with TLS needs BearSSL's source at %s\n", src); exit(1); }
            double tb = now_ms();
            char *obj = xs_sprintf("%s/bearssl.%d", tdir, xs_pid());
#if defined(_WIN32) /* no find/xargs/sh: each file compiled from here, then archived */
            if (!xs_mkdirs(obj)) { fprintf(stderr, "xisto: cannot create %s\n", obj); exit(1); }
            char **files = NULL;
            int nf = 0;
            xs_list_files(xs_sprintf("%s/src", src), ".c", ".S", &files, &nf);
            Buf objs = {0};
            for (int k = 0; k < nf; k++) {
                char *o = xs_sprintf("%s/%d.o", obj, k);
                if (xs_sh(xs_sprintf("%s -O2 -w -I%s -I%s -c -o %s %s", cc, xs_q(xs_sprintf("%s/inc", src)), xs_q(xs_sprintf("%s/src", src)), xs_q(o),
                                     xs_q(xs_sprintf("%s/src/%s", src, files[k])))) != 0) { fprintf(stderr, "xisto: BearSSL did not compile for %s (%s)\n", xs_target, files[k]); exit(1); }
                buf_printf(&objs, " %s", xs_q(o));
            }
            /* an archive of ~300 objects: through a response file, the command line has a limit */
            char *rsp = xs_sprintf("%s/objs.rsp", obj);
            FILE *rf = fopen(rsp, "wb");
            if (!rf) exit(1);
            fputs(objs.data ? objs.data : "", rf);
            fclose(rf);
            if (xs_sh(xs_sprintf("%s ar rcs %s @%s", xs_q(zig), xs_q(xs_sprintf("%s.tmp", lib)), xs_q(rsp))) != 0 || !xs_rename(xs_sprintf("%s.tmp", lib), lib)) {
                fprintf(stderr, "xisto: BearSSL did not compile for %s\n", xs_target);
                exit(1);
            }
            if (0) {
#else
            {
#endif
            char *c = xs_sprintf("mkdir -p '%s' && cd '%s/src' && find . -name '*.c' -o -name '*.S' | sed 's|^\\./||' | "
                                 "xargs -P 8 -I{} sh -c 'o=\"%s/$(echo {} | tr / _).o\"; %s -O2 -w %s -I../inc -I. -c -o \"$o\" {}' && "
                                 "'%s' ar rcs '%s.tmp' '%s'/*.o && mv '%s.tmp' '%s' && rm -rf '%s'",
                                 obj, src, obj, cc, strstr(xs_target, "windows") ? "" : "-DBR_USE_GETENTROPY=1", /* Windows: BearSSL's own CryptGenRandom path */
                                 zig, lib, obj, lib, lib, obj);
            if (system(c) != 0) { fprintf(stderr, "xisto: BearSSL did not compile for %s\n", xs_target); exit(1); }
            }
            if (show_time) fprintf(stderr, "xisto: BearSSL compiled once for %s: %.0f ms\n", xs_target, now_ms() - tb);
        }
        tls_extra = xs_sprintf("-I%s %s", xs_q(xs_sprintf("%s/inc", src)), xs_q(lib));
    }
    const char *flags = xs_debug ? "-O0 -g" : dev ? "-O0" : "-O3"; /* (as on the host, run_cc_raw) */
    char *extra = "";
    if (link_sqlite) {
        char *dir = tdir;
        char *obj = xs_sprintf("%s/sqlite3.o", dir);
        if (access(obj, R_OK) != 0) {
            char *src = xs_sprintf("%s/../third_party/sqlite/sqlite3.c", xisto_dir);
            if (access(src, R_OK) != 0) { fprintf(stderr, "xisto: --target with SQLite needs its source at %s\n", src); exit(1); }
            char *tmp = xs_sprintf("%s.%d.tmp", obj, xs_pid());
            if (!xs_mkdirs(dir) || xs_sh(xs_sprintf("%s -O2 -w -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -c -o %s %s", cc, xs_q(tmp), xs_q(src))) != 0 || !xs_rename(tmp, obj)) {
                fprintf(stderr, "xisto: SQLite did not compile for %s\n", xs_target);
                exit(1);
            }
        }
        extra = xs_sprintf("-I%s %s", xs_q(xisto_dir), xs_q(obj));
    }
    bool win = strstr(xs_target, "windows") != NULL;
    char *cmd = xs_sprintf("%s -std=c11 %s -mllvm -hot-cold-split=false -ffp-contract=off -fno-omit-frame-pointer -ffunction-sections -fdata-sections "
                           "%s -static -Wno-unused-value -Wno-unused-variable -Wno-unused-function -Wno-parentheses-equality "
                           "-Wno-ignored-qualifiers -Wno-return-type -Wno-incompatible-pointer-types-discards-qualifiers -o %s %s %s -lm -lpthread%s",
                           cc, flags, win ? "" : "-Wl,--gc-sections", xs_q(outfile), xs_q(cfile), xs_sprintf("%s %s", extra, tls_extra), win ? " -lws2_32" : "");
    if (xs_sh(cmd) != 0) {
        fprintf(stderr, "xisto: the C compiler failed for %s (this is a bug in xisto; generated file kept at %s)\n", xs_target, cfile);
        exit(1);
    }
    if (show_time) fprintf(stderr, "xisto: cc (%s) %.1f ms\n", xs_target, now_ms() - t0);
}
static void run_cc_raw(const char *cfile, const char *outfile, bool dev) {
    if (xs_target) { run_cc_target(cfile, outfile, dev); return; }
    double t0 = now_ms();
    const char *cc = host_cc();
    const char *opt = getenv("XISTO_OPT"); /* the release optimization level (measuring knob) */
    const char *flags = xs_debug ? "-O0 -g" : dev ? "-O0" : opt ? xs_sprintf("-O%s", opt) : "-O3"; /* -O3: objects 5.2 -> 3.7 ns per iteration, nbody 0.42 -> 0.40 s, nothing slower; Conduit builds as fast, smaller (07/11) */
    if (xs_cpu && !dev && !xs_debug) { /* (-ffp-contract=off below still forbids fused multiply-adds: the same numbers) */
#if defined(__aarch64__)
        flags = xs_sprintf("%s -mcpu=%s", flags, xs_cpu);
#else
        flags = xs_sprintf("%s -march=%s", flags, xs_cpu);
#endif
    }
    /* JS semantics are strict IEEE 754: no fused multiply-add. */
#ifdef XS_CLANG_FLAGS
    if (cc_is_clang(cc)) flags = xs_sprintf("%s %s", flags, XS_CLANG_FLAGS);
#endif
    if (!cc_is_clang(cc)) flags = xs_sprintf("%s -w", flags);
#if defined(__aarch64__)
    /* -O0 on arm64: clang's GlobalISel selector costs a third more than FastISel for the same code (Conduit's
       unit: 372 -> 234 ms); --dev and --debug want the build to be quick, not the code */
    if ((dev || xs_debug) && cc_is_clang(cc)) flags = xs_sprintf("%s -mllvm -global-isel=0", flags);
#endif
    char *rtobj = runtime_object(cc);
    char *units = dev && !xs_debug && rtobj ? compile_units(cc, flags, cfile) : NULL;
    /* the units' objects take the place of the C file in the link */
    char *input = units ? units : xs_sprintf("'%s'", cfile);
    if (build_dylib) input = xs_sprintf("-dynamiclib -Wl,-exported_symbol,_main %s", input);
    if (units) flags = xs_sprintf("%s -Wno-unused-command-line-argument", flags); /* a link: no compile flags apply */
    char *cmd = xs_sprintf("%s -std=c11 %s -ffp-contract=off -Wno-unused-value -Wno-unused-variable -Wno-unused-function -Wno-parentheses-equality -Wno-ignored-qualifiers -Wno-return-type %s -o '%s' %s %s -lm %s",
                           cc, flags, XS_LINK_FLAGS, outfile, input, rtobj ? xs_sprintf("'%s'", rtobj) : "", XS_LINK_LIBS);
    if (link_tls && tls_awslc()) { /* AWS-LC: libssl is C++ inside, hence the C++ runtime */
        const char *d = awslc_dir();
        cmd = xs_sprintf("%s -I'%s/include' '%s/lib/libssl.a' '%s/lib/libcrypto.a' %s -lpthread", cmd, d, d, d, XS_CXX_LIB);
    } else if (link_tls) /* BearSSL, compiled from source into a static library: only the parts the program uses are linked */
        cmd = xs_sprintf("%s -I'%s/bearssl-inc' '%s/libbearssl.a'", cmd, xisto_dir, xisto_dir);
    if (link_sqlite) /* SQLite: the header and the object compiled from source live next to the xisto executable */
        cmd = xs_sprintf("%s -I'%s' '%s/sqlite3.o' -lpthread", cmd, xisto_dir, xisto_dir);
    int rc = 0;
    if (!xs_cc_run(&cmd, 1, 1, &rc)) exit(1);
    if (rc != 0) {
        fprintf(stderr, "xisto: the C compiler failed (this is a bug in xisto; generated file kept at %s)\n", cfile);
        exit(1);
    }
    if (show_time) fprintf(stderr, "xisto: cc %.1f ms\n", now_ms() - t0);
}

/* xisto clean [DIR]: removes DIR/.xisto/cache (default: the current directory). It is only a cache. */
static int clean_cache(const char *dir) {
    char *cache = xs_sprintf("%s/.xisto/cache", dir);
    if (access(cache, F_OK) != 0) {
        printf("xisto: no cache at %s\n", cache);
        return 0;
    }
    int nobj = 0;
    long long bytes = xs_tree_size(cache, &nobj);
    xs_rmtree(cache);
    if (xs_exists(cache)) {
        fprintf(stderr, "xisto: cannot remove %s\n", cache);
        return 1;
    }
    rmdir(xs_sprintf("%s/.xisto", dir)); /* only if empty */
    char du[32];
    if (bytes >= 1 << 20) snprintf(du, sizeof du, "%.1fM", (double)bytes / (1 << 20));
    else snprintf(du, sizeof du, "%.0fK", (double)bytes / 1024);
    printf("xisto: removed %s (%s, %d files)\n", cache, du, nobj);
    return 0;
}

static int cmd_fmt(int argc, char **argv) {
    bool check = false;
    int changed = 0, files = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0) { check = true; continue; }
        char *src = xs_read_file(argv[i]);
        if (!src) { fprintf(stderr, "xisto: cannot open '%s'\n", argv[i]); return 2; }
        files++;
        char *f = xs_format(src);
        if (strcmp(f, src) == 0) continue;
        changed++;
        if (check) { printf("%s\n", argv[i]); continue; }
        FILE *o = fopen(argv[i], "wb");
        if (!o) { fprintf(stderr, "xisto: cannot write '%s'\n", argv[i]); return 2; }
        fputs(f, o);
        fclose(o);
    }
    if (check) {
        if (changed) fprintf(stderr, "xisto fmt: %d of %d file(s) need formatting\n", changed, files);
        return changed ? 1 : 0;
    }
    fprintf(stderr, "xisto fmt: %d of %d file(s) formatted\n", changed, files);
    return 0;
}

/* xisto test [DIR]: every *.test.ts under DIR is built and run. A test passes when it exits 0, leaves no live
   objects, and (if NAME.expected exists next to it) prints exactly that. assert(cond, message) fails it. */
static int str_cmp_(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static int cmd_test(int argc, char **argv) {
    const char *dir = argc >= 3 ? argv[2] : ".";
    char **found = NULL;
    int nfound = 0;
    xs_list_files(dir, ".test.ts", NULL, &found, &nfound);
    char **files = NULL;
    int nfiles = 0;
    for (int i = 0; i < nfound; i++) { /* not packages, not node_modules */
        if (strstr(found[i], ".xisto/") || strstr(found[i], "node_modules/")) continue;
        files = realloc(files, sizeof(char *) * (size_t)(nfiles + 1));
        files[nfiles++] = strcmp(dir, ".") == 0 ? found[i] : xs_sprintf("%s/%s", dir, found[i]);
    }
    if (!nfiles) { printf("xisto test: no *.test.ts files under %s\n", dir); return 0; }
    qsort(files, (size_t)nfiles, sizeof(char *), str_cmp_);
#if defined(_WIN32)
    char *self = xs_sprintf("%s/xisto.exe", xisto_dir);
#else
    char *self = xs_sprintf("%s/xisto", xisto_dir);
#endif
    char *work = xs_make_temp_dir("xisto-test");
    if (!work) { fprintf(stderr, "xisto: cannot create temp dir\n"); return 2; }
    int pass = 0, fail = 0;
    double t_all = now_ms();
    for (int k = 0; k < nfiles; k++) {
        const char *f = files[k];
        double t0 = now_ms();
#if defined(_WIN32)
        char *exe = xs_sprintf("%s/t%d.exe", work, k);
#else
        char *exe = xs_sprintf("%s/t%d", work, k);
#endif
        char *log = xs_sprintf("%s/log", work);
        bool ok = true;
        const char *why = "";
        if (xs_sh(xs_sprintf("%s build -o %s %s > %s 2>&1", xs_q(self), xs_q(exe), xs_q(f), xs_q(log))) != 0) { ok = false; why = "build failed"; }
        else {
            xs_setenv("XISTO_RC_STATS", "1");
            int rc = xs_sh(xs_sprintf("%s > %s 2> %s", xs_q(exe), xs_q(xs_sprintf("%s.out", log)), xs_q(xs_sprintf("%s.err", log))));
            xs_setenv("XISTO_RC_STATS", NULL);
            char *err = xs_slurp(xs_sprintf("%s.err", log), NULL);
            if (rc != 0) { ok = false; why = "exited with an error"; }
            else if (!err || !strstr(err, " 0 still alive")) { ok = false; why = "leaked objects"; }
            else {
                char *expected = xs_sprintf("%.*s.expected", (int)(strlen(f) - 8), f);
                size_t le = 0, lo = 0;
                char *want = xs_slurp(expected, &le);
                char *got = want ? xs_slurp(xs_sprintf("%s.out", log), &lo) : NULL;
                if (want && (!got || le != lo || memcmp(want, got, le) != 0)) { ok = false; why = "output differs from .expected"; }
            }
        }
        if (ok) { pass++; printf("ok    %s (%.0f ms)\n", f, now_ms() - t0); }
        else {
            fail++;
            printf("FAIL  %s: %s\n", f, why);
            /* the build log and the program's errors, first lines, indented (the leak report line left out) */
            int shown = 0;
            for (int w = 0; w < 2 && shown < 8; w++) {
                char *t = xs_slurp(w ? xs_sprintf("%s.err", log) : log, NULL);
                for (char *line = t; line && *line && shown < 8;) {
                    char *nl = strchr(line, '\n');
                    size_t len = nl ? (size_t)(nl - line) : strlen(line);
                    if (len && !(strstr(line, "still alive at exit") && (!nl || strstr(line, "still alive at exit") < nl))) { printf("      %.*s\n", (int)len, line); shown++; }
                    line = nl ? nl + 1 : line + len;
                }
            }
        }
    }
    xs_rmtree(work);
    printf("\n%d passed, %d failed (%.0f ms)\n", pass, fail, now_ms() - t_all);
    return fail ? 1 : 0;
}

/* ---- packages: always source, locked by commit (docs/bibliotecas.md) ----
   xisto.json  {"name": ..., "dependencies": {"name": "source@ref"}}
   xisto.lock  {"name": {"source": ..., "commit": ...}}
   Both files are written one dependency per line so they diff cleanly. Nothing from a package is ever run
   at install time: a package is only source code. */
typedef struct { char *name, *source, *ref, *commit; } Dep;

static int read_deps(const char *path, Dep *deps, int max, bool lock) {
    char *t = xs_read_file(path);
    if (!t) return 0;
    int n = 0;
    char *sec = strstr(t, lock ? "{" : "\"dependencies\"");
    if (!sec) return 0;
    for (char *line = strchr(sec, '\n'); line && n < max; line = strchr(line + 1, '\n')) {
        char name[256], a[1024], b[256];
        if (lock) {
            if (sscanf(line, " \"%255[^\"]\": {\"source\": \"%1023[^\"]\", \"commit\": \"%255[^\"]\"", name, a, b) == 3) {
                deps[n].name = xs_strndup(name, strlen(name)); deps[n].source = xs_strndup(a, strlen(a)); deps[n].commit = xs_strndup(b, strlen(b)); n++;
            }
        } else if (sscanf(line, " \"%255[^\"]\": \"%1023[^\"]\"", name, a) == 2) {
            deps[n].name = xs_strndup(name, strlen(name));
            char *at = strrchr(a, '@');
            deps[n].ref = at && at != a && !strstr(at, "/") ? xs_strndup(at + 1, strlen(at + 1)) : NULL;
            if (at && deps[n].ref) *at = 0;
            deps[n].source = xs_strndup(a, strlen(a));
            n++;
        }
    }
    return n;
}

static void write_deps(Dep *deps, int n) {
    char cwd[1024];
    if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "app");
    const char *base = strrchr(cwd, '/') ? strrchr(cwd, '/') + 1 : cwd;
    FILE *f = fopen("xisto.json", "w");
    fprintf(f, "{\n  \"name\": \"%s\",\n  \"dependencies\": {\n", base);
    for (int i = 0; i < n; i++) fprintf(f, "    \"%s\": \"%s%s%s\"%s\n", deps[i].name, deps[i].source, deps[i].ref ? "@" : "", deps[i].ref ? deps[i].ref : "", i + 1 < n ? "," : "");
    fprintf(f, "  }\n}\n");
    fclose(f);
    f = fopen("xisto.lock", "w");
    fprintf(f, "{\n");
    for (int i = 0; i < n; i++) fprintf(f, "  \"%s\": {\"source\": \"%s\", \"commit\": \"%s\"}%s\n", deps[i].name, deps[i].source, deps[i].commit ? deps[i].commit : "", i + 1 < n ? "," : "");
    fprintf(f, "}\n");
    fclose(f);
}

static bool valid_name(const char *s) {
    if (!*s) return false;
    for (; *s; s++) if (!(isalnum((unsigned char)*s) || *s == '-' || *s == '_' || *s == '.')) return false;
    return true;
}

/* clone (or reuse) .xisto/pkgs/NAME and check out REF or COMMIT; returns the commit */
static char *fetch_pkg(const char *name, const char *source, const char *rev) {
    char *dir = xs_sprintf(".xisto/pkgs/%s", name);
    if (!xs_mkdirs(".xisto/pkgs")) return NULL;
    if (!xs_exists(dir) && xs_sh(xs_sprintf("git clone --quiet %s %s 2>&1", xs_q(source), xs_q(dir))) != 0) {
        fprintf(stderr, "xisto: cannot clone %s\n", source);
        return NULL;
    }
    if (rev && *rev) {
        if (xs_sh(xs_sprintf("git -C %s fetch --quiet origin 2>%s", xs_q(dir), xs_devnull())) != 0) { /* (a local checkout may have nothing to fetch) */ }
        if (xs_sh(xs_sprintf("git -C %s -c advice.detachedHead=false checkout --quiet %s 2>&1", xs_q(dir), xs_q(rev))) != 0) {
            fprintf(stderr, "xisto: %s has no revision '%s'\n", source, rev);
            return NULL;
        }
    }
    return shell_output(xs_sprintf("git -C %s rev-parse HEAD", xs_q(dir)));
}

static int cmd_add(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: xisto add SOURCE[@REF] [--name NAME]\n"); return 2; }
    char *src = xs_strndup(argv[2], strlen(argv[2]));
    char *ref = NULL;
    char *at = strrchr(src, '@');
    if (at && at != src && !strchr(at, '/') && !strchr(at, ':')) { *at = 0; ref = at + 1; }
    const char *name = NULL;
    for (int i = 3; i + 1 < argc; i++) if (strcmp(argv[i], "--name") == 0) name = argv[i + 1];
    if (!name) {
        const char *b = strrchr(src, '/') ? strrchr(src, '/') + 1 : src;
        char *n = xs_strndup(b, strlen(b));
        size_t l = strlen(n);
        if (l > 4 && strcmp(n + l - 4, ".git") == 0) n[l - 4] = 0;
        name = n;
    }
    if (!valid_name(name)) { fprintf(stderr, "xisto: invalid package name '%s'\n", name); return 2; }
    Dep deps[256];
    int n = read_deps("xisto.lock", deps, 256, true);
    Dep refs[256];
    int nr = read_deps("xisto.json", refs, 256, false);
    for (int i = 0; i < n; i++) for (int k = 0; k < nr; k++) if (strcmp(deps[i].name, refs[k].name) == 0) deps[i].ref = refs[k].ref;
    xs_rmtree(xs_sprintf(".xisto/pkgs/%s", name));
    char *commit = fetch_pkg(name, src, ref);
    if (!commit) return 1;
    int idx = -1;
    for (int i = 0; i < n; i++) if (strcmp(deps[i].name, name) == 0) idx = i;
    if (idx < 0) idx = n++;
    deps[idx].name = (char *)name; deps[idx].source = src; deps[idx].ref = ref; deps[idx].commit = commit;
    write_deps(deps, n);
    printf("xisto: added %s at %.12s (%s%s%s)\n", name, commit, src, ref ? "@" : "", ref ? ref : "");
    return 0;
}

static int cmd_install(void) {
    Dep deps[256];
    int n = read_deps("xisto.lock", deps, 256, true);
    if (n == 0) { printf("xisto: nothing to install (no xisto.lock)\n"); return 0; }
    for (int i = 0; i < n; i++) {
        char *c = fetch_pkg(deps[i].name, deps[i].source, deps[i].commit);
        if (!c) return 1;
        if (strcmp(c, deps[i].commit) != 0) { fprintf(stderr, "xisto: %s is at %s, lock says %s\n", deps[i].name, c, deps[i].commit); return 1; }
        printf("xisto: %s at %.12s\n", deps[i].name, c);
    }
    return 0;
}

/* xisto debug: the program built with --debug into exe; false (and the errors printed) when it does not compile */
bool xs_build_debug(const char *file, const char *exe) {
    xs_debug = true;
    use_qbe = getenv("XISTO_DEBUG_BACKEND") && strcmp(getenv("XISTO_DEBUG_BACKEND"), "qbe") == 0;
    if (getenv("XISTO_NODE_COMPAT") && strcmp(getenv("XISTO_NODE_COMPAT"), "1") == 0) xs_node_compat = true;
    if (!xs_cpu && getenv("XISTO_CPU") && *getenv("XISTO_CPU")) xs_cpu = getenv("XISTO_CPU");
    if (use_qbe) { compile_to_c(file); build_qbe_cached(last_program, file, exe); return access(exe, X_OK) == 0; }
    Buf b = compile_to_c(file);
    char *cfile = xs_sprintf("%s.c", exe);
    FILE *f = fopen(cfile, "wb");
    if (!f) { fprintf(stderr, "xisto: cannot write '%s'\n", cfile); return false; }
    fwrite(b.data, 1, b.len, f);
    cc_entry = file;
    cc_text = b.data;
    cc_len = b.len;
    fclose(f);
    run_cc(cfile, exe, true);
    unlink(cfile);
    return access(exe, X_OK) == 0;
}

/* xisto check's child: one entry file in collect mode. Prints the modules it loaded (so the parent does not check
   them again), every error as a JSON line on the same stream, and "quiet N" (follow-on errors hidden) at the end. */
static void check1_done(void) {
    printf("quiet\t%d\n", xs_quiet_count);
    fflush(stdout);
}

static int cmd_check1(const char *file, const char *argv0) {
    find_xisto_dir(argv0);
    xs_check_mode = true;
    xs_json_errors = true;
    xs_warnings = false;
    dup2(1, 2); /* one stream, in order */
    atexit(check1_done);
    Program *p = parse_program(file);
    for (int i = 0; i < p->nmodules; i++)
        if (!p->modules[i]->builtin) printf("module\t%s\n", p->modules[i]->path);
    fflush(stdout);
    check_program(p);
    return 0;
}

int cmd_check(int argc, char **argv);

int main(int argc, char **argv) {
#if defined(_WIN32)
    if (argc >= 2 && (!strcmp(argv[1], "debug") || !strcmp(argv[1], "dap"))) {
        fprintf(stderr, "xisto: '%s' is not available on Windows yet (build, run, emit and fmt are)\n", argv[1]);
        return 2;
    }
#endif
    if (argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "version") == 0)) { printf("xisto %s\n", XISTO_VERSION); return 0; }
    if (argc >= 2 && strcmp(argv[1], "add") == 0) return cmd_add(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "install") == 0) return cmd_install();
    if (argc >= 2 && strcmp(argv[1], "clean") == 0) return clean_cache(argc >= 3 ? argv[2] : ".");
    if (argc >= 2 && strcmp(argv[1], "fmt") == 0) return cmd_fmt(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "check") == 0) return cmd_check(argc, argv);
    if (argc >= 3 && strcmp(argv[1], "__check1") == 0) return cmd_check1(argv[2], argv[0]);
    if (argc >= 2 && strcmp(argv[1], "test") == 0) { find_xisto_dir(argv[0]); return cmd_test(argc, argv); }
    if (argc >= 2 && strcmp(argv[1], "debug") == 0) { find_xisto_dir(argv[0]); return cmd_debug(argc, argv); }
    if (argc >= 2 && strcmp(argv[1], "dap") == 0) { find_xisto_dir(argv[0]); return cmd_debug(argc, argv); }
    if (argc < 3) usage();
    const char *cmd = argv[1];
    bool dev = false;
    const char *outname = NULL;
    const char *file = NULL;
    int rest = argc;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--dev") == 0) dev = true;
        else if (strcmp(argv[i], "--debug") == 0) xs_debug = dev = true;
        else if (strcmp(argv[i], "--backend=qbe") == 0) use_qbe = true;
        else if (strcmp(argv[i], "--backend=c") == 0) use_qbe = false;
        else if (strncmp(argv[i], "--target=", 9) == 0) {
            const char *t = argv[i] + 9;
            if (strcmp(t, "linux-arm64") == 0) xs_target = "aarch64-linux-musl";
            else if (strcmp(t, "linux-x64") == 0) xs_target = "x86_64-linux-musl";
            else if (strcmp(t, "windows-x64") == 0) xs_target = "x86_64-windows-gnu";
            else if (strcmp(t, "windows-arm64") == 0) xs_target = "aarch64-windows-gnu";
            else if (strcmp(t, "native") == 0) xs_target = NULL;
            else { fprintf(stderr, "xisto: unknown target '%s' (supported: linux-arm64, linux-x64, windows-x64, windows-arm64, native)\n", t); return 2; }
        }
        else if (strcmp(argv[i], "--json") == 0) xs_json_errors = true;
        else if (strcmp(argv[i], "--profile") == 0) xs_profile = true;
        else if (strcmp(argv[i], "--time") == 0) show_time = true;
        else if (strcmp(argv[i], "--no-warnings") == 0) xs_warnings = false;
        else if (strcmp(argv[i], "--split-init") == 0) setenv("XISTO_MOD_INIT_MIN", "0", 1); /* each imported module's top level in its own C function, however small the program (XB-176) */
        else if (strcmp(argv[i], "--warnings") == 0) xs_show_warnings = true;
        else if (strcmp(argv[i], "--deny-warnings") == 0) xs_deny_warnings = true;
        else if (strncmp(argv[i], "--cpu=", 6) == 0) xs_cpu = argv[i] + 6;
        else if (strcmp(argv[i], "--tls=off") == 0) xs_tls_off = true;
        else if (strcmp(argv[i], "--node-compat") == 0) xs_node_compat = true;
        else if (strcmp(argv[i], "--tsc-checked") == 0) xs_setenv("XISTO_TSC_CHECKED", "1");
        else if (strcmp(argv[i], "--tls=awslc") == 0 || strcmp(argv[i], "--tls=bearssl") == 0) tls_choice = argv[i] + 6;
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) outname = argv[++i];
        else if (strcmp(argv[i], "--pkgs") == 0 && i + 1 < argc) { if (!xs_use_pkgs_dir(argv[++i])) return 2; }
        else if (!file) file = argv[i];
        else { rest = i; break; }
    }
    if (!file) usage();
    if (outname && xs_is_dir(outname)) { fprintf(stderr, "xisto: -o %s is a directory: give the executable's file name (-o %s/name)\n", outname, outname); return 2; }
    if (getenv("XISTO_NODE_COMPAT") && strcmp(getenv("XISTO_NODE_COMPAT"), "1") == 0) xs_node_compat = true;
    if (!xs_cpu && getenv("XISTO_CPU") && *getenv("XISTO_CPU")) xs_cpu = getenv("XISTO_CPU"); /* (before the cache key) */
#if defined(_WIN32)
    /* on Windows the program is built for Windows through zig: the same path as --target from elsewhere */
    bool host_target = !xs_target;
#if defined(__aarch64__)
    if (!xs_target) xs_target = "aarch64-windows-gnu";
#else
    if (!xs_target) xs_target = "x86_64-windows-gnu";
#endif
    if (use_qbe) { fprintf(stderr, "xisto: the qbe backend is not available on Windows yet: build through C (drop --backend=qbe)\n"); return 2; }
    if (xs_debug) { fprintf(stderr, "xisto: the debugger is not available on Windows yet (it runs on macOS and Linux)\n"); return 2; }
#else
    bool host_target = false;
#endif
    find_xisto_dir(argv[0]);

    if (strcmp(cmd, "emit") == 0) {
        Buf b = compile_to_c(file);
        fwrite(b.data, 1, b.len, stdout);
        return 0;
    }
    if (strcmp(cmd, "build") != 0 && strcmp(cmd, "run") != 0) usage();

    /* the runtime as a precompiled library: --dev only. A release build stays one translation unit: split, the
       code runs 3-8% slower (a function called once loses the compiler's inlining bonus), and ThinLTO, which
       wins that back, made Conduit's build 2.0 -> 2.8 s. */
    split_rt = dev && !xs_debug && !use_qbe && !xs_target && !getenv("XISTO_NO_SPLIT");
    if (xs_target && use_qbe) { fprintf(stderr, "xisto: --target builds through C (drop --backend=qbe)\n"); return 2; }
    if (xs_target && !host_target && strcmp(cmd, "run") == 0) { fprintf(stderr, "xisto: --target makes an executable for another system: use build\n"); return 2; }
#if defined(__APPLE__)
    in_process = strcmp(cmd, "run") == 0 && dev && !xs_debug && !use_qbe && !getenv("XISTO_NO_INPROC");
#endif
    Buf b = compile_to_c(file);
    char *dir = xs_make_temp_dir("xisto");
    if (!dir) {
        fprintf(stderr, "xisto: cannot create temp dir\n");
        return 1;
    }
    char *cfile = xs_sprintf("%s/%s.%s", dir, strip_ts(file), use_qbe ? "ssa" : "c");
    FILE *f = fopen(cfile, "wb");
    if (!f) {
        fprintf(stderr, "xisto: cannot write '%s'\n", cfile);
        return 1;
    }
    fwrite(b.data, 1, b.len, f);
    cc_entry = file;
    cc_text = b.data;
    cc_len = b.len;
    fclose(f);

    bool exe_suffix = xs_target && strstr(xs_target, "windows"); /* Windows runs only .exe files */
    if (strcmp(cmd, "build") == 0) {
        if (!outname) outname = exe_suffix ? xs_sprintf("%s.exe", strip_ts(file)) : strip_ts(file);
        if (use_qbe) { unlink(cfile); rmdir(dir); build_qbe_cached(last_program, file, outname); return 0; }
        else run_cc(cfile, outname, dev);
        unlink(cfile);
        rmdir(dir);
        return 0;
    }
    char *exe = xs_sprintf("%s/%s%s", dir, strip_ts(file), exe_suffix ? ".exe" : "");
#if defined(__APPLE__)
    /* --dev: no new executable file for macOS to check on its first run (~150 ms); this process runs it */
    if (in_process && !(link_tls && tls_awslc())) {
        char *lib = xs_sprintf("%s.dylib", exe);
        build_dylib = true;
        run_cc(cfile, lib, dev);
        build_dylib = false;
        char **pargv = malloc(sizeof(char *) * (size_t)(argc - rest + 2));
        int pargc = 0;
        pargv[pargc++] = strip_ts(file);
        for (int i = rest; i < argc; i++) pargv[pargc++] = argv[i];
        pargv[pargc] = NULL;
        if (show_time) fprintf(stderr, "xisto: running in this process\n");
        fflush(stdout);
        fflush(stderr);
        xs_run_in_process(lib, pargc, pargv); /* does not return when it ran */
        if (show_time) fprintf(stderr, "xisto: the loader declined; running an executable\n");
        unlink(lib);
    }
#endif
    if (use_qbe) build_qbe_cached(last_program, file, exe);
    else run_cc(cfile, exe, dev);
    Buf cmdline = {0};
    buf_puts(&cmdline, xs_q(exe));
    for (int i = rest; i < argc; i++) buf_printf(&cmdline, " %s", xs_q(argv[i]));
    fflush(stdout);
    int rc = xs_sh(cmdline.data);
    unlink(cfile);
    unlink(exe);
    rmdir(dir);
    return rc < 0 ? 1 : rc & 0xff;
}
