/* jose: RSA for RS256 (RSASSA-PKCS1-v1_5 with SHA-256), keys as PEM (PKCS#8 private, SubjectPublicKeyInfo public).
   - Numbers are little-endian arrays of 64-bit limbs, up to 4096-bit moduli.
   - Modular exponentiation is Montgomery multiplication (CIOS) with a fixed 4-bit window; the private operations
     (signing, prime tests) run without branches or table indexes that depend on secret values: the window entry
     is picked by scanning the whole table with masks, and the final subtraction is a masked select.
   - Signing uses the CRT (m^dp mod p, m^dq mod q, Garner) and checks the result with the public exponent before
     it leaves (a fault would otherwise reveal a factor).
   - Key generation: two random primes of half the size, top two bits set (so n has exactly the size asked for),
     a sieve of the small primes, then 40 Miller-Rabin rounds with random bases from the system's secure random;
     e = 65537, d = e^-1 mod (p-1)(q-1), dp, dq and qi = q^-1 mod p (Fermat). */

#define XN_JR_MAX 64 /* limbs: 4096 bits */
typedef unsigned __int128 xn_jr_u128;

/* ---- limb arithmetic ---- */
static void xn_jr_zero(uint64_t *r, int n) { memset(r, 0, sizeof(uint64_t) * (size_t)n); }
static void xn_jr_copy(uint64_t *r, const uint64_t *a, int n) { memmove(r, a, sizeof(uint64_t) * (size_t)n); }
/* big-endian bytes into n limbs (the bytes must fit) */
static void xn_jr_from_be(uint64_t *r, int n, const uint8_t *p, size_t len) {
    xn_jr_zero(r, n);
    for (size_t i = 0; i < len; i++) {
        size_t bit = (len - 1 - i) * 8;
        if (bit / 64 < (size_t)n) r[bit / 64] |= (uint64_t)p[i] << (bit % 64);
    }
}
static void xn_jr_to_be(uint8_t *out, size_t len, const uint64_t *a, int n) {
    for (size_t i = 0; i < len; i++) {
        size_t bit = (len - 1 - i) * 8;
        out[i] = bit / 64 < (size_t)n ? (uint8_t)(a[bit / 64] >> (bit % 64)) : 0;
    }
}
static int xn_jr_bits(const uint64_t *a, int n) {
    for (int i = n - 1; i >= 0; i--)
        if (a[i]) return i * 64 + 64 - __builtin_clzll(a[i]);
    return 0;
}
static int xn_jr_cmp(const uint64_t *a, const uint64_t *b, int n) { /* (public values only) */
    for (int i = n - 1; i >= 0; i--)
        if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
    return 0;
}
static uint64_t xn_jr_sub(uint64_t *r, const uint64_t *a, const uint64_t *b, int n) { /* returns the borrow */
    uint64_t borrow = 0;
    for (int i = 0; i < n; i++) {
        xn_jr_u128 d = (xn_jr_u128)a[i] - b[i] - borrow;
        r[i] = (uint64_t)d;
        borrow = (uint64_t)(d >> 64) & 1;
    }
    return borrow;
}
static uint64_t xn_jr_add(uint64_t *r, const uint64_t *a, const uint64_t *b, int n) { /* returns the carry */
    uint64_t carry = 0;
    for (int i = 0; i < n; i++) {
        xn_jr_u128 s = (xn_jr_u128)a[i] + b[i] + carry;
        r[i] = (uint64_t)s;
        carry = (uint64_t)(s >> 64);
    }
    return carry;
}
/* r = mask ? a : b, mask all ones or zero */
static void xn_jr_select(uint64_t *r, const uint64_t *a, const uint64_t *b, int n, uint64_t mask) {
    for (int i = 0; i < n; i++) r[i] = (a[i] & mask) | (b[i] & ~mask);
}
/* r (an + bn limbs) = a * b */
static void xn_jr_mul(uint64_t *r, const uint64_t *a, int an, const uint64_t *b, int bn) {
    xn_jr_zero(r, an + bn);
    for (int i = 0; i < an; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < bn; j++) {
            xn_jr_u128 t = (xn_jr_u128)a[i] * b[j] + r[i + j] + carry;
            r[i + j] = (uint64_t)t;
            carry = (uint64_t)(t >> 64);
        }
        r[i + bn] = carry;
    }
}
/* r (mn limbs) = a (an limbs) mod m (mn limbs, m > 0), one bit at a time without secret-dependent branches */
static void xn_jr_mod(uint64_t *r, const uint64_t *a, int an, const uint64_t *m, int mn) {
    uint64_t rem[XN_JR_MAX + 1], t[XN_JR_MAX + 1], mm[XN_JR_MAX + 1];
    xn_jr_zero(rem, mn + 1);
    xn_jr_copy(mm, m, mn);
    mm[mn] = 0;
    for (int bit = an * 64 - 1; bit >= 0; bit--) {
        for (int i = mn; i > 0; i--) rem[i] = rem[i] << 1 | rem[i - 1] >> 63;
        rem[0] = rem[0] << 1 | ((a[bit / 64] >> (bit % 64)) & 1);
        uint64_t borrow = xn_jr_sub(t, rem, mm, mn + 1);
        xn_jr_select(rem, rem, t, mn + 1, (uint64_t)0 - borrow); /* keep rem when rem < m */
    }
    xn_jr_copy(r, rem, mn);
}
/* a mod d for a small d */
static uint32_t xn_jr_mod_small(const uint64_t *a, int n, uint32_t d) {
    xn_jr_u128 rem = 0;
    for (int i = n - 1; i >= 0; i--) rem = ((rem << 64) | a[i]) % d;
    return (uint32_t)rem;
}

