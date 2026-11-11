/* Parallel codegen units for --dev builds: the program's C (one translation unit, everything `static`) is cut
   into units that the C compiler builds at the same time, like Rust's codegen-units.

   The runtime part of the text (before the program marker) is the shared prelude, as are the program's types,
   function prototypes and its globals as `extern`; each unit then defines a share of the functions, and unit 0
   also defines the globals. Program definitions lose `static` (and the always-inline macros), so a call from
   one unit to a function another unit defines is a plain call. Only the program's own top-level items are
   rewritten: function bodies are copied as they are. */
#include "xisto.h"

#include <ctype.h>
#include <string.h>

enum { IT_KEEP, IT_FUNC, IT_VAR };

typedef struct {
    size_t at, end;  /* the item's text */
    size_t body;     /* IT_FUNC: where the body's `{` is */
    size_t eq;       /* IT_VAR: the top-level `=`, or 0 */
    int kind;
} Item;

static size_t skip_literal(const char *s, size_t i, size_t n) {
    char q = s[i];
    for (i++; i < n && s[i] != q; i++)
        if (s[i] == '\\') i++;
    return i < n ? i + 1 : n;
}

static size_t skip_comment(const char *s, size_t i, size_t n) {
    if (s[i + 1] == '/') {
        while (i < n && s[i] != '\n') i++;
        return i;
    }
    const char *e = strstr(s + i + 2, "*/");
    return e ? (size_t)(e - s) + 2 : n;
}

