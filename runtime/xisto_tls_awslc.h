/* TLS for Xisto with AWS-LC (OpenSSL-compatible API, BoringSSL fork): PoC backend selected with
   XISTO_TLS=awslc at build time, same interface as runtime/xisto_tls.h (BearSSL).
   Client: system CA bundle (SSL_CERT_FILE overrides, NODE_EXTRA_CA_CERTS adds), host name checked.
   Server: PEM chain + key; TLS 1.2 and 1.3; session cache and tickets on by default. */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

struct xs_tls { SSL *ssl; bool server; bool fd_set; int err; long verify; };
typedef struct xs_tls_cred { SSL_CTX *ctx; } xs_tls_cred;

static SSL_CTX *xs_tls_client_ctx;
static pthread_once_t xs_tls_client_once = PTHREAD_ONCE_INIT;
static void xs_tls_client_init(void) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_method());
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    const char *file = getenv("SSL_CERT_FILE");
    if (!file) file = access("/etc/ssl/cert.pem", R_OK) == 0 ? "/etc/ssl/cert.pem" : "/etc/ssl/certs/ca-certificates.crt";
    SSL_CTX_load_verify_locations(ctx, file, NULL);
    const char *extra = getenv("NODE_EXTRA_CA_CERTS");
    if (extra) SSL_CTX_load_verify_locations(ctx, extra, NULL);
    SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    xs_tls_client_ctx = ctx;
}

XS_NOINLINE xs_tls *xs_tls_client_new(const char *host) {
    if (!host || !host[0]) xs_panic("fetch: https needs a host name");
    pthread_once(&xs_tls_client_once, xs_tls_client_init);
    xs_tls *t = calloc(1, sizeof *t);
    t->ssl = SSL_new(xs_tls_client_ctx);
    SSL_set_tlsext_host_name(t->ssl, host);
    SSL_set1_host(t->ssl, host);
    SSL_set_connect_state(t->ssl);
    return t;
}
static void xs_tls_free(xs_tls *t) { SSL_free(t->ssl); free(t); }

static const char *xs_tls_err(xs_tls *t) {
    switch (t->verify) {
    case X509_V_OK: break;
    case X509_V_ERR_HOSTNAME_MISMATCH: return "the certificate does not match the host name";
    case X509_V_ERR_CERT_HAS_EXPIRED: return "the certificate has expired";
    case X509_V_ERR_CERT_NOT_YET_VALID: return "the certificate is not yet valid";
    default: return "certificate is not trusted";
    }
    return "TLS handshake failed";
}
static int xs_tls_last_error(xs_tls *t) { return t->err; }

/* one SSL call, waiting on the socket (yielding inside a coroutine) when it asks to */
static int xs_tls_io(xs_tls *t, int fd, int r, int64_t timeout_ms, uint64_t deadline) {
    int e = SSL_get_error(t->ssl, r);
    if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
        int64_t left = timeout_ms < 0 ? -1 : (int64_t)((deadline > xs_now() ? deadline - xs_now() : 0) / 1000000ull);
        return xs_wait_fd_ms(fd, e == SSL_ERROR_WANT_READ ? EVFILT_READ : EVFILT_WRITE, left) ? 1 : -1;
    }
    if (e == SSL_ERROR_ZERO_RETURN) return 0;
    t->verify = SSL_get_verify_result(t->ssl);
    t->err = e ? e : -1;
    ERR_clear_error();
    return -1;
}
static void xs_tls_bind(xs_tls *t, int fd) { if (!t->fd_set) { SSL_set_fd(t->ssl, fd); t->fd_set = true; } }
/* node:tls: the handshake now (tls.connect's 'secureConnect'), waiting on the socket; false when it failed */
XS_NOINLINE bool xs_tls_handshake(xs_tls *t, int fd) {
    xs_tls_bind(t, fd);
    for (;;) {
        int r = SSL_do_handshake(t->ssl);
        if (r == 1) return true;
        if (xs_tls_io(t, fd, r, -1, 0) <= 0) return false;
    }
}
/* node:tls / node:https option ca: these CA certificates (a PEM file) trusted too, for the connections made after */
XS_NOINLINE void xs_tls_add_ca_file(const char *path) {
    pthread_once(&xs_tls_client_once, xs_tls_client_init);
    SSL_CTX_load_verify_locations(xs_tls_client_ctx, path, NULL);
}

