/* TLS for Xisto with BearSSL (third_party/bearssl, compiled from source into build/libbearssl.a).
   Client: fetch("https://...") verifies the server against the system CA bundle (SSL_CERT_FILE overrides it,
   NODE_EXTRA_CA_CERTS adds to it, like Node). Server: app.listen(port, { cert, key }) with PEM files.
   BearSSL is a state machine over two buffers; these functions move bytes between it and a non-blocking
   socket, yielding (inside a coroutine) or polling when the socket is not ready. */
#include "bearssl.h"

typedef struct { char *name; unsigned char *data; size_t len; } xs_pem_obj;

static void xs_wipe(void *p, size_t n) { volatile unsigned char *v = p; while (n--) *v++ = 0; } /* not optimized away */

static void xs_pem_append(void *ctx, const void *buf, size_t len) {
    xs_pem_obj *o = ctx;
    o->data = realloc(o->data, o->len + len);
    memcpy(o->data + o->len, buf, len);
    o->len += len;
}

/* every PEM object of a file, in order */
static xs_pem_obj *xs_pem_read(const char *path, size_t *n_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    xs_buf b = {0};
    char tmp[16384];
    size_t r;
    while ((r = fread(tmp, 1, sizeof tmp, f)) > 0) xs_buf_append(&b, tmp, r);
    fclose(f);
    br_pem_decoder_context pc;
    br_pem_decoder_init(&pc);
    xs_pem_obj *objs = NULL, cur = {0};
    size_t n = 0, cap = 0, pos = 0;
    bool inobj = false;
    xs_buf_append(&b, "\n", 1); /* the decoder only closes the last object on a newline */
    while (pos < b.len) {
        pos += br_pem_decoder_push(&pc, b.data + pos, b.len - pos);
        switch (br_pem_decoder_event(&pc)) {
        case BR_PEM_BEGIN_OBJ:
            memset(&cur, 0, sizeof cur);
            cur.name = strdup(br_pem_decoder_name(&pc));
            br_pem_decoder_setdest(&pc, xs_pem_append, &cur);
            inobj = true;
            break;
        case BR_PEM_END_OBJ:
            if (inobj) {
                if (n == cap) { cap = cap ? cap * 2 : 64; objs = realloc(objs, cap * sizeof *objs); }
                objs[n++] = cur;
                inobj = false;
            }
            break;
        case BR_PEM_ERROR:
            pos = b.len;
            break;
        }
    }
    xs_wipe(b.data, b.len); /* key files pass through here */
    free(b.data);
    *n_out = n;
    return objs;
}

/* ---- trust anchors from CA certificates (the conversion BearSSL's tools/certs.c does) ---- */
static void xs_dn_append(void *ctx, const void *buf, size_t len) { xs_buf_append(ctx, buf, len); }

static bool xs_cert_to_ta(const unsigned char *der, size_t len, br_x509_trust_anchor *ta) {
    br_x509_decoder_context dc;
    xs_buf dn = {0};
    br_x509_decoder_init(&dc, xs_dn_append, &dn);
    br_x509_decoder_push(&dc, der, len);
    br_x509_pkey *pk = br_x509_decoder_get_pkey(&dc);
    if (!pk) { free(dn.data); return false; }
    ta->dn.data = (unsigned char *)dn.data;
    ta->dn.len = dn.len;
    ta->flags = br_x509_decoder_isCA(&dc) ? BR_X509_TA_CA : 0;
    switch (pk->key_type) {
    case BR_KEYTYPE_RSA:
        ta->pkey.key_type = BR_KEYTYPE_RSA;
        ta->pkey.key.rsa.n = memcpy(malloc(pk->key.rsa.nlen), pk->key.rsa.n, pk->key.rsa.nlen);
        ta->pkey.key.rsa.nlen = pk->key.rsa.nlen;
        ta->pkey.key.rsa.e = memcpy(malloc(pk->key.rsa.elen), pk->key.rsa.e, pk->key.rsa.elen);
        ta->pkey.key.rsa.elen = pk->key.rsa.elen;
        return true;
    case BR_KEYTYPE_EC:
        ta->pkey.key_type = BR_KEYTYPE_EC;
        ta->pkey.key.ec.curve = pk->key.ec.curve;
        ta->pkey.key.ec.q = memcpy(malloc(pk->key.ec.qlen), pk->key.ec.q, pk->key.ec.qlen);
        ta->pkey.key.ec.qlen = pk->key.ec.qlen;
        return true;
    default:
        free(dn.data);
        return false;
    }
}

