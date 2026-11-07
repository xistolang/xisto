/* A small JSON reader and writer for the debug protocol (DAP messages), shared by the debugger agent inside a
   --debug binary (runtime/xisto_dbg.h) and by the `xisto debug` client (src/debug.c). Not the program's JSON:
   it allocates with malloc, keeps no refcounts and is never in a release binary. */
#ifndef XS_DJ_H
#define XS_DJ_H
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef enum { DJ_NULL, DJ_BOOL, DJ_NUM, DJ_STR, DJ_ARR, DJ_OBJ } dj_kind;
typedef struct dj {
    dj_kind kind;
    double num;          /* DJ_NUM, DJ_BOOL (0/1) */
    char *str;           /* DJ_STR; the key, for members of an object */
    char *key;
    struct dj **items;   /* DJ_ARR elements, DJ_OBJ members */
    int n;
} dj;

static void dj_free(dj *v) {
    if (!v) return;
    for (int i = 0; i < v->n; i++) dj_free(v->items[i]);
    free(v->items);
    free(v->str);
    free(v->key);
    free(v);
}

typedef struct { const char *s; int depth; bool bad; } dj_in;
static void dj_ws(dj_in *in) { while (*in->s == ' ' || *in->s == '\t' || *in->s == '\n' || *in->s == '\r') in->s++; }
static void dj_add(dj *v, dj *it) {
    v->items = realloc(v->items, sizeof(dj *) * (size_t)(v->n + 1));
    v->items[v->n++] = it;
}
static char *dj_parse_str(dj_in *in) {
    if (*in->s != '"') { in->bad = true; return NULL; }
    in->s++;
    size_t cap = 32, len = 0;
    char *o = malloc(cap);
    while (*in->s && *in->s != '"') {
        unsigned c = (unsigned char)*in->s++;
        if (c == '\\') {
            char e = *in->s++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': {
                unsigned u = 0;
                for (int k = 0; k < 4; k++) {
                    char h = *in->s;
                    if (!h) { in->bad = true; free(o); return NULL; }
                    in->s++;
                    u = u * 16 + (unsigned)(h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0');
                }
                if (len + 4 >= cap) o = realloc(o, cap *= 2);
                if (u < 0x80) o[len++] = (char)u; /* the protocol's strings are paths and messages: BMP is enough */
                else if (u < 0x800) { o[len++] = (char)(0xC0 | (u >> 6)); o[len++] = (char)(0x80 | (u & 0x3F)); }
                else { o[len++] = (char)(0xE0 | (u >> 12)); o[len++] = (char)(0x80 | ((u >> 6) & 0x3F)); o[len++] = (char)(0x80 | (u & 0x3F)); }
                continue;
            }
            case 0: in->bad = true; free(o); return NULL;
            default: c = (unsigned char)e; break;
            }
        }
        if (len + 1 >= cap) o = realloc(o, cap *= 2);
        o[len++] = (char)c;
    }
    if (*in->s != '"') { in->bad = true; free(o); return NULL; }
    in->s++;
    o[len] = 0;
    return o;
}
static dj *dj_value(dj_in *in) {
    dj_ws(in);
    if (++in->depth > 64) { in->bad = true; return NULL; }
    dj *v = calloc(1, sizeof *v);
    char c = *in->s;
    if (c == '{' || c == '[') {
        v->kind = c == '{' ? DJ_OBJ : DJ_ARR;
        in->s++;
        dj_ws(in);
        char close = c == '{' ? '}' : ']';
        if (*in->s == close) in->s++;
        else for (;;) {
            char *key = NULL;
            if (v->kind == DJ_OBJ) {
                dj_ws(in);
                key = dj_parse_str(in);
                dj_ws(in);
                if (in->bad || *in->s != ':') { in->bad = true; free(key); break; }
                in->s++;
            }
            dj *it = dj_value(in);
            if (in->bad) { free(key); dj_free(it); break; }
            it->key = key;
            dj_add(v, it);
            dj_ws(in);
            if (*in->s == ',') { in->s++; continue; }
            if (*in->s == close) { in->s++; break; }
            in->bad = true;
            break;
        }
    } else if (c == '"') {
        v->kind = DJ_STR;
        v->str = dj_parse_str(in);
    } else if (strncmp(in->s, "true", 4) == 0) { v->kind = DJ_BOOL; v->num = 1; in->s += 4; }
    else if (strncmp(in->s, "false", 5) == 0) { v->kind = DJ_BOOL; in->s += 5; }
    else if (strncmp(in->s, "null", 4) == 0) { v->kind = DJ_NULL; in->s += 4; }
    else if (c == '-' || (c >= '0' && c <= '9')) {
        char *end;
        v->kind = DJ_NUM;
        v->num = strtod(in->s, &end);
        in->s = end;
    } else in->bad = true;
    in->depth--;
    return v;
}
/* NULL when the text is not one JSON value */
static dj *dj_parse(const char *s) {
    dj_in in = {s, 0, false};
    dj *v = dj_value(&in);
    dj_ws(&in);
    if (in.bad || *in.s) { dj_free(v); return NULL; }
    return v;
}
static dj *dj_get(const dj *v, const char *key) {
    if (!v || v->kind != DJ_OBJ) return NULL;
    for (int i = 0; i < v->n; i++) if (v->items[i]->key && strcmp(v->items[i]->key, key) == 0) return v->items[i];
    return NULL;
}
static const char *dj_str(const dj *v, const char *key, const char *dflt) {
    dj *x = dj_get(v, key);
    return x && x->kind == DJ_STR ? x->str : dflt;
}
static long long dj_int(const dj *v, const char *key, long long dflt) {
    dj *x = dj_get(v, key);
    return x && x->kind == DJ_NUM ? (long long)x->num : dflt;
}
static bool dj_bool(const dj *v, const char *key, bool dflt) {
    dj *x = dj_get(v, key);
    return x && x->kind == DJ_BOOL ? x->num != 0 : dflt;
}

