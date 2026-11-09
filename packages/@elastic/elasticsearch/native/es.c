/* @elastic/elasticsearch: the JSON work of the client on the text of a request (xisto's `object` is JSON text).
   An API call's parameters are one object whose top-level keys go to three places: the path (index, id, name),
   the query string (refresh, routing...) and the body (everything else). These functions pick and drop top-level
   keys without parsing the values (a value is skipped by its brackets and strings), and turn the operations of a
   bulk call into NDJSON (one value per line). */

static const char *xn_es_ws(const char *p, const char *e) { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; return p; }
/* the end of the JSON value at p */
static const char *xn_es_skip(const char *p, const char *e) {
    if (p >= e) return p;
    if (*p == '"') {
        p++;
        while (p < e && *p != '"') p += *p == '\\' ? 2 : 1;
        return p < e ? p + 1 : e;
    }
    if (*p == '{' || *p == '[') {
        int d = 0;
        while (p < e) {
            if (*p == '"') { p = xn_es_skip(p, e); continue; }
            if (*p == '{' || *p == '[') d++;
            else if (*p == '}' || *p == ']') { d--; if (d == 0) return p + 1; }
            p++;
        }
        return e;
    }
    while (p < e && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
    return p;
}
/* each top-level member of the object in s: key (with its quotes) [ks, ke), value [vs, ve) */
typedef struct { const char *ks, *ke, *vs, *ve; } xn_es_member;
static int xn_es_members(const char *s, size_t n, xn_es_member *out, int max) {
    const char *p = s, *e = s + n;
    p = xn_es_ws(p, e);
    if (p >= e || *p != '{') return -1;
    p++;
    int k = 0;
    for (;;) {
        p = xn_es_ws(p, e);
        if (p >= e || *p == '}') break;
        if (*p != '"') return -1;
        const char *ks = p;
        p = xn_es_skip(p, e);
        const char *ke = p;
        p = xn_es_ws(p, e);
        if (p >= e || *p != ':') return -1;
        p = xn_es_ws(p + 1, e);
        const char *vs = p;
        p = xn_es_skip(p, e);
        if (k < max) out[k] = (xn_es_member){ks, ke, vs, p};
        k++;
        p = xn_es_ws(p, e);
        if (p < e && *p == ',') p++;
    }
    return k < max ? k : max;
}
static bool xn_es_key_is(const xn_es_member *m, const char *key) {
    size_t kl = strlen(key);
    return (size_t)(m->ke - m->ks) == kl + 2 && memcmp(m->ks + 1, key, kl) == 0;
}
static bool xn_es_in_list(const xn_es_member *m, const char *list, size_t ln) {
    const char *p = list, *e = list + ln;
    while (p < e) {
        const char *c = memchr(p, ',', (size_t)(e - p));
        if (!c) c = e;
        size_t kl = (size_t)(c - p);
        if ((size_t)(m->ke - m->ks) == kl + 2 && memcmp(m->ks + 1, p, kl) == 0) return true;
        p = c + 1;
    }
    return false;
}
#define XN_ES_MAX 256

/* field(json, key): the JSON text of the top-level member key, or "" */
XS_NOINLINE xs_str *xn__elastic_elasticsearch_field(xs_str *json, xs_str *key) {
    xn_es_member m[XN_ES_MAX];
    int n = xn_es_members(json->data, (size_t)json->len, m, XN_ES_MAX);
    char *k = xs_strndup_(key->data, (size_t)key->len);
    for (int i = 0; i < n; i++) {
        if (xn_es_key_is(&m[i], k)) { free(k); return xs_str_new(m[i].vs, (size_t)(m[i].ve - m[i].vs)); }
    }
    free(k);
    return xs_str_new("", 0);
}

/* without(json, keys): the object without the top-level members whose keys are in the comma-separated list
   ("" when nothing is left) */
XS_NOINLINE xs_str *xn__elastic_elasticsearch_without(xs_str *json, xs_str *keys) {
    xn_es_member m[XN_ES_MAX];
    int n = xn_es_members(json->data, (size_t)json->len, m, XN_ES_MAX);
    if (n < 0) return xs_str_new("", 0);
    xs_sb sb = {0};
    int kept = 0;
    xs_sb_put(&sb, "{", 1);
    for (int i = 0; i < n; i++) {
        if (xn_es_in_list(&m[i], keys->data, (size_t)keys->len)) continue;
        if (kept++) xs_sb_put(&sb, ",", 1);
        xs_sb_put(&sb, m[i].ks, (size_t)(m[i].ke - m[i].ks));
        xs_sb_put(&sb, ":", 1);
        xs_sb_put(&sb, m[i].vs, (size_t)(m[i].ve - m[i].vs));
    }
    xs_sb_put(&sb, "}", 1);
    if (!kept) { xs_str *s = xs_sb_finish(&sb); xs_release(s); return xs_str_new("", 0); }
    return xs_sb_finish(&sb);
}

/* text(raw): a JSON scalar as text for a path or a query string: a string unescaped, a number or boolean as it is
   written; arrays of those joined by commas (index: ["a", "b"] -> a,b) */
static void xn_es_unescape(xs_sb *sb, const char *p, const char *e) {
    for (; p < e; p++) {
        if (*p != '\\' || p + 1 >= e) { xs_sb_put(sb, p, 1); continue; }
        p++;
        char c = *p;
        if (c == 'n') xs_sb_put(sb, "\n", 1);
        else if (c == 't') xs_sb_put(sb, "\t", 1);
        else if (c == 'r') xs_sb_put(sb, "\r", 1);
        else if (c == 'u' && p + 4 < e) {
            unsigned v = 0;
            for (int i = 1; i <= 4; i++) { char h = p[i]; v = v * 16 + (unsigned)(h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0'); }
            char u[4]; int k = 0;
            if (v < 0x80) u[k++] = (char)v;
            else if (v < 0x800) { u[k++] = (char)(0xC0 | (v >> 6)); u[k++] = (char)(0x80 | (v & 0x3F)); }
            else { u[k++] = (char)(0xE0 | (v >> 12)); u[k++] = (char)(0x80 | ((v >> 6) & 0x3F)); u[k++] = (char)(0x80 | (v & 0x3F)); }
            xs_sb_put(sb, u, (size_t)k);
            p += 4;
        } else xs_sb_put(sb, &c, 1);
    }
}
XS_NOINLINE xs_str *xn__elastic_elasticsearch_text(xs_str *raw) {
    const char *p = raw->data, *e = raw->data + raw->len;
    p = xn_es_ws(p, e);
    xs_sb sb = {0};
    if (p < e && *p == '[') {
        p++;
        int k = 0;
        for (;;) {
            p = xn_es_ws(p, e);
            if (p >= e || *p == ']') break;
            const char *q = xn_es_skip(p, e);
            if (k++) xs_sb_put(&sb, ",", 1);
            if (*p == '"') xn_es_unescape(&sb, p + 1, q - 1); else xs_sb_put(&sb, p, (size_t)(q - p));
            p = xn_es_ws(q, e);
            if (p < e && *p == ',') p++;
        }
    } else if (p < e && *p == '"') {
        const char *q = xn_es_skip(p, e);
        xn_es_unescape(&sb, p + 1, q - 1);
    } else if (p < e && strncmp(p, "null", 4) != 0) {
        const char *q = xn_es_skip(p, e);
        xs_sb_put(&sb, p, (size_t)(q - p));
    }
    if (!sb.len) { xs_str *s = xs_sb_finish(&sb); xs_release(s); return xs_str_new("", 0); }
    return xs_sb_finish(&sb);
}

/* ndjson(raw): the elements of a JSON array, one per line, each line ended by "\n" (the body of _bulk) */
XS_NOINLINE xs_str *xn__elastic_elasticsearch_ndjson(xs_str *raw) {
    const char *p = raw->data, *e = raw->data + raw->len;
    p = xn_es_ws(p, e);
    xs_sb sb = {0};
    if (p < e && *p == '[') {
        p++;
        for (;;) {
            p = xn_es_ws(p, e);
            if (p >= e || *p == ']') break;
            const char *q = xn_es_skip(p, e);
            /* a value written with newlines (pretty) is kept on one line: JSON allows whitespace to go */
            for (const char *c = p; c < q; c++) {
                if (*c == '"') { const char *s = xn_es_skip(c, q); xs_sb_put(&sb, c, (size_t)(s - c)); c = s - 1; continue; }
                if (*c == '\n' || *c == '\r') continue;
                xs_sb_put(&sb, c, 1);
            }
            xs_sb_put(&sb, "\n", 1);
            p = xn_es_ws(q, e);
            if (p < e && *p == ',') p++;
        }
    }
    if (!sb.len) { xs_str *s = xs_sb_finish(&sb); xs_release(s); return xs_str_new("", 0); }
    return xs_sb_finish(&sb);
}

/* encode(s, path): encodeURIComponent (letters, digits and - _ . ! ~ * ' ( ) as they are, every other byte of the
   UTF-8 text as %XX); in a path segment a comma (index lists) and * (patterns) stay as they are, as the npm client
   writes them */
XS_NOINLINE xs_str *xn__elastic_elasticsearch_encode(xs_str *s, bool path) {
    static const char hex[] = "0123456789ABCDEF";
    xs_sb sb = {0};
    for (int64_t i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->data[i];
        bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-_.!~*'()", c) != NULL;
        if (c == 0) keep = false;
        if (path && c == ',') keep = true;
        if (keep) { char ch = (char)c; xs_sb_put(&sb, &ch, 1); continue; }
        char e[3] = {'%', hex[c >> 4], hex[c & 15]};
        xs_sb_put(&sb, e, 3);
    }
    if (!sb.len) { xs_str *r = xs_sb_finish(&sb); xs_release(r); return xs_str_new("", 0); }
    return xs_sb_finish(&sb);
}
