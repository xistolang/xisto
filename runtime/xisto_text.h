/* Unicode text beyond the core: String.prototype.normalize (UAX #15) over the tables of runtime/xisto_unicode.h
   (generated from ICU by tools/gen_unicode.c, the Unicode version of the Node the output is compared with).
   Appended after the runtime only for a program that normalizes text (XS_RT_NORMALIZE; XS_RT_NORMALIZE_K when it
   asks for NFKC or NFKD, which take the compatibility table too). */
#ifndef XISTO_TEXT_H
#define XISTO_TEXT_H
#ifdef XS_RT_NORMALIZE

static int xs_un_ccc_of(uint32_t c) {
    if (c < 0x300) return 0;
    size_t lo = 0, hi = sizeof xs_un_ccc / sizeof xs_un_ccc[0];
    while (lo < hi) {
        size_t m = (lo + hi) / 2;
        if (c < xs_un_ccc[m][0]) hi = m;
        else if (c > xs_un_ccc[m][1]) lo = m + 1;
        else return (int)xs_un_ccc[m][2];
    }
    return 0;
}
/* the index of c in a sorted table of code points, or -1 */
static int xs_un_find(const uint32_t *t, int n, uint32_t c) {
    int lo = 0, hi = n;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (t[m] < c) lo = m + 1;
        else hi = m;
    }
    return lo < n && t[lo] == c ? lo : -1;
}
enum { XS_UN_SBASE = 0xAC00, XS_UN_LBASE = 0x1100, XS_UN_VBASE = 0x1161, XS_UN_TBASE = 0x11A7,
       XS_UN_LCOUNT = 19, XS_UN_VCOUNT = 21, XS_UN_TCOUNT = 28, XS_UN_NCOUNT = 588, XS_UN_SCOUNT = 11172 };