static br_x509_trust_anchor *xs_tas;
static size_t xs_ntas;
static pthread_once_t xs_tas_once = PTHREAD_ONCE_INIT;

static void xs_tas_add_file(const char *path) {
    size_t n;
    xs_pem_obj *objs = xs_pem_read(path, &n);
    for (size_t i = 0; objs && i < n; i++) {
        if (strcmp(objs[i].name, "CERTIFICATE") == 0 || strcmp(objs[i].name, "X509 CERTIFICATE") == 0) {
            xs_tas = realloc(xs_tas, (xs_ntas + 1) * sizeof *xs_tas);
            if (xs_cert_to_ta(objs[i].data, objs[i].len, &xs_tas[xs_ntas])) xs_ntas++;
        }
        free(objs[i].data);
        free(objs[i].name);
    }
    free(objs);
}

static void xs_tas_load(void) {
    const char *file = getenv("SSL_CERT_FILE");
    if (!file) {
        static const char *defaults[] = {"/etc/ssl/cert.pem", "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt"};
        for (size_t i = 0; i < 3 && !file; i++) if (access(defaults[i], R_OK) == 0) file = defaults[i];
    }
    if (file) xs_tas_add_file(file);
    const char *extra = getenv("NODE_EXTRA_CA_CERTS");
    if (extra) xs_tas_add_file(extra);
}

/* ---- one TLS connection, either side ---- */
/* The record buffer is allocated apart: the client (fetch) gets the full-duplex size, the server the
   half-duplex one (16.7 KB instead of 33 KB per connection). Half-duplex fits the server: it never reads
   while a response is still queued (read interest is paused until the output drains). */
struct xs_tls {
    bool server;
    union {
        struct { br_ssl_client_context cc; br_x509_minimal_context xc; } c;
        br_ssl_server_context sc;
    } u;
    br_ssl_engine_context *eng;
    unsigned char *iobuf;
};

static xs_tls *xs_tls_client_new(const char *host) {
    if (!host || !host[0]) xs_panic("fetch: https needs a host name"); /* BearSSL skips the name check on "" */
    pthread_once(&xs_tas_once, xs_tas_load);
    xs_tls *t = calloc(1, sizeof *t);
    br_ssl_client_init_full(&t->u.c.cc, &t->u.c.xc, xs_tas, xs_ntas);
    t->eng = &t->u.c.cc.eng;
    t->iobuf = malloc(BR_SSL_BUFSIZE_BIDI);
    br_ssl_engine_set_buffer(t->eng, t->iobuf, BR_SSL_BUFSIZE_BIDI, 1);
    br_ssl_client_reset(&t->u.c.cc, host, 0);
    return t;
}

static const char *xs_tls_err(xs_tls *t) {
    int e = br_ssl_engine_last_error(t->eng);
    switch (e) {
    case BR_ERR_X509_NOT_TRUSTED: return "certificate is not trusted";
    case BR_ERR_X509_BAD_SERVER_NAME: return "certificate does not match the host name";
    case BR_ERR_X509_EXPIRED: return "certificate has expired";
    default: { static XS_TLS char b[64]; snprintf(b, sizeof b, "TLS error %d", e); return b; }
    }
}

/* Moves records between the engine and the socket until `done(t)` holds. Waits (with an optional deadline)
   when the socket is not ready. Returns false if the connection closed or failed. */
