#include "xisto.h"
#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>

const char *xs_file_name = "<input>";
const char *xs_source = "";

void *xs_alloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) {
        fprintf(stderr, "xisto: out of memory\n");
        exit(2);
    }
    return p;
}

char *xs_strndup(const char *s, size_t n) {
    char *d = xs_alloc(n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

char *xs_sprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = xs_alloc((size_t)n + 2); /* (two zero bytes: the lexer peeks one past the last character) */
    vsnprintf(s, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return s;
}

static void buf_reserve(Buf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 4096;
    while (cap < b->len + extra + 1) cap *= 2;
    b->data = realloc(b->data, cap);
    if (!b->data) {
        fprintf(stderr, "xisto: out of memory\n");
        exit(2);
    }
    b->cap = cap;
}

void buf_putc(Buf *b, char c) {
    buf_reserve(b, 1);
    b->data[b->len++] = c;
    b->data[b->len] = 0;
}

void buf_puts(Buf *b, const char *s) {
    size_t n = strlen(s);
    buf_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

void buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    buf_reserve(b, (size_t)n);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
}

static void print_source_line(Pos pos) {
    const char *s = xs_source;
    int line = 1;
    while (*s && line < pos.line) {
        if (*s == '\n') line++;
        s++;
    }
    const char *end = s;
    while (*end && *end != '\n') end++;
    fprintf(stderr, "  %4d | %.*s\n", pos.line, (int)(end - s), s);
    fprintf(stderr, "       | %*s^\n", pos.col > 0 ? pos.col - 1 : 0, "");
}

bool xs_json_errors;

static void json_str(FILE *f, const char *s) {
    fputc('"', f);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
        else if (c == '\n') fputs("\\n", f);
        else if (c == '\t') fputs("\\t", f);
        else if (c < 32) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static const char *source_line(Pos pos) {
    const char *s = xs_source;
    int line = 1;
    while (*s && line < pos.line) {
        if (*s == '\n') line++;
        s++;
    }
    const char *end = s;
    while (*end && *end != '\n') end++;
    return xs_strndup(s, (size_t)(end - s));
}

jmp_buf *xs_error_recover; /* set by the checker: an error is reported and checking continues elsewhere */
int xs_error_count;

static const struct { const char *name; DiagCat cat; } diag_table[] = {
    {"", DC_INTERNAL},
#define DIAG(code, cat, msg) {#code, cat},
#include "diag.def"
#undef DIAG
};
const char *xs_diag_name(DiagCode c) { return c > 0 && c < XS_DIAG_COUNT ? diag_table[c].name : ""; }
DiagCat xs_diag_cat(DiagCode c) { return c > 0 && c < XS_DIAG_COUNT ? diag_table[c].cat : DC_INTERNAL; }
static const struct { const char *id, *title, *fix; } cat_table[DC_COUNT] = {
    [DC_SYNTAX] = {"syntax", "syntax xisto does not read", "manual"},
    [DC_DYNAMIC] = {"dynamic", "dynamic value (any, undefined, maybe null)", "manual"},
    [DC_MISSING_TYPE] = {"missing-type", "missing type annotation", "manual"},
    [DC_NODE_IMPORT] = {"node-import", "Node module import (node:x -> xisto:x)", "automatic"},
    [DC_MODULE] = {"module", "module or package not available", "package"},
    [DC_UNSUPPORTED] = {"unsupported", "TypeScript feature not supported", "manual"},
    [DC_RUNTIME_API] = {"runtime-api", "built-in API missing or used differently", "manual"},
    [DC_TYPE_RULE] = {"type-rule", "type error under xisto's rules", "manual"},
    [DC_DECLARATION] = {"declaration", "unknown, duplicate or unexported name", "manual"},
    [DC_INTERNAL] = {"internal", "internal compiler error", "report"},
    [DC_SEMANTIC] = {"semantic", "may behave differently from Node", "review"},
    [DC_DYNPATH] = {"dynamic-path", "dynamic path (any): runs, slower", "optional"},
};
const char *xs_diag_cat_title(DiagCat c) { return c < DC_COUNT ? cat_table[c].title : ""; }
const char *xs_diag_cat_fix(DiagCat c) { return c < DC_COUNT ? cat_table[c].fix : ""; }
const char *xs_diag_cat_id(DiagCat c) { return c < DC_COUNT ? cat_table[c].id : ""; }
bool xs_check_mode;
bool xs_lex_skipping;
int xs_quiet_count;

static void report(DiagCode code, const char *severity, Pos pos, const char *help, const char *msg) {
    if (xs_json_errors) {
        fprintf(stderr, "{\"severity\":\"%s\",", severity);
        if (code) { fputs("\"code\":", stderr); json_str(stderr, xs_diag_name(code)); fputs(",\"category\":", stderr); json_str(stderr, xs_diag_cat_id(xs_diag_cat(code))); fputc(',', stderr); }
        fputs("\"file\":", stderr); json_str(stderr, xs_file_name);
        fprintf(stderr, ",\"line\":%d,\"col\":%d,\"message\":", pos.line, pos.col); json_str(stderr, msg);
        fputs(",\"help\":", stderr);
        if (help) json_str(stderr, help); else fputs("null", stderr);
        fputs(",\"source\":", stderr); json_str(stderr, source_line(pos));
        fputs("}\n", stderr);
        return;
    }
    if (code) fprintf(stderr, "%s:%d:%d: %s[%s]: %s\n", xs_file_name, pos.line, pos.col, severity, xs_diag_name(code), msg);
    else fprintf(stderr, "%s:%d:%d: %s: %s\n", xs_file_name, pos.line, pos.col, severity, msg);
    print_source_line(pos);
    if (help) fprintf(stderr, "  help: %s\n", help);
}

static char *vformat(const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    char *msg = xs_alloc((size_t)n + 1);
    vsnprintf(msg, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return msg;
}

void xs_error(DiagCode code, Pos pos, const char *help, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *msg = vformat(fmt, ap);
    va_end(ap);
    report(code, "error", pos, help, msg);
    xs_error_count++;
    if (xs_error_recover && (xs_error_count < 20 || xs_check_mode)) _longjmp(*xs_error_recover, 1);
    if (xs_error_recover && !xs_json_errors) fprintf(stderr, "xisto: stopping after %d errors\n", xs_error_count);
    exit(1);
}

void xs_error_continue(DiagCode code, Pos pos, const char *help, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *msg = vformat(fmt, ap);
    va_end(ap);
    report(code, "error", pos, help, msg);
    xs_error_count++;
}

/* stops checking the current statement for an error already reported (a use of a failed declaration) */
void xs_error_quiet(void) {
    xs_quiet_count++;
    if (xs_error_recover) _longjmp(*xs_error_recover, 1);
    exit(1);
}

bool xs_warnings = true;
bool xs_tls_off = false;
bool xs_node_compat = false;
int xs_warning_count;

void xs_warn(Pos pos, const char *help, const char *fmt, ...) {
    if (!xs_warnings) return;
    va_list ap;
    va_start(ap, fmt);
    char *msg = vformat(fmt, ap);
    va_end(ap);
    report(XS0000, "warning", pos, help, msg);
    xs_warning_count++;
}

/* a use of any: compiles (the dynamic path), so it is no change to make; xisto check lists it apart */
void xs_note_dynamic(Pos pos) {
    if (!xs_check_mode || !xs_json_errors) return;
    report(XS2007, "note", pos, "a declared type (an interface, a class, Record<string, T>) takes the typed path, with no check at run time",
           "'any' takes the dynamic path: read and written by name, checked at run time (slower than a typed value)");
}

static int edit_distance(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    int *prev = xs_alloc(sizeof(int) * (lb + 1)), *cur = xs_alloc(sizeof(int) * (lb + 1));
    for (size_t j = 0; j <= lb; j++) prev[j] = (int)j;
    for (size_t i = 1; i <= la; i++) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= lb; j++) {
            int cost = tolower((unsigned char)a[i - 1]) == tolower((unsigned char)b[j - 1]) ? 0 : 1;
            int d = prev[j - 1] + cost;
            if (prev[j] + 1 < d) d = prev[j] + 1;
            if (cur[j - 1] + 1 < d) d = cur[j - 1] + 1;
            cur[j] = d;
        }
        int *t = prev; prev = cur; cur = t;
    }
    int r = prev[lb];
    free(prev);
    free(cur);
    return r;
}

const char *xs_suggest(const char *name, const char **candidates, int n) {
    const char *best = NULL;
    int best_d = 1 << 30;
    size_t len = strlen(name);
    for (int i = 0; i < n; i++) {
        if (!candidates[i] || strcmp(candidates[i], name) == 0) continue;
        int d = edit_distance(name, candidates[i]);
        if (d < best_d) { best_d = d; best = candidates[i]; }
    }
    int limit = len <= 3 ? 1 : len <= 6 ? 2 : 3;
    return best && best_d <= limit ? best : NULL;
}

char *xs_help_suggest(const char *name, const char **candidates, int n, const char *fallback) {
    const char *s = xs_suggest(name, candidates, n);
    if (s) return xs_sprintf("did you mean '%s'?", s);
    return (char *)fallback;
}

void expr_list_push(ExprList *l, Expr *e) {
    if (l->len == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->items = realloc(l->items, sizeof(Expr *) * (size_t)l->cap);
    }
    l->items[l->len++] = e;
}

void stmt_list_push(StmtList *l, Stmt *s) {
    if (l->len == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->items = realloc(l->items, sizeof(Stmt *) * (size_t)l->cap);
    }
    l->items[l->len++] = s;
}

/* ---------- types ---------- */

static Type t_number = {.kind = TY_NUMBER}, t_int = {.kind = TY_INT}, t_litint = {.kind = TY_LITINT},
            t_boolean = {.kind = TY_BOOLEAN}, t_string = {.kind = TY_STRING}, t_void = {.kind = TY_VOID}, t_null = {.kind = TY_NULL},
            t_json = {.kind = TY_JSON}, t_unknown = {.kind = TY_UNKNOWN},
            t_any = {.kind = TY_UNKNOWN, .tname = "any"}; /* any: an unknown read and written by name (the dynamic path) */
Type *ty_number = &t_number, *ty_int = &t_int, *ty_litint = &t_litint, *ty_boolean = &t_boolean,
     *ty_string = &t_string, *ty_void = &t_void, *ty_null = &t_null, *ty_json = &t_json, *ty_unknown = &t_unknown, *ty_any = &t_any;

Type *ty_nullable_of(Type *inner) {
    if (inner->kind == TY_NULLABLE) return inner;
    Type *t = xs_alloc(sizeof *t);
    t->kind = TY_NULLABLE;
    t->elem = inner;
    return t;
}

/* x?: T: T | null whose null is "absent" (undefined in JavaScript's output); x?: T | null stays T | null */
Type *ty_optional_of(Type *inner) {
    if (inner->kind == TY_NULLABLE) return inner;
    Type *t = ty_nullable_of(inner);
    t->absent = true;
    return t;
}

Type *ty_strip_null(Type *t) { return t->kind == TY_NULLABLE ? t->elem : t; }

Type *ty_map_of(Type *key, Type *value) {
    Type *t = xs_alloc(sizeof *t);
    t->kind = TY_MAP;
    t->key = key;
    t->elem = value;
    return t;
}

Type *ty_chan_of(Type *elem) {
    Type *t = xs_alloc(sizeof *t);
    t->kind = TY_CHAN;
    t->elem = elem;
    return t;
}

Type *ty_stream_of(Type *elem) {
    Type *t = xs_alloc(sizeof *t);
    t->kind = TY_STREAM;
    t->elem = elem;
    return t;
}

Type *ty_set_of(Type *elem) {
    Type *t = xs_alloc(sizeof *t);
    t->kind = TY_SET;
    t->elem = elem;
    return t;
}

Type *ty_array_of(Type *elem) {
    Type *t = xs_alloc(sizeof *t);
    t->kind = TY_ARRAY;
    t->elem = elem;
    return t;
}

Type *ty_object_of(Class *cls) {
    Type *t = xs_alloc(sizeof *t);
    t->kind = TY_OBJECT;
    t->cls = cls;
    return t;
}

Type *ty_enum_of(Enum *en) {
    Type *t = xs_alloc(sizeof *t);
    t->kind = TY_ENUM;
    t->en = en;
    return t;
}

Type *ty_func(Type **params, int nparams, Type *ret) {
    Type *t = xs_alloc(sizeof *t);
    t->kind = TY_FUNC;
    t->params = params;
    t->nparams = nparams;
    t->ret = ret;
    return t;
}

bool ty_equal(Type *a, Type *b) {
    if (a == b) return true;
    if (!a || !b || a->kind != b->kind) return false;
    switch (a->kind) {
    case TY_ARRAY: case TY_NULLABLE: case TY_SET: case TY_STREAM: case TY_CHAN: return ty_equal(a->elem, b->elem);
    case TY_MAP: return ty_equal(a->key, b->key) && ty_equal(a->elem, b->elem);
    case TY_OBJECT: return a->cls == b->cls;
    case TY_UNION: { /* the same members, in any order */
        if (a->nparams != b->nparams) return false;
        for (int i = 0; i < a->nparams; i++) {
            bool found = false;
            for (int k = 0; k < b->nparams && !found; k++) found = ty_equal(a->params[i], b->params[k]);
            if (!found) return false;
        }
        return true;
    }
    case TY_ENUM: return a->en == b->en;
    case TY_FUNC:
        if (a->nparams != b->nparams || !ty_equal(a->ret, b->ret)) return false;
        for (int i = 0; i < a->nparams; i++)
            if (!ty_equal(a->params[i], b->params[i])) return false;
        return true;
    default: return true;
    }
}

bool ty_is_heap(Type *t) {
    return t->kind == TY_STRING || t->kind == TY_JSON || t->kind == TY_UNKNOWN || t->kind == TY_UNION || t->kind == TY_ARRAY || t->kind == TY_OBJECT || t->kind == TY_MAP || t->kind == TY_SET || t->kind == TY_STREAM || t->kind == TY_CHAN ||
           t->kind == TY_FUNC || (t->kind == TY_NULLABLE && ty_is_heap(t->elem));
}

const char *ty_name(Type *t) {
    switch (t->kind) {
    case TY_TUPLE: { /* [A, B] as written, before the checker makes its class */
        char *r = "[";
        for (int i = 0; i < t->nparams; i++) r = xs_sprintf("%s%s%s", r, i ? ", " : "", ty_name(t->params[i]));
        return xs_sprintf("%s]", r);
    }
    case TY_NUMBER: return "number";
    case TY_INT: return "int";
    case TY_LITINT: return "integer literal";
    case TY_BOOLEAN: return "boolean";
    case TY_STRING: return t->tname ? xs_sprintf("\"%s\"", t->tname) : "string";
    case TY_VOID: return "void";
    case TY_ARRAY: return xs_sprintf(t->elem->kind == TY_NULLABLE ? "(%s)[]" : "%s[]", ty_name(t->elem));
    case TY_OBJECT: return t->cls->name;
    case TY_ENUM: return t->en->name;
    case TY_NULLABLE: return xs_sprintf("%s | null", ty_name(t->elem));
    case TY_NULL: return "null";
    case TY_TPARAM: return t->tname;
    case TY_UNION: {
        Buf b = {0};
        for (int i = 0; i < t->nparams; i++) buf_printf(&b, "%s%s", i ? " | " : "", ty_name(t->params[i]));
        return b.data;
    }
    case TY_JSON: return "object";
    case TY_UNKNOWN: return t->tname ? t->tname : "unknown";
    case TY_MAP: return xs_sprintf("Map<%s, %s>", ty_name(t->key), ty_name(t->elem));
    case TY_SET: return xs_sprintf("Set<%s>", ty_name(t->elem));
    case TY_STREAM: return xs_sprintf("Stream<%s>", ty_name(t->elem));
    case TY_CHAN: return xs_sprintf("Channel<%s>", ty_name(t->elem));
    case TY_FUNC: {
        Buf b = {0};
        buf_puts(&b, "(");
        for (int i = 0; i < t->nparams; i++) buf_printf(&b, "%s%s", i ? ", " : "", ty_name(t->params[i]));
        buf_printf(&b, ") => %s", ty_name(t->ret));
        return b.data;
    }
    }
    return "?";
}

char *xs_read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = xs_alloc((size_t)n + 2); /* (two zero bytes: the lexer peeks one past the last character) */
    if (n > 0 && fread(s, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return NULL;
    }
    s[n] = 0;
    fclose(f);
    return s;
}

/* A module path for messages and stack traces: relative to the current directory when it is below it,
   without "./" segments ("/work/app/./lib.ts" -> "app/lib.ts" from /work). */
const char *xs_display_path(const char *path) {
    static char cwd[4096];
    if (!cwd[0] && !getcwd(cwd, sizeof cwd)) cwd[0] = 0;
    Buf b = {0};
    for (const char *p = path; *p; p++) {
        if (p[0] == '/' && p[1] == '.' && p[2] == '/') { p++; continue; }
        buf_putc(&b, *p);
    }
    const char *r = b.data ? b.data : path;
    size_t n = strlen(cwd);
    if (n && strncmp(r, cwd, n) == 0 && r[n] == '/') r += n + 1;
    return r;
}

/* compiler strings keep NUL as C0 80 (lexer.c, xs_unescape): the length the program sees */
size_t xs_lit_len(const char *s) {
    size_t n = 0;
    for (; *s; s++, n++) if ((unsigned char)s[0] == 0xC0 && (unsigned char)s[1] == 0x80) s++;
    return n;
}
