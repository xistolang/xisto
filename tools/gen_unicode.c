/* Generates the Unicode data of the runtime from ICU, the library Node itself uses for both: runtime/xisto_unicode.h
   (String.prototype.normalize) and runtime/xisto_uprops.h (the RegExp property escapes, \p{...}). The ICU linked here
   must be the version of the Node the output is compared with (Node 26: ICU 78, Unicode 17; `node -p process.versions`
   shows both).

   Build and run (macOS with Homebrew's icu4c@78; any ICU 78 install works the same way):
     ICU=/opt/homebrew/opt/icu4c@78
     cc -O1 -I$ICU/include -o build/gen_unicode tools/gen_unicode.c -L$ICU/lib -licuuc -licudata
     DYLD_LIBRARY_PATH=$ICU/lib build/gen_unicode norm > runtime/xisto_unicode.h
     DYLD_LIBRARY_PATH=$ICU/lib build/gen_unicode props > runtime/xisto_uprops.h
   `gen_unicode names` and `gen_unicode dump` list the property names and their ranges, to check them against Node.

   The tables are plain sorted arrays (code point ranges, binary search); the headers guard each family with the
   macro a program defines only when it uses it (XS_RT_NORMALIZE, XS_RT_NORMALIZE_K for the compatibility forms;
   XS_RT_UPROPS and one XR_UP_<n> per property), so a program that does not normalize text or match property escapes
   carries none of it. */
#include <stdbool.h>
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

/* ---- RegExp property escapes ---- */
/* ECMAScript's binary properties (the table of the spec's \p{...}): ICU knows each by its long name, except the three
   the spec defines itself */
static const char *const bin_props[] = {
    "ASCII", "ASCII_Hex_Digit", "Alphabetic", "Any", "Assigned", "Bidi_Control", "Bidi_Mirrored", "Case_Ignorable", "Cased",
    "Changes_When_Casefolded", "Changes_When_Casemapped", "Changes_When_Lowercased", "Changes_When_NFKC_Casefolded",
    "Changes_When_Titlecased", "Changes_When_Uppercased", "Dash", "Default_Ignorable_Code_Point", "Deprecated", "Diacritic",
    "Emoji", "Emoji_Component", "Emoji_Modifier", "Emoji_Modifier_Base", "Emoji_Presentation", "Extended_Pictographic",
    "Extender", "Grapheme_Base", "Grapheme_Extend", "Hex_Digit", "IDS_Binary_Operator", "IDS_Trinary_Operator",
    "ID_Continue", "ID_Start", "Ideographic", "Join_Control", "Logical_Order_Exception", "Lowercase", "Math",
    "Noncharacter_Code_Point", "Pattern_Syntax", "Pattern_White_Space", "Quotation_Mark", "Radical", "Regional_Indicator",
    "Sentence_Terminal", "Soft_Dotted", "Terminal_Punctuation", "Unified_Ideograph", "Uppercase", "Variation_Selector",
    "White_Space", "XID_Continue", "XID_Start", NULL};
static int nprop;
static int list_names; /* (gen_unicode names: the names, one per line, kind and name, for checking them against Node) */
static unsigned char in_set[MAXCP];
/* one property: its ranges (guarded by its own macro) and its names in the lookup table, kind 0 binary, 1 a General_Category
   value, 2 a Script, 3 a Script_Extensions value */
