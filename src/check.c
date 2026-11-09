/* xisto check: what a TypeScript project needs to change to compile with xisto, counted by kind of change
   (the categories of src/diag.def), with --details listing every place.

   Each entry file is checked in a child process (xisto __check1 FILE): the compiler in collect mode reports
   every error as one JSON line (the --json format, with the code and category), and the modules it loaded,
   so a file already checked as another's import is not checked again. The parent dedups, counts and prints. */
#include "xisto.h"
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char *file, *code, *category, *message, *help, *source;
    int line, col;
    bool test; /* in a test file (*.spec.ts, *.test.ts, __tests__/, __mocks__/) */
} Diag;

typedef struct {
    char **items;
    int len, cap;
} Strs;

static void strs_push(Strs *s, char *v) {
    if (s->len == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->items = realloc(s->items, sizeof(char *) * (size_t)s->cap);
    }
    s->items[s->len++] = v;
}

static bool strs_has(Strs *s, const char *v) {
    for (int i = 0; i < s->len; i++) if (strcmp(s->items[i], v) == 0) return true;
    return false;
}

static Diag *diags;
static int ndiags, cap_diags, quiet_total, crashed;

/* ---------- the child's JSON lines (flat objects of strings, integers and null, as util.c writes them) ---------- */

static char *json_string(const char **pp) {
    const char *p = *pp;
    if (*p != '"') return NULL;
    p++;
    char *out = malloc(strlen(p) + 1), *o = out;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': *o++ = '\n'; break;
            case 't': *o++ = '\t'; break;
            case 'u': { unsigned v = 0; sscanf(p + 1, "%4x", &v); *o++ = (char)v; p += 4; break; }
            default: *o++ = *p;
            }
            p++;
        } else *o++ = *p++;
    }
    *o = 0;
    if (*p == '"') p++;
    *pp = p;
    return out;
}

static bool parse_diag(const char *line, Diag *d) {
    memset(d, 0, sizeof *d);
    const char *p = line;
    if (*p++ != '{') return false;
    bool error = false;
    while (*p && *p != '}') {
        char *key = json_string(&p);
        if (!key || *p != ':') { free(key); return false; }
        p++;
        char *sval = NULL;
        long ival = 0;
        if (*p == '"') sval = json_string(&p);
        else if (strncmp(p, "null", 4) == 0) p += 4;
        else { ival = strtol(p, (char **)&p, 10); }
        if (strcmp(key, "severity") == 0) error = sval && strcmp(sval, "error") == 0;
        else if (strcmp(key, "code") == 0) d->code = sval, sval = NULL;
        else if (strcmp(key, "category") == 0) d->category = sval, sval = NULL;
        else if (strcmp(key, "file") == 0) d->file = sval, sval = NULL;
        else if (strcmp(key, "message") == 0) d->message = sval, sval = NULL;
        else if (strcmp(key, "help") == 0) d->help = sval, sval = NULL;
        else if (strcmp(key, "source") == 0) d->source = sval, sval = NULL;
        else if (strcmp(key, "line") == 0) d->line = (int)ival;
        else if (strcmp(key, "col") == 0) d->col = (int)ival;
        free(key);
        free(sval);
        if (*p == ',') p++;
    }
    return error && d->file && d->message;
}

/* ---------- files ---------- */

static bool skipped_dir(const char *name) {
    return name[0] == '.' || strcmp(name, "node_modules") == 0 || strcmp(name, "dist") == 0 || strcmp(name, "build") == 0 ||
           strcmp(name, "out") == 0 || strcmp(name, "coverage") == 0;
}

static bool ts_file(const char *name) {
    size_t n = strlen(name);
    if (n > 5 && strcmp(name + n - 5, ".d.ts") == 0) return false;
    return (n > 3 && strcmp(name + n - 3, ".ts") == 0) || (n > 4 && strcmp(name + n - 4, ".tsx") == 0);
}

