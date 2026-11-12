/* Packages: xisto init / add / install, and the project's entry for `xisto build` without a file.

   A package is source code in a git repository: the whole repository (xisto add URL), or one folder of a
   monorepo (xisto add URL#SUBDIR), or a name in the registry, the monorepo github.com/xistolang/packages
   (xisto add @nestjs/common: SUBDIR is the name). Each repository is cloned once, bare, into a shared cache
   (~/.cache/xisto/git, or $XDG_CACHE_HOME/xisto/git); the package's folder at the chosen commit is then copied
   (git archive) into <project>/.xisto/pkgs/<name>, so a project never depends on the cache once installed.

   xisto.json   {"name": ..., "main": ..., "dependencies": {"name": "SPEC"}}: what the user asked for. SPEC is
                "*" (the registry's default branch), a ref of the registry ("v1.2"), or SOURCE[#SUBDIR][@REF].
   xisto.lock   {"name": {"source": ..., "subdir": ..., "ref": ..., "commit": ..., "indirect": true}}, one line per
                package (direct and indirect), so it diffs cleanly; "subdir", "ref" and "indirect" only when set.
                Older locks ({"source", "commit"}) read the same.

   A package's own xisto.json "dependencies" (name -> version) come from the same source at the same commit
   (a monorepo is consistent at a commit); a dependency given as a git URL comes from that URL. A dependency
   already locked at another commit stays there, with a warning, when the user asked for it or another package
   needs it; else it moves with its dependent. Indirect packages nothing needs any more are removed. Nothing from a
   package is ever run at install time: a package is only source code. */
#include "xisto.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_REGISTRY "https://github.com/xistolang/packages"

/* ---- a small JSON reader, enough for xisto.json and xisto.lock ---- */

static const char *js_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* p at '"': the string's text (unescaped) in *out; returns the position after the closing quote */
static const char *js_str(const char *p, char **out) {
    Buf b = {0};
    buf_puts(&b, "");
    for (p++; *p && *p != '"'; p++) {
        if (*p != '\\' || !p[1]) { buf_putc(&b, *p); continue; }
        char c = *++p;
        switch (c) {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'u': {
            unsigned v = 0;
            int k = 1;
            for (; k <= 4 && isxdigit((unsigned char)p[k]); k++) v = v * 16 + (unsigned)(isdigit((unsigned char)p[k]) ? p[k] - '0' : tolower((unsigned char)p[k]) - 'a' + 10);
            p += k - 1;
            c = v < 128 ? (char)v : '?';
            break;
        }
        default: break;
        }
        buf_putc(&b, c);
    }
    if (out) *out = b.data;
    return *p ? p + 1 : p;
}

static const char *js_skip(const char *p) {
    p = js_ws(p);
    if (*p == '"') return js_str(p, NULL);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') { p = js_str(p, NULL); continue; }
            if (*p == '{' || *p == '[') depth++;
            else if (*p == '}' || *p == ']') { if (--depth == 0) return p + 1; }
            p++;
        }
        return p;
    }
    while (*p && !strchr(",}] \t\r\n", *p)) p++;
    return p;
}

typedef struct { const char *p; } JsIt;

static bool js_obj(JsIt *it, const char *v) {
    if (!v) return false;
    v = js_ws(v);
    if (*v != '{') return false;
    it->p = v + 1;
    return true;
}

static bool js_next(JsIt *it, char **key, const char **val) {
    const char *p = js_ws(it->p);
    if (*p == ',') p = js_ws(p + 1);
    if (*p != '"') return false;
    p = js_ws(js_str(p, key));
    if (*p != ':') return false;
    *val = js_ws(p + 1);
    it->p = js_skip(*val);
    return true;
}

static const char *js_get(const char *obj, const char *key) {
    JsIt it;
    char *k;
    const char *v;
    if (!js_obj(&it, obj)) return NULL;
    while (js_next(&it, &k, &v)) if (strcmp(k, key) == 0) return v;
    return NULL;
}

static char *js_get_str(const char *obj, const char *key) {
    const char *v = js_get(obj, key);
    if (!v || *v != '"') return NULL;
    char *s;
    js_str(v, &s);
    return *s ? s : NULL;
}

static void js_put(Buf *b, const char *s) {
    buf_putc(b, '"');
    for (; *s; s++) {
        if ((unsigned char)*s < 0x20) { buf_printf(b, "\\u%04x", (unsigned)(unsigned char)*s); continue; }
        if (*s == '"' || *s == '\\') buf_putc(b, '\\');
        buf_putc(b, *s);
    }
    buf_putc(b, '"');
}

/* ---- names, sources, refs ---- */

