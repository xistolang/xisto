/* Generates runtime/xisto_unicode.h: the Unicode data behind String.prototype.normalize and the RegExp property
   escapes (\p{...}), from ICU, the library Node itself uses for both. The ICU linked here must be the version of the
   Node the output is compared with (Node 26: ICU 78, Unicode 17; `node -p process.versions` shows both).

   Build and run (macOS with Homebrew's icu4c@78; any ICU 78 install works the same way):
     ICU=/opt/homebrew/opt/icu4c@78
     cc -O1 -I$ICU/include -o build/gen_unicode tools/gen_unicode.c -L$ICU/lib -licuuc -licudata
     DYLD_LIBRARY_PATH=$ICU/lib build/gen_unicode > runtime/xisto_unicode.h

   The tables are plain sorted arrays (code point ranges, binary search); the header guards each family with the
   macro a program defines only when it uses it (XS_RT_NORMALIZE; XS_RT_NORMALIZE_K for the compatibility forms; XS_RT_UPROPS and one XR_UP_<n> per property), so a
   program that does not normalize text or match property escapes carries none of it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unicode/uchar.h>
#include <unicode/unorm2.h>
#include <unicode/uscript.h>
#include <unicode/uversion.h>

#define MAXCP 0x110000

/* the separator before element i of a list printed k to a line */
#define SEP(i, k) ((i) == 0 ? "\n    " : (i) % (k) ? "," : ",\n    ")

static void die(const char *m) { fprintf(stderr, "gen_unicode: %s\n", m); exit(1); }

/* the code points s (UTF-16) holds */
static int cps(const UChar *s, int n, UChar32 *out) {
    int k = 0;
    for (int i = 0; i < n;) { UChar32 c; U16_NEXT(s, i, n, c); out[k++] = c; }
    return k;
}
static int norm1(const UNormalizer2 *nz, UChar32 c, UChar32 *out) {
    UChar in[2], buf[64];
    int n = 0;
    U16_APPEND_UNSAFE(in, n, c);
    UErrorCode e = U_ZERO_ERROR;
    int m = unorm2_normalize(nz, in, n, buf, 64, &e);
    if (U_FAILURE(e)) die("normalize");
    return cps(buf, m, out);
}

/* a family of ranges printed as {lo, hi} pairs */
static void ranges_out(const char *name, const unsigned char *in) {
    printf("static const uint32_t %s[][2] = {", name);
    int n = 0;
    for (UChar32 c = 0; c < MAXCP;) {
        if (!in[c]) { c++; continue; }
        UChar32 lo = c;
        while (c < MAXCP && in[c]) c++;
        printf("%s{0x%X,0x%X}", SEP(n, 6), lo, c - 1);
        n++;
    }
    if (!n) printf("{1,0}");
    printf("};\n");
}

