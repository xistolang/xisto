/* oracledb: the hashing and ciphers Oracle's O5LOGON (12c verifier) needs, in portable C: SHA-512, HMAC-SHA512,
   PBKDF2-SHA512 (FIPS 180-4, RFC 2104, RFC 8018) and AES-256 in CBC mode with a zero IV (FIPS 197). The AES here
   is the plain table-free byte form: not constant-time, used only on the login handshake's session keys. */

typedef struct { uint64_t h[8]; uint8_t buf[128]; uint64_t total; size_t n; } xs_ora_sha512;
static const uint64_t xs_ora_k512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
    0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
    0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
    0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
    0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
    0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
    0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
    0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};
#define XS_ORA_ROR(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
static void xs_ora_sha512_block(xs_ora_sha512 *c, const uint8_t *p) {
    uint64_t w[80], a, b, cc, d, e, f, g, h;
    for (int i = 0; i < 16; i++) {
        w[i] = 0;
        for (int k = 0; k < 8; k++) w[i] = (w[i] << 8) | p[i * 8 + k];
    }
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = XS_ORA_ROR(w[i - 15], 1) ^ XS_ORA_ROR(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = XS_ORA_ROR(w[i - 2], 19) ^ XS_ORA_ROR(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (int i = 0; i < 80; i++) {
        uint64_t t1 = h + (XS_ORA_ROR(e, 14) ^ XS_ORA_ROR(e, 18) ^ XS_ORA_ROR(e, 41)) + ((e & f) ^ (~e & g)) + xs_ora_k512[i] + w[i];
        uint64_t t2 = (XS_ORA_ROR(a, 28) ^ XS_ORA_ROR(a, 34) ^ XS_ORA_ROR(a, 39)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
static void xs_ora_sha512_init(xs_ora_sha512 *c) {
    static const uint64_t iv[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                                   0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    memcpy(c->h, iv, sizeof iv);
    c->total = 0;
    c->n = 0;
}
static void xs_ora_sha512_update(xs_ora_sha512 *c, const void *data, size_t len) {
    const uint8_t *p = data;
    c->total += len;
    while (len > 0) {
        size_t k = 128 - c->n < len ? 128 - c->n : len;
        memcpy(c->buf + c->n, p, k);
        c->n += k; p += k; len -= k;
        if (c->n == 128) { xs_ora_sha512_block(c, c->buf); c->n = 0; }
    }
}
static void xs_ora_sha512_final(xs_ora_sha512 *c, uint8_t out[64]) {
    uint64_t bits = c->total * 8;
    uint8_t pad = 0x80, zero = 0, lenb[16] = {0};
    xs_ora_sha512_update(c, &pad, 1);
    while (c->n != 112) xs_ora_sha512_update(c, &zero, 1);
    for (int i = 0; i < 8; i++) lenb[15 - i] = (uint8_t)(bits >> (8 * i));
    xs_ora_sha512_update(c, lenb, 16);
    for (int i = 0; i < 8; i++)
        for (int k = 0; k < 8; k++) out[i * 8 + k] = (uint8_t)(c->h[i] >> (56 - 8 * k));
}
static void xs_ora_hmac512(const uint8_t *key, size_t klen, const uint8_t *data, size_t len, uint8_t out[64]) {
    uint8_t k[128] = {0}, ipad[128], opad[128], inner[64];
    if (klen > 128) { xs_ora_sha512 c; xs_ora_sha512_init(&c); xs_ora_sha512_update(&c, key, klen); xs_ora_sha512_final(&c, k); }
    else memcpy(k, key, klen);
    for (int i = 0; i < 128; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    xs_ora_sha512 c;
    xs_ora_sha512_init(&c); xs_ora_sha512_update(&c, ipad, 128); xs_ora_sha512_update(&c, data, len); xs_ora_sha512_final(&c, inner);
    xs_ora_sha512_init(&c); xs_ora_sha512_update(&c, opad, 128); xs_ora_sha512_update(&c, inner, 64); xs_ora_sha512_final(&c, out);
    memset(k, 0, sizeof k); memset(ipad, 0, sizeof ipad); memset(opad, 0, sizeof opad);
}
/* PBKDF2 with HMAC-SHA512; the precomputed inner and outer states make each of the thousands of rounds two blocks */
static void xs_ora_pbkdf2_512(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t slen, uint32_t iters, uint8_t *out, size_t outlen) {
    uint8_t k[128] = {0}, pad[128];
    if (pwlen > 128) { xs_ora_sha512 c; xs_ora_sha512_init(&c); xs_ora_sha512_update(&c, pw, pwlen); xs_ora_sha512_final(&c, k); }
    else memcpy(k, pw, pwlen);
    xs_ora_sha512 in0, out0;
    for (int i = 0; i < 128; i++) pad[i] = k[i] ^ 0x36;
    xs_ora_sha512_init(&in0); xs_ora_sha512_update(&in0, pad, 128);
    for (int i = 0; i < 128; i++) pad[i] = k[i] ^ 0x5c;
    xs_ora_sha512_init(&out0); xs_ora_sha512_update(&out0, pad, 128);
    for (uint32_t block = 1; outlen > 0; block++) {
        uint8_t u[64], t[64], be[4] = {(uint8_t)(block >> 24), (uint8_t)(block >> 16), (uint8_t)(block >> 8), (uint8_t)block};
        xs_ora_sha512 c = in0;
        xs_ora_sha512_update(&c, salt, slen); xs_ora_sha512_update(&c, be, 4); xs_ora_sha512_final(&c, u);
        c = out0; xs_ora_sha512_update(&c, u, 64); xs_ora_sha512_final(&c, u);
        memcpy(t, u, 64);
        for (uint32_t i = 1; i < iters; i++) {
            c = in0; xs_ora_sha512_update(&c, u, 64); xs_ora_sha512_final(&c, u);
            c = out0; xs_ora_sha512_update(&c, u, 64); xs_ora_sha512_final(&c, u);
            for (int j = 0; j < 64; j++) t[j] ^= u[j];
        }
        size_t take = outlen < 64 ? outlen : 64;
        memcpy(out, t, take);
        out += take; outlen -= take;
    }
    memset(k, 0, sizeof k); memset(pad, 0, sizeof pad);
}

/* ---- AES-256 ---- */
static const uint8_t xs_ora_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,
    0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,
    0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,
    0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,
    0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,
    0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
static uint8_t xs_ora_xt(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
static uint8_t xs_ora_mul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    while (b) { if (b & 1) r ^= a; a = xs_ora_xt(a); b >>= 1; }
    return r;
}
typedef struct { uint8_t rk[240]; uint8_t inv[256]; } xs_ora_aes;
static void xs_ora_aes_init(xs_ora_aes *a, const uint8_t key[32]) {
    memcpy(a->rk, key, 32);
    uint8_t rcon = 1;
    for (int i = 8; i < 60; i++) {
        uint8_t t[4];
        memcpy(t, a->rk + (i - 1) * 4, 4);
        if (i % 8 == 0) {
            uint8_t x = t[0];
            t[0] = xs_ora_sbox[t[1]] ^ rcon; t[1] = xs_ora_sbox[t[2]]; t[2] = xs_ora_sbox[t[3]]; t[3] = xs_ora_sbox[x];
            rcon = xs_ora_xt(rcon);
        } else if (i % 8 == 4) {
            for (int k = 0; k < 4; k++) t[k] = xs_ora_sbox[t[k]];
        }
        for (int k = 0; k < 4; k++) a->rk[i * 4 + k] = a->rk[(i - 8) * 4 + k] ^ t[k];
    }
    for (int i = 0; i < 256; i++) a->inv[xs_ora_sbox[i]] = (uint8_t)i;
}
static void xs_ora_aes_enc(const xs_ora_aes *a, uint8_t s[16]) {
    for (int k = 0; k < 16; k++) s[k] ^= a->rk[k];
    for (int r = 1; r <= 14; r++) {
        uint8_t t[16];
        for (int k = 0; k < 16; k++) t[k] = xs_ora_sbox[s[k]];
        for (int c = 0; c < 4; c++) /* shift rows: row i moves left by i */
            for (int i = 0; i < 4; i++) s[c * 4 + i] = t[((c + i) % 4) * 4 + i];
        if (r < 14)
            for (int c = 0; c < 4; c++) {
                uint8_t *m = s + c * 4, a0 = m[0], a1 = m[1], a2 = m[2], a3 = m[3];
                m[0] = xs_ora_xt(a0) ^ (xs_ora_xt(a1) ^ a1) ^ a2 ^ a3;
                m[1] = a0 ^ xs_ora_xt(a1) ^ (xs_ora_xt(a2) ^ a2) ^ a3;
                m[2] = a0 ^ a1 ^ xs_ora_xt(a2) ^ (xs_ora_xt(a3) ^ a3);
                m[3] = (xs_ora_xt(a0) ^ a0) ^ a1 ^ a2 ^ xs_ora_xt(a3);
            }
        for (int k = 0; k < 16; k++) s[k] ^= a->rk[r * 16 + k];
    }
}
static void xs_ora_aes_dec(const xs_ora_aes *a, uint8_t s[16]) {
    for (int r = 14; r >= 1; r--) {
        for (int k = 0; k < 16; k++) s[k] ^= a->rk[r * 16 + k];
        if (r < 14)
            for (int c = 0; c < 4; c++) {
                uint8_t *m = s + c * 4, a0 = m[0], a1 = m[1], a2 = m[2], a3 = m[3];
                m[0] = xs_ora_mul(a0, 14) ^ xs_ora_mul(a1, 11) ^ xs_ora_mul(a2, 13) ^ xs_ora_mul(a3, 9);
                m[1] = xs_ora_mul(a0, 9) ^ xs_ora_mul(a1, 14) ^ xs_ora_mul(a2, 11) ^ xs_ora_mul(a3, 13);
                m[2] = xs_ora_mul(a0, 13) ^ xs_ora_mul(a1, 9) ^ xs_ora_mul(a2, 14) ^ xs_ora_mul(a3, 11);
                m[3] = xs_ora_mul(a0, 11) ^ xs_ora_mul(a1, 13) ^ xs_ora_mul(a2, 9) ^ xs_ora_mul(a3, 14);
            }
        uint8_t t[16];
        for (int c = 0; c < 4; c++) /* inverse shift rows: row i moves right by i */
            for (int i = 0; i < 4; i++) t[((c + i) % 4) * 4 + i] = s[c * 4 + i];
        for (int k = 0; k < 16; k++) s[k] = a->inv[t[k]];
    }
    for (int k = 0; k < 16; k++) s[k] ^= a->rk[k];
}
/* CBC with a zero IV over whole blocks (n a multiple of 16), in place */
static void xs_ora_cbc(const uint8_t key[32], uint8_t *data, size_t n, bool encrypt) {
    xs_ora_aes a;
    xs_ora_aes_init(&a, key);
    uint8_t prev[16] = {0}, cur[16];
    for (size_t off = 0; off + 16 <= n; off += 16) {
        uint8_t *b = data + off;
        if (encrypt) {
            for (int k = 0; k < 16; k++) b[k] ^= prev[k];
            xs_ora_aes_enc(&a, b);
            memcpy(prev, b, 16);
        } else {
            memcpy(cur, b, 16);
            xs_ora_aes_dec(&a, b);
            for (int k = 0; k < 16; k++) b[k] ^= prev[k];
            memcpy(prev, cur, 16);
        }
    }
    memset(&a, 0, sizeof a);
}
