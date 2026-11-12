/* mongodb (the Node driver's contract, the part mongoose needs): a MongoDB client over the OP_MSG wire protocol.
   Builds on the coroutine runtime (sockets) and xisto:crypto (SCRAM-SHA-256).

   - Commands come from xisto as text: JSON plus a few tagged values the program wrote from its own objects
     ($O"hex" ObjectId, $D<ms> date, $R["src","flags"] regex, $B"base64" binary, $L<n> int64, NaN/Infinity).
     Numbers follow js-bson: an integer in the int32 range is an int32, anything else a double.
   - Replies go back as JSON text: ObjectId as its hex string, a date as ISO text, int64 as a number, a binary as
     Node's Buffer JSON ({"type":"Buffer","data":[...]}), decimal128 as {"$numberDecimal":"..."}.
   - Connections are pooled per worker thread (a coroutine and its socket stay on one event loop); each one is
     opened with hello (and SCRAM-SHA-256 when there is a user). A connection that cannot be opened is tried again
     until serverSelectionTimeoutMS, as the driver's server selection does; the error is then the last one.
   - Inside a request handler every wait yields the coroutine; elsewhere it blocks.
   - A cursor (find, aggregate) is drained with getMore; its documents are kept as raw BSON until the last batch and
     only then written as JSON, without yielding. */

#include <sys/utsname.h>
#include <arpa/inet.h>

/* ---------- BSON writer ---------- */
typedef struct { uint8_t *p; size_t len, cap; } xs_mg_buf;
static void xs_mg_need(xs_mg_buf *b, size_t n) {
    if (b->len + n <= b->cap) return;
    b->cap = (b->len + n) * 2 + 256;
    b->p = realloc(b->p, b->cap);
    if (!b->p) xs_panic("out of memory");
}
static void xs_mg_put(xs_mg_buf *b, const void *s, size_t n) { xs_mg_need(b, n); memcpy(b->p + b->len, s, n); b->len += n; }
static void xs_mg_u8(xs_mg_buf *b, uint8_t v) { xs_mg_put(b, &v, 1); }
static void xs_mg_i32(xs_mg_buf *b, int32_t v) { uint32_t u = (uint32_t)v; uint8_t x[4] = {(uint8_t)u, (uint8_t)(u >> 8), (uint8_t)(u >> 16), (uint8_t)(u >> 24)}; xs_mg_put(b, x, 4); }
static void xs_mg_i64(xs_mg_buf *b, int64_t v) { uint64_t u = (uint64_t)v; uint8_t x[8]; for (int i = 0; i < 8; i++) x[i] = (uint8_t)(u >> (8 * i)); xs_mg_put(b, x, 8); }
static void xs_mg_f64(xs_mg_buf *b, double d) { uint64_t u; memcpy(&u, &d, 8); xs_mg_i64(b, (int64_t)u); }
static void xs_mg_set32(xs_mg_buf *b, size_t at, int32_t v) { uint32_t u = (uint32_t)v; b->p[at] = (uint8_t)u; b->p[at + 1] = (uint8_t)(u >> 8); b->p[at + 2] = (uint8_t)(u >> 16); b->p[at + 3] = (uint8_t)(u >> 24); }
static int32_t xs_mg_get32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static int64_t xs_mg_get64(const uint8_t *p) { uint64_t u = 0; for (int i = 7; i >= 0; i--) u = u << 8 | p[i]; return (int64_t)u; }
/* an element's type and key */
static void xs_mg_key(xs_mg_buf *b, uint8_t type, const char *key, size_t klen) { xs_mg_u8(b, type); xs_mg_put(b, key, klen); xs_mg_u8(b, 0); }
static size_t xs_mg_doc_begin(xs_mg_buf *b) { size_t at = b->len; xs_mg_i32(b, 0); return at; }
static void xs_mg_doc_end(xs_mg_buf *b, size_t at) { xs_mg_u8(b, 0); xs_mg_set32(b, at, (int32_t)(b->len - at)); }
static void xs_mg_str_el(xs_mg_buf *b, const char *key, const char *s, size_t n) {
    xs_mg_key(b, 0x02, key, strlen(key)); xs_mg_i32(b, (int32_t)n + 1); xs_mg_put(b, s, n); xs_mg_u8(b, 0);
}
static void xs_mg_i32_el(xs_mg_buf *b, const char *key, int32_t v) { xs_mg_key(b, 0x10, key, strlen(key)); xs_mg_i32(b, v); }
static void xs_mg_bin_el(xs_mg_buf *b, const char *key, const void *s, size_t n) {
    xs_mg_key(b, 0x05, key, strlen(key)); xs_mg_i32(b, (int32_t)n); xs_mg_u8(b, 0); xs_mg_put(b, s, n);
}

