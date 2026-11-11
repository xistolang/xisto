/* the login ciphers and hashes of packages/oracledb against published known answers: DES (FIPS 81 worked example),
   AES-128/256 (FIPS 197 appendix C), MD5 (RFC 1321) and Oracle's 10g verifier (SCOTT/TIGER, SYSTEM/MANAGER) */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../../packages/oracledb/native/ora_crypto.c"
static int bad;
static void check(const char *what, const uint8_t *p, int n, const char *want) {
    char got[65];
    for (int i = 0; i < n; i++) snprintf(got + 2 * i, 3, "%02X", p[i]);
    if (strcmp(got, want) != 0) { printf("FAIL %s: %s, want %s\n", what, got, want); bad = 1; }
}
int main(void) {
    uint8_t k[8] = {0x13, 0x34, 0x57, 0x79, 0x9B, 0xBC, 0xDF, 0xF1}, m[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF}, o[8];
    xs_ora_des_enc(k, m, o);
    check("des", o, 8, "85E813540F0AB405");
    uint8_t key[32], b[16];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
    for (int i = 0; i < 16; i++) b[i] = (uint8_t)(i * 0x11);
    xs_ora_cbc(key, 16, b, 16, true);
    check("aes-128", b, 16, "69C4E0D86A7B0430D8CDB78070B4C55A");
    xs_ora_cbc(key, 16, b, 16, false);
    check("aes-128 decrypt", b, 16, "00112233445566778899AABBCCDDEEFF");
    xs_ora_cbc(key, 24, b, 16, true);
    check("aes-192", b, 16, "DDA97CA4864CDFE06EAF70A0EC0D7191");
    xs_ora_cbc(key, 24, b, 16, false);
    xs_ora_cbc(key, 32, b, 16, true);
    check("aes-256", b, 16, "8EA2B7CA516745BFEAFC49904B496089");
    uint8_t md[16];
    xs_ora_md5((const uint8_t *)"abc", 3, md);
    check("md5 abc", md, 16, "900150983CD24FB0D6963F7D28E17F72");
    const char *l = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
    xs_ora_md5((const uint8_t *)l, 80, md);
    check("md5 80 bytes", md, 16, "57EDF4A22BE3C955AC49DA2E2107B67A");
    uint8_t h[8];
    xs_ora_10g_hash("scott", "tiger", h);
    check("10g scott/tiger", h, 8, "F894844C34402B67");
    xs_ora_10g_hash("SYSTEM", "MANAGER", h);
    check("10g system/manager", h, 8, "D4DF7931AB130E37");
    return bad;
}