static bool word_at(const char *s, size_t i, size_t end, const char *w) {
    size_t k = strlen(w);
    if (i + k > end || strncmp(s + i, w, k) != 0) return false;
    if (i > 0 && (isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_')) return false;
    return i + k == end || !(isalnum((unsigned char)s[i + k]) || s[i + k] == '_');
}

/* the item's head without `static` and `inline`: the program's definitions get external linkage
   (`used` only means something on a definition) */
static void put_head(Buf *b, const char *s, size_t at, size_t end, bool keep_used) {
    for (size_t i = at; i < end;) {
        if (word_at(s, i, end, "static")) { i += 6; while (i < end && s[i] == ' ') i++; continue; }
        if (word_at(s, i, end, "inline")) { i += 6; while (i < end && s[i] == ' ') i++; continue; }
        /* the runtime's storage macros, which say `static` too */
        if (word_at(s, i, end, "XS_NOINLINE") || word_at(s, i, end, "XS_FN")) { buf_puts(b, "__attribute__((noinline))"); i += s[i + 3] == 'N' ? 11 : 5; continue; }
        if (word_at(s, i, end, "XS_FI") || word_at(s, i, end, "XS_FA") || word_at(s, i, end, "XS_AI")) { i += 5; while (i < end && s[i] == ' ') i++; continue; }
        if (word_at(s, i, end, "XS_FII")) { i += 6; while (i < end && s[i] == ' ') i++; continue; } /* (small constructors) */
        if (strncmp(s + i, "__attribute__((used))", 21) == 0 && !keep_used) { i += 21; while (i < end && s[i] == ' ') i++; continue; }
        buf_putc(b, s[i++]);
    }
}

static void put_text(Buf *b, const char *s, size_t at, size_t end) {
    for (size_t i = at; i < end; i++) buf_putc(b, s[i]);
}

/* the parts of a declaration's head before its declarator's first `(` that is not `(*` (a function pointer) */
static bool is_prototype(const char *s, size_t at, size_t end) {
    for (size_t i = at; i < end; i++) {
        if (word_at(s, i, end, "__attribute__")) { /* skip __attribute__((...)) */
            int d = 0;
            for (i += 13; i < end; i++) {
                if (s[i] == '(') d++;
                else if (s[i] == ')' && --d == 0) break;
            }
            continue;
        }
        if (s[i] == '(') {
            size_t k = i + 1;
            while (k < end && s[k] == ' ') k++;
            return s[k] != '*';
        }
    }
    return false;
}

/* the next top-level item at or after *pi, up to `n`; false at the end. Directives are items of their own. */
static bool next_item(const char *s, size_t n, size_t *pi, Item *out) {
    size_t i = *pi;
    for (;;) {
        if (i >= n) return false;
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n') { i++; continue; }
        if (c == '/' && (s[i + 1] == '*' || s[i + 1] == '/')) { i = skip_comment(s, i, n); continue; }
        break;
    }
    Item it = {.at = i, .kind = IT_KEEP};
    if (s[i] == '#') { /* a directive, with its continued lines */
        while (i < n && !(s[i] == '\n' && s[i - 1] != '\\')) i++;
        it.end = i;
        it.kind = -1;
        *out = it;
        *pi = i;
        return true;
    }
    int par = 0, br = 0;
    size_t last = 0; /* the last non-space character outside comments */
    bool eq = false;
    for (; i < n; i++) {
        char c = s[i];
        if (c == '"' || c == '\'') { i = skip_literal(s, i, n) - 1; last = i; continue; }
        if (c == '/' && (s[i + 1] == '*' || s[i + 1] == '/')) { i = skip_comment(s, i, n) - 1; continue; }
        if (c == '(') par++;
        else if (c == ')') par--;
        else if (c == '=' && par == 0 && br == 0 && !eq && s[i + 1] != '=') { eq = true; it.eq = i; }
        else if (c == '{' && par == 0) {
            if (br == 0 && !eq && last && s[last] == ')') { /* a function body */
                it.kind = IT_FUNC;
                it.body = i;
                int d = 0;
                for (; i < n; i++) {
                    c = s[i];
                    if (c == '"' || c == '\'') { i = skip_literal(s, i, n) - 1; continue; }
                    if (c == '/' && (s[i + 1] == '*' || s[i + 1] == '/')) { i = skip_comment(s, i, n) - 1; continue; }
                    if (c == '{') d++;
                    else if (c == '}' && --d == 0) break;
                }
                i++;
                break;
            }
            br++;
        } else if (c == '}' && par == 0) br--;
        else if (c == ';' && par == 0 && br == 0) { i++; break; }
        if (c != ' ' && c != '\t' && c != '\n') last = i;
    }
    it.end = i;
    if (it.kind != IT_FUNC) {
        bool type = word_at(s, it.at, it.end, "typedef") || word_at(s, it.at, it.end, "struct") ||
                    word_at(s, it.at, it.end, "union") || word_at(s, it.at, it.end, "enum") ||
                    word_at(s, it.at, it.end, "extern");
        if (!type && !is_prototype(s, it.at, it.eq ? it.eq : it.end)) it.kind = IT_VAR;
    }
    *out = it;
    *pi = i;
    return true;
}

static bool has_word(const char *s, size_t at, size_t end, const char *w) {
    for (size_t i = at; i < end; i++)
        if (word_at(s, i, end, w)) return true;
    return false;
}

/* XISTO_UNITS_WHY=1: what keeps the program in one unit */
static bool why(const char *s, Item it, const char *what) {
    if (getenv("XISTO_UNITS_WHY")) fprintf(stderr, "xisto: one unit: %s in package C: %.80s\n", what, s + it.at);
    return false;
}

/* a function's name: the identifier before its declarator's `(` */
static bool same_name(const char *s, size_t at, size_t end, const char *name, size_t len) {
    for (size_t i = at; i < end; i++) {
        if (word_at(s, i, end, "__attribute__")) { int d = 0; for (i += 13; i < end; i++) { if (s[i] == '(') d++; else if (s[i] == ')' && --d == 0) break; } continue; }
        if (s[i] == '(') {
            size_t e = i;
            while (e > at && s[e - 1] == ' ') e--;
            size_t b = e;
            while (b > at && (isalnum((unsigned char)s[b - 1]) || s[b - 1] == '_')) b--;
            return e - b == len && strncmp(s + b, name, len) == 0;
        }
    }
    return false;
}
static size_t name_of(const char *s, size_t at, size_t end, size_t *len) {
    for (size_t i = at; i < end; i++) {
        if (word_at(s, i, end, "__attribute__")) { int d = 0; for (i += 13; i < end; i++) { if (s[i] == '(') d++; else if (s[i] == ')' && --d == 0) break; } continue; }
        if (s[i] == '(') {
            size_t e = i;
            while (e > at && s[e - 1] == ' ') e--;
            size_t b = e;
            while (b > at && (isalnum((unsigned char)s[b - 1]) || s[b - 1] == '_')) b--;
            *len = e - b;
            return b;
        }
    }
    *len = 0;
    return at;
}

/* read-only data, the same in every unit: `static const T x`, `static const char *const p` */
static bool is_readonly(const char *s, size_t at, size_t e) {
    if (!has_word(s, at, e, "static") || !has_word(s, at, e, "const")) return false;
    const char *star = memchr(s + at, '*', e - at);
    if (!star) return true;
    for (const char *q = star; q < s + e; q++)
        if (*q == '*') {
            const char *r = q + 1;
            while (r < s + e && *r == ' ') r++;
            if ((size_t)(s + e - r) >= 5 && strncmp(r, "const", 5) == 0) return true;
        }
    return false;
}

/* Package C is part of the prelude, so every unit has a copy: its functions must have internal linkage, or
   the program stays in one unit. Its globals, and its functions with `static` locals (state), are declared
   there and defined once, in unit 0 (`defs`), like the program's. */
static bool native_rewrite(const char *s, size_t at, size_t end, Buf *pre, Buf *defs) {
    Item it;
    static size_t moved[8192], moved_len[8192]; /* the functions defined in the package unit: their declarations lose `static` */
    int nmoved = 0;
    for (size_t i = at; next_item(s, end, &i, &it);)
        if (it.kind == IT_FUNC) { /* every package function is defined once, in the package unit */
            if (nmoved == 8192) return why(s, it, "too many package functions");
            moved[nmoved] = name_of(s, it.at, it.body, &moved_len[nmoved]);
            nmoved++;
        }
    size_t done = at;
    for (size_t i = at; next_item(s, end, &i, &it);) {
        if (it.kind == IT_KEEP && nmoved && is_prototype(s, it.at, it.end)) {
            for (int m = 0; m < nmoved; m++)
                if (same_name(s, it.at, it.end, s + moved[m], moved_len[m])) {
                    put_text(pre, s, done, it.at);
                    put_head(pre, s, it.at, it.end, false);
                    done = it.end;
                    break;
                }
            continue;
        }
        if (it.kind == -1 || it.kind == IT_KEEP) continue;
        if (it.kind == IT_VAR) {
            size_t e = it.eq ? it.eq : it.end - 1;
            if (is_readonly(s, it.at, e)) continue;
            put_text(pre, s, done, it.at);
            buf_puts(pre, "extern ");
            put_head(pre, s, it.at, e, false);
            buf_puts(pre, ";");
            done = it.end;
            buf_putc(defs, '\n');
            put_head(defs, s, it.at, e, true);
            put_text(defs, s, e, it.end);
            buf_putc(defs, '\n');
            continue;
        }
        bool internal = false; /* `static`, or the runtime's macros for it */
        static const char *const words[] = {"static", "XS_NOINLINE", "XS_FN", "XS_FI", "XS_FA", "XS_AI", "XS_FII"};
        for (int w = 0; w < 7 && !internal; w++) internal = has_word(s, it.at, it.body, words[w]);
        if (!internal) return why(s, it, "an external function");
        { /* defined once, in the package unit; the others see a declaration */
            put_text(pre, s, done, it.at);
            put_head(pre, s, it.at, it.body, false);
            buf_puts(pre, ";");
            done = it.end;
            buf_putc(defs, '\n');
            put_head(defs, s, it.at, it.body, true);
            put_text(defs, s, it.body, it.end);
            buf_putc(defs, '\n');
        }
    }
    put_text(pre, s, done, end);
    return true;
}

/* the identifiers of a piece of C: an open-addressing set of (pointer, length) into the text */
typedef struct { const char **v; size_t *l; size_t cap, n; } IdSet;
static uint64_t id_hash(const char *p, size_t len) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < len; i++) { h ^= (unsigned char)p[i]; h *= 1099511628211ull; }
    return h;
}
static bool ids_has(IdSet *s, const char *p, size_t len) {
    if (!s->cap) return false;
    for (size_t j = id_hash(p, len) & (s->cap - 1); s->v[j]; j = (j + 1) & (s->cap - 1))
        if (s->l[j] == len && memcmp(s->v[j], p, len) == 0) return true;
    return false;
}
static void ids_add(IdSet *s, const char *p, size_t len) {
    if ((s->n + 1) * 2 > s->cap) {
        IdSet g = {calloc(s->cap ? s->cap * 2 : 4096, sizeof(char *)), calloc(s->cap ? s->cap * 2 : 4096, sizeof(size_t)), s->cap ? s->cap * 2 : 4096, 0};
        for (size_t i = 0; i < s->cap; i++) if (s->v[i]) ids_add(&g, s->v[i], s->l[i]);
        free(s->v); free(s->l);
        *s = g;
    }
    size_t j = id_hash(p, len) & (s->cap - 1);
    for (; s->v[j]; j = (j + 1) & (s->cap - 1)) if (s->l[j] == len && memcmp(s->v[j], p, len) == 0) return;
    s->v[j] = p; s->l[j] = len; s->n++;
}
static void ids_collect(IdSet *s, const char *t) {
    for (const char *p = t; *p;) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *q = p;
            while (isalnum((unsigned char)*q) || *q == '_') q++;
            ids_add(s, p, (size_t)(q - p));
            p = q;
        } else if (*p == '"') { /* names inside strings (asm labels, XS_POST's table) are kept by the extern rule */
            for (p++; *p && *p != '"'; p++) if (*p == '\\' && p[1]) p++;
            if (*p) p++;
        } else p++;
    }
}
/* a variable's name: the last identifier of its declarator, before any [size] */
static size_t var_name(const char *s, size_t at, size_t end, size_t *len) {
    size_t e = end;
    while (e > at && (s[e - 1] == ' ' || s[e - 1] == '\n' || s[e - 1] == ';')) e--;
    while (e > at && s[e - 1] == ']') { while (e > at && s[e - 1] != '[') e--; if (e > at) e--; while (e > at && s[e - 1] == ' ') e--; }
    size_t b = e;
    while (b > at && (isalnum((unsigned char)s[b - 1]) || s[b - 1] == '_')) b--;
    *len = e - b;
    return b;
}

