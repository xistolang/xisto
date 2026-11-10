/* jwks-rsa: an RSA JWK (n, e in base64url) as a PEM public key (SubjectPublicKeyInfo), and a token's header
   fields (kid, alg) without checking it. */

static void xn_jwk_der_len(xs_buf *b, size_t n) {
    uint8_t t[4];
    if (n < 0x80) { t[0] = (uint8_t)n; xs_buf_append(b, t, 1); }
    else if (n < 0x100) { t[0] = 0x81; t[1] = (uint8_t)n; xs_buf_append(b, t, 2); }
    else { t[0] = 0x82; t[1] = (uint8_t)(n >> 8); t[2] = (uint8_t)n; xs_buf_append(b, t, 3); }
}
static void xn_jwk_der(xs_buf *b, uint8_t tag, const uint8_t *body, size_t n) {
    xs_buf_append(b, &tag, 1);
    xn_jwk_der_len(b, n);
    xs_buf_append(b, body, n);
}
/* an unsigned big-endian INTEGER: a leading 0 when the top bit is set */
static void xn_jwk_int(xs_buf *b, const uint8_t *v, size_t n) {
    while (n > 1 && v[0] == 0) { v++; n--; }
    uint8_t tag = 0x02;
    bool pad = v[0] & 0x80;
    xs_buf_append(b, &tag, 1);
    xn_jwk_der_len(b, n + pad);
    if (pad) { uint8_t z = 0; xs_buf_append(b, &z, 1); }
    xs_buf_append(b, v, n);
}

XS_NOINLINE xs_str *xn_jwks_rsa_jwkToPem(xs_str *n64, xs_str *e64) {
    uint8_t *n = malloc((size_t)n64->len + 4), *e = malloc((size_t)e64->len + 4);
    long nl = xs_b64_decode(n64->data, (size_t)n64->len, n), el = xs_b64_decode(e64->data, (size_t)e64->len, e);
    if (nl <= 0 || el <= 0) { free(n); free(e); return xs_str_new("", 0); }
    xs_buf ints = {0}, rsa = {0}, bits = {0}, spki = {0}, alg = {0};
    xn_jwk_int(&ints, n, (size_t)nl);
    xn_jwk_int(&ints, e, (size_t)el);
    xn_jwk_der(&rsa, 0x30, (uint8_t *)ints.data, ints.len);
    uint8_t z = 0;
    xs_buf_append(&bits, &z, 1);
    xs_buf_append(&bits, rsa.data, rsa.len);
    static const uint8_t algid[] = {0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00};
    xs_buf_append(&alg, algid, sizeof algid);
    xn_jwk_der(&alg, 0x03, (uint8_t *)bits.data, bits.len);
    xn_jwk_der(&spki, 0x30, (uint8_t *)alg.data, alg.len);
    size_t b64n = (spki.len + 2) / 3 * 4;
    char *b64 = malloc(b64n + 1);
    xs_b64_encode((uint8_t *)spki.data, spki.len, b64, false);
    xs_buf pem = {0};
    const char *head = "-----BEGIN PUBLIC KEY-----\n", *tail = "-----END PUBLIC KEY-----\n";
    xs_buf_append(&pem, head, strlen(head));
    for (size_t i = 0; i < b64n; i += 64) {
        xs_buf_append(&pem, b64 + i, b64n - i < 64 ? b64n - i : 64);
        xs_buf_append(&pem, "\n", 1);
    }
    xs_buf_append(&pem, tail, strlen(tail));
    xs_str *out = xs_str_new(pem.data, pem.len);
    free(n); free(e); free(b64);
    free(ints.data); free(rsa.data); free(bits.data); free(spki.data); free(alg.data); free(pem.data);
    return out;
}

/* a string field of the token's header ("" when the token is not one, or the field is absent) */
XS_NOINLINE xs_str *xn_jwks_rsa_headerField(xs_str *token, xs_str *name) {
    const char *t = token->data;
    const char *dot = memchr(t, '.', (size_t)token->len);
    if (!dot) return xs_str_new("", 0);
    size_t hl = (size_t)(dot - t);
    uint8_t *h = malloc(hl + 4);
    long n = xs_b64_decode(t, hl, h);
    if (n <= 0) { free(h); return xs_str_new("", 0); }
    h[n] = 0;
    char key[64];
    snprintf(key, sizeof key, "\"%s\":\"", name->data);
    const char *at = strstr((char *)h, key);
    xs_str *out;
    if (!at) out = xs_str_new("", 0);
    else {
        at += strlen(key);
        const char *end = strchr(at, '"');
        out = xs_str_new(at, end ? (size_t)(end - at) : 0);
    }
    free(h);
    return out;
}