static char *sdup(const char *s) { return s ? xs_strndup(s, strlen(s)) : NULL; }

static bool seg_ok(const char *s, size_t n, bool subdir) {
    if (n == 0 || (n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.')) return false;
    if (!subdir && s[0] == '.') return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '-' || c == '_' || c == '.' || (subdir && (c == '@' || c == '+')))) return false;
    }
    return true;
}

/* a package name: "name" or "@scope/name" (as npm's) */
static bool valid_pkg_name(const char *s) {
    if (!s || !*s) return false;
    if (s[0] == '@') {
        const char *slash = strchr(s, '/');
        return slash && seg_ok(s + 1, (size_t)(slash - s - 1), false) && seg_ok(slash + 1, strlen(slash + 1), false);
    }
    return seg_ok(s, strlen(s), false);
}

/* a folder of a repository: relative, no "." or ".." segments */
static bool valid_subdir(const char *s) {
    if (!s || !*s || *s == '/') return false;
    for (;;) {
        const char *slash = strchr(s, '/');
        size_t n = slash ? (size_t)(slash - s) : strlen(s);
        if (!seg_ok(s, n, true)) return false;
        if (!slash) return true;
        s = slash + 1;
    }
}

static bool valid_ref(const char *s) {
    if (!s || !*s || *s == '-') return false;
    for (; *s; s++) if ((unsigned char)*s <= ' ' || strchr("\"'\\~^:?*[", *s)) return false;
    return true;
}

static bool valid_commit(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n < 7) return false;
    for (; *s; s++) if (!isxdigit((unsigned char)*s)) return false;
    return true;
}

static bool is_url(const char *s) {
    if (strstr(s, "://")) return true;
    const char *colon = strchr(s, ':'), *slash = strchr(s, '/');
    return colon && colon - s > 1 && (!slash || colon < slash); /* git@host:path (C:/x is a Windows path) */
}

/* a git URL or a path (else a package name of the registry) */
static bool is_location(const char *s) { return strchr(s, ':') || s[0] == '/' || s[0] == '.' || s[0] == '~' || s[0] == '\\'; }

/* a URL as printed: without the user and password (https://user:token@host/...) */
static const char *shown(const char *src) {
    const char *p = strstr(src, "://");
    if (!p) return src;
    const char *at = strchr(p + 3, '@'), *slash = strchr(p + 3, '/');
    if (!at || (slash && slash < at)) return src;
    return xs_sprintf("%.*s%s", (int)(p + 3 - src), src, at + 1);
}

static const char *registry(void) {
    const char *r = getenv("XISTO_REGISTRY");
    return r && *r ? r : DEFAULT_REGISTRY;
}

/* "path@ref", path's segments possibly scoped ("@scope/name@ref"): the ref's @ is the last one not opening a segment */
static void split_ref(char *s, char **ref) {
    *ref = NULL;
    char *at = strrchr(s, '@');
    if (at && at != s && at[-1] != '/') { *at = 0; *ref = at + 1; }
}

typedef struct {
    char *source, *subdir, *ref;
    bool registry; /* a bare name: source is the registry, subdir the name */
} Spec;

/* SOURCE[@REF], SOURCE#SUBDIR[@REF] or NAME[@REF]; false (and the error printed) when malformed */
static bool parse_spec(const char *arg, Spec *s) {
    memset(s, 0, sizeof *s);
    char *t = sdup(arg);
    if (is_location(t)) {
        char *hash = strchr(t, '#');
        if (hash) {
            *hash = 0;
            s->subdir = hash + 1;
            split_ref(s->subdir, &s->ref);
            if (!valid_subdir(s->subdir)) { fprintf(stderr, "xisto: invalid folder '%s' in '%s' (a relative path inside the repository, without '..')\n", s->subdir, arg); return false; }
        } else {
            char *at = strrchr(t, '@');
            if (at && at != t && !strchr(at, '/') && !strchr(at, ':')) { *at = 0; s->ref = at + 1; }
        }
        s->source = t;
        if (!*t) { fprintf(stderr, "xisto: missing repository in '%s'\n", arg); return false; }
    } else {
        split_ref(t, &s->ref);
        if (!valid_pkg_name(t)) { fprintf(stderr, "xisto: invalid package name '%s' (a name is 'name' or '@scope/name': letters, digits, '-', '_', '.')\n", t); return false; }
        s->registry = true;
        s->subdir = t;
        s->source = sdup(registry());
    }
    if (s->ref && !valid_ref(s->ref)) { fprintf(stderr, "xisto: invalid revision '%s' in '%s'\n", s->ref, arg); return false; }
    return true;
}

/* ---- the cache of repositories ---- */

