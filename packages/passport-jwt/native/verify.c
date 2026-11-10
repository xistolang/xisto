/* passport-jwt's token check, as jsonwebtoken.verify does it: HS256 (HMAC-SHA256 with a secret) and RS256
   (RSASSA-PKCS1-v1_5 with SHA-256, the public key as PEM: SubjectPublicKeyInfo or PKCS#1), then "exp", "nbf",
   "iss" and "aud". The RSA check is the public operation only (s^e mod n) on 32-bit limbs; no secret is involved. */
#include <time.h>

XS_NOINLINE void xn_pj_fail(const char *name, const char *msg) {
    xs_Error *e = xs_Error_from(msg);
    xs_release(e->f_name);
    e->f_name = xs_str_new(name, strlen(name));
    xs_throw(e);
}

/* ---- big numbers: little-endian 32-bit limbs, at most 4096-bit moduli ---- */
#define XN_PJ_W 130
typedef struct { uint32_t w[XN_PJ_W]; int n; } xn_pj_big;

static void xn_pj_from_bytes(xn_pj_big *b, const uint8_t *p, size_t len, int n) {
    memset(b, 0, sizeof *b);
    b->n = n;
    for (size_t i = 0; i < len; i++) {
        size_t bit = (len - 1 - i) * 8;
        if (bit / 32 < XN_PJ_W) b->w[bit / 32] |= (uint32_t)p[i] << (bit % 32);
    }
}
static int xn_pj_cmp(const uint32_t *a, const uint32_t *b, int n) {
    for (int i = n - 1; i >= 0; i--)
        if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
    return 0;
}
static void xn_pj_sub(uint32_t *a, const uint32_t *b, int n) {
    int64_t borrow = 0;
    for (int i = 0; i < n; i++) {
        int64_t d = (int64_t)a[i] - b[i] - borrow;
        borrow = d < 0;
        a[i] = (uint32_t)(d + (borrow ? ((int64_t)1 << 32) : 0));
    }
}
/* r = a * b mod m (n limbs each): the product, then reduced one bit at a time */
static void xn_pj_mulmod(uint32_t *r, const uint32_t *a, const uint32_t *b, const uint32_t *m, int n) {
    uint32_t prod[2 * XN_PJ_W] = {0};
    for (int i = 0; i < n; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < n; j++) {
            uint64_t t = (uint64_t)a[i] * b[j] + prod[i + j] + carry;
            prod[i + j] = (uint32_t)t;
            carry = t >> 32;
        }
        prod[i + n] = (uint32_t)carry;
    }
    uint32_t acc[XN_PJ_W + 1] = {0};
    for (int bit = 2 * n * 32 - 1; bit >= 0; bit--) {
        uint32_t top = acc[n] << 1 | acc[n - 1] >> 31;
        for (int i = n - 1; i > 0; i--) acc[i] = acc[i] << 1 | acc[i - 1] >> 31;
        acc[0] = acc[0] << 1 | ((prod[bit / 32] >> (bit % 32)) & 1);
        acc[n] = top;
        if (acc[n] || xn_pj_cmp(acc, m, n) >= 0) {
            int64_t borrow = 0;
            for (int i = 0; i < n; i++) {
                int64_t d = (int64_t)acc[i] - m[i] - borrow;
                borrow = d < 0;
                acc[i] = (uint32_t)(d + (borrow ? ((int64_t)1 << 32) : 0));
            }
            acc[n] -= (uint32_t)borrow;
        }
    }
    memcpy(r, acc, sizeof(uint32_t) * (size_t)n);
}

