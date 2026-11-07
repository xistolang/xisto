/* Xisto runtime: node:crypto (SHA-256, HMAC, PBKDF2, random bytes, base64). Plain C, constant-time where
   secrets are compared. Appended after xisto_rt.h when a program imports node:crypto, or a native package
   needs it (xisto.json "needs": ["crypto"]). Test vectors: tests/crypto/vectors.c. */
#include <pthread.h>
#include <sys/random.h>

/* ---- SHA-256 (FIPS 180-4) ---- */
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; } xs_sha256;
static const uint32_t xs_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
#define XS_ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void xs_sha256_block(xs_sha256 *c, const uint8_t *p) {
    uint32_t w[64], a, b, d, e, f, g, h, cc;
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = XS_ROR32(w[i - 15], 7) ^ XS_ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = XS_ROR32(w[i - 2], 17) ^ XS_ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (XS_ROR32(e, 6) ^ XS_ROR32(e, 11) ^ XS_ROR32(e, 25)) + ((e & f) ^ (~e & g)) + xs_sha256_k[i] + w[i];
        uint32_t t2 = (XS_ROR32(a, 2) ^ XS_ROR32(a, 13) ^ XS_ROR32(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
static void xs_sha256_init(xs_sha256 *c) {
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(c->h, iv, sizeof iv);
    c->len = 0;
    c->n = 0;
}
static void xs_sha256_update(xs_sha256 *c, const void *data, size_t len) {
    const uint8_t *p = data;
    c->len += len;
    if (c->n) {
        size_t take = 64 - c->n < len ? 64 - c->n : len;
        memcpy(c->buf + c->n, p, take);
        c->n += take; p += take; len -= take;
        if (c->n == 64) { xs_sha256_block(c, c->buf); c->n = 0; }
    }
    for (; len >= 64; p += 64, len -= 64) xs_sha256_block(c, p);
    if (len) { memcpy(c->buf, p, len); c->n = len; }
}
static void xs_sha256_final(xs_sha256 *c, uint8_t out[32]) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80, zero = 0;
    xs_sha256_update(c, &pad, 1);
    while (c->n != 56) xs_sha256_update(c, &zero, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
    xs_sha256_update(c, lenb, 8);
    for (int i = 0; i < 8; i++) { out[4 * i] = (uint8_t)(c->h[i] >> 24); out[4 * i + 1] = (uint8_t)(c->h[i] >> 16); out[4 * i + 2] = (uint8_t)(c->h[i] >> 8); out[4 * i + 3] = (uint8_t)c->h[i]; }
}
static void xs_sha256_hash(const void *data, size_t len, uint8_t out[32]) {
    xs_sha256 c;
    xs_sha256_init(&c);
    xs_sha256_update(&c, data, len);
    xs_sha256_final(&c, out);
}

/* ---- HMAC-SHA256 (RFC 2104) ---- */
typedef struct { xs_sha256 in, out; } xs_hmac256_ctx;
static void xs_hmac256_init(xs_hmac256_ctx *m, const void *key, size_t klen) {
    uint8_t k[64] = {0}, pad[64];
    if (klen > 64) xs_sha256_hash(key, klen, k);
    else memcpy(k, key, klen);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    xs_sha256_init(&m->in);
    xs_sha256_update(&m->in, pad, 64);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    xs_sha256_init(&m->out);
    xs_sha256_update(&m->out, pad, 64);
}
static void xs_hmac256_final(xs_hmac256_ctx *m, uint8_t out[32]) {
    uint8_t ih[32];
    xs_sha256_final(&m->in, ih);
    xs_sha256_update(&m->out, ih, 32);
    xs_sha256_final(&m->out, out);
}
static void xs_hmac256(const void *key, size_t klen, const void *data, size_t len, uint8_t out[32]) {
    xs_hmac256_ctx m;
    xs_hmac256_init(&m, key, klen);
    xs_sha256_update(&m.in, data, len);
    xs_hmac256_final(&m, out);
}

/* ---- PBKDF2-HMAC-SHA256 (RFC 8018), one 32-byte block: what SCRAM-SHA-256 needs ---- */
static void xs_pbkdf2_256(const void *pw, size_t pwlen, const uint8_t *salt, size_t slen, uint32_t iters, uint8_t out[32]) {
    xs_hmac256_ctx base, m;
    xs_hmac256_init(&base, pw, pwlen);
    uint8_t u[32], one[4] = {0, 0, 0, 1};
    m = base;
    xs_sha256_update(&m.in, salt, slen);
    xs_sha256_update(&m.in, one, 4);
    xs_hmac256_final(&m, u);
    memcpy(out, u, 32);
    for (uint32_t i = 1; i < iters; i++) {
        m = base;
        xs_sha256_update(&m.in, u, 32);
        xs_hmac256_final(&m, u);
        for (int k = 0; k < 32; k++) out[k] ^= u[k];
    }
}

static bool xs_ct_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
    return d == 0;
}
static void xs_random_bytes(void *out, size_t n) {
    uint8_t *p = out;
    while (n) { size_t k = n > 256 ? 256 : n; if (getentropy(p, k) != 0) xs_panic("no entropy"); p += k; n -= k; }
}

/* ---- base64 (RFC 4648): standard with padding, url-safe without ---- */
static size_t xs_b64_encode(const uint8_t *in, size_t n, char *out, bool url) {
    const char *al = url ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_" : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0, i = 0;
    for (; i + 2 < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
        out[o++] = al[v >> 18]; out[o++] = al[(v >> 12) & 63]; out[o++] = al[(v >> 6) & 63]; out[o++] = al[v & 63];
    }
    if (i < n) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0);
        out[o++] = al[v >> 18];
        out[o++] = al[(v >> 12) & 63];
        if (i + 1 < n) out[o++] = al[(v >> 6) & 63]; else if (!url) out[o++] = '=';
        if (!url) out[o++] = '=';
    }
    out[o] = 0;
    return o;
}
/* decodes both alphabets, padding optional; returns the length or -1 */
static long xs_b64_decode(const char *in, size_t n, uint8_t *out) {
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        char c = in[i];
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=') break;
        else return -1;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (uint8_t)(acc >> bits); }
    }
    return (long)o;
}