static char *run_capture(const char *cmd, bool first_line) {
#if defined(_WIN32)
    FILE *f = popen(xs_sprintf("\"%s\"", cmd), "r"); /* (cmd.exe drops the line's first quote) */
#else
    FILE *f = popen(cmd, "r");
#endif
    if (!f) return NULL;
    Buf b = {0};
    buf_puts(&b, "");
    char tmp[4096];
    size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) for (size_t i = 0; i < n; i++) buf_putc(&b, tmp[i]);
    if (pclose(f) != 0) return NULL;
    if (first_line) {
        char *nl = strpbrk(b.data, "\r\n");
        if (nl) *nl = 0;
        if (!*b.data) return NULL;
    }
    return b.data;
}

static char *git_out(const char *repo, const char *args, bool first_line) {
    return run_capture(xs_sprintf("git -C %s %s 2>%s", xs_q(repo), args, xs_devnull()), first_line);
}

static const char *cache_root(void) {
    const char *x = getenv("XDG_CACHE_HOME");
    if (x && *x) return xs_sprintf("%s/xisto/git", x);
#if defined(_WIN32)
    const char *l = getenv("LOCALAPPDATA");
    if (l && *l) return xs_sprintf("%s/xisto/git", l);
#endif
    return xs_sprintf("%s/.cache/xisto/git", xs_home());
}

/* what git clones, and the cache's key: a relative path made absolute (the cache is shared by every project).
   Symlinks are not resolved: the key stays the same when the folder is gone (xisto install from the cache). */
static char *clone_source(const char *src) {
    char cwd[PATH_MAX];
    char *s = sdup(src);
    size_t n = strlen(s);
    while (n > 1 && s[n - 1] == '/') s[--n] = 0;
    if (is_url(s) || s[0] == '/' || (isalpha((unsigned char)s[0]) && s[1] == ':') || !getcwd(cwd, sizeof cwd)) return s;
    if (strncmp(s, "./", 2) == 0) s += 2;
    return xs_sprintf("%s/%s", cwd, s);
}

static uint64_t fnv64(const char *s) {
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ull; }
    return h;
}

static char *fetched[256];
static int nfetched;

static bool fetch_repo(const char *repo, const char *key) {
    for (int i = 0; i < nfetched; i++) if (strcmp(fetched[i], repo) == 0) return true;
    if (nfetched < 256) fetched[nfetched++] = sdup(repo);
    return xs_sh(xs_sprintf("git -C %s fetch --quiet --prune --tags -- %s %s", xs_q(repo), xs_q(key), xs_q("+refs/heads/*:refs/heads/*"))) == 0;
}

/* the bare clone of source in the cache (cloned on first use); update: fetch it (once per run) */
static char *repo_cache(const char *source, bool update) {
    char *key = clone_source(source);
    const char *base = strrchr(key, '/');
    base = base ? base + 1 : key;
    char clean[48];
    size_t n = 0;
    for (const char *c = base; *c && n < sizeof clean - 1; c++) clean[n++] = isalnum((unsigned char)*c) || *c == '-' || *c == '_' ? *c : '_';
    clean[n] = 0;
    const char *root = cache_root();
    char *dir = xs_sprintf("%s/%s-%016llx", root, clean, (unsigned long long)fnv64(key));
    if (!xs_is_dir(dir)) {
        if (!xs_mkdirs(root)) { fprintf(stderr, "xisto: cannot create the cache folder %s\n", root); return NULL; }
        char *tmp = xs_sprintf("%s.tmp%d", dir, xs_pid());
        xs_rmtree(tmp);
        if (xs_sh(xs_sprintf("git clone --bare --quiet -- %s %s", xs_q(key), xs_q(tmp))) != 0) {
            xs_rmtree(tmp);
            fprintf(stderr, "xisto: cannot clone %s\n", shown(source));
            return NULL;
        }
        if (!xs_rename(tmp, dir)) xs_rmtree(tmp); /* another xisto cloned it meanwhile */
        if (nfetched < 256) fetched[nfetched++] = sdup(dir);
    } else if (update && !fetch_repo(dir, key)) {
        fprintf(stderr, "xisto: warning: cannot fetch %s: using the copy in the cache\n", shown(source));
    }
    return dir;
}

static char *rev_commit(const char *repo, const char *rev) {
    return git_out(repo, xs_sprintf("rev-parse --verify --quiet %s", xs_q(xs_sprintf("%s^{commit}", rev))), true);
}

