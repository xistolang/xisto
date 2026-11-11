/* pg (node-postgres contract): a PostgreSQL client, frontend/backend protocol 3.0. Builds on the coroutine
   runtime (sockets) and xisto:crypto (SCRAM). The result goes back to xisto as JSON text
   {"rows":[...],"rowCount":n} with node-postgres's type mapping (int8 and numeric as strings).

   - Login: trust, cleartext, MD5-less SCRAM-SHA-256 (the default since PostgreSQL 14).
   - Queries: the extended protocol with an unnamed statement, text parameters and text results, sent in one
     write (Parse, Bind, Describe, Execute, Sync), as node-postgres does for a query with values.
   - Inside a request handler every wait yields the coroutine; elsewhere it blocks.
   - Connections are pooled per worker thread (a coroutine and its socket stay on one event loop); when all are
     busy, the coroutine parks in a FIFO and the next release wakes it.
   - The result is kept as raw bytes until it is complete, then turned into rows without yielding: the JSON
     arena that the rows are built in is per thread and other coroutines use it while this one waits. */

typedef struct xs_pg_conn {
    int fd;
    char *in;               /* bytes read and not yet consumed */
    size_t in_len, in_cap, in_off;
    bool broken;
    struct xs_pg_conn *next;
} xs_pg_conn;
typedef struct xs_pg_waiter { xs_coro *co; xs_pg_conn *got; struct xs_pg_waiter *next; } xs_pg_waiter;
#define XS_PG_MAX_POOLS 8
typedef struct { xs_pg_conn *idle; int open; xs_pg_waiter *head, *tail; } xs_pg_local;
typedef struct xn_pg_NativePool {
    xs_hdr hdr;
    char *host, *user, *password, *database;
    int port, max, id;
} xn_pg_NativePool;
static XS_TLS xs_pg_local xs_pg_locals[XS_PG_MAX_POOLS];
static int xs_pg_npools;
static int xs_pg_workers(void) {
    const char *w = getenv("XISTO_WORKERS");
    int n = w ? atoi(w) : 1;
    return n < 1 ? 1 : n;
}

static void xn_pg_NativePool_free(void *p) {
    xn_pg_NativePool *pool = p;
    free(pool->host); free(pool->user); free(pool->database);
    if (pool->password) { memset(pool->password, 0, strlen(pool->password)); free(pool->password); }
    xs_obj_free(p);
}
static void xn_pg_NativePool_print(void *p) { (void)p; fputs("Pool {}", stdout); }
static char *xn_pg_cfg_str(xs_jv *cfg, const char *key, const char *dflt) {
    xs_jv *v = cfg ? xs_json_field_opt(cfg, key) : NULL;
    if (v && v->kind == XJ_STR) return xs_strndup_(v->str, v->slen);
    return dflt ? xs_strndup_(dflt, strlen(dflt)) : NULL;
}
static int64_t xn_pg_cfg_int(xs_jv *cfg, const char *key, int64_t dflt) {
    xs_jv *v = cfg ? xs_json_field_opt(cfg, key) : NULL;
    return v && v->kind == XJ_NUM ? (int64_t)v->num : dflt;
}
/* new Pool({ host, port, user, password, database, max }): the config arrives as JSON */
XS_NOINLINE xn_pg_NativePool *xn_pg_NativePool_new(xs_str *config) {
    xn_pg_NativePool *p = xs_obj_alloc(sizeof *p, xn_pg_NativePool_free, xn_pg_NativePool_print);
    xs_jarena_reset();
    xs_jv *cfg = xs_json_parse(config);
    const char *u = getenv("USER");
    p->host = xn_pg_cfg_str(cfg, "host", "localhost");
    p->user = xn_pg_cfg_str(cfg, "user", u ? u : "postgres");
    p->password = xn_pg_cfg_str(cfg, "password", NULL);
    p->database = xn_pg_cfg_str(cfg, "database", p->user);
    p->port = (int)xn_pg_cfg_int(cfg, "port", 5432);
    int per = (int)xn_pg_cfg_int(cfg, "max", 10);
    xs_jv_free(cfg);
    p->max = (per + xs_pg_workers() - 1) / xs_pg_workers();
    if (p->max < 1) p->max = 1;
    p->id = __atomic_fetch_add(&xs_pg_npools, 1, __ATOMIC_RELAXED);
    if (p->id >= XS_PG_MAX_POOLS) xs_panic("pg: too many pools (8)");
    return p;
}