static bool xs_tls_write_all(xs_tls *t, int fd, const char *p, size_t n, int64_t timeout_ms, uint64_t deadline) {
    xs_tls_bind(t, fd);
    while (n > 0) {
        int w = SSL_write(t->ssl, p, n > INT32_MAX ? INT32_MAX : (int)n);
        if (w > 0) { p += w; n -= (size_t)w; continue; }
        if (xs_tls_io(t, fd, w, timeout_ms, deadline) <= 0) return false;
    }
    return true;
}
static ssize_t xs_tls_read_some(xs_tls *t, int fd, char *out, size_t cap, int64_t timeout_ms, uint64_t deadline) {
    xs_tls_bind(t, fd);
    for (;;) {
        int r = SSL_read(t->ssl, out, cap > INT32_MAX ? INT32_MAX : (int)cap);
        if (r > 0) return r;
        int k = xs_tls_io(t, fd, r, timeout_ms, deadline);
        if (k == 0) return 0;
        if (k < 0) return t->err ? -1 : 0;
    }
}

/* ---- server ---- */
XS_NOINLINE xs_tls_cred *xs_tls_cred_load(xs_str *cert, xs_str *key) {
    xs_tls_cred *c = calloc(1, sizeof *c);
    SSL_CTX *ctx = SSL_CTX_new(TLS_method());
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    if (SSL_CTX_use_certificate_chain_file(ctx, cert->data) != 1) xs_panicf("TLS: cannot read certificate '%s'", cert->data);
    if (SSL_CTX_use_PrivateKey_file(ctx, key->data, SSL_FILETYPE_PEM) != 1) xs_panicf("TLS: unsupported private key in '%s'", key->data);
    if (SSL_CTX_check_private_key(ctx) != 1) xs_panicf("TLS: the key in '%s' does not match the certificate", key->data);
    SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_RELEASE_BUFFERS);
    SSL_CTX_set_options(ctx, SSL_OP_NO_RENEGOTIATION);
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER);
    c->ctx = ctx;
    return c;
}
XS_NOINLINE void *xs_tls_cred_load_any(xs_str *cert, xs_str *key) { return xs_tls_cred_load(cert, key); }

static xs_tls *xs_tls_server_new(xs_tls_cred *cr) {
    xs_tls *t = calloc(1, sizeof *t);
    t->server = true;
    t->ssl = SSL_new(cr->ctx);
    SSL_set_accept_state(t->ssl);
    return t;
}

/* event-loop side: never blocks. 0 = nothing yet, -1 = closed or failed */
static ssize_t xs_tls_srv_read(xs_tls *t, int fd, char *out, size_t cap) {
    xs_tls_bind(t, fd);
    int r = SSL_read(t->ssl, out, cap > INT32_MAX ? INT32_MAX : (int)cap);
    if (r > 0) return r;
    int e = SSL_get_error(t->ssl, r);
    if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) return 0;
    ERR_clear_error();
    return -1;
}
/* plaintext bytes accepted (0 when the socket is full: the caller retries the same bytes later) */
static ssize_t xs_tls_srv_write(xs_tls *t, int fd, const char *p, size_t n) {
    xs_tls_bind(t, fd);
    size_t taken = 0;
    while (taken < n) {
        int w = SSL_write(t->ssl, p + taken, n - taken > INT32_MAX ? INT32_MAX : (int)(n - taken));
        if (w > 0) { taken += (size_t)w; continue; }
        int e = SSL_get_error(t->ssl, w);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) return (ssize_t)taken;
        ERR_clear_error();
        return -1;
    }
    return (ssize_t)taken;
}
/* records the library could not write stay in its buffer only while a write of the same bytes is pending,
   which the caller already tracks as unsent output */
static bool xs_tls_srv_pending(xs_tls *t) { (void)t; return false; }
static void xs_tls_srv_close_notify(xs_tls *t, int fd) { xs_tls_bind(t, fd); SSL_shutdown(t->ssl); ERR_clear_error(); }