/* ---- Montgomery arithmetic modulo an odd m ---- */
typedef struct {
    uint64_t m[XN_JR_MAX];
    int n;
    uint64_t m0i;            /* -m^-1 mod 2^64 */
    uint64_t rr[XN_JR_MAX];  /* R^2 mod m, R = 2^(64n) */
    uint64_t one[XN_JR_MAX]; /* R mod m: 1 in Montgomery form */
} xn_jr_mont;

static void xn_jr_mont_init(xn_jr_mont *c, const uint64_t *m, int n) {
    xn_jr_copy(c->m, m, n);
    c->n = n;
    uint64_t inv = m[0]; /* Newton: inv = m0^-1 mod 2^64 */
    for (int i = 0; i < 6; i++) inv *= 2 - m[0] * inv;
    c->m0i = (uint64_t)0 - inv;
    uint64_t big[2 * XN_JR_MAX + 1];
    xn_jr_zero(big, 2 * n + 1);
    big[2 * n] = 1;
    xn_jr_mod(c->rr, big, 2 * n + 1, m, n);
    xn_jr_zero(big, n + 1);
    big[n] = 1;
    xn_jr_mod(c->one, big, n + 1, m, n);
}
/* r = a * b / R mod m (a, b < m); r may be a or b */
static void xn_jr_montmul(uint64_t *r, const uint64_t *a, const uint64_t *b, const xn_jr_mont *c) {
    int n = c->n;
    uint64_t t[XN_JR_MAX + 2];
    xn_jr_zero(t, n + 2);
    for (int i = 0; i < n; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < n; j++) {
            xn_jr_u128 s = (xn_jr_u128)a[j] * b[i] + t[j] + carry;
            t[j] = (uint64_t)s;
            carry = (uint64_t)(s >> 64);
        }
        xn_jr_u128 s = (xn_jr_u128)t[n] + carry;
        t[n] = (uint64_t)s;
        t[n + 1] = (uint64_t)(s >> 64);
        uint64_t q = t[0] * c->m0i;
        s = (xn_jr_u128)q * c->m[0] + t[0];
        carry = (uint64_t)(s >> 64);
        for (int j = 1; j < n; j++) {
            s = (xn_jr_u128)q * c->m[j] + t[j] + carry;
            t[j - 1] = (uint64_t)s;
            carry = (uint64_t)(s >> 64);
        }
        s = (xn_jr_u128)t[n] + carry;
        t[n - 1] = (uint64_t)s;
        t[n] = t[n + 1] + (uint64_t)(s >> 64);
    }
    uint64_t u[XN_JR_MAX];
    uint64_t borrow = xn_jr_sub(u, t, c->m, n);
    uint64_t take = (t[n] | (borrow ^ 1)) & 1; /* t >= m */
    xn_jr_select(r, u, t, n, (uint64_t)0 - take);
}
/* r = base^exp in Montgomery form (base < m, in normal form; exp of en limbs) */
static void xn_jr_modexp_mont(uint64_t *r, const uint64_t *base, const uint64_t *exp, int en, const xn_jr_mont *c) {
    int n = c->n;
    uint64_t *table = malloc(sizeof(uint64_t) * 16 * (size_t)n);
    uint64_t acc[XN_JR_MAX], pick[XN_JR_MAX];
    if (!table) xs_panic("out of memory");
    xn_jr_copy(table, c->one, n);
    xn_jr_montmul(table + n, base, c->rr, c); /* base * R mod m */
    for (int i = 2; i < 16; i++) xn_jr_montmul(table + i * n, table + (i - 1) * n, table + n, c);
    xn_jr_copy(acc, c->one, n);
    for (int w = en * 16 - 1; w >= 0; w--) {
        for (int k = 0; k < 4; k++) xn_jr_montmul(acc, acc, acc, c);
        uint64_t idx = (exp[w / 16] >> ((w % 16) * 4)) & 15;
        xn_jr_zero(pick, n);
        for (uint64_t i = 0; i < 16; i++) {
            uint64_t mask = (uint64_t)0 - (uint64_t)(((i ^ idx) - 1) >> 63); /* i == idx */
            for (int j = 0; j < n; j++) pick[j] |= table[i * (uint64_t)n + (uint64_t)j] & mask;
        }
        xn_jr_montmul(acc, acc, pick, c);
    }
    xn_jr_copy(r, acc, n);
    memset(table, 0, sizeof(uint64_t) * 16 * (size_t)n);
    free(table);
}
/* r = base^exp mod m (base < m) */
static void xn_jr_modexp(uint64_t *r, const uint64_t *base, const uint64_t *exp, int en, const xn_jr_mont *c) {
    uint64_t one[XN_JR_MAX];
    xn_jr_zero(one, c->n);
    one[0] = 1;
    xn_jr_modexp_mont(r, base, exp, en, c);
    xn_jr_montmul(r, r, one, c); /* out of Montgomery form */
}