static void walk(const char *dir, Strs *out) {
    DIR *d = opendir(dir);
    if (!d) return;
    Strs names = {0};
    struct dirent *e;
    while ((e = readdir(d))) strs_push(&names, xs_sprintf("%s", e->d_name));
    closedir(d);
    for (int i = 0; i < names.len; i++) /* sorted: the same order on every system */
        for (int j = i + 1; j < names.len; j++)
            if (strcmp(names.items[j], names.items[i]) < 0) { char *t = names.items[i]; names.items[i] = names.items[j]; names.items[j] = t; }
    for (int i = 0; i < names.len; i++) {
        const char *name = names.items[i];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        char *path = strcmp(dir, ".") == 0 ? xs_sprintf("%s", name) : xs_sprintf("%s/%s", dir, name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { if (!skipped_dir(name)) walk(path, out); }
        else if (ts_file(name)) strs_push(out, path);
    }
}

/* the project's entry, read from xisto.json or package.json ("main"), checked first: its imports cover most files */
static char *project_main(const char *dir) {
    const char *metas[] = {"xisto.json", "package.json"};
    for (int k = 0; k < 2; k++) {
        char *meta = xs_read_file(strcmp(dir, ".") == 0 ? xs_sprintf("%s", metas[k]) : xs_sprintf("%s/%s", dir, metas[k]));
        if (!meta) continue;
        char *m = strstr(meta, "\"main\"");
        if (m && (m = strchr(m + 6, '"'))) {
            char *e = strchr(m + 1, '"');
            if (e) {
                char *rel = xs_sprintf("%.*s", (int)(e - m - 1), m + 1);
                char *path = strcmp(dir, ".") == 0 ? rel : xs_sprintf("%s/%s", dir, rel);
                if (access(path, R_OK) == 0 && ts_file(path)) return path;
            }
        }
    }
    return NULL;
}

/* ---------- packages ---------- */

typedef struct {
    char *name, *status;
    int files, test_files;
} Pkg;
static Pkg *pkgs;
static int npkgs;

static const char *package_status(const char *root, const char *name) {
    char dir[PATH_MAX];
    if (!xs_real_path(root, dir)) snprintf(dir, sizeof dir, "%s", root);
    for (;;) {
        if (access(xs_sprintf("%s/.xisto/pkgs/%s", dir, name), R_OK) == 0) return "installed (.xisto/pkgs)";
        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir) break;
        *slash = 0;
    }
    return "not installed";
}

/* a test file: counted apart from the application's code */
static bool test_file(const char *path) {
    size_t n = strlen(path);
    const char *suffixes[] = {".spec.ts", ".test.ts", ".spec.tsx", ".test.tsx", ".e2e-spec.ts", NULL};
    for (int i = 0; suffixes[i]; i++) {
        size_t k = strlen(suffixes[i]);
        if (n >= k && strcmp(path + n - k, suffixes[i]) == 0) return true;
    }
    return strstr(path, "__tests__/") || strstr(path, "__mocks__/");
}

/* the bare package names a file imports (not relative paths, not node:, xisto: or the built-in fs and sqlite) */
static void scan_imports(const char *root, const char *file) {
    char *src = xs_read_file(file);
    if (!src) return;
    Strs seen = {0};
    for (char *p = src; (p = strstr(p, "from ")) || (p = NULL); p++) {
        /* an import's or a re-export's "from": its line starts with import, export or the '}' that closes a list */
        char *ls = p;
        while (ls > src && ls[-1] != '\n') ls--;
        while (*ls == ' ' || *ls == '\t') ls++;
        if (strncmp(ls, "import", 6) != 0 && strncmp(ls, "export", 6) != 0 && *ls != '}') continue;
        char *q = p + 5;
        while (*q == ' ') q++;
        if (*q != '"' && *q != '\'') continue;
        char quote = *q++;
        char *end = strchr(q, quote);
        if (!end || end - q > 200) continue;
        char *spec = xs_sprintf("%.*s", (int)(end - q), q);
        if (spec[0] == '.' || spec[0] == '/' || strncmp(spec, "node:", 5) == 0 || strncmp(spec, "xisto:", 6) == 0 || strcmp(spec, "sqlite") == 0 ||
            xs_node_core_name(spec)) /* (a Node module without "node:" is counted as a Node module import) */
            continue;
        if (xs_tsconfig_resolve(file, spec)) continue; /* a tsconfig "paths" name: the project's own code */
        char *name = spec; /* "@scope/pkg/sub" -> "@scope/pkg", "pkg/sub" -> "pkg" */
        char *slash = strchr(spec[0] == '@' ? (strchr(spec, '/') ? strchr(spec, '/') + 1 : spec) : spec, '/');
        if (slash) name = xs_sprintf("%.*s", (int)(slash - spec), spec);
        if (strs_has(&seen, name)) continue;
        strs_push(&seen, name);
        int k = 0;
        while (k < npkgs && strcmp(pkgs[k].name, name) != 0) k++;
        if (k == npkgs) {
            pkgs = realloc(pkgs, sizeof(Pkg) * (size_t)(npkgs + 1));
            pkgs[npkgs++] = (Pkg){name, (char *)package_status(root, name), 0, 0};
        }
        if (test_file(file)) pkgs[k].test_files++; else pkgs[k].files++;
    }
}