/* ---- DER: the modulus and exponent of a public key ---- */
static bool xn_pj_der(const uint8_t **p, const uint8_t *end, uint8_t tag, const uint8_t **body, size_t *len) {
    const uint8_t *q = *p;
    if (q + 2 > end || *q++ != tag) return false;
    size_t l = *q++;
    if (l & 0x80) {
        int k = (int)(l & 0x7f);
        if (k < 1 || k > 3 || q + k > end) return false;
        l = 0;
        while (k--) l = l << 8 | *q++;
    }
    if (q + l > end) return false;
    *body = q;
    *len = l;
    *p = q + l;
    return true;
}
/* RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER } */
static bool xn_pj_rsa_seq(const uint8_t *p, const uint8_t *end, const uint8_t **n, size_t *nl, const uint8_t **e, size_t *el) {
    const uint8_t *seq;
    size_t sl;
    if (!xn_pj_der(&p, end, 0x30, &seq, &sl)) return false;
    const uint8_t *q = seq, *qe = seq + sl;
    if (!xn_pj_der(&q, qe, 0x02, n, nl) || !xn_pj_der(&q, qe, 0x02, e, el)) return false;
    while (*nl > 0 && **n == 0) { (*n)++; (*nl)--; }
    while (*el > 0 && **e == 0) { (*e)++; (*el)--; }
    return *nl > 0 && *el > 0 && *el <= 4;
}
static bool xn_pj_public_key(const char *pem, size_t len, xn_pj_big *mod, uint32_t *exp, size_t *klen) {
    const char *b = strstr(pem, "-----BEGIN ");
    if (!b) return false;
    bool pkcs1 = strncmp(b, "-----BEGIN RSA PUBLIC KEY-----", 30) == 0;
    b = strchr(b + 11, '\n');
    const char *e = b ? strstr(b, "-----END ") : NULL;
    if (!b || !e) return false;
    char *clean = malloc((size_t)(e - b) + 1);
    size_t c = 0;
    for (const char *s = b; s < e; s++)
        if (*s != '\n' && *s != '\r' && *s != ' ' && *s != '\t') clean[c++] = *s;
    uint8_t *der = malloc(c);
    long dl = xs_b64_decode(clean, c, der);
    free(clean);
    bool ok = false;
    const uint8_t *n = NULL, *ex = NULL;
    size_t nl = 0, el = 0;
    if (dl > 0) {
        const uint8_t *p = der, *end = der + dl;
        if (pkcs1) ok = xn_pj_rsa_seq(p, end, &n, &nl, &ex, &el);
        else { /* SubjectPublicKeyInfo ::= SEQUENCE { algorithm SEQUENCE, subjectPublicKey BIT STRING } */
            const uint8_t *spki, *alg, *bits;
            size_t sl, al, bl;
            if (xn_pj_der(&p, end, 0x30, &spki, &sl)) {
                const uint8_t *q = spki, *qe = spki + sl;
                if (xn_pj_der(&q, qe, 0x30, &alg, &al) && xn_pj_der(&q, qe, 0x03, &bits, &bl) && bl > 1 && bits[0] == 0)
                    ok = xn_pj_rsa_seq(bits + 1, bits + bl, &n, &nl, &ex, &el);
            }
        }
    }
    (void)len;
    if (ok && nl <= (XN_PJ_W - 2) * 4) {
        int words = (int)((nl + 3) / 4);
        xn_pj_from_bytes(mod, n, nl, words);
        *exp = 0;
        for (size_t i = 0; i < el; i++) *exp = *exp << 8 | ex[i];
        *klen = nl;
    } else ok = false;
    free(der);
    return ok;
}

/* RSASSA-PKCS1-v1_5 with SHA-256: sig^e mod n is 00 01 FF..FF 00 DigestInfo(SHA-256, hash) */
static bool xn_pj_rs256(const char *pem, size_t pemlen, const char *data, size_t dlen, const uint8_t *sig, size_t slen) {
    xn_pj_big n, s, r, x;
    uint32_t e;
    size_t k;
    if (!xn_pj_public_key(pem, pemlen, &n, &e, &k)) xn_pj_fail("JsonWebTokenError", "secretOrPublicKey must be an asymmetric key when using RS256");
    if (slen != k || e < 3) return false;
    xn_pj_from_bytes(&s, sig, slen, n.n);
    if (xn_pj_cmp(s.w, n.w, n.n) >= 0) return false;
    /* s^e by square and multiply, from the top bit of e */
    int top = 31;
    while (top > 0 && !((e >> top) & 1)) top--;
    r = s;
    for (int i = top - 1; i >= 0; i--) {
        xn_pj_mulmod(x.w, r.w, r.w, n.w, n.n);
        r = x;
        if ((e >> i) & 1) {
            xn_pj_mulmod(x.w, r.w, s.w, n.w, n.n);
            r = x;
        }
    }
    uint8_t em[(XN_PJ_W) * 4];
    for (size_t i = 0; i < k; i++) {
        size_t bit = (k - 1 - i) * 8;
        em[i] = (uint8_t)(r.w[bit / 32] >> (bit % 32));
    }
    static const uint8_t info[] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
    uint8_t hash[32];
    xs_sha256_hash(data, dlen, hash);
    size_t tlen = sizeof info + 32;
    if (k < tlen + 11) return false;
    uint8_t want[(XN_PJ_W) * 4];
    want[0] = 0;
    want[1] = 1;
    memset(want + 2, 0xff, k - tlen - 3);
    want[k - tlen - 1] = 0;
    memcpy(want + k - tlen, info, sizeof info);
    memcpy(want + k - 32, hash, 32);
    return xs_ct_equal(em, want, k);
}

/* a field of a flat JSON object: its raw text (a string without quotes), or NULL */
static char *xn_pj_field(const char *json, const char *name) {
    char key[64];
    snprintf(key, sizeof key, "\"%s\":", name);
    const char *at = strstr(json, key);
    if (!at) return NULL;
    at += strlen(key);
    while (*at == ' ') at++;
    if (*at == '"') {
        const char *end = strchr(at + 1, '"');
        return end ? xs_strndup_(at + 1, (size_t)(end - at - 1)) : NULL;
    }
    size_t l = strcspn(at, ",}]");
    return xs_strndup_(at, l);
}