/* r = base^e mod m for a public exponent (no secret involved): left to right, square and multiply */
static void xn_jr_modexp_public(uint64_t *r, const uint64_t *base, const uint64_t *e, int en, const xn_jr_mont *c) {
    int n = c->n;
    uint64_t bm[XN_JR_MAX], acc[XN_JR_MAX], one[XN_JR_MAX];
    xn_jr_montmul(bm, base, c->rr, c);
    xn_jr_copy(acc, c->one, n);
    for (int bit = xn_jr_bits(e, en) - 1; bit >= 0; bit--) {
        xn_jr_montmul(acc, acc, acc, c);
        if ((e[bit / 64] >> (bit % 64)) & 1) xn_jr_montmul(acc, acc, bm, c);
    }
    xn_jr_zero(one, n);
    one[0] = 1;
    xn_jr_montmul(r, acc, one, c);
}

/* ---- DER and PEM ---- */
static bool xn_jr_der(const uint8_t **p, const uint8_t *end, uint8_t tag, const uint8_t **body, size_t *len) {
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
/* a non-negative INTEGER into limbs (false when it does not fit) */
static bool xn_jr_der_int(const uint8_t **p, const uint8_t *end, uint64_t *r, int n) {
    const uint8_t *b;
    size_t l;
    if (!xn_jr_der(p, end, 0x02, &b, &l) || l == 0 || (b[0] & 0x80)) return false;
    while (l > 0 && *b == 0) { b++; l--; }
    if (l > (size_t)n * 8) return false;
    xn_jr_from_be(r, n, b, l);
    return true;
}
static void xn_jr_put_len(xs_sb *b, size_t n) {
    char t[4];
    if (n < 0x80) { t[0] = (char)n; xs_sb_put(b, t, 1); }
    else if (n < 0x100) { t[0] = (char)0x81; t[1] = (char)n; xs_sb_put(b, t, 2); }
    else { t[0] = (char)0x82; t[1] = (char)(n >> 8); t[2] = (char)n; xs_sb_put(b, t, 3); }
}
static void xn_jr_put(xs_sb *b, uint8_t tag, const char *body, size_t n) {
    char t = (char)tag;
    xs_sb_put(b, &t, 1);
    xn_jr_put_len(b, n);
    xs_sb_put(b, body, n);
}
static void xn_jr_put_int(xs_sb *b, const uint64_t *a, int n) {
    uint8_t buf[XN_JR_MAX * 8 + 1];
    size_t len = (size_t)n * 8;
    xn_jr_to_be(buf + 1, len, a, n);
    uint8_t *v = buf + 1;
    while (len > 1 && v[0] == 0) { v++; len--; }
    if (v[0] & 0x80) { v--; v[0] = 0; len++; }
    xn_jr_put(b, 0x02, (const char *)v, len);
    memset(buf, 0, sizeof buf);
}
static const uint8_t xn_jr_rsa_algid[] = {0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00};
/* the body of the PEM block "-----BEGIN <label>-----", decoded (malloc'd), or NULL */
static uint8_t *xn_jr_pem(const char *pem, const char *label, long *len) {
    char head[64];
    snprintf(head, sizeof head, "-----BEGIN %s-----", label);
    const char *b = strstr(pem, head);
    if (!b) return NULL;
    b += strlen(head);
    char tail[64];
    snprintf(tail, sizeof tail, "-----END %s-----", label);
    const char *e = strstr(b, tail);
    if (!e) return NULL;
    char *clean = malloc((size_t)(e - b) + 1);
    size_t c = 0;
    for (const char *s = b; s < e; s++)
        if (*s != '\n' && *s != '\r' && *s != ' ' && *s != '\t') clean[c++] = *s;
    uint8_t *der = malloc(c + 4);
    *len = xs_b64_decode(clean, c, der);
    free(clean);
    if (*len <= 0) { free(der); return NULL; }
    return der;
}
static xs_str *xn_jr_to_pem(const char *label, const xs_sb *der) {
    size_t b64n = (der->len + 2) / 3 * 4;
    char *b64 = malloc(b64n + 1);
    xs_b64_encode((const uint8_t *)der->data, der->len, b64, false);
    xs_sb pem = {0};
    char line[80];
    snprintf(line, sizeof line, "-----BEGIN %s-----\n", label);
    xs_sb_puts(&pem, line);
    for (size_t i = 0; i < b64n; i += 64) {
        xs_sb_put(&pem, b64 + i, b64n - i < 64 ? b64n - i : 64);
        xs_sb_put(&pem, "\n", 1);
    }
    snprintf(line, sizeof line, "-----END %s-----\n", label);
    xs_sb_puts(&pem, line);
    xs_str *out = xs_str_new(pem.data, pem.len);
    free(b64);
    free(pem.data);
    return out;
}

/* ---- keys ---- */
typedef struct {
    int n;                                      /* limbs of the modulus */
    int bits;
    bool priv;
    uint64_t mod[XN_JR_MAX], e[XN_JR_MAX];
    uint64_t d[XN_JR_MAX], p[XN_JR_MAX], q[XN_JR_MAX], dp[XN_JR_MAX], dq[XN_JR_MAX], qi[XN_JR_MAX];
    int hn;                                     /* limbs of p and q */
} xn_jr_key;

static void xn_jr_key_wipe(xn_jr_key *k) { memset(k, 0, sizeof *k); }
static int xn_jr_limbs_of(const uint8_t *b, size_t l) {
    while (l > 0 && *b == 0) { b++; l--; }
    return (int)((l + 7) / 8);
}
/* RSAPublicKey ::= SEQUENCE { modulus, publicExponent } */
static bool xn_jr_rsa_public_seq(const uint8_t *p, const uint8_t *end, xn_jr_key *k) {
    const uint8_t *seq, *nb;
    size_t sl, nl;
    if (!xn_jr_der(&p, end, 0x30, &seq, &sl)) return false;
    const uint8_t *q = seq, *qe = seq + sl, *peek = seq;
    if (!xn_jr_der(&peek, qe, 0x02, &nb, &nl)) return false;
    k->n = xn_jr_limbs_of(nb, nl);
    if (k->n < 8 || k->n > XN_JR_MAX) return false; /* 512 to 4096 bits */
    if (!xn_jr_der_int(&q, qe, k->mod, k->n) || !xn_jr_der_int(&q, qe, k->e, k->n)) return false;
    k->bits = xn_jr_bits(k->mod, k->n);
    return (k->mod[0] & 1) && xn_jr_bits(k->e, k->n) > 1;
}
/* PrivateKeyInfo ::= SEQUENCE { version, AlgorithmIdentifier (rsaEncryption), OCTET STRING RSAPrivateKey } */
static bool xn_jr_parse_pkcs8(const uint8_t *der, long len, xn_jr_key *k) {
    const uint8_t *p = der, *end = der + len, *seq, *alg, *oct, *ver;
    size_t sl, al, ol, vl;
    if (!xn_jr_der(&p, end, 0x30, &seq, &sl)) return false;
    const uint8_t *q = seq, *qe = seq + sl;
    if (!xn_jr_der(&q, qe, 0x02, &ver, &vl) || !xn_jr_der(&q, qe, 0x30, &alg, &al) || al < 11 || memcmp(alg, xn_jr_rsa_algid + 2, 11) != 0) return false;
    if (!xn_jr_der(&q, qe, 0x04, &oct, &ol)) return false;
    /* RSAPrivateKey ::= SEQUENCE { version, n, e, d, p, q, dp, dq, qi } */
    const uint8_t *r = oct, *re = oct + ol, *rs, *nb, *peek;
    size_t rl, nl;
    if (!xn_jr_der(&r, re, 0x30, &rs, &rl)) return false;
    const uint8_t *s = rs, *se = rs + rl;
    if (!xn_jr_der(&s, se, 0x02, &ver, &vl)) return false;
    peek = s;
    if (!xn_jr_der(&peek, se, 0x02, &nb, &nl)) return false;
    k->n = xn_jr_limbs_of(nb, nl);
    if (k->n < 8 || k->n > XN_JR_MAX) return false;
    k->hn = (k->n + 1) / 2;
    if (!xn_jr_der_int(&s, se, k->mod, k->n) || !xn_jr_der_int(&s, se, k->e, k->n) || !xn_jr_der_int(&s, se, k->d, k->n) ||
        !xn_jr_der_int(&s, se, k->p, k->hn) || !xn_jr_der_int(&s, se, k->q, k->hn) || !xn_jr_der_int(&s, se, k->dp, k->hn) ||
        !xn_jr_der_int(&s, se, k->dq, k->hn) || !xn_jr_der_int(&s, se, k->qi, k->hn))
        return false;
    k->bits = xn_jr_bits(k->mod, k->n);
    k->priv = true;
    return (k->mod[0] & 1) && (k->p[0] & 1) && (k->q[0] & 1);
}
/* SubjectPublicKeyInfo ::= SEQUENCE { AlgorithmIdentifier (rsaEncryption), BIT STRING RSAPublicKey } */
static bool xn_jr_parse_spki(const uint8_t *der, long len, xn_jr_key *k) {
    const uint8_t *p = der, *end = der + len, *seq, *alg, *bits;
    size_t sl, al, bl;
    if (!xn_jr_der(&p, end, 0x30, &seq, &sl)) return false;
    const uint8_t *q = seq, *qe = seq + sl;
    if (!xn_jr_der(&q, qe, 0x30, &alg, &al) || al < 11 || memcmp(alg, xn_jr_rsa_algid + 2, 11) != 0) return false;
    if (!xn_jr_der(&q, qe, 0x03, &bits, &bl) || bl < 2 || bits[0] != 0) return false;
    return xn_jr_rsa_public_seq(bits + 1, bits + bl, k);
}
/* a key from PEM: "pkcs8" (private), "spki" (public), or "any" (either) */
static bool xn_jr_load(const char *pem, const char *kind, xn_jr_key *k) {
    memset(k, 0, sizeof *k);
    long len = 0;
    uint8_t *der;
    bool ok = false;
    if (strcmp(kind, "spki") != 0 && (der = xn_jr_pem(pem, "PRIVATE KEY", &len))) {
        ok = xn_jr_parse_pkcs8(der, len, k);
        memset(der, 0, (size_t)len);
        free(der);
        if (ok || strcmp(kind, "pkcs8") == 0) return ok;
    }
    if (strcmp(kind, "pkcs8") != 0 && (der = xn_jr_pem(pem, "PUBLIC KEY", &len))) {
        ok = xn_jr_parse_spki(der, len, k);
        free(der);
    }
    return ok;
}
static xs_str *xn_jr_spki_pem(const xn_jr_key *k) {
    xs_sb ints = {0}, rsa = {0}, bits = {0}, alg = {0}, spki = {0};
    xn_jr_put_int(&ints, k->mod, k->n);
    xn_jr_put_int(&ints, k->e, k->n);
    xn_jr_put(&rsa, 0x30, ints.data, ints.len);
    xs_sb_put(&bits, "\0", 1);
    xs_sb_put(&bits, rsa.data, rsa.len);
    xs_sb_put(&alg, (const char *)xn_jr_rsa_algid, sizeof xn_jr_rsa_algid);
    xn_jr_put(&alg, 0x03, bits.data, bits.len);
    xn_jr_put(&spki, 0x30, alg.data, alg.len);
    xs_str *out = xn_jr_to_pem("PUBLIC KEY", &spki);
    free(ints.data); free(rsa.data); free(bits.data); free(alg.data); free(spki.data);
    return out;
}
static xs_str *xn_jr_pkcs8_pem(const xn_jr_key *k) {
    xs_sb ints = {0}, rsa = {0}, info = {0}, outer = {0};
    xs_sb_put(&ints, "\x02\x01\x00", 3); /* version 0 */
    xn_jr_put_int(&ints, k->mod, k->n);
    xn_jr_put_int(&ints, k->e, k->n);
    xn_jr_put_int(&ints, k->d, k->n);
    xn_jr_put_int(&ints, k->p, k->hn);
    xn_jr_put_int(&ints, k->q, k->hn);
    xn_jr_put_int(&ints, k->dp, k->hn);
    xn_jr_put_int(&ints, k->dq, k->hn);
    xn_jr_put_int(&ints, k->qi, k->hn);
    xn_jr_put(&rsa, 0x30, ints.data, ints.len);
    xs_sb_put(&info, "\x02\x01\x00", 3);
    xs_sb_put(&info, (const char *)xn_jr_rsa_algid, sizeof xn_jr_rsa_algid);
    xn_jr_put(&info, 0x04, rsa.data, rsa.len);
    xn_jr_put(&outer, 0x30, info.data, info.len);
    xs_str *out = xn_jr_to_pem("PRIVATE KEY", &outer);
    memset(ints.data, 0, ints.len); memset(rsa.data, 0, rsa.len); memset(info.data, 0, info.len); memset(outer.data, 0, outer.len);
    free(ints.data); free(rsa.data); free(info.data); free(outer.data);
    return out;
}

/* ---- primes ---- */
static uint32_t xn_jr_small_primes[300];
static int xn_jr_nsmall;
static void xn_jr_init_small(void) {
    if (xn_jr_nsmall) return;
    int count = 0;
    for (uint32_t c = 3; count < 300; c += 2) {
        bool prime = true;
        for (uint32_t d = 3; d * d <= c; d += 2)
            if (c % d == 0) { prime = false; break; }
        if (prime) xn_jr_small_primes[count++] = c;
    }
    __atomic_store_n(&xn_jr_nsmall, count, __ATOMIC_RELEASE);
}
static void xn_jr_random(uint64_t *r, int n) { xs_random_bytes(r, sizeof(uint64_t) * (size_t)n); }
/* Miller-Rabin with `rounds` random bases: false when c is composite */
static bool xn_jr_probable_prime(const uint64_t *c, int n, int rounds) {
    xn_jr_mont *ctx = malloc(sizeof *ctx);
    if (!ctx) xs_panic("out of memory");
    xn_jr_mont_init(ctx, c, n);
    uint64_t cm1[XN_JR_MAX], d[XN_JR_MAX], one[XN_JR_MAX], a[XN_JR_MAX], x[XN_JR_MAX], minus1[XN_JR_MAX];
    xn_jr_zero(one, n);
    one[0] = 1;
    xn_jr_sub(cm1, c, one, n);
    int s = 0;
    while (!((cm1[s / 64] >> (s % 64)) & 1)) s++;
    xn_jr_zero(d, n); /* d = (c - 1) >> s */
    for (int i = 0; i < n * 64 - s; i++)
        if ((cm1[(i + s) / 64] >> ((i + s) % 64)) & 1) d[i / 64] |= (uint64_t)1 << (i % 64);
    xn_jr_sub(minus1, c, ctx->one, n); /* -1 in Montgomery form: m - R mod m */
    bool prime = true;
    for (int r = 0; r < rounds && prime; r++) {
        do { /* a random base in [2, c - 2] */
            xn_jr_random(a, n);
            a[n - 1] &= c[n - 1] >> 1;
        } while (xn_jr_bits(a, n) < 2);
        xn_jr_modexp_mont(x, a, d, n, ctx);
        if (xn_jr_cmp(x, ctx->one, n) == 0 || xn_jr_cmp(x, minus1, n) == 0) continue;
        bool found = false;
        for (int i = 1; i < s && !found; i++) {
            xn_jr_montmul(x, x, x, ctx);
            if (xn_jr_cmp(x, minus1, n) == 0) found = true;
            else if (xn_jr_cmp(x, ctx->one, n) == 0) break;
        }
        if (!found) prime = false;
    }
    memset(ctx, 0, sizeof *ctx);
    free(ctx);
    return prime;
}
/* a random prime of `bits` bits (a multiple of 64), top two bits set, p mod e != 1 */
static void xn_jr_random_prime(uint64_t *p, int bits, uint32_t e) {
    int n = bits / 64;
    xn_jr_init_small();
    uint32_t res[300];
    for (;;) {
        xn_jr_random(p, n);
        p[n - 1] |= (uint64_t)3 << 62;
        p[0] |= 1;
        for (int i = 0; i < xn_jr_nsmall; i++) res[i] = xn_jr_mod_small(p, n, xn_jr_small_primes[i]);
        uint32_t re = xn_jr_mod_small(p, n, e);
        for (uint32_t delta = 0; delta < (1u << 20); delta += 2) {
            bool divisible = false;
            for (int i = 0; i < xn_jr_nsmall && !divisible; i++) divisible = (res[i] + delta) % xn_jr_small_primes[i] == 0;
            if (divisible || (re + delta) % e == 1) continue;
            uint64_t c[XN_JR_MAX], dl[XN_JR_MAX];
            xn_jr_zero(dl, n);
            dl[0] = delta;
            if (xn_jr_add(c, p, dl, n) || !(c[n - 1] >> 62 == 3)) break; /* (left the range: start again) */
            if (xn_jr_probable_prime(c, n, 40)) {
                xn_jr_copy(p, c, n);
                return;
            }
        }
    }
}
/* a^-1 mod m for small a, m (gcd 1) */
static uint32_t xn_jr_inv_small(uint32_t a, uint32_t m) {
    int64_t t = 0, nt = 1, r = m, nr = a;
    while (nr) {
        int64_t q = r / nr, x;
        x = t - q * nt; t = nt; nt = x;
        x = r - q * nr; r = nr; nr = x;
    }
    return (uint32_t)(t < 0 ? t + m : t);
}
static void xn_jr_generate(xn_jr_key *k, int bits) {
    memset(k, 0, sizeof *k);
    const uint32_t e = 65537;
    k->n = bits / 64;
    k->hn = k->n / 2;
    k->bits = bits;
    k->priv = true;
    int hn = k->hn, n = k->n;
    do {
        xn_jr_random_prime(k->p, bits / 2, e);
        xn_jr_random_prime(k->q, bits / 2, e);
    } while (xn_jr_cmp(k->p, k->q, hn) == 0);
    if (xn_jr_cmp(k->p, k->q, hn) < 0) { /* p > q, as OpenSSL writes them */
        uint64_t t[XN_JR_MAX];
        xn_jr_copy(t, k->p, hn); xn_jr_copy(k->p, k->q, hn); xn_jr_copy(k->q, t, hn);
        memset(t, 0, sizeof t);
    }
    xn_jr_mul(k->mod, k->p, hn, k->q, hn);
    xn_jr_zero(k->e, n);
    k->e[0] = e;
    /* d = (1 + k*phi) / e, with k = -phi^-1 mod e */
    uint64_t one[XN_JR_MAX], pm1[XN_JR_MAX], qm1[XN_JR_MAX], phi[XN_JR_MAX], t[XN_JR_MAX + 1];
    xn_jr_zero(one, hn);
    one[0] = 1;
    xn_jr_sub(pm1, k->p, one, hn);
    xn_jr_sub(qm1, k->q, one, hn);
    xn_jr_mul(phi, pm1, hn, qm1, hn);
    uint32_t kk = e - xn_jr_inv_small(xn_jr_mod_small(phi, n, e), e);
    uint64_t carry = 1;
    for (int i = 0; i < n; i++) {
        xn_jr_u128 s = (xn_jr_u128)phi[i] * kk + carry;
        t[i] = (uint64_t)s;
        carry = (uint64_t)(s >> 64);
    }
    t[n] = carry;
    xn_jr_u128 rem = 0;
    for (int i = n; i >= 0; i--) {
        xn_jr_u128 cur = (rem << 64) | t[i];
        t[i] = (uint64_t)(cur / e);
        rem = cur % e;
    }
    xn_jr_copy(k->d, t, n);
    xn_jr_mod(k->dp, k->d, n, pm1, hn);
    xn_jr_mod(k->dq, k->d, n, qm1, hn);
    /* qi = q^(p-2) mod p */
    xn_jr_mont *ctx = malloc(sizeof *ctx);
    if (!ctx) xs_panic("out of memory");
    xn_jr_mont_init(ctx, k->p, hn);
    uint64_t two[XN_JR_MAX], pm2[XN_JR_MAX], qr[XN_JR_MAX];
    xn_jr_zero(two, hn);
    two[0] = 2;
    xn_jr_sub(pm2, k->p, two, hn);
    xn_jr_mod(qr, k->q, hn, k->p, hn);
    xn_jr_modexp(k->qi, qr, pm2, hn, ctx);
    memset(ctx, 0, sizeof *ctx);
    free(ctx);
    memset(phi, 0, sizeof phi); memset(t, 0, sizeof t); memset(pm1, 0, sizeof pm1); memset(qm1, 0, sizeof qm1); memset(pm2, 0, sizeof pm2); memset(qr, 0, sizeof qr);
}

/* ---- RS256 ---- */
static const uint8_t xn_jr_sha256_info[] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
/* EMSA-PKCS1-v1_5 of SHA-256(data), k bytes; false when the modulus is too small */
static bool xn_jr_encode(uint8_t *em, size_t k, const char *data, size_t dlen) {
    size_t tlen = sizeof xn_jr_sha256_info + 32;
    if (k < tlen + 11) return false;
    em[0] = 0;
    em[1] = 1;
    memset(em + 2, 0xff, k - tlen - 3);
    em[k - tlen - 1] = 0;
    memcpy(em + k - tlen, xn_jr_sha256_info, sizeof xn_jr_sha256_info);
    xs_sha256_hash(data, dlen, em + k - 32);
    return true;
}
/* A key read from PEM, with its Montgomery contexts: the last few keys of this thread are kept (a server signs and
   verifies with the same one or two keys; parsing and the R^2 mod m set-up cost more than a verification). */
typedef struct {
    char *pem;
    size_t len;
    xn_jr_key key;
    xn_jr_mont cn, cp, cq;
} xn_jr_loaded;
#define XN_JR_CACHE 4
static XS_TLS xn_jr_loaded *xn_jr_cache[XN_JR_CACHE];
static XS_TLS int xn_jr_cache_next;
static xn_jr_loaded *xn_jr_get(const xs_str *pem) {
    for (int i = 0; i < XN_JR_CACHE; i++) {
        xn_jr_loaded *l = xn_jr_cache[i];
        if (l && l->len == (size_t)pem->len && memcmp(l->pem, pem->data, l->len) == 0) return l;
    }
    xn_jr_loaded *l = calloc(1, sizeof *l);
    if (!l) xs_panic("out of memory");
    if (!xn_jr_load(pem->data, "any", &l->key)) { free(l); return NULL; }
    xn_jr_mont_init(&l->cn, l->key.mod, l->key.n);
    if (l->key.priv) {
        xn_jr_mont_init(&l->cp, l->key.p, l->key.hn);
        xn_jr_mont_init(&l->cq, l->key.q, l->key.hn);
    }
    l->pem = malloc((size_t)pem->len + 1);
    memcpy(l->pem, pem->data, (size_t)pem->len + 1);
    l->len = (size_t)pem->len;
    xn_jr_loaded *old = xn_jr_cache[xn_jr_cache_next];
    if (old) {
        memset(old->pem, 0, old->len);
        free(old->pem);
        memset(old, 0, sizeof *old);
        free(old);
    }
    xn_jr_cache[xn_jr_cache_next] = l;
    xn_jr_cache_next = (xn_jr_cache_next + 1) % XN_JR_CACHE;
    return l;
}
/* s^e mod n */
static void xn_jr_public_op(uint64_t *r, const uint64_t *s, const xn_jr_key *k, const xn_jr_mont *cn) {
    xn_jr_modexp_public(r, s, k->e, k->n, cn);
}
static bool xn_jr_sign(const xn_jr_loaded *l, const char *data, size_t dlen, uint8_t *sig, size_t *slen) {
    const xn_jr_key *k = &l->key;
    const xn_jr_mont *cp = &l->cp, *cq = &l->cq;
    size_t kb = (size_t)(k->bits + 7) / 8;
    uint8_t em[XN_JR_MAX * 8];
    if (!xn_jr_encode(em, kb, data, dlen)) return false;
    int n = k->n, hn = k->hn;
    uint64_t m[XN_JR_MAX], m1[XN_JR_MAX], m2[XN_JR_MAX], h[XN_JR_MAX], diff[XN_JR_MAX], tmp[XN_JR_MAX], s[XN_JR_MAX], check[XN_JR_MAX];
    xn_jr_from_be(m, n, em, kb);
    /* m1 = (m mod p)^dp mod p, m2 = (m mod q)^dq mod q */
    xn_jr_mod(tmp, m, n, k->p, hn);
    xn_jr_modexp(m1, tmp, k->dp, hn, cp);
    xn_jr_mod(tmp, m, n, k->q, hn);
    xn_jr_modexp(m2, tmp, k->dq, hn, cq);
    /* h = qi * (m1 - m2) mod p; (m1 - m2) mod p: m2 < q may exceed p, so m2 mod p first */
    xn_jr_mod(tmp, m2, hn, k->p, hn);
    uint64_t borrow = xn_jr_sub(diff, m1, tmp, hn);
    xn_jr_add(tmp, diff, k->p, hn);
    xn_jr_select(diff, tmp, diff, hn, (uint64_t)0 - borrow);
    /* qi * diff mod p, by Montgomery: (qi*R) * diff / R */
    xn_jr_montmul(tmp, k->qi, cp->rr, cp);
    xn_jr_montmul(h, tmp, diff, cp);
    /* s = m2 + h * q */
    uint64_t hq[2 * XN_JR_MAX];
    xn_jr_mul(hq, h, hn, k->q, hn);
    xn_jr_zero(s, n);
    xn_jr_copy(s, m2, hn);
    xn_jr_add(s, s, hq, n);
    /* the check: s^e mod n must give m back */
    xn_jr_public_op(check, s, k, &l->cn);
    bool ok = xn_jr_cmp(check, m, n) == 0;
    if (ok) {
        xn_jr_to_be(sig, kb, s, n);
        *slen = kb;
    }
    memset(m1, 0, sizeof m1); memset(m2, 0, sizeof m2); memset(h, 0, sizeof h); memset(diff, 0, sizeof diff); memset(tmp, 0, sizeof tmp); memset(hq, 0, sizeof hq);
    return ok;
}
static bool xn_jr_verify(const xn_jr_loaded *l, const char *data, size_t dlen, const uint8_t *sig, size_t slen) {
    const xn_jr_key *k = &l->key;
    size_t kb = (size_t)(k->bits + 7) / 8;
    if (slen != kb) return false;
    uint64_t s[XN_JR_MAX], r[XN_JR_MAX];
    xn_jr_from_be(s, k->n, sig, slen);
    if (xn_jr_cmp(s, k->mod, k->n) >= 0) return false;
    xn_jr_public_op(r, s, k, &l->cn);
    uint8_t em[XN_JR_MAX * 8], want[XN_JR_MAX * 8];
    xn_jr_to_be(em, kb, r, k->n);
    if (!xn_jr_encode(want, kb, data, dlen)) return false;
    return xs_ct_equal(em, want, kb);
}

/* ---- what index.ts declares ---- */

/* a new key pair: the private key as PKCS#8 PEM (bits: 2048 to 4096, a multiple of 128) */
XS_NOINLINE xs_str *xn_jose_rsaGenerate(int64_t bits) {
    if (bits < 1024 || bits > XN_JR_MAX * 64 || bits % 128 != 0) xs_panic("jose: the modulus length must be a multiple of 128 from 1024 to 4096");
    xn_jr_key *k = malloc(sizeof *k);
    if (!k) xs_panic("out of memory");
    xn_jr_generate(k, (int)bits);
    xs_str *pem = xn_jr_pkcs8_pem(k);
    xn_jr_key_wipe(k);
    free(k);
    return pem;
}
/* the modulus size in bits of a PEM key of that kind ("pkcs8", "spki", "any"), 0 when it is not one */
XS_NOINLINE int64_t xn_jose_rsaKeyBits(xs_str *pem, xs_str *kind) {
    xn_jr_key *k = malloc(sizeof *k);
    if (!k) xs_panic("out of memory");
    int64_t bits = xn_jr_load(pem->data, kind->data, k) ? k->bits : 0;
    xn_jr_key_wipe(k);
    free(k);
    return bits;
}
/* the public key (SPKI PEM) of a PEM key */
XS_NOINLINE xs_str *xn_jose_rsaPublicPem(xs_str *pem) {
    xn_jr_key *k = malloc(sizeof *k);
    if (!k) xs_panic("out of memory");
    xs_str *out = xn_jr_load(pem->data, "any", k) ? xn_jr_spki_pem(k) : xs_str_new("", 0);
    xn_jr_key_wipe(k);
    free(k);
    return out;
}
static xs_str *xn_jr_b64url(const uint8_t *p, size_t n) {
    char *b = malloc((n + 2) / 3 * 4 + 1);
    size_t l = xs_b64_encode(p, n, b, true);
    xs_str *s = xs_str_new(b, l);
    free(b);
    return s;
}
static xs_str *xn_jr_limbs_b64url(const uint64_t *a, int n) {
    uint8_t buf[XN_JR_MAX * 8];
    size_t len = (size_t)(xn_jr_bits(a, n) + 7) / 8;
    if (len == 0) len = 1;
    xn_jr_to_be(buf, len, a, n);
    xs_str *s = xn_jr_b64url(buf, len);
    memset(buf, 0, sizeof buf);
    return s;
}
/* the JWK members of a PEM key in base64url, joined by ".": n.e, or n.e.d.p.q.dp.dq.qi for a private key */
XS_NOINLINE xs_str *xn_jose_rsaJwk(xs_str *pem) {
    xn_jr_key *k = malloc(sizeof *k);
    if (!k) xs_panic("out of memory");
    if (!xn_jr_load(pem->data, "any", k)) { free(k); return xs_str_new("", 0); }
    xs_sb sb = {0};
    const uint64_t *parts[8] = {k->mod, k->e, k->d, k->p, k->q, k->dp, k->dq, k->qi};
    int sizes[8] = {k->n, k->n, k->n, k->hn, k->hn, k->hn, k->hn, k->hn};
    for (int i = 0; i < (k->priv ? 8 : 2); i++) {
        xs_str *s = xn_jr_limbs_b64url(parts[i], sizes[i]);
        if (i) xs_sb_put(&sb, ".", 1);
        xs_sb_put(&sb, s->data, (size_t)s->len);
        memset(s->data, 0, (size_t)s->len);
        xs_release(s);
    }
    xs_str *out = xs_str_new(sb.data, sb.len);
    memset(sb.data, 0, sb.len);
    free(sb.data);
    xn_jr_key_wipe(k);
    free(k);
    return out;
}
/* RS256 signature (base64url) of data with a PKCS#8 PEM key */
XS_NOINLINE xs_str *xn_jose_rsaSign(xs_str *pem, xs_str *data) {
    xn_jr_loaded *l = xn_jr_get(pem);
    if (!l || !l->key.priv) xs_panic("jose: not an RSA private key");
    uint8_t sig[XN_JR_MAX * 8];
    size_t slen = 0;
    bool ok = xn_jr_sign(l, data->data, (size_t)data->len, sig, &slen);
    if (!ok) xs_panic("jose: RSA signing failed");
    return xn_jr_b64url(sig, slen);
}
/* whether sig (base64url) is the RS256 signature of data under a PEM key (public, or the public half of a private one) */
XS_NOINLINE bool xn_jose_rsaVerify(xs_str *pem, xs_str *data, xs_str *sig64) {
    xn_jr_loaded *l = xn_jr_get(pem);
    bool ok = false;
    if (l) {
        uint8_t *sig = malloc((size_t)sig64->len + 4);
        long sl = xs_b64_decode(sig64->data, (size_t)sig64->len, sig);
        ok = sl > 0 && xn_jr_verify(l, data->data, (size_t)data->len, sig, (size_t)sl);
        free(sig);
    }
    return ok;
}
/* text as base64url (no padding), and back (null when it is not base64url) */
XS_NOINLINE xs_str *xn_jose_b64urlEncode(xs_str *s) { return xn_jr_b64url((const uint8_t *)s->data, (size_t)s->len); }
XS_NOINLINE xs_str *xn_jose_b64urlDecode(xs_str *s) {
    for (int64_t i = 0; i < s->len; i++) {
        char c = s->data[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return NULL;
    }
    uint8_t *out = malloc((size_t)s->len + 4);
    long n = xs_b64_decode(s->data, (size_t)s->len, out);
    xs_str *r = n < 0 ? NULL : xs_str_new((const char *)out, (size_t)n);
    free(out);
    return r;
}
