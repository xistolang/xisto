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

/* one digest of any of the four, by Node's name: the length (0: not a digest xisto has) */
static int xs_digest_len(const char *alg) {
    if (strcmp(alg, "md5") == 0) return 16;
    if (strcmp(alg, "sha1") == 0) return 20;
    if (strcmp(alg, "sha256") == 0) return 32;
    if (strcmp(alg, "sha512") == 0) return 64;
    return 0;
}
static int xs_digest_block(const char *alg) { return strcmp(alg, "sha512") == 0 ? 128 : 64; }
static void xs_digest_raw(const char *alg, const void *a, size_t an, const void *b, size_t bn, uint8_t *out) {
    if (strcmp(alg, "md5") == 0) { xs_md5 c; xs_md5_init(&c); xs_md5_update(&c, a, an); xs_md5_update(&c, b, bn); xs_md5_final(&c, out); }
    else if (strcmp(alg, "sha1") == 0) { xs_sha1 c; xs_sha1_init(&c); xs_sha1_update(&c, a, an); xs_sha1_update(&c, b, bn); xs_sha1_final(&c, out); }
    else if (strcmp(alg, "sha256") == 0) { xs_sha256 c; xs_sha256_init(&c); xs_sha256_update(&c, a, an); xs_sha256_update(&c, b, bn); xs_sha256_final(&c, out); }
    else { xs_sha512 c; xs_sha512_init(&c); xs_sha512_update(&c, a, an); xs_sha512_update(&c, b, bn); xs_sha512_final(&c, out); }
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