/* the commit in the cache, fetched when it is not there yet */
static bool have_commit(const char *repo, const char *source, const char *commit) {
    if (rev_commit(repo, commit)) return true;
    char *key = clone_source(source);
    fetch_repo(repo, key);
    if (rev_commit(repo, commit)) return true;
    xs_sh(xs_sprintf("git -C %s fetch --quiet -- %s %s 2>%s", xs_q(repo), xs_q(key), xs_q(commit), xs_devnull()));
    return rev_commit(repo, commit) != NULL;
}

/* the folder subdir (NULL: the whole repository) at commit, copied into dest */
static bool extract(const char *repo, const char *commit, const char *subdir, const char *dest) {
    xs_rmtree(dest);
    if (!xs_mkdirs(dest)) { fprintf(stderr, "xisto: cannot create %s\n", dest); return false; }
    const char *tree = subdir ? xs_sprintf("%s:%s", commit, subdir) : commit;
    if (xs_sh(xs_sprintf("git -C %s archive --format=tar %s | tar -xf - -C %s", xs_q(repo), xs_q(tree), xs_q(dest))) != 0) {
        fprintf(stderr, "xisto: cannot copy the package into %s\n", dest);
        return false;
    }
    return true;
}

/* ---- the project's packages ---- */

typedef struct {
    char *name, *source, *subdir, *ref, *commit;
    bool indirect, touched; /* touched: installed by this run */
} Dep;
typedef struct { char *name, *spec; } Direct;

#define MAXDEPS 1024
static Dep deps[MAXDEPS];
static int ndeps;
static Direct direct[MAXDEPS];
static int ndirect;
static int errors;

static Dep *dep_find(const char *name) {
    for (int i = 0; i < ndeps; i++) if (strcmp(deps[i].name, name) == 0) return &deps[i];
    return NULL;
}

static void read_project(void) {
    char *lock = xs_read_file("xisto.lock");
    JsIt it;
    char *k;
    const char *v;
    if (lock && js_obj(&it, lock)) {
        while (js_next(&it, &k, &v) && ndeps < MAXDEPS) {
            Dep *d = &deps[ndeps];
            memset(d, 0, sizeof *d);
            d->name = k;
            d->source = js_get_str(v, "source");
            d->subdir = js_get_str(v, "subdir");
            d->ref = js_get_str(v, "ref");
            d->commit = js_get_str(v, "commit");
            const char *ind = js_get(v, "indirect");
            d->indirect = ind && strncmp(ind, "true", 4) == 0;
            if (d->source && d->commit) ndeps++;
        }
    }
    char *meta = xs_read_file("xisto.json");
    if (meta && js_obj(&it, js_get(meta, "dependencies"))) {
        while (js_next(&it, &k, &v) && ndirect < MAXDEPS) {
            if (*v != '"') continue;
            direct[ndirect].name = k;
            js_str(v, &direct[ndirect].spec);
            ndirect++;
        }
    }
}

static int dep_cmp(const void *a, const void *b) { return strcmp(((const Dep *)a)->name, ((const Dep *)b)->name); }

static bool write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "xisto: cannot write %s\n", path); return false; }
    fputs(text, f);
    fclose(f);
    return true;
}

static void write_project(void) {
    qsort(deps, (size_t)ndeps, sizeof deps[0], dep_cmp);
    Buf b = {0};
    buf_puts(&b, "{\n");
    for (int i = 0; i < ndeps; i++) {
        Dep *d = &deps[i];
        buf_puts(&b, "  ");
        js_put(&b, d->name);
        buf_puts(&b, ": {\"source\": ");
        js_put(&b, d->source);
        if (d->subdir) { buf_puts(&b, ", \"subdir\": "); js_put(&b, d->subdir); }
        if (d->ref) { buf_puts(&b, ", \"ref\": "); js_put(&b, d->ref); }
        buf_puts(&b, ", \"commit\": ");
        js_put(&b, d->commit);
        if (d->indirect) buf_puts(&b, ", \"indirect\": true");
        buf_puts(&b, i + 1 < ndeps ? "},\n" : "}\n");
    }
    buf_puts(&b, "}\n");
    write_text("xisto.lock", b.data);

    Buf o = {0};
    buf_puts(&o, ndirect ? "{\n" : "{}");
    for (int i = 0; i < ndirect; i++) {
        buf_puts(&o, "    ");
        js_put(&o, direct[i].name);
        buf_puts(&o, ": ");
        js_put(&o, direct[i].spec);
        buf_puts(&o, i + 1 < ndirect ? ",\n" : "\n  }");
    }
    char *t = xs_read_file("xisto.json");
    Buf j = {0};
    const char *v = t ? js_get(t, "dependencies") : NULL;
    if (v && *v == '{') { /* only the dependencies change: the rest of the file stays as written */
        buf_printf(&j, "%.*s", (int)(v - t), t);
        buf_puts(&j, o.data);
        buf_puts(&j, js_skip(v));
    } else if (t && strrchr(t, '}') && strchr(t, '{')) {
        char *end = strrchr(t, '}');
        char *last = end;
        while (last > t && isspace((unsigned char)last[-1])) last--;
        bool empty = last[-1] == '{';
        buf_printf(&j, "%.*s%s\n  \"dependencies\": %s\n%s", (int)(last - t), t, empty ? "" : ",", o.data, end);
    } else {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "app");
        const char *base = strrchr(cwd, '/') ? strrchr(cwd, '/') + 1 : cwd;
        buf_puts(&j, "{\n  \"name\": ");
        js_put(&j, base);
        buf_printf(&j, ",\n  \"dependencies\": %s\n}\n", o.data);
    }
    write_text("xisto.json", j.data);
}

