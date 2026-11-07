/* Known-answer tests for runtime/xisto_crypto.h (node:crypto) and packages/argon2: FIPS 180-4, RFC 4231, RFC 7914/8018, RFC 7693, RFC 9106. */
#define XS_THREADS 1
#include "../../runtime/xisto_rt.h"
#include "../../runtime/xisto_net.h"
#include "../../runtime/xisto_crypto.h"
#include "../../packages/argon2/native/argon2.c"
static int fails;
static void hex(const uint8_t *p, size_t n, char *out) { for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", p[i]); }
static void check(const char *name, const uint8_t *got, size_t n, const char *want) {
    char h[2100];
    hex(got, n, h);
    if (strcmp(h, want) != 0) { printf("FAIL %s\n  got  %s\n  want %s\n", name, h, want); fails++; }
    else printf("ok   %s\n", name);
}
int main(void) {
    uint8_t out[128];
    xs_sha256_hash("abc", 3, out);
    check("sha256 abc", out, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    xs_sha256_hash("", 0, out);
    check("sha256 empty", out, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const char *m448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    xs_sha256_hash(m448, strlen(m448), out);
    check("sha256 448 bits", out, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    uint8_t key[131];
    memset(key, 0x0b, 20);
    xs_hmac256(key, 20, "Hi There", 8, out);
    check("hmac-sha256 rfc4231 1", out, 32, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    memset(key, 0xaa, 131);
    const char *big = "Test Using Larger Than Block-Size Key - Hash Key First";
    xs_hmac256(key, 131, big, strlen(big), out);
    check("hmac-sha256 rfc4231 6", out, 32, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    xs_pbkdf2_256("password", 8, (const uint8_t *)"salt", 4, 1, out);
    check("pbkdf2-sha256 c=1", out, 32, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    xs_pbkdf2_256("password", 8, (const uint8_t *)"salt", 4, 4096, out);
    check("pbkdf2-sha256 c=4096", out, 32, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    xs_blake2b b;
    xs_b2_init(&b, 64);
    xs_b2_update(&b, "abc", 3);
    xs_b2_final(&b, out);
    check("blake2b-512 abc", out, 64, "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923");
    /* RFC 9106 5.3: Argon2id, t=3, m=32 KiB, p=4, with a secret and associated data */
    uint8_t pw[32], salt[16], secret[8], ad[12];
    memset(pw, 1, 32); memset(salt, 2, 16); memset(secret, 3, 8); memset(ad, 4, 12);
    xs_argon2id_ex(pw, 32, salt, 16, secret, 8, ad, 12, 3, 32, 4, out, 32);
    check("argon2id rfc9106", out, 32, "0d640df58d78766c08c037a34a8b53c9d01ef0452d75b65eb52520e96b01e659");
    /* the PHC string round trip at node-argon2's defaults (m=64 MiB, t=3, p=4) */
    char enc[160];
    double t0 = (double)clock();
    xs_argon2_encode((const uint8_t *)"s3cret", 6, 3, 65536, 4, enc, sizeof enc);
    printf("     %s (%.0f ms cpu)\n", enc, ((double)clock() - t0) * 1000 / CLOCKS_PER_SEC);
    if (!xs_argon2_verify(enc, (const uint8_t *)"s3cret", 6) || xs_argon2_verify(enc, (const uint8_t *)"s3cres", 6)) { printf("FAIL argon2 verify\n"); fails++; }
    else printf("ok   argon2 encode/verify\n");
    char b64[64];
    xs_b64_encode((const uint8_t *)"any carnal pleas", 16, b64, false);
    if (strcmp(b64, "YW55IGNhcm5hbCBwbGVhcw==") != 0) { printf("FAIL base64 %s\n", b64); fails++; } else printf("ok   base64\n");
    long n = xs_b64_decode("YW55IGNhcm5hbCBwbGVhcw", 22, out);
    if (n != 16 || memcmp(out, "any carnal pleas", 16) != 0) { printf("FAIL base64 decode\n"); fails++; } else printf("ok   base64 decode\n");
    printf(fails ? "crypto vectors: %d FAILED\n" : "crypto vectors: all ok\n", fails);
    return fails != 0;
}
