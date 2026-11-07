/* The AArch64 assembly f256 Montgomery multiplication must agree, modulo p, with BearSSL's C version
   (built here from ec_p256_m64.c with the assembly disabled), on random and edge inputs, including
   aliasing of the output with an input. Also prints the speed of both. */
#define XS_P256_NO_ASM 1
#include "ec/ec_p256_m64.c"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#if defined(__aarch64__)
void xs_f256_montymul_arm64(uint64_t *d, const uint64_t *a, const uint64_t *b);
#else
/* other processors have no assembly: the C path they run is checked against a slow, independent reference,
   a*b*2^-256 mod p from the schoolbook product, a bitwise reduction and 256 halvings mod p */
static const uint64_t XP[4] = {0xFFFFFFFFFFFFFFFFull, 0x00000000FFFFFFFFull, 0, 0xFFFFFFFF00000001ull};
static int ge5(const uint64_t *r, const uint64_t *m) { /* r (5 limbs) >= m (4 limbs) */
    if (r[4]) return 1;
    for (int i = 3; i >= 0; i--) if (r[i] != m[i]) return r[i] > m[i];
    return 1;
}
static void sub5(uint64_t *r, const uint64_t *m) {
    unsigned __int128 br = 0;
    for (int i = 0; i < 5; i++) {
        unsigned __int128 x = (unsigned __int128)r[i] - (i < 4 ? m[i] : 0) - br;
        r[i] = (uint64_t)x;
        br = (x >> 64) ? 1 : 0;
    }
}
static void xs_f256_montymul_arm64(uint64_t *d, const uint64_t *a, const uint64_t *b) {
    uint64_t t[8] = {0};
    for (int i = 0; i < 4; i++) {
        unsigned __int128 c = 0;
        for (int j = 0; j < 4; j++) {
            c += (unsigned __int128)a[i] * b[j] + t[i + j];
            t[i + j] = (uint64_t)c;
            c >>= 64;
        }
        t[i + 4] = (uint64_t)c;
    }
    uint64_t r[5] = {0};
    for (int bit = 511; bit >= 0; bit--) { /* r = 2r + bit, kept below p */
        for (int k = 4; k > 0; k--) r[k] = r[k] << 1 | r[k - 1] >> 63;
        r[0] = r[0] << 1 | (t[bit / 64] >> (bit % 64) & 1);
        if (ge5(r, XP)) sub5(r, XP);
    }
    for (int k = 0; k < 256; k++) { /* r / 2 mod p */
        if (r[0] & 1) {
            unsigned __int128 c = 0;
            for (int i = 0; i < 5; i++) { c += (unsigned __int128)r[i] + (i < 4 ? XP[i] : 0); r[i] = (uint64_t)c; c >>= 64; }
        }
        for (int i = 0; i < 4; i++) r[i] = r[i] >> 1 | r[i + 1] << 63;
        r[4] >>= 1;
    }
    memcpy(d, r, 32);
}
#endif
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static int same_mod_p(const uint64_t *x, const uint64_t *y) {
    uint64_t a[4], b[4];
    memcpy(a, x, 32); memcpy(b, y, 32);
    f256_final_reduce(a); f256_final_reduce(b);
    return memcmp(a, b, 32) == 0;
}
int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 5000000;
    static const uint64_t edges[][4] = {
        { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { ~0ull, ~0ull, ~0ull, ~0ull },
        { 0xFFFFFFFFFFFFFFFF, 0x00000000FFFFFFFF, 0, 0xFFFFFFFF00000001 },          /* p */
        { 0xFFFFFFFFFFFFFFFE, 0x00000000FFFFFFFF, 0, 0xFFFFFFFF00000001 },          /* p-1 */
        { 0, 0x00000000FFFFFFFF, 0, 0xFFFFFFFF00000001 }, { ~0ull, 0, 0, 0 }, { 0, 0, 0, 1ull << 63 },
    };
    long bad = 0;
    int ne = sizeof edges / sizeof edges[0];
    for (int i = 0; i < ne; i++) for (int j = 0; j < ne; j++) {
        uint64_t c[4], s[4];
        f256_montymul(c, edges[i], edges[j]); xs_f256_montymul_arm64(s, edges[i], edges[j]);
        if (!same_mod_p(c, s)) { if (bad++ < 5) printf("edge mismatch %d %d\n", i, j); }
    }
    for (long t = 0; t < n; t++) {
        uint64_t a[4], b[4], c[4], s[4], s2[4];
        for (int k = 0; k < 4; k++) { a[k] = rnd(); b[k] = rnd(); }
        if (t % 5 == 0) a[3] |= 0xFFFFFFFF00000000ull;     /* push towards 2^256 */
        if (t % 7 == 0) memcpy(b, a, 32);                 /* squaring */
        f256_montymul(c, a, b); xs_f256_montymul_arm64(s, a, b);
        memcpy(s2, a, 32); xs_f256_montymul_arm64(s2, s2, b); /* d aliases a */
        if (!same_mod_p(c, s) || memcmp(s, s2, 32)) { if (bad++ < 5) printf("random mismatch at %ld\n", t); }
    }
    uint64_t a[4] = { 0x0123456789ABCDEF, 0x1111222233334444, 0x5555666677778888, 0x0FFFFFFF00000000 }, b[4] = { 3, 5, 7, 9 };
    double t0 = now(); for (int i = 0; i < 20000000; i++) f256_montymul(a, a, b); double tc = now() - t0;
    t0 = now(); for (int i = 0; i < 20000000; i++) xs_f256_montymul_arm64(a, a, b); double ta = now() - t0;
#if defined(__aarch64__)
    printf("p256 montymul asm: %s (%ld random + edges); C %.2f ns, asm %.2f ns\n", bad ? "FAILED" : "ok", n, tc * 1e9 / 2e7, ta * 1e9 / 2e7);
#else
    printf("p256 montymul C against the reference: %s (%ld random + edges)\n", bad ? "FAILED" : "ok", n);
#endif
    return bad != 0;
}