/* ---------- the run ---------- */

/* a path as shown: relative to the folder that was checked (else to the current one) */
static char *display(const char *path, const char *cwd, const char *root) {
    char real[PATH_MAX];
    const char *p = xs_real_path(path, real) ? real : path;
    size_t r = strlen(root);
    if (r && strncmp(p, root, r) == 0 && p[r] == '/') return xs_sprintf("%s", p + r + 1);
    size_t n = strlen(cwd);
    if (strncmp(p, cwd, n) == 0 && p[n] == '/') return xs_sprintf("%s", p + n + 1);
    return xs_sprintf("%s", p);
}

/* the diagnostics already kept, by file, line, column and message (a hash set: a large project has tens of thousands) */
static char **seen_keys;
static size_t seen_cap, seen_len;
static uint64_t hash_str(const char *s) { uint64_t h = 1469598103934665603ull; while (*s) h = (h ^ (unsigned char)*s++) * 1099511628211ull; return h; }
static bool seen_add(const char *key) {
    if (seen_len * 2 >= seen_cap) {
        size_t nc = seen_cap ? seen_cap * 2 : 1024;
        char **nk = calloc(nc, sizeof(char *));
        for (size_t i = 0; i < seen_cap; i++)
            if (seen_keys[i]) { size_t j = hash_str(seen_keys[i]) & (nc - 1); while (nk[j]) j = (j + 1) & (nc - 1); nk[j] = seen_keys[i]; }
        free(seen_keys);
        seen_keys = nk;
        seen_cap = nc;
    }
    size_t j = hash_str(key) & (seen_cap - 1);
    while (seen_keys[j]) { if (strcmp(seen_keys[j], key) == 0) return false; j = (j + 1) & (seen_cap - 1); }
    seen_keys[j] = (char *)key;
    seen_len++;
    return true;
}

static void check_entry(const char *self, const char *file, Strs *covered, const char *cwd, const char *root_real) {
    char *cmd = xs_sprintf("'%s' __check1 '%s' 2>&1", self, file);
    FILE *f = popen(cmd, "r");
    if (!f) return;
    char *line = NULL;
    size_t cap = 0;
    bool done = false;
    while (getline(&line, &cap, f) > 0) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
        if (strncmp(line, "module\t", 7) == 0) { if (!strs_has(covered, line + 7)) strs_push(covered, xs_sprintf("%s", line + 7)); continue; }
        if (strncmp(line, "quiet\t", 6) == 0) { quiet_total += atoi(line + 6); done = true; continue; }
        if (strcmp(line, "done") == 0) { done = true; continue; }
        Diag d;
        if (!parse_diag(line, &d)) continue;
        char real[PATH_MAX];
        const char *rp = xs_real_path(d.file, real) ? real : d.file;
        size_t rn = strlen(root_real);
        if (strncmp(rp, root_real, rn) != 0 || strstr(rp, "/.xisto/") || strstr(rp, "/node_modules/")) continue; /* a package's own code */
        d.file = display(d.file, cwd, root_real);
        d.test = test_file(d.file);
        if (!seen_add(xs_sprintf("%s\x1f%d\x1f%d\x1f%s", d.file, d.line, d.col, d.message))) continue;
        if (ndiags == cap_diags) {
            cap_diags = cap_diags ? cap_diags * 2 : 64;
            diags = realloc(diags, sizeof(Diag) * (size_t)cap_diags);
        }
        diags[ndiags++] = d;
    }
    free(line);
    int st = pclose(f);
    (void)st;
    if (!done) crashed++; /* the child stopped without finishing (a compiler crash): its file's report may be partial */
}

static int cmp_diag(const void *a, const void *b) {
    const Diag *x = a, *y = b;
    int c = strcmp(x->file, y->file);
    if (c) return c;
    if (x->line != y->line) return x->line - y->line;
    return x->col - y->col;
}

