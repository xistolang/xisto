/* Module names a project maps in its tsconfig.json: compilerOptions.paths ("@app/*": ["libs/app/src/*"]) under
   compilerOptions.baseUrl, through "extends" (a relative file). The tsconfig is the nearest one above the importing
   file; a child's paths replace its parent's, and the paths resolve against the baseUrl of the file that sets it,
   else against the folder of the tsconfig that sets the paths (TypeScript 4.1 and later).

   Also the Node modules a file may import without "node:" (path, os, crypto...), which xisto reads as node:x. */
#include "xisto.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *node_core[] = {"assert", "async_hooks", "buffer", "child_process", "cluster", "console", "crypto", "dgram", "dns",
                                  "events", "fs", "http", "http2", "https", "module", "net", "os", "path", "perf_hooks", "process",
                                  "querystring", "readline", "stream", "string_decoder", "timers", "tls", "tty", "url", "util", "v8",
                                  "vm", "worker_threads", "zlib", NULL};

/* "path", "fs/promises", "stream/web": a Node module named without the "node:" prefix */
bool xs_node_core_name(const char *spec) {
    size_t n = strcspn(spec, "/");
    for (int i = 0; node_core[i]; i++)
        if (strlen(node_core[i]) == n && strncmp(spec, node_core[i], n) == 0) return true;
    return false;
}

/* ---------- a tolerant reader of tsconfig JSON (comments and trailing commas allowed) ---------- */

static char *strip_comments(const char *s) {
    size_t n = strlen(s);
    char *o = malloc(n + 1), *w = o;
    for (size_t i = 0; i < n;) {
        if (s[i] == '"') { /* a string, copied whole */
            *w++ = s[i++];
            while (i < n && s[i] != '"') { if (s[i] == '\\' && i + 1 < n) *w++ = s[i++]; *w++ = s[i++]; }
            if (i < n) *w++ = s[i++];
        } else if (s[i] == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') i++;
        } else if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i += 2;
        } else *w++ = s[i++];
    }
    *w = 0;
    return o;
}

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static char *read_str(const char **pp) {
    const char *p = skip_ws(*pp);
    if (*p != '"') return NULL;
    p++;
    const char *b = p;
    while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
    char *v = xs_strndup(b, (size_t)(p - b));
    if (*p == '"') p++;
    *pp = p;
    return v;
}

/* past one JSON value (object, array, string or scalar) */
static const char *skip_value(const char *p) {
    p = skip_ws(p);
    if (*p == '"') { const char *q = p; char *s = read_str(&q); (void)s; return q; }
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') { const char *q = p; read_str(&q); p = q; continue; }
            if (*p == '{' || *p == '[') depth++;
            if (*p == '}' || *p == ']') { depth--; if (!depth) return p + 1; }
            p++;
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}

/* the value of key in the object that starts at obj ('{'), or NULL */
static const char *find_key(const char *obj, const char *key) {
    const char *p = skip_ws(obj);
    if (*p != '{') return NULL;
    p++;
    for (;;) {
        p = skip_ws(p);
        if (*p == '}' || !*p) return NULL;
        if (*p == ',') { p++; continue; }
        char *k = read_str(&p);
        if (!k) return NULL;
        p = skip_ws(p);
        if (*p == ':') p++;
        p = skip_ws(p);
        if (strcmp(k, key) == 0) return p;
        p = skip_value(p);
    }
}

typedef struct {
    char *pattern;  /* "@app/*" */
    char **targets; /* "libs/app/src/*", absolute after reading */
    int ntargets;
} PathMap;

typedef struct {
    char *file; /* the tsconfig.json, resolved */
    PathMap *maps;
    int nmaps;
    bool read;
} TsConfig;

static TsConfig *configs;
static int nconfigs;

static char *dir_of(const char *file) {
    const char *s = strrchr(file, '/');
    return s ? xs_strndup(file, (size_t)(s - file)) : xs_strndup(".", 1);
}