/* ---- wire helpers ---- */
typedef struct { char *p; size_t len, cap; } xs_pg_buf;
static void xs_pg_put(xs_pg_buf *b, const void *s, size_t n) {
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2 + 64;
        b->p = realloc(b->p, b->cap);
        if (!b->p) xs_panic("out of memory");
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
}
static void xs_pg_put32(xs_pg_buf *b, int32_t v) { uint8_t x[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; xs_pg_put(b, x, 4); }
static void xs_pg_put16(xs_pg_buf *b, int16_t v) { uint8_t x[2] = {(uint8_t)(v >> 8), (uint8_t)v}; xs_pg_put(b, x, 2); }
static void xs_pg_putz(xs_pg_buf *b, const char *s) { xs_pg_put(b, s, strlen(s) + 1); }
/* starts a message: type byte and a length placeholder; returns where the length goes */
static size_t xs_pg_begin(xs_pg_buf *b, char type) { if (type) xs_pg_put(b, &type, 1); size_t at = b->len; xs_pg_put32(b, 0); return at; }
static void xs_pg_end(xs_pg_buf *b, size_t at) {
    uint32_t n = (uint32_t)(b->len - at);
    b->p[at] = (char)(n >> 24); b->p[at + 1] = (char)(n >> 16); b->p[at + 2] = (char)(n >> 8); b->p[at + 3] = (char)n;
}
static uint32_t xs_pg_get32(const char *p) { const uint8_t *u = (const uint8_t *)p; return (uint32_t)u[0] << 24 | (uint32_t)u[1] << 16 | (uint32_t)u[2] << 8 | u[3]; }
static uint16_t xs_pg_get16(const char *p) { const uint8_t *u = (const uint8_t *)p; return (uint16_t)(u[0] << 8 | u[1]); }

static bool xs_pg_send(xs_pg_conn *c, const xs_pg_buf *b) {
    if (!xs_write_all(c->fd, b->p, b->len)) { c->broken = true; return false; }
    return true;
}
/* the next backend message: type, body and body length; false when the connection broke */
static bool xs_pg_next(xs_pg_conn *c, char *type, const char **body, uint32_t *blen) {
    for (;;) {
        size_t avail = c->in_len - c->in_off;
        if (avail >= 5) {
            uint32_t n = xs_pg_get32(c->in + c->in_off + 1);
            if (n < 4) { c->broken = true; return false; }
            if (avail >= 1 + (size_t)n) {
                *type = c->in[c->in_off];
                *body = c->in + c->in_off + 5;
                *blen = n - 4;
                c->in_off += 1 + n;
                return true;
            }
            if (c->in_off + 1 + n > c->in_cap) { /* make room: move the partial message to the front, grow */
                memmove(c->in, c->in + c->in_off, avail);
                c->in_len = avail;
                c->in_off = 0;
                if (1 + (size_t)n > c->in_cap) { c->in_cap = 1 + (size_t)n + 16384; c->in = realloc(c->in, c->in_cap); if (!c->in) xs_panic("out of memory"); }
            }
        } else if (c->in_off > 0 && c->in_off == c->in_len) {
            c->in_off = c->in_len = 0;
        }
        if (c->in_len == c->in_cap) {
            if (c->in_off > 0) { memmove(c->in, c->in + c->in_off, c->in_len - c->in_off); c->in_len -= c->in_off; c->in_off = 0; }
            else { c->in_cap = c->in_cap ? c->in_cap * 2 : 16384; c->in = realloc(c->in, c->in_cap); if (!c->in) xs_panic("out of memory"); }
        }
        ssize_t r = read(c->fd, c->in + c->in_len, c->in_cap - c->in_len);
        if (r > 0) { c->in_len += (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && errno == EAGAIN) { xs_wait_fd(c->fd, EVFILT_READ); continue; }
        c->broken = true;
        return false;
    }
}
/* "SERROR\0Vxxx\0C23505\0Mduplicate key...\0..." -> the M field */
static void xs_pg_error_text(const char *body, uint32_t blen, char *out, size_t cap) {
    const char *p = body, *end = body + blen;
    snprintf(out, cap, "postgres error");
    while (p < end && *p) {
        char f = *p++;
        size_t n = strnlen(p, (size_t)(end - p));
        if (f == 'M') { snprintf(out, cap, "%.*s", (int)n, p); return; }
        p += n + 1;
    }
}

/* ---- SCRAM-SHA-256 (RFC 7677, as PostgreSQL uses it: empty user name in the client-first message) ---- */
static bool xs_pg_scram(xs_pg_conn *c, const char *password, char *err, size_t errcap) {
    uint8_t nonce_raw[18];
    char nonce[32];
    xs_random_bytes(nonce_raw, sizeof nonce_raw);
    xs_b64_encode(nonce_raw, sizeof nonce_raw, nonce, false);
    char first_bare[64];
    snprintf(first_bare, sizeof first_bare, "n=,r=%s", nonce);
    xs_pg_buf b = {0};
    size_t at = xs_pg_begin(&b, 'p');
    xs_pg_putz(&b, "SCRAM-SHA-256");
    char first[80];
    int flen = snprintf(first, sizeof first, "n,,%s", first_bare);
    xs_pg_put32(&b, flen);
    xs_pg_put(&b, first, (size_t)flen);
    xs_pg_end(&b, at);
    bool ok = xs_pg_send(c, &b);
    b.len = 0;
    char type;
    const char *body;
    uint32_t blen;
    if (!ok || !xs_pg_next(c, &type, &body, &blen)) { snprintf(err, errcap, "connection lost during login"); free(b.p); return false; }
    if (type == 'E') { xs_pg_error_text(body, blen, err, errcap); free(b.p); return false; }
    if (type != 'R' || blen < 4 || xs_pg_get32(body) != 11) { snprintf(err, errcap, "unexpected SASL message"); free(b.p); return false; }
    char server_first[512];
    snprintf(server_first, sizeof server_first, "%.*s", (int)(blen - 4), body + 4);
    /* r=<nonce>,s=<salt>,i=<iterations> */
    char *r = strstr(server_first, "r="), *s = strstr(server_first, ",s="), *i = strstr(server_first, ",i=");
    if (!r || !s || !i || strncmp(r + 2, nonce, strlen(nonce)) != 0) { snprintf(err, errcap, "bad SCRAM server message"); free(b.p); return false; }
    char full_nonce[256], salt64[256];
    snprintf(full_nonce, sizeof full_nonce, "%.*s", (int)(s - r - 2), r + 2);
    snprintf(salt64, sizeof salt64, "%.*s", (int)(i - s - 3), s + 3);
    long iters = atol(i + 3);
    uint8_t salt[192];
    long slen = xs_b64_decode(salt64, strlen(salt64), salt);
    if (slen <= 0 || iters < 1 || iters > 10000000) { snprintf(err, errcap, "bad SCRAM parameters"); free(b.p); return false; }
    uint8_t salted[32], client_key[32], stored_key[32], server_key[32], sig[32], proof[32];
    xs_pbkdf2_256(password, strlen(password), salt, (size_t)slen, (uint32_t)iters, salted);
    xs_hmac256(salted, 32, "Client Key", 10, client_key);
    xs_sha256_hash(client_key, 32, stored_key);
    char final_bare[320], auth_msg[1024];
    snprintf(final_bare, sizeof final_bare, "c=biws,r=%s", full_nonce);
    int alen = snprintf(auth_msg, sizeof auth_msg, "%s,%s,%s", first_bare, server_first, final_bare);
    xs_hmac256(stored_key, 32, auth_msg, (size_t)alen, sig);
    for (int k = 0; k < 32; k++) proof[k] = client_key[k] ^ sig[k];
    char proof64[64], final[400];
    xs_b64_encode(proof, 32, proof64, false);
    int fl = snprintf(final, sizeof final, "%s,p=%s", final_bare, proof64);
    at = xs_pg_begin(&b, 'p');
    xs_pg_put(&b, final, (size_t)fl);
    xs_pg_end(&b, at);
    ok = xs_pg_send(c, &b);
    free(b.p);
    if (!ok || !xs_pg_next(c, &type, &body, &blen)) { snprintf(err, errcap, "connection lost during login"); return false; }
    if (type == 'E') { xs_pg_error_text(body, blen, err, errcap); return false; }
    if (type != 'R' || blen < 4 || xs_pg_get32(body) != 12) { snprintf(err, errcap, "unexpected SASL final message"); return false; }
    /* the server proves it knows the password too: v=<ServerSignature> */
    xs_hmac256(salted, 32, "Server Key", 10, server_key);
    xs_hmac256(server_key, 32, auth_msg, (size_t)alen, sig);
    char want[64];
    xs_b64_encode(sig, 32, want, false);
    if (blen - 4 < 2 + strlen(want) || memcmp(body + 4, "v=", 2) != 0 || memcmp(body + 6, want, strlen(want)) != 0) {
        snprintf(err, errcap, "the server's SCRAM signature does not match");
        return false;
    }
    memset(salted, 0, sizeof salted);
    memset(client_key, 0, sizeof client_key);
    return true;
}

static void xs_pg_close(xs_pg_conn *c) {
    if (c->fd >= 0) {
        if (!c->broken) { char x[5] = {'X', 0, 0, 0, 4}; ssize_t w = write(c->fd, x, 5); (void)w; } /* Terminate */
        close(c->fd);
    }
    free(c->in);
    free(c);
}
static xs_pg_conn *xs_pg_open(xn_pg_NativePool *p, char *err, size_t errcap) {
    xs_pg_conn *c = calloc(1, sizeof *c);
    if (!c) xs_panic("out of memory");
    c->fd = xs_connect(p->host, p->port);
    if (c->fd < 0) { snprintf(err, errcap, "connect ECONNREFUSED %s:%d", p->host, p->port); free(c); return NULL; }
    xs_pg_buf b = {0};
    size_t at = xs_pg_begin(&b, 0);
    xs_pg_put32(&b, 196608); /* protocol 3.0 */
    xs_pg_putz(&b, "user"); xs_pg_putz(&b, p->user);
    xs_pg_putz(&b, "database"); xs_pg_putz(&b, p->database);
    xs_pg_putz(&b, "client_encoding"); xs_pg_putz(&b, "UTF8");
    xs_pg_put(&b, "", 1);
    xs_pg_end(&b, at);
    bool ok = xs_pg_send(c, &b);
    free(b.p);
    char type;
    const char *body;
    uint32_t blen;
    while (ok && xs_pg_next(c, &type, &body, &blen)) {
        if (type == 'R') {
            uint32_t code = blen >= 4 ? xs_pg_get32(body) : 99;
            if (code == 0) continue;
            if (code == 3) { /* cleartext password */
                if (!p->password) { snprintf(err, errcap, "the server asks for a password"); break; }
                xs_pg_buf pb = {0};
                size_t a2 = xs_pg_begin(&pb, 'p');
                xs_pg_putz(&pb, p->password);
                xs_pg_end(&pb, a2);
                ok = xs_pg_send(c, &pb);
                free(pb.p);
                continue;
            }
            if (code == 10) { /* SASL: only SCRAM-SHA-256 is offered by PostgreSQL without channel binding */
                if (!p->password) { snprintf(err, errcap, "the server asks for a password"); break; }
                if (!xs_pg_scram(c, p->password, err, errcap)) break;
                continue;
            }
            snprintf(err, errcap, "unsupported authentication method %u (use scram-sha-256)", code);
            break;
        }
        if (type == 'E') { xs_pg_error_text(body, blen, err, errcap); break; }
        if (type == 'Z') return c; /* ReadyForQuery */
        /* ParameterStatus, BackendKeyData, NoticeResponse: nothing to keep */
    }
    if (!err[0]) snprintf(err, errcap, "connection lost during login");
    c->broken = true;
    xs_pg_close(c);
    return NULL;
}

static xs_pg_conn *xs_pg_acquire(xn_pg_NativePool *p) {
    xs_pg_local *l = &xs_pg_locals[p->id];
    if (l->idle) { xs_pg_conn *c = l->idle; l->idle = c->next; c->next = NULL; return c; }
    if (l->open < p->max || !xs_cur_coro) {
        l->open++;
        char err[256] = {0};
        xs_pg_conn *c = xs_pg_open(p, err, sizeof err);
        if (!c) {
            l->open--;
            if (l->head) { /* the slot is free again: the first one waiting opens its own (or fails the same way) */
                xs_pg_waiter *w = l->head;
                l->head = w->next;
                if (!l->head) l->tail = NULL;
                w->got = NULL;
                xs_ready_push(w->co);
            }
            static XS_TLS char msg[300];
            snprintf(msg, sizeof msg, "%s", err);
            xs_throw(xs_Error_from(msg));
        }
        return c;
    }
    /* on the heap: tasks share one stack, and a parked task's stack is not where it was while others run (a waiter on
       it was overwritten with Promise.all over more queries than the pool has connections) */
    xs_pg_waiter *w = calloc(1, sizeof *w);
    w->co = xs_cur_coro;
    if (l->tail) l->tail->next = w; else l->head = w;
    l->tail = w;
    xs_coro_yield(); /* resumed by xs_pg_release with a connection */
    xs_pg_conn *got = w->got;
    free(w);
    if (!got) return xs_pg_acquire(p); /* (the release could not open one in its place: try again) */
    return got;
}
static void xs_pg_release(xn_pg_NativePool *p, xs_pg_conn *c) {
    xs_pg_local *l = &xs_pg_locals[p->id];
    if (c->broken) { xs_pg_close(c); l->open--; c = NULL; }
    if (l->head) { /* hand it to the first waiter (or let the waiter open a new one) */
        xs_pg_waiter *w = l->head;
        l->head = w->next;
        if (!l->head) l->tail = NULL;
        if (!c) { char err[256] = {0}; l->open++; c = xs_pg_open(p, err, sizeof err); if (!c) l->open--; }
        w->got = c;
        xs_ready_push(w->co);
        return;
    }
    if (c) { c->next = l->idle; l->idle = c; }
}

/* ---- a query: the raw result, decoded into rows by the caller ---- */
typedef struct {
    int ncols;
    char **names;           /* column names */
    uint32_t *oids;         /* column types */
    char *data;             /* every DataRow body, back to back */
    size_t data_len, data_cap;
    size_t *rows;           /* offset of each row in data */
    size_t nrows, rows_cap;
    int64_t row_count;      /* from the command tag: rows read, inserted, updated or deleted */
} xs_pg_res;
static void xs_pg_res_free(xs_pg_res *r) {
    if (!r) return;
    for (int i = 0; i < r->ncols; i++) free(r->names[i]);
    free(r->names); free(r->oids); free(r->data); free(r->rows); free(r);
}
/* parameters: the same tagged values as node:sqlite (xs_sqarg); text format on the wire */
static xs_pg_res *xs_pg_query(xn_pg_NativePool *p, xs_str *sql, int n, const xs_sqarg *args) {
    xs_pg_buf b = {0};
    size_t at = xs_pg_begin(&b, 'P'); /* Parse: unnamed statement, parameter types inferred */
    xs_pg_put(&b, "", 1);
    xs_pg_put(&b, sql->data, (size_t)sql->len);
    xs_pg_put(&b, "", 1);
    xs_pg_put16(&b, 0);
    xs_pg_end(&b, at);
    at = xs_pg_begin(&b, 'B'); /* Bind */
    xs_pg_put(&b, "", 1);
    xs_pg_put(&b, "", 1);
    xs_pg_put16(&b, 0);        /* all parameters in text */
    xs_pg_put16(&b, (int16_t)n);
    for (int i = 0; i < n; i++) {
        char num[40];
        const char *s = NULL;
        size_t len = 0;
        switch (args[i].kind) {
        case 0: break;
        case 1: len = xs_itoa(args[i].i, num); s = num; break;
        case 2: xs_num_to_str(args[i].d, num); s = num; len = strlen(num); break;
        case 4: s = args[i].i ? "true" : "false"; len = strlen(s); break;
        default: if (args[i].s) { s = args[i].s->data; len = (size_t)args[i].s->len; } break;
        }
        if (!s) { xs_pg_put32(&b, -1); continue; }
        xs_pg_put32(&b, (int32_t)len);
        xs_pg_put(&b, s, len);
    }
    xs_pg_put16(&b, 0);        /* all results in text */
    xs_pg_end(&b, at);
    at = xs_pg_begin(&b, 'D'); xs_pg_put(&b, "P", 1); xs_pg_put(&b, "", 1); xs_pg_end(&b, at); /* Describe the portal */
    at = xs_pg_begin(&b, 'E'); xs_pg_put(&b, "", 1); xs_pg_put32(&b, 0); xs_pg_end(&b, at);    /* Execute, all rows */
    at = xs_pg_begin(&b, 'S'); xs_pg_end(&b, at);                                               /* Sync */

    xs_pg_conn *c = xs_pg_acquire(p);
    xs_pg_res *r = calloc(1, sizeof *r);
    if (!r) xs_panic("out of memory");
    char err[512] = {0};
    bool ok = xs_pg_send(c, &b);
    free(b.p);
    char type;
    const char *body;
    uint32_t blen;
    while (ok && xs_pg_next(c, &type, &body, &blen)) {
        switch (type) {
        case 'T': { /* RowDescription */
            r->ncols = xs_pg_get16(body);
            r->names = calloc((size_t)r->ncols + 1, sizeof(char *));
            r->oids = calloc((size_t)r->ncols + 1, sizeof(uint32_t));
            const char *q = body + 2;
            for (int k = 0; k < r->ncols; k++) {
                size_t nl = strlen(q);
                r->names[k] = xs_strndup_(q, nl);
                q += nl + 1;
                r->oids[k] = xs_pg_get32(q + 6);
                q += 18;
            }
            break;
        }
        case 'D': { /* DataRow: kept as is */
            if (r->nrows == r->rows_cap) { r->rows_cap = r->rows_cap ? r->rows_cap * 2 : 16; r->rows = realloc(r->rows, r->rows_cap * sizeof(size_t)); }
            if (r->data_len + blen > r->data_cap) { r->data_cap = (r->data_len + blen) * 2 + 4096; r->data = realloc(r->data, r->data_cap); }
            if (!r->rows || !r->data) xs_panic("out of memory");
            r->rows[r->nrows++] = r->data_len;
            memcpy(r->data + r->data_len, body, blen);
            r->data_len += blen;
            break;
        }
        case 'C': { /* CommandComplete: "SELECT 3", "INSERT 0 1", "UPDATE 2", "DELETE 1" */
            const char *sp = NULL;
            for (uint32_t k = 0; k < blen; k++) if (body[k] == ' ') sp = body + k;
            r->row_count = sp ? atoll(sp + 1) : 0;
            break;
        }
        case 'E': if (!err[0]) xs_pg_error_text(body, blen, err, sizeof err); break;
        case 'Z': goto done; /* ReadyForQuery */
        default: break;      /* ParseComplete, BindComplete, NoData, NoticeResponse... */
        }
    }
    if (!err[0]) snprintf(err, sizeof err, "Connection terminated unexpectedly");
done:
    xs_pg_release(p, c);
    if (err[0]) {
        xs_pg_res_free(r);
        static XS_TLS char msg[520];
        snprintf(msg, sizeof msg, "%s", err);
        xs_throw(xs_Error_from(msg));
    }
    return r;
}

/* the rows as node-postgres returns them, written as JSON: bool, int2/int4/oid/float4/float8 as numbers,
   int8 and numeric as strings, int4[]/text[] as arrays, NULL as null, the rest as text */
static void xn_pg_json_value(xs_sb *sb, uint32_t oid, const char *q, int32_t len) {
    switch (oid) {
    case 16: xs_sb_put(sb, len > 0 && q[0] == 't' ? "true" : "false", len > 0 && q[0] == 't' ? 4 : 5); return;
    case 21: case 23: case 26: case 700: case 701: xs_sb_put(sb, q, (size_t)len); return;
    case 1009: case 1015: case 1007: case 1005: { /* text[], varchar[], int4[], int2[]: "{a,"b,c",NULL}" */
        bool nums = oid == 1007 || oid == 1005;
        xs_sb_put(sb, "[", 1);
        const char *s = q + 1, *end = q + len - 1;
        bool first = true;
        while (len > 2 && s <= end) {
            char *buf = malloc((size_t)(end - s) + 1);
            size_t bl = 0;
            bool quoted = *s == '"';
            if (quoted) s++;
            while (s < end && (quoted ? *s != '"' : *s != ',')) { if (quoted && *s == '\\' && s + 1 < end) s++; buf[bl++] = *s++; }
            if (quoted) s++;
            if (!first) xs_sb_put(sb, ",", 1);
            first = false;
            if (!quoted && bl == 4 && memcmp(buf, "NULL", 4) == 0) xs_sb_put(sb, "null", 4);
            else if (nums) xs_sb_put(sb, buf, bl);
            else { xs_str *t = xs_str_new(buf, bl); xs_sb_str(sb, t); xs_release(t); }
            free(buf);
            s++;
        }
        xs_sb_put(sb, "]", 1);
        return;
    }
    default: { xs_str *t = xs_str_new(q, (size_t)len); xs_sb_str(sb, t); xs_release(t); return; }
    }
}
static xs_str *xn_pg_json(xs_pg_res *r) {
    xs_sb sb = {0};
    xs_sb_put(&sb, "{\"rows\":[", 9);
    for (size_t i = 0; i < r->nrows; i++) {
        if (i) xs_sb_put(&sb, ",", 1);
        const char *q = r->data + r->rows[i];
        int nc = xs_pg_get16(q);
        q += 2;
        xs_sb_put(&sb, "{", 1);
        for (int k = 0; k < nc && k < r->ncols; k++) {
            int32_t len = (int32_t)xs_pg_get32(q);
            q += 4;
            if (k) xs_sb_put(&sb, ",", 1);
            xs_str *name = xs_str_new(r->names[k], strlen(r->names[k]));
            xs_sb_str(&sb, name);
            xs_release(name);
            xs_sb_put(&sb, ":", 1);
            if (len < 0) xs_sb_put(&sb, "null", 4);
            else { xn_pg_json_value(&sb, r->oids[k], q, len); q += len; }
        }
        xs_sb_put(&sb, "}", 1);
    }
    char tail[48];
    int n = snprintf(tail, sizeof tail, "],\"rowCount\":%lld}", (long long)r->row_count);
    xs_sb_put(&sb, tail, (size_t)n);
    return xs_sb_finish(&sb);
}
/* pool.query(text, values): values are any xisto values (unknown): strings, numbers, booleans, null */
XS_NOINLINE xs_str *xn_pg_NativePool_query(xn_pg_NativePool *p, xs_str *text, xs_arr_ref *values) {
    int n = values ? (int)values->len : 0;
    xs_sqarg *a = calloc((size_t)(n ? n : 1), sizeof *a);
    if (!a) xs_panic("out of memory");
    for (int i = 0; i < n; i++) {
        void *v = values->data[i];
        if (!v) { a[i].kind = 0; continue; }
        void (*f)(void *) = xs_type_of(v)->free;
        if (f == xs_str_free) { a[i].kind = 3; a[i].s = v; }
        else if (f == xs_box_int_free) { a[i].kind = 1; a[i].i = ((xs_box_i *)v)->v; }
        else if (f == xs_box_num_free) { a[i].kind = 2; a[i].d = ((xs_box_d *)v)->v; }
        else if (f == xs_box_bool_free) { a[i].kind = 4; a[i].i = ((xs_box_i *)v)->v; }
        else { free(a); xs_panic("pg: a query parameter must be a string, number, boolean or null"); }
    }
    xs_pg_res *r = xs_pg_query(p, text, n, a);
    free(a);
    xs_str *json = xn_pg_json(r);
    xs_pg_res_free(r);
    return json;
}