static void count_table(bool want_test, bool all) {
    printf("\n  %-46s %7s  %5s  %s\n", "kind of change", "changes", "files", "fix");
    for (int c = 0; c < DC_COUNT; c++) {
        int n = 0;
        Strs fs = {0};
        for (int i = 0; i < ndiags; i++)
            if ((all || diags[i].test == want_test) && diags[i].category && strcmp(diags[i].category, xs_diag_cat_id((DiagCat)c)) == 0) {
                n++;
                if (!strs_has(&fs, diags[i].file)) strs_push(&fs, diags[i].file);
            }
        if (n) printf("  %-46s %7d  %5d  %s\n", xs_diag_cat_title((DiagCat)c), n, fs.len, xs_diag_cat_fix((DiagCat)c));
        free(fs.items);
    }
}

int cmd_check(int argc, char **argv) {
    bool details = false, with_tests = false;
    Strs paths = {0};
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--details") == 0) details = true;
        else if (strcmp(argv[i], "--tests") == 0) with_tests = true;
        else if (argv[i][0] == '-') { fprintf(stderr, "xisto check: unknown option '%s' (--details, --tests)\n", argv[i]); return 2; }
        else {
            char *p = xs_sprintf("%s", argv[i]);
            for (size_t n = strlen(p); n > 1 && p[n - 1] == '/'; n--) p[n - 1] = 0; /* "dir/" as "dir" */
            strs_push(&paths, p);
        }
    }
    if (!paths.len) strs_push(&paths, ".");
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) return 2;
    char self[PATH_MAX];
    if (!xs_real_path(argv[0], self) && !xs_real_path(xs_sprintf("/proc/self/exe"), self)) snprintf(self, sizeof self, "%s", argv[0]);

    Strs files = {0}, entries = {0};
    char root_real[PATH_MAX] = "";
    for (int i = 0; i < paths.len; i++) {
        const char *p = paths.items[i];
        struct stat st;
        if (stat(p, &st) != 0) { fprintf(stderr, "xisto check: cannot open '%s'\n", p); return 2; }
        const char *dir = p;
        size_t n = strlen(p);
        if (!S_ISDIR(st.st_mode) && n >= 13 && strcmp(p + n - 13, "tsconfig.json") == 0) { /* the project of a tsconfig.json: its folder */
            char *slash = strrchr(p, '/');
            dir = slash ? xs_sprintf("%.*s", (int)(slash - p), p) : ".";
        } else if (!S_ISDIR(st.st_mode)) {
            if (!ts_file(p)) { fprintf(stderr, "xisto check: '%s' is not a .ts file, a folder or a tsconfig.json\n", p); return 2; }
            strs_push(&files, (char *)p);
            strs_push(&entries, (char *)p);
            char *slash = strrchr(p, '/');
            dir = slash ? xs_sprintf("%.*s", (int)(slash - p), p) : ".";
            if (!root_real[0] && !xs_real_path(dir, root_real)) snprintf(root_real, sizeof root_real, "%s", dir);
            continue;
        }
        if (!root_real[0] && !xs_real_path(dir, root_real)) snprintf(root_real, sizeof root_real, "%s", dir);
        char *main_file = project_main(dir);
        if (main_file) strs_push(&entries, main_file);
        walk(dir, &files);
    }
    if (!files.len) { fprintf(stderr, "xisto check: no .ts files found\n"); return 2; }
    for (int i = 0; i < files.len; i++) if (!strs_has(&entries, files.items[i])) strs_push(&entries, files.items[i]);

    Strs covered = {0};
    for (int i = 0; i < entries.len; i++) {
        char real[PATH_MAX];
        if (xs_real_path(entries.items[i], real) && strs_has(&covered, real)) continue; /* checked as another file's import */
        check_entry(self, entries.items[i], &covered, cwd, root_real);
    }
    for (int i = 0; i < files.len; i++) scan_imports(root_real, files.items[i]);
    qsort(diags, (size_t)ndiags, sizeof(Diag), cmp_diag);

    bool tty = isatty(1);
    const char *B = tty ? "\033[1m" : "", *R = tty ? "\033[0m" : "", *D = tty ? "\033[2m" : "";
    int app_files = 0, test_files_n = 0;
    for (int i = 0; i < files.len; i++) { if (test_file(display(files.items[i], cwd, root_real))) test_files_n++; else app_files++; }
    Strs affected = {0}, test_affected = {0};
    int app_n = 0, test_n = 0;
    for (int i = 0; i < ndiags; i++) {
        if (diags[i].test) { test_n++; if (!strs_has(&test_affected, diags[i].file)) strs_push(&test_affected, diags[i].file); }
        else { app_n++; if (!strs_has(&affected, diags[i].file)) strs_push(&affected, diags[i].file); }
    }
    int shown_n = with_tests ? ndiags : app_n;
    int shown_files = with_tests ? app_files + test_files_n : app_files;
    int shown_affected = affected.len + (with_tests ? test_affected.len : 0);

    if (details) {
        for (int i = 0; i < ndiags;) {
            int j = i;
            while (j < ndiags && strcmp(diags[j].file, diags[i].file) == 0) j++;
            if (diags[i].test && !with_tests) { i = j; continue; }
            printf("%s%s%s (%d)\n", B, diags[i].file, R, j - i);
            for (int k = i; k < j; k++) {
                Diag *d = &diags[k];
                printf("  %d:%d  %s  %s\n", d->line, d->col, d->code ? d->code : "", d->message);
                if (d->source && *d->source) {
                    printf("    %s%4d |%s %s\n", D, d->line, R, d->source);
                    printf("    %s     |%s %*s^\n", D, R, d->col > 0 ? d->col - 1 : 0, "");
                }
                if (d->help) printf("    help: %s\n", d->help);
            }
            printf("\n");
            i = j;
        }
    }

    if (!shown_n) printf("xisto check: %d file%s, no changes needed\n", shown_files, shown_files == 1 ? "" : "s");
    else printf("%sxisto check: %d file%s, %d change%s needed in %d file%s%s\n", B, shown_files, shown_files == 1 ? "" : "s", shown_n,
                shown_n == 1 ? "" : "s", shown_affected, shown_affected == 1 ? "" : "s", R);
    if (shown_n) {
        count_table(false, with_tests);
        if (quiet_total) printf("  (%d follow-on error%s of these hidden)\n", quiet_total, quiet_total == 1 ? "" : "s");
        /* the files with the most changes */
        Strs *af = &affected;
        Strs both = {0};
        if (with_tests) { for (int k = 0; k < affected.len; k++) strs_push(&both, affected.items[k]); for (int k = 0; k < test_affected.len; k++) strs_push(&both, test_affected.items[k]); af = &both; }
        int *counts = calloc((size_t)af->len + 1, sizeof(int));
        for (int i = 0; i < ndiags; i++)
            for (int k = 0; k < af->len; k++) if (strcmp(diags[i].file, af->items[k]) == 0) { counts[k]++; break; }
        printf("\n  most changes:\n");
        for (int shown = 0; shown < 5 && shown < af->len; shown++) {
            int best = -1;
            for (int k = 0; k < af->len; k++) if (counts[k] >= 0 && (best < 0 || counts[k] > counts[best])) best = k;
            printf("    %5d  %s\n", counts[best], af->items[best]);
            counts[best] = -1;
        }
        free(counts);
    }
    if (!with_tests && test_files_n)
        printf("\n  tests: %d change%s in %d of %d test file%s, not counted above (--tests counts them)\n", test_n, test_n == 1 ? "" : "s",
               test_affected.len, test_files_n, test_files_n == 1 ? "" : "s");
    if (npkgs) {
        printf("\n  packages:\n");
        for (int k = 0; k < npkgs; k++) {
            int nf = pkgs[k].files + (with_tests ? pkgs[k].test_files : 0);
            if (!with_tests && !pkgs[k].files) continue;
            printf("    %-32s %-24s %d file%s", pkgs[k].name, pkgs[k].status, nf, nf == 1 ? "" : "s");
            if (strcmp(pkgs[k].status, "not installed") == 0) printf("  (xisto add <git url> --name %s)", pkgs[k].name);
            printf("\n");
        }
        int test_only = 0;
        for (int k = 0; k < npkgs; k++) if (!pkgs[k].files) test_only++;
        if (!with_tests && test_only) printf("    (and %d package%s only the tests import)\n", test_only, test_only == 1 ? "" : "s");
    }
    if (crashed) printf("\n  note: the compiler stopped early on %d file%s; their changes may be incomplete\n", crashed, crashed == 1 ? "" : "s");
    if (shown_n && !details) printf("\n  xisto check --details lists every place to change.\n");
    return shown_n ? 1 : 0;
}