typedef struct { uint32_t *c; size_t n, cap; } xs_un_buf;
static void xs_un_push(xs_un_buf *b, uint32_t c) {
    if (b->n == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        b->c = realloc(b->c, b->cap * sizeof(uint32_t));
        if (!b->c) xs_panic("out of memory");
    }
    b->c[b->n++] = c;
}
/* the full decomposition of c (compat: the compatibility one) appended to b */
static void xs_un_decompose(xs_un_buf *b, uint32_t c, bool compat) {
    if (c < 0xA0) { xs_un_push(b, c); return; }
    if (c >= XS_UN_SBASE && c < XS_UN_SBASE + XS_UN_SCOUNT) { /* Hangul syllable: L V (T) */
        uint32_t s = c - XS_UN_SBASE;
        xs_un_push(b, XS_UN_LBASE + s / XS_UN_NCOUNT);
        xs_un_push(b, XS_UN_VBASE + (s % XS_UN_NCOUNT) / XS_UN_TCOUNT);
        if (s % XS_UN_TCOUNT) xs_un_push(b, XS_UN_TBASE + s % XS_UN_TCOUNT);
        return;
    }
#ifdef XS_RT_NORMALIZE_K
    if (compat) {
        int k = xs_un_find(xs_un_kd_cp, XS_UN_KD_N, c);
        if (k >= 0) {
            for (int i = xs_un_kd_ix[k]; i < xs_un_kd_ix[k + 1]; i++) xs_un_push(b, xs_un_kd_pool[i]);
            return;
        }
    }
#else
    (void)compat;
#endif
    int k = xs_un_find(xs_un_d_cp, XS_UN_D_N, c);
    if (k < 0) { xs_un_push(b, c); return; }
    for (int i = xs_un_d_ix[k]; i < xs_un_d_ix[k + 1]; i++) xs_un_push(b, xs_un_d_pool[i]);
}
/* the primary composite of a + b, or 0 */
static uint32_t xs_un_compose_pair(uint32_t a, uint32_t b) {
    if (a >= XS_UN_LBASE && a < XS_UN_LBASE + XS_UN_LCOUNT && b >= XS_UN_VBASE && b < XS_UN_VBASE + XS_UN_VCOUNT)
        return XS_UN_SBASE + ((a - XS_UN_LBASE) * XS_UN_VCOUNT + (b - XS_UN_VBASE)) * XS_UN_TCOUNT;
    if (a >= XS_UN_SBASE && a < XS_UN_SBASE + XS_UN_SCOUNT && (a - XS_UN_SBASE) % XS_UN_TCOUNT == 0 && b > XS_UN_TBASE &&
        b < XS_UN_TBASE + XS_UN_TCOUNT)
        return a + (b - XS_UN_TBASE);
    uint64_t key = (uint64_t)a << 21 | b;
    int lo = 0, hi = XS_UN_COMP_N;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (xs_un_comp[m][0] < key) lo = m + 1;
        else hi = m;
    }
    return lo < XS_UN_COMP_N && xs_un_comp[lo][0] == key ? (uint32_t)xs_un_comp[lo][1] : 0;
}
/* s.normalize(form): "NFC" (the default), "NFD", "NFKC" or "NFKD", as Node's (ICU) */
XS_NOINLINE xs_str *xs_str_normalize(xs_str *s, xs_str *form) {
    bool compat, compose;
    const char *f = form ? form->data : "NFC";
    if (!strcmp(f, "NFC")) compat = false, compose = true;
    else if (!strcmp(f, "NFD")) compat = false, compose = false;
    else if (!strcmp(f, "NFKC")) compat = true, compose = true;
    else if (!strcmp(f, "NFKD")) compat = true, compose = false;
    else xs_panic("RangeError: The normalization form should be one of NFC, NFD, NFKC, NFKD.");
#ifndef XS_RT_NORMALIZE_K
    if (compat) xs_panic("internal: NFKC/NFKD tables not linked");
#endif
    const unsigned char *d = (const unsigned char *)s->data;
    int64_t n = s->len, i = 0;
    while (i < n && d[i] < 0x80) i++;
    if (i == n) return xs_retain(s); /* ASCII is in every form already */
    xs_un_buf b = {0};
    for (int64_t k = 0; k < n;) {
        int w;
        uint32_t c = xs_u8_next(d + k, &w);
        k += w;
        size_t at = b.n;
        xs_un_decompose(&b, c, compat);
        /* canonical order: each mark moves before the marks of a higher class ahead of it (stable) */
        for (size_t j = at; j < b.n; j++) {
            int cc = xs_un_ccc_of(b.c[j]);
            if (!cc) continue;
            size_t q = j;
            uint32_t x = b.c[j];
            while (q > 0 && xs_un_ccc_of(b.c[q - 1]) > cc) { b.c[q] = b.c[q - 1]; q--; }
            b.c[q] = x;
        }
    }
    if (compose && b.n > 1) { /* canonical composition (UAX #15's): each mark joins the last starter unless blocked */
        size_t starter = 0, out = 1;
        uint32_t sc = b.c[0];
        int last = xs_un_ccc_of(sc) ? 256 : 0;
        for (size_t j = 1; j < b.n; j++) {
            uint32_t c = b.c[j];
            int cc = xs_un_ccc_of(c);
            uint32_t comp = xs_un_compose_pair(sc, c);
            if (comp && (last < cc || last == 0)) {
                b.c[starter] = sc = comp;
                continue;
            }
            if (cc == 0) { starter = out; sc = c; }
            last = cc;
            b.c[out++] = c;
        }
        b.n = out;
    }
    char *o = malloc(b.n * 4 + 1), *p = o;
    if (!o) xs_panic("out of memory");
    for (size_t j = 0; j < b.n; j++) p += xs_u8_put(p, b.c[j]);
    xs_str *r = xs_str_new(o, (size_t)(p - o));
    free(o);
    free(b.c);
    return r;
}
#endif /* XS_RT_NORMALIZE */
#endif