/* ---------- the program's command text -> BSON ---------- */
typedef struct { const char *s, *end; char err[160]; } xs_mg_rd;
static void xs_mg_ws(xs_mg_rd *r) { while (r->s < r->end && (*r->s == ' ' || *r->s == '\n' || *r->s == '\t' || *r->s == '\r')) r->s++; }
static bool xs_mg_fail(xs_mg_rd *r, const char *what) { if (!r->err[0]) snprintf(r->err, sizeof r->err, "mongodb: bad command text (%s)", what); return false; }
static int xs_mg_hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static void xs_mg_utf8(xs_mg_buf *o, uint32_t cp) {
    uint8_t x[4];
    if (cp < 0x80) { x[0] = (uint8_t)cp; xs_mg_put(o, x, 1); }
    else if (cp < 0x800) { x[0] = (uint8_t)(0xC0 | cp >> 6); x[1] = (uint8_t)(0x80 | (cp & 63)); xs_mg_put(o, x, 2); }
    else if (cp < 0x10000) { x[0] = (uint8_t)(0xE0 | cp >> 12); x[1] = (uint8_t)(0x80 | ((cp >> 6) & 63)); x[2] = (uint8_t)(0x80 | (cp & 63)); xs_mg_put(o, x, 3); }
    else { x[0] = (uint8_t)(0xF0 | cp >> 18); x[1] = (uint8_t)(0x80 | ((cp >> 12) & 63)); x[2] = (uint8_t)(0x80 | ((cp >> 6) & 63)); x[3] = (uint8_t)(0x80 | (cp & 63)); xs_mg_put(o, x, 4); }
}
/* a JSON string at r->s, unescaped into o (no terminator) */
static bool xs_mg_jstr(xs_mg_rd *r, xs_mg_buf *o) {
    if (r->s >= r->end || *r->s != '"') return xs_mg_fail(r, "string expected");
    r->s++;
    while (r->s < r->end && *r->s != '"') {
        const char *q = r->s;
        while (q < r->end && *q != '"' && *q != '\\') q++;
        xs_mg_put(o, r->s, (size_t)(q - r->s));
        r->s = q;
        if (q >= r->end || *q == '"') break;
        r->s++;
        if (r->s >= r->end) break;
        char e = *r->s++;
        switch (e) {
        case 'n': xs_mg_u8(o, '\n'); break;
        case 't': xs_mg_u8(o, '\t'); break;
        case 'r': xs_mg_u8(o, '\r'); break;
        case 'b': xs_mg_u8(o, '\b'); break;
        case 'f': xs_mg_u8(o, '\f'); break;
        case 'u': {
            uint32_t cp = 0;
            for (int i = 0; i < 4; i++) { int h = r->s < r->end ? xs_mg_hex(*r->s++) : -1; if (h < 0) return xs_mg_fail(r, "bad \\u escape"); cp = cp << 4 | (uint32_t)h; }
            if (cp >= 0xD800 && cp < 0xDC00 && r->end - r->s >= 6 && r->s[0] == '\\' && r->s[1] == 'u') {
                uint32_t lo = 0;
                bool ok = true;
                for (int i = 0; i < 4; i++) { int h = xs_mg_hex(r->s[2 + i]); if (h < 0) ok = false; lo = lo << 4 | (uint32_t)(h < 0 ? 0 : h); }
                if (ok && lo >= 0xDC00 && lo < 0xE000) { r->s += 6; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
            }
            xs_mg_utf8(o, cp);
            break;
        }
        default: xs_mg_u8(o, (uint8_t)e); break;
        }
    }
    if (r->s >= r->end) return xs_mg_fail(r, "unterminated string");
    r->s++;
    return true;
}
static bool xs_mg_value(xs_mg_rd *r, xs_mg_buf *b, const char *key, size_t klen, int depth);
static bool xs_mg_object(xs_mg_rd *r, xs_mg_buf *b, int depth) { /* at '{' */
    size_t at = xs_mg_doc_begin(b);
    r->s++;
    xs_mg_ws(r);
    xs_mg_buf k = {0};
    if (r->s < r->end && *r->s == '}') { r->s++; xs_mg_doc_end(b, at); return true; }
    for (;;) {
        xs_mg_ws(r);
        k.len = 0;
        if (!xs_mg_jstr(r, &k)) { free(k.p); return false; }
        if (memchr(k.p, 0, k.len)) { free(k.p); return xs_mg_fail(r, "a key with a NUL byte"); }
        xs_mg_ws(r);
        if (r->s >= r->end || *r->s != ':') { free(k.p); return xs_mg_fail(r, "':' expected"); }
        r->s++;
        xs_mg_ws(r);
        if (!xs_mg_value(r, b, k.len ? (const char *)k.p : "", k.len, depth + 1)) { free(k.p); return false; }
        xs_mg_ws(r);
        if (r->s < r->end && *r->s == ',') { r->s++; continue; }
        if (r->s < r->end && *r->s == '}') { r->s++; break; }
        free(k.p);
        return xs_mg_fail(r, "',' or '}' expected");
    }
    free(k.p);
    xs_mg_doc_end(b, at);
    return true;
}
static bool xs_mg_array(xs_mg_rd *r, xs_mg_buf *b, int depth) { /* at '[' */
    size_t at = xs_mg_doc_begin(b);
    r->s++;
    xs_mg_ws(r);
    if (r->s < r->end && *r->s == ']') { r->s++; xs_mg_doc_end(b, at); return true; }
    for (int i = 0;; i++) {
        char key[24];
        int kl = snprintf(key, sizeof key, "%d", i);
        xs_mg_ws(r);
        if (!xs_mg_value(r, b, key, (size_t)kl, depth + 1)) return false;
        xs_mg_ws(r);
        if (r->s < r->end && *r->s == ',') { r->s++; continue; }
        if (r->s < r->end && *r->s == ']') { r->s++; break; }
        return xs_mg_fail(r, "',' or ']' expected");
    }
    xs_mg_doc_end(b, at);
    return true;
}
static bool xs_mg_number(xs_mg_rd *r, double *d, bool *integral) {
    const char *q = r->s;
    bool frac = false;
    if (r->end - q >= 3 && memcmp(q, "NaN", 3) == 0) { r->s += 3; *d = NAN; *integral = false; return true; }
    if (r->end - q >= 8 && memcmp(q, "Infinity", 8) == 0) { r->s += 8; *d = INFINITY; *integral = false; return true; }
    if (r->end - q >= 9 && memcmp(q, "-Infinity", 9) == 0) { r->s += 9; *d = -INFINITY; *integral = false; return true; }
    char tmp[64];
    size_t n = 0;
    while (q < r->end && n + 1 < sizeof tmp && (isdigit((unsigned char)*q) || *q == '-' || *q == '+' || *q == '.' || *q == 'e' || *q == 'E')) {
        if (*q == '.' || *q == 'e' || *q == 'E') frac = true;
        tmp[n++] = *q++;
    }
    tmp[n] = 0;
    if (n == 0) return xs_mg_fail(r, "value expected");
    r->s = q;
    *d = strtod(tmp, NULL);
    *integral = !frac;
    return true;
}
static bool xs_mg_value(xs_mg_rd *r, xs_mg_buf *b, const char *key, size_t klen, int depth) {
    if (depth > 100) return xs_mg_fail(r, "nested too deep");
    if (r->s >= r->end) return xs_mg_fail(r, "value expected");
    char c = *r->s;
    if (c == '{') { xs_mg_key(b, 0x03, key, klen); return xs_mg_object(r, b, depth); }
    if (c == '[') { xs_mg_key(b, 0x04, key, klen); return xs_mg_array(r, b, depth); }
    if (c == '"') {
        xs_mg_key(b, 0x02, key, klen);
        size_t at = b->len;
        xs_mg_i32(b, 0);
        if (!xs_mg_jstr(r, b)) return false;
        xs_mg_u8(b, 0);
        xs_mg_set32(b, at, (int32_t)(b->len - at - 4));
        return true;
    }
    if (r->end - r->s >= 4 && memcmp(r->s, "null", 4) == 0) { r->s += 4; xs_mg_key(b, 0x0A, key, klen); return true; }
    if (r->end - r->s >= 4 && memcmp(r->s, "true", 4) == 0) { r->s += 4; xs_mg_key(b, 0x08, key, klen); xs_mg_u8(b, 1); return true; }
    if (r->end - r->s >= 5 && memcmp(r->s, "false", 5) == 0) { r->s += 5; xs_mg_key(b, 0x08, key, klen); xs_mg_u8(b, 0); return true; }
    if (c == '$' && r->end - r->s >= 2) {
        char t = r->s[1];
        r->s += 2;
        if (t == 'O') { /* ObjectId: 24 hex digits */
            xs_mg_buf h = {0};
            if (!xs_mg_jstr(r, &h)) { free(h.p); return false; }
            if (h.len != 24) { free(h.p); return xs_mg_fail(r, "an ObjectId needs 24 hex digits"); }
            uint8_t id[12];
            for (int i = 0; i < 12; i++) {
                int hi = xs_mg_hex((char)h.p[2 * i]), lo = xs_mg_hex((char)h.p[2 * i + 1]);
                if (hi < 0 || lo < 0) { free(h.p); return xs_mg_fail(r, "an ObjectId needs 24 hex digits"); }
                id[i] = (uint8_t)(hi << 4 | lo);
            }
            free(h.p);
            xs_mg_key(b, 0x07, key, klen);
            xs_mg_put(b, id, 12);
            return true;
        }
        if (t == 'D' || t == 'L') { /* a date (ms since the epoch) or an int64 */
            double d;
            bool integral;
            if (!xs_mg_number(r, &d, &integral)) return false;
            xs_mg_key(b, t == 'D' ? 0x09 : 0x12, key, klen);
            xs_mg_i64(b, isfinite(d) ? (int64_t)d : 0);
            return true;
        }
        if (t == 'R') { /* ["source","flags"] */
            if (r->s >= r->end || *r->s != '[') return xs_mg_fail(r, "'[' expected");
            r->s++;
            xs_mg_buf src = {0}, fl = {0};
            bool ok = xs_mg_jstr(r, &src);
            xs_mg_ws(r);
            if (ok && r->s < r->end && *r->s == ',') { r->s++; xs_mg_ws(r); ok = xs_mg_jstr(r, &fl); } else ok = false;
            if (ok && r->s < r->end && *r->s == ']') r->s++; else ok = false;
            if (ok) {
                xs_mg_key(b, 0x0B, key, klen);
                xs_mg_put(b, src.p, src.len); xs_mg_u8(b, 0);
                /* BSON wants the options sorted; JavaScript's g and y have no meaning there (as js-bson) */
                const char *order = "ilmsux";
                for (const char *o = order; *o; o++) if (fl.len && memchr(fl.p, *o, fl.len)) xs_mg_u8(b, (uint8_t)*o);
                xs_mg_u8(b, 0);
            }
            free(src.p); free(fl.p);
            return ok ? true : xs_mg_fail(r, "bad regex");
        }
        if (t == 'B') { /* binary, subtype 0, from base64 */
            xs_mg_buf s = {0};
            if (!xs_mg_jstr(r, &s)) { free(s.p); return false; }
            uint8_t *raw = malloc(s.len + 4);
            long n = s.len ? xs_b64_decode((const char *)s.p, s.len, raw) : 0;
            free(s.p);
            if (n < 0) { free(raw); return xs_mg_fail(r, "bad base64"); }
            xs_mg_key(b, 0x05, key, klen);
            xs_mg_i32(b, (int32_t)n);
            xs_mg_u8(b, 0);
            xs_mg_put(b, raw, (size_t)n);
            free(raw);
            return true;
        }
        return xs_mg_fail(r, "unknown tag");
    }
    double d;
    bool integral;
    if (!xs_mg_number(r, &d, &integral)) return false;
    if (integral && d >= -2147483648.0 && d <= 2147483647.0 && d == floor(d) && !(d == 0 && signbit(d))) {
        xs_mg_key(b, 0x10, key, klen);
        xs_mg_i32(b, (int32_t)d);
    } else {
        xs_mg_key(b, 0x01, key, klen);
        xs_mg_f64(b, d);
    }
    return true;
}
/* the command document: the text's object with "$db" (and more fields) added at the end */
static bool xs_mg_encode_cmd(xs_mg_buf *b, const char *text, size_t n, const char *db, char *err, size_t errcap) {
    xs_mg_rd r = {text, text + n, {0}};
    xs_mg_ws(&r);
    if (r.s >= r.end || *r.s != '{') { snprintf(err, errcap, "mongodb: a command must be an object"); return false; }
    size_t start = b->len;
    if (!xs_mg_object(&r, b, 0)) { snprintf(err, errcap, "%s", r.err); return false; }
    /* reopen the document to append $db */
    b->len--;
    xs_mg_str_el(b, "$db", db, strlen(db));
    xs_mg_doc_end(b, start);
    return true;
}

/* ---------- BSON reader ---------- */
/* the size of the value of type t at p (within end); -1 when malformed */
static int64_t xs_mg_vsize(uint8_t t, const uint8_t *p, const uint8_t *end) {
    int64_t avail = end - p;
    switch (t) {
    case 0x01: case 0x09: case 0x11: case 0x12: return avail >= 8 ? 8 : -1;
    case 0x02: case 0x0D: case 0x0E: { if (avail < 4) return -1; int64_t n = xs_mg_get32(p); return n >= 1 && 4 + n <= avail ? 4 + n : -1; }
    case 0x03: case 0x04: { if (avail < 5) return -1; int64_t n = xs_mg_get32(p); return n >= 5 && n <= avail ? n : -1; }
    case 0x05: { if (avail < 5) return -1; int64_t n = xs_mg_get32(p); return n >= 0 && 5 + n <= avail ? 5 + n : -1; }
    case 0x06: case 0x0A: case 0x7F: case 0xFF: return 0;
    case 0x07: return avail >= 12 ? 12 : -1;
    case 0x08: return avail >= 1 ? 1 : -1;
    case 0x0B: { const uint8_t *q = memchr(p, 0, (size_t)avail); if (!q) return -1; const uint8_t *q2 = memchr(q + 1, 0, (size_t)(end - q - 1)); return q2 ? q2 + 1 - p : -1; }
    case 0x0C: { if (avail < 4) return -1; int64_t n = xs_mg_get32(p); return n >= 1 && 4 + n + 12 <= avail ? 4 + n + 12 : -1; }
    case 0x0F: { if (avail < 4) return -1; int64_t n = xs_mg_get32(p); return n >= 4 && n <= avail ? n : -1; }
    case 0x10: return avail >= 4 ? 4 : -1;
    case 0x13: return avail >= 16 ? 16 : -1;
    default: return -1;
    }
}
/* finds a top-level field of a document; its type and value */
static bool xs_mg_find(const uint8_t *doc, size_t dlen, const char *key, uint8_t *type, const uint8_t **val, int64_t *vlen) {
    if (dlen < 5) return false;
    const uint8_t *p = doc + 4, *end = doc + dlen - 1;
    while (p < end) {
        uint8_t t = *p++;
        const uint8_t *k = p;
        const uint8_t *z = memchr(p, 0, (size_t)(end - p));
        if (!z) return false;
        p = z + 1;
        int64_t n = xs_mg_vsize(t, p, end);
        if (n < 0) return false;
        if (strcmp((const char *)k, key) == 0) { *type = t; *val = p; *vlen = n; return true; }
        p += n;
    }
    return false;
}
static double xs_mg_num(const uint8_t *doc, size_t dlen, const char *key, double dflt) {
    uint8_t t; const uint8_t *v; int64_t n;
    if (!xs_mg_find(doc, dlen, key, &t, &v, &n)) return dflt;
    if (t == 0x10) return xs_mg_get32(v);
    if (t == 0x12) return (double)xs_mg_get64(v);
    if (t == 0x01) { double d; memcpy(&d, v, 8); return d; }
    if (t == 0x08) return v[0] ? 1 : 0;
    return dflt;
}
static bool xs_mg_str(const uint8_t *doc, size_t dlen, const char *key, char *out, size_t cap) {
    uint8_t t; const uint8_t *v; int64_t n;
    if (!xs_mg_find(doc, dlen, key, &t, &v, &n) || t != 0x02) return false;
    snprintf(out, cap, "%.*s", (int)(n - 5), (const char *)v + 4);
    return true;
}

/* ---------- BSON -> JSON text ---------- */
static void xs_mg_jesc(xs_sb *sb, const char *s, size_t n) {
    static const char hx[] = "0123456789abcdef";
    xs_sb_put(sb, "\"", 1);
    size_t run = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c != '"' && c != '\\') continue;
        if (i > run) xs_sb_put(sb, s + run, i - run);
        run = i + 1;
        switch (c) {
        case '"': xs_sb_put(sb, "\\\"", 2); break;
        case '\\': xs_sb_put(sb, "\\\\", 2); break;
        case '\n': xs_sb_put(sb, "\\n", 2); break;
        case '\r': xs_sb_put(sb, "\\r", 2); break;
        case '\t': xs_sb_put(sb, "\\t", 2); break;
        case '\b': xs_sb_put(sb, "\\b", 2); break;
        case '\f': xs_sb_put(sb, "\\f", 2); break;
        default: { char u[6] = {'\\', 'u', '0', '0', hx[c >> 4], hx[c & 15]}; xs_sb_put(sb, u, 6); }
        }
    }
    if (n > run) xs_sb_put(sb, s + run, n - run);
    xs_sb_put(sb, "\"", 1);
}
/* JavaScript's toISOString of a time in ms */
static void xs_mg_iso(xs_sb *sb, int64_t ms) {
    int64_t days = ms >= 0 ? ms / 86400000 : -((-ms + 86399999) / 86400000);
    int64_t rem = ms - days * 86400000;
    /* civil from days (H. Hinnant) */
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    char out[48];
    int n;
    if (y >= 0 && y <= 9999) n = snprintf(out, sizeof out, "\"%04lld", (long long)y);
    else n = snprintf(out, sizeof out, "\"%c%06lld", y < 0 ? '-' : '+', (long long)(y < 0 ? -y : y));
    n += snprintf(out + n, sizeof out - (size_t)n, "-%02lld-%02lldT%02lld:%02lld:%02lld.%03lldZ\"", (long long)m, (long long)d, (long long)(rem / 3600000),
                  (long long)(rem / 60000 % 60), (long long)(rem / 1000 % 60), (long long)(rem % 1000));
    xs_sb_put(sb, out, (size_t)n);
}
/* decimal128 (IEEE 754-2008 BID) as the string the bson package prints */
static void xs_mg_dec128(xs_sb *sb, const uint8_t *p) {
    uint64_t lo = (uint64_t)xs_mg_get64(p), hi = (uint64_t)xs_mg_get64(p + 8);
    bool neg = hi >> 63;
    char out[80];
    int n = 0;
    if (neg) out[n++] = '-';
    uint64_t comb = (hi >> 58) & 0x1F;
    if (comb == 0x1F || comb == 0x1E) {
        if (comb == 0x1F) snprintf(out, sizeof out, "NaN"); else snprintf(out + n, sizeof out - (size_t)n, "Infinity");
        xs_sb_put(sb, "{\"$numberDecimal\":\"", 19);
        xs_sb_put(sb, out, strlen(out));
        xs_sb_put(sb, "\"}", 2);
        return;
    }
    int exp;
    unsigned __int128 coef;
    if (((hi >> 61) & 3) == 3) { exp = (int)((hi >> 47) & 0x3FFF) - 6176; coef = 0; } /* non-canonical: zero */
    else { exp = (int)((hi >> 49) & 0x3FFF) - 6176; coef = ((unsigned __int128)(hi & 0x1FFFFFFFFFFFFULL) << 64) | lo; }
    char digits[40];
    int nd = 0;
    if (coef == 0) digits[nd++] = '0';
    while (coef) { digits[nd++] = (char)('0' + (int)(coef % 10)); coef /= 10; }
    for (int i = 0; i < nd / 2; i++) { char t = digits[i]; digits[i] = digits[nd - 1 - i]; digits[nd - 1 - i] = t; }
    int adj = exp + nd - 1;
    if (exp <= 0 && adj >= -6) { /* plain notation */
        if (exp == 0) { memcpy(out + n, digits, (size_t)nd); n += nd; }
        else {
            int point = nd + exp;
            if (point <= 0) { out[n++] = '0'; out[n++] = '.'; for (int i = 0; i < -point; i++) out[n++] = '0'; memcpy(out + n, digits, (size_t)nd); n += nd; }
            else { memcpy(out + n, digits, (size_t)point); n += point; out[n++] = '.'; memcpy(out + n, digits + point, (size_t)(nd - point)); n += nd - point; }
        }
    } else { /* scientific */
        out[n++] = digits[0];
        if (nd > 1) { out[n++] = '.'; memcpy(out + n, digits + 1, (size_t)(nd - 1)); n += nd - 1; }
        n += snprintf(out + n, sizeof out - (size_t)n, "E%+d", adj);
    }
    out[n] = 0;
    xs_sb_put(sb, "{\"$numberDecimal\":", 18);
    xs_str *s = xs_str_new(out, (size_t)n);
    xs_sb_str(sb, s);
    xs_release(s);
    xs_sb_put(sb, "}", 1);
}
static void xs_mg_json_doc(xs_sb *sb, const uint8_t *doc, int64_t dlen, bool array, bool top);
static void xs_mg_json_val(xs_sb *sb, uint8_t t, const uint8_t *v, int64_t n) {
    static const char hx[] = "0123456789abcdef";
    switch (t) {
    case 0x01: {
        double d;
        memcpy(&d, v, 8);
        if (!isfinite(d)) { xs_sb_put(sb, "null", 4); break; }
        char buf[32];
        if (d == 0) { buf[0] = '0'; buf[1] = 0; } else xs_num_to_str(d, buf);
        xs_sb_put(sb, buf, strlen(buf));
        break;
    }
    case 0x02: case 0x0D: case 0x0E: xs_mg_jesc(sb, (const char *)v + 4, (size_t)(n - 5)); break;
    case 0x03: xs_mg_json_doc(sb, v, n, false, false); break;
    case 0x04: xs_mg_json_doc(sb, v, n, true, false); break;
    case 0x05: {
        int32_t len = xs_mg_get32(v);
        xs_sb_put(sb, "{\"type\":\"Buffer\",\"data\":[", 25);
        for (int32_t i = 0; i < len; i++) { char x[8]; int k = snprintf(x, sizeof x, i ? ",%u" : "%u", v[5 + i]); xs_sb_put(sb, x, (size_t)k); }
        xs_sb_put(sb, "]}", 2);
        break;
    }
    case 0x07: { char h[26]; h[0] = '"'; for (int i = 0; i < 12; i++) { h[1 + 2 * i] = hx[v[i] >> 4]; h[2 + 2 * i] = hx[v[i] & 15]; } h[25] = '"'; xs_sb_put(sb, h, 26); break; }
    case 0x08: xs_sb_put(sb, v[0] ? "true" : "false", v[0] ? 4 : 5); break;
    case 0x09: xs_mg_iso(sb, xs_mg_get64(v)); break;
    case 0x0B: {
        size_t sl = strlen((const char *)v);
        xs_sb_put(sb, "{\"$regex\":", 10);
        xs_mg_jesc(sb, (const char *)v, sl);
        xs_sb_put(sb, ",\"$options\":", 12);
        xs_mg_jesc(sb, (const char *)v + sl + 1, strlen((const char *)v + sl + 1));
        xs_sb_put(sb, "}", 1);
        break;
    }
    case 0x10: { char x[16]; int k = snprintf(x, sizeof x, "%d", xs_mg_get32(v)); xs_sb_put(sb, x, (size_t)k); break; }
    case 0x11: { char x[64]; int k = snprintf(x, sizeof x, "{\"t\":%u,\"i\":%u}", (uint32_t)xs_mg_get32(v + 4), (uint32_t)xs_mg_get32(v)); xs_sb_put(sb, x, (size_t)k); break; }
    case 0x12: { char x[24]; int k = snprintf(x, sizeof x, "%lld", (long long)xs_mg_get64(v)); xs_sb_put(sb, x, (size_t)k); break; }
    case 0x13: xs_mg_dec128(sb, v); break;
    case 0xFF: xs_sb_put(sb, "{\"$minKey\":1}", 13); break;
    case 0x7F: xs_sb_put(sb, "{\"$maxKey\":1}", 13); break;
    default: xs_sb_put(sb, "null", 4); break; /* undefined, null, DBPointer, code with scope */
    }
}
/* top: the reply itself, whose cluster-time fields (gossip for sessions) are left out */
static void xs_mg_json_doc(xs_sb *sb, const uint8_t *doc, int64_t dlen, bool array, bool top) {
    xs_sb_put(sb, array ? "[" : "{", 1);
    const uint8_t *p = doc + 4, *end = doc + dlen - 1;
    bool first = true;
    while (p < end) {
        uint8_t t = *p++;
        const char *k = (const char *)p;
        size_t kl = strnlen(k, (size_t)(end - p));
        p += kl + 1;
        int64_t n = xs_mg_vsize(t, p, end);
        if (n < 0) break;
        if (top && (strcmp(k, "$clusterTime") == 0 || strcmp(k, "operationTime") == 0)) { p += n; continue; }
        if (!first) xs_sb_put(sb, ",", 1);
        first = false;
        if (!array) { xs_mg_jesc(sb, k, kl); xs_sb_put(sb, ":", 1); }
        xs_mg_json_val(sb, t, p, n);
        p += n;
    }
    xs_sb_put(sb, array ? "]" : "}", 1);
}