/* the payload (JSON text) of a token that passes, or a thrown JsonWebTokenError / TokenExpiredError /
   NotBeforeError with jsonwebtoken's message */
XS_NOINLINE xs_str *xn_passport_jwt_verifyToken(xs_str *token, xs_str *key, xs_str *algorithms, xs_str *issuer, xs_str *audience, bool ignoreExpiration, int64_t clockTolerance) {
    if (token->len == 0) xn_pj_fail("JsonWebTokenError", "jwt must be provided");
    const char *t = token->data;
    const char *d1 = memchr(t, '.', (size_t)token->len);
    const char *d2 = d1 ? memchr(d1 + 1, '.', (size_t)(t + token->len - d1 - 1)) : NULL;
    if (!d1 || !d2 || memchr(d2 + 1, '.', (size_t)(t + token->len - d2 - 1))) xn_pj_fail("JsonWebTokenError", "jwt malformed");
    size_t hl64 = (size_t)(d1 - t), pl64 = (size_t)(d2 - d1 - 1), sl64 = (size_t)(t + token->len - d2 - 1);
    uint8_t *hdr = malloc(hl64 + 4), *pay = malloc(pl64 + 4), *sig = malloc(sl64 + 4);
    long hl = xs_b64_decode(t, hl64, hdr), pl = xs_b64_decode(d1 + 1, pl64, pay), sl = xs_b64_decode(d2 + 1, sl64, sig);
    if (hl <= 0 || pl <= 0 || sl < 0) { free(hdr); free(pay); free(sig); xn_pj_fail("JsonWebTokenError", "invalid token"); }
    hdr[hl] = 0;
    pay[pl] = 0;
    char *alg = xn_pj_field((char *)hdr, "alg");
    free(hdr);
    bool listed = false;
    if (alg) { /* algorithms: "RS256,HS256" (empty: the key's kind decides, as jsonwebtoken's defaults) */
        if (algorithms->len == 0) listed = strcmp(alg, strstr(key->data, "-----BEGIN") ? "RS256" : "HS256") == 0;
        else {
            const char *a = algorithms->data;
            size_t al = strlen(alg);
            while (*a) {
                size_t l = strcspn(a, ",");
                if (l == al && strncmp(a, alg, l) == 0) listed = true;
                a += l + (a[l] == ',');
            }
        }
    }
    bool good = false;
    if (!listed || (strcmp(alg, "HS256") != 0 && strcmp(alg, "RS256") != 0)) { free(alg); free(pay); free(sig); xn_pj_fail("JsonWebTokenError", "invalid algorithm"); }
    if (strcmp(alg, "HS256") == 0) {
        if (strstr(key->data, "-----BEGIN")) { free(alg); free(pay); free(sig); xn_pj_fail("JsonWebTokenError", "secretOrPublicKey must be a symmetric key when using HS256"); }
        uint8_t mac[32];
        xs_hmac256(key->data, (size_t)key->len, t, (size_t)(d2 - t), mac);
        good = sl == 32 && xs_ct_equal(mac, sig, 32);
    } else {
        good = xn_pj_rs256(key->data, (size_t)key->len, t, (size_t)(d2 - t), sig, (size_t)sl);
    }
    free(alg);
    free(sig);
    if (!good) { free(pay); xn_pj_fail("JsonWebTokenError", "invalid signature"); }
    double now = (double)time(NULL);
    char *nbf = xn_pj_field((char *)pay, "nbf");
    if (nbf && now + (double)clockTolerance < strtod(nbf, NULL)) { free(nbf); free(pay); xn_pj_fail("NotBeforeError", "jwt not active"); }
    free(nbf);
    char *exp = xn_pj_field((char *)pay, "exp");
    if (exp && !ignoreExpiration && now - (double)clockTolerance >= strtod(exp, NULL)) { free(exp); free(pay); xn_pj_fail("TokenExpiredError", "jwt expired"); }
    free(exp);
    if (audience->len > 0) {
        char *aud = xn_pj_field((char *)pay, "aud");
        bool match = aud && strcmp(aud, audience->data) == 0;
        free(aud);
        if (!match) {
            char msg[512];
            snprintf(msg, sizeof msg, "jwt audience invalid. expected: %s", audience->data);
            free(pay);
            xn_pj_fail("JsonWebTokenError", msg);
        }
    }
    if (issuer->len > 0) {
        char *iss = xn_pj_field((char *)pay, "iss");
        bool match = iss && strcmp(iss, issuer->data) == 0;
        free(iss);
        if (!match) {
            char msg[512];
            snprintf(msg, sizeof msg, "jwt issuer invalid. expected: %s", issuer->data);
            free(pay);
            xn_pj_fail("JsonWebTokenError", msg);
        }
    }
    xs_str *out = xs_str_new((char *)pay, (size_t)pl);
    free(pay);
    return out;
}
