/* jsonwebtoken (HS256) on xisto:crypto: HMAC-SHA256, base64url, constant-time comparison. */
#include <time.h>

static xs_str *xn_jwt_b64url(const char *data, size_t n) {
    xs_str *s = xs_str_alloc((int64_t)((n + 2) / 3 * 4 + 1));
    s->len = (int64_t)xs_b64_encode((const uint8_t *)data, n, s->data, true);
    return s;
}
XS_NOINLINE void xn_jwt_fail(const char *name, const char *msg) {
    xs_Error *e = xs_Error_from(msg);
    xs_release(e->f_name);
    e->f_name = xs_str_new(name, strlen(name));
    xs_throw(e);
}
XS_NOINLINE xs_str *xn_jsonwebtoken_sign(xs_str *payload, xs_str *secret) {
    /* jsonwebtoken adds "iat" (issued at, seconds) to an object payload unless it is there */
    const char *p = payload->data;
    size_t n = (size_t)payload->len;
    char *body = NULL;
    size_t blen = n;
    if (n >= 2 && p[0] == '{' && p[n - 1] == '}' && !strstr(p, "\"iat\":")) {
        char iat[48];
        int k = snprintf(iat, sizeof iat, "%s\"iat\":%lld}", n > 2 ? "," : "", (long long)time(NULL));
        body = malloc(n - 1 + (size_t)k);
        memcpy(body, p, n - 1);
        memcpy(body + n - 1, iat, (size_t)k);
        blen = n - 1 + (size_t)k;
    }
    static const char header[] = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    xs_str *h64 = xn_jwt_b64url(header, sizeof header - 1);
    xs_str *p64 = xn_jwt_b64url(body ? body : p, blen);
    free(body);
    size_t sl = (size_t)h64->len + 1 + (size_t)p64->len;
    char *signing = malloc(sl);
    memcpy(signing, h64->data, (size_t)h64->len);
    signing[h64->len] = '.';
    memcpy(signing + h64->len + 1, p64->data, (size_t)p64->len);
    uint8_t mac[32];
    xs_hmac256(secret->data, (size_t)secret->len, signing, sl, mac);
    char sig[48];
    size_t siglen = xs_b64_encode(mac, 32, sig, true);
    xs_str *out = xs_str_alloc((int64_t)(sl + 1 + siglen));
    memcpy(out->data, signing, sl);
    out->data[sl] = '.';
    memcpy(out->data + sl + 1, sig, siglen + 1);
    free(signing);
    xs_release(h64);
    xs_release(p64);
    return out;
}
XS_NOINLINE xs_str *xn_jsonwebtoken_verify(xs_str *token, xs_str *secret) {
    if (token->len == 0) xn_jwt_fail("JsonWebTokenError", "jwt must be provided");
    const char *t = token->data;
    const char *d1 = memchr(t, '.', (size_t)token->len);
    const char *d2 = d1 ? memchr(d1 + 1, '.', (size_t)(t + token->len - d1 - 1)) : NULL;
    if (!d1 || !d2 || memchr(d2 + 1, '.', (size_t)(t + token->len - d2 - 1))) xn_jwt_fail("JsonWebTokenError", "jwt malformed");
    uint8_t buf[4096];
    long hl = (size_t)(d1 - t) * 3 / 4 + 3 < sizeof buf ? xs_b64_decode(t, (size_t)(d1 - t), buf) : -1;
    if (hl <= 0) xn_jwt_fail("JsonWebTokenError", "invalid token");
    buf[hl] = 0;
    if (!strstr((char *)buf, "\"alg\":\"HS256\"")) xn_jwt_fail("JsonWebTokenError", "invalid algorithm");
    uint8_t mac[32], got[48];
    xs_hmac256(secret->data, (size_t)secret->len, t, (size_t)(d2 - t), mac);
    long gl = (size_t)(t + token->len - d2 - 1) <= 64 ? xs_b64_decode(d2 + 1, (size_t)(t + token->len - d2 - 1), got) : -1;
    if (gl != 32 || !xs_ct_equal(mac, got, 32)) xn_jwt_fail("JsonWebTokenError", "invalid signature");
    size_t pl64 = (size_t)(d2 - d1 - 1);
    if (pl64 * 3 / 4 + 3 >= sizeof buf) xn_jwt_fail("JsonWebTokenError", "invalid token");
    long pl = xs_b64_decode(d1 + 1, pl64, buf);
    if (pl <= 0) xn_jwt_fail("JsonWebTokenError", "invalid token");
    xs_str *payload = xs_str_new((char *)buf, (size_t)pl);
    /* "exp": seconds since the epoch; expired when now >= exp, as in jsonwebtoken */
    const char *exp = strstr(payload->data, "\"exp\":");
    if (exp && (double)time(NULL) >= strtod(exp + 6, NULL)) { xs_release(payload); xn_jwt_fail("TokenExpiredError", "jwt expired"); }
    return payload;
}