static char entries[200000];
static size_t nentries;
static void prop_out(int kind, const char *const *names) {
    int id = nprop++;
    if (list_names == 1) {
        for (int i = 0; names[i]; i++) printf("%d %s\n", kind, names[i]);
        return;
    }
    if (list_names == 2) { /* (gen_unicode dump: kind, first name, then the ranges, for checking them against Node) */
        printf("%d %s", kind, names[0]);
        for (UChar32 c = 0; c < MAXCP;) {
            if (!in_set[c]) { c++; continue; }
            UChar32 lo = c;
            while (c < MAXCP && in_set[c]) c++;
            printf(" %X-%X", lo, c - 1);
        }
        printf("\n");
        return;
    }
    char nm[32];
    snprintf(nm, sizeof nm, "xr_up_%d", id);
    printf("#if defined(XR_UP_ALL) || defined(XR_UP_%d)\n", id);
    ranges_out(nm, in_set);
    printf("#define XR_UP_%d_R %s, (int)(sizeof %s / sizeof %s[0])\n#else\n#define XR_UP_%d_R NULL, 0\n#endif\n", id, nm, nm, nm, id);
    for (int i = 0; names[i]; i++)
        nentries += (size_t)snprintf(entries + nentries, sizeof entries - nentries, "%s{\"%s\", %d, %d, XR_UP_%d_R}", nentries ? ",\n    " : "    ",
                                     names[i], kind, id, id);
}
/* the names ICU gives a value of property p (short, long, then other aliases) */
static int value_names(UProperty p, int32_t v, const char **out) {
    int n = 0;
    for (int ch = 0; ch < 8; ch++) {
        const char *s = u_getPropertyValueName(p, v, (UPropertyNameChoice)ch);
        if (!s) { if (ch >= 2) break; continue; }
        int dup = 0;
        for (int k = 0; k < n; k++) dup |= !strcmp(out[k], s);
        if (!dup) out[n++] = s;
    }
    out[n] = NULL;
    return n;
}
static void gen_props(void) {
    if (!list_names) {
        printf("#ifdef XS_RT_UPROPS\n");
        printf("/* RegExp property escapes: each property's code points as {first, last} ranges, linked only when a pattern of the\n"
               "   program names it (XR_UP_<n>), or all of them for a pattern known only at run time (XR_UP_ALL) */\n");
    }
    for (int i = 0; bin_props[i]; i++) {
        const char *names[8] = {bin_props[i], NULL};
        memset(in_set, 0, sizeof in_set);
        if (!strcmp(bin_props[i], "ASCII")) memset(in_set, 1, 0x80);
        else if (!strcmp(bin_props[i], "Any")) memset(in_set, 1, MAXCP);
        else if (!strcmp(bin_props[i], "Assigned")) { for (UChar32 c = 0; c < MAXCP; c++) in_set[c] = u_charType(c) != U_UNASSIGNED; }
        else {
            UProperty p = u_getPropertyEnum(bin_props[i]);
            if (p == UCHAR_INVALID_CODE || p >= UCHAR_BINARY_LIMIT) die(bin_props[i]);
            for (UChar32 c = 0; c < MAXCP; c++) in_set[c] = (unsigned char)u_hasBinaryProperty(c, p);
            const char *sn = u_getPropertyName(p, U_SHORT_PROPERTY_NAME);
            if (sn && strcmp(sn, bin_props[i])) names[1] = sn, names[2] = NULL;
            for (int ch = 2, k = names[1] ? 2 : 1; ch < 6; ch++) { /* other aliases (White_Space: space) */
                const char *a = u_getPropertyName(p, (UPropertyNameChoice)ch);
                if (!a) break;
                names[k++] = a;
                names[k] = NULL;
            }
        }
        prop_out(0, names);
    }
    /* General_Category: the 30 values and the groups (L, LC, M, N, P, S, Z, C) */
    static const uint32_t groups[] = {U_GC_L_MASK, U_GC_LC_MASK, U_GC_M_MASK, U_GC_N_MASK, U_GC_P_MASK, U_GC_S_MASK, U_GC_Z_MASK, U_GC_C_MASK};
    for (int g = 0; g < U_CHAR_CATEGORY_COUNT + 8; g++) {
        uint32_t mask = g < U_CHAR_CATEGORY_COUNT ? U_MASK(g) : groups[g - U_CHAR_CATEGORY_COUNT];
        const char *names[8];
        if (!value_names(UCHAR_GENERAL_CATEGORY_MASK, (int32_t)mask, names)) die("gc name");
        for (UChar32 c = 0; c < MAXCP; c++) in_set[c] = (U_GET_GC_MASK(c) & mask) != 0;
        prop_out(1, names);
    }
    /* Script and Script_Extensions: the values with a Unicode name */
    for (int pass = 0; pass < 2; pass++)
        for (int32_t v = 0; v < u_getIntPropertyMaxValue(UCHAR_SCRIPT) + 1; v++) {
            const char *names[8];
            if (!value_names(UCHAR_SCRIPT, v, names)) continue;
            int any = 0; /* (a script of no character, as ISO 15924's that Unicode does not encode or Hrkt: V8 refuses its name) */
            for (UChar32 c = 0; c < MAXCP && !any; c++) { UErrorCode e = U_ZERO_ERROR; any = uscript_getScript(c, &e) == v; }
            if (!any) continue;
            for (UChar32 c = 0; c < MAXCP; c++) {
                UErrorCode e = U_ZERO_ERROR;
                if (!pass) in_set[c] = uscript_getScript(c, &e) == v;
                else in_set[c] = (unsigned char)uscript_hasScript(c, (UScriptCode)v);
            }
            prop_out(pass ? 3 : 2, names);
        }
    if (list_names) return;
    printf("#define XR_UP_COUNT %d\n", nprop);
    printf("/* the names of the properties and of their values (kind 0 a binary property, 1 a General_Category value, 2 a\n"
           "   Script, 3 a Script_Extensions value), as \\p{...} takes them */\n");
    printf("typedef struct { const char *name; uint8_t kind; int16_t id; const uint32_t (*r)[2]; int n; } xr_uprop_t;\n");
    printf("static const xr_uprop_t xr_uprops[] = {\n%s};\n", entries);
    printf("#endif /* XS_RT_UPROPS */\n");
}

int main(int argc, char **argv) {
    const char *what = argc > 1 ? argv[1] : "";
    if (!strcmp(what, "names")) { list_names = 1; gen_props(); return 0; }
    if (!strcmp(what, "dump")) { list_names = 2; gen_props(); return 0; }
    bool norm = !strcmp(what, "norm");
    if (!norm && strcmp(what, "props")) die("usage: gen_unicode norm|props|names|dump");
    UVersionInfo uv, iv;
    char us[32], is[32];
    u_getUnicodeVersion(uv);
    u_versionToString(uv, us);
    u_getVersion(iv);
    u_versionToString(iv, is);
    printf("/* Unicode %s data (ICU %s), generated by tools/gen_unicode.c (%s): do not edit. */\n", us, is, what);
    printf("#ifndef XISTO_%s_H\n#define XISTO_%s_H\n#include <stdint.h>\n", norm ? "UNICODE" : "UPROPS", norm ? "UNICODE" : "UPROPS");
    if (norm) gen_normalize();
    else gen_props();
    printf("#endif\n");
    return 0;
}