/* ---------- connections ---------- */
typedef struct xs_mg_conn {
    int fd;
    int id;
    bool broken;
    struct xs_mg_conn *next;
} xs_mg_conn;
typedef struct xs_mg_waiter { xs_coro *co; xs_mg_conn *got; struct xs_mg_waiter *next; } xs_mg_waiter;
#define XS_MG_MAX_CLIENTS 32
typedef struct { xs_mg_conn *idle; int open; xs_mg_waiter *head, *tail; } xs_mg_local;
typedef struct xn_mongodb_NativeClient {
    xs_hdr hdr;
    int nhosts;
    char **hosts;
    int *ports;
    int primary;            /* the host that answered as a writable primary (or the only one) */
    char *user, *password, *source, *app_name;
    int64_t connect_ms, select_ms, socket_ms;
    int max, id;
    bool direct;
    char *replica_set;
    int max_wire;
    char last_err[300];
} xn_mongodb_NativeClient;
static XS_TLS xs_mg_local xs_mg_locals[XS_MG_MAX_CLIENTS];
static int xs_mg_nclients;
static int xs_mg_conn_seq;

static void xn_mongodb_NativeClient_free(void *ptr) {
    xn_mongodb_NativeClient *c = ptr;
    for (int i = 0; i < c->nhosts; i++) free(c->hosts[i]);
    free(c->hosts); free(c->ports); free(c->user); free(c->source); free(c->app_name); free(c->replica_set);
    if (c->password) { memset(c->password, 0, strlen(c->password)); free(c->password); }
    xs_obj_free(ptr);
}
static void xn_mongodb_NativeClient_print(void *p) { (void)p; fputs("MongoClient {}", stdout); }
static char *xs_mg_cfg_str(xs_jv *cfg, const char *key) {
    xs_jv *v = cfg ? xs_json_field_opt(cfg, key) : NULL;
    return v && v->kind == XJ_STR ? xs_strndup_(v->str, v->slen) : NULL;
}
static int64_t xs_mg_cfg_int(xs_jv *cfg, const char *key, int64_t dflt) {
    xs_jv *v = cfg ? xs_json_field_opt(cfg, key) : NULL;
    return v && v->kind == XJ_NUM ? (int64_t)v->num : dflt;
}
static int xs_mg_workers(void) {
    const char *w = getenv("XISTO_WORKERS");
    int n = w ? atoi(w) : 1;
    return n < 1 ? 1 : n;
}
/* new NativeClient({ hosts: [{ host, port }], user, password, source, appName, connectTimeoutMS,
   serverSelectionTimeoutMS, socketTimeoutMS, maxPoolSize, directConnection, replicaSet }) */