int cc_split_units(const char *s, size_t n, int want, Buf *units) {
    bool force = want < 0; /* an explicit count (XISTO_UNITS): also for a small program */
    if (force) want = -want;
    const char *mark = strstr(s, "\n/* ---- program ---- */\n");
    if (!mark || want < 2) return 0;
    size_t start = (size_t)(mark - s) + 1;
    Item *items = NULL;
    int nitems = 0, cap = 0;
    size_t func_bytes = 0;
    const char *native = strstr(s, "\n/* ---- native package code ---- */\n");
    if (!native || native > mark) native = mark;
    Buf pre = {0}, ndefs = {0};
    put_text(&pre, s, 0, (size_t)(native - s));
    if (!native_rewrite(s, (size_t)(native - s), start, &pre, &ndefs)) { free(pre.data); free(ndefs.data); return 0; }
    Item it;
    for (size_t i = start; next_item(s, n, &i, &it);) {
        if (it.kind == -1) { free(items); free(pre.data); free(ndefs.data); return 0; } /* a directive in the program: keep the one unit */
        if (it.kind == IT_FUNC) func_bytes += it.end - it.at;
        if (nitems == cap) items = realloc(items, sizeof(Item) * (size_t)(cap = cap ? cap * 2 : 1024));
        items[nitems++] = it;
    }
    if (!force && func_bytes / (size_t)want < 32 * 1024) want = (int)(func_bytes / (32 * 1024)); /* units worth a process */
    if (want < 2) { free(items); free(pre.data); free(ndefs.data); return 0; }

    buf_puts(&pre, "/* ---- codegen unit: the program's functions are external, defined in one unit each ---- */\n"
                   "#undef XS_FN\n#define XS_FN __attribute__((noinline))\n#undef XS_FI\n#define XS_FI\n#undef XS_FA\n#define XS_FA\n"
                   "#undef XS_AI\n#define XS_AI\n#undef XS_FII\n#define XS_FII\n#undef XS_NOINLINE\n#define XS_NOINLINE __attribute__((noinline))\n");
    /* Stable units, so that an edit recompiles little: a function goes to the unit its name hashes to; the data
       (globals, type descriptors, the literals and their initializer, the call-site table with its line numbers)
       to a unit of its own; and each unit declares only the program names its code uses. */
    if (want > 6) want = 6;
    if (want < 2) want = 2;
    int nu = want + 2, data = want, mainu = want + 1; /* main (the top level: the biggest function) alone */
    int natu = ndefs.data ? nu++ : -1;  /* the packages' C: changes only with them */
    Buf body[12];
    for (int u = 0; u < nu; u++) body[u] = (Buf){0};
    if (ndefs.data) buf_puts(&body[natu], ndefs.data);
    /* the literals live where they are used: a weak definition in each unit that names one, and that unit's
       constructor sets it up (before main). A new or changed literal then changes only the units using it, not
       the data unit every edit used to recompile. */
    const char *lit_init = NULL; size_t lit_init_len = 0;
    for (int k = 0; k < nitems; k++) {
        Item *it = &items[k];
        size_t len, at;
        if (it->kind == IT_FUNC) {
            at = name_of(s, it->at, it->body, &len);
            if (len == 12 && strncmp(s + at, "xs_init_lits", 12) == 0) { lit_init = s + it->body; lit_init_len = it->end - it->body; }
        }
    }
    for (int k = 0; k < nitems; k++) {
        Item *it = &items[k];
        if (it->kind == IT_KEEP) continue;
        if (lit_init && it->kind == IT_VAR) {
            size_t len, at = var_name(s, it->at, it->eq ? it->eq : it->end, &len);
            if (len == 20 && strncmp(s + at, "lit_", 4) == 0) continue; /* placed with its users below */
        }
        if (lit_init && it->kind == IT_FUNC) {
            size_t len, at = name_of(s, it->at, it->body, &len);
            if (len == 12 && strncmp(s + at, "xs_init_lits", 12) == 0) { /* now empty: each unit sets its own */
                buf_puts(&body[data], "\n");
                put_head(&body[data], s, it->at, it->body, true);
                buf_puts(&body[data], "{ }\n");
                continue;
            }
        }
        int u = data;
        if (it->kind == IT_FUNC) {
            size_t len, at = name_of(s, it->at, it->body, &len);
            if (len == 4 && strncmp(s + at, "main", 4) == 0) u = mainu;
            else if (!(len == 12 && strncmp(s + at, "xs_init_lits", 12) == 0)) {
                uint64_t h = 1469598103934665603ull;
                for (size_t i = 0; i < len; i++) { h ^= (unsigned char)s[at + i]; h *= 1099511628211ull; }
                u = (int)(h % (uint64_t)want);
            }
        }
        buf_putc(&body[u], '\n');
        size_t head_end = it->kind == IT_FUNC ? it->body : it->eq ? it->eq : it->end;
        put_head(&body[u], s, it->at, head_end, true);
        put_text(&body[u], s, head_end, it->end);
        buf_putc(&body[u], '\n');
    }
    for (int u = 0; u < nu; u++) {
        IdSet used = {0};
        ids_collect(&used, body[u].data ? body[u].data : "");
        units[u] = (Buf){0};
        buf_puts(&units[u], pre.data);
        for (int k = 0; k < nitems; k++) {
            Item *it = &items[k];
            bool type = word_at(s, it->at, it->end, "typedef") || word_at(s, it->at, it->end, "struct") || word_at(s, it->at, it->end, "union") ||
                        word_at(s, it->at, it->end, "enum");
            bool ext = word_at(s, it->at, it->end, "extern");
            if (it->kind == IT_KEEP && (type || ext)) { put_text(&units[u], s, it->at, it->end); buf_putc(&units[u], '\n'); continue; }
            size_t len, at;
            if (it->kind == IT_VAR) at = var_name(s, it->at, it->eq ? it->eq : it->end, &len);
            else at = name_of(s, it->at, it->kind == IT_FUNC ? it->body : it->end, &len);
            if (len && !ids_has(&used, s + at, len)) continue; /* this unit does not use it */
            if (lit_init && it->kind == IT_VAR && len == 20 && strncmp(s + at, "lit_", 4) == 0) continue; /* weak below */
            if (it->kind == IT_KEEP) put_head(&units[u], s, it->at, it->end, false);
            else if (it->kind == IT_FUNC) { put_head(&units[u], s, it->at, it->body, false); buf_puts(&units[u], ";"); }
            else {
                buf_puts(&units[u], "extern ");
                put_head(&units[u], s, it->at, it->eq ? it->eq : it->end - 1, false);
                buf_puts(&units[u], ";");
            }
            buf_putc(&units[u], '\n');
        }
        if (lit_init) { /* this unit's literals: weak definitions and their setup */
            Buf init = {0};
            for (const char *q = lit_init; q < lit_init + lit_init_len; q++) {
                if (strncmp(q, "lit_", 4) != 0 || (q > lit_init && (isalnum((unsigned char)q[-1]) || q[-1] == '_'))) continue;
                const char *e = strstr(q, ";\n");
                if (!e || (size_t)(e - lit_init) > lit_init_len) break;
                if (ids_has(&used, q, 20)) {
                    buf_printf(&units[u], "__attribute__((weak)) xs_str *%.20s = 0;\n", q);
                    buf_printf(&init, "\n    if (!%.20s) %.*s;", q, (int)(e - q), q);
                }
                q = e;
            }
            if (init.data) buf_printf(&units[u], "__attribute__((constructor)) static void xs_lits_here(void) {%s\n}\n", init.data);
            free(init.data);
        }
        if (body[u].data) buf_puts(&units[u], body[u].data);
        free(body[u].data);
        free(used.v);
    }
    want = nu;
    free(pre.data);
    free(ndefs.data);
    free(items);
    return want;
}
