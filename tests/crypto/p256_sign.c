/* The patched P-256 ECDSA signature must be byte-identical to BearSSL's original (signatures are
   deterministic: k comes from HMAC_DRBG over the key and the hash), and verify with the m31 code. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bearssl.h"
size_t orig_ecdsa_i31_sign_raw(const br_ec_impl *impl, const br_hash_class *hf, const void *hash_value,
                               const br_ec_private_key *sk, void *sig);
int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 20000;
    br_hmac_drbg_context rng;
    br_hmac_drbg_init(&rng, &br_sha256_vtable, "p256-sign-test", 14);
    unsigned char kbuf[BR_EC_KBUF_PRIV_MAX_SIZE], pbuf[BR_EC_KBUF_PUB_MAX_SIZE], h[64], a[132], b[132];
    br_ec_private_key sk; br_ec_public_key pk;
    long bad = 0;
    for (long t = 0; t < n; t++) {
        if (t % 50 == 0) {
            br_ec_keygen(&rng.vtable, &br_ec_p256_m64, &sk, kbuf, BR_EC_secp256r1);
            br_ec_compute_pub(&br_ec_p256_m64, &pk, pbuf, &sk);
        }
        br_hmac_drbg_generate(&rng, h, sizeof h);
        if (t % 7 == 0) memset(h, 0xFF, 32);           /* hash above n: exercises the reduction of m */
        if (t % 11 == 0) memset(h, 0, 32);             /* m = 0 */
        const br_hash_class *hf = (t & 1) ? &br_sha256_vtable : &br_sha384_vtable; /* 32- and 48-byte hashes */
        size_t la = br_ecdsa_i31_sign_raw(&br_ec_p256_m64, hf, h, &sk, a);
        size_t lb = orig_ecdsa_i31_sign_raw(&br_ec_p256_m64, hf, h, &sk, b);
        size_t hl = (t & 1) ? 32 : 48;
        if (la != lb || memcmp(a, b, la) != 0) { if (bad++ < 3) printf("MISMATCH at %ld\n", t); continue; }
        /* the verifier refuses an all-zero hash by contract (u1 = 0); identity with the original covers it */
        if (t % 11 != 0 && !br_ecdsa_i31_vrfy_raw(&br_ec_p256_m31, h, hl, &pk, a, la)) { if (bad++ < 3) printf("verify failed at %ld\n", t); }
    }
    printf("p256 sign: %s (%ld signatures identical to the original and verified)\n", bad ? "FAILED" : "ok", n);
    return bad != 0;
}