/* the paths of one tsconfig file, through its extends: base_dir is where its targets resolve */
static void read_config(const char *file, PathMap **maps, int *nmaps, char **base_url, char **paths_dir, int depth) {
    if (depth > 8) return;
    char *raw = xs_read_file(file);
    if (!raw) return;
    char *src = strip_comments(raw);
    char *dir = dir_of(file);
    const char *ext = find_key(src, "extends");
    if (ext && *ext == '"') { /* the parent first: this file's settings replace its */
        char *e = read_str(&ext);
        if (e && (e[0] == '.' || e[0] == '/')) {
            char *pf = e[0] == '/' ? e : xs_sprintf("%s/%s", dir, e);
            size_t n = strlen(pf);
            if (access(pf, R_OK) != 0 && !(n > 5 && strcmp(pf + n - 5, ".json") == 0)) pf = xs_sprintf("%s.json", pf);
            read_config(pf, maps, nmaps, base_url, paths_dir, depth + 1);
        }
    }
    const char *co = find_key(src, "compilerOptions");
    if (!co) return;
    const char *bu = find_key(co, "baseUrl");
    if (bu && *bu == '"') { char *b = read_str(&bu); if (b) *base_url = b[0] == '/' ? b : xs_sprintf("%s/%s", dir, b); }
    const char *pa = find_key(co, "paths");
    if (!pa || *pa != '{') return;
    *paths_dir = dir; /* the folder of the tsconfig that sets the paths (when no baseUrl) */
    *maps = NULL;
    *nmaps = 0;
    const char *p = pa + 1;
    for (;;) {
        p = skip_ws(p);
        if (*p == '}' || !*p) break;
        if (*p == ',') { p++; continue; }
        char *pat = read_str(&p);
        if (!pat) break;
        p = skip_ws(p);
        if (*p == ':') p++;
        p = skip_ws(p);
        PathMap m = {pat, NULL, 0};
        if (*p == '[') {
            p++;
            for (;;) {
                p = skip_ws(p);
                if (*p == ']') { p++; break; }
                if (*p == ',') { p++; continue; }
                char *t = read_str(&p);
                if (!t) { p = skip_value(p); continue; }
                m.targets = realloc(m.targets, sizeof(char *) * (size_t)(m.ntargets + 1));
                m.targets[m.ntargets++] = t;
            }
        } else p = skip_value(p);
        *maps = realloc(*maps, sizeof(PathMap) * (size_t)(*nmaps + 1));
        (*maps)[(*nmaps)++] = m;
    }
}

static TsConfig *config_for(const char *from_file) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", from_file);
    for (;;) {
        char *slash = strrchr(dir, '/');
        if (!slash) return NULL;
        *slash = 0;
        char *probe = xs_sprintf("%s/tsconfig.json", dir[0] ? dir : "");
        if (access(probe, R_OK) != 0) { /* an Nx project: tsconfig.app.json (or .lib.json) is the project's own */
            probe = xs_sprintf("%s/tsconfig.app.json", dir[0] ? dir : "");
            if (access(probe, R_OK) != 0) probe = xs_sprintf("%s/tsconfig.lib.json", dir[0] ? dir : "");
            if (access(probe, R_OK) != 0) continue;
        }
        for (int i = 0; i < nconfigs; i++) if (strcmp(configs[i].file, probe) == 0) return &configs[i];
        configs = realloc(configs, sizeof(TsConfig) * (size_t)(nconfigs + 1));
        TsConfig *c = &configs[nconfigs++];
        memset(c, 0, sizeof *c);
        c->file = probe;
        char *base_url = NULL, *paths_dir = NULL;
        read_config(probe, &c->maps, &c->nmaps, &base_url, &paths_dir, 0);
        const char *root = base_url ? base_url : paths_dir;
        for (int i = 0; i < c->nmaps; i++)
            for (int k = 0; k < c->maps[i].ntargets; k++)
                if (c->maps[i].targets[k][0] != '/' && root) c->maps[i].targets[k] = xs_sprintf("%s/%s", root, c->maps[i].targets[k]);
        c->read = true;
        return c;
    }
}

static bool exists_module(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return true; /* a file, or a folder with an index */
    return access(xs_sprintf("%s.ts", path), R_OK) == 0 || access(xs_sprintf("%s.tsx", path), R_OK) == 0;
}

/* the file or folder a tsconfig path maps spec to, from the importing file's project; NULL when none matches */
char *xs_tsconfig_resolve(const char *from_file, const char *spec) {
    if (!from_file || !spec[0] || spec[0] == '.' || spec[0] == '/') return NULL;
    TsConfig *c = config_for(from_file);
    if (!c) return NULL;
    PathMap *best = NULL;
    size_t best_prefix = 0;
    const char *best_star = NULL;
    for (int i = 0; i < c->nmaps; i++) { /* the longest prefix wins, as TypeScript picks */
        const char *pat = c->maps[i].pattern;
        const char *star = strchr(pat, '*');
        if (!star) {
            if (strcmp(pat, spec) == 0 && (!best || strlen(pat) > best_prefix)) { best = &c->maps[i]; best_prefix = strlen(pat); best_star = NULL; }
            continue;
        }
        size_t pre = (size_t)(star - pat), suf = strlen(star + 1), n = strlen(spec);
        if (n >= pre + suf && strncmp(spec, pat, pre) == 0 && strcmp(spec + n - suf, star + 1) == 0 && (!best || pre > best_prefix)) {
            best = &c->maps[i];
            best_prefix = pre;
            best_star = spec + pre;
        }
    }
    if (!best) return NULL;
    for (int k = 0; k < best->ntargets; k++) {
        const char *t = best->targets[k];
        char *path;
        const char *ts = strchr(t, '*');
        if (ts && best_star) {
            size_t pre = best_prefix, suf = strlen(best->pattern) - pre - 1;
            size_t mid = strlen(best_star) - suf;
            path = xs_sprintf("%.*s%.*s%s", (int)(ts - t), t, (int)mid, best_star, ts + 1);
        } else path = xs_sprintf("%s", t);
        if (exists_module(path)) return path;
    }
    return NULL;
}