/* writing: a growing buffer and JSON string escaping */
typedef struct { char *data; size_t len, cap; } dj_buf;
static void dj_put(dj_buf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap) b->cap = b->cap ? b->cap * 2 : 256;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}
static void dj_puts(dj_buf *b, const char *s) { dj_put(b, s, strlen(s)); }
static void dj_printf(dj_buf *b, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) { dj_put(b, tmp, (size_t)n); return; }
    char *big = malloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    dj_put(b, big, (size_t)n);
    free(big);
}
static void dj_quote(dj_buf *b, const char *s) {
    dj_put(b, "\"", 1);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { char e[2] = {'\\', (char)c}; dj_put(b, e, 2); }
        else if (c == '\n') dj_put(b, "\\n", 2);
        else if (c == '\t') dj_put(b, "\\t", 2);
        else if (c < 0x20) dj_printf(b, "\\u%04x", c);
        else dj_put(b, (const char *)&c, 1);
    }
    dj_put(b, "\"", 1);
}

/* DAP framing: "Content-Length: N\r\n\r\n" + N bytes. Returns the body (malloc'd) or NULL at end of input. */
static char *dj_read_message(int fd) {
    char header[256];
    size_t hl = 0;
    long length = -1;
    for (;;) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r <= 0) return NULL;
        if (hl + 1 < sizeof header) header[hl++] = c;
        if (hl >= 4 && memcmp(header + hl - 4, "\r\n\r\n", 4) == 0) {
            header[hl] = 0;
            const char *p = strstr(header, "Content-Length:");
            if (p) length = strtol(p + 15, NULL, 10);
            break;
        }
    }
    if (length < 0 || length > (8L << 20)) return NULL; /* DAP messages are small: 8 MB ends a hostile client */
    char *body = malloc((size_t)length + 1);
    size_t got = 0;
    while (got < (size_t)length) {
        ssize_t r = read(fd, body + got, (size_t)length - got);
        if (r <= 0) { free(body); return NULL; }
        got += (size_t)r;
    }
    body[length] = 0;
    return body;
}
static bool dj_write_message(int fd, const char *body) {
    char header[64];
    size_t n = strlen(body);
    int hl = snprintf(header, sizeof header, "Content-Length: %zu\r\n\r\n", n);
    dj_buf all = {0};
    dj_put(&all, header, (size_t)hl);
    dj_put(&all, body, n);
    size_t off = 0;
    while (off < all.len) {
        ssize_t w = write(fd, all.data + off, all.len - off);
        if (w <= 0) { free(all.data); return false; }
        off += (size_t)w;
    }
    free(all.data);
    return true;
}
#endif