static bool xs_tls_pump(xs_tls *t, int fd, bool (*done)(xs_tls *), int64_t timeout_ms, uint64_t deadline) {
    for (;;) {
        unsigned st = br_ssl_engine_current_state(t->eng);
        if (st & BR_SSL_CLOSED) return false;
        if (done(t)) return true;
        if (st & BR_SSL_SENDREC) {
            size_t len;
            unsigned char *buf = br_ssl_engine_sendrec_buf(t->eng, &len);
            ssize_t w = write(fd, buf, len);
            if (w > 0) { br_ssl_engine_sendrec_ack(t->eng, (size_t)w); continue; }
            if (w < 0 && errno == EINTR) continue;
            if (w < 0 && errno == EAGAIN) {
                int64_t left = timeout_ms < 0 ? -1 : (int64_t)((deadline > xs_now() ? deadline - xs_now() : 0) / 1000000ull);
                if (!xs_wait_fd_ms(fd, EVFILT_WRITE, left)) return false;
                continue;
            }
            return false;
        }
        if (st & BR_SSL_RECVREC) {
            size_t len;
            unsigned char *buf = br_ssl_engine_recvrec_buf(t->eng, &len);
            ssize_t r = read(fd, buf, len);
            if (r > 0) { br_ssl_engine_recvrec_ack(t->eng, (size_t)r); continue; }
            if (r < 0 && errno == EINTR) continue;
            if (r < 0 && errno == EAGAIN) {
                int64_t left = timeout_ms < 0 ? -1 : (int64_t)((deadline > xs_now() ? deadline - xs_now() : 0) / 1000000ull);
                if (!xs_wait_fd_ms(fd, EVFILT_READ, left)) return false;
                continue;
            }
            br_ssl_engine_close(t->eng);
            return false;
        }
        return false; /* nothing to do and not done: should not happen */
    }
}
static bool xs_tls_can_send(xs_tls *t) { return (br_ssl_engine_current_state(t->eng) & BR_SSL_SENDAPP) != 0; }
static bool xs_tls_can_recv(xs_tls *t) { return (br_ssl_engine_current_state(t->eng) & BR_SSL_RECVAPP) != 0; }
static bool xs_tls_sent(xs_tls *t) { return (br_ssl_engine_current_state(t->eng) & BR_SSL_SENDREC) == 0; }

/* blocking-style API for the client (fetch): yields inside a coroutine */
static bool xs_tls_write_all(xs_tls *t, int fd, const char *p, size_t n, int64_t timeout_ms, uint64_t deadline) {
    while (n > 0) {
        if (!xs_tls_pump(t, fd, xs_tls_can_send, timeout_ms, deadline)) return false;
        size_t len;
        unsigned char *buf = br_ssl_engine_sendapp_buf(t->eng, &len);
        if (len > n) len = n;
        memcpy(buf, p, len);
        br_ssl_engine_sendapp_ack(t->eng, len);
        p += len; n -= len;
    }
    br_ssl_engine_flush(t->eng, 0);
    return xs_tls_pump(t, fd, xs_tls_sent, timeout_ms, deadline);
}
static ssize_t xs_tls_read_some(xs_tls *t, int fd, char *out, size_t cap, int64_t timeout_ms, uint64_t deadline) {
    if (!xs_tls_pump(t, fd, xs_tls_can_recv, timeout_ms, deadline)) return br_ssl_engine_last_error(t->eng) ? -1 : 0;
    size_t len;
    unsigned char *buf = br_ssl_engine_recvapp_buf(t->eng, &len);
    if (len > cap) len = cap;
    memcpy(out, buf, len);
    br_ssl_engine_recvapp_ack(t->eng, len);
    return (ssize_t)len;
}

/* ---- server side: certificate chain and key, loaded once per listen ---- */
typedef struct xs_tls_cred {
    br_x509_certificate *chain; size_t chain_len;
    int key_type; /* BR_KEYTYPE_RSA or BR_KEYTYPE_EC */
    br_rsa_private_key rsa; br_ec_private_key ec;
} xs_tls_cred;