static void set_direct(const char *name, const char *spec) {
    for (int i = 0; i < ndirect; i++)
        if (strcmp(direct[i].name, name) == 0) { direct[i].spec = sdup(spec); return; }
    if (ndirect < MAXDEPS) { direct[ndirect].name = sdup(name); direct[ndirect].spec = sdup(spec); ndirect++; }
}

static char *pkg_dir(const char *name) { return xs_sprintf(".xisto/pkgs/%s", name); }

/* a locked package back into .xisto/pkgs, from the cache */
static bool restore(Dep *d) {
    if (!valid_pkg_name(d->name) || (d->subdir && !valid_subdir(d->subdir)) || !valid_commit(d->commit)) {
        fprintf(stderr, "xisto: xisto.lock: invalid entry '%s'\n", d->name);
        return false;
    }
    char *repo = repo_cache(d->source, false);
    if (!repo) return false;
    if (!have_commit(repo, d->source, d->commit)) { fprintf(stderr, "xisto: %s has no commit %s (%s)\n", shown(d->source), d->commit, d->name); return false; }
    return extract(repo, d->commit, d->subdir, pkg_dir(d->name));
}

/* does a package this run has not installed (re)need name? (its installed xisto.json lists it) */
static bool needed_by_others(const char *name) {
    for (int i = 0; i < ndeps; i++) {
        if (deps[i].touched || strcmp(deps[i].name, name) == 0) continue;
        char *meta = xs_read_file(xs_sprintf("%s/xisto.json", pkg_dir(deps[i].name)));
        if (meta && js_get(js_get(meta, "dependencies"), name)) return true;
    }
    return false;
}

#define MAXSTACK 64
static const char *stack[MAXSTACK];
static int nstack;

/* install the package at subdir (NULL: the whole repository) of source, at ref (NULL: the default branch) or at
   commit; name: --name, else its xisto.json "name", else the folder's or the repository's name.
   want: the name asked for (errors); needed_by: the dependent package (NULL: the user asked for it).
   Returns the installed package's lock entry, or NULL (the error printed). */