XS_NOINLINE xn_mongodb_NativeClient *xn_mongodb_NativeClient_new(xs_str *config) {
    xn_mongodb_NativeClient *c = xs_obj_alloc(sizeof *c, xn_mongodb_NativeClient_free, xn_mongodb_NativeClient_print);
    xs_jarena_reset();
    xs_jv *cfg = xs_json_parse(config);
    xs_jv *hs = cfg ? xs_json_field_opt(cfg, "hosts") : NULL;
    int n = hs && hs->kind == XJ_ARR ? (int)hs->n : 0;
    c->hosts = calloc((size_t)(n ? n : 1), sizeof(char *));
    c->ports = calloc((size_t)(n ? n : 1), sizeof(int));
    for (int i = 0; i < n; i++) {
        c->hosts[i] = xs_mg_cfg_str(hs->items[i], "host");
        if (!c->hosts[i]) c->hosts[i] = strdup("localhost");
        c->ports[i] = (int)xs_mg_cfg_int(hs->items[i], "port", 27017);
    }
    if (n == 0) { c->hosts[0] = strdup("localhost"); c->ports[0] = 27017; n = 1; }
    c->nhosts = n;
    c->user = xs_mg_cfg_str(cfg, "user");
    c->password = xs_mg_cfg_str(cfg, "password");
    c->source = xs_mg_cfg_str(cfg, "source");
    if (!c->source) c->source = strdup("admin");
    c->app_name = xs_mg_cfg_str(cfg, "appName");
    c->replica_set = xs_mg_cfg_str(cfg, "replicaSet");
    c->connect_ms = xs_mg_cfg_int(cfg, "connectTimeoutMS", 30000);
    c->select_ms = xs_mg_cfg_int(cfg, "serverSelectionTimeoutMS", 30000);
    c->socket_ms = xs_mg_cfg_int(cfg, "socketTimeoutMS", 0);
    xs_jv *dc = cfg ? xs_json_field_opt(cfg, "directConnection") : NULL;
    c->direct = dc && dc->kind == XJ_BOOL && dc->b;
    int per = (int)xs_mg_cfg_int(cfg, "maxPoolSize", 100);
    xs_jv_free(cfg);
    if (per < 1) per = 100;
    c->max = (per + xs_mg_workers() - 1) / xs_mg_workers();
    if (c->max < 1) c->max = 1;
    c->primary = -1;
    c->id = __atomic_fetch_add(&xs_mg_nclients, 1, __ATOMIC_RELAXED);
    if (c->id >= XS_MG_MAX_CLIENTS) xs_panic("mongodb: too many clients (32)");
    return c;
}