static xs_tls_cred *xs_tls_cred_load(xs_str *cert, xs_str *key) {
    xs_tls_cred *c = calloc(1, sizeof *c);
    size_t n;
    xs_pem_obj *objs = xs_pem_read(cert->data, &n);
    if (!objs) xs_panicf("TLS: cannot read certificate '%s'", cert->data);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(objs[i].name, "CERTIFICATE") != 0) continue;
        c->chain = realloc(c->chain, (c->chain_len + 1) * sizeof *c->chain);
        c->chain[c->chain_len].data = objs[i].data;
        c->chain[c->chain_len].data_len = objs[i].len;
        c->chain_len++;
    }
    if (!c->chain_len) xs_panicf("TLS: no certificate in '%s'", cert->data);
    for (size_t i = 0; i < n; i++) if (strcmp(objs[i].name, "CERTIFICATE") != 0) { free(objs[i].data); }
    for (size_t i = 0; i < n; i++) free(objs[i].name);
    free(objs);
    objs = xs_pem_read(key->data, &n);
    if (!objs || !n) xs_panicf("TLS: cannot read key '%s'", key->data);
    /* the key may follow other blocks (openssl ecparam -genkey writes EC PARAMETERS first; a combined
       cert+key file has CERTIFICATE first): take the first block that decodes as a private key */
    br_skey_decoder_context kd;
    bool found = false;
    for (size_t i = 0; i < n && !found; i++) {
        if (!strstr(objs[i].name, "PRIVATE KEY")) continue;
        br_skey_decoder_init(&kd);
        br_skey_decoder_push(&kd, objs[i].data, objs[i].len);
        found = br_skey_decoder_last_error(&kd) == 0 && br_skey_decoder_key_type(&kd) != 0;
    }
    for (size_t i = 0; i < n; i++) { xs_wipe(objs[i].data, objs[i].len); free(objs[i].data); free(objs[i].name); }
    free(objs);
    if (!found) { xs_wipe(&kd, sizeof kd); xs_panicf("TLS: unsupported private key in '%s'", key->data); }
    c->key_type = br_skey_decoder_key_type(&kd);
    if (c->key_type == BR_KEYTYPE_RSA) {
        const br_rsa_private_key *k = br_skey_decoder_get_rsa(&kd);
        c->rsa = *k;
#define XS_DUP(f, l) c->rsa.f = memcpy(malloc(k->l), k->f, k->l)
        XS_DUP(p, plen); XS_DUP(q, qlen); XS_DUP(dp, dplen); XS_DUP(dq, dqlen); XS_DUP(iq, iqlen);
#undef XS_DUP
    } else {
        const br_ec_private_key *k = br_skey_decoder_get_ec(&kd);
        c->ec = *k;
        c->ec.x = memcpy(malloc(k->xlen), k->x, k->xlen);
    }
    xs_wipe(&kd, sizeof kd); /* the decoder held the key in clear on the stack */
    return c;
}

static xs_tls *xs_tls_server_new(xs_tls_cred *cr) {
    xs_tls *t = calloc(1, sizeof *t);
    t->server = true;
    if (cr->key_type == BR_KEYTYPE_RSA) br_ssl_server_init_full_rsa(&t->u.sc, cr->chain, cr->chain_len, &cr->rsa);
    else br_ssl_server_init_full_ec(&t->u.sc, cr->chain, cr->chain_len, BR_KEYTYPE_EC, &cr->ec);
    t->eng = &t->u.sc.eng;
    /* Our suite order wins over the client's: ChaCha20-Poly1305 first. BearSSL's AES is
       constant-time bitsliced C (no AES instructions), so ChaCha20 is the faster safe choice. */
    br_ssl_engine_add_flags(t->eng, BR_OPT_ENFORCE_SERVER_PREFERENCES | BR_OPT_NO_RENEGOTIATION); /* renegotiation: a CPU DoS lever */
    t->iobuf = malloc(BR_SSL_BUFSIZE_MONO);
    br_ssl_engine_set_buffer(t->eng, t->iobuf, BR_SSL_BUFSIZE_MONO, 0);
    /* A session cache per worker thread: returning clients resume with an abbreviated
       handshake (no ECDHE, no signature). 256 KB holds ~2,000 sessions. */
    static XS_TLS br_ssl_session_cache_lru lru; static XS_TLS unsigned char *lru_store; /* per worker: no locks */
    if (!lru_store) { lru_store = malloc(256 * 1024); br_ssl_session_cache_lru_init(&lru, lru_store, 256 * 1024); }
    br_ssl_server_set_cache(&t->u.sc, &lru.vtable);
    br_ssl_server_reset(&t->u.sc);
    return t;
}

/* Event-loop side of a server connection: feed what the socket has, take out plaintext.
   Returns bytes of plaintext copied, 0 if none is available yet, -1 on close/error. Never blocks. */