static Dep *install(const char *source, const char *subdir, const char *ref, const char *commit, const char *name,
                    const char *want, const char *needed_by, bool update) {
    char *repo = repo_cache(source, update);
    if (!repo) return NULL;
    const char *by = needed_by ? xs_sprintf(" (needed by %s)", needed_by) : "";
    if (commit) {
        if (!have_commit(repo, source, commit)) { fprintf(stderr, "xisto: %s has no commit %s%s\n", shown(source), commit, by); return NULL; }
        commit = rev_commit(repo, commit);
    } else {
        commit = rev_commit(repo, ref ? ref : "HEAD");
        if (!commit) {
            if (ref) fprintf(stderr, "xisto: %s has no revision '%s'%s\n", shown(source), ref, by);
            else fprintf(stderr, "xisto: %s has no commits\n", shown(source));
            return NULL;
        }
    }
    if (subdir) {
        char *type = git_out(repo, xs_sprintf("cat-file -t %s", xs_q(xs_sprintf("%s:%s", commit, subdir))), true);
        if (!type || strcmp(type, "tree") != 0) {
            fprintf(stderr, "xisto: package '%s' not found in %s at %.12s%s\n", want ? want : subdir, shown(source), commit, by);
            return NULL;
        }
    }
    char *meta = git_out(repo, xs_sprintf("show %s", xs_q(subdir ? xs_sprintf("%s:%s/xisto.json", commit, subdir) : xs_sprintf("%s:xisto.json", commit))), false);
    char *meta_name = meta ? js_get_str(meta, "name") : NULL;
    if (!name) name = meta_name;
    if (!name && subdir) name = valid_pkg_name(subdir) ? subdir : (strrchr(subdir, '/') ? strrchr(subdir, '/') + 1 : subdir);
    if (!name) {
        const char *b = strrchr(source, '/') ? strrchr(source, '/') + 1 : strrchr(source, ':') ? strrchr(source, ':') + 1 : source;
        char *n = sdup(b);
        size_t l = strlen(n);
        if (l > 4 && strcmp(n + l - 4, ".git") == 0) n[l - 4] = 0;
        name = n;
    }
    if (!valid_pkg_name(name)) { fprintf(stderr, "xisto: invalid package name '%s'%s (give one with --name)\n", name, by); return NULL; }
    if (want && strcmp(want, name) != 0 && needed_by)
        fprintf(stderr, "xisto: warning: '%s' at %s is named '%s' in its xisto.json\n", want, subdir ? subdir : shown(source), name);
    if (!extract(repo, commit, subdir, pkg_dir(name))) return NULL;

    Dep *d = dep_find(name);
    bool was_direct = d && !d->indirect;
    if (!d) {
        if (ndeps == MAXDEPS) { fprintf(stderr, "xisto: too many packages\n"); return NULL; }
        d = &deps[ndeps++];
        memset(d, 0, sizeof *d);
        d->name = sdup(name);
    }
    d->source = sdup(source);
    d->subdir = subdir ? sdup(subdir) : NULL;
    d->ref = ref ? sdup(ref) : NULL;
    d->commit = sdup(commit);
    d->indirect = needed_by && !was_direct;
    d->touched = true;
    if (needed_by) printf("xisto: added %s at %.12s (needed by %s)\n", name, commit, needed_by);
    else printf("xisto: added %s at %.12s (%s%s%s%s%s)\n", name, commit, shown(source), subdir ? "#" : "", subdir ? subdir : "", ref ? "@" : "", ref ? ref : "");

    /* its dependencies: from the same repository at the same commit, unless given as a git URL */
    JsIt it;
    char *dn;
    const char *dv;
    if (nstack == MAXSTACK) { fprintf(stderr, "xisto: dependencies nested too deep at %s\n", name); errors++; return d; }
    stack[nstack++] = d->name;
    const char *dsource = d->source, *dsubdir = d->subdir, *dcommit = d->commit, *dname = d->name;
    if (meta && js_obj(&it, js_get(meta, "dependencies"))) {
        while (js_next(&it, &dn, &dv)) {
            char *val = NULL;
            if (*dv == '"') js_str(dv, &val);
            bool cycle = false;
            for (int i = 0; i < nstack; i++) if (strcmp(stack[i], dn) == 0) cycle = true;
            if (cycle) {
                Buf c = {0};
                for (int i = 0; i < nstack; i++) buf_printf(&c, "%s -> ", stack[i]);
                printf("xisto: note: dependency cycle %s%s\n", c.data, dn);
                continue;
            }
            if (!valid_pkg_name(dn)) { fprintf(stderr, "xisto: invalid dependency name '%s' in %s\n", dn, dname); errors++; continue; }
            Dep *have = dep_find(dn);
            if (have && have->touched) continue;
            if (have && have->indirect && !needed_by_others(dn)) have = NULL; /* only its re-added dependents need it: it moves with them */
            if (have) {
                if (strcmp(have->source, dsource) != 0 || strcmp(have->commit, dcommit) != 0)
                    fprintf(stderr, "xisto: warning: %s depends on %s: keeping %s at %.12s (%s), not %.12s; `xisto add %s` moves it\n",
                            dname, dn, dn, have->commit, shown(have->source), dcommit, dn);
                if (!xs_is_dir(pkg_dir(dn)) && !restore(have)) errors++;
                have->touched = true;
                continue;
            }
            Dep *got;
            if (val && (strchr(val, ':') || val[0] == '/')) { /* a git URL (or an absolute path) */
                Spec s;
                if (!parse_spec(val, &s)) { errors++; continue; }
                got = install(s.source, s.subdir, s.ref, NULL, s.subdir ? NULL : dn, dn, dname, true);
            } else if (dsubdir) { /* a monorepo: the dependency is the folder named as it, next to its dependent, at the same commit */
                size_t ls = strlen(dsubdir), ln = strlen(dname);
                bool nested = ls > ln && strcmp(dsubdir + ls - ln, dname) == 0 && dsubdir[ls - ln - 1] == '/'; /* <root>/<dependent's name> */
                got = install(dsource, nested ? xs_sprintf("%.*s%s", (int)(ls - ln), dsubdir, dn) : dn, NULL, dcommit, NULL, dn, dname, false);
            }
            else /* a package of its own repository: the dependency from the registry */
                got = install(registry(), dn, NULL, NULL, NULL, dn, dname, true);
            if (!got) errors++;
        }
    }
    nstack--;
    return d;
}

