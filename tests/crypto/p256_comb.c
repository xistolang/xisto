/* Differential test of the patched P-256 generator multiplication (fixed-base comb) against two
   independent paths: the generic m64 point multiplication applied to G, and the m31 implementation.
   Also ECDSA sign (patched) -> verify (m31), both directions. Exit code 0 when everything matches. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bearssl.h"

static const unsigned char N[32] = { /* P-256 order */
    0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xBC,0xE6,0xFA,0xAD,0xA7,0x17,0x9E,0x84,0xF3,0xB9,0xCA,0xC2,0xFC,0x63,0x25,0x51 };

static int check(const unsigned char *k, size_t klen, const char *what) {
    unsigned char a[65], b[65], c[65];
    size_t glen;
    const unsigned char *G = br_ec_p256_m64.generator(BR_EC_secp256r1, &glen);
    br_ec_p256_m64.mulgen(a, k, klen, BR_EC_secp256r1);
    memcpy(b, G, 65);
    br_ec_p256_m64.mul(b, 65, k, klen, BR_EC_secp256r1);
    br_ec_p256_m31.mulgen(c, k, klen, BR_EC_secp256r1);
    if (memcmp(a, b, 65) || memcmp(a, c, 65)) { printf("MISMATCH %s klen=%zu\n", what, klen); return 1; }
    return 0;
}

int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 100000;
    int bad = 0;
    unsigned char k[32];
    /* edges: 1, 2, 15, 16, 255, n-1, n-2, nibble patterns */
    for (int v = 1; v <= 300; v++) { memset(k, 0, 32); k[31] = (unsigned char)v; k[30] = (unsigned char)(v >> 8); bad += check(k, 32, "small"); }
    memcpy(k, N, 32); k[31] -= 1; bad += check(k, 32, "n-1");
    memcpy(k, N, 32); k[31] -= 2; bad += check(k, 32, "n-2");
    memset(k, 0xFF, 32); k[0] = 0x0F; bad += check(k, 32, "0x0FFF..F");
    memset(k, 0x11, 32); bad += check(k, 32, "0x1111..1");
    for (int i = 0; i < 32; i++) { memset(k, 0, 32); k[i] = 0xF0; bad += check(k, 32, "single high nibble"); k[i] = 0x0F; bad += check(k, 32, "single low nibble"); }
    k[0] = 0x05; bad += check(k, 1, "klen 1");
    /* random scalars below n (and short ones) */
    br_hmac_drbg_context rng;
    br_hmac_drbg_init(&rng, &br_sha256_vtable, "p256-comb-test", 14);
    for (long t = 0; t < n; t++) {
        br_hmac_drbg_generate(&rng, k, 32);
        if (memcmp(k, N, 32) >= 0) k[0] &= 0x7F;
        size_t klen = (t % 17 == 0) ? 1 + (size_t)(t % 32) : 32;
        if (klen < 32 && k[32 - klen] == 0) k[32 - klen] = 1;
        bad += check(k + 32 - klen, klen, "random");
        if (bad > 5) break;
    }
    /* ECDSA: sign with the patched implementation, verify with m31; and the other way */
    unsigned char kbuf[BR_EC_KBUF_PRIV_MAX_SIZE], pbuf[BR_EC_KBUF_PUB_MAX_SIZE], h[32], sig[80];
    br_ec_private_key sk; br_ec_public_key pk;
    for (int t = 0; t < 2000; t++) {
        br_ec_keygen(&rng.vtable, &br_ec_p256_m64, &sk, kbuf, BR_EC_secp256r1);
        br_ec_compute_pub(&br_ec_p256_m64, &pk, pbuf, &sk);
        br_hmac_drbg_generate(&rng, h, 32);
        size_t sl = br_ecdsa_i31_sign_raw(&br_ec_p256_m64, &br_sha256_vtable, h, &sk, sig);
        if (!br_ecdsa_i31_vrfy_raw(&br_ec_p256_m31, h, 32, &pk, sig, sl)) { printf("ECDSA m64-sign/m31-verify failed\n"); bad++; break; }
        sl = br_ecdsa_i31_sign_raw(&br_ec_p256_m31, &br_sha256_vtable, h, &sk, sig);
        if (!br_ecdsa_i31_vrfy_raw(&br_ec_p256_m64, h, 32, &pk, sig, sl)) { printf("ECDSA m31-sign/m64-verify failed\n"); bad++; break; }
    }
    printf("p256 comb: %s (%ld random scalars + edges, 2000 ECDSA round trips)\n", bad ? "FAILED" : "ok", n);
    return bad != 0;
}