static void gen_normalize(void) {
    UErrorCode e = U_ZERO_ERROR;
    const UNormalizer2 *nfd = unorm2_getNFDInstance(&e), *nfkd = unorm2_getNFKDInstance(&e), *nfc = unorm2_getNFCInstance(&e);
    if (U_FAILURE(e)) die("instances");
    printf("#ifdef XS_RT_NORMALIZE\n");
    /* canonical combining classes: runs of one class */
    printf("/* canonical combining class: {first, last, class} for every run of code points of a class other than 0 */\n");
    printf("static const uint32_t xs_un_ccc[][3] = {");
    int n = 0;
    for (UChar32 c = 0; c < MAXCP;) {
        int k = u_getCombiningClass(c);
        if (!k) { c++; continue; }
        UChar32 lo = c;
        while (c < MAXCP && u_getCombiningClass(c) == k) c++;
        printf("%s{0x%X,0x%X,%d}", SEP(n, 5), lo, c - 1, k);
        n++;
    }
    printf("};\n");
    /* full decompositions: canonical for every code point that has one; compatibility only where it differs */
    for (int pass = 0; pass < 2; pass++) {
        const UNormalizer2 *nz = pass ? nfkd : nfd;
        static UChar32 pool[200000];
        static UChar32 keys[40000];
        static int offs[40001];
        int np = 0, nk = 0;
        for (UChar32 c = 0; c < MAXCP; c++) {
            if (c >= 0xAC00 && c <= 0xD7A3) continue; /* Hangul syllables: by the algorithm */
            if (c >= 0xD800 && c <= 0xDFFF) continue;
            UChar32 d[64];
            int m = norm1(nz, c, d);
            if (m == 1 && d[0] == c) continue;
            if (pass) { /* only where the compatibility decomposition is not the canonical one */
                UChar32 d2[64];
                int m2 = norm1(nfd, c, d2);
                if (m2 == m && !memcmp(d, d2, sizeof(UChar32) * (size_t)m)) continue;
            }
            keys[nk] = c;
            offs[nk++] = np;
            for (int i = 0; i < m; i++) pool[np++] = d[i];
        }
        offs[nk] = np;
        const char *nm = pass ? "kd" : "d";
        if (pass) printf("#ifdef XS_RT_NORMALIZE_K /* (NFKC, NFKD) */\n");
        printf("/* %s decompositions (full): xs_un_%s_cp[i] decomposes to xs_un_%s_pool[xs_un_%s_ix[i] .. xs_un_%s_ix[i + 1]) */\n",
               pass ? "compatibility" : "canonical", nm, nm, nm, nm);
        printf("#define XS_UN_%s_N %d\n", pass ? "KD" : "D", nk);
        printf("static const uint32_t xs_un_%s_cp[] = {", nm);
        for (int i = 0; i < nk; i++) printf("%s0x%X", SEP(i, 12), keys[i]);
        printf("};\nstatic const uint16_t xs_un_%s_ix[] = {", nm);
        for (int i = 0; i <= nk; i++) printf("%s%d", SEP(i, 16), offs[i]);
        printf("};\nstatic const uint32_t xs_un_%s_pool[] = {", nm);
        for (int i = 0; i < np; i++) printf("%s0x%X", SEP(i, 12), pool[i]);
        printf("};\n");
        if (pass) printf("#endif\n");
        if (np > 65535) die("pool too big for uint16 offsets");
    }
    /* primary composites: the pairs canonical composition joins (Hangul by the algorithm) */
    printf("/* canonical composition: {first << 21 | second, composite}, sorted (primary composites, Hangul excluded) */\n");
    printf("static const uint64_t xs_un_comp[][2] = {");
    n = 0;
    typedef struct { unsigned long long k; UChar32 c; } P;
    static P ps[4000];
    for (UChar32 c = 0; c < MAXCP; c++) {
        if (c >= 0xAC00 && c <= 0xD7A3) continue;
        UChar raw[16];
        UErrorCode e2 = U_ZERO_ERROR;
        int m = unorm2_getRawDecomposition(nfc, c, raw, 16, &e2);
        if (m <= 0 || U_FAILURE(e2)) continue;
        UChar32 d[8];
        int k = cps(raw, m, d);
        if (k != 2) continue;
        if (unorm2_composePair(nfc, d[0], d[1]) != c) continue;
        ps[n].k = ((unsigned long long)d[0] << 21) | (unsigned long long)d[1];
        ps[n++].c = c;
    }
    for (int i = 1; i < n; i++) { P t = ps[i]; int j = i - 1; while (j >= 0 && ps[j].k > t.k) { ps[j + 1] = ps[j]; j--; } ps[j + 1] = t; }
    for (int i = 0; i < n; i++) printf("%s{0x%llXull,0x%X}", SEP(i, 4), ps[i].k, ps[i].c);
    printf("};\n#define XS_UN_COMP_N %d\n", n);
    printf("#endif /* XS_RT_NORMALIZE */\n");
}

int main(void) {
    UVersionInfo uv, iv;
    char us[32], is[32];
    u_getUnicodeVersion(uv);
    u_versionToString(uv, us);
    u_getVersion(iv);
    u_versionToString(iv, is);
    printf("/* Unicode %s data (ICU %s), generated by tools/gen_unicode.c: do not edit. */\n", us, is);
    printf("#ifndef XISTO_UNICODE_H\n#define XISTO_UNICODE_H\n#include <stdint.h>\n");
    gen_normalize();
    printf("#endif\n");
    return 0;
}
