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


/* ---- MD5 (RFC 1321), SHA-1 (FIPS 180-4) and SHA-512: for createHash/createHmac (xisto:crypto) ---- */
typedef struct { uint32_t h[4]; uint64_t len; uint8_t buf[64]; size_t n; } xs_md5;
#define XS_ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
static void xs_md5_block(xs_md5 *c, const uint8_t *p) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
        0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
        0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
        0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
        0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t w[16];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] | ((uint32_t)p[4 * i + 1] << 8) | ((uint32_t)p[4 * i + 2] << 16) | ((uint32_t)p[4 * i + 3] << 24);
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f; int g;
        if (i < 16) { f = (b & cc) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & cc); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ cc ^ d; g = (3 * i + 5) % 16; }
        else { f = cc ^ (b | ~d); g = (7 * i) % 16; }
        uint32_t t = d; d = cc; cc = b;
        b = b + XS_ROL32(a + f + K[i] + w[g], R[i]);
        a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
}
static void xs_md5_init(xs_md5 *c) { c->h[0] = 0x67452301; c->h[1] = 0xefcdab89; c->h[2] = 0x98badcfe; c->h[3] = 0x10325476; c->len = 0; c->n = 0; }
static void xs_md5_update(xs_md5 *c, const void *data, size_t len) {
    const uint8_t *p = data;
    c->len += len;
    while (len) { size_t k = 64 - c->n < len ? 64 - c->n : len; memcpy(c->buf + c->n, p, k); c->n += k; p += k; len -= k; if (c->n == 64) { xs_md5_block(c, c->buf); c->n = 0; } }
}
static void xs_md5_final(xs_md5 *c, uint8_t out[16]) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80, z = 0;
    xs_md5_update(c, &pad, 1);
    while (c->n != 56) xs_md5_update(c, &z, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (8 * i));
    xs_md5_update(c, lenb, 8);
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(c->h[i] >> (8 * j));
}

typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; size_t n; } xs_sha1;
static void xs_sha1_block(xs_sha1 *c, const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = XS_ROL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & cc) | (~b & d); k = 0x5a827999; }
        else if (i < 40) { f = b ^ cc ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8f1bbcdc; }
        else { f = b ^ cc ^ d; k = 0xca62c1d6; }
        uint32_t t = XS_ROL32(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = XS_ROL32(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}
static void xs_sha1_init(xs_sha1 *c) { c->h[0] = 0x67452301; c->h[1] = 0xefcdab89; c->h[2] = 0x98badcfe; c->h[3] = 0x10325476; c->h[4] = 0xc3d2e1f0; c->len = 0; c->n = 0; }
static void xs_sha1_update(xs_sha1 *c, const void *data, size_t len) {
    const uint8_t *p = data;
    c->len += len;
    while (len) { size_t k = 64 - c->n < len ? 64 - c->n : len; memcpy(c->buf + c->n, p, k); c->n += k; p += k; len -= k; if (c->n == 64) { xs_sha1_block(c, c->buf); c->n = 0; } }
}
static void xs_sha1_final(xs_sha1 *c, uint8_t out[20]) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80, z = 0;
    xs_sha1_update(c, &pad, 1);
    while (c->n != 56) xs_sha1_update(c, &z, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
    xs_sha1_update(c, lenb, 8);
    for (int i = 0; i < 5; i++) for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(c->h[i] >> (24 - 8 * j));
}

typedef struct { uint64_t h[8]; uint64_t len; uint8_t buf[128]; size_t n; } xs_sha512;
#define XS_ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
static void xs_sha512_block(xs_sha512 *c, const uint8_t *p) {
    static const uint64_t K[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
        0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
        0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
        0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
        0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
        0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
        0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
        0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};
    uint64_t w[80];
    for (int i = 0; i < 16; i++) { w[i] = 0; for (int j = 0; j < 8; j++) w[i] = (w[i] << 8) | p[8 * i + j]; }
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = XS_ROR64(w[i - 15], 1) ^ XS_ROR64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = XS_ROR64(w[i - 2], 19) ^ XS_ROR64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
    for (int i = 0; i < 80; i++) {
        uint64_t S1 = XS_ROR64(e, 14) ^ XS_ROR64(e, 18) ^ XS_ROR64(e, 41), ch = (e & f) ^ (~e & g);
        uint64_t t1 = h + S1 + ch + K[i] + w[i];
        uint64_t S0 = XS_ROR64(a, 28) ^ XS_ROR64(a, 34) ^ XS_ROR64(a, 39), mj = (a & b) ^ (a & cc) ^ (b & cc);
        uint64_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
static void xs_sha512_init(xs_sha512 *c) {
    static const uint64_t H[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                                  0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    memcpy(c->h, H, sizeof H); c->len = 0; c->n = 0;
}
static void xs_sha512_update(xs_sha512 *c, const void *data, size_t len) {
    const uint8_t *p = data;
    c->len += len;
    while (len) { size_t k = 128 - c->n < len ? 128 - c->n : len; memcpy(c->buf + c->n, p, k); c->n += k; p += k; len -= k; if (c->n == 128) { xs_sha512_block(c, c->buf); c->n = 0; } }
}
static void xs_sha512_final(xs_sha512 *c, uint8_t out[64]) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80, z = 0;
    xs_sha512_update(c, &pad, 1);
    while (c->n != 112) xs_sha512_update(c, &z, 1);
    uint8_t lenb[16] = {0};
    for (int i = 0; i < 8; i++) lenb[8 + i] = (uint8_t)(bits >> (56 - 8 * i));
    xs_sha512_update(c, lenb, 16);
    for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) out[8 * i + j] = (uint8_t)(c->h[i] >> (56 - 8 * j));
}

/* one digest of any of the six, by Node's name: the length (0: not a digest xisto has) */
static int xs_digest_len(const char *alg) {
    if (strcmp(alg, "md5") == 0) return 16;
    if (strcmp(alg, "sha1") == 0) return 20;
    if (strcmp(alg, "sha224") == 0) return 28;
    if (strcmp(alg, "sha256") == 0) return 32;
    if (strcmp(alg, "sha384") == 0) return 48;
    if (strcmp(alg, "sha512") == 0) return 64;
    return 0;
}
static int xs_digest_block(const char *alg) { return strcmp(alg, "sha512") == 0 || strcmp(alg, "sha384") == 0 ? 128 : 64; }
/* SHA-224 and SHA-384 (FIPS 180-4): SHA-256 and SHA-512 from other initial values, cut */
static void xs_digest_raw(const char *alg, const void *a, size_t an, const void *b, size_t bn, uint8_t *out) {
    if (strcmp(alg, "md5") == 0) { xs_md5 c; xs_md5_init(&c); xs_md5_update(&c, a, an); xs_md5_update(&c, b, bn); xs_md5_final(&c, out); }
    else if (strcmp(alg, "sha1") == 0) { xs_sha1 c; xs_sha1_init(&c); xs_sha1_update(&c, a, an); xs_sha1_update(&c, b, bn); xs_sha1_final(&c, out); }
    else if (strcmp(alg, "sha256") == 0 || strcmp(alg, "sha224") == 0) {
        static const uint32_t iv224[8] = {0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939, 0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4};
        xs_sha256 c;
        uint8_t full[32];
        xs_sha256_init(&c);
        bool short_ = alg[3] == '2' && alg[4] == '2';
        if (short_) memcpy(c.h, iv224, sizeof iv224);
        xs_sha256_update(&c, a, an); xs_sha256_update(&c, b, bn); xs_sha256_final(&c, full);
        memcpy(out, full, short_ ? 28 : 32);
    } else {
        static const uint64_t iv384[8] = {0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
                                          0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL};
        xs_sha512 c;
        uint8_t full[64];
        xs_sha512_init(&c);
        bool short_ = strcmp(alg, "sha384") == 0;
        if (short_) memcpy(c.h, iv384, sizeof iv384);
        xs_sha512_update(&c, a, an); xs_sha512_update(&c, b, bn); xs_sha512_final(&c, full);
        memcpy(out, full, short_ ? 48 : 64);
    }
}
/* HMAC (RFC 2104) of a || b over any of the six, into out (the digest's length) */
static void xs_hmac_raw(const char *alg, const uint8_t *key, size_t klen, const void *a, size_t an, const void *b, size_t bn, uint8_t *out) {
    int bl = xs_digest_block(alg), dl = xs_digest_len(alg);
    uint8_t k[128] = {0}, ipad[128], opad[128], inner[64];
    if ((int)klen > bl) xs_digest_raw(alg, key, klen, "", 0, k);
    else if (klen) memcpy(k, key, klen);
    for (int i = 0; i < bl; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    /* H(ipad || a || b): a and b joined when both are given (the digests take two pieces) */
    if (bn) {
        uint8_t *ab = malloc(an + bn);
        if (!ab) xs_panic("out of memory");
        memcpy(ab, a, an);
        memcpy(ab + an, b, bn);
        xs_digest_raw(alg, ipad, (size_t)bl, ab, an + bn, inner);
        free(ab);
    } else xs_digest_raw(alg, ipad, (size_t)bl, a, an, inner);
    xs_digest_raw(alg, opad, (size_t)bl, inner, (size_t)dl, out);
}
/* bytes -> text in Node's digest encodings: hex, base64, base64url, latin1 (binary) */
static xs_str *xs_digest_encode(const uint8_t *d, int n, const char *enc) {
    char buf[200];
    size_t k = 0;
    if (strcmp(enc, "hex") == 0) { static const char hx[] = "0123456789abcdef"; for (int i = 0; i < n; i++) { buf[k++] = hx[d[i] >> 4]; buf[k++] = hx[d[i] & 15]; } }
    else if (strcmp(enc, "base64") == 0) k = xs_b64_encode(d, (size_t)n, buf, false);
    else if (strcmp(enc, "base64url") == 0) k = xs_b64_encode(d, (size_t)n, buf, true);
    else if (strcmp(enc, "latin1") == 0 || strcmp(enc, "binary") == 0) {
        for (int i = 0; i < n; i++) { if (d[i] < 0x80) buf[k++] = (char)d[i]; else { buf[k++] = (char)(0xc0 | (d[i] >> 6)); buf[k++] = (char)(0x80 | (d[i] & 0x3f)); } }
    } else {
        static XS_TLS char msg[160];
        snprintf(msg, sizeof msg, "The argument 'encoding' is invalid for data of length %d. Received '%s'", n, enc);
        xs_throw(xs_Error_from(msg));
    }
    return xs_str_new(buf, k);
}
XS_NOINLINE bool xs_crypto_supported(xs_str *alg) { return xs_digest_len(alg->data) > 0; }
/* Node's (OpenSSL's) other names of the six: SHA256, RSA-SHA256, sha-256, SHA2-256, ecdsa-with-SHA256: the name
   xisto uses, or "" when it is none of them */
XS_NOINLINE xs_str *xs_crypto_digest_name(xs_str *alg) {
    char b[32];
    size_t n = (size_t)alg->len, k = 0;
    if (n >= sizeof b) return xs_str_new("", 0);
    for (size_t i = 0; i < n; i++) b[i] = (char)tolower((unsigned char)alg->data[i]);
    b[n] = 0;
    const char *p = b;
    if (!strncmp(p, "rsa-", 4)) p += 4;
    else if (!strncmp(p, "ecdsa-with-", 11)) p += 11;
    char out[32];
    if (!strncmp(p, "sha-", 4) || !strncmp(p, "sha2-", 5)) { snprintf(out, sizeof out, "sha%s", p + (p[3] == '-' ? 4 : 5)); p = out; }
    if (!strcmp(p, "sha2-1")) p = "";
    k = strlen(p);
    return xs_digest_len(p) ? xs_str_new(p, k) : xs_str_new("", 0);
}
XS_NOINLINE xs_str *xs_crypto_digest(xs_str *alg, xs_str *data, xs_str *enc) {
    uint8_t out[64];
    xs_digest_raw(alg->data, data->data, (size_t)data->len, "", 0, out);
    return xs_digest_encode(out, xs_digest_len(alg->data), enc->data);
}
/* HMAC (RFC 2104) over any of the four */
XS_NOINLINE xs_str *xs_crypto_hmac(xs_str *alg, xs_str *key, xs_str *data, xs_str *enc) {
    int bl = xs_digest_block(alg->data), dl = xs_digest_len(alg->data);
    uint8_t k[128] = {0}, ipad[128], opad[128], inner[64], out[64];
    if ((int)key->len > bl) xs_digest_raw(alg->data, key->data, (size_t)key->len, "", 0, k);
    else memcpy(k, key->data, (size_t)key->len);
    for (int i = 0; i < bl; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    xs_digest_raw(alg->data, ipad, (size_t)bl, data->data, (size_t)data->len, inner);
    xs_digest_raw(alg->data, opad, (size_t)bl, inner, (size_t)dl, out);
    return xs_digest_encode(out, dl, enc->data);
}
/* HMAC with a key of bytes (createHmac(alg, buffer)), over UTF-8 data, as bytes */
XS_NOINLINE xs_Uint8Array *xs_crypto_hmac_bytes(xs_str *alg, xs_Uint8Array *key, xs_str *data) {
    int bl = xs_digest_block(alg->data), dl = xs_digest_len(alg->data);
    uint8_t k[128] = {0}, ipad[128], opad[128], inner[64];
    if ((int)key->f_length > bl) xs_digest_raw(alg->data, key->data, (size_t)key->f_length, "", 0, k);
    else memcpy(k, key->data, (size_t)key->f_length);
    for (int i = 0; i < bl; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    xs_digest_raw(alg->data, ipad, (size_t)bl, data->data, (size_t)data->len, inner);
    xs_Uint8Array *r = xs_u8_new(dl);
    xs_digest_raw(alg->data, opad, (size_t)bl, inner, (size_t)dl, r->data);
    return r;
}
/* the digest of bytes, as bytes (RSA padding and signatures hash binary data) */
XS_NOINLINE xs_Uint8Array *xs_crypto_digest_bytes(xs_str *alg, xs_Uint8Array *data) {
    int dl = xs_digest_len(alg->data);
    xs_Uint8Array *r = xs_u8_new(dl);
    xs_digest_raw(alg->data, data->data, (size_t)data->f_length, "", 0, r->data);
    return r;
}
/* n random bytes from the system's entropy (randomBytes, randomInt, RSA padding) */
XS_NOINLINE xs_Uint8Array *xs_crypto_random_bytes(int64_t n) {
    if (n < 0) n = 0;
    xs_Uint8Array *r = xs_u8_new(n);
    if (n) xs_random_bytes(r->data, (size_t)n);
    return r;
}
/* randomUUID: version 4, variant 10 (RFC 9562), from the system's entropy as Node's */
XS_NOINLINE xs_str *xs_crypto_uuid(void) {
    uint8_t b[16];
    xs_random_bytes(b, 16);
    b[6] = (uint8_t)((b[6] & 0x0f) | 0x40);
    b[8] = (uint8_t)((b[8] & 0x3f) | 0x80);
    static const char hx[] = "0123456789abcdef";
    char s[36];
    size_t k = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) s[k++] = '-';
        s[k++] = hx[b[i] >> 4]; s[k++] = hx[b[i] & 15];
    }
    return xs_str_new(s, k);
}

/* ---- BearSSL (build/libbearssl.a, linked with node:crypto): ciphers, KDFs, RSA/ECDSA signatures and key
   generation. The functions below are only referenced by the parts of xisto:crypto that use them, so a
   program that only hashes takes nothing from the library. ---- */
#include <bearssl.h>

/* a digest by Node's name: BearSSL's hash class (for its RSA and ECDSA code), its PKCS#1 OID (NULL for md5),
   or NULL when unknown */
static const br_hash_class *xs_br_hash(const char *alg, const unsigned char **oid) {
    const unsigned char *o = NULL;
    const br_hash_class *h = NULL;
    if (strcmp(alg, "sha1") == 0) { h = &br_sha1_vtable; o = BR_HASH_OID_SHA1; }
    else if (strcmp(alg, "sha224") == 0) { h = &br_sha224_vtable; o = BR_HASH_OID_SHA224; }
    else if (strcmp(alg, "sha256") == 0) { h = &br_sha256_vtable; o = BR_HASH_OID_SHA256; }
    else if (strcmp(alg, "sha384") == 0) { h = &br_sha384_vtable; o = BR_HASH_OID_SHA384; }
    else if (strcmp(alg, "sha512") == 0) { h = &br_sha512_vtable; o = BR_HASH_OID_SHA512; }
    if (oid) *oid = o;
    return h;
}
static xs_Uint8Array *xs_buffer_of(const void *p, size_t n) {
    xs_Uint8Array *r = xs_u8_raw((int64_t)n, true, false);
    if (n) memcpy(r->data, p, n);
    return r;
}

/* (hashes, HMACs and the KDFs are xisto's own C above: a program that only hashes takes nothing from BearSSL) */
/* the digest of bytes by any of md5, sha1, sha224, sha256, sha384, sha512 (null: an unknown algorithm) */
XS_NOINLINE xs_Uint8Array *xs_crypto_hash2(xs_str *alg, xs_Uint8Array *data) {
    int dl = xs_digest_len(alg->data);
    if (!dl) return NULL;
    uint8_t out[64];
    xs_digest_raw(alg->data, data->data, (size_t)data->f_length, "", 0, out);
    return xs_buffer_of(out, (size_t)dl);
}
/* the same digest as text in an encoding (hex, base64, base64url, latin1) */
XS_NOINLINE xs_str *xs_crypto_hash2_text(xs_str *alg, xs_Uint8Array *data, xs_str *enc) {
    uint8_t out[64];
    xs_digest_raw(alg->data, data->data, (size_t)data->f_length, "", 0, out);
    return xs_digest_encode(out, xs_digest_len(alg->data), enc->data);
}
/* HMAC of bytes with a key of bytes, as text in an encoding */
XS_NOINLINE xs_str *xs_crypto_hmac2_text(xs_str *alg, xs_Uint8Array *key, xs_Uint8Array *data, xs_str *enc) {
    uint8_t out[64];
    xs_hmac_raw(alg->data, key->data, (size_t)key->f_length, data->data, (size_t)data->f_length, "", 0, out);
    return xs_digest_encode(out, xs_digest_len(alg->data), enc->data);
}
/* HMAC of bytes with a key of bytes, by any of those digests (null: an unknown algorithm) */
XS_NOINLINE xs_Uint8Array *xs_crypto_hmac2(xs_str *alg, xs_Uint8Array *key, xs_Uint8Array *data) {
    int dl = xs_digest_len(alg->data);
    if (!dl) return NULL;
    uint8_t out[64];
    xs_hmac_raw(alg->data, key->data, (size_t)key->f_length, data->data, (size_t)data->f_length, "", 0, out);
    return xs_buffer_of(out, (size_t)dl);
}

/* ---- PBKDF2 (RFC 8018) over HMAC with any of the digests ---- */
XS_NOINLINE xs_Uint8Array *xs_crypto_pbkdf2(xs_Uint8Array *pw, xs_Uint8Array *salt, int64_t iters, int64_t keylen, xs_str *alg) {
    const char *a = alg->data;
    size_t hl = (size_t)xs_digest_len(a);
    if (!hl) return NULL;
    xs_Uint8Array *r = xs_u8_raw(keylen, true, true);
    uint8_t u[64], t[64];
    for (uint32_t block = 1; (int64_t)(block - 1) * (int64_t)hl < keylen; block++) {
        uint8_t be[4] = {(uint8_t)(block >> 24), (uint8_t)(block >> 16), (uint8_t)(block >> 8), (uint8_t)block};
        xs_hmac_raw(a, pw->data, (size_t)pw->f_length, salt->data, (size_t)salt->f_length, be, 4, u);
        memcpy(t, u, hl);
        for (int64_t i = 1; i < iters; i++) {
            xs_hmac_raw(a, pw->data, (size_t)pw->f_length, u, hl, "", 0, u);
            for (size_t k = 0; k < hl; k++) t[k] ^= u[k];
        }
        size_t off = (size_t)(block - 1) * hl, n = (size_t)keylen - off < hl ? (size_t)keylen - off : hl;
        memcpy(r->data + off, t, n);
    }
    return r;
}

/* ---- scrypt (RFC 7914) ---- */
static void xs_salsa8(uint32_t b[16]) {
    uint32_t x[16];
    memcpy(x, b, 64);
    for (int i = 0; i < 8; i += 2) {
#define XS_R(a, n) (((a) << (n)) | ((a) >> (32 - (n))))
        x[4] ^= XS_R(x[0] + x[12], 7); x[8] ^= XS_R(x[4] + x[0], 9); x[12] ^= XS_R(x[8] + x[4], 13); x[0] ^= XS_R(x[12] + x[8], 18);
        x[9] ^= XS_R(x[5] + x[1], 7); x[13] ^= XS_R(x[9] + x[5], 9); x[1] ^= XS_R(x[13] + x[9], 13); x[5] ^= XS_R(x[1] + x[13], 18);
        x[14] ^= XS_R(x[10] + x[6], 7); x[2] ^= XS_R(x[14] + x[10], 9); x[6] ^= XS_R(x[2] + x[14], 13); x[10] ^= XS_R(x[6] + x[2], 18);
        x[3] ^= XS_R(x[15] + x[11], 7); x[7] ^= XS_R(x[3] + x[15], 9); x[11] ^= XS_R(x[7] + x[3], 13); x[15] ^= XS_R(x[11] + x[7], 18);
        x[1] ^= XS_R(x[0] + x[3], 7); x[2] ^= XS_R(x[1] + x[0], 9); x[3] ^= XS_R(x[2] + x[1], 13); x[0] ^= XS_R(x[3] + x[2], 18);
        x[6] ^= XS_R(x[5] + x[4], 7); x[7] ^= XS_R(x[6] + x[5], 9); x[4] ^= XS_R(x[7] + x[6], 13); x[5] ^= XS_R(x[4] + x[7], 18);
        x[11] ^= XS_R(x[10] + x[9], 7); x[8] ^= XS_R(x[11] + x[10], 9); x[9] ^= XS_R(x[8] + x[11], 13); x[10] ^= XS_R(x[9] + x[8], 18);
        x[12] ^= XS_R(x[15] + x[14], 7); x[13] ^= XS_R(x[12] + x[15], 9); x[14] ^= XS_R(x[13] + x[12], 13); x[15] ^= XS_R(x[14] + x[13], 18);
#undef XS_R
    }
    for (int i = 0; i < 16; i++) b[i] += x[i];
}
/* BlockMix: B (2r blocks of 16 words) through Salsa20/8 into Y, then back in the even-odd order */
static void xs_blockmix(uint32_t *b, uint32_t *y, size_t r) {
    uint32_t x[16];
    memcpy(x, &b[(2 * r - 1) * 16], 64);
    for (size_t i = 0; i < 2 * r; i++) {
        for (int k = 0; k < 16; k++) x[k] ^= b[i * 16 + k];
        xs_salsa8(x);
        memcpy(&y[i * 16], x, 64);
    }
    for (size_t i = 0; i < r; i++) memcpy(&b[i * 16], &y[(2 * i) * 16], 64);
    for (size_t i = 0; i < r; i++) memcpy(&b[(r + i) * 16], &y[(2 * i + 1) * 16], 64);
}
/* null when N, r and p ask for more memory than maxmem (or N is not a power of two above 1) */
XS_NOINLINE xs_Uint8Array *xs_crypto_scrypt(xs_Uint8Array *pw, xs_Uint8Array *salt, int64_t N, int64_t r, int64_t p, int64_t keylen, int64_t maxmem) {
    if (N < 2 || (N & (N - 1)) != 0 || r < 1 || p < 1) return NULL;
    uint64_t need = 128ull * (uint64_t)r * ((uint64_t)N + (uint64_t)p + 2);
    if (need > (uint64_t)maxmem) return NULL;
    size_t blen = 128 * (size_t)r * (size_t)p;
    xs_Uint8Array *bs = xs_u8_raw((int64_t)blen, false, false);
    xs_str *alg = xs_str_new("sha256", 6);
    xs_Uint8Array *b0 = xs_crypto_pbkdf2(pw, salt, 1, (int64_t)blen, alg);
    memcpy(bs->data, b0->data, blen);
    xs_release(b0);
    size_t words = 32 * (size_t)r;
    uint32_t *x = malloc(words * 4), *y = malloc(words * 4), *v = malloc(words * 4 * (size_t)N);
    if (!x || !y || !v) xs_panic("out of memory");
    for (int64_t k = 0; k < p; k++) {
        uint8_t *blk = bs->data + (size_t)k * 128 * (size_t)r;
        for (size_t i = 0; i < words; i++) x[i] = (uint32_t)blk[4 * i] | (uint32_t)blk[4 * i + 1] << 8 | (uint32_t)blk[4 * i + 2] << 16 | (uint32_t)blk[4 * i + 3] << 24;
        for (int64_t i = 0; i < N; i++) {
            memcpy(&v[(size_t)i * words], x, words * 4);
            xs_blockmix(x, y, (size_t)r);
        }
        for (int64_t i = 0; i < N; i++) {
            uint64_t j = x[(2 * (size_t)r - 1) * 16] & (uint64_t)(N - 1);
            for (size_t w = 0; w < words; w++) x[w] ^= v[j * words + w];
            xs_blockmix(x, y, (size_t)r);
        }
        for (size_t i = 0; i < words; i++) { blk[4 * i] = (uint8_t)x[i]; blk[4 * i + 1] = (uint8_t)(x[i] >> 8); blk[4 * i + 2] = (uint8_t)(x[i] >> 16); blk[4 * i + 3] = (uint8_t)(x[i] >> 24); }
    }
    free(x); free(y); free(v);
    xs_Uint8Array *out = xs_crypto_pbkdf2(pw, bs, 1, keylen, alg);
    xs_release(bs);
    xs_release(alg);
    return out;
}

/* ---- HKDF (RFC 5869) ---- */
XS_NOINLINE xs_Uint8Array *xs_crypto_hkdf(xs_str *alg, xs_Uint8Array *ikm, xs_Uint8Array *salt, xs_Uint8Array *info, int64_t keylen) {
    const char *a = alg->data;
    size_t hl = (size_t)xs_digest_len(a);
    if (!hl) return NULL;
    uint8_t prk[64], t[64];
    xs_hmac_raw(a, salt->data, (size_t)salt->f_length, ikm->data, (size_t)ikm->f_length, "", 0, prk); /* (no salt: HMAC's zero key) */
    xs_Uint8Array *r = xs_u8_raw(keylen, true, false);
    size_t ilen = (size_t)info->f_length, tl = 0;
    uint8_t *msg = malloc(hl + ilen + 1);
    if (!msg) xs_panic("out of memory");
    for (size_t off = 0, i = 1; off < (size_t)keylen; i++) {
        memcpy(msg, t, tl);
        memcpy(msg + tl, info->data, ilen);
        msg[tl + ilen] = (uint8_t)i;
        xs_hmac_raw(a, prk, hl, msg, tl + ilen + 1, "", 0, t);
        tl = hl;
        size_t n = (size_t)keylen - off < hl ? (size_t)keylen - off : hl;
        memcpy(r->data + off, t, n);
        off += n;
    }
    free(msg);
    return r;
}

/* ---- AES: CBC (PKCS#7 padding), CTR (128-bit counter, as OpenSSL), GCM, ECB ----
   The state lives in the bytes of a Uint8Array the Cipher object holds (freed with it); the pointers BearSSL
   keeps inside it point into those same bytes, which never move. */
enum { XS_AES_CBC = 1, XS_AES_CTR, XS_AES_GCM, XS_AES_ECB };
typedef struct {
    int kind, decrypt, padding, finished, error, tag_set, aad_done;
    union { br_aes_ct64_cbcenc_keys ce; br_aes_ct64_cbcdec_keys cd; br_aes_ct64_ctr_keys ctr; } k;
    br_gcm_context gcm;
    uint8_t iv[16];      /* CBC: the chaining value; CTR: the counter block */
    uint8_t buf[32];     /* CBC/ECB: the bytes of a block not yet complete (and, decrypting, the last block held back);
                            CTR: the keystream block in use */
    size_t nbuf;         /* CTR: bytes of buf's keystream used */
    uint8_t tag[16];
    size_t taglen;
} xs_aes_state;
static xs_aes_state *xs_aes_of(xs_Uint8Array *st) { return (xs_aes_state *)(void *)st->data; }

XS_NOINLINE xs_Uint8Array *xs_crypto_cipher_new(int64_t kind, xs_Uint8Array *key, xs_Uint8Array *iv, bool decrypt) {
    xs_Uint8Array *st = xs_u8_raw((int64_t)sizeof(xs_aes_state), false, true);
    xs_aes_state *s = xs_aes_of(st);
    s->kind = (int)kind;
    s->decrypt = decrypt;
    s->padding = 1;
    s->taglen = 16;
    size_t kl = (size_t)key->f_length;
    if (kind == XS_AES_CBC || kind == XS_AES_ECB) {
        if (decrypt) br_aes_ct64_cbcdec_init(&s->k.cd, key->data, kl);
        else br_aes_ct64_cbcenc_init(&s->k.ce, key->data, kl);
        if (kind == XS_AES_CBC) memcpy(s->iv, iv->data, 16);
    } else {
        br_aes_ct64_ctr_init(&s->k.ctr, key->data, kl);
        if (kind == XS_AES_CTR) { memcpy(s->iv, iv->data, 16); s->nbuf = 16; }
        else {
            br_gcm_init(&s->gcm, &s->k.ctr.vtable, br_ghash_ctmul64);
            br_gcm_reset(&s->gcm, iv->data, (size_t)iv->f_length);
        }
    }
    return st;
}
/* one AES block, encrypted (ECB): CBC with a zero chaining value */
static void xs_aes_ecb1(xs_aes_state *s, uint8_t *blk) {
    uint8_t zero[16] = {0};
    if (s->decrypt) br_aes_ct64_cbcdec_run(&s->k.cd, zero, blk, 16);
    else br_aes_ct64_cbcenc_run(&s->k.ce, zero, blk, 16);
}
static void xs_ctr_next(xs_aes_state *s) {
    uint8_t zero[16] = {0};
    /* the keystream block: AES(counter), by CTR mode over 16 zero bytes from the counter's 32 low bits */
    memcpy(s->buf, zero, 16);
    uint32_t cc = (uint32_t)s->iv[12] << 24 | (uint32_t)s->iv[13] << 16 | (uint32_t)s->iv[14] << 8 | s->iv[15];
    br_aes_ct64_ctr_run(&s->k.ctr, s->iv, cc, s->buf, 16);
    for (int i = 15; i >= 0; i--) if (++s->iv[i] != 0) break; /* the whole 128-bit counter, big-endian */
    s->nbuf = 0;
}
/* CBC/ECB blocks of data, in place */
static void xs_aes_blocks(xs_aes_state *s, uint8_t *p, size_t n) {
    if (s->kind == XS_AES_ECB) { for (size_t i = 0; i < n; i += 16) xs_aes_ecb1(s, p + i); return; }
    if (s->decrypt) br_aes_ct64_cbcdec_run(&s->k.cd, s->iv, p, n);
    else br_aes_ct64_cbcenc_run(&s->k.ce, s->iv, p, n);
}
XS_NOINLINE xs_Uint8Array *xs_crypto_cipher_update(xs_Uint8Array *st, xs_Uint8Array *data) {
    xs_aes_state *s = xs_aes_of(st);
    size_t n = (size_t)data->f_length;
    if (s->kind == XS_AES_CTR || s->kind == XS_AES_GCM) {
        xs_Uint8Array *r = xs_buffer_of(data->data, n);
        if (s->kind == XS_AES_GCM) {
            if (!s->aad_done) { br_gcm_flip(&s->gcm); s->aad_done = 1; }
            br_gcm_run(&s->gcm, !s->decrypt, r->data, n);
            return r;
        }
        for (size_t i = 0; i < n; i++) {
            if (s->nbuf == 16) xs_ctr_next(s);
            r->data[i] ^= s->buf[s->nbuf++];
        }
        return r;
    }
    /* CBC/ECB: whole blocks out; decrypting with padding, the last whole block is held back for final() */
    size_t have = s->nbuf + n;
    size_t keep = have % 16;
    if (s->decrypt && s->padding && keep == 0 && have > 0) keep = 16;
    size_t outn = have - keep;
    xs_Uint8Array *r = xs_u8_raw((int64_t)outn, true, false);
    size_t from_buf = s->nbuf < outn ? s->nbuf : outn;
    memcpy(r->data, s->buf, from_buf);
    memcpy(r->data + from_buf, data->data, outn - from_buf);
    /* what stays: the rest of buf, then the rest of data */
    uint8_t tmp[32];
    size_t t = 0;
    for (size_t i = from_buf; i < s->nbuf; i++) tmp[t++] = s->buf[i];
    for (size_t i = outn - from_buf; i < n; i++) tmp[t++] = data->data[i];
    memcpy(s->buf, tmp, t);
    s->nbuf = t;
    if (outn) xs_aes_blocks(s, r->data, outn);
    return r;
}
/* final(): the last block (CBC/ECB), the tag computed (GCM encrypting) or checked (decrypting); null on an
   error, then xs_crypto_cipher_error says which: 1 bad decrypt, 2 wrong final block length, 3 tag not set */
XS_NOINLINE xs_Uint8Array *xs_crypto_cipher_final(xs_Uint8Array *st) {
    xs_aes_state *s = xs_aes_of(st);
    s->finished = 1;
    if (s->kind == XS_AES_CTR) return xs_u8_raw(0, true, false);
    if (s->kind == XS_AES_GCM) {
        if (!s->aad_done) { br_gcm_flip(&s->gcm); s->aad_done = 1; }
        if (!s->decrypt) { br_gcm_get_tag_trunc(&s->gcm, s->tag, s->taglen); return xs_u8_raw(0, true, false); }
        if (!s->tag_set) { s->error = 3; return NULL; }
        if (!br_gcm_check_tag_trunc(&s->gcm, s->tag, s->taglen)) { s->error = 1; return NULL; }
        return xs_u8_raw(0, true, false);
    }
    if (!s->padding) {
        if (s->nbuf) { s->error = 2; return NULL; }
        return xs_u8_raw(0, true, false);
    }
    if (!s->decrypt) {
        uint8_t pad = (uint8_t)(16 - s->nbuf);
        for (size_t i = s->nbuf; i < 16; i++) s->buf[i] = pad;
        xs_aes_blocks(s, s->buf, 16);
        return xs_buffer_of(s->buf, 16);
    }
    if (s->nbuf != 16) { s->error = 2; return NULL; }
    xs_aes_blocks(s, s->buf, 16);
    uint8_t pad = s->buf[15];
    if (pad == 0 || pad > 16) { s->error = 1; return NULL; }
    for (int i = 16 - pad; i < 16; i++) if (s->buf[i] != pad) { s->error = 1; return NULL; }
    return xs_buffer_of(s->buf, (size_t)(16 - pad));
}
XS_NOINLINE int64_t xs_crypto_cipher_error(xs_Uint8Array *st) { return xs_aes_of(st)->error; }
XS_NOINLINE void xs_crypto_cipher_padding(xs_Uint8Array *st, bool on) { xs_aes_of(st)->padding = on; }
XS_NOINLINE void xs_crypto_cipher_aad(xs_Uint8Array *st, xs_Uint8Array *aad) {
    xs_aes_state *s = xs_aes_of(st);
    br_gcm_aad_inject(&s->gcm, aad->data, (size_t)aad->f_length);
}
XS_NOINLINE void xs_crypto_cipher_set_tag(xs_Uint8Array *st, xs_Uint8Array *tag) {
    xs_aes_state *s = xs_aes_of(st);
    s->taglen = (size_t)tag->f_length;
    memcpy(s->tag, tag->data, s->taglen);
    s->tag_set = 1;
}
XS_NOINLINE void xs_crypto_cipher_tag_length(xs_Uint8Array *st, int64_t n) { xs_aes_of(st)->taglen = (size_t)n; }
XS_NOINLINE xs_Uint8Array *xs_crypto_cipher_tag(xs_Uint8Array *st) {
    xs_aes_state *s = xs_aes_of(st);
    return xs_buffer_of(s->tag, s->taglen);
}

/* ---- RSA and EC keys: the numbers of a key travel packed, each as a 4-byte big-endian length and its bytes
   (RSA private: n e d p q dp dq qi; RSA public: n e; EC: the curve's id, then d or the public point) ---- */
static const uint8_t *xs_pk_field(const xs_Uint8Array *k, int idx, size_t *len) {
    size_t at = 0, n = (size_t)k->f_length;
    for (int i = 0;; i++) {
        if (at + 4 > n) { *len = 0; return NULL; }
        size_t l = (size_t)k->data[at] << 24 | (size_t)k->data[at + 1] << 16 | (size_t)k->data[at + 2] << 8 | k->data[at + 3];
        at += 4;
        if (at + l > n) { *len = 0; return NULL; }
        if (i == idx) { *len = l; return k->data + at; }
        at += l;
    }
}
static void xs_pk_put(uint8_t **p, const uint8_t *v, size_t n) {
    while (n > 1 && *v == 0) { v++; n--; } /* minimal unsigned big-endian */
    (*p)[0] = (uint8_t)(n >> 24); (*p)[1] = (uint8_t)(n >> 16); (*p)[2] = (uint8_t)(n >> 8); (*p)[3] = (uint8_t)n;
    memcpy(*p + 4, v, n);
    *p += 4 + n;
}
static bool xs_rsa_priv(const xs_Uint8Array *k, br_rsa_private_key *sk) {
    size_t nl;
    const uint8_t *n = xs_pk_field(k, 0, &nl);
    if (!n) return false;
    while (nl > 0 && *n == 0) { n++; nl--; }
    if (!nl) return false;
    unsigned bits = (unsigned)nl * 8;
    for (uint8_t top = n[0]; !(top & 0x80); top <<= 1) bits--;
    sk->n_bitlen = bits;
    sk->p = (unsigned char *)xs_pk_field(k, 3, &sk->plen);
    sk->q = (unsigned char *)xs_pk_field(k, 4, &sk->qlen);
    sk->dp = (unsigned char *)xs_pk_field(k, 5, &sk->dplen);
    sk->dq = (unsigned char *)xs_pk_field(k, 6, &sk->dqlen);
    sk->iq = (unsigned char *)xs_pk_field(k, 7, &sk->iqlen);
    return sk->p && sk->q && sk->dp && sk->dq && sk->iq;
}
static bool xs_rsa_pub(const xs_Uint8Array *k, br_rsa_public_key *pk) {
    pk->n = (unsigned char *)xs_pk_field(k, 0, &pk->nlen);
    pk->e = (unsigned char *)xs_pk_field(k, 1, &pk->elen);
    while (pk->n && pk->nlen > 0 && *pk->n == 0) { pk->n++; pk->nlen--; }
    return pk->n && pk->e && pk->nlen;
}
static void xs_br_rng(br_hmac_drbg_context *rng) {
    uint8_t seed[48];
    xs_random_bytes(seed, sizeof seed);
    br_hmac_drbg_init(rng, &br_sha256_vtable, seed, sizeof seed);
}

/* RSA signature of a hash: PKCS#1 v1.5 (salt < 0) or PSS with that salt length; null on a failure */
XS_NOINLINE xs_Uint8Array *xs_crypto_rsa_sign(xs_str *alg, xs_Uint8Array *hash, xs_Uint8Array *key, int64_t salt) {
    const unsigned char *oid;
    const br_hash_class *h = xs_br_hash(alg->data, &oid);
    br_rsa_private_key sk;
    if (!h || !xs_rsa_priv(key, &sk)) return NULL;
    size_t len = (sk.n_bitlen + 7) / 8;
    xs_Uint8Array *r = xs_u8_raw((int64_t)len, true, true);
    uint32_t ok;
    if (salt < 0) ok = oid ? br_rsa_i31_pkcs1_sign(oid, hash->data, (size_t)hash->f_length, &sk, r->data) : 0;
    else {
        br_hmac_drbg_context rng;
        xs_br_rng(&rng);
        ok = br_rsa_i31_pss_sign(&rng.vtable, h, h, hash->data, (size_t)salt, &sk, r->data);
    }
    if (!ok) { xs_release(r); return NULL; }
    return r;
}
/* RSA signature check (PKCS#1 v1.5 when salt < 0, else PSS) */
XS_NOINLINE bool xs_crypto_rsa_verify(xs_str *alg, xs_Uint8Array *hash, xs_Uint8Array *key, xs_Uint8Array *sig, int64_t salt) {
    const unsigned char *oid;
    const br_hash_class *h = xs_br_hash(alg->data, &oid);
    br_rsa_public_key pk;
    if (!h || !xs_rsa_pub(key, &pk) || (size_t)sig->f_length != pk.nlen) return false;
    if (salt < 0) {
        uint8_t out[64];
        if (!oid || !br_rsa_i31_pkcs1_vrfy(sig->data, (size_t)sig->f_length, oid, (size_t)hash->f_length, &pk, out)) return false;
        return xs_ct_equal(out, hash->data, (size_t)hash->f_length);
    }
    return br_rsa_i31_pss_vrfy(sig->data, (size_t)sig->f_length, h, h, hash->data, (size_t)salt, &pk) == 1;
}
/* RSA key generation: bits (BearSSL's i31 engine: up to 4096) and the public exponent; the key packed (n e d p q dp dq qi) */
#define XS_RSA_MAX_BITS 4096
XS_NOINLINE xs_Uint8Array *xs_crypto_rsa_keygen(int64_t bits, int64_t e) {
    br_hmac_drbg_context rng;
    xs_br_rng(&rng);
    br_rsa_private_key sk;
    br_rsa_public_key pk;
    size_t kp = BR_RSA_KBUF_PRIV_SIZE(XS_RSA_MAX_BITS), kb = BR_RSA_KBUF_PUB_SIZE(XS_RSA_MAX_BITS);
    uint8_t *privbuf = malloc(kp), *pubbuf = malloc(kb), *d = malloc(XS_RSA_MAX_BITS / 8 + 1);
    if (!privbuf || !pubbuf || !d) xs_panic("out of memory");
    if (!br_rsa_i31_keygen(&rng.vtable, &sk, privbuf, &pk, pubbuf, (unsigned)bits, (uint32_t)e)) { free(privbuf); free(pubbuf); free(d); return NULL; }
    size_t dl = br_rsa_i31_compute_privexp(d, &sk, (uint32_t)e);
    size_t total = 32 + pk.nlen + pk.elen + dl + sk.plen + sk.qlen + sk.dplen + sk.dqlen + sk.iqlen;
    xs_Uint8Array *r = xs_u8_raw((int64_t)total, false, false);
    uint8_t *p = r->data;
    xs_pk_put(&p, pk.n, pk.nlen); xs_pk_put(&p, pk.e, pk.elen); xs_pk_put(&p, d, dl);
    xs_pk_put(&p, sk.p, sk.plen); xs_pk_put(&p, sk.q, sk.qlen); xs_pk_put(&p, sk.dp, sk.dplen); xs_pk_put(&p, sk.dq, sk.dqlen); xs_pk_put(&p, sk.iq, sk.iqlen);
    r->f_length = p - r->data;
    free(privbuf); free(pubbuf); free(d);
    return r;
}
/* EC (P-256, P-384, P-521): a new private scalar; the public point from it; ECDSA. P-256 on its own fast code,
   the other two on the generic one (not br_ec_get_default(), which also brings Curve25519) */
static const br_ec_impl *xs_ec_impl(int curve) {
    if (curve == BR_EC_secp256r1) {
        const br_ec_impl *m = br_ec_p256_m64_get(); /* (NULL without 64x64 multiplications: the generic code) */
        if (m) return m;
    }
    return &br_ec_prime_i31;
}
XS_NOINLINE xs_Uint8Array *xs_crypto_ec_keygen(int64_t curve) {
    br_hmac_drbg_context rng;
    xs_br_rng(&rng);
    br_ec_private_key sk;
    uint8_t kbuf[BR_EC_KBUF_PRIV_MAX_SIZE];
    size_t n = br_ec_keygen(&rng.vtable, xs_ec_impl((int)curve), &sk, kbuf, (int)curve);
    return n ? xs_buffer_of(kbuf, n) : NULL;
}
XS_NOINLINE xs_Uint8Array *xs_crypto_ec_public(int64_t curve, xs_Uint8Array *d) {
    br_ec_private_key sk = {(int)curve, d->data, (size_t)d->f_length};
    uint8_t kbuf[BR_EC_KBUF_PUB_MAX_SIZE];
    size_t n = br_ec_compute_pub(xs_ec_impl((int)curve), NULL, kbuf, &sk);
    return n ? xs_buffer_of(kbuf, n) : NULL;
}
/* ECDSA of a hash with the private scalar d; DER (raw false) or r||s (IEEE P1363) */
XS_NOINLINE xs_Uint8Array *xs_crypto_ec_sign(xs_str *alg, xs_Uint8Array *hash, int64_t curve, xs_Uint8Array *d, bool raw) {
    const br_hash_class *h = xs_br_hash(alg->data, NULL);
    if (!h) return NULL;
    br_ec_private_key sk = {(int)curve, d->data, (size_t)d->f_length};
    uint8_t sig[160];
    size_t n = raw ? br_ecdsa_i31_sign_raw(xs_ec_impl((int)curve), h, hash->data, &sk, sig) : br_ecdsa_i31_sign_asn1(xs_ec_impl((int)curve), h, hash->data, &sk, sig);
    return n ? xs_buffer_of(sig, n) : NULL;
}
XS_NOINLINE bool xs_crypto_ec_verify(xs_Uint8Array *hash, int64_t curve, xs_Uint8Array *q, xs_Uint8Array *sig, bool raw) {
    br_ec_public_key pk = {(int)curve, q->data, (size_t)q->f_length};
    if (raw) return br_ecdsa_i31_vrfy_raw(xs_ec_impl((int)curve), hash->data, (size_t)hash->f_length, &pk, sig->data, (size_t)sig->f_length) == 1;
    return br_ecdsa_i31_vrfy_asn1(xs_ec_impl((int)curve), hash->data, (size_t)hash->f_length, &pk, sig->data, (size_t)sig->f_length) == 1;
}