/* indirect packages no package needs any more (their dependent moved to another commit): out of the lock and of
   .xisto/pkgs. Needed: what the user asked for, and the dependencies of what is needed (installed xisto.json). */
static void prune(void) {
    bool *need = calloc((size_t)ndeps + 1, 1);
    int todo[MAXDEPS], ntodo = 0;
    for (int i = 0; i < ndeps; i++) if (!deps[i].indirect) { need[i] = true; todo[ntodo++] = i; }
    while (ntodo) {
        char *meta = xs_read_file(xs_sprintf("%s/xisto.json", pkg_dir(deps[todo[--ntodo]].name)));
        JsIt it;
        char *dn;
        const char *dv;
        if (!meta || !js_obj(&it, js_get(meta, "dependencies"))) continue;
        while (js_next(&it, &dn, &dv)) {
            Dep *d = dep_find(dn);
            if (d && !need[d - deps]) { need[d - deps] = true; todo[ntodo++] = (int)(d - deps); }
        }
    }
    int k = 0;
    for (int i = 0; i < ndeps; i++) {
        if (need[i]) { deps[k++] = deps[i]; continue; }
        xs_rmtree(pkg_dir(deps[i].name));
        printf("xisto: removed %s (no package needs it now)\n", deps[i].name);
    }
    ndeps = k;
    free(need);
}

/* what xisto.json records for a package the user asked for */
static const char *spec_text(const char *arg, const Spec *s) {
    if (!s->registry) return arg;
    if (!s->ref) return "*";
    if (strchr(s->ref, '/')) return xs_sprintf("%s#%s@%s", s->source, s->subdir, s->ref);
    return s->ref;
}

/* a dependency as xisto.json writes it: "*", a ref of the registry, or SOURCE[#SUBDIR][@REF] */
static bool spec_of_json(const char *name, const char *val, Spec *s) {
    if (strcmp(val, "*") == 0 || (!is_location(val) && !strchr(val, '#'))) {
        memset(s, 0, sizeof *s);
        s->registry = true;
        s->source = sdup(registry());
        s->subdir = sdup(name);
        s->ref = strcmp(val, "*") == 0 ? NULL : sdup(val);
        if (!valid_pkg_name(name)) { fprintf(stderr, "xisto: invalid package name '%s' in xisto.json\n", name); return false; }
        return true;
    }
    return parse_spec(val, s);
}

int xs_cmd_add(int argc, char **argv) {
    const char *name = NULL;
    const char *args[256];
    int nargs = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) name = argv[++i];
        else if (argv[i][0] == '-') { fprintf(stderr, "xisto: unknown option '%s'\n", argv[i]); nargs = 0; break; }
        else if (nargs < 256) args[nargs++] = argv[i];
    }
    if (nargs == 0) {
        fprintf(stderr, "usage: xisto add NAME[@REF]...                    a package of the registry (%s)\n"
                        "       xisto add SOURCE#FOLDER[@REF]...           the package in FOLDER of a git repository (URL or path)\n"
                        "       xisto add SOURCE[@REF] [--name NAME]       a whole git repository as one package\n", registry());
        return 2;
    }
    if (name && nargs > 1) { fprintf(stderr, "xisto: --name names one package: add them one at a time\n"); return 2; }
    if (name && !valid_pkg_name(name)) { fprintf(stderr, "xisto: invalid package name '%s'\n", name); return 2; }
    Spec specs[256];
    for (int i = 0; i < nargs; i++) if (!parse_spec(args[i], &specs[i])) return 2;
    xs_setenv("GIT_TERMINAL_PROMPT", "0"); /* a missing repository is an error, not a password prompt */
    read_project();
    for (int i = 0; i < nargs; i++) {
        Spec *s = &specs[i];
        Dep *d = install(s->source, s->subdir, s->ref, NULL, name, s->registry ? s->subdir : NULL, NULL, true);
        if (!d) { errors++; continue; }
        set_direct(d->name, spec_text(args[i], s));
    }
    if (ndeps) {
        if (!errors) prune();
        write_project();
    }
    return errors ? 1 : 0;
}

