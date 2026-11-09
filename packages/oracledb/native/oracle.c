/* oracledb (node-oracledb contract), thin mode: an Oracle Database client that speaks Oracle Net (TNS) and the
   Two-Task Common protocol (TTC) over TCP itself, with no Oracle Client libraries.

   Ported from node-oracledb 6.10 in thin mode (lib/thin: sqlnet/packet.js, protocol/packet.js, messages/(all),
   impl/datahandlers/buffer.js), dual-licensed under the Apache License 2.0 and the Universal Permissive License 1.0,
   Copyright (c) 2022, 2025, Oracle and/or its affiliates. The wire layouts, constants and the O5LOGON key exchange
   follow that code; the C is written for xisto (sockets and coroutines of the runtime, results as JSON).

   - Connect: an Easy Connect string (host[:port][/service]) or a full (DESCRIPTION=...) descriptor; TCP only.
   - Login: O5LOGON with the 12c verifier (PBKDF2-SHA512, AES-256-CBC), as 12.2 and later servers require by
     default. With the server's fast authentication (23ai) the protocol, data types and first login phase go in one
     request; otherwise (19c, 21c) as three. XISTO_ORACLE_NO_FAST_AUTH=1 takes the three-request path anyway.
   - Statements: SQL, DML, DDL and PL/SQL blocks; binds positional (an array, :1 :2 or any names in order) or by
     name (an object); rows fetched 100 at a time; CLOB and BLOB columns come whole (as TypeORM asks with
     fetchAsString/fetchAsBuffer): a second request defines them as LONG / LONG RAW.
   - Cursors: each statement's cursor is closed on the connection's next request (a piggyback), as the driver's
     statement cache does when it evicts.
   - Results: {rows, metaData, rowsAffected} as JSON, with node-oracledb's types: NUMBER as a number, VARCHAR2/CHAR
     and CLOB as text, DATE/TIMESTAMP as ISO instants (DATE and TIMESTAMP read in the local time zone, the TZ kinds
     as UTC), RAW and BLOB as lowercase hex text.
   Secrets: the password and the derived keys are wiped after the login and never logged. */

#define XS_ORA_MAX_POOLS 8
#define XS_ORA_FETCH 100

typedef struct xs_ora_conn {
    int fd;
    bool broken;
    uint8_t *in;            /* payload bytes of the data packets read for the current response */
    size_t in_len, in_cap, pos;
    bool last_eor;          /* the last data packet read carried the end-of-request flag */
    bool large_sdu, eor, fast;
    uint32_t sdu;
    uint16_t version;
    int ttcfv;              /* negotiated TTC field version */
    uint32_t max_str;       /* 4000 or 32767 (MAX_STRING_SIZE=EXTENDED) */
    uint8_t seq;
    uint32_t *close_cursors; /* cursors to close on the next request */
    int nclose, close_cap;
    struct xs_ora_conn *next;
} xs_ora_conn;

typedef struct xs_ora_waiter { xs_coro *co; xs_ora_conn *got; struct xs_ora_waiter *next; } xs_ora_waiter;
typedef struct { xs_ora_conn *idle; int open; xs_ora_waiter *head, *tail; xs_ora_conn **held; int nheld, held_cap; } xs_ora_local;
typedef struct xn_oracledb_NativePool {
    xs_hdr hdr;
    char *host, *descriptor, *user, *password;
    int port, max, id;
} xn_oracledb_NativePool;
static XS_TLS xs_ora_local xs_ora_locals[XS_ORA_MAX_POOLS];
static int xs_ora_npools;