/* "Network|..." / "ServerSelection|..." / "Server|code|codeName|..." : the kind of error, read by index.ts */
__attribute__((noreturn)) static void xs_mg_throw(const char *kind, const char *msg) {
    static XS_TLS char buf[600];
    snprintf(buf, sizeof buf, "%s|%s", kind, msg);
    xs_throw(xs_Error_from(buf));
    __builtin_unreachable();
}

/* a TCP connection within the connect timeout; err gets Node's words ("connect ECONNREFUSED 127.0.0.1:27017") */
static int xs_mg_dial(const char *host, int port, int64_t timeout_ms, char *err, size_t errcap) {
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char ports[16];
    snprintf(ports, sizeof ports, "%d", port);
    int g = getaddrinfo(host, ports, &hints, &res);
    if (g != 0) { snprintf(err, errcap, "getaddrinfo ENOTFOUND %s", host); return -1; }
    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        fcntl(fd, F_SETFL, O_NONBLOCK);
        char addr[64] = "";
        if (ai->ai_family == AF_INET) inet_ntop(AF_INET, &((struct sockaddr_in *)ai->ai_addr)->sin_addr, addr, sizeof addr);
        else if (ai->ai_family == AF_INET6) inet_ntop(AF_INET6, &((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr, addr, sizeof addr);
        int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
        int e = r == 0 ? 0 : errno;
        if (r != 0 && errno == EINPROGRESS) {
            if (!xs_wait_fd_ms(fd, EVFILT_WRITE, timeout_ms > 0 ? timeout_ms : -1)) e = ETIMEDOUT;
            else { socklen_t len = sizeof e; e = 0; getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &len); }
        }
        if (e == 0) break;
        if (e == ETIMEDOUT) snprintf(err, errcap, "connection timed out");
        else snprintf(err, errcap, "connect %s %s:%d", e == ECONNREFUSED ? "ECONNREFUSED" : e == EHOSTUNREACH ? "EHOSTUNREACH" : e == ENETUNREACH ? "ENETUNREACH" : strerror(e), addr, port);
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static void xs_mg_close_conn(xs_mg_conn *k) {
    if (k->fd >= 0) close(k->fd);
    free(k);
}
static bool xs_mg_send(xs_mg_conn *k, const uint8_t *p, size_t n) {
    while (n > 0) {
#ifdef MSG_NOSIGNAL
        ssize_t w = send(k->fd, p, n, MSG_NOSIGNAL);
#else
        ssize_t w = write(k->fd, p, n);
#endif
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { xs_wait_fd(k->fd, EVFILT_WRITE); continue; }
            k->broken = true;
            return false;
        }
        p += w;
        n -= (size_t)w;
    }
    return true;
}
static bool xs_mg_read_n(xs_mg_conn *k, uint8_t *p, size_t n, int64_t timeout_ms, bool *timed_out) {
    while (n > 0) {
        ssize_t r = read(k->fd, p, n);
        if (r > 0) { p += r; n -= (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && errno == EAGAIN) {
            if (!xs_wait_fd_ms(k->fd, EVFILT_READ, timeout_ms > 0 ? timeout_ms : -1)) { *timed_out = true; k->broken = true; return false; }
            continue;
        }
        k->broken = true;
        return false;
    }
    return true;
}
static int32_t xs_mg_reqid;
/* one OP_MSG round trip: the command (a BSON document) out, the reply's section-0 document back (malloc'd) */
static uint8_t *xs_mg_roundtrip(xs_mg_conn *k, const uint8_t *cmd, size_t clen, int64_t timeout_ms, size_t *rlen, char *err, size_t errcap,
                                const char *host, int port) {
    xs_mg_buf m = {0};
    int32_t rid = __atomic_add_fetch(&xs_mg_reqid, 1, __ATOMIC_RELAXED);
    xs_mg_i32(&m, 0);
    xs_mg_i32(&m, rid);
    xs_mg_i32(&m, 0);
    xs_mg_i32(&m, 2013); /* OP_MSG */
    xs_mg_i32(&m, 0);    /* flagBits */
    xs_mg_u8(&m, 0);     /* section kind 0: the body */
    xs_mg_put(&m, cmd, clen);
    xs_mg_set32(&m, 0, (int32_t)m.len);
    bool ok = xs_mg_send(k, m.p, m.len);
    free(m.p);
    if (!ok) { snprintf(err, errcap, "connection %d to %s:%d closed", k->id, host, port); return NULL; }
    uint8_t hdr[16];
    bool to = false;
    for (;;) {
        if (!xs_mg_read_n(k, hdr, 16, timeout_ms, &to)) break;
        int32_t len = xs_mg_get32(hdr), op = xs_mg_get32(hdr + 12);
        if (len < 21 || len > 64 * 1024 * 1024) { k->broken = true; break; }
        uint8_t *body = malloc((size_t)len - 16);
        if (!body) xs_panic("out of memory");
        if (!xs_mg_read_n(k, body, (size_t)len - 16, timeout_ms, &to)) { free(body); break; }
        if (xs_mg_get32(hdr + 8) != rid) { free(body); continue; } /* (not ours: a reply to a request we gave up on) */
        if (op != 2013) { free(body); k->broken = true; snprintf(err, errcap, "mongodb: unexpected reply opcode %d", op); return NULL; }
        uint32_t flags = (uint32_t)xs_mg_get32(body);
        size_t end = (size_t)len - 16 - ((flags & 1) ? 4 : 0);
        size_t p = 4;
        while (p < end) { /* the kind-0 section; kind-1 sequences are not used by replies to these commands */
            uint8_t kind = body[p++];
            if (p + 4 > end) break;
            int32_t sz = xs_mg_get32(body + p);
            if (sz < 5 || p + (size_t)sz > end) break;
            if (kind == 0) {
                uint8_t *doc = malloc((size_t)sz);
                memcpy(doc, body + p, (size_t)sz);
                free(body);
                *rlen = (size_t)sz;
                return doc;
            }
            if (kind == 1) p += (size_t)sz; else break;
        }
        free(body);
        k->broken = true;
        snprintf(err, errcap, "mongodb: malformed reply");
        return NULL;
    }
    if (to) snprintf(err, errcap, "connection %d to %s:%d timed out", k->id, host, port);
    else snprintf(err, errcap, "connection %d to %s:%d closed", k->id, host, port);
    return NULL;
}

/* SCRAM-SHA-256 (RFC 7677) over saslStart/saslContinue */
static bool xs_mg_scram(xn_mongodb_NativeClient *c, xs_mg_conn *k, const char *host, int port, char *err, size_t errcap, int *code) {
    uint8_t nonce_raw[24];
    char nonce[48];
    xs_random_bytes(nonce_raw, sizeof nonce_raw);
    xs_b64_encode(nonce_raw, sizeof nonce_raw, nonce, false);
    /* the user name with '=' and ',' escaped */
    char uname[512];
    size_t un = 0;
    for (const char *s = c->user; *s && un + 4 < sizeof uname; s++) {
        if (*s == '=') { memcpy(uname + un, "=3D", 3); un += 3; }
        else if (*s == ',') { memcpy(uname + un, "=2C", 3); un += 3; }
        else uname[un++] = *s;
    }
    uname[un] = 0;
    char first_bare[640];
    int fbl = snprintf(first_bare, sizeof first_bare, "n=%s,r=%s", uname, nonce);
    char first[660];
    int fl = snprintf(first, sizeof first, "n,,%s", first_bare);
    xs_mg_buf b = {0};
    size_t at = xs_mg_doc_begin(&b);
    xs_mg_i32_el(&b, "saslStart", 1);
    xs_mg_str_el(&b, "mechanism", "SCRAM-SHA-256", 13);
    xs_mg_bin_el(&b, "payload", first, (size_t)fl);
    xs_mg_i32_el(&b, "autoAuthorize", 1);
    xs_mg_key(&b, 0x03, "options", 7);
    { size_t o = xs_mg_doc_begin(&b); xs_mg_key(&b, 0x08, "skipEmptyExchange", 17); xs_mg_u8(&b, 1); xs_mg_doc_end(&b, o); }
    xs_mg_str_el(&b, "$db", c->source, strlen(c->source));
    xs_mg_doc_end(&b, at);
    size_t rl = 0;
    uint8_t *rep = xs_mg_roundtrip(k, b.p, b.len, c->connect_ms, &rl, err, errcap, host, port);
    free(b.p);
    b = (xs_mg_buf){0};
    if (!rep) return false;
    if (xs_mg_num(rep, rl, "ok", 0) != 1) {
        *code = (int)xs_mg_num(rep, rl, "code", 18);
        if (!xs_mg_str(rep, rl, "errmsg", err, errcap)) snprintf(err, errcap, "Authentication failed.");
        free(rep);
        return false;
    }
    uint8_t t; const uint8_t *v; int64_t vn;
    char server_first[512] = "";
    if (xs_mg_find(rep, rl, "payload", &t, &v, &vn) && t == 0x05) snprintf(server_first, sizeof server_first, "%.*s", (int)xs_mg_get32(v), (const char *)v + 5);
    uint8_t conv_type = 0x10;
    int64_t conv = 1;
    if (xs_mg_find(rep, rl, "conversationId", &t, &v, &vn)) { conv_type = t; conv = t == 0x12 ? xs_mg_get64(v) : t == 0x10 ? xs_mg_get32(v) : 1; }
    free(rep);
    char *r = strstr(server_first, "r="), *s = strstr(server_first, ",s="), *i = strstr(server_first, ",i=");
    if (!r || !s || !i || strncmp(r + 2, nonce, strlen(nonce)) != 0) { snprintf(err, errcap, "Invalid SCRAM server response"); return false; }
    char full_nonce[256], salt64[256];
    snprintf(full_nonce, sizeof full_nonce, "%.*s", (int)(s - r - 2), r + 2);
    snprintf(salt64, sizeof salt64, "%.*s", (int)(i - s - 3), s + 3);
    long iters = atol(i + 3);
    uint8_t salt[192];
    long slen = xs_b64_decode(salt64, strlen(salt64), salt);
    if (slen <= 0 || iters < 4096 || iters > 10000000) { snprintf(err, errcap, "Invalid SCRAM server response"); return false; }
    uint8_t salted[32], client_key[32], stored_key[32], server_key[32], sig[32], proof[32];
    xs_pbkdf2_256(c->password ? c->password : "", c->password ? strlen(c->password) : 0, salt, (size_t)slen, (uint32_t)iters, salted);
    xs_hmac256(salted, 32, "Client Key", 10, client_key);
    xs_sha256_hash(client_key, 32, stored_key);
    char final_bare[320], auth_msg[1400];
    snprintf(final_bare, sizeof final_bare, "c=biws,r=%s", full_nonce);
    int alen = snprintf(auth_msg, sizeof auth_msg, "%.*s,%s,%s", fbl, first_bare, server_first, final_bare);
    xs_hmac256(stored_key, 32, auth_msg, (size_t)alen, sig);
    for (int q = 0; q < 32; q++) proof[q] = client_key[q] ^ sig[q];
    char proof64[64], final[400];
    xs_b64_encode(proof, 32, proof64, false);
    int fnl = snprintf(final, sizeof final, "%s,p=%s", final_bare, proof64);
    for (int round = 0; round < 2; round++) {
        at = xs_mg_doc_begin(&b);
        xs_mg_i32_el(&b, "saslContinue", 1);
        xs_mg_key(&b, conv_type == 0x12 ? 0x12 : 0x10, "conversationId", 14);
        if (conv_type == 0x12) xs_mg_i64(&b, conv); else xs_mg_i32(&b, (int32_t)conv);
        xs_mg_bin_el(&b, "payload", round == 0 ? final : "", round == 0 ? (size_t)fnl : 0);
        xs_mg_str_el(&b, "$db", c->source, strlen(c->source));
        xs_mg_doc_end(&b, at);
        rep = xs_mg_roundtrip(k, b.p, b.len, c->connect_ms, &rl, err, errcap, host, port);
        free(b.p);
        b = (xs_mg_buf){0};
        if (!rep) return false;
        if (xs_mg_num(rep, rl, "ok", 0) != 1) {
            *code = (int)xs_mg_num(rep, rl, "code", 18);
            if (!xs_mg_str(rep, rl, "errmsg", err, errcap)) snprintf(err, errcap, "Authentication failed.");
            free(rep);
            return false;
        }
        if (round == 0) { /* the server proves it knows the password too: v=<ServerSignature> */
            char sfinal[256] = "";
            if (xs_mg_find(rep, rl, "payload", &t, &v, &vn) && t == 0x05) snprintf(sfinal, sizeof sfinal, "%.*s", (int)xs_mg_get32(v), (const char *)v + 5);
            xs_hmac256(salted, 32, "Server Key", 10, server_key);
            xs_hmac256(server_key, 32, auth_msg, (size_t)alen, sig);
            char want[64];
            xs_b64_encode(sig, 32, want, false);
            if (strncmp(sfinal, "v=", 2) != 0 || strcmp(sfinal + 2, want) != 0) {
                free(rep);
                snprintf(err, errcap, "Server returned an invalid signature");
                return false;
            }
        }
        bool done = xs_mg_num(rep, rl, "done", 0) != 0;
        free(rep);
        if (done) break;
    }
    memset(salted, 0, sizeof salted);
    memset(client_key, 0, sizeof client_key);
    return true;
}

static const char *xs_mg_os_name(void) {
#if defined(__APPLE__)
    return "darwin";
#elif defined(_WIN32)
    return "win32";
#else
    return "linux";
#endif
}
/* hello: the handshake (client metadata, the server's limits); fills *writable and *primary_hint */
static bool xs_mg_hello(xn_mongodb_NativeClient *c, xs_mg_conn *k, const char *host, int port, bool *writable, char *hint, size_t hintcap,
                        char *err, size_t errcap, xs_str **hello_json) {
    xs_mg_buf b = {0};
    size_t at = xs_mg_doc_begin(&b);
    xs_mg_i32_el(&b, "hello", 1);
    xs_mg_key(&b, 0x08, "helloOk", 7); xs_mg_u8(&b, 1);
    xs_mg_key(&b, 0x03, "client", 6);
    size_t cl = xs_mg_doc_begin(&b);
    xs_mg_key(&b, 0x03, "driver", 6);
    { size_t d = xs_mg_doc_begin(&b); xs_mg_str_el(&b, "name", "nodejs|Mongoose", 15); xs_mg_str_el(&b, "version", "6.20.0|8.24.8", 13); xs_mg_doc_end(&b, d); }
    struct utsname u;
    bool have_u = uname(&u) == 0;
    xs_mg_key(&b, 0x03, "os", 2);
    {
        size_t d = xs_mg_doc_begin(&b);
        xs_mg_str_el(&b, "type", have_u ? u.sysname : "unknown", strlen(have_u ? u.sysname : "unknown"));
        xs_mg_str_el(&b, "name", xs_mg_os_name(), strlen(xs_mg_os_name()));
        if (have_u) { xs_mg_str_el(&b, "architecture", u.machine, strlen(u.machine)); xs_mg_str_el(&b, "version", u.release, strlen(u.release)); }
        xs_mg_doc_end(&b, d);
    }
    xs_mg_str_el(&b, "platform", "xisto", 5);
    if (c->app_name) { xs_mg_key(&b, 0x03, "application", 11); size_t d = xs_mg_doc_begin(&b); xs_mg_str_el(&b, "name", c->app_name, strlen(c->app_name)); xs_mg_doc_end(&b, d); }
    xs_mg_doc_end(&b, cl);
    xs_mg_key(&b, 0x04, "compression", 11); { size_t d = xs_mg_doc_begin(&b); xs_mg_doc_end(&b, d); }
    if (c->user) {
        char mech[600];
        snprintf(mech, sizeof mech, "%s.%s", c->source, c->user);
        xs_mg_str_el(&b, "saslSupportedMechs", mech, strlen(mech));
    }
    xs_mg_str_el(&b, "$db", "admin", 5);
    xs_mg_doc_end(&b, at);
    size_t rl = 0;
    uint8_t *rep = xs_mg_roundtrip(k, b.p, b.len, c->connect_ms, &rl, err, errcap, host, port);
    free(b.p);
    if (!rep) return false;
    if (xs_mg_num(rep, rl, "ok", 0) != 1) {
        if (!xs_mg_str(rep, rl, "errmsg", err, errcap)) snprintf(err, errcap, "hello failed");
        free(rep);
        return false;
    }
    int maxw = (int)xs_mg_num(rep, rl, "maxWireVersion", 0);
    if (maxw < 6) {
        snprintf(err, errcap, "Server at %s:%d reports maximum wire version %d, but this version of the Node.js Driver requires at least 6 (MongoDB 3.6)", host, port, maxw);
        free(rep);
        return false;
    }
    c->max_wire = maxw;
    *writable = xs_mg_num(rep, rl, "isWritablePrimary", xs_mg_num(rep, rl, "ismaster", 0)) != 0;
    hint[0] = 0;
    xs_mg_str(rep, rl, "primary", hint, hintcap);
    if (hello_json) {
        xs_sb sb = {0};
        xs_mg_json_doc(&sb, rep, (int64_t)rl, false, true);
        *hello_json = xs_sb_finish(&sb);
    }
    free(rep);
    return true;
}

/* opens a connection to host i (hello, then the login); NULL with err set. auth_code: the server's code when the
   login was refused (the error is then the server's, not a network one) */
static xs_mg_conn *xs_mg_open_host(xn_mongodb_NativeClient *c, int i, bool *writable, char *hint, size_t hintcap, char *err, size_t errcap,
                                   int *auth_code, xs_str **hello_json) {
    xs_mg_conn *k = calloc(1, sizeof *k);
    if (!k) xs_panic("out of memory");
    k->id = __atomic_add_fetch(&xs_mg_conn_seq, 1, __ATOMIC_RELAXED);
    k->fd = xs_mg_dial(c->hosts[i], c->ports[i], c->connect_ms, err, errcap);
    if (k->fd < 0) { free(k); return NULL; }
    if (!xs_mg_hello(c, k, c->hosts[i], c->ports[i], writable, hint, hintcap, err, errcap, hello_json)) { xs_mg_close_conn(k); return NULL; }
    if (c->user && !xs_mg_scram(c, k, c->hosts[i], c->ports[i], err, errcap, auth_code)) {
        if (!*auth_code && strstr(err, "signature")) *auth_code = 18;
        xs_mg_close_conn(k);
        return NULL;
    }
    return k;
}
/* server selection: the primary (or the single host) within serverSelectionTimeoutMS. NULL on failure, with
   fail = "ServerSelection|<the last error>" or "Server|<code>|<codeName>|<message>" (a refused login is final) */
static xs_mg_conn *xs_mg_open(xn_mongodb_NativeClient *c, xs_str **hello_json, char *fail, size_t failcap) {
    int64_t deadline = (int64_t)xs_date_now() + (c->select_ms > 0 ? c->select_ms : 30000);
    char err[300] = "";
    for (;;) {
        int order[64];
        int n = 0;
        int pr = __atomic_load_n(&c->primary, __ATOMIC_RELAXED);
        if (pr >= 0 && pr < c->nhosts) order[n++] = pr;
        for (int i = 0; i < c->nhosts && n < 64; i++) if (i != pr) order[n++] = i;
        for (int j = 0; j < n; j++) {
            int i = order[j];
            bool writable = false;
            char hint[300];
            int auth_code = 0;
            err[0] = 0;
            xs_mg_conn *k = xs_mg_open_host(c, i, &writable, hint, sizeof hint, err, sizeof err, &auth_code, hello_json);
            if (!k && auth_code) {
                snprintf(fail, failcap, "Server|%d|%s|%s", auth_code, auth_code == 18 ? "AuthenticationFailed" : "", err);
                return NULL;
            }
            if (!k) { snprintf(c->last_err, sizeof c->last_err, "%s", err); continue; }
            if (writable || c->nhosts == 1 || c->direct) { __atomic_store_n(&c->primary, i, __ATOMIC_RELAXED); return k; }
            /* a secondary: go to the primary it names (added to the hosts when it is not one of them) */
            if (hello_json && *hello_json) { xs_release(*hello_json); *hello_json = NULL; }
            xs_mg_close_conn(k);
            snprintf(c->last_err, sizeof c->last_err, "Server selection timed out after %lld ms", (long long)c->select_ms);
            char *colon = strrchr(hint, ':');
            if (hint[0] && colon) {
                *colon = 0;
                int port = atoi(colon + 1);
                bool known = false;
                for (int q = 0; q < c->nhosts; q++) if (c->ports[q] == port && strcmp(c->hosts[q], hint) == 0) { known = true; __atomic_store_n(&c->primary, q, __ATOMIC_RELAXED); }
                if (!known && c->nhosts < 64) {
                    c->hosts = realloc(c->hosts, sizeof(char *) * (size_t)(c->nhosts + 1));
                    c->ports = realloc(c->ports, sizeof(int) * (size_t)(c->nhosts + 1));
                    c->hosts[c->nhosts] = strdup(hint);
                    c->ports[c->nhosts] = port;
                    __atomic_store_n(&c->primary, c->nhosts, __ATOMIC_RELAXED);
                    c->nhosts++;
                }
                break;
            }
        }
        int64_t now = (int64_t)xs_date_now();
        if (now >= deadline) break;
        int64_t wait = deadline - now < 500 ? deadline - now : 500; /* the driver checks a failed server again after 500 ms */
        xs_sleep_ms(wait);
    }
    snprintf(fail, failcap, "ServerSelection|%s", c->last_err[0] ? c->last_err : "Server selection timed out");
    return NULL;
}

__attribute__((noreturn)) static void xs_mg_throw_raw(const char *msg) {
    static XS_TLS char buf[600];
    snprintf(buf, sizeof buf, "%s", msg);
    xs_throw(xs_Error_from(buf));
    __builtin_unreachable();
}

static xs_mg_conn *xs_mg_acquire(xn_mongodb_NativeClient *c) {
    xs_mg_local *l = &xs_mg_locals[c->id];
    if (l->idle) { xs_mg_conn *k = l->idle; l->idle = k->next; k->next = NULL; return k; }
    if (l->open < c->max || !xs_cur_coro) {
        l->open++;
        char fail[600];
        xs_mg_conn *k = xs_mg_open(c, NULL, fail, sizeof fail);
        if (!k) {
            l->open--;
            if (l->head) { xs_mg_waiter *w = l->head; l->head = w->next; if (!l->head) l->tail = NULL; w->got = NULL; xs_ready_push(w->co); }
            xs_mg_throw_raw(fail);
        }
        return k;
    }
    xs_mg_waiter *w = calloc(1, sizeof *w);
    w->co = xs_cur_coro;
    if (l->tail) l->tail->next = w; else l->head = w;
    l->tail = w;
    xs_coro_yield(); /* resumed by xs_mg_release with a connection */
    xs_mg_conn *got = w->got;
    free(w);
    if (!got) return xs_mg_acquire(c);
    return got;
}
static void xs_mg_release(xn_mongodb_NativeClient *c, xs_mg_conn *k) {
    xs_mg_local *l = &xs_mg_locals[c->id];
    if (k && k->broken) { xs_mg_close_conn(k); l->open--; k = NULL; }
    if (l->head) {
        xs_mg_waiter *w = l->head;
        l->head = w->next;
        if (!l->head) l->tail = NULL;
        w->got = k; /* (NULL: the waiter opens its own in the free slot) */
        xs_ready_push(w->co);
        return;
    }
    if (k) { k->next = l->idle; l->idle = k; }
}

/* connect(): server selection and a first connection; the hello reply (JSON), the connection kept in the pool */
XS_NOINLINE xs_str *xn_mongodb_NativeClient_connect(xn_mongodb_NativeClient *c) {
    xs_str *hello = NULL;
    char fail[600];
    xs_mg_conn *k = xs_mg_open(c, &hello, fail, sizeof fail);
    if (!k) xs_mg_throw_raw(fail);
    xs_mg_local *l = &xs_mg_locals[c->id];
    l->open++;
    xs_mg_release(c, k);
    return hello ? hello : xs_str_new("{}", 2);
}

/* runs one command; with drain, a cursor reply (find, aggregate) is followed with getMore to its end and the
   result is {"ok":1,"docs":[...]} (any other reply comes back as it is) */
static xs_str *xs_mg_run(xn_mongodb_NativeClient *c, xs_str *db, xs_str *text, bool drain) {
    char err[400] = "";
    xs_mg_buf cmd = {0};
    char dbz[256];
    snprintf(dbz, sizeof dbz, "%.*s", (int)db->len, db->data);
    if (!xs_mg_encode_cmd(&cmd, text->data, (size_t)text->len, dbz, err, sizeof err)) { free(cmd.p); static XS_TLS char m[400]; snprintf(m, sizeof m, "%s", err); xs_throw(xs_Error_from(m)); }
    xs_mg_conn *k = xs_mg_acquire(c);
    int pr = c->primary >= 0 ? c->primary : 0;
    const char *host = c->hosts[pr];
    int port = c->ports[pr];
    size_t rl = 0;
    uint8_t *rep = xs_mg_roundtrip(k, cmd.p, cmd.len, c->socket_ms, &rl, err, sizeof err, host, port);
    free(cmd.p);
    if (!rep) { xs_mg_release(c, k); xs_mg_throw("Network", err); }
    uint8_t t; const uint8_t *v; int64_t vn;
    if (!drain || xs_mg_num(rep, rl, "ok", 0) != 1 || !xs_mg_find(rep, rl, "cursor", &t, &v, &vn) || t != 0x03) {
        xs_mg_release(c, k);
        xs_sb sb = {0};
        xs_mg_json_doc(&sb, rep, (int64_t)rl, false, true);
        free(rep);
        return xs_sb_finish(&sb);
    }
    /* the batches: raw documents back to back */
    xs_mg_buf docs = {0};
    size_t ndocs = 0;
    uint8_t *cur = rep;
    size_t curlen = rl;
    const char *batch = "firstBatch";
    xs_str *fail = NULL;
    for (;;) {
        if (!xs_mg_find(cur, curlen, "cursor", &t, &v, &vn) || t != 0x03) break;
        const uint8_t *cdoc = v;
        size_t cl = (size_t)vn;
        const uint8_t *bv;
        int64_t bn;
        if (xs_mg_find(cdoc, cl, batch, &t, &bv, &bn) && t == 0x04) {
            const uint8_t *p = bv + 4, *end = bv + bn - 1;
            while (p < end) {
                uint8_t et = *p++;
                p += strnlen((const char *)p, (size_t)(end - p)) + 1;
                int64_t en = xs_mg_vsize(et, p, end);
                if (en < 0) break;
                if (et == 0x03) { xs_mg_put(&docs, p, (size_t)en); ndocs++; }
                p += en;
            }
        }
        int64_t id = 0;
        const uint8_t *iv;
        int64_t in;
        if (xs_mg_find(cdoc, cl, "id", &t, &iv, &in)) id = t == 0x12 ? xs_mg_get64(iv) : t == 0x10 ? xs_mg_get32(iv) : 0;
        if (id == 0) break;
        char ns[512] = "";
        xs_mg_str(cdoc, cl, "ns", ns, sizeof ns);
        const char *coll = strchr(ns, '.');
        coll = coll ? coll + 1 : ns;
        xs_mg_buf g = {0};
        size_t at = xs_mg_doc_begin(&g);
        xs_mg_key(&g, 0x12, "getMore", 7);
        xs_mg_i64(&g, id);
        xs_mg_str_el(&g, "collection", coll, strlen(coll));
        xs_mg_str_el(&g, "$db", dbz, strlen(dbz));
        xs_mg_doc_end(&g, at);
        free(cur);
        cur = xs_mg_roundtrip(k, g.p, g.len, c->socket_ms, &curlen, err, sizeof err, host, port);
        free(g.p);
        if (!cur) break;
        if (xs_mg_num(cur, curlen, "ok", 0) != 1) { /* the server's error, as the reply */
            xs_sb sb = {0};
            xs_mg_json_doc(&sb, cur, (int64_t)curlen, false, true);
            fail = xs_sb_finish(&sb);
            break;
        }
        batch = "nextBatch";
    }
    xs_mg_release(c, k);
    if (!cur) { free(docs.p); xs_mg_throw("Network", err); }
    free(cur);
    if (fail) { free(docs.p); return fail; }
    xs_sb sb = {0};
    xs_sb_put(&sb, "{\"ok\":1,\"docs\":[", 16);
    size_t off = 0;
    for (size_t i = 0; i < ndocs; i++) {
        if (i) xs_sb_put(&sb, ",", 1);
        int32_t n = xs_mg_get32(docs.p + off);
        xs_mg_json_doc(&sb, docs.p + off, n, false, false);
        off += (size_t)n;
    }
    xs_sb_put(&sb, "]}", 2);
    free(docs.p);
    return xs_sb_finish(&sb);
}
XS_NOINLINE xs_str *xn_mongodb_NativeClient_command(xn_mongodb_NativeClient *c, xs_str *db, xs_str *text) { return xs_mg_run(c, db, text, false); }
XS_NOINLINE xs_str *xn_mongodb_NativeClient_cursor(xn_mongodb_NativeClient *c, xs_str *db, xs_str *text) { return xs_mg_run(c, db, text, true); }

/* close(): the idle connections of this thread go (the busy ones close when they come back broken or at exit) */
XS_NOINLINE void xn_mongodb_NativeClient_close(xn_mongodb_NativeClient *c) {
    xs_mg_local *l = &xs_mg_locals[c->id];
    while (l->idle) { xs_mg_conn *k = l->idle; l->idle = k->next; xs_mg_close_conn(k); l->open--; }
}

/* the BSON bytes of a command text (lowercase hex), and a BSON document (hex) as JSON text: for the tests */
XS_NOINLINE xs_str *xn_mongodb_NativeClient_encodeHex(xn_mongodb_NativeClient *c, xs_str *text) {
    (void)c;
    char err[200] = "";
    xs_mg_buf b = {0};
    xs_mg_rd r = {text->data, text->data + text->len, {0}};
    xs_mg_ws(&r);
    if (r.s >= r.end || *r.s != '{' || !xs_mg_object(&r, &b, 0)) {
        snprintf(err, sizeof err, "%s", r.err[0] ? r.err : "mongodb: a document must be an object");
        free(b.p);
        static XS_TLS char m[200];
        snprintf(m, sizeof m, "%s", err);
        xs_throw(xs_Error_from(m));
    }
    static const char hx[] = "0123456789abcdef";
    char *h = malloc(b.len * 2 + 1);
    for (size_t i = 0; i < b.len; i++) { h[2 * i] = hx[b.p[i] >> 4]; h[2 * i + 1] = hx[b.p[i] & 15]; }
    xs_str *s = xs_str_new(h, b.len * 2);
    free(h);
    free(b.p);
    return s;
}
XS_NOINLINE xs_str *xn_mongodb_NativeClient_decodeHex(xn_mongodb_NativeClient *c, xs_str *hex) {
    (void)c;
    size_t n = (size_t)hex->len / 2;
    uint8_t *b = malloc(n + 1);
    for (size_t i = 0; i < n; i++) {
        int hi = xs_mg_hex(hex->data[2 * i]), lo = xs_mg_hex(hex->data[2 * i + 1]);
        b[i] = (uint8_t)((hi < 0 ? 0 : hi) << 4 | (lo < 0 ? 0 : lo));
    }
    if (n < 5 || (size_t)xs_mg_get32(b) != n || b[n - 1] != 0) { free(b); xs_throw(xs_Error_from("mongodb: not a BSON document")); }
    xs_sb sb = {0};
    xs_mg_json_doc(&sb, b, (int64_t)n, false, false);
    free(b);
    return xs_sb_finish(&sb);
}