int xs_cmd_install(void) {
    xs_setenv("GIT_TERMINAL_PROMPT", "0");
    read_project();
    if (ndeps == 0 && ndirect == 0) { printf("xisto: nothing to install (no xisto.lock)\n"); return 0; }
    for (int i = 0; i < ndeps; i++) {
        if (!restore(&deps[i])) { errors++; continue; }
        printf("xisto: %s at %.12s\n", deps[i].name, deps[i].commit);
    }
    /* a dependency written into xisto.json by hand (or a project without a lock): resolved now, and locked */
    int before = ndeps;
    bool changed = false;
    for (int i = 0; i < ndirect; i++) {
        Dep *have = dep_find(direct[i].name);
        if (have && !have->indirect) continue;
        Spec s;
        if (!spec_of_json(direct[i].name, direct[i].spec, &s)) { errors++; continue; }
        if (have) { have->indirect = false; changed = true; continue; }
        Dep *d = install(s.source, s.subdir, s.ref, NULL, s.subdir ? NULL : direct[i].name, direct[i].name, NULL, true);
        if (!d) errors++;
        else changed = true;
    }
    if (changed || ndeps != before) write_project();
    return errors ? 1 : 0;
}

/* ---- xisto init ---- */

int xs_cmd_init(int argc, char **argv) {
    const char *dir = argc >= 3 ? argv[2] : ".";
    if (argc > 3 || dir[0] == '-') { fprintf(stderr, "usage: xisto init [DIR]\n"); return 2; }
    if (!xs_mkdirs(dir)) { fprintf(stderr, "xisto: cannot create %s\n", dir); return 1; }
    char real[PATH_MAX];
    if (!xs_real_path(dir, real)) snprintf(real, sizeof real, "%s", dir);
    const char *name = strrchr(real, '/') ? strrchr(real, '/') + 1 : real;
    if (!*name) name = "app";
    /* the executable `xisto build` makes: the project's name (see xs_project_main) */
    bool plain = valid_pkg_name(name) && name[0] != '@';
    Buf meta = {0};
    buf_puts(&meta, "{\n  \"name\": ");
    js_put(&meta, name);
    buf_puts(&meta, ",\n  \"main\": \"src/main.ts\",\n  \"dependencies\": {}\n}\n");
    Buf ign = {0};
    buf_puts(&ign, "# the build cache and the installed packages (`xisto install` restores them from xisto.lock)\n.xisto/\n");
    buf_printf(&ign, "# the executable `xisto build` makes\n/%s\n/%s.exe\n", plain ? name : "main", plain ? name : "main");
    struct { const char *path, *text; } files[] = {
        {"xisto.json", meta.data},
        {"src/main.ts", "// the program's entry (xisto.json \"main\"): `xisto run` runs it, `xisto build` makes a native executable\n"
                        "console.log(\"Hello, world!\");\n"},
        {".gitignore", ign.data},
    };
    int made = 0;
    const char *pre = strcmp(dir, ".") == 0 ? "" : xs_sprintf("%s/", dir);
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        char *path = xs_sprintf("%s%s", pre, files[i].path);
        if (xs_exists(path)) { printf("xisto: %s already exists: left as it is\n", path); continue; }
        if (strchr(files[i].path, '/') && !xs_mkdirs(xs_sprintf("%ssrc", pre))) { fprintf(stderr, "xisto: cannot create %ssrc\n", pre); return 1; }
        if (!write_text(path, files[i].text)) return 1;
        printf("xisto: created %s\n", path);
        made++;
    }
    printf("\nnext steps:\n");
    if (*pre) printf("  cd %s\n", dir);
    printf("  xisto run              compile and run src/main.ts\n"
           "  xisto add <package>    add a package, e.g. xisto add @nestjs/common @nestjs/core\n"
           "  xisto build            a native executable: ./%s\n", plain ? name : "main");
    (void)made;
    return 0;
}

/* `xisto build` / `xisto run` without a file: the "main" of the nearest xisto.json (the cwd or an ancestor).
   *exe gets the executable's path (<project>/<name>), or NULL. NULL when there is no project; exits when the
   project has no "main". */
char *xs_project_main(char **exe) {
    char cwd[PATH_MAX];
    *exe = NULL;
    if (!getcwd(cwd, sizeof cwd)) return NULL;
    for (char *dir = sdup(cwd);;) {
        char *path = xs_sprintf("%s/xisto.json", dir);
        char *meta = xs_read_file(path);
        if (meta) {
            bool here = strcmp(dir, cwd) == 0;
            char *main = js_get_str(meta, "main");
            if (!main) { fprintf(stderr, "xisto: %s has no \"main\": give the file to compile (xisto build FILE.ts)\n", here ? "xisto.json" : path); exit(2); }
            char *file = here ? main : xs_sprintf("%s/%s", dir, main);
            char *name = js_get_str(meta, "name");
            if (name && name[0] == '@' && strchr(name, '/')) name = strchr(name, '/') + 1;
            if (name && valid_pkg_name(name)) *exe = here ? name : xs_sprintf("%s/%s", dir, name);
            return file;
        }
        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir) return NULL;
        *slash = 0;
    }
}