/* ---- errors ---- */
XS_NOINLINE void xs_ora_fail(const char *fmt, ...) {
    static XS_TLS char msg[1200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    xs_throw(xs_Error_from(msg));
}

/* ---- writing a request ---- */
typedef struct { uint8_t *p; size_t len, cap; } xs_ora_w;
static void xs_ora_put(xs_ora_w *w, const void *s, size_t n) {
    if (w->len + n > w->cap) {
        w->cap = (w->len + n) * 2 + 256;
        w->p = realloc(w->p, w->cap);
        if (!w->p) xs_panic("out of memory");
    }
    memcpy(w->p + w->len, s, n);
    w->len += n;
}
static void xs_ora_u8(xs_ora_w *w, unsigned v) { uint8_t b = (uint8_t)v; xs_ora_put(w, &b, 1); }
static void xs_ora_u16be(xs_ora_w *w, unsigned v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; xs_ora_put(w, b, 2); }
/* UB2/UB4/UB8: a length byte, then the value big-endian in that many bytes (0 is the single byte 0) */
static void xs_ora_ub(xs_ora_w *w, uint64_t v) {
    if (v == 0) { xs_ora_u8(w, 0); return; }
    int n = v <= 0xff ? 1 : v <= 0xffff ? 2 : v <= 0xffffffffULL ? 4 : 8;
    xs_ora_u8(w, (unsigned)n);
    for (int i = n - 1; i >= 0; i--) xs_ora_u8(w, (unsigned)(v >> (8 * i)) & 0xff);
}
/* bytes with their length: up to 252 after one length byte; longer, 254 then chunks (UB4 length, bytes), then 0 */
static void xs_ora_bwl(xs_ora_w *w, const void *s, size_t n) {
    if (n <= 252) { xs_ora_u8(w, (unsigned)n); if (n) xs_ora_put(w, s, n); return; }
    xs_ora_u8(w, 254);
    const uint8_t *p = s;
    while (n > 0) {
        size_t k = n < 65536 ? n : 65536;
        xs_ora_ub(w, k);
        xs_ora_put(w, p, k);
        p += k; n -= k;
    }
    xs_ora_ub(w, 0);
}
static void xs_ora_kv(xs_ora_w *w, const char *k, const char *v, unsigned flags) {
    size_t kl = strlen(k), vl = strlen(v);
    xs_ora_ub(w, kl); xs_ora_bwl(w, k, kl);
    xs_ora_ub(w, vl); if (vl) xs_ora_bwl(w, v, vl);
    xs_ora_ub(w, flags);
}

/* ---- packets ---- */
static bool xs_ora_read_n(xs_ora_conn *c, uint8_t *p, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(c->fd, p + got, n - got);
        if (r > 0) { got += (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && errno == EAGAIN) { xs_wait_fd(c->fd, EVFILT_READ); continue; }
        c->broken = true;
        return false;
    }
    return true;
}
/* one packet: its type and whole bytes (header included) in *pkt (malloc'd) */
static int xs_ora_recv_packet(xs_ora_conn *c, uint8_t **pkt, size_t *plen) {
    uint8_t h[8];
    if (!xs_ora_read_n(c, h, 8)) return -1;
    size_t n = c->large_sdu ? ((size_t)h[0] << 24 | (size_t)h[1] << 16 | (size_t)h[2] << 8 | h[3]) : ((size_t)h[0] << 8 | h[1]);
    if (n < 8 || n > (64u << 20)) { c->broken = true; return -1; }
    uint8_t *p = malloc(n);
    if (!p) xs_panic("out of memory");
    memcpy(p, h, 8);
    if (!xs_ora_read_n(c, p + 8, n - 8)) { free(p); return -1; }
    *pkt = p;
    *plen = n;
    return h[4];
}
static bool xs_ora_send_raw(xs_ora_conn *c, const uint8_t *p, size_t n) {
    if (!xs_write_all(c->fd, (const char *)p, n)) { c->broken = true; return false; }
    return true;
}
/* a request's payload in data packets of at most the SDU; the last one flags the end of the request (0x0800) */
static bool xs_ora_send(xs_ora_conn *c, const xs_ora_w *w) {
    size_t max = c->sdu > 64 ? c->sdu - 10 : 8182, off = 0;
    uint8_t *pkt = malloc(max + 10);
    if (!pkt) xs_panic("out of memory");
    bool ok = true;
    do {
        size_t k = w->len - off < max ? w->len - off : max;
        bool last = off + k >= w->len;
        size_t n = k + 10;
        if (c->large_sdu) { pkt[0] = (uint8_t)(n >> 24); pkt[1] = (uint8_t)(n >> 16); pkt[2] = (uint8_t)(n >> 8); pkt[3] = (uint8_t)n; }
        else { pkt[0] = (uint8_t)(n >> 8); pkt[1] = (uint8_t)n; pkt[2] = pkt[3] = 0; }
        pkt[4] = 6; pkt[5] = 0; pkt[6] = pkt[7] = 0;
        pkt[8] = last ? 0x08 : 0; pkt[9] = 0;
        memcpy(pkt + 10, w->p + off, k);
        off += k;
        ok = xs_ora_send_raw(c, pkt, n);
    } while (ok && off < w->len);
    free(pkt);
    return ok;
}
static bool xs_ora_marker(xs_ora_conn *c, int data) {
    uint8_t m[11] = {0};
    if (c->large_sdu) { m[3] = 11; } else { m[1] = 11; }
    m[4] = 12; m[8] = 1; m[10] = (uint8_t)data;
    return xs_ora_send_raw(c, m, 11);
}

/* ---- reading a response: the payload of its data packets, pulled in as the reader needs it ---- */
static void xs_ora_start_response(xs_ora_conn *c) { c->in_len = c->pos = 0; c->last_eor = false; }
static bool xs_ora_more(xs_ora_conn *c) {
    for (;;) {
        uint8_t *p;
        size_t n;
        int t = xs_ora_recv_packet(c, &p, &n);
        if (t < 0) return false;
        if (t == 12) { /* marker: the server broke the call (an error follows); answer with a reset, await its reset */
            int data = n >= 11 ? p[10] : 0;
            free(p);
            if (data != 2) {
                if (!xs_ora_marker(c, 2)) return false;
                for (;;) {
                    t = xs_ora_recv_packet(c, &p, &n);
                    if (t < 0) return false;
                    bool reset = t == 12 && n >= 11 && p[10] == 2;
                    if (t != 12) { /* data already: keep it */
                        if (t == 6 && n >= 10) goto data;
                        free(p);
                        continue;
                    }
                    free(p);
                    if (reset) break;
                }
            }
            continue;
        }
        if (t != 6 || n < 10) { free(p); c->broken = true; return false; }
    data:;
        uint16_t flags = (uint16_t)(p[8] << 8 | p[9]);
        if (c->pos > 0 && c->pos == c->in_len) c->pos = c->in_len = 0;
        size_t k = n - 10;
        if (c->in_len + k > c->in_cap) {
            if (c->pos > 0) { memmove(c->in, c->in + c->pos, c->in_len - c->pos); c->in_len -= c->pos; c->pos = 0; }
            if (c->in_len + k > c->in_cap) {
                c->in_cap = (c->in_len + k) * 2 + 8192;
                c->in = realloc(c->in, c->in_cap);
                if (!c->in) xs_panic("out of memory");
            }
        }
        memcpy(c->in + c->in_len, p + 10, k);
        c->in_len += k;
        c->last_eor = (flags & 0x2000) != 0;
        free(p);
        if (k > 0) return true;
    }
}
static void xs_ora_need(xs_ora_conn *c, size_t n) {
    while (c->in_len - c->pos < n)
        if (!xs_ora_more(c)) xs_ora_fail("ORA-03113: end-of-file on communication channel");
}
static unsigned xs_ora_r8(xs_ora_conn *c) { xs_ora_need(c, 1); return c->in[c->pos++]; }
static const uint8_t *xs_ora_rraw(xs_ora_conn *c, size_t n) { xs_ora_need(c, n); const uint8_t *p = c->in + c->pos; c->pos += n; return p; }
static int64_t xs_ora_rub(xs_ora_conn *c) {
    unsigned n = xs_ora_r8(c);
    if (n == 0) return 0;
    bool neg = (n & 0x80) != 0;
    n &= 0x7f;
    if (n > 8) xs_ora_fail("oracledb: bad integer on the wire");
    const uint8_t *p = xs_ora_rraw(c, n);
    uint64_t v = 0;
    for (unsigned i = 0; i < n; i++) v = v << 8 | p[i];
    return neg ? -(int64_t)v : (int64_t)v;
}
/* bytes with their length (NULL for SQL NULL); *out is malloc'd, NUL-terminated */
static bool xs_ora_rbwl(xs_ora_conn *c, uint8_t **out, size_t *olen) {
    unsigned n = xs_ora_r8(c);
    *out = NULL; *olen = 0;
    if (n == 0 || n == 255) return false;
    if (n != 254) {
        const uint8_t *p = xs_ora_rraw(c, n);
        *out = malloc(n + 1);
        if (!*out) xs_panic("out of memory");
        memcpy(*out, p, n);
        (*out)[n] = 0;
        *olen = n;
        return true;
    }
    size_t cap = 256, len = 0;
    uint8_t *b = malloc(cap);
    for (;;) {
        int64_t k = xs_ora_rub(c);
        if (k <= 0) break;
        if (len + (size_t)k + 1 > cap) { cap = (len + (size_t)k + 1) * 2; b = realloc(b, cap); }
        if (!b) xs_panic("out of memory");
        memcpy(b + len, xs_ora_rraw(c, (size_t)k), (size_t)k);
        len += (size_t)k;
    }
    b[len] = 0;
    *out = b;
    *olen = len;
    return true;
}
static void xs_ora_skip_chunked(xs_ora_conn *c) {
    unsigned n = xs_ora_r8(c);
    if (n == 0 || n == 255) return;
    if (n != 254) { xs_ora_rraw(c, n); return; }
    for (;;) {
        int64_t k = xs_ora_rub(c);
        if (k <= 0) break;
        xs_ora_rraw(c, (size_t)k);
    }
}
static char *xs_ora_rstr(xs_ora_conn *c) { uint8_t *s; size_t n; return xs_ora_rbwl(c, &s, &n) ? (char *)s : NULL; }
static void xs_ora_skip_str(xs_ora_conn *c) { free(xs_ora_rstr(c)); }

/* ---- messages every response may carry ---- */
typedef struct {
    int64_t num;            /* ORA- number, 0 when none */
    char *message;
    int64_t row_count;
    int cursor;
    bool end;               /* the response is complete */
} xs_ora_status;

static void xs_ora_error_info(xs_ora_conn *c, xs_ora_status *st) {
    xs_ora_rub(c);                  /* end of call status */
    xs_ora_rub(c); xs_ora_rub(c); xs_ora_rub(c); xs_ora_rub(c); xs_ora_rub(c); /* seq, row, error, array elem x2 */
    int cursor = (int)xs_ora_rub(c);
    if (cursor) st->cursor = cursor;
    xs_ora_rub(c);                  /* error position */
    xs_ora_rraw(c, 5);              /* sql type, fatal, flags, user cursor options, UPI parameter */
    xs_ora_r8(c);                   /* warning flag */
    xs_ora_rub(c); xs_ora_rub(c); xs_ora_r8(c); xs_ora_rub(c); xs_ora_rub(c); /* rowid */
    xs_ora_rub(c); xs_ora_r8(c); xs_ora_r8(c); xs_ora_rub(c); xs_ora_rub(c);  /* os error, stmt error, call, padding, iters */
    if (xs_ora_rub(c) > 0) xs_ora_skip_chunked(c);                             /* logical rowid */
    int64_t nerr = xs_ora_rub(c);
    if (nerr > 0) { /* batch error codes */
        unsigned first = xs_ora_r8(c);
        for (int64_t i = 0; i < nerr; i++) { if (first == 254) xs_ora_rub(c); xs_ora_rub(c); }
        if (first == 254) xs_ora_rraw(c, 1);
    }
    int64_t noff = xs_ora_rub(c);
    if (noff > 0) {
        unsigned first = xs_ora_r8(c);
        for (int64_t i = 0; i < noff; i++) { if (first == 254) xs_ora_rub(c); xs_ora_rub(c); }
        if (first == 254) xs_ora_rraw(c, 1);
    }
    int64_t nmsg = xs_ora_rub(c);
    if (nmsg > 0) {
        xs_ora_rraw(c, 1);
        for (int64_t i = 0; i < nmsg; i++) { xs_ora_rub(c); xs_ora_skip_str(c); xs_ora_rraw(c, 2); }
    }
    st->num = xs_ora_rub(c);
    st->row_count = xs_ora_rub(c);
    if (c->ttcfv >= 14) { xs_ora_rub(c); xs_ora_rub(c); } /* sql type, server checksum */
    if (st->num != 0) {
        char *m = xs_ora_rstr(c);
        if (m) { size_t l = strlen(m); while (l > 0 && (m[l - 1] == '\n' || m[l - 1] == ' ')) m[--l] = 0; }
        free(st->message);
        st->message = m;
    }
    if (!c->eor) st->end = true;
}
static void xs_ora_piggyback(xs_ora_conn *c) {
    unsigned op = xs_ora_r8(c);
    switch (op) {
    case 1: case 3: break;                                   /* query cache invalidation, trace event */
    case 2: { int64_t n = xs_ora_rub(c); xs_ora_r8(c); xs_ora_rraw(c, (size_t)n); break; } /* os pid (MTS) */
    case 4: {                                                /* session return */
        xs_ora_rub(c); xs_ora_r8(c);
        int64_t n = xs_ora_rub(c);
        if (n > 0) {
            xs_ora_r8(c);
            for (int64_t i = 0; i < n; i++) {
                if (xs_ora_rub(c) > 0) xs_ora_skip_chunked(c);
                if (xs_ora_rub(c) > 0) xs_ora_skip_chunked(c);
                xs_ora_rub(c);
            }
        }
        xs_ora_rub(c); xs_ora_rub(c); xs_ora_rub(c);
        break;
    }
    case 5: {                                                /* sync: session state key/values */
        xs_ora_rub(c); xs_ora_r8(c);
        int64_t n = xs_ora_rub(c);
        xs_ora_rraw(c, 1);
        for (int64_t i = 0; i < n; i++) {
            if (xs_ora_rub(c) > 0) xs_ora_skip_str(c);
            if (xs_ora_rub(c) > 0) xs_ora_skip_chunked(c);
            xs_ora_rub(c);
        }
        xs_ora_rub(c);
        break;
    }
    case 7: if (xs_ora_rub(c) > 0) xs_ora_skip_chunked(c); break;   /* LTXID */
    case 8: xs_ora_rub(c); xs_ora_r8(c); xs_ora_rub(c); xs_ora_rub(c); xs_ora_r8(c); if (xs_ora_rub(c) > 0) xs_ora_skip_chunked(c); break;
    case 9: xs_ora_rub(c); xs_ora_r8(c); break;
    case 10: xs_ora_rub(c); xs_ora_r8(c); xs_ora_rub(c); xs_ora_rub(c); xs_ora_rub(c); break; /* session signature */
    default: xs_ora_fail("oracledb: unknown server piggyback %u", op);
    }
}
static void xs_ora_warning(xs_ora_conn *c) {
    xs_ora_rub(c);
    int64_t n = xs_ora_rub(c);
    xs_ora_rub(c);
    if (n > 0) xs_ora_skip_str(c);
}
/* a message every kind of response may have; false when the type is not one of them */
static bool xs_ora_common(xs_ora_conn *c, unsigned type, xs_ora_status *st) {
    switch (type) {
    case 4: xs_ora_error_info(c, st); return true;
    case 9: xs_ora_rub(c); xs_ora_rub(c); if (!c->eor) st->end = true; return true;
    case 15: xs_ora_warning(c); return true;
    case 23: xs_ora_piggyback(c); return true;
    case 29: st->end = true; return true;
    default: return false;
    }
}
/* the function header: message type 3, function code, sequence number, and the token number from 23.1 */
static void xs_ora_func(xs_ora_conn *c, xs_ora_w *w, unsigned code) {
    if (c->nclose > 0) { /* close the cursors of finished statements first */
        xs_ora_u8(w, 17); xs_ora_u8(w, 105); xs_ora_u8(w, c->seq); c->seq = (uint8_t)(c->seq + 1);
        if (c->ttcfv >= 18) xs_ora_ub(w, 0);
        xs_ora_u8(w, 1);
        xs_ora_ub(w, (uint64_t)c->nclose);
        for (int i = 0; i < c->nclose; i++) xs_ora_ub(w, c->close_cursors[i]);
        c->nclose = 0;
    }
    xs_ora_u8(w, 3); xs_ora_u8(w, code); xs_ora_u8(w, c->seq); c->seq = (uint8_t)(c->seq + 1);
    if (c->ttcfv >= 18) xs_ora_ub(w, 0);
}
static void xs_ora_close_later(xs_ora_conn *c, int cursor) {
    if (cursor <= 0) return;
    if (c->nclose == c->close_cap) {
        c->close_cap = c->close_cap ? c->close_cap * 2 : 16;
        c->close_cursors = realloc(c->close_cursors, sizeof(uint32_t) * (size_t)c->close_cap);
        if (!c->close_cursors) xs_panic("out of memory");
    }
    c->close_cursors[c->nclose++] = (uint32_t)cursor;
}

/* ---- connect and log in ---- */
static void xs_ora_hex(const uint8_t *p, size_t n, char *out) {
    static const char d[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; i++) { out[2 * i] = d[p[i] >> 4]; out[2 * i + 1] = d[p[i] & 15]; }
    out[2 * n] = 0;
}
static size_t xs_ora_unhex(const char *s, uint8_t *out, size_t cap) {
    size_t n = strlen(s) / 2;
    if (n > cap) n = cap;
    for (size_t i = 0; i < n; i++) {
        int hi = s[2 * i], lo = s[2 * i + 1];
        hi = hi <= '9' ? hi - '0' : (hi | 32) - 'a' + 10;
        lo = lo <= '9' ? lo - '0' : (lo | 32) - 'a' + 10;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return n;
}
typedef struct { char *k[64]; char *v[64]; unsigned flags[64]; int n; } xs_ora_kvs;
static const char *xs_ora_kget(const xs_ora_kvs *kv, const char *k, unsigned *flags) {
    for (int i = 0; i < kv->n; i++) if (strcmp(kv->k[i], k) == 0) { if (flags) *flags = kv->flags[i]; return kv->v[i]; }
    return NULL;
}
static void xs_ora_kfree(xs_ora_kvs *kv) {
    for (int i = 0; i < kv->n; i++) { free(kv->k[i]); if (kv->v[i]) { memset(kv->v[i], 0, strlen(kv->v[i])); free(kv->v[i]); } }
    kv->n = 0;
}
/* the login responses: protocol info (1), data types (2), key/value parameters (8) and the common messages */
static void xs_ora_read_login(xs_ora_conn *c, xs_ora_kvs *kv, xs_ora_status *st) {
    while (!st->end) {
        unsigned t = xs_ora_r8(c);
        if (t == 1) {
            xs_ora_r8(c); xs_ora_r8(c);                      /* server version, zero */
            for (int i = 0; i < 48 && xs_ora_r8(c) != 0; i++) {} /* banner */
            xs_ora_rraw(c, 3);                               /* charset (LE), flags */
            const uint8_t *ne = xs_ora_rraw(c, 2);
            size_t nelem = (size_t)(ne[0] | ne[1] << 8);
            if (nelem) xs_ora_rraw(c, nelem * 5);
            const uint8_t *fl = xs_ora_rraw(c, 2);
            xs_ora_rraw(c, (size_t)(fl[0] << 8 | fl[1]));
            uint8_t *cc, *rc;
            size_t ccn, rcn;
            xs_ora_rbwl(c, &cc, &ccn);
            xs_ora_rbwl(c, &rc, &rcn);
            if (cc && ccn > 7 && cc[7] < c->ttcfv) c->ttcfv = cc[7];
            if (c->ttcfv < 24 && c->eor) c->eor = false;     /* end of request needs 23.4 */
            c->max_str = rc && rcn > 6 && (rc[6] & 0x04) ? 32767 : 4000;
            free(cc); free(rc);
            if (!c->eor && !c->fast) st->end = true;
        } else if (t == 2) {
            for (;;) {
                const uint8_t *d = xs_ora_rraw(c, 2);
                if (!(d[0] | d[1])) break;
                const uint8_t *cv = xs_ora_rraw(c, 2);
                if (cv[0] | cv[1]) xs_ora_rraw(c, 4);
            }
            if (!c->eor && !c->fast) st->end = true;
        } else if (t == 8) {
            int64_t n = xs_ora_rub(c);
            for (int64_t i = 0; i < n; i++) {
                xs_ora_rub(c);
                char *k = xs_ora_rstr(c), *v = NULL;
                if (xs_ora_rub(c) > 0) v = xs_ora_rstr(c);
                unsigned flags = (unsigned)xs_ora_rub(c);
                if (kv->n < 64 && k) { kv->k[kv->n] = k; kv->v[kv->n] = v ? v : strdup(""); kv->flags[kv->n] = flags; kv->n++; }
                else { free(k); free(v); }
            }
        } else if (t == 28) {
            xs_ora_fail("oracledb: the server asked to renegotiate the connection (not supported)");
        } else if (!xs_ora_common(c, t, st)) {
            xs_ora_fail("oracledb: unexpected message %u while logging in", t);
        }
    }
}
static void xs_ora_auth_one(xs_ora_conn *c, xs_ora_w *w, const char *user) {
    char pid[24];
    snprintf(pid, sizeof pid, "%d", (int)getpid());
    size_t ul = strlen(user);
    xs_ora_func(c, w, 118);
    xs_ora_u8(w, ul ? 1 : 0); xs_ora_ub(w, ul); xs_ora_ub(w, 0x101); /* logon, with password */
    xs_ora_u8(w, 1); xs_ora_ub(w, 5); xs_ora_u8(w, 0); xs_ora_u8(w, 1);
    if (ul) xs_ora_bwl(w, user, ul);
    xs_ora_kv(w, "AUTH_TERMINAL", "unknown", 0);
    xs_ora_kv(w, "AUTH_PROGRAM_NM", "xisto", 0);
    xs_ora_kv(w, "AUTH_MACHINE", "xisto", 0);
    xs_ora_kv(w, "AUTH_PID", pid, 0);
    xs_ora_kv(w, "AUTH_SID", "xisto", 0);
}
static void xs_ora_login_error(xs_ora_status *st) {
    static XS_TLS char msg[600];
    snprintf(msg, sizeof msg, "%s", st->message ? st->message : "ORA-01017: invalid credential or not authorized; logon denied");
    free(st->message);
    st->message = NULL;
    xs_throw(xs_Error_from(msg));
}
/* the second login phase: O5LOGON with the 12c verifier */
static void xs_ora_auth_two(xs_ora_conn *c, const char *user, const char *password, xs_ora_kvs *kv) {
    unsigned vtype = 0;
    const char *vfr = xs_ora_kget(kv, "AUTH_VFR_DATA", &vtype), *skey = xs_ora_kget(kv, "AUTH_SESSKEY", NULL);
    const char *vgen = xs_ora_kget(kv, "AUTH_PBKDF2_VGEN_COUNT", NULL), *csalt = xs_ora_kget(kv, "AUTH_PBKDF2_CSK_SALT", NULL);
    const char *sder = xs_ora_kget(kv, "AUTH_PBKDF2_SDER_COUNT", NULL);
    if (!vfr || !skey) xs_ora_fail("oracledb: the server sent no session key (ORA-01017?)");
    if (vtype != 0x4815 || !vgen || !csalt || !sder)
        xs_ora_fail("oracledb: the account's password verifier is 11g (0x%x); xisto supports the 12c verifier (ALTER USER ... IDENTIFIED BY ... with SQLNET.ALLOWED_LOGON_VERSION_SERVER=12)", vtype);
    uint8_t vfr_b[64], skey_b[64], salt[128], pkey[64], phash[64], parta[64], partb[64], enc[64], combo[32];
    size_t vfr_n = xs_ora_unhex(vfr, vfr_b, sizeof vfr_b), skey_n = xs_ora_unhex(skey, skey_b, sizeof skey_b);
    if (skey_n != 32 && skey_n != 48) xs_ora_fail("oracledb: unexpected session key length %zu", skey_n);
    memcpy(salt, vfr_b, vfr_n);
    memcpy(salt + vfr_n, "AUTH_PBKDF2_SPEEDY_KEY", 22);
    xs_ora_pbkdf2_512((const uint8_t *)password, strlen(password), salt, vfr_n + 22, (uint32_t)atol(vgen), pkey, 64);
    xs_ora_sha512 h;
    xs_ora_sha512_init(&h); xs_ora_sha512_update(&h, pkey, 64); xs_ora_sha512_update(&h, vfr_b, vfr_n); xs_ora_sha512_final(&h, phash);
    memcpy(parta, skey_b, skey_n);
    xs_ora_cbc(phash, parta, skey_n, false);                  /* the server's half of the session key */
    xs_random_bytes(partb, skey_n);
    memcpy(enc, partb, skey_n);
    xs_ora_cbc(phash, enc, skey_n, true);                     /* ours, encrypted for the server */
    char sesskey[129];
    xs_ora_hex(enc, 32, sesskey);
    uint8_t ab[64];
    memcpy(ab, partb, 32);
    memcpy(ab + 32, parta, 32);
    char abhex[129];
    xs_ora_hex(ab, 64, abhex);
    uint8_t csalt_b[64];
    size_t csalt_n = xs_ora_unhex(csalt, csalt_b, sizeof csalt_b);
    xs_ora_pbkdf2_512((const uint8_t *)abhex, 128, csalt_b, csalt_n, (uint32_t)atol(sder), combo, 32);
    /* the speedy key: 16 random bytes and the PBKDF2 key, encrypted with the combo key (80 bytes shown) */
    uint8_t sp[96];
    xs_random_bytes(sp, 16);
    memcpy(sp + 16, pkey, 64);
    memset(sp + 80, 16, 16);                                   /* PKCS#7 padding of a full block */
    xs_ora_cbc(combo, sp, 96, true);
    char speedy[161];
    xs_ora_hex(sp, 80, speedy);
    /* the password: 16 random bytes and the password, padded (PKCS#7), encrypted with the combo key */
    size_t pl = strlen(password), tl = 16 + pl, padn = 16 - tl % 16;
    uint8_t *pw = malloc(tl + padn);
    char *pwhex = malloc((tl + padn) * 2 + 1);
    if (!pw || !pwhex) xs_panic("out of memory");
    xs_random_bytes(pw, 16);
    memcpy(pw + 16, password, pl);
    memset(pw + tl, (int)padn, padn);
    xs_ora_cbc(combo, pw, tl + padn, true);
    xs_ora_hex(pw, tl + padn, pwhex);
    /* the session's time zone: the client's, so DATE values read as local time agree with SYSDATE math */
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    long off = lt.tm_gmtoff / 60;
    char alter[80];
    snprintf(alter, sizeof alter, "ALTER SESSION SET TIME_ZONE='%c%02ld:%02ld'", off < 0 ? '-' : '+', labs(off) / 60, labs(off) % 60);
    size_t al = strlen(alter);
    alter[al] = 0; /* (sent with its NUL, as the driver does) */
    xs_ora_w w = {0};
    size_t ul = strlen(user);
    xs_ora_func(c, &w, 115);
    xs_ora_u8(&w, ul ? 1 : 0); xs_ora_ub(&w, ul); xs_ora_ub(&w, 0x101);
    xs_ora_u8(&w, 1); xs_ora_ub(&w, 7); xs_ora_u8(&w, 1); xs_ora_u8(&w, 1);
    if (ul) xs_ora_bwl(&w, user, ul);
    xs_ora_kv(&w, "AUTH_SESSKEY", sesskey, 1);
    xs_ora_kv(&w, "AUTH_PBKDF2_SPEEDY_KEY", speedy, 0);
    xs_ora_kv(&w, "SESSION_CLIENT_CHARSET", "873", 0);
    xs_ora_kv(&w, "SESSION_CLIENT_DRIVER_NAME", "xisto-oracledb", 0);
    xs_ora_kv(&w, "SESSION_CLIENT_VERSION", "1", 0);
    { /* AUTH_ALTER_SESSION goes with its terminating NUL */
        const char *k = "AUTH_ALTER_SESSION";
        xs_ora_ub(&w, strlen(k)); xs_ora_bwl(&w, k, strlen(k));
        xs_ora_ub(&w, al + 1); xs_ora_bwl(&w, alter, al + 1);
        xs_ora_ub(&w, 1);
    }
    xs_ora_kv(&w, "AUTH_PASSWORD", pwhex, 0);
    bool ok = xs_ora_send(c, &w);
    memset(w.p, 0, w.len);
    free(w.p);
    memset(pw, 0, tl + padn); free(pw);
    memset(pwhex, 0, (tl + padn) * 2); free(pwhex);
    memset(pkey, 0, sizeof pkey); memset(phash, 0, sizeof phash); memset(parta, 0, sizeof parta); memset(partb, 0, sizeof partb);
    memset(sp, 0, sizeof sp); memset(speedy, 0, sizeof speedy); memset(ab, 0, sizeof ab); memset(abhex, 0, sizeof abhex);
    if (!ok) xs_ora_fail("ORA-03113: end-of-file on communication channel");
    xs_ora_kvs kv2 = {0};
    xs_ora_status st = {0};
    xs_ora_start_response(c);
    xs_ora_read_login(c, &kv2, &st);
    if (st.num != 0) { xs_ora_kfree(&kv2); memset(combo, 0, sizeof combo); xs_ora_login_error(&st); }
    /* the server proves it derived the same combo key: AUTH_SVR_RESPONSE decrypts to ...SERVER_TO_CLIENT... */
    const char *resp = xs_ora_kget(&kv2, "AUTH_SVR_RESPONSE", NULL);
    uint8_t rb[64];
    size_t rn = resp ? xs_ora_unhex(resp, rb, sizeof rb) : 0;
    bool good = rn >= 32 && rn % 16 == 0;
    if (good) { xs_ora_cbc(combo, rb, rn, false); good = memcmp(rb + 16, "SERVER_TO_CLIENT", 16) == 0; }
    memset(combo, 0, sizeof combo);
    xs_ora_kfree(&kv2);
    if (!good) xs_ora_fail("oracledb: the server's authentication response does not match (wrong verifier?)");
}

/* "(DESCRIPTION=...(HOST=x)(PORT=n)...)" -> the value of KEY= inside, case-insensitive */
static bool xs_ora_desc_value(const char *d, const char *key, char *out, size_t cap) {
    size_t kl = strlen(key);
    for (const char *p = d; *p; p++) {
        if (*p != '(') continue;
        const char *q = p + 1;
        while (*q == ' ') q++;
        if (strncasecmp(q, key, kl) != 0) continue;
        q += kl;
        while (*q == ' ') q++;
        if (*q != '=') continue;
        q++;
        while (*q == ' ') q++;
        size_t n = 0;
        while (q[n] && q[n] != ')' && n + 1 < cap) { out[n] = q[n]; n++; }
        while (n > 0 && out[n - 1] == ' ') n--;
        out[n] = 0;
        return true;
    }
    return false;
}
static void xs_ora_close(xs_ora_conn *c) {
    if (c->fd >= 0) {
        if (!c->broken) { /* log off, then an empty data packet with the end-of-file flag */
            xs_ora_w w = {0};
            xs_ora_func(c, &w, 9);
            if (xs_ora_send(c, &w)) {
                xs_ora_status st = {0};
                xs_ora_start_response(c);
                while (!st.end && !c->broken) {
                    if (c->in_len - c->pos == 0 && !xs_ora_more(c)) break;
                    unsigned t = c->in[c->pos++];
                    if (!xs_ora_common(c, t, &st)) break;
                }
                free(st.message);
                uint8_t eof[10] = {0};
                if (c->large_sdu) eof[3] = 10; else eof[1] = 10;
                eof[4] = 6; eof[9] = 0x40;
                xs_ora_send_raw(c, eof, 10);
            }
            free(w.p);
        }
        close(c->fd);
    }
    free(c->in);
    free(c->close_cursors);
    free(c);
}
static void xs_ora_login(xs_ora_conn *c, xn_oracledb_NativePool *p);
static xs_ora_conn *xs_ora_open(xn_oracledb_NativePool *p) {
    xs_ora_conn *c = calloc(1, sizeof *c);
    if (!c) xs_panic("out of memory");
    c->fd = xs_connect(p->host, p->port);
    if (c->fd < 0) { free(c); xs_ora_fail("NJS-503: connection to host %s port %d could not be established (ECONNREFUSED)", p->host, p->port); }
    c->seq = 1;
    c->ttcfv = 24;
    c->max_str = 4000;
    size_t dl = strlen(p->descriptor);
    if (dl > 230) dl = 230; /* (longer connect data would go after the packet; descriptors here are short) */
    uint8_t h[74 + 230] = {0};
    size_t n = 74 + dl;
    h[0] = (uint8_t)(n >> 8); h[1] = (uint8_t)n; h[4] = 1;
    h[8] = 0x01; h[9] = 0x3f;   /* version 319 */
    h[10] = 0x01; h[11] = 0x2c; /* lowest 300 */
    h[13] = 1;                  /* options: don't care */
    h[14] = 0x20;               /* SDU 8192 */
    h[16] = 0xff; h[17] = 0xff; /* TDU */
    h[18] = 0x4f; h[19] = 0x98; /* protocol characteristics */
    h[23] = 1;                  /* 1 in host order */
    h[24] = (uint8_t)(dl >> 8); h[25] = (uint8_t)dl;
    h[27] = 74;                 /* connect data offset */
    h[32] = h[33] = 0x08;       /* no network services (encryption, checksums) */
    h[60] = 0x20;               /* large SDU 8192 */
    h[63] = 0x20;               /* large TDU 2 MB */
    memcpy(h + 74, p->descriptor, dl);
    for (int tries = 0;; tries++) {
        if (!xs_ora_send_raw(c, h, n)) break;
        uint8_t *pk;
        size_t pn;
        int t = xs_ora_recv_packet(c, &pk, &pn);
        if (t == 2 && pn >= 32) { /* accept */
            c->version = (uint16_t)(pk[8] << 8 | pk[9]);
            c->large_sdu = c->version >= 315;
            c->sdu = c->large_sdu && pn >= 36 ? ((uint32_t)pk[32] << 24 | (uint32_t)pk[33] << 16 | (uint32_t)pk[34] << 8 | pk[35]) : (uint32_t)(pk[12] << 8 | pk[13]);
            uint32_t flags2 = c->version >= 318 && pn >= 45 ? ((uint32_t)pk[41] << 24 | (uint32_t)pk[42] << 16 | (uint32_t)pk[43] << 8 | pk[44]) : 0;
            free(pk);
            bool no_fast = getenv("XISTO_ORACLE_NO_FAST_AUTH") && *getenv("XISTO_ORACLE_NO_FAST_AUTH") == '1';
            c->eor = c->version >= 319 && (flags2 & 0x02000000) && !no_fast;
            c->fast = (flags2 & 0x10000000) && !no_fast;
            goto accepted;
        }
        if (t == 11 && tries < 3) { free(pk); continue; } /* resend */
        char reason[300] = "";
        if (t == 4 && pn > 12) { /* refuse: "(DESCRIPTION=(ERR=12514)...)" */
            char err[16] = "";
            char *data = strndup((char *)pk + 12, pn - 12);
            if (data && xs_ora_desc_value(data, "ERR", err, sizeof err)) {
                if (strcmp(err, "12514") == 0) snprintf(reason, sizeof reason, "ORA-12514: Cannot connect to database. Service is not registered with the listener at host %s port %d.", p->host, p->port);
                else if (strcmp(err, "12505") == 0) snprintf(reason, sizeof reason, "ORA-12505: Cannot connect to database. SID is not registered with the listener at host %s port %d.", p->host, p->port);
                else snprintf(reason, sizeof reason, "ORA-%s: the listener at %s:%d refused the connection", err, p->host, p->port);
            }
            free(data);
        } else if (t == 5) {
            snprintf(reason, sizeof reason, "oracledb: the listener redirected the connection (not supported yet: connect to the instance's own address)");
        }
        if (t >= 0) free(pk);
        close(c->fd); free(c->in); free(c);
        xs_ora_fail("%s", reason[0] ? reason : "NJS-500: connection to the Oracle Database was broken");
    }
    close(c->fd); free(c);
    xs_ora_fail("NJS-500: connection to the Oracle Database was broken");
accepted:;
    jmp_buf jb;
    xs_try_push(&jb);
    if (_setjmp(jb) != 0) { /* the login failed: the socket and buffers go, the error goes on */
        void *e = xs_caught();
        c->broken = true;
        if (c->fd >= 0) close(c->fd);
        free(c->in); free(c->close_cursors); free(c);
        xs_throw(e);
    }
    xs_ora_login(c, p);
    xs_try_pop();
    return c;
}
static void xs_ora_login(xs_ora_conn *c, xn_oracledb_NativePool *p) {
    /* protocol, data types and the first login phase: one request with fast auth, three otherwise */
    xs_ora_kvs kv = {0};
    xs_ora_status st = {0};
    uint8_t *dty = malloc(sizeof xs_ora_dty_msg);
    if (!dty) xs_panic("out of memory");
    memcpy(dty, xs_ora_dty_msg, sizeof xs_ora_dty_msg);
    if (!c->eor) dty[XS_ORA_DTY_CCAPS + 40] &= (uint8_t)~0x20;
    xs_ora_w w = {0};
    if (c->fast) {
        xs_ora_u8(&w, 34); xs_ora_u8(&w, 1); xs_ora_u8(&w, 1); xs_ora_u8(&w, 0); /* fast auth v1, server converts chars */
        xs_ora_u8(&w, 1); xs_ora_u8(&w, 6); xs_ora_u8(&w, 0); xs_ora_put(&w, "xisto-oracledb", 15);
        xs_ora_u8(&w, 0); xs_ora_u8(&w, 0); xs_ora_u8(&w, 0); xs_ora_u8(&w, 0); xs_ora_u8(&w, 0);
        xs_ora_u8(&w, 13); /* field version 19.1 ext 1 for the rest of this request */
        xs_ora_put(&w, dty, sizeof xs_ora_dty_msg);
        int keep = c->ttcfv;
        c->ttcfv = 13;
        xs_ora_auth_one(c, &w, p->user);
        c->ttcfv = keep;
        bool ok = xs_ora_send(c, &w);
        w.len = 0;
        if (!ok) { free(dty); free(w.p); xs_ora_fail("NJS-500: connection to the Oracle Database was broken"); }
        xs_ora_start_response(c);
        xs_ora_read_login(c, &kv, &st);
    } else {
        xs_ora_u8(&w, 1); xs_ora_u8(&w, 6); xs_ora_u8(&w, 0); xs_ora_put(&w, "xisto-oracledb", 15);
        bool ok = xs_ora_send(c, &w);
        w.len = 0;
        if (ok) { xs_ora_start_response(c); xs_ora_read_login(c, &kv, &st); }
        if (ok && st.num == 0) {
            dty[XS_ORA_DTY_CCAPS + 7] = (uint8_t)c->ttcfv; /* our field version: the negotiated one */
            if (!c->eor) dty[XS_ORA_DTY_CCAPS + 40] &= (uint8_t)~0x20;
            xs_ora_put(&w, dty, sizeof xs_ora_dty_msg);
            st.end = false;
            ok = xs_ora_send(c, &w);
            w.len = 0;
            if (ok) { xs_ora_start_response(c); xs_ora_read_login(c, &kv, &st); }
        }
        if (ok && st.num == 0) {
            xs_ora_auth_one(c, &w, p->user);
            st.end = false;
            ok = xs_ora_send(c, &w);
            w.len = 0;
            if (ok) { xs_ora_start_response(c); xs_ora_read_login(c, &kv, &st); }
        }
        if (!ok) { free(dty); free(w.p); xs_ora_fail("NJS-500: connection to the Oracle Database was broken"); }
    }
    free(dty);
    free(w.p);
    if (st.num != 0) { xs_ora_kfree(&kv); xs_ora_login_error(&st); }
    xs_ora_auth_two(c, p->user, p->password ? p->password : "", &kv);
    xs_ora_kfree(&kv);
}

/* ---- the pool: connections per worker thread, as in pg ---- */
static int xs_ora_workers(void) {
    const char *w = getenv("XISTO_WORKERS");
    int n = w ? atoi(w) : 1;
    return n < 1 ? 1 : n;
}
static xs_ora_conn *xs_ora_acquire(xn_oracledb_NativePool *p) {
    xs_ora_local *l = &xs_ora_locals[p->id];
    if (l->idle) { xs_ora_conn *c = l->idle; l->idle = c->next; c->next = NULL; return c; }
    if (l->open < p->max || !xs_cur_coro) {
        l->open++; /* the slot is ours while the connection opens (other coroutines run during its I/O) */
        jmp_buf jb;
        xs_try_push(&jb);
        if (_setjmp(jb) == 0) {
            xs_ora_conn *c = xs_ora_open(p);
            xs_try_pop();
            return c;
        }
        void *e = xs_caught();
        l->open--;
        xs_throw(e);
    }
    /* on the heap: tasks share one stack, and a parked task's stack is not where it was while others run */
    xs_ora_waiter *w = calloc(1, sizeof *w);
    w->co = xs_cur_coro;
    if (l->tail) l->tail->next = w; else l->head = w;
    l->tail = w;
    xs_coro_yield();
    xs_ora_conn *got = w->got;
    free(w);
    if (!got) return xs_ora_acquire(p);
    return got;
}
static void xs_ora_release(xn_oracledb_NativePool *p, xs_ora_conn *c) {
    xs_ora_local *l = &xs_ora_locals[p->id];
    if (c && c->broken) { xs_ora_close(c); l->open--; c = NULL; }
    if (l->head) {
        xs_ora_waiter *w = l->head;
        l->head = w->next;
        if (!l->head) l->tail = NULL;
        w->got = c;
        xs_ready_push(w->co);
        return;
    }
    if (c) { c->next = l->idle; l->idle = c; }
}

static void xn_oracledb_NativePool_free(void *ptr) {
    xn_oracledb_NativePool *p = ptr;
    free(p->host); free(p->descriptor); free(p->user);
    if (p->password) { memset(p->password, 0, strlen(p->password)); free(p->password); }
    xs_obj_free(ptr);
}
static void xn_oracledb_NativePool_print(void *p) { (void)p; fputs("Pool {}", stdout); }
static char *xs_ora_cfg_str(xs_jv *cfg, const char *key) {
    xs_jv *v = cfg ? xs_json_field_opt(cfg, key) : NULL;
    return v && v->kind == XJ_STR ? xs_strndup_(v->str, v->slen) : NULL;
}
/* new NativePool({ user, password, connectString, poolMax }) */
XS_NOINLINE xn_oracledb_NativePool *xn_oracledb_NativePool_new(xs_str *config) {
    xn_oracledb_NativePool *p = xs_obj_alloc(sizeof *p, xn_oracledb_NativePool_free, xn_oracledb_NativePool_print);
    xs_jarena_reset();
    xs_jv *cfg = xs_json_parse(config);
    p->user = xs_ora_cfg_str(cfg, "user");
    p->password = xs_ora_cfg_str(cfg, "password");
    char *cs = xs_ora_cfg_str(cfg, "connectString");
    if (!cs) cs = xs_ora_cfg_str(cfg, "connectionString");
    xs_jv *mx = cfg ? xs_json_field_opt(cfg, "poolMax") : NULL;
    int per = mx && mx->kind == XJ_NUM ? (int)mx->num : 4;
    xs_jv_free(cfg);
    if (!p->user) p->user = strdup("");
    if (!cs || !*cs) { free(cs); xs_ora_fail("NJS-125: \"connectString\" cannot be empty or undefined. Bequeath connections are not supported in Thin mode"); }
    char host[256] = "localhost", port[16] = "1521", service[256] = "";
    const char *s = cs;
    while (*s == ' ') s++;
    if (*s == '(') { /* a descriptor: host and port for the socket; the descriptor itself is the connect data */
        xs_ora_desc_value(s, "HOST", host, sizeof host);
        xs_ora_desc_value(s, "PORT", port, sizeof port);
        p->descriptor = strdup(s);
    } else { /* Easy Connect: [//]host[:port][/service] */
        if (strncmp(s, "//", 2) == 0) s += 2;
        size_t i = 0;
        if (*s == '[') { s++; while (*s && *s != ']' && i + 1 < sizeof host) host[i++] = *s++; if (*s == ']') s++; }
        else while (*s && *s != ':' && *s != '/' && i + 1 < sizeof host) host[i++] = *s++;
        host[i] = 0;
        if (*s == ':') { s++; i = 0; while (*s && *s != '/' && i + 1 < sizeof port) port[i++] = *s++; port[i] = 0; }
        if (*s == '/') { s++; i = 0; while (*s && *s != ':' && *s != '?' && i + 1 < sizeof service) service[i++] = *s++; service[i] = 0; }
        char d[700];
        snprintf(d, sizeof d, "(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=%s)(PORT=%s))(CONNECT_DATA=(SERVICE_NAME=%s)(CID=(PROGRAM=xisto)(HOST=xisto)(USER=xisto))))", host, port, service);
        p->descriptor = strdup(d);
    }
    free(cs);
    p->host = strdup(host);
    p->port = atoi(port) > 0 ? atoi(port) : 1521;
    p->max = (per + xs_ora_workers() - 1) / xs_ora_workers();
    if (p->max < 1) p->max = 1;
    p->id = __atomic_fetch_add(&xs_ora_npools, 1, __ATOMIC_RELAXED);
    if (p->id >= XS_ORA_MAX_POOLS) xs_panic("oracledb: too many pools (8)");
    return p;
}

/* connections handed to the program: a handle (an index in this thread's table) per checked-out connection */
static int64_t xs_ora_hold(xn_oracledb_NativePool *p, xs_ora_conn *c) {
    xs_ora_local *l = &xs_ora_locals[p->id];
    for (int i = 0; i < l->nheld; i++) if (!l->held[i]) { l->held[i] = c; return i + 1; }
    if (l->nheld == l->held_cap) {
        l->held_cap = l->held_cap ? l->held_cap * 2 : 8;
        l->held = realloc(l->held, sizeof(xs_ora_conn *) * (size_t)l->held_cap);
        if (!l->held) xs_panic("out of memory");
    }
    l->held[l->nheld++] = c;
    return l->nheld;
}
static xs_ora_conn *xs_ora_held(xn_oracledb_NativePool *p, int64_t h) {
    xs_ora_local *l = &xs_ora_locals[p->id];
    if (h < 1 || h > l->nheld || !l->held[h - 1]) xs_ora_fail("NJS-003: invalid or closed connection");
    return l->held[h - 1];
}
XS_NOINLINE int64_t xn_oracledb_NativePool_acquire(xn_oracledb_NativePool *p) { return xs_ora_hold(p, xs_ora_acquire(p)); }
XS_NOINLINE void xn_oracledb_NativePool_release(xn_oracledb_NativePool *p, int64_t h) {
    xs_ora_conn *c = xs_ora_held(p, h);
    xs_ora_locals[p->id].held[h - 1] = NULL;
    if (!c->broken) { /* a connection back in the pool starts without an open transaction */
        xs_ora_w w = {0};
        xs_ora_func(c, &w, 15);
        if (xs_ora_send(c, &w)) {
            xs_ora_status st = {0};
            xs_ora_start_response(c);
            while (!st.end) { unsigned t = xs_ora_r8(c); if (!xs_ora_common(c, t, &st)) { c->broken = true; break; } }
            free(st.message);
        }
        free(w.p);
    }
    xs_ora_release(p, c);
}
XS_NOINLINE void xn_oracledb_NativePool_close(xn_oracledb_NativePool *p) {
    xs_ora_local *l = &xs_ora_locals[p->id];
    while (l->idle) { xs_ora_conn *c = l->idle; l->idle = c->next; xs_ora_close(c); l->open--; }
}

/* ---- values ---- */
/* Oracle NUMBER: an exponent byte, then base-100 digits (positive: digit+1; negative: 101-digit, then 102) */
static void xs_ora_number_text(const uint8_t *b, size_t n, char *out) {
    if (n == 0) { strcpy(out, "0"); return; }
    int exponent = b[0];
    bool pos = (exponent & 0x80) != 0;
    if (!pos) exponent ^= 0xff;
    exponent -= 193;
    int dp = exponent * 2 + 2;
    if (n == 1) { strcpy(out, pos ? "0" : "-1e126"); return; }
    size_t nb = n;
    if (!pos && b[n - 1] == 102) nb--;
    char digits[90];
    int nd = 0;
    for (size_t i = 1; i < nb; i++) {
        int d100 = pos ? b[i] - 1 : 101 - b[i];
        int d = d100 / 10;
        if (d == 0 && i == 1) dp--;
        else if (d == 10) { digits[nd++] = '1'; digits[nd++] = '0'; dp++; }
        else if (d != 0 || i > 1) digits[nd++] = (char)('0' + d);
        d = d100 % 10;
        if (d != 0 || i < nb - 1) digits[nd++] = (char)('0' + d);
    }
    char *o = out;
    if (!pos) *o++ = '-';
    if (dp <= 0) { *o++ = '0'; *o++ = '.'; for (int k = 0; k < -dp; k++) *o++ = '0'; }
    for (int i = 0; i < nd; i++) {
        if (i > 0 && i == dp) *o++ = '.';
        *o++ = digits[i];
    }
    for (int i = nd; i < dp; i++) *o++ = '0';
    *o = 0;
}
static void xs_ora_put_number(xs_ora_w *w, const char *text) {
    bool neg = text[0] == '-';
    if (neg || text[0] == '+') text++;
    char digits[128];
    int nd = 0, exp = 0, seen_dot = -1;
    const char *e = NULL;
    for (const char *s = text; *s; s++) {
        if (*s == '.') { seen_dot = nd; continue; }
        if (*s == 'e' || *s == 'E') { e = s + 1; break; }
        if (nd < 120) digits[nd++] = *s;
    }
    if (e) exp = atoi(e);
    if (seen_dot >= 0) exp -= nd - seen_dot;
    int start = 0;
    while (start < nd && digits[start] == '0') start++;
    int end = nd;
    while (end > start && digits[end - 1] == '0') { end--; exp++; }
    int len = end - start;
    char v[130];
    memcpy(v, digits + start, (size_t)len);
    if ((exp > 0 && exp % 2 == 1) || (exp < 0 && exp % 2 == -1)) { exp--; v[len++] = '0'; }
    char vv[132];
    int off = 0;
    if (len % 2 == 1) vv[off++] = '0';
    memcpy(vv + off, v, (size_t)len);
    len += off;
    if (len > 40 || exp >= 126 || exp <= -131) xs_ora_fail("NJS-139: value cannot be represented as an Oracle Database number");
    bool sentinel = neg && len < 40;
    int pairs = len / 2;
    int ew = (exp + len) / 2 + 192;
    if (neg) ew ^= 0xff;
    else if (len == 0 && exp == 0) ew = 128;
    xs_ora_u8(w, (unsigned)(pairs + 1 + sentinel));
    xs_ora_u8(w, (unsigned)ew);
    for (int i = 0; i < len; i += 2) {
        int d = (vv[i] - '0') * 10 + (vv[i + 1] - '0');
        xs_ora_u8(w, (unsigned)(neg ? 101 - d : d + 1));
    }
    if (sentinel) xs_ora_u8(w, 102);
}
/* days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant's algorithm) */
static int64_t xs_ora_days(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
/* DATE / TIMESTAMP bytes -> an ISO instant: local wall time for DATE and TIMESTAMP, UTC for the TZ kinds */
static void xs_ora_date_json(xs_sb *sb, const uint8_t *b, size_t n, bool local) {
    if (n < 7) { xs_sb_put(sb, "null", 4); return; }
    int64_t year = ((int64_t)b[0] - 100) * 100 + b[1] - 100;
    double ms = (double)xs_ora_days(year, b[2], b[3]) * 86400000.0 + ((b[4] - 1) * 3600.0 + (b[5] - 1) * 60.0 + (b[6] - 1)) * 1000.0;
    if (n >= 11) ms += (double)(((uint32_t)b[7] << 24 | (uint32_t)b[8] << 16 | (uint32_t)b[9] << 8 | b[10]) / 1000000);
    if (local) ms = xs_local_to_utc(ms);
    char t[40];
    int k = xs_iso_into(ms, t);
    xs_sb_put(sb, "\"", 1);
    xs_sb_put(sb, t, (size_t)k);
    xs_sb_put(sb, "\"", 1);
}
static void xs_ora_hex_json(xs_sb *sb, const uint8_t *b, size_t n) {
    static const char d[] = "0123456789abcdef";
    xs_sb_put(sb, "\"", 1);
    for (size_t i = 0; i < n; i++) { char x[2] = {d[b[i] >> 4], d[b[i] & 15]}; xs_sb_put(sb, x, 2); }
    xs_sb_put(sb, "\"", 1);
}

/* ---- binds ---- */
typedef struct { int kind; int64_t i; double d; xs_str *s; double t; const uint8_t *bytes; size_t nbytes; } xs_ora_val;
enum { XS_ORA_NULL, XS_ORA_INT, XS_ORA_NUM, XS_ORA_STR, XS_ORA_BOOL, XS_ORA_DATE, XS_ORA_BYTES };
/* the bind placeholders of a statement, in order: ":name" or ":1" outside quotes and comments */
typedef struct { char name[64]; } xs_ora_ph;
static int xs_ora_placeholders(const char *sql, size_t n, xs_ora_ph *out, int cap, bool plsql) {
    int k = 0;
    for (size_t i = 0; i < n; i++) {
        char ch = sql[i];
        if (ch == '\'') { i++; while (i < n && sql[i] != '\'') i++; continue; }
        if (ch == '"') { i++; while (i < n && sql[i] != '"') i++; continue; }
        if (ch == '-' && i + 1 < n && sql[i + 1] == '-') { while (i < n && sql[i] != '\n') i++; continue; }
        if (ch == '/' && i + 1 < n && sql[i + 1] == '*') { i += 2; while (i + 1 < n && !(sql[i] == '*' && sql[i + 1] == '/')) i++; i++; continue; }
        if (ch != ':' || i + 1 >= n) continue;
        char nx = sql[i + 1];
        if (nx == '=') continue; /* PL/SQL assignment */
        size_t j = i + 1;
        char name[64];
        int nl = 0;
        if (nx == '"') { j++; while (j < n && sql[j] != '"' && nl < 63) name[nl++] = sql[j++]; j++; }
        else if (isalnum((unsigned char)nx) || nx == '_') {
            while (j < n && (isalnum((unsigned char)sql[j]) || sql[j] == '_' || sql[j] == '$' || sql[j] == '#') && nl < 63) name[nl++] = (char)toupper((unsigned char)sql[j++]);
        } else continue;
        name[nl] = 0;
        bool dup = false;
        if (plsql) for (int q = 0; q < k; q++) if (strcmp(out[q].name, name) == 0) dup = true;
        if (!dup && k < cap) { memcpy(out[k].name, name, (size_t)nl + 1); k++; }
        i = j - 1;
    }
    return k;
}
typedef enum { XS_ORA_QUERY, XS_ORA_DML, XS_ORA_DDL, XS_ORA_PLSQL } xs_ora_kind;
static xs_ora_kind xs_ora_classify(const char *sql, size_t n) {
    size_t i = 0;
    for (;;) {
        while (i < n && (isspace((unsigned char)sql[i]) || sql[i] == '(')) i++;
        if (i + 1 < n && sql[i] == '-' && sql[i + 1] == '-') { while (i < n && sql[i] != '\n') i++; continue; }
        if (i + 1 < n && sql[i] == '/' && sql[i + 1] == '*') { i += 2; while (i + 1 < n && !(sql[i] == '*' && sql[i + 1] == '/')) i++; i += 2; continue; }
        break;
    }
    char w[16];
    int k = 0;
    while (i < n && isalpha((unsigned char)sql[i]) && k < 15) w[k++] = (char)tolower((unsigned char)sql[i++]);
    w[k] = 0;
    if (!strcmp(w, "select") || !strcmp(w, "with")) return XS_ORA_QUERY;
    if (!strcmp(w, "begin") || !strcmp(w, "declare") || !strcmp(w, "call")) return XS_ORA_PLSQL;
    static const char *ddl[] = {"create", "alter", "drop", "truncate", "grant", "revoke", "analyze", "audit", "comment", "rename", "purge", "flashback", "noaudit", "associate", "disassociate", NULL};
    for (int q = 0; ddl[q]; q++) if (!strcmp(w, ddl[q])) return XS_ORA_DDL;
    return XS_ORA_DML;
}
static void xs_ora_bind_meta(xs_ora_w *w, int type, uint32_t maxsize, int ttcfv) {
    xs_ora_u8(w, (unsigned)type); xs_ora_u8(w, 1); xs_ora_u8(w, 0); xs_ora_u8(w, 0);
    xs_ora_ub(w, maxsize);
    xs_ora_ub(w, 0); xs_ora_ub(w, 0);       /* max elements, cont flags */
    xs_ora_ub(w, 0); xs_ora_ub(w, 0);       /* OID, version */
    bool chars = type == 1 || type == 96 || type == 8;
    xs_ora_ub(w, chars ? 873 : 0);
    xs_ora_u8(w, chars ? 1 : 0);
    xs_ora_ub(w, 0);                        /* LOB prefetch */
    if (ttcfv >= 8) xs_ora_ub(w, 0);        /* oaccolid */
}
static int xs_ora_bind_type(const xs_ora_val *v, uint32_t *maxsize, uint32_t max_str) {
    switch (v->kind) {
    case XS_ORA_INT: case XS_ORA_NUM: case XS_ORA_BOOL: *maxsize = 22; return 2;
    case XS_ORA_DATE: *maxsize = 11; return 180;
    case XS_ORA_BYTES: *maxsize = v->nbytes ? (uint32_t)v->nbytes : 1; return v->nbytes > max_str ? 24 : 23;
    case XS_ORA_STR: { uint32_t n = v->s ? (uint32_t)v->s->len : 0; *maxsize = n ? n : 1; return n > max_str ? 8 : 1; }
    default: *maxsize = 1; return 1;
    }
}
static void xs_ora_bind_value(xs_ora_w *w, const xs_ora_val *v) {
    char num[64];
    switch (v->kind) {
    case XS_ORA_NULL: xs_ora_u8(w, 0); return;
    case XS_ORA_INT: snprintf(num, sizeof num, "%lld", (long long)v->i); xs_ora_put_number(w, num); return;
    case XS_ORA_BOOL: xs_ora_put_number(w, v->i ? "1" : "0"); return;
    case XS_ORA_NUM:
        if (!isfinite(v->d)) xs_ora_fail("NJS-139: value cannot be represented as an Oracle Database number");
        xs_num_to_str(v->d, num);
        xs_ora_put_number(w, num);
        return;
    case XS_ORA_STR: xs_ora_bwl(w, v->s->data, (size_t)v->s->len); return;
    case XS_ORA_BYTES: xs_ora_bwl(w, v->bytes, v->nbytes); return;
    case XS_ORA_DATE: { /* TIMESTAMP from the local wall time, as node-oracledb writes a Date it binds */
        if (!isfinite(v->t)) xs_ora_fail("NJS-140: invalid Date");
        double local = v->t + xs_tz_offset(v->t);
        int64_t days = (int64_t)floor(local / 86400000.0);
        double rem = local - (double)days * 86400000.0;
        int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
        int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        int64_t mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp + (mp < 10 ? 3 : -9);
        y += m <= 2;
        int64_t ms = (int64_t)rem, secs = ms / 1000, frac = ms % 1000;
        uint8_t b[11] = {(uint8_t)(y / 100 + 100), (uint8_t)(y % 100 + 100), (uint8_t)m, (uint8_t)d,
                         (uint8_t)(secs / 3600 + 1), (uint8_t)(secs / 60 % 60 + 1), (uint8_t)(secs % 60 + 1)};
        uint32_t fs = (uint32_t)(frac * 1000000);
        b[7] = (uint8_t)(fs >> 24); b[8] = (uint8_t)(fs >> 16); b[9] = (uint8_t)(fs >> 8); b[10] = (uint8_t)fs;
        xs_ora_bwl(w, b, frac ? 11 : 7);
        return;
    }
    default: xs_ora_u8(w, 0);
    }
}

/* ---- statements ---- */
typedef struct { char *name; int type; uint32_t maxsize; int csfrm; } xs_ora_col;
typedef struct {
    xs_ora_col *cols;
    int ncols;
    xs_sb rows;             /* the rows as JSON values, comma-separated */
    size_t nrows, maxrows;
    bool more;              /* rows remain (no ORA-01403 yet) */
    bool objects;           /* outFormat: objects (else arrays) */
    char **last;            /* the previous row's values as JSON, for columns the server sends only when they change */
} xs_ora_rs;
static void xs_ora_rs_free(xs_ora_rs *r) {
    for (int i = 0; i < r->ncols; i++) { free(r->cols[i].name); if (r->last) free(r->last[i]); }
    free(r->cols);
    free(r->last);
    free(r->rows.data);
}
static void xs_ora_describe(xs_ora_conn *c, xs_ora_rs *r) {
    xs_ora_skip_chunked(c);
    xs_ora_rub(c);                          /* max row size */
    int n = (int)xs_ora_rub(c);
    if (n > 0) xs_ora_r8(c);
    for (int i = 0; i < r->ncols; i++) { free(r->cols[i].name); if (r->last) free(r->last[i]); }
    free(r->cols); free(r->last);
    r->cols = calloc((size_t)(n ? n : 1), sizeof *r->cols);
    r->last = calloc((size_t)(n ? n : 1), sizeof(char *));
    r->ncols = n;
    for (int i = 0; i < n; i++) {
        xs_ora_col *col = &r->cols[i];
        col->type = (int)xs_ora_r8(c);
        xs_ora_r8(c); xs_ora_r8(c); xs_ora_r8(c); /* flags, precision, scale */
        col->maxsize = (uint32_t)xs_ora_rub(c);
        xs_ora_rub(c); xs_ora_rub(c);           /* max elements, cont flags */
        if (xs_ora_rub(c) > 0) xs_ora_skip_chunked(c); /* OID */
        xs_ora_rub(c); xs_ora_rub(c);           /* version, charset */
        col->csfrm = (int)xs_ora_r8(c);
        xs_ora_rub(c);                          /* size */
        if (c->ttcfv >= 8) xs_ora_rub(c);       /* oaccolid */
        xs_ora_r8(c); xs_ora_r8(c);             /* nullable, v7 name length */
        col->name = xs_ora_rub(c) > 0 ? xs_ora_rstr(c) : NULL;
        if (!col->name) col->name = strdup("");
        if (xs_ora_rub(c) > 0) xs_ora_skip_str(c); /* schema */
        if (xs_ora_rub(c) > 0) xs_ora_skip_str(c); /* type name */
        xs_ora_rub(c); xs_ora_rub(c);           /* position, uds flags */
        if (c->ttcfv >= 17) {
            if (xs_ora_rub(c) > 0) xs_ora_skip_str(c);
            if (xs_ora_rub(c) > 0) xs_ora_skip_str(c);
        }
        if (c->ttcfv >= 20 && xs_ora_rub(c) > 0) { /* annotations */
            xs_ora_r8(c);
            int64_t na = xs_ora_rub(c);
            xs_ora_r8(c);
            for (int64_t a = 0; a < na; a++) {
                xs_ora_rub(c); xs_ora_skip_str(c);
                if (xs_ora_rub(c) > 0) xs_ora_skip_str(c);
                xs_ora_rub(c);
            }
            xs_ora_rub(c);
        }
        if (c->ttcfv >= 24) { xs_ora_rub(c); xs_ora_r8(c); xs_ora_r8(c); } /* vector dimensions, format, flags */
    }
    if (xs_ora_rub(c) > 0) xs_ora_skip_chunked(c); /* current date */
    xs_ora_rub(c); xs_ora_rub(c); xs_ora_rub(c); xs_ora_rub(c);
    if (xs_ora_rub(c) > 0) xs_ora_skip_chunked(c);
}
/* one column of a row as JSON (into sb); define: LOBs were defined as LONG / LONG RAW */
static void xs_ora_column(xs_ora_conn *c, const xs_ora_col *col, xs_sb *sb, bool fetch) {
    (void)0;
    int t = col->type;
    if (col->maxsize == 0 && t != 8 && t != 24 && t != 208 && t != 112 && t != 113) { xs_sb_put(sb, "null", 4); return; }
    uint8_t *b = NULL;
    size_t n = 0;
    bool islong = t == 8 || t == 24 || t == 112 || t == 113;
    switch (t) {
    case 1: case 96: case 8: case 112: /* VARCHAR2, CHAR, LONG; CLOB defined as LONG */
        if (xs_ora_rbwl(c, &b, &n)) { xs_str *s = xs_str_new((char *)b, n); xs_sb_str(sb, s); xs_release(s); }
        else xs_sb_put(sb, "null", 4);
        break;
    case 23: case 24: case 113: /* RAW, LONG RAW; BLOB defined as LONG RAW */
        if (xs_ora_rbwl(c, &b, &n)) xs_ora_hex_json(sb, b, n); else xs_sb_put(sb, "null", 4);
        break;
    case 2: case 3: { /* NUMBER, BINARY_INTEGER: a JSON number */
        if (xs_ora_rbwl(c, &b, &n)) { char num[100]; xs_ora_number_text(b, n, num); xs_sb_put(sb, num, strlen(num)); }
        else xs_sb_put(sb, "null", 4);
        break;
    }
    case 100: case 101: { /* BINARY_FLOAT, BINARY_DOUBLE: sortable big-endian IEEE */
        if (xs_ora_rbwl(c, &b, &n) && (n == 4 || n == 8)) {
            uint8_t x[8];
            memcpy(x, b, n);
            if (x[0] & 0x80) x[0] &= 0x7f; else for (size_t k = 0; k < n; k++) x[k] = (uint8_t)~x[k];
            double d;
            if (n == 4) { uint32_t u = (uint32_t)x[0] << 24 | (uint32_t)x[1] << 16 | (uint32_t)x[2] << 8 | x[3]; float f; memcpy(&f, &u, 4); d = f; }
            else { uint64_t u = 0; for (int k = 0; k < 8; k++) u = u << 8 | x[k]; memcpy(&d, &u, 8); }
            char num[40];
            xs_num_to_str(d, num);
            xs_sb_put(sb, num, strlen(num));
        } else xs_sb_put(sb, "null", 4);
        break;
    }
    case 12: case 180: if (xs_ora_rbwl(c, &b, &n)) xs_ora_date_json(sb, b, n, true); else xs_sb_put(sb, "null", 4); break;
    case 181: case 231: if (xs_ora_rbwl(c, &b, &n)) xs_ora_date_json(sb, b, n, false); else xs_sb_put(sb, "null", 4); break;
    case 252: if (xs_ora_rbwl(c, &b, &n)) xs_sb_put(sb, n && b[n - 1] == 1 ? "true" : "false", n && b[n - 1] == 1 ? 4 : 5); else xs_sb_put(sb, "null", 4); break;
    case 11: case 208: { /* ROWID, UROWID: as text */
        if (!fetch || t == 208) { char *s = xs_ora_rstr(c); if (s) { xs_str *x = xs_str_new(s, strlen(s)); xs_sb_str(sb, x); xs_release(x); free(s); } else xs_sb_put(sb, "null", 4); }
        else { unsigned k = xs_ora_r8(c); if (k == 0 || k == 255) xs_sb_put(sb, "null", 4); else { xs_ora_rub(c); xs_ora_rub(c); xs_ora_r8(c); xs_ora_rub(c); xs_ora_rub(c); xs_sb_put(sb, "\"(rowid)\"", 9); } }
        break;
    }
    default: xs_ora_fail("NJS-010: unsupported data type %d in column %s", t, col->name);
    }
    free(b);
    (void)fetch; /* (query rows never carry the actual length that OUT bind values do) */
    if (islong || col->maxsize > c->max_str) { xs_ora_rub(c); xs_ora_rub(c); } /* LONG and longer: null indicator, return code */
}
static void xs_ora_row(xs_ora_conn *c, xs_ora_rs *r, const uint8_t *bitvec, bool fetch) {
    if (r->nrows > 0) xs_sb_put(&r->rows, ",", 1);
    xs_sb_put(&r->rows, r->objects ? "{" : "[", 1);
    for (int i = 0; i < r->ncols; i++) {
        if (i) xs_sb_put(&r->rows, ",", 1);
        if (r->objects) { xs_str *nm = xs_str_new(r->cols[i].name, strlen(r->cols[i].name)); xs_sb_str(&r->rows, nm); xs_release(nm); xs_sb_put(&r->rows, ":", 1); }
        if (bitvec && !(bitvec[i / 8] & (1 << (i % 8))) && r->last[i]) { /* the same value as the previous row */
            xs_sb_put(&r->rows, r->last[i], strlen(r->last[i]));
            continue;
        }
        xs_sb one = {0};
        xs_ora_column(c, &r->cols[i], &one, fetch);
        xs_sb_put(&r->rows, one.data, one.len);
        free(r->last[i]);
        r->last[i] = malloc(one.len + 1);
        if (!r->last[i]) xs_panic("out of memory");
        memcpy(r->last[i], one.data, one.len);
        r->last[i][one.len] = 0;
        free(one.data);
    }
    xs_sb_put(&r->rows, r->objects ? "}" : "]", 1);
    r->nrows++;
}
/* an execute or fetch response: describe info, rows, return parameters, errors */
static void xs_ora_read_result(xs_ora_conn *c, xs_ora_rs *r, xs_ora_status *st, bool fetch) {
    uint8_t *bitvec = NULL;
    size_t bvlen = 0;
    while (!st->end) {
        unsigned t = xs_ora_r8(c);
        switch (t) {
        case 16: xs_ora_describe(c, r); break;
        case 6: { /* row header */
            xs_ora_r8(c); xs_ora_rub(c); xs_ora_rub(c); xs_ora_rub(c); xs_ora_rub(c);
            if (xs_ora_rub(c) > 0) { free(bitvec); xs_ora_rbwl(c, &bitvec, &bvlen); }
            if (xs_ora_rub(c) > 0) xs_ora_skip_chunked(c);
            break;
        }
        case 21: { /* bit vector: which columns this row sends */
            xs_ora_rub(c);
            size_t nb = (size_t)(r->ncols + 7) / 8;
            free(bitvec);
            bitvec = malloc(nb ? nb : 1);
            memcpy(bitvec, xs_ora_rraw(c, nb), nb);
            bvlen = nb;
            break;
        }
        case 7: xs_ora_row(c, r, bvlen ? bitvec : NULL, fetch); free(bitvec); bitvec = NULL; bvlen = 0; break;
        case 8: { /* return parameters */
            int64_t n = xs_ora_rub(c);
            for (int64_t i = 0; i < n; i++) xs_ora_rub(c);
            int64_t k = xs_ora_rub(c);
            if (k > 0) xs_ora_rraw(c, (size_t)k);
            n = xs_ora_rub(c);
            for (int64_t i = 0; i < n; i++) {
                if (xs_ora_rub(c) > 0) xs_ora_skip_str(c);
                if (xs_ora_rub(c) > 0) xs_ora_skip_chunked(c);
                xs_ora_rub(c);
            }
            k = xs_ora_rub(c);
            if (k > 0) xs_ora_rraw(c, (size_t)k);
            break;
        }
        case 11: { /* IO vector (PL/SQL binds): bind directions */
            xs_ora_r8(c);
            int64_t lo = xs_ora_rub(c), hi = xs_ora_rub(c);
            xs_ora_rub(c); xs_ora_rub(c);
            int64_t k = xs_ora_rub(c);
            if (k > 0) xs_ora_rraw(c, (size_t)k);
            k = xs_ora_rub(c);
            if (k > 0) xs_ora_rraw(c, (size_t)k);
            int64_t nb = hi * 256 + lo;
            for (int64_t i = 0; i < nb; i++) if (xs_ora_r8(c) != 32) xs_ora_fail("oracledb: OUT binds are not supported yet");
            break;
        }
        case 19: st->end = true; break; /* flush out binds */
        case 27: xs_ora_fail("oracledb: implicit result sets are not supported yet");
        default:
            if (!xs_ora_common(c, t, st)) xs_ora_fail("oracledb: unexpected message %u in a result", t);
        }
    }
    free(bitvec);
    if (st->num == 1403) { st->num = 0; r->more = false; free(st->message); st->message = NULL; }
}
static void xs_ora_throw_status(xs_ora_status *st) {
    static XS_TLS char msg[1100];
    snprintf(msg, sizeof msg, "%s", st->message ? st->message : "ORA-00000: unknown error");
    free(st->message);
    st->message = NULL;
    xs_throw(xs_Error_from(msg));
}
/* one execute: parse, bind, execute and (queries) prefetch; define: the LOB-as-LONG re-request */
static void xs_ora_send_execute(xs_ora_conn *c, const char *sql, size_t sqln, xs_ora_kind kind, int cursor, bool define,
                                const xs_ora_rs *r, int nb, const xs_ora_val *vals, bool autocommit) {
    xs_ora_w w = {0};
    unsigned options = 0, dml = 0;
    bool query = kind == XS_ORA_QUERY, plsql = kind == XS_ORA_PLSQL;
    int nparams = define ? 0 : nb;
    if (define) options |= 0x10;
    else { dml |= 0x8000; options |= 0x20; }
    if (cursor == 0 || kind == XS_ORA_DDL) options |= 0x01;
    uint32_t iters = 1;
    if (query) { iters = XS_ORA_FETCH; if (!define) options |= 0x40; }
    if (!plsql) options |= 0x8000;
    else if (nparams > 0) options |= 0x400;
    if (nparams > 0) options |= 0x08;
    if (autocommit && !query) options |= 0x100;
    xs_ora_func(c, &w, 94);
    xs_ora_ub(&w, options);
    xs_ora_ub(&w, (uint64_t)cursor);
    if (cursor == 0 || kind == XS_ORA_DDL) { xs_ora_u8(&w, 1); xs_ora_ub(&w, sqln); }
    else { xs_ora_u8(&w, 0); xs_ora_ub(&w, 0); }
    xs_ora_u8(&w, 1); xs_ora_ub(&w, 13);            /* al8i4 */
    xs_ora_u8(&w, 0); xs_ora_u8(&w, 0); xs_ora_u8(&w, 0);
    xs_ora_ub(&w, iters);
    xs_ora_ub(&w, 0x7fffffff);                      /* maximum long size */
    if (nparams == 0) { xs_ora_u8(&w, 0); xs_ora_ub(&w, 0); } else { xs_ora_u8(&w, 1); xs_ora_ub(&w, (uint64_t)nparams); }
    for (int i = 0; i < 5; i++) xs_ora_u8(&w, 0);    /* al8pp, al8txn, al8txl, al8kv, al8kvl */
    if (define) { xs_ora_u8(&w, 1); xs_ora_ub(&w, (uint64_t)r->ncols); } else { xs_ora_u8(&w, 0); xs_ora_ub(&w, 0); }
    xs_ora_ub(&w, 0);                               /* registration id */
    xs_ora_u8(&w, 0); xs_ora_u8(&w, 1); xs_ora_u8(&w, 0); xs_ora_ub(&w, 0); /* objlist, objlen, blv */
    xs_ora_u8(&w, 0); xs_ora_ub(&w, 0); xs_ora_ub(&w, 0);                   /* dnam, dnaml, regid msb */
    xs_ora_u8(&w, 0); xs_ora_ub(&w, 0); xs_ora_u8(&w, 0);                   /* DML row counts */
    if (c->ttcfv >= 8) {
        xs_ora_u8(&w, 0); xs_ora_ub(&w, 0); xs_ora_u8(&w, 0); xs_ora_ub(&w, 0); xs_ora_u8(&w, 0);
        if (c->ttcfv >= 9) { xs_ora_u8(&w, 0); xs_ora_ub(&w, 0); }
    }
    if (cursor == 0 || kind == XS_ORA_DDL) { xs_ora_bwl(&w, sql, sqln); xs_ora_ub(&w, 1); }
    else xs_ora_ub(&w, 0);
    if (query) xs_ora_ub(&w, cursor == 0 ? 0 : iters); else xs_ora_ub(&w, 1);
    for (int i = 0; i < 5; i++) xs_ora_ub(&w, 0);    /* al8i4[2..6] */
    xs_ora_ub(&w, query ? 1 : 0);
    xs_ora_ub(&w, 0);
    xs_ora_ub(&w, dml);
    xs_ora_ub(&w, 0); xs_ora_ub(&w, 0); xs_ora_ub(&w, 0);
    if (define) {
        for (int i = 0; i < r->ncols; i++) {
            const xs_ora_col *col = &r->cols[i];
            int t = col->type == 112 ? 8 : col->type == 113 ? 24 : col->type;
            uint32_t mx = (col->type == 112 || col->type == 113) ? 0x7fffffff : col->maxsize;
            xs_ora_u8(&w, (unsigned)t); xs_ora_u8(&w, 1); xs_ora_u8(&w, 0); xs_ora_u8(&w, 0);
            xs_ora_ub(&w, mx);
            xs_ora_ub(&w, 0); xs_ora_ub(&w, 0); xs_ora_ub(&w, 0); xs_ora_ub(&w, 0);
            bool chars = t == 1 || t == 96 || t == 8;
            xs_ora_ub(&w, chars ? 873 : 0);
            xs_ora_u8(&w, chars ? (unsigned)(col->csfrm ? col->csfrm : 1) : 0);
            xs_ora_ub(&w, 0);
            if (c->ttcfv >= 8) xs_ora_ub(&w, 0);
        }
    } else if (nparams > 0) {
        int *types = calloc((size_t)nparams, sizeof(int));
        bool anylong = false;
        for (int i = 0; i < nparams; i++) {
            uint32_t mx;
            types[i] = xs_ora_bind_type(&vals[i], &mx, c->max_str);
            if (types[i] == 8 || types[i] == 24) anylong = true;
            xs_ora_bind_meta(&w, types[i], mx, c->ttcfv);
        }
        xs_ora_u8(&w, 7); /* the row of values: LONG values after the others */
        for (int i = 0; i < nparams; i++) if (!(anylong && (types[i] == 8 || types[i] == 24))) xs_ora_bind_value(&w, &vals[i]);
        if (anylong) for (int i = 0; i < nparams; i++) if (types[i] == 8 || types[i] == 24) xs_ora_bind_value(&w, &vals[i]);
        free(types);
    }
    bool ok = xs_ora_send(c, &w);
    free(w.p);
    if (!ok) xs_ora_fail("NJS-500: connection to the Oracle Database was broken");
}
static xs_str *xs_ora_execute(xs_ora_conn *c, xs_str *sql, int nb, const xs_ora_val *vals, bool objects, bool autocommit, size_t maxrows) {
    xs_ora_kind kind = xs_ora_classify(sql->data, (size_t)sql->len);
    xs_ora_rs r = {0};
    r.objects = objects;
    r.more = true;
    r.maxrows = maxrows;
    xs_ora_status st = {0};
    xs_ora_send_execute(c, sql->data, (size_t)sql->len, kind, 0, false, &r, nb, vals, autocommit);
    xs_ora_start_response(c);
    xs_ora_read_result(c, &r, &st, false);
    int cursor = st.cursor;
    if (st.num != 0) { xs_ora_close_later(c, cursor); xs_ora_rs_free(&r); xs_ora_throw_status(&st); }
    int64_t affected = st.row_count;
    if (kind == XS_ORA_QUERY) {
        bool lobs = false;
        for (int i = 0; i < r.ncols; i++) if (r.cols[i].type == 112 || r.cols[i].type == 113) lobs = true;
        if (lobs && r.nrows == 0 && r.more) { /* CLOB/BLOB: define them as LONG / LONG RAW and fetch */
            st.end = false;
            xs_ora_send_execute(c, sql->data, (size_t)sql->len, kind, cursor, true, &r, 0, NULL, false);
            xs_ora_start_response(c);
            xs_ora_read_result(c, &r, &st, true);
            if (st.num != 0) { xs_ora_close_later(c, cursor); xs_ora_rs_free(&r); xs_ora_throw_status(&st); }
        }
        while (r.more && (r.maxrows == 0 || r.nrows < r.maxrows)) {
            xs_ora_w w = {0};
            xs_ora_func(c, &w, 5);
            xs_ora_ub(&w, (uint64_t)cursor);
            xs_ora_ub(&w, XS_ORA_FETCH);
            bool ok = xs_ora_send(c, &w);
            free(w.p);
            if (!ok) { xs_ora_rs_free(&r); xs_ora_fail("NJS-500: connection to the Oracle Database was broken"); }
            st.end = false;
            xs_ora_start_response(c);
            xs_ora_read_result(c, &r, &st, true);
            if (st.num != 0) { xs_ora_close_later(c, cursor); xs_ora_rs_free(&r); xs_ora_throw_status(&st); }
        }
    }
    xs_ora_close_later(c, cursor);
    xs_sb sb = {0};
    if (kind == XS_ORA_QUERY) {
        xs_sb_put(&sb, "{\"rows\":[", 9);
        size_t keep = r.maxrows && r.nrows > r.maxrows ? r.maxrows : r.nrows;
        if (keep == r.nrows) xs_sb_put(&sb, r.rows.data ? r.rows.data : "", r.rows.len);
        else { /* (cut at maxRows: the rows are comma-separated top-level values) */
            size_t depth = 0, seen = 0, k = 0;
            bool instr = false;
            for (; k < r.rows.len; k++) {
                char ch = r.rows.data[k];
                if (instr) { if (ch == '\\') k++; else if (ch == '"') instr = false; continue; }
                if (ch == '"') instr = true;
                else if (ch == '{' || ch == '[') depth++;
                else if (ch == '}' || ch == ']') { if (--depth == 0 && ++seen == keep) { k++; break; } }
            }
            xs_sb_put(&sb, r.rows.data, k);
        }
        xs_sb_put(&sb, "],\"metaData\":[", 14);
        for (int i = 0; i < r.ncols; i++) {
            if (i) xs_sb_put(&sb, ",", 1);
            xs_sb_put(&sb, "{\"name\":", 8);
            xs_str *nm = xs_str_new(r.cols[i].name, strlen(r.cols[i].name));
            xs_sb_str(&sb, nm);
            xs_release(nm);
            xs_sb_put(&sb, "}", 1);
        }
        xs_sb_put(&sb, "]}", 2);
    } else {
        char tail[64];
        int n = snprintf(tail, sizeof tail, "{\"rows\":[],\"metaData\":[],\"rowsAffected\":%lld}", (long long)affected);
        xs_sb_put(&sb, tail, (size_t)n);
    }
    xs_ora_rs_free(&r);
    free(st.message);
    return xs_sb_finish(&sb);
}

/* the values: positional (an array) in placeholder order, or named (JSON object) by placeholder name */
static void xs_ora_value_of(void *v, xs_ora_val *out) {
    memset(out, 0, sizeof *out);
    if (!v) { out->kind = XS_ORA_NULL; return; }
    void (*f)(void *) = xs_type_of(v)->free;
    if (f == xs_str_free) { out->kind = XS_ORA_STR; out->s = v; }
    else if (f == xs_box_int_free) { out->kind = XS_ORA_INT; out->i = ((xs_box_i *)v)->v; }
    else if (f == xs_box_num_free) { out->kind = XS_ORA_NUM; out->d = ((xs_box_d *)v)->v; }
    else if (f == xs_box_bool_free) { out->kind = XS_ORA_BOOL; out->i = ((xs_box_i *)v)->v; }
    else if (f == xs_Date_free) { out->kind = XS_ORA_DATE; out->t = ((xs_Date *)v)->t; }
    else if (f == xs_u8_free) { out->kind = XS_ORA_BYTES; out->bytes = ((xs_Uint8Array *)v)->data; out->nbytes = (size_t)((xs_Uint8Array *)v)->f_length; }
    else xs_ora_fail("NJS-044: bind value must be a string, number, boolean, Date, Uint8Array or null");
}
XS_NOINLINE xs_str *xn_oracledb_NativePool_execute(xn_oracledb_NativePool *p, int64_t h, xs_str *sql, xs_arr_ref *values, xs_str *named, bool objects, bool autocommit, int64_t maxrows) {
    xs_ora_conn *c = xs_ora_held(p, h);
    xs_ora_kind kind = xs_ora_classify(sql->data, (size_t)sql->len);
    xs_ora_ph ph[512];
    int nph = xs_ora_placeholders(sql->data, (size_t)sql->len, ph, 512, kind == XS_ORA_PLSQL);
    xs_ora_val *vals = calloc((size_t)(nph ? nph : 1), sizeof *vals);
    xs_str **owned = calloc((size_t)(nph ? nph : 1), sizeof *owned);
    if (!vals || !owned) xs_panic("out of memory");
    if (named && named->len > 2) {
        xs_jarena_reset();
        xs_jv *o = xs_json_parse(named);
        for (int i = 0; i < nph; i++) {
            xs_jv *v = NULL;
            if (o && o->kind == XJ_OBJ)
                for (size_t k = 0; k < o->n; k++) if (strcasecmp(o->keys[k], ph[i].name) == 0) { v = o->items[k]; break; }
            if (!v) { xs_jv_free(o); free(vals); for (int q = 0; q < i; q++) if (owned[q]) xs_release(owned[q]); free(owned); xs_ora_fail("NJS-097: no bind placeholder named: :%s", ph[i].name); }
            if (v->kind == XJ_NULL) vals[i].kind = XS_ORA_NULL;
            else if (v->kind == XJ_BOOL) { vals[i].kind = XS_ORA_BOOL; vals[i].i = v->b; }
            else if (v->kind == XJ_NUM) { vals[i].kind = XS_ORA_NUM; vals[i].d = v->num; }
            else if (v->kind == XJ_STR) { vals[i].kind = XS_ORA_STR; owned[i] = xs_str_new(v->str, v->slen); vals[i].s = owned[i]; }
            else { xs_jv_free(o); xs_ora_fail("NJS-044: named bind %s must be a string, number, boolean or null", ph[i].name); }
        }
        xs_jv_free(o);
    } else {
        int nv = values ? (int)values->len : 0;
        if (nv != nph) { free(vals); free(owned); xs_ora_fail("NJS-098: %d positional bind values are required but %d were provided", nph, nv); }
        for (int i = 0; i < nph; i++) xs_ora_value_of(values->data[i], &vals[i]);
    }
    xs_str *json = xs_ora_execute(c, sql, nph, vals, objects, autocommit, maxrows > 0 ? (size_t)maxrows : 0);
    for (int i = 0; i < nph; i++) if (owned[i]) xs_release(owned[i]);
    free(owned);
    free(vals);
    return json;
}
static void xs_ora_simple(xn_oracledb_NativePool *p, int64_t h, unsigned code) {
    xs_ora_conn *c = xs_ora_held(p, h);
    xs_ora_w w = {0};
    xs_ora_func(c, &w, code);
    bool ok = xs_ora_send(c, &w);
    free(w.p);
    if (!ok) xs_ora_fail("NJS-500: connection to the Oracle Database was broken");
    xs_ora_status st = {0};
    xs_ora_start_response(c);
    while (!st.end) { unsigned t = xs_ora_r8(c); if (!xs_ora_common(c, t, &st)) xs_ora_fail("oracledb: unexpected message %u", t); }
    if (st.num != 0) xs_ora_throw_status(&st);
}
XS_NOINLINE void xn_oracledb_NativePool_commit(xn_oracledb_NativePool *p, int64_t h) { xs_ora_simple(p, h, 14); }
XS_NOINLINE void xn_oracledb_NativePool_rollback(xn_oracledb_NativePool *p, int64_t h) { xs_ora_simple(p, h, 15); }
XS_NOINLINE void xn_oracledb_NativePool_ping(xn_oracledb_NativePool *p, int64_t h) { xs_ora_simple(p, h, 147); }