static ssize_t xs_tls_srv_read(xs_tls *t, int fd, char *out, size_t cap) {
    for (;;) {
        unsigned st = br_ssl_engine_current_state(t->eng);
        if (st & BR_SSL_CLOSED) return -1;
        if (st & BR_SSL_RECVAPP) {
            size_t len;
            unsigned char *buf = br_ssl_engine_recvapp_buf(t->eng, &len);
            if (len > cap) len = cap;
            memcpy(out, buf, len);
            br_ssl_engine_recvapp_ack(t->eng, len);
            return (ssize_t)len;
        }
        if (st & BR_SSL_SENDREC) { /* handshake messages to send */
            size_t len;
            unsigned char *buf = br_ssl_engine_sendrec_buf(t->eng, &len);
            ssize_t w = write(fd, buf, len);
            if (w > 0) { br_ssl_engine_sendrec_ack(t->eng, (size_t)w); continue; }
            if (w < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
            return -1;
        }
        if (st & BR_SSL_RECVREC) {
            size_t len;
            unsigned char *buf = br_ssl_engine_recvrec_buf(t->eng, &len);
            ssize_t r = read(fd, buf, len);
            if (r > 0) { br_ssl_engine_recvrec_ack(t->eng, (size_t)r); continue; }
            if (r < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
            return -1;
        }
        return 0;
    }
}

/* Encrypts plaintext and writes records without blocking. Returns the plaintext bytes accepted, -1 on error.
   Records that could not be written stay in the engine (see xs_tls_srv_pending). */
static ssize_t xs_tls_srv_write(xs_tls *t, int fd, const char *p, size_t n) {
    size_t taken = 0;
    for (;;) {
        unsigned st = br_ssl_engine_current_state(t->eng);
        if (st & BR_SSL_CLOSED) return -1;
        if (st & BR_SSL_SENDREC) {
            size_t len;
            unsigned char *buf = br_ssl_engine_sendrec_buf(t->eng, &len);
            ssize_t w = write(fd, buf, len);
            if (w > 0) { br_ssl_engine_sendrec_ack(t->eng, (size_t)w); continue; }
            if (w < 0 && (errno == EAGAIN || errno == EINTR)) return (ssize_t)taken;
            return -1;
        }
        if (taken < n && (st & BR_SSL_SENDAPP)) {
            size_t len;
            unsigned char *buf = br_ssl_engine_sendapp_buf(t->eng, &len);
            if (len > n - taken) len = n - taken;
            memcpy(buf, p + taken, len);
            br_ssl_engine_sendapp_ack(t->eng, len);
            taken += len;
            if (taken == n) br_ssl_engine_flush(t->eng, 0);
            continue;
        }
        if (taken == n) return (ssize_t)taken;
        return (ssize_t)taken; /* engine busy (handshake): try again on the next event */
    }
}
/* Before closing the socket: send close_notify so strict clients can tell a complete response from a cut one.
   Best effort and non-blocking: whatever the socket does not take right now is dropped. */
static void xs_tls_srv_close_notify(xs_tls *t, int fd) {
    if (br_ssl_engine_current_state(t->eng) & BR_SSL_CLOSED) return;
    br_ssl_engine_close(t->eng);
    for (int i = 0; i < 4; i++) {
        if (!(br_ssl_engine_current_state(t->eng) & BR_SSL_SENDREC)) return;
        size_t len;
        unsigned char *buf = br_ssl_engine_sendrec_buf(t->eng, &len);
        ssize_t w = write(fd, buf, len);
        if (w <= 0) return;
        br_ssl_engine_sendrec_ack(t->eng, (size_t)w);
    }
}
static void xs_tls_free(xs_tls *t) { free(t->iobuf); free(t); }
static bool xs_tls_srv_pending(xs_tls *t) { return (br_ssl_engine_current_state(t->eng) & BR_SSL_SENDREC) != 0; }

static void *xs_tls_cred_load_any(xs_str *cert, xs_str *key) { return xs_tls_cred_load(cert, key); }
static int xs_tls_last_error(xs_tls *t) { return br_ssl_engine_last_error(t->eng); }
