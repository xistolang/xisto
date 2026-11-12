/* RegExp in linear time: JavaScript syntax, no backtracking (so no ReDoS).

   A pattern is parsed to a tree and compiled twice: forward (with an unanchored `.*?` entry) and reversed.
   Searching runs a lazy DFA built from the forward program (leftmost-first, as JS prefers alternatives in
   order) to find where the leftmost match ends, then the reversed DFA backwards from there (longest) to
   find where it starts; only when capture groups are asked for does a Pike VM run, over the match alone.
   test() stops at the first accepting state. A table of the bytes a match can start with lets the forward
   scan skip with memchr while no match is in progress.

   Not supported (they need backtracking): backreferences (\1, \k<name>) and lookaround ((?=, (?!, (?<=,
   (?<!); they are rejected when the pattern is compiled, with that reason. Case folding (flag i) covers
   ASCII, Latin-1, Greek and Cyrillic. Positions are code points, as everywhere in xisto strings.

   This part needs only libc: the compiler includes it (XR_CORE_ONLY) to report pattern errors at compile time. */
#ifndef XISTO_REGEX_H
#define XISTO_REGEX_H
#ifndef XS_NOINLINE /* (the compiler includes this header alone, without the runtime's definition) */
#define XS_NOINLINE __attribute__((noinline)) static
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* SIMD for the Teddy prefilter (fixed-length alternatives): NEON on arm64; SSSE3 on x86-64, checked at run time */
#if !defined(XR_CORE_ONLY) && defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#define XR_TEDDY_NEON 1
#elif !defined(XR_CORE_ONLY) && defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#include <tmmintrin.h>
#define XR_TEDDY_SSSE3 1
#endif

enum { XR_CHAR, XR_CLASS, XR_ANY, XR_ANYNL, XR_SPLIT, XR_JMP, XR_SAVE, XR_ASSERT, XR_MATCH, XR_RESET /* slots x..y back to -1 */ };
enum { XA_BOT, XA_EOT, XA_BOL, XA_EOL, XA_WB, XA_NWB };
enum { XF_G = 1, XF_I = 2, XF_M = 4, XF_S = 8, XF_U = 16, XF_Y = 32, XF_D = 64 };
enum { XC_WORD = 1, XC_NL = 2, XC_EDGE = 4 }; /* the context of a position's neighbour, for assertions */

typedef struct { uint32_t lo, hi; } xr_range;
typedef struct {
    xr_range *r;
    int n;
    uint32_t ascii[4]; /* membership of code points below 128 */
} xr_class;
typedef struct { uint8_t op; int32_t x, y; } xr_inst;

typedef struct {
    xr_inst *ins;
    int n, cap;
} xr_code;

/* the lazy DFA over one program: states are ordered lists of instruction indices plus the previous context */
typedef struct {
    int32_t *pcs;
    int n;
    uint8_t prev;
    bool idle;          /* only the unanchored loop is alive: nothing in progress */
    uint32_t hash;
    int32_t id;
    int32_t trans[129]; /* per ASCII byte and end of text: (next << 1) | matched-before, -1 unknown */
    int32_t fin[8];     /* the last step of a backward search stopped before the text's start, per context of the
                           character before (patterns with ^ $ \b \B): the same encoding, -1 unknown */
    uintptr_t tp[256];  /* the hot loop's table: next state's address | 1 matched-before | 2 next is dead | 4 next is idle;
                           0 unknown (and always for bytes >= 128, which take the slow path) */
} xr_dstate;
typedef struct {
    xr_dstate **st;
    int n, cap;
    int32_t *tab; /* hash -> state index + 1 */
    int tabcap;
    bool longest;
    unsigned epoch; /* bumped when the cache is dropped: transitions computed before are not stored */
    int32_t start[2][8]; /* start state per entry (unanchored, anchored) and previous context, + 1 */
} xr_dfa;

typedef struct xr_re {
    int rc;
    int flags;
    int ngroups;          /* capture groups, without the whole match */
    char **names;         /* per group (1..ngroups): its name, or NULL */
    xr_class *cls;
    int ncls;
    xr_code fwd, rev;
    int entry_u, entry_a; /* forward program: unanchored and anchored entries */
    int entry_r;
    bool anchored_start;  /* begins with ^ (not multiline): only position 0 can match */
    bool nullable;
    bool uses_ctx;        /* has ^ $ \b \B: DFA states depend on the previous character's kind */
    /* prefilter for patterns that begin with C+ followed by a literal byte L not in C (\d+\. \w+@ [a-z]+:):
       memchr finds L, the run of C before it is the only possible start */
    int lit;              /* L, or -1 */
    int lit_min;          /* the + or {n,} minimum of the run */
    uint32_t lit_cls[4];  /* C over ASCII */
    /* the whole pattern is C+ (\s+ \d+ \w+ [,;]+): a match is a maximal run of C, found with a table */
    bool run;
    int run_min;
    int run_cls;          /* the class (for characters beyond ASCII), or -1 for a single char */
    uint8_t run_tab[256]; /* 1: in C; 2: a non-ASCII lead byte, decode and ask the class */
    uint8_t *join;        /* forward program: instructions a jump or split lands on (where paths meet) */
    struct xr_onepass *op; /* captures in one pass, when the next character always decides the way (or NULL) */
    bool op_tried;
    bool ends_eot;        /* every way to a match ends with $ (not multiline): an anchored match ends at the end */
    /* prefilter for patterns whose matches all begin with one of a few K-byte strings (K = 8, 4, 3 or 2), as
       agggtaaa|tttaccct or [cgt]gggtaaa|tttaccc[acg]: each position's next K bytes, case-folded with | 0x20 (the
       pattern's too, so nothing that matches is missed), hashed into a 65536-bit table. It is the DFA's skip:
       when nothing is in progress, the search jumps to the next position whose bit is set. Positions are
       independent, so the scan has no chain from byte to byte, and the DFA still reads each byte once at most */
    int pf_k;             /* K, or 0 */
    uint64_t *pf_bm;      /* the table */
    /* the whole pattern is an alternation of fixed-length sequences of ASCII characters and classes, as
       a[NSt]|BY or tHa[Nt] or agggtaaa|tttaccct (flag i too): a match is found where it starts, with no DFA and no
       backward search for its start. fx_m[i][b]: bit k set when alternative k accepts byte b at offset i, or is
       shorter than i + 1 (already complete there); the lowest alternative still set after its length wins, as
       the first alternative that matches does in JS */
    int fx_n;             /* alternatives (<= 32), 0: not this kind of pattern */
    int fx_max;           /* the longest one's length (<= 16) */
    uint8_t fx_len[32];
    uint32_t (*fx_m)[256];
    /* Teddy (Hyperscan's literal matcher) over the fixed-length alternatives: for each of the first td_k bytes
       (1..4) of a candidate, a byte's low and high nibbles index two 16-byte tables of alternative bits (k % 8),
       and a position stays a candidate while the AND of the lookups is not zero. One table lookup classifies
       16 bytes, so 16 positions are judged at a time; each candidate is then checked with fx_m (the tables
       accept a superset: low and high nibbles are judged apart) */
    int td_k;
    uint8_t td_lo[4][16], td_hi[4][16];
    /* the whole pattern is P C{min,} E, with P a few ASCII bytes and C every character but the ASCII byte E, as
       <[^>]*> or \|[^|][^|]*\| or "[^"]*": the greedy run cannot pass E, so a match is P, then everything up to
       the first E after it (at least min characters), then E. Found with two memchr, no DFA */
    int dl_np;            /* the length of P (1..8), 0: not this kind of pattern */
    unsigned char dl_p[8];
    int dl_min;
    int dl_end;           /* E */
    bool skip;            /* first[] is usable */
    int first_single;     /* the only byte a match can start with, or -1 */
    int first_pair[2];    /* the only two bytes a match can start with ([0] -1 when not two) */
    uint8_t first[256];
    xr_dfa dfwd, drev;
} xr_re;

/* ---------- UTF-8 ---------- */

static uint32_t xr_utf8(const unsigned char *s, size_t n, size_t *len) {
    unsigned c = s[0];
    if (c < 0x80) { *len = 1; return c; }
    int k = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (k == 1 || (size_t)k > n) { *len = 1; return 0xFFFD; }
    uint32_t cp = c & (0xFF >> (k + 1));
    for (int i = 1; i < k; i++) {
        if ((s[i] & 0xC0) != 0x80) { *len = 1; return 0xFFFD; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *len = (size_t)k;
    return cp;
}
static int xr_put_utf8(char *o, uint32_t c) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) { o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 0x3F)); o[2] = (char)(0x80 | (c & 0x3F)); return 3; }
    o[0] = (char)(0xF0 | (c >> 18)); o[1] = (char)(0x80 | ((c >> 12) & 0x3F)); o[2] = (char)(0x80 | ((c >> 6) & 0x3F)); o[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}
static inline int xr_ctx(uint32_t c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') return XC_WORD;
    if (c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029) return XC_NL;
    return 0;
}

/* ---------- character classes ---------- */

typedef struct { xr_range *r; int n, cap; } xr_rset;
static void xr_rs_add(xr_rset *s, uint32_t lo, uint32_t hi) {
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->r = realloc(s->r, sizeof(xr_range) * (size_t)s->cap);
    }
    s->r[s->n].lo = lo;
    s->r[s->n++].hi = hi;
}
static int xr_rcmp(const void *a, const void *b) {
    const xr_range *x = a, *y = b;
    return x->lo < y->lo ? -1 : x->lo > y->lo;
}
static void xr_rs_norm(xr_rset *s) {
    if (s->n < 2) return;
    qsort(s->r, (size_t)s->n, sizeof(xr_range), xr_rcmp);
    int k = 0;
    for (int i = 1; i < s->n; i++) {
        if (s->r[i].lo <= s->r[k].hi + 1) { if (s->r[i].hi > s->r[k].hi) s->r[k].hi = s->r[i].hi; }
        else s->r[++k] = s->r[i];
    }
    s->n = k + 1;
}
static void xr_rs_negate(xr_rset *s) {
    xr_rs_norm(s);
    xr_rset o = {0};
    uint32_t next = 0;
    for (int i = 0; i < s->n; i++) {
        if (s->r[i].lo > next) xr_rs_add(&o, next, s->r[i].lo - 1);
        next = s->r[i].hi + 1;
    }
    if (next <= 0x10FFFF) xr_rs_add(&o, next, 0x10FFFF);
    free(s->r);
    *s = o;
}
/* simple case folding: blocks [lo, hi] <-> [lo + d, hi + d] */
static void xr_rs_fold(xr_rset *s) {
    static const struct { uint32_t lo, hi, d; } blocks[] = {
        {'A', 'Z', 32}, {0xC0, 0xD6, 32}, {0xD8, 0xDE, 32}, {0x391, 0x3A1, 32}, {0x3A3, 0x3AB, 32}, {0x410, 0x42F, 32}, {0x400, 0x40F, 80},
    };
    int n = s->n;
    for (int i = 0; i < n; i++) {
        for (size_t b = 0; b < sizeof blocks / sizeof blocks[0]; b++) {
            uint32_t lo = s->r[i].lo, hi = s->r[i].hi, L = blocks[b].lo, H = blocks[b].hi, d = blocks[b].d;
            uint32_t a = lo > L ? lo : L, z = hi < H ? hi : H;
            if (a <= z) xr_rs_add(s, a + d, z + d);
            a = lo > L + d ? lo : L + d;
            z = hi < H + d ? hi : H + d;
            if (a <= z) xr_rs_add(s, a - d, z - d);
        }
    }
    xr_rs_norm(s);
}
static void xr_rs_digit(xr_rset *s) { xr_rs_add(s, '0', '9'); }
static void xr_rs_word(xr_rset *s) { xr_rs_add(s, '0', '9'); xr_rs_add(s, 'A', 'Z'); xr_rs_add(s, '_', '_'); xr_rs_add(s, 'a', 'z'); }
static void xr_rs_space(xr_rset *s) {
    xr_rs_add(s, 9, 13); xr_rs_add(s, ' ', ' '); xr_rs_add(s, 0xA0, 0xA0); xr_rs_add(s, 0x1680, 0x1680); xr_rs_add(s, 0x2000, 0x200A);
    xr_rs_add(s, 0x2028, 0x2029); xr_rs_add(s, 0x202F, 0x202F); xr_rs_add(s, 0x205F, 0x205F); xr_rs_add(s, 0x3000, 0x3000); xr_rs_add(s, 0xFEFF, 0xFEFF);
}

static inline bool xr_class_has(const xr_class *c, uint32_t cp) {
    if (cp < 128) return (c->ascii[cp >> 5] >> (cp & 31)) & 1;
    int lo = 0, hi = c->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (cp < c->r[m].lo) hi = m - 1;
        else if (cp > c->r[m].hi) lo = m + 1;
        else return true;
    }
    return false;
}

/* ---------- parser: pattern -> tree ---------- */

enum { XN_EMPTY, XN_CHAR, XN_CLASS, XN_ANY, XN_CAT, XN_ALT, XN_REP, XN_GROUP, XN_ASSERT };
typedef struct { uint8_t kind; bool greedy; int32_t v; int min, max; int a, b; } xr_node;

typedef struct {
    const unsigned char *s;
    size_t i, n;
    int flags;
    xr_node *nd;
    int nn, ncap;
    xr_re *re;
    int ccap;
    int gcap;
    const char *err;
} xr_parser;

static int xr_node_new(xr_parser *p, int kind) {
    if (p->nn == p->ncap) {
        p->ncap = p->ncap ? p->ncap * 2 : 64;
        p->nd = realloc(p->nd, sizeof(xr_node) * (size_t)p->ncap);
    }
    memset(&p->nd[p->nn], 0, sizeof(xr_node));
    p->nd[p->nn].kind = (uint8_t)kind;
    p->nd[p->nn].a = p->nd[p->nn].b = -1;
    return p->nn++;
}
static int xr_class_new(xr_parser *p, xr_rset *s) {
    xr_re *re = p->re;
    if (re->ncls == p->ccap) {
        p->ccap = p->ccap ? p->ccap * 2 : 8;
        re->cls = realloc(re->cls, sizeof(xr_class) * (size_t)p->ccap);
    }
    xr_rs_norm(s);
    xr_class *c = &re->cls[re->ncls];
    memset(c, 0, sizeof *c);
    c->r = s->r;
    c->n = s->n;
    for (int i = 0; i < s->n; i++)
        for (uint32_t k = s->r[i].lo; k <= s->r[i].hi && k < 128; k++) c->ascii[k >> 5] |= 1u << (k & 31);
    return re->ncls++;
}
static int xr_fail(xr_parser *p, const char *msg) {
    if (!p->err) p->err = msg;
    return -1;
}
static inline bool xr_end(xr_parser *p) { return p->i >= p->n; }
static inline int xr_peek(xr_parser *p) { return p->i < p->n ? p->s[p->i] : -1; }
static uint32_t xr_next_cp(xr_parser *p) {
    size_t len;
    uint32_t c = xr_utf8(p->s + p->i, p->n - p->i, &len);
    p->i += len;
    return c;
}
static int xr_hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static bool xr_hex(xr_parser *p, int digits, uint32_t *out) {
    uint32_t v = 0;
    for (int k = 0; k < digits; k++) {
        int h = p->i + (size_t)k < p->n ? xr_hexval(p->s[p->i + (size_t)k]) : -1;
        if (h < 0) return false;
        v = v * 16 + (uint32_t)h;
    }
    p->i += (size_t)digits;
    *out = v;
    return true;
}

/* a char or class node (folded under flag i) */
static int xr_char_node(xr_parser *p, uint32_t c) {
    if (p->flags & XF_I) {
        xr_rset s = {0};
        xr_rs_add(&s, c, c);
        xr_rs_fold(&s);
        if (s.n > 1 || s.r[0].lo != s.r[0].hi) {
            int n = xr_node_new(p, XN_CLASS);
            p->nd[n].v = xr_class_new(p, &s);
            return n;
        }
        free(s.r);
    }
    int n = xr_node_new(p, XN_CHAR);
    p->nd[n].v = (int32_t)c;
    return n;
}
static int xr_set_node(xr_parser *p, xr_rset *s) {
    int n = xr_node_new(p, XN_CLASS);
    p->nd[n].v = xr_class_new(p, s);
    return n;
}

/* an escape after '\': a single char (*cp, returns 0), a class (fills *set, returns 1), or an assertion
   (returns 2 with *cp the kind); -1 on error */
static int xr_escape(xr_parser *p, bool in_class, uint32_t *cp, xr_rset *set) {
    if (xr_end(p)) return xr_fail(p, "\\ at end of pattern");
    int c = p->s[p->i];
    switch (c) {
    case 'd': case 'D': p->i++; xr_rs_digit(set); if (c == 'D') xr_rs_negate(set); return 1;
    case 'w': case 'W': p->i++; xr_rs_word(set); if (c == 'W') xr_rs_negate(set); return 1;
    case 's': case 'S': p->i++; xr_rs_space(set); if (c == 'S') xr_rs_negate(set); return 1;
    case 'b':
        p->i++;
        if (in_class) { *cp = 8; return 0; }
        *cp = XA_WB;
        return 2;
    case 'B':
        p->i++;
        if (in_class) return xr_fail(p, "invalid escape \\B in a class");
        *cp = XA_NWB;
        return 2;
    case 'n': p->i++; *cp = '\n'; return 0;
    case 'r': p->i++; *cp = '\r'; return 0;
    case 't': p->i++; *cp = '\t'; return 0;
    case 'v': p->i++; *cp = 11; return 0;
    case 'f': p->i++; *cp = 12; return 0;
    case '0':
        p->i++;
        if (!xr_end(p) && p->s[p->i] >= '0' && p->s[p->i] <= '9') return xr_fail(p, "octal escapes are not supported");
        *cp = 0;
        return 0;
    case 'x':
        p->i++;
        if (!xr_hex(p, 2, cp)) { *cp = 'x'; }
        return 0;
    case 'u':
        p->i++;
        if (xr_peek(p) == '{' && (p->flags & XF_U)) {
            p->i++;
            uint32_t v = 0;
            int nd = 0;
            while (!xr_end(p) && xr_hexval(p->s[p->i]) >= 0) { v = v * 16 + (uint32_t)xr_hexval(p->s[p->i++]); nd++; if (v > 0x10FFFF) return xr_fail(p, "\\u{...} out of range"); }
            if (!nd || xr_peek(p) != '}') return xr_fail(p, "invalid \\u{...} escape");
            p->i++;
            *cp = v;
            return 0;
        }
        if (!xr_hex(p, 4, cp)) { *cp = 'u'; return 0; }
        if (*cp >= 0xD800 && *cp <= 0xDBFF && p->i + 6 <= p->n && p->s[p->i] == '\\' && p->s[p->i + 1] == 'u') { /* a surrogate pair */
            size_t save = p->i;
            uint32_t lo;
            p->i += 2;
            if (xr_hex(p, 4, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) *cp = 0x10000 + ((*cp - 0xD800) << 10) + (lo - 0xDC00);
            else p->i = save;
        }
        return 0;
    case 'c': {
        int l = p->i + 1 < p->n ? p->s[p->i + 1] : 0;
        if ((l >= 'a' && l <= 'z') || (l >= 'A' && l <= 'Z')) { p->i += 2; *cp = (uint32_t)(l % 32); return 0; }
        *cp = '\\'; /* Annex B: \c without a letter is a backslash */
        return 0;
    }
    case 'k':
        if (p->re->names || (p->flags & XF_U)) return xr_fail(p, "backreferences (\\k<name>) are not supported: they need backtracking, and xisto's RegExp runs in linear time");
        p->i++;
        *cp = 'k';
        return 0;
    case 'p': case 'P':
        if (p->flags & XF_U) return xr_fail(p, "Unicode property escapes (\\p{...}) are not supported yet");
        p->i++;
        *cp = (uint32_t)c;
        return 0;
    default:
        if (c >= '1' && c <= '9') {
            if (in_class) return xr_fail(p, "octal escapes are not supported");
            return xr_fail(p, "backreferences (\\1) are not supported: they need backtracking, and xisto's RegExp runs in linear time");
        }
        *cp = xr_next_cp(p); /* identity escape: \. \* \/ ... */
        return 0;
    }
}

static int xr_parse_class(xr_parser *p) { /* after '[' */
    bool neg = false;
    if (xr_peek(p) == '^') { neg = true; p->i++; }
    xr_rset s = {0};
    for (;;) {
        if (xr_end(p)) { free(s.r); return xr_fail(p, "unterminated character class"); }
        if (p->s[p->i] == ']') { p->i++; break; }
        uint32_t lo;
        xr_rset sub = {0};
        int k;
        if (p->s[p->i] == '\\') {
            p->i++;
            k = xr_escape(p, true, &lo, &sub);
            if (k < 0) { free(s.r); free(sub.r); return -1; }
        } else {
            lo = xr_next_cp(p);
            k = 0;
        }
        if (k == 1) { /* \d, \w, \s inside: union; a '-' after it is literal */
            for (int i = 0; i < sub.n; i++) xr_rs_add(&s, sub.r[i].lo, sub.r[i].hi);
            free(sub.r);
            continue;
        }
        if (xr_peek(p) == '-' && p->i + 1 < p->n && p->s[p->i + 1] != ']') {
            size_t save = p->i;
            p->i++;
            uint32_t hi;
            xr_rset sub2 = {0};
            int k2;
            if (p->s[p->i] == '\\') {
                p->i++;
                k2 = xr_escape(p, true, &hi, &sub2);
                if (k2 < 0) { free(s.r); free(sub2.r); return -1; }
            } else {
                hi = xr_next_cp(p);
                k2 = 0;
            }
            if (k2 == 1) { /* [a-\d]: Annex B, '-' is literal */
                xr_rs_add(&s, lo, lo);
                xr_rs_add(&s, '-', '-');
                for (int i = 0; i < sub2.n; i++) xr_rs_add(&s, sub2.r[i].lo, sub2.r[i].hi);
                free(sub2.r);
                continue;
            }
            (void)save;
            if (hi < lo) { free(s.r); return xr_fail(p, "range out of order in character class"); }
            xr_rs_add(&s, lo, hi);
            continue;
        }
        xr_rs_add(&s, lo, lo);
    }
    if (p->flags & XF_I) xr_rs_fold(&s);
    if (neg) xr_rs_negate(&s);
    return xr_set_node(p, &s);
}

static int xr_alt(xr_parser *p);

static bool xr_is_quant_brace(xr_parser *p, size_t at, int *mn, int *mx, size_t *end) { /* {n}, {n,}, {n,m} at s[at] == '{' */
    size_t i = at + 1;
    long a = -1, b = -1;
    if (i >= p->n || p->s[i] < '0' || p->s[i] > '9') return false;
    a = 0;
    while (i < p->n && p->s[i] >= '0' && p->s[i] <= '9') { a = a * 10 + (p->s[i++] - '0'); if (a > 100000) a = 100000; }
    if (i < p->n && p->s[i] == '}') { b = a; }
    else if (i < p->n && p->s[i] == ',') {
        i++;
        if (i < p->n && p->s[i] == '}') b = -1;
        else {
            if (i >= p->n || p->s[i] < '0' || p->s[i] > '9') return false;
            b = 0;
            while (i < p->n && p->s[i] >= '0' && p->s[i] <= '9') { b = b * 10 + (p->s[i++] - '0'); if (b > 100000) b = 100000; }
            if (i >= p->n || p->s[i] != '}') return false;
        }
    } else return false;
    *mn = (int)a;
    *mx = (int)b;
    *end = i + 1;
    return true;
}

XS_NOINLINE int xr_atom(xr_parser *p) {
    int c = p->s[p->i];
    switch (c) {
    case '(': {
        p->i++;
        int group = -1;
        if (xr_peek(p) == '?') {
            int c2 = p->i + 1 < p->n ? p->s[p->i + 1] : -1;
            if (c2 == ':') p->i += 2;
            else if (c2 == '=' || c2 == '!') return xr_fail(p, "lookahead ((?= and (?!) is not supported: it needs backtracking, and xisto's RegExp runs in linear time");
            else if (c2 == '<' && p->i + 2 < p->n && (p->s[p->i + 2] == '=' || p->s[p->i + 2] == '!'))
                return xr_fail(p, "lookbehind ((?<= and (?<!) is not supported: it needs backtracking, and xisto's RegExp runs in linear time");
            else if (c2 == '<') {
                p->i += 2;
                size_t st = p->i;
                while (!xr_end(p) && p->s[p->i] != '>') p->i++;
                if (xr_end(p) || p->i == st) return xr_fail(p, "invalid group name");
                group = ++p->re->ngroups;
                if (group >= p->gcap) { int old = p->gcap; p->gcap = group * 2 + 4; p->re->names = realloc(p->re->names, sizeof(char *) * (size_t)p->gcap); for (int k = old; k < p->gcap; k++) p->re->names[k] = NULL; }
                char *nm = malloc(p->i - st + 1);
                memcpy(nm, p->s + st, p->i - st);
                nm[p->i - st] = 0;
                for (int k = 1; k < group; k++) if (p->re->names[k] && strcmp(p->re->names[k], nm) == 0) { free(nm); return xr_fail(p, "duplicate capture group name"); }
                p->re->names[group] = nm;
                p->i++;
            } else return xr_fail(p, "invalid group");
        } else {
            group = ++p->re->ngroups;
        }
        int in = xr_alt(p);
        if (in < 0) return -1;
        if (xr_peek(p) != ')') return xr_fail(p, "unterminated group");
        p->i++;
        if (group < 0) return in;
        int n = xr_node_new(p, XN_GROUP);
        p->nd[n].v = group;
        p->nd[n].a = in;
        return n;
    }
    case ')': return xr_fail(p, "unmatched ')'");
    case '[': p->i++; return xr_parse_class(p);
    case '.': {
        p->i++;
        int n = xr_node_new(p, XN_ANY);
        p->nd[n].v = (p->flags & XF_S) ? 1 : 0;
        return n;
    }
    case '^': case '$': {
        p->i++;
        int n = xr_node_new(p, XN_ASSERT);
        p->nd[n].v = c == '^' ? ((p->flags & XF_M) ? XA_BOL : XA_BOT) : ((p->flags & XF_M) ? XA_EOL : XA_EOT);
        return n;
    }
    case '\\': {
        p->i++;
        uint32_t cp;
        xr_rset s = {0};
        int k = xr_escape(p, false, &cp, &s);
        if (k < 0) { free(s.r); return -1; }
        if (k == 1) { if (p->flags & XF_I) xr_rs_fold(&s); return xr_set_node(p, &s); }
        if (k == 2) { int n = xr_node_new(p, XN_ASSERT); p->nd[n].v = (int32_t)cp; return n; }
        return xr_char_node(p, cp);
    }
    case '*': case '+': case '?': return xr_fail(p, "nothing to repeat");
    case '{': {
        int mn, mx;
        size_t end;
        if (xr_is_quant_brace(p, p->i, &mn, &mx, &end)) return xr_fail(p, "nothing to repeat");
        p->i++;
        return xr_char_node(p, '{');
    }
    default:
        return xr_char_node(p, xr_next_cp(p));
    }
}

static int xr_quant(xr_parser *p) {
    bool paren = xr_peek(p) == '(';
    int a = xr_atom(p);
    if (a < 0) return -1;
    bool quantified = false;
    for (;;) {
        int c = xr_peek(p), mn, mx;
        size_t end = 0;
        if (c == '*') { mn = 0; mx = -1; p->i++; }
        else if (c == '+') { mn = 1; mx = -1; p->i++; }
        else if (c == '?') { mn = 0; mx = 1; p->i++; }
        else if (c == '{' && xr_is_quant_brace(p, p->i, &mn, &mx, &end)) {
            p->i = end;
            if (mx >= 0 && mx < mn) return xr_fail(p, "numbers out of order in {} quantifier");
        } else return a;
        if ((p->nd[a].kind == XN_ASSERT && !paren) || quantified) return xr_fail(p, "nothing to repeat");
        quantified = true;
        bool greedy = true;
        if (xr_peek(p) == '?') { greedy = false; p->i++; }
        int n = xr_node_new(p, XN_REP);
        p->nd[n].min = mn;
        p->nd[n].max = mx;
        p->nd[n].greedy = greedy;
        p->nd[n].a = a;
        a = n;
    }
}

static int xr_cat(xr_parser *p) {
    int t = -1;
    while (!xr_end(p) && p->s[p->i] != '|' && p->s[p->i] != ')') {
        int a = xr_quant(p);
        if (a < 0) return -1;
        if (t < 0) t = a;
        else {
            int n = xr_node_new(p, XN_CAT);
            p->nd[n].a = t;
            p->nd[n].b = a;
            t = n;
        }
    }
    return t < 0 ? xr_node_new(p, XN_EMPTY) : t;
}

static int xr_alt(xr_parser *p) {
    int t = xr_cat(p);
    if (t < 0) return -1;
    while (xr_peek(p) == '|') {
        p->i++;
        int u = xr_cat(p);
        if (u < 0) return -1;
        int n = xr_node_new(p, XN_ALT);
        p->nd[n].a = t;
        p->nd[n].b = u;
        t = n;
    }
    return t;
}

/* ---------- empty iterations ----------
   In JS an iteration of a quantifier beyond its minimum may not match the empty string (it fails and the
   engine backtracks into the iteration's other choices). Optional iterations of a body that can match
   empty compile from ne(body): the same choices in the same order, without the empty ones. */
static bool xr_nullable(xr_parser *p, int ni) {
    xr_node *n = &p->nd[ni];
    switch (n->kind) {
    case XN_EMPTY: case XN_ASSERT: return true;
    case XN_CHAR: case XN_CLASS: case XN_ANY: return false;
    case XN_CAT: return xr_nullable(p, n->a) && xr_nullable(p, p->nd[ni].b);
    case XN_ALT: return xr_nullable(p, n->a) || xr_nullable(p, p->nd[ni].b);
    case XN_GROUP: return xr_nullable(p, n->a);
    case XN_REP: return n->min == 0 || xr_nullable(p, n->a);
    }
    return true;
}
/* is the empty way of a nullable node its preferred choice (else: its last)? */
static bool xr_empty_first(xr_parser *p, int ni) {
    xr_node *n = &p->nd[ni];
    switch (n->kind) {
    case XN_EMPTY: case XN_ASSERT: return true;
    case XN_CAT: return xr_empty_first(p, n->a) && xr_empty_first(p, p->nd[ni].b);
    case XN_ALT: return xr_nullable(p, n->a) && xr_empty_first(p, n->a);
    case XN_GROUP: return xr_empty_first(p, n->a);
    case XN_REP: return n->min == 0 ? !n->greedy || xr_empty_first(p, n->a) : xr_empty_first(p, n->a);
    }
    return false;
}
static int xr_mk(xr_parser *p, int kind, int a, int b) {
    int n = xr_node_new(p, kind);
    p->nd[n].a = a;
    p->nd[n].b = b;
    return n;
}
static int xr_fail_node(xr_parser *p) {
    xr_rset none = {0};
    return xr_set_node(p, &none);
}
/* the node's ways of matching empty (keeping its assertions and groups) */
static int xr_eps(xr_parser *p, int ni) {
    xr_node n = p->nd[ni];
    switch (n.kind) {
    case XN_EMPTY: case XN_ASSERT: return ni;
    case XN_CHAR: case XN_CLASS: case XN_ANY: return xr_fail_node(p);
    case XN_CAT: { int a = xr_eps(p, n.a); int b = xr_eps(p, n.b); return xr_mk(p, XN_CAT, a, b); }
    case XN_ALT: { int a = xr_eps(p, n.a); int b = xr_eps(p, n.b); return xr_mk(p, XN_ALT, a, b); }
    case XN_GROUP: { int a = xr_eps(p, n.a); int g = xr_mk(p, XN_GROUP, a, -1); p->nd[g].v = n.v; return g; }
    case XN_REP: {
        if (n.min == 0) return xr_node_new(p, XN_EMPTY);
        int e = xr_eps(p, n.a), t = e;
        for (int k = 1; k < n.min && k < 4; k++) t = xr_mk(p, XN_CAT, t, e);
        return t;
    }
    }
    return ni;
}
static int xr_ne(xr_parser *p, int ni) {
    if (!xr_nullable(p, ni)) return ni;
    if (p->nn > 50000) { xr_fail(p, "pattern too large"); return ni; }
    xr_node n = p->nd[ni];
    switch (n.kind) {
    case XN_EMPTY: case XN_ASSERT: return xr_fail_node(p);
    case XN_CAT: {
        if (!xr_nullable(p, n.a)) return ni;
        bool ef = xr_empty_first(p, n.a);
        int x = xr_mk(p, XN_CAT, xr_ne(p, n.a), n.b);
        int ea = xr_eps(p, n.a);
        int y = xr_mk(p, XN_CAT, ea, xr_ne(p, n.b));
        return ef ? xr_mk(p, XN_ALT, y, x) : xr_mk(p, XN_ALT, x, y);
    }
    case XN_ALT: { int a = xr_ne(p, n.a); int b = xr_ne(p, n.b); return xr_mk(p, XN_ALT, a, b); }
    case XN_GROUP: { int a = xr_ne(p, n.a); int g = xr_mk(p, XN_GROUP, a, -1); p->nd[g].v = n.v; return g; }
    case XN_REP: { /* at least one iteration, and the first one not empty */
        if (n.min > 0) { /* mandatory iterations may be empty: only the all-empty way goes */
            int rest = xr_node_new(p, XN_REP);
            p->nd[rest] = n;
            p->nd[rest].min = n.min - 1;
            p->nd[rest].max = n.max < 0 ? -1 : n.max - 1;
            int x = xr_mk(p, XN_CAT, xr_ne(p, n.a), rest);
            int y = xr_mk(p, XN_CAT, xr_eps(p, n.a), xr_ne(p, rest));
            return xr_empty_first(p, n.a) ? xr_mk(p, XN_ALT, y, x) : xr_mk(p, XN_ALT, x, y);
        }
        if (n.max == 0) return xr_fail_node(p);
        int rest = xr_node_new(p, XN_REP);
        p->nd[rest] = n;
        p->nd[rest].max = n.max < 0 ? -1 : n.max - 1;
        return xr_mk(p, XN_CAT, xr_ne(p, n.a), rest);
    }
    }
    return ni;
}

/* ---------- compiler: tree -> program ---------- */

#define XR_MAX_INST 200000

static int xr_emit(xr_code *c, int op, int x, int y) {
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 64;
        c->ins = realloc(c->ins, sizeof(xr_inst) * (size_t)c->cap);
    }
    c->ins[c->n].op = (uint8_t)op;
    c->ins[c->n].x = x;
    c->ins[c->n].y = y;
    return c->n++;
}

/* the capture groups inside a node: lowest and highest index (0 when none) */
static void xr_groups_in(xr_parser *p, int ni, int *lo, int *hi) {
    if (ni < 0) return;
    xr_node *n = &p->nd[ni];
    if (n->kind == XN_GROUP) { if (!*lo || n->v < *lo) *lo = n->v; if (n->v > *hi) *hi = n->v; }
    xr_groups_in(p, n->a, lo, hi);
    xr_groups_in(p, n->b, lo, hi);
}
static bool xr_comp(xr_parser *p, xr_code *c, int ni, bool rev);
/* one iteration of a quantified body: as in JS, the groups inside start each iteration unset */
static bool xr_comp_iter(xr_parser *p, xr_code *c, int ni, bool rev) {
    int lo = 0, hi = 0;
    if (!rev) xr_groups_in(p, ni, &lo, &hi);
    if (lo) xr_emit(c, XR_RESET, 2 * lo, 2 * hi + 1);
    return xr_comp(p, c, ni, rev);
}
static bool xr_comp(xr_parser *p, xr_code *c, int ni, bool rev) {
    if (c->n > XR_MAX_INST) { xr_fail(p, "pattern too large (repetition counts expand it)"); return false; }
    xr_node *n = &p->nd[ni];
    switch (n->kind) {
    case XN_EMPTY: return true;
    case XN_CHAR: xr_emit(c, XR_CHAR, n->v, 0); return true;
    case XN_CLASS: xr_emit(c, XR_CLASS, n->v, 0); return true;
    case XN_ANY: xr_emit(c, n->v ? XR_ANY : XR_ANYNL, 0, 0); return true;
    case XN_ASSERT: {
        int k = n->v;
        if (rev) k = k == XA_BOT ? XA_EOT : k == XA_EOT ? XA_BOT : k == XA_BOL ? XA_EOL : k == XA_EOL ? XA_BOL : k;
        xr_emit(c, XR_ASSERT, k, 0);
        return true;
    }
    case XN_CAT: {
        int a = n->a, b = n->b;
        return rev ? xr_comp(p, c, b, rev) && xr_comp(p, c, a, rev) : xr_comp(p, c, a, rev) && xr_comp(p, c, b, rev);
    }
    case XN_ALT: {
        int a = n->a, b = n->b;
        int sp = xr_emit(c, XR_SPLIT, 0, 0);
        c->ins[sp].x = c->n;
        if (!xr_comp(p, c, a, rev)) return false;
        int j = xr_emit(c, XR_JMP, 0, 0);
        c->ins[sp].y = c->n;
        if (!xr_comp(p, c, b, rev)) return false;
        c->ins[j].x = c->n;
        return true;
    }
    case XN_GROUP: {
        int v = n->v, a = n->a;
        if (rev) return xr_comp(p, c, a, rev);
        xr_emit(c, XR_SAVE, 2 * v, 0);
        if (!xr_comp(p, c, a, rev)) return false;
        xr_emit(c, XR_SAVE, 2 * v + 1, 0);
        return true;
    }
    case XN_REP: {
        int mn = n->min, mx = n->max, a = n->a;
        bool greedy = n->greedy;
        for (int k = 0; k < mn; k++) if (!xr_comp_iter(p, c, a, rev)) return false;
        if (xr_nullable(p, a)) { a = xr_ne(p, a); if (p->err) return false; }
        if (mx < 0) { /* L: split body, out; body; jmp L */
            int sp = xr_emit(c, XR_SPLIT, 0, 0);
            int body = c->n;
            if (!xr_comp_iter(p, c, a, rev)) return false;
            xr_emit(c, XR_JMP, sp, 0);
            c->ins[sp].x = greedy ? body : c->n;
            c->ins[sp].y = greedy ? c->n : body;
            return true;
        }
        int pending[256], np = 0;
        int *pend = mx - mn > 256 ? malloc(sizeof(int) * (size_t)(mx - mn)) : pending;
        for (int k = mn; k < mx; k++) {
            int sp = xr_emit(c, XR_SPLIT, 0, 0);
            pend[np++] = sp;
            c->ins[sp].x = c->n; /* body (greedy: x is taken first) */
            if (!xr_comp_iter(p, c, a, rev)) { if (pend != pending) free(pend); return false; }
        }
        for (int k = 0; k < np; k++) {
            int sp = pend[k], body = c->ins[sp].x;
            c->ins[sp].x = greedy ? body : c->n;
            c->ins[sp].y = greedy ? c->n : body;
        }
        if (pend != pending) free(pend);
        return true;
    }
    }
    return true;
}

/* can the node match the empty string? adds to first[] the bytes its first character can start with */
static bool xr_first(xr_parser *p, int ni, uint8_t *first, bool *all) {
    xr_node *n = &p->nd[ni];
    switch (n->kind) {
    case XN_EMPTY: case XN_ASSERT: return true;
    case XN_CHAR: {
        char b[4];
        xr_put_utf8(b, (uint32_t)n->v);
        first[(unsigned char)b[0]] = 1;
        return false;
    }
    case XN_CLASS: {
        xr_class *c = &p->re->cls[n->v];
        for (int i = 0; i < c->n; i++) {
            for (uint32_t k = c->r[i].lo; k <= c->r[i].hi && k < 128; k++) first[k] = 1;
            if (c->r[i].hi >= 128) for (int k = 0xC0; k < 0xF8; k++) first[k] = 1;
        }
        return false;
    }
    case XN_ANY: *all = true; return false;
    case XN_CAT: return xr_first(p, n->a, first, all) && xr_first(p, n->b, first, all);
    case XN_ALT: { bool x = xr_first(p, n->a, first, all); bool y = xr_first(p, n->b, first, all); return x || y; }
    case XN_GROUP: return xr_first(p, n->a, first, all);
    case XN_REP: { bool x = xr_first(p, n->a, first, all); return n->min == 0 || x; }
    }
    return true;
}
static bool xr_starts_bot(xr_parser *p, int ni) {
    xr_node *n = &p->nd[ni];
    if (n->kind == XN_ASSERT) return n->v == XA_BOT;
    if (n->kind == XN_CAT) return xr_starts_bot(p, n->a);
    if (n->kind == XN_GROUP) return xr_starts_bot(p, n->a);
    return false;
}

#ifndef XR_CORE_ONLY
static void xr_dfa_free(xr_dfa *d);
#endif
struct xr_onepass;
static void xr_onepass_free(struct xr_onepass *op);
static void xr_free(xr_re *re) {
    if (!re) return;
    for (int i = 0; i < re->ncls; i++) free(re->cls[i].r);
    free(re->cls);
    if (re->names) for (int i = 0; i <= re->ngroups; i++) free(re->names[i]);
    free(re->names);
    free(re->fwd.ins);
    free(re->rev.ins);
    free(re->join);
    free(re->pf_bm);
    free(re->fx_m);
#ifndef XR_CORE_ONLY
    xr_onepass_free(re->op);
#endif
#ifndef XR_CORE_ONLY
    xr_dfa_free(&re->dfwd);
    xr_dfa_free(&re->drev);
#endif
    free(re);
}

static int xr_parse_flags(const char *f, size_t n, const char **err) {
    int fl = 0;
    for (size_t i = 0; i < n; i++) {
        int b = f[i] == 'g' ? XF_G : f[i] == 'i' ? XF_I : f[i] == 'm' ? XF_M : f[i] == 's' ? XF_S : f[i] == 'u' ? XF_U : f[i] == 'y' ? XF_Y : f[i] == 'd' ? XF_D : f[i] == 'v' ? XF_U : 0;
        if (!b || (fl & b)) { *err = "invalid regular expression flags"; return -1; }
        fl |= b;
    }
    return fl;
}

/* compiles a pattern; NULL with *err set when it is not valid (or uses what needs backtracking) */
/* ---- the K-byte prefix filter (xr_re.pf_k): the strings every match starts with ---- */
typedef struct { uint8_t b[8]; uint8_t len; } xr_pfs;
#define XR_PF_CAP 64
/* in: strings built so far (those of length K are done); out: each extended by what node k matches first.
   Returns the count, or -1 when the node cannot be expanded into few enough fixed strings */
static int xr_pf_expand(xr_re *re, xr_parser *p, int k, const xr_pfs *in, int nin, xr_pfs *out, int K) {
    int nout = 0, open = 0;
    for (int i = 0; i < nin; i++) if (in[i].len < K) open++;
    if (!open) { memcpy(out, in, sizeof *in * (size_t)nin); return nin; }
    xr_node *nd = &p->nd[k];
    switch (nd->kind) {
    case XN_GROUP: return xr_pf_expand(re, p, nd->a, in, nin, out, K);
    case XN_CAT: {
        xr_pfs mid[XR_PF_CAP];
        int m = xr_pf_expand(re, p, nd->a, in, nin, mid, K);
        return m < 0 ? -1 : xr_pf_expand(re, p, nd->b, mid, m, out, K);
    }
    case XN_ALT: {
        int a = xr_pf_expand(re, p, nd->a, in, nin, out, K);
        if (a < 0) return -1;
        xr_pfs right[XR_PF_CAP];
        int b = xr_pf_expand(re, p, nd->b, in, nin, right, K);
        if (b < 0 || a + b > XR_PF_CAP) return -1;
        memcpy(out + a, right, sizeof *right * (size_t)b);
        return a + b;
    }
    case XN_CHAR: case XN_CLASS: {
        uint8_t bytes[128];
        int nb = 0;
        if (nd->kind == XN_CHAR) {
            if (nd->v >= 128) return -1;
            bytes[nb++] = (uint8_t)(nd->v | 0x20);
        } else {
            xr_class *cl = &re->cls[nd->v];
            for (int i = 0; i < cl->n; i++) if (cl->r[i].hi >= 128) return -1; /* (a UTF-8 lead byte could start a match) */
            bool seen[256] = {0};
            for (int c = 0; c < 128; c++)
                if (((cl->ascii[c >> 5] >> (c & 31)) & 1) && !seen[c | 0x20]) { seen[c | 0x20] = true; bytes[nb++] = (uint8_t)(c | 0x20); }
        }
        for (int i = 0; i < nin; i++) {
            if (in[i].len == K) { if (nout == XR_PF_CAP) return -1; out[nout++] = in[i]; continue; }
            for (int j = 0; j < nb; j++) {
                if (nout == XR_PF_CAP) return -1;
                out[nout] = in[i];
                out[nout].b[out[nout].len++] = bytes[j];
                nout++;
            }
        }
        return nout;
    }
    default: return -1; /* repetitions, ., assertions, the empty string: no fixed prefix here */
    }
}
static uint32_t xr_pf_hash(uint64_t x) { return (uint32_t)((x * 0x9E3779B97F4A7C15ULL) >> 48); }
static void xr_pf_build(xr_re *re, xr_parser *p, int root) {
    static const int Ks[] = {8, 4, 3, 2};
    for (int ki = 0; ki < 4; ki++) {
        int K = Ks[ki];
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
        if (K < 4) continue; /* (the scan masks the first K bytes as the low ones) */
#endif
        xr_pfs seed = {{0}, 0}, out[XR_PF_CAP];
        int n = xr_pf_expand(re, p, root, &seed, 1, out, K);
        if (n <= 0) continue;
        bool full = true;
        for (int i = 0; i < n; i++) if (out[i].len != K) full = false;
        if (!full) continue;
        re->pf_bm = calloc(1024, sizeof(uint64_t));
        for (int i = 0; i < n; i++) {
            uint64_t x = 0;
            memcpy(&x, out[i].b, (size_t)K);
            uint32_t h = xr_pf_hash(x);
            re->pf_bm[h >> 6] |= (uint64_t)1 << (h & 63);
        }
        re->pf_k = K;
        return;
    }
}

/* the alternatives of node k, left to right, into alts (fixed-length: xr_fx_build); false if more than 32 */
static bool xr_fx_alts(xr_parser *p, int k, int *alts, int *na) {
    if (p->nd[k].kind == XN_ALT) return xr_fx_alts(p, p->nd[k].a, alts, na) && xr_fx_alts(p, p->nd[k].b, alts, na);
    if (*na == 32) return false;
    alts[(*na)++] = k;
    return true;
}
/* the items of a concatenation, in order, each an ASCII char or class; -1 if it has anything else */
static int xr_fx_items(xr_re *re, xr_parser *p, int k, int *items, int ni) {
    xr_node *nd = &p->nd[k];
    if (nd->kind == XN_CAT) {
        ni = xr_fx_items(re, p, nd->a, items, ni);
        return ni < 0 ? -1 : xr_fx_items(re, p, nd->b, items, ni);
    }
    if (ni == 16) return -1;
    if (nd->kind == XN_CHAR) { if (nd->v >= 128) return -1; }
    else if (nd->kind == XN_CLASS) {
        xr_class *cl = &re->cls[nd->v];
        for (int i = 0; i < cl->n; i++) if (cl->r[i].hi >= 128) return -1; /* ([^x] and the like reach beyond ASCII) */
    } else return -1;
    items[ni++] = k;
    return ni;
}
static void xr_fx_build(xr_re *re, xr_parser *p, int root) {
    if (re->ngroups) return; /* (the groups would need the general search) */
    int alts[32], na = 0;
    if (!xr_fx_alts(p, root, alts, &na)) return;
    int items[32][16], len[32], mx = 0;
    for (int a = 0; a < na; a++) {
        len[a] = xr_fx_items(re, p, alts[a], items[a], 0);
        if (len[a] <= 0) return;
        if (len[a] > mx) mx = len[a];
    }
    re->fx_m = calloc((size_t)mx, sizeof *re->fx_m);
    for (int a = 0; a < na; a++) {
        re->fx_len[a] = (uint8_t)len[a];
        for (int i = 0; i < mx; i++) {
            uint32_t bit = 1u << a;
            if (i >= len[a]) { for (int b = 0; b < 256; b++) re->fx_m[i][b] |= bit; continue; }
            xr_node *nd = &p->nd[items[a][i]];
            if (nd->kind == XN_CHAR) re->fx_m[i][nd->v] |= bit;
            else {
                xr_class *cl = &re->cls[nd->v];
                for (int b = 0; b < 128; b++) if ((cl->ascii[b >> 5] >> (b & 31)) & 1) re->fx_m[i][b] |= bit;
            }
        }
    }
    re->fx_n = na;
    re->fx_max = mx;
    int mn = 16;
    for (int a = 0; a < na; a++) if (len[a] < mn) mn = len[a];
    re->td_k = mn < 4 ? mn : 4;
    for (int a = 0; a < na; a++)
        for (int i = 0; i < re->td_k; i++)
            for (int b = 0; b < 128; b++)
                if (re->fx_m[i][b] & (1u << a)) {
                    re->td_lo[i][b & 15] |= (uint8_t)(1u << (a & 7));
                    re->td_hi[i][b >> 4] |= (uint8_t)(1u << (a & 7));
                }
}

/* a class that is every character but the ASCII byte e */
static bool xr_cls_all_but(const xr_class *c, int e) {
    if (e <= 0 || e >= 128 || c->n != 2) return false;
    return c->r[0].lo == 0 && c->r[0].hi == (uint32_t)e - 1 && c->r[1].lo == (uint32_t)e + 1 && c->r[1].hi == 0x10FFFF;
}
static void xr_dl_build(xr_re *re, xr_parser *p, int root) {
    if (re->ngroups || (re->flags & XF_I)) return;
    int items[24], ni = 0, stack[64], sp = 0;
    stack[sp++] = root;
    while (sp) { /* the concatenation's items, in order */
        int k = stack[--sp];
        xr_node *nd = &p->nd[k];
        if (nd->kind == XN_CAT) { if (sp + 2 > 64) return; stack[sp++] = nd->b; stack[sp++] = nd->a; }
        else { if (ni == 24) return; items[ni++] = k; }
    }
    int np = 0;
    unsigned char pre[8];
    while (np < ni && np < 8 && p->nd[items[np]].kind == XN_CHAR && p->nd[items[np]].v < 128) { pre[np] = (unsigned char)p->nd[items[np]].v; np++; }
    if (np == 0 || ni < np + 2) return;
    xr_node *last = &p->nd[items[ni - 1]];
    if (last->kind != XN_CHAR || last->v <= 0 || last->v >= 128) return;
    int e = last->v, min = 0;
    bool star = false;
    for (int k = np; k < ni - 1; k++) {
        xr_node *nd = &p->nd[items[k]];
        if (star) return; /* (only the last item before E may repeat) */
        if (nd->kind == XN_CLASS && xr_cls_all_but(&re->cls[nd->v], e)) { min++; continue; }
        if (nd->kind == XN_REP && nd->max < 0 && nd->a >= 0 && p->nd[nd->a].kind == XN_CLASS && xr_cls_all_but(&re->cls[p->nd[nd->a].v], e)) {
            min += nd->min; star = true; continue; /* (greedy or lazy: the run stops at the first E either way) */
        }
        return;
    }
    if (!star || min > 16) return;
    memcpy(re->dl_p, pre, (size_t)np);
    re->dl_np = np;
    re->dl_min = min;
    re->dl_end = e;
}

static xr_re *xr_compile(const char *pat, size_t n, int flags, const char **err) {
    xr_re *re = calloc(1, sizeof *re);
    re->rc = 1;
    re->flags = flags;
    xr_parser p = {(const unsigned char *)pat, 0, n, flags, NULL, 0, 0, re, 0, 0, NULL};
    int root = xr_alt(&p);
    if (root >= 0 && !xr_end(&p)) xr_fail(&p, "unmatched ')'");
    if (p.err) { *err = p.err; free(p.nd); xr_free(re); return NULL; }
    if (re->names) { /* one slot per group, NULL for the unnamed */
        re->names = realloc(re->names, sizeof(char *) * (size_t)(re->ngroups + 1));
        for (int k = p.gcap; k <= re->ngroups; k++) re->names[k] = NULL;
    }
    /* forward: u: split a, u+1; any; jmp u; a: save 0; body; save 1; match */
    xr_code *c = &re->fwd;
    re->entry_u = xr_emit(c, XR_SPLIT, 0, 0);
    xr_emit(c, XR_ANY, 0, 0);
    xr_emit(c, XR_JMP, re->entry_u, 0);
    re->entry_a = c->n;
    c->ins[re->entry_u].x = re->entry_a;
    c->ins[re->entry_u].y = re->entry_u + 1;
    xr_emit(c, XR_SAVE, 0, 0);
    bool ok = xr_comp(&p, c, root, false);
    xr_emit(c, XR_SAVE, 1, 0);
    xr_emit(c, XR_MATCH, 0, 0);
    re->entry_r = 0;
    ok = ok && xr_comp(&p, &re->rev, root, true);
    xr_emit(&re->rev, XR_MATCH, 0, 0);
    if (!ok) { *err = p.err; free(p.nd); xr_free(re); return NULL; }
    bool all = false;
    re->nullable = xr_first(&p, root, re->first, &all);
    re->skip = !re->nullable && !all;
    re->first_single = -1;
    re->first_pair[0] = -1;
    if (re->skip) {
        int cnt = 0, last = -1;
        for (int k = 0; k < 256; k++) if (re->first[k]) { cnt++; last = k; }
        if (cnt == 1) re->first_single = last;
        if (cnt == 2) { re->first_pair[1] = last; for (int k = 0; k < 256; k++) if (re->first[k]) { re->first_pair[0] = k; break; } }
        if (cnt > 200) re->skip = false;
    }
    re->anchored_start = xr_starts_bot(&p, root);
    re->lit = -1;
    { /* the whole pattern C+ */
        int k = root;
        while (p.nd[k].kind == XN_GROUP) k = p.nd[k].a;
        xr_node *a = &p.nd[k];
        int body = a->kind == XN_REP ? a->a : -1;
        while (body >= 0 && p.nd[body].kind == XN_GROUP) body = p.nd[body].a;
        if (a->kind == XN_REP && a->min >= 1 && a->max < 0 && a->greedy && body >= 0 && (p.nd[body].kind == XN_CLASS || p.nd[body].kind == XN_CHAR)) {
            bool ok = true;
            memset(re->run_tab, 0, sizeof re->run_tab);
            re->run_cls = -1;
            if (p.nd[body].kind == XN_CHAR) { if (p.nd[body].v < 128) re->run_tab[p.nd[body].v] = 1; else ok = false; }
            else {
                xr_class *cl = &re->cls[p.nd[body].v];
                for (int i = 0; i < 128; i++) if ((cl->ascii[i >> 5] >> (i & 31)) & 1) re->run_tab[i] = 1;
                bool wide = false;
                for (int i = 0; i < cl->n; i++) if (cl->r[i].hi >= 128) wide = true;
                if (wide) { re->run_cls = p.nd[body].v; for (int b = 0xC0; b < 0xF8; b++) re->run_tab[b] = 2; }
            }
            if (ok) { re->run = true; re->run_min = a->min; }
        }
    }
    { /* the leading C+ L of the pattern */
        int items[3], ni = 0, stack[64], sp = 0;
        stack[sp++] = root;
        while (sp && ni < 3) { /* leftmost items of the concatenation, in order */
            int k = stack[--sp];
            xr_node *nd = &p.nd[k];
            if (nd->kind == XN_CAT) { if (sp + 2 > 64) break; stack[sp++] = nd->b; stack[sp++] = nd->a; }
            else if (nd->kind == XN_GROUP) { stack[sp++] = nd->a; }
            else items[ni++] = k;
        }
        if (ni >= 2) {
            xr_node *a = &p.nd[items[0]], *b = &p.nd[items[1]];
            int body = a->kind == XN_REP ? a->a : -1;
            while (body >= 0 && p.nd[body].kind == XN_GROUP) body = p.nd[body].a;
            if (a->kind == XN_REP && a->min >= 1 && a->max < 0 && body >= 0 && b->kind == XN_CHAR && b->v < 128 &&
                (p.nd[body].kind == XN_CLASS || p.nd[body].kind == XN_CHAR)) {
                uint32_t bm[4] = {0};
                bool ascii_only = true;
                if (p.nd[body].kind == XN_CHAR) {
                    uint32_t c0 = (uint32_t)p.nd[body].v;
                    if (c0 < 128) bm[c0 >> 5] |= 1u << (c0 & 31); else ascii_only = false;
                } else {
                    xr_class *cl = &re->cls[p.nd[body].v];
                    memcpy(bm, cl->ascii, sizeof bm);
                    for (int i = 0; i < cl->n; i++) if (cl->r[i].hi >= 128) ascii_only = false;
                }
                int L = b->v;
                if (ascii_only && !((bm[L >> 5] >> (L & 31)) & 1)) {
                    re->lit = L;
                    re->lit_min = a->min;
                    memcpy(re->lit_cls, bm, sizeof bm);
                }
            }
        }
    }
    for (int k = 0; k < c->n; k++) if (c->ins[k].op == XR_ASSERT) re->uses_ctx = true;
    re->join = calloc((size_t)c->n + 1, 1);
    for (int k = 0; k < c->n; k++) {
        if (c->ins[k].op == XR_JMP) re->join[c->ins[k].x] = 1;
        if (c->ins[k].op == XR_SPLIT) { re->join[c->ins[k].x] = 1; re->join[c->ins[k].y] = 1; }
    }
    if (!re->run && re->lit < 0 && !re->anchored_start) xr_pf_build(re, &p, root);
    if (!re->run && re->lit < 0 && !re->anchored_start && !re->nullable) xr_fx_build(re, &p, root);
    if (!re->run && re->lit < 0 && !re->anchored_start && !re->fx_n) xr_dl_build(re, &p, root);
    re->drev.longest = true;
    free(p.nd);
    return re;
}

#ifndef XR_CORE_ONLY

/* ---------- lazy DFA ---------- */

#define XR_DFA_MAX 4096

static void xr_dfa_free(xr_dfa *d) {
    for (int i = 0; i < d->n; i++) { free(d->st[i]->pcs); free(d->st[i]); }
    free(d->st);
    free(d->tab);
    d->st = NULL;
    d->tab = NULL;
    d->n = d->cap = d->tabcap = 0;
    d->epoch++;
    memset(d->start, 0, sizeof d->start);
}
static uint32_t xr_hash(const int32_t *pcs, int n, int prev) {
    uint32_t h = 2166136261u ^ (uint32_t)prev;
    for (int i = 0; i < n; i++) { h ^= (uint32_t)pcs[i]; h *= 16777619u; }
    return h ? h : 1;
}
static int xr_dfa_find(xr_dfa *d, const int32_t *pcs, int n, int prev, uint32_t h) {
    if (!d->tabcap) return -1;
    for (uint32_t i = h & (uint32_t)(d->tabcap - 1);; i = (i + 1) & (uint32_t)(d->tabcap - 1)) {
        int32_t s = d->tab[i];
        if (!s) return -1;
        xr_dstate *st = d->st[s - 1];
        if (st->hash == h && st->n == n && st->prev == prev && memcmp(st->pcs, pcs, sizeof(int32_t) * (size_t)n) == 0) return s - 1;
    }
}
static int xr_dfa_add(xr_dfa *d, const int32_t *pcs, int n, int prev, int loop_pc) {
    uint32_t h = xr_hash(pcs, n, prev);
    int f = xr_dfa_find(d, pcs, n, prev, h);
    if (f >= 0) return f;
    if (d->n >= XR_DFA_MAX) { /* the cache is full: start over (states in use are rebuilt on demand) */
        xr_dfa_free(d);
    }
    if (d->n == d->cap) {
        d->cap = d->cap ? d->cap * 2 : 16;
        d->st = realloc(d->st, sizeof(xr_dstate *) * (size_t)d->cap);
    }
    if ((d->n + 1) * 2 > d->tabcap) {
        int nc = d->tabcap ? d->tabcap * 2 : 64;
        free(d->tab);
        d->tab = calloc((size_t)nc, sizeof(int32_t));
        d->tabcap = nc;
        for (int i = 0; i < d->n; i++) {
            uint32_t j = d->st[i]->hash & (uint32_t)(nc - 1);
            while (d->tab[j]) j = (j + 1) & (uint32_t)(nc - 1);
            d->tab[j] = i + 1;
        }
    }
    xr_dstate *st = malloc(sizeof *st);
    st->pcs = malloc(sizeof(int32_t) * (size_t)(n ? n : 1));
    memcpy(st->pcs, pcs, sizeof(int32_t) * (size_t)n);
    st->n = n;
    st->prev = (uint8_t)prev;
    st->hash = h;
    st->idle = n == 1 && loop_pc >= 0 && (pcs[0] == loop_pc || pcs[0] == loop_pc + 2);
    for (int i = 0; i < 129; i++) st->trans[i] = -1;
    for (int i = 0; i < 8; i++) st->fin[i] = -1;
    memset(st->tp, 0, sizeof st->tp);
    st->id = d->n;
    uint32_t j = h & (uint32_t)(d->tabcap - 1);
    while (d->tab[j]) j = (j + 1) & (uint32_t)(d->tabcap - 1);
    d->tab[j] = d->n + 1;
    d->st[d->n] = st;
    return d->n++;
}

static inline bool xr_assert_ok(int k, int prev, int next) {
    switch (k) {
    case XA_BOT: return prev & XC_EDGE;
    case XA_EOT: return next & XC_EDGE;
    case XA_BOL: return prev & (XC_EDGE | XC_NL);
    case XA_EOL: return next & (XC_EDGE | XC_NL);
    case XA_WB: return !!(prev & XC_WORD) != !!(next & XC_WORD);
    default: return !!(prev & XC_WORD) == !!(next & XC_WORD);
    }
}
static inline bool xr_consumes(const xr_re *re, const xr_inst *in, uint32_t c) {
    switch (in->op) {
    case XR_CHAR: return (uint32_t)in->x == c;
    case XR_CLASS: return xr_class_has(&re->cls[in->x], c);
    case XR_ANY: return true;
    case XR_ANYNL: return !(c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029);
    default: return false;
    }
}

typedef struct {
    int32_t *list, *stack, *next;
    uint32_t *mark;
    uint32_t gen;
    int cap;
} xr_work;
static XS_TLS xr_work xr_w;
static void xr_work_need(int n) {
    if (xr_w.cap >= n) return;
    xr_w.cap = n * 2;
    xr_w.list = realloc(xr_w.list, sizeof(int32_t) * (size_t)xr_w.cap);
    xr_w.stack = realloc(xr_w.stack, sizeof(int32_t) * (size_t)xr_w.cap * 2);
    xr_w.next = realloc(xr_w.next, sizeof(int32_t) * (size_t)xr_w.cap);
    free(xr_w.mark);
    xr_w.mark = calloc((size_t)xr_w.cap, sizeof(uint32_t));
    xr_w.gen = 0;
}
static inline uint32_t xr_newgen(void) {
    if (++xr_w.gen == 0) { memset(xr_w.mark, 0, sizeof(uint32_t) * (size_t)xr_w.cap); xr_w.gen = 1; }
    return xr_w.gen;
}

/* One DFA step: the closure of the state's threads with the next character's context known, cut after a
   match (leftmost-first) unless longest; then the threads that consume c (eof: none). Returns the next
   state (<< 1) | (a match ended here). */
static int32_t xr_dfa_step(xr_re *re, xr_code *code, xr_dfa *d, int si, uint32_t c, int nextctx, bool eof, int loop_pc) {
    if (!re->uses_ctx) nextctx = 0; /* no assertions: one state per thread set, whatever came before */
    xr_dstate *st = d->st[si];
    int prev = st->prev;
    xr_work_need(code->n + 4);
    uint32_t g = xr_newgen();
    int nl = 0, sp = 0;
    bool matched = false;
    /* threads in priority order: a stack holding the pending ones, highest priority on top */
    for (int i = st->n - 1; i >= 0; i--) xr_w.stack[sp++] = st->pcs[i];
    while (sp > 0) {
        int pc = xr_w.stack[--sp];
        if (xr_w.mark[pc] == g) continue;
        xr_w.mark[pc] = g;
        xr_inst *in = &code->ins[pc];
        switch (in->op) {
        case XR_JMP: xr_w.stack[sp++] = in->x; break;
        case XR_SPLIT: xr_w.stack[sp++] = in->y; xr_w.stack[sp++] = in->x; break;
        case XR_SAVE: case XR_RESET: xr_w.stack[sp++] = pc + 1; break;
        case XR_ASSERT: if (xr_assert_ok(in->x, prev, nextctx)) xr_w.stack[sp++] = pc + 1; break;
        case XR_MATCH:
            matched = true;
            if (!d->longest) sp = 0; /* lower-priority threads lose to this match */
            break;
        default: xr_w.list[nl++] = pc; break;
        }
    }
    int nn = 0;
    if (!eof) {
        uint32_t g2 = xr_newgen();
        for (int i = 0; i < nl; i++) {
            xr_inst *in = &code->ins[xr_w.list[i]];
            if (xr_consumes(re, in, c)) {
                int to = xr_w.list[i] + 1;
                if (xr_w.mark[to] != g2) { xr_w.mark[to] = g2; xr_w.next[nn++] = to; }
            }
        }
    }
    int ns = nn ? xr_dfa_add(d, xr_w.next, nn, nextctx, loop_pc) : xr_dfa_add(d, xr_w.next, 0, 0, -1);
    return (ns << 1) | (matched ? 1 : 0);
}

static inline int xr_ctx_before(const unsigned char *s, size_t i) {
    if (i == 0) return XC_EDGE;
    size_t j = i - 1;
    while (j > 0 && (s[j] & 0xC0) == 0x80) j--;
    size_t len;
    return xr_ctx(xr_utf8(s + j, i - j, &len));
}
static int xr_dfa_start(xr_re *re, xr_code *code, xr_dfa *d, int entry, int which, int prev, int loop_pc) {
    (void)code;
    if (!re->uses_ctx) prev = 0;
    if (d->start[which][prev] && d->start[which][prev] - 1 < d->n) {
        int s = d->start[which][prev] - 1;
        if (d->st[s]->n == 1 && d->st[s]->pcs[0] == entry && d->st[s]->prev == prev) return s;
    }
    int32_t pc = entry;
    int s = xr_dfa_add(d, &pc, 1, prev, loop_pc);
    d->start[which][prev] = s + 1;
    return s;
}

/* Forward search from byte `from`: the end of the leftmost-first match, -1 if none. test: stop at the first. */
/* the first of two bytes in s[from, n), or n: 16 bytes at a time (NEON, SSE2), as memchr does for one */
#if !defined(XR_CORE_ONLY) && defined(__x86_64__)
#include <emmintrin.h>
#endif
static size_t xr_memchr2(const unsigned char *s, size_t from, size_t n, int a, int b) {
    size_t i = from;
#if XR_TEDDY_NEON
    const uint8x16_t va = vdupq_n_u8((uint8_t)a), vb = vdupq_n_u8((uint8_t)b);
    for (; i + 16 <= n; i += 16) {
        uint8x16_t v = vld1q_u8(s + i);
        uint8x16_t m = vorrq_u8(vceqq_u8(v, va), vceqq_u8(v, vb));
        uint64_t bits = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(m), 4)), 0);
        if (bits) return i + (size_t)(__builtin_ctzll(bits) >> 2);
    }
#elif !defined(XR_CORE_ONLY) && defined(__x86_64__)
    const __m128i va = _mm_set1_epi8((char)a), vb = _mm_set1_epi8((char)b);
    for (; i + 16 <= n; i += 16) {
        __m128i v = _mm_loadu_si128((const __m128i *)(s + i));
        unsigned bits = (unsigned)_mm_movemask_epi8(_mm_or_si128(_mm_cmpeq_epi8(v, va), _mm_cmpeq_epi8(v, vb)));
        if (bits) return i + (size_t)__builtin_ctz(bits);
    }
#endif
    for (; i < n; i++) if (s[i] == a || s[i] == b) return i;
    return n;
}
/* the first position >= from whose next K bytes, folded, hit the prefix table; -1 if none */
static int64_t xr_pf_next(const xr_re *re, const unsigned char *s, size_t n, size_t from) {
    const uint64_t *bm = re->pf_bm;
    size_t K = (size_t)re->pf_k;
    if (n < K) return -1;
    size_t last = n - K;
    if (K == 8) {
        for (size_t p = from; p <= last; p++) {
            uint64_t x;
            memcpy(&x, s + p, 8);
            uint32_t h = xr_pf_hash(x | 0x2020202020202020ULL);
            if ((bm[h >> 6] >> (h & 63)) & 1) return (int64_t)p;
        }
    } else {
        uint32_t mask = K == 4 ? 0xFFFFFFFFu : K == 3 ? 0xFFFFFFu : 0xFFFFu; /* (the low bytes are the first ones only
                                                                                  on little-endian machines) */
        size_t p = from;
        for (; p <= last && p + 4 <= n; p++) {
            uint32_t x;
            memcpy(&x, s + p, 4);
            uint32_t h = xr_pf_hash((uint64_t)((x & mask) | (0x20202020u & mask)));
            if ((bm[h >> 6] >> (h & 63)) & 1) return (int64_t)p;
        }
        for (; p <= last; p++) { /* the last bytes, without reading past the end */
            uint8_t b4[4] = {0};
            memcpy(b4, s + p, K);
            uint32_t x;
            memcpy(&x, b4, 4);
            uint32_t h = xr_pf_hash((uint64_t)((x & mask) | (0x20202020u & mask)));
            if ((bm[h >> 6] >> (h & 63)) & 1) return (int64_t)p;
        }
    }
    return -1;
}
/* idle_out (may be NULL): the last position, before the match was seen, where nothing was in progress (only the
   unanchored loop alive): no match starts before it, so the backward search for the start can stop there */
static int64_t xr_search_end(xr_re *re, const unsigned char *s, size_t n, size_t from, bool anchored, bool test, size_t *idle_out) {
    xr_dfa *d = &re->dfwd;
    xr_code *code = &re->fwd;
    int loop_pc = anchored ? -1 : re->entry_u;
    int entry = anchored ? re->entry_a : re->entry_u;
    int si = xr_dfa_start(re, code, d, entry, anchored, xr_ctx_before(s, from), loop_pc);
    xr_dstate *st = d->st[si];
    int64_t last = -1;
    size_t i = from;
    /* skipping to a byte a match can start with pays when such bytes are rare; in text full of them (DNA for
       /agggtaaa|tttaccct/i) every return to the idle state would leave the hot loop for a skip of nothing, so
       after enough skips that moved less than 4 bytes on average, idle states stay in the hot loop */
    bool skip_on = re->skip || re->pf_k;
    size_t skips = 0, skipped = 0;
    size_t idle_at = from;
#define XR_IDLE_OUT() do { if (idle_out) *idle_out = idle_at; } while (0)
    for (;;) {
        /* the hot loop: bytes with known transitions, following addresses; whether the next state matched, is
           dead or is idle travels in the low bits, so nothing of the next state is read but its table */
        if (!st->idle || !skip_on)
            while (i < n) {
                uintptr_t t = st->tp[s[i]];
                if (t == (uintptr_t)st) { /* a run that stays in this state (\d+, [^"]*) */
                    i++;
                    while (i < n && st->tp[s[i]] == t) i++;
                    continue;
                }
                if (!t) break;
                if (t & 1) {
                    last = (int64_t)i;
                    if (test) { XR_IDLE_OUT(); return last; }
                }
                if (t & 2) { XR_IDLE_OUT(); return last; }
                st = (xr_dstate *)(t & ~(uintptr_t)7);
                i++;
                if (t & 4) { /* idle again: the skip below */
                    if (last < 0) idle_at = i;
                    if (skip_on) break;
                }
            }
        si = st->id;
        unsigned ep = d->epoch;
        if (st->idle && skip_on && i < n) { /* nothing in progress: jump to a byte a match can start with */
            size_t j = i;
            if (re->pf_k) { /* or to a position whose next K bytes can begin one: the DFA takes over from there, and
                               comes back here when it is idle again, so each byte still goes through it once at most */
                int64_t c = xr_pf_next(re, s, n, i);
                j = c < 0 ? n : (size_t)c;
            } else if (re->first_single >= 0) {
                const unsigned char *hit = memchr(s + i, re->first_single, n - i);
                j = hit ? (size_t)(hit - s) : n;
            } else if (re->first_pair[0] >= 0) {
                j = xr_memchr2(s, i, n, re->first_pair[0], re->first_pair[1]);
            } else {
                while (j < n && !re->first[s[j]]) j++;
            }
            skipped += j - i;
            if (++skips == 64) { /* judged every 64 skips */
                if (skipped < 4 * skips) skip_on = false;
                skips = skipped = 0;
            }
            if (j != i) {
                i = j;
                if (last < 0) idle_at = i;
                if (re->uses_ctx) { si = xr_dfa_start(re, code, d, entry, 0, xr_ctx_before(s, i), loop_pc); st = d->st[si]; }
                if (i >= n) { XR_IDLE_OUT(); return last; } /* (skip is only on for patterns that cannot match empty) */
                continue;
            }
        }
        int32_t t;
        if (i >= n) {
            t = st->trans[128];
            if (t < 0) { t = xr_dfa_step(re, code, d, si, 0, XC_EDGE, true, loop_pc); if (d->epoch == ep) st->trans[128] = t; }
            if (t & 1) last = (int64_t)i;
            XR_IDLE_OUT();
            return last;
        }
        unsigned b = s[i];
        size_t len = 1;
        if (b < 128) {
            t = st->trans[b];
            if (t < 0) {
                t = xr_dfa_step(re, code, d, si, b, xr_ctx(b), false, loop_pc);
                if (d->epoch == ep) {
                    st->trans[b] = t;
                    xr_dstate *nx = d->st[t >> 1];
                    st->tp[b] = (uintptr_t)nx | (uintptr_t)(t & 1) | (nx->n == 0 ? 2u : 0u) | (nx->idle ? 4u : 0u);
                }
            }
        } else {
            uint32_t c = xr_utf8(s + i, n - i, &len);
            t = xr_dfa_step(re, code, d, si, c, xr_ctx(c), false, loop_pc);
        }
        if (t & 1) {
            last = (int64_t)i;
            if (test) { XR_IDLE_OUT(); return last; }
        }
        st = d->st[t >> 1];
        if (st->n == 0) { XR_IDLE_OUT(); return last; }
        i += len;
        if (st->idle && last < 0) idle_at = i;
    }
#undef XR_IDLE_OUT
}

/* Backward from the end of a match: where the leftmost match starts (longest reversed match, not before `from`) */
static int64_t xr_search_start(xr_re *re, const unsigned char *s, size_t n, size_t from, size_t end) {
    xr_dfa *d = &re->drev;
    xr_code *code = &re->rev;
    int prev = end >= n ? XC_EDGE : ({ size_t l; xr_ctx(xr_utf8(s + end, n - end, &l)); });
    int si = xr_dfa_start(re, code, d, re->entry_r, 0, prev, -1);
    int64_t first = -1;
    size_t i = end;
    for (;;) {
        xr_dstate *st = d->st[si];
        unsigned ep = d->epoch;
        int32_t t;
        if (i <= from) { /* the scan ends here: the context beyond is the real previous character */
            int nc = xr_ctx_before(s, i);
            if (nc == XC_EDGE || !re->uses_ctx) { /* (without ^ $ \b \B the context changes nothing: the cached step) */
                t = st->trans[128];
                if (t < 0) { t = xr_dfa_step(re, code, d, si, 0, XC_EDGE, true, -1); if (d->epoch == ep) st->trans[128] = t; }
            } else {
                t = st->fin[nc & 7];
                if (t < 0) { t = xr_dfa_step(re, code, d, si, 0, nc, true, -1); if (d->epoch == ep) st->fin[nc & 7] = t; }
            }
            if (t & 1) first = (int64_t)i;
            return first;
        }
        size_t j = i - 1;
        while (j > from && (s[j] & 0xC0) == 0x80) j--;
        unsigned b = s[j];
        if (b < 128) {
            t = st->trans[b];
            if (t < 0) { t = xr_dfa_step(re, code, d, si, b, xr_ctx(b), false, -1); if (d->epoch == ep) st->trans[b] = t; }
        } else {
            size_t len;
            uint32_t c = xr_utf8(s + j, i - j, &len);
            t = xr_dfa_step(re, code, d, si, c, xr_ctx(c), false, -1);
        }
        if (t & 1) first = (int64_t)i;
        si = t >> 1;
        if (d->st[si]->n == 0) return first;
        i = j;
    }
}

/* ---------- Pike VM: capture groups of a match known to span [start, end) ---------- */

typedef struct { int32_t pc; int32_t caps; } xr_thr;
static void xr_pike_add(xr_re *re, xr_thr *list, int *nl, int32_t *capbuf, int *ncap, int nsave, int pc, int32_t *caps, size_t pos,
                        int prev, int next, uint32_t *mark, uint32_t g, int32_t *stack) {
    /* explicit stack of (pc, restore slot, restore value) */
    int sp = 0;
    stack[sp++] = pc; stack[sp++] = -1; stack[sp++] = 0;
    while (sp > 0) {
        int32_t val = stack[--sp], slot = stack[--sp], p = stack[--sp];
        if (p < 0) { caps[slot] = val; continue; } /* restore a saved slot */
        if (mark[p] == g) continue;
        mark[p] = g;
        xr_inst *in = &re->fwd.ins[p];
        switch (in->op) {
        case XR_JMP: stack[sp++] = in->x; stack[sp++] = -1; stack[sp++] = 0; break;
        case XR_SPLIT:
            stack[sp++] = in->y; stack[sp++] = -1; stack[sp++] = 0;
            stack[sp++] = in->x; stack[sp++] = -1; stack[sp++] = 0;
            break;
        case XR_SAVE:
            stack[sp++] = -1; stack[sp++] = in->x; stack[sp++] = caps[in->x];
            caps[in->x] = (int32_t)pos;
            stack[sp++] = p + 1; stack[sp++] = -1; stack[sp++] = 0;
            break;
        case XR_ASSERT:
            if (xr_assert_ok(in->x, prev, next)) { stack[sp++] = p + 1; stack[sp++] = -1; stack[sp++] = 0; }
            break;
        case XR_RESET:
            for (int k = in->x; k <= in->y; k++) { stack[sp++] = -1; stack[sp++] = k; stack[sp++] = caps[k]; caps[k] = -1; }
            stack[sp++] = p + 1; stack[sp++] = -1; stack[sp++] = 0;
            break;
        default: {
            int k = (*ncap)++;
            memcpy(capbuf + (size_t)k * (size_t)nsave, caps, sizeof(int32_t) * (size_t)nsave);
            list[*nl].pc = p;
            list[(*nl)++].caps = k;
        }
        }
    }
}
/* fills caps[0..2*(ngroups+1)) with byte offsets (-1: the group did not take part) */
static bool xr_captures(xr_re *re, const unsigned char *s, size_t n, size_t start, size_t end, int32_t *out) {
    int nsave = 2 * (re->ngroups + 1);
    int np = re->fwd.n;
    /* scratch kept per thread, grown to the largest program seen */
    static XS_TLS struct { xr_thr *cl, *nl; int32_t *cb, *nb, *stack, *caps; uint32_t *mark; size_t np, ns; uint32_t g; } w;
    if ((size_t)np > w.np || (size_t)nsave > w.ns) {
        size_t P = (size_t)np > w.np ? (size_t)np : w.np, S = (size_t)nsave > w.ns ? (size_t)nsave : w.ns;
        w.cl = realloc(w.cl, sizeof(xr_thr) * P);
        w.nl = realloc(w.nl, sizeof(xr_thr) * P);
        w.cb = realloc(w.cb, sizeof(int32_t) * S * P);
        w.nb = realloc(w.nb, sizeof(int32_t) * S * P);
        w.stack = realloc(w.stack, sizeof(int32_t) * 3 * (P * 3 + 8 + S * P));
        w.caps = realloc(w.caps, sizeof(int32_t) * S);
        free(w.mark);
        w.mark = calloc(P, sizeof(uint32_t));
        w.g = 0;
        w.np = P;
        w.ns = S;
    }
    xr_thr *cl = w.cl, *nlst = w.nl;
    int32_t *cb = w.cb, *nb = w.nb, *stack = w.stack, *caps = w.caps;
    uint32_t *mark = w.mark;
    if (w.g > UINT32_MAX - (uint32_t)(n + 8)) { memset(mark, 0, sizeof(uint32_t) * w.np); w.g = 0; }
    for (int k = 0; k < nsave; k++) caps[k] = -1;
    uint32_t g = ++w.g;
    int ncl = 0, ncb = 0;
    bool found = false;
    size_t i = start;
    int prev = xr_ctx_before(s, start);
    size_t len = 0;
    uint32_t c = 0;
    int next = i < n ? xr_ctx(c = xr_utf8(s + i, n - i, &len)) : XC_EDGE;
    xr_pike_add(re, cl, &ncl, cb, &ncb, nsave, re->entry_a, caps, i, prev, next, mark, g, stack);
    for (;;) {
        int nnl = 0, nnb = 0;
        g = ++w.g;
        size_t ni = i + len;
        int nprev = next;
        uint32_t nc = 0;
        size_t nlen = 0;
        int nnext = ni < n ? xr_ctx(nc = xr_utf8(s + ni, n - ni, &nlen)) : XC_EDGE;
        for (int t = 0; t < ncl; t++) {
            xr_inst *in = &re->fwd.ins[cl[t].pc];
            int32_t *tc = cb + (size_t)cl[t].caps * (size_t)nsave;
            if (in->op == XR_MATCH) {
                memcpy(out, tc, sizeof(int32_t) * (size_t)nsave);
                found = true;
                break; /* lower-priority threads lose */
            }
            if (i < n && i < end && xr_consumes(re, in, c)) {
                memcpy(caps, tc, sizeof(int32_t) * (size_t)nsave);
                xr_pike_add(re, nlst, &nnl, nb, &nnb, nsave, cl[t].pc + 1, caps, ni, nprev, nnext, mark, g, stack);
            }
        }
        if (i >= end || nnl == 0) break;
        xr_thr *tl = cl; cl = nlst; nlst = tl;
        int32_t *tb = cb; cb = nb; nb = tb;
        ncl = nnl;
        ncb = nnb;
        i = ni;
        len = nlen;
        c = nc;
        next = nnext;
        (void)prev;
    }
    return found;
}

/* ---------- one-pass captures ----------
   When, from every point of the program, the next character selects at most one way forward (the classes of
   the instructions reachable without consuming are disjoint), the groups are found in a single left-to-right
   walk: a table per point says, for each ASCII byte, which way, which groups to set and which assertions
   must hold. Reaching the known end of the match ends it. */
typedef struct { int32_t next; int32_t inst; int32_t act, nact; uint8_t asserts; } xr_leaf;
typedef struct { int32_t tr[128]; int32_t leaf0, nleaf; int32_t match; int16_t ascii[128]; } xr_opnode; /* tr: next node, or (leaf << 1 | 1) when the leaf has work (groups, assertions); -1 none */
typedef struct xr_onepass {
    xr_opnode *nodes;
    int nn;
    xr_leaf *leaves;
    int nl, lcap;
    int32_t *acts; /* pairs: (slot, 0) save the position; (-1 - lo, hi) unset lo..hi */
    int na, acap;
    int32_t *node_of; /* pc -> node, -1 */
} xr_onepass;
static void xr_onepass_free(xr_onepass *op) {
    if (!op) return;
    free(op->nodes); free(op->leaves); free(op->acts); free(op->node_of); free(op);
}
static void xr_inst_ranges(xr_re *re, xr_inst *in, xr_rset *o) {
    switch (in->op) {
    case XR_CHAR: xr_rs_add(o, (uint32_t)in->x, (uint32_t)in->x); break;
    case XR_CLASS: for (int i = 0; i < re->cls[in->x].n; i++) xr_rs_add(o, re->cls[in->x].r[i].lo, re->cls[in->x].r[i].hi); break;
    case XR_ANY: xr_rs_add(o, 0, 0x10FFFF); break;
    default: /* ANYNL */
        xr_rs_add(o, 0, 9); xr_rs_add(o, 11, 12); xr_rs_add(o, 14, 0x2027); xr_rs_add(o, 0x202A, 0x10FFFF);
    }
}
static bool xr_overlap(xr_re *re, xr_inst *a, xr_inst *b) {
    xr_rset x = {0}, y = {0};
    xr_inst_ranges(re, a, &x);
    xr_inst_ranges(re, b, &y);
    xr_rs_norm(&x);
    xr_rs_norm(&y);
    bool ov = false;
    for (int i = 0, j = 0; i < x.n && j < y.n && !ov;) {
        if (x.r[i].hi < y.r[j].lo) i++;
        else if (y.r[j].hi < x.r[i].lo) j++;
        else ov = true;
    }
    free(x.r);
    free(y.r);
    return ov;
}
static int xr_op_node(xr_onepass *op, int pc, int *queue, int *nq) {
    if (op->node_of[pc] >= 0) return op->node_of[pc];
    op->node_of[pc] = op->nn;
    queue[(*nq)++] = pc;
    return op->nn++;
}
static void xr_op_act(xr_onepass *op, int32_t a, int32_t b) {
    if (op->na + 2 > op->acap) { op->acap = op->acap ? op->acap * 2 : 64; op->acts = realloc(op->acts, sizeof(int32_t) * (size_t)op->acap); }
    op->acts[op->na++] = a;
    op->acts[op->na++] = b;
}
static xr_onepass *xr_onepass_build(xr_re *re) {
    xr_code *c = &re->fwd;
    if (c->n > 2000) return NULL;
    if ((size_t)c->n * sizeof(xr_opnode) > (1u << 29)) return NULL;
    xr_onepass *op = calloc(1, sizeof *op);
    op->node_of = malloc(sizeof(int32_t) * (size_t)c->n);
    for (int i = 0; i < c->n; i++) op->node_of[i] = -1;
    int *queue = malloc(sizeof(int) * (size_t)c->n), nq = 0, qi = 0;
    op->nodes = calloc((size_t)c->n, sizeof(xr_opnode));
    xr_op_node(op, re->entry_a, queue, &nq);
    /* the closure of one point, depth first in priority order: (pc, action count, assertion mask) */
    int32_t *st = malloc(sizeof(int32_t) * 3 * (size_t)(c->n * 2 + 8));
    int32_t *pathacts = malloc(sizeof(int32_t) * 2 * (size_t)(c->n + 8) * 4);
    uint8_t *seen = malloc((size_t)c->n);
    bool ok = true;
    bool all_eot = true, any_match = false;
    while (ok && qi < nq) {
        int pc0 = queue[qi++];
        int node = op->node_of[pc0];
        xr_opnode *nd = &op->nodes[node];
        nd->leaf0 = op->nl;
        nd->match = -1;
        for (int k = 0; k < 128; k++) nd->ascii[k] = -1;
        memset(seen, 0, (size_t)c->n);
        /* recursive walk with an explicit stack of frames; actions along the path are kept in pathacts */
        int sp = 0, npa = 0;
        st[sp++] = pc0; st[sp++] = 0; st[sp++] = 0;
        bool asserted_path = false;
        while (sp > 0 && ok) {
            int mask = st[--sp], plen = st[--sp], pc = st[--sp];
            npa = plen;
            for (;;) {
                if (seen[pc]) { if (mask || asserted_path) ok = false; break; } /* another way to the same point */
                seen[pc] = 1;
                xr_inst *in = &c->ins[pc];
                if (in->op == XR_JMP) { pc = in->x; continue; }
                if (in->op == XR_SPLIT) { st[sp++] = in->y; st[sp++] = npa; st[sp++] = mask; pc = in->x; continue; }
                if (in->op == XR_SAVE) { pathacts[npa++] = in->x; pathacts[npa++] = 0; pc++; continue; }
                if (in->op == XR_RESET) { pathacts[npa++] = -1 - in->x; pathacts[npa++] = in->y; pc++; continue; }
                if (in->op == XR_ASSERT) { mask |= 1 << in->x; asserted_path = true; pc++; continue; }
                /* a leaf: a consuming instruction or the match */
                if (op->nl == op->lcap) { op->lcap = op->lcap ? op->lcap * 2 : 64; op->leaves = realloc(op->leaves, sizeof(xr_leaf) * (size_t)op->lcap); }
                xr_leaf *lf = &op->leaves[op->nl];
                lf->inst = pc;
                lf->asserts = (uint8_t)mask;
                lf->act = op->na;
                lf->nact = npa / 2;
                for (int k = 0; k < npa; k += 2) xr_op_act(op, pathacts[k], pathacts[k + 1]);
                nd = &op->nodes[node]; /* (no realloc of nodes: sized to the program) */
                if (in->op == XR_MATCH) {
                    any_match = true;
                    if (!(mask & (1 << XA_EOT))) all_eot = false;
                    if (nd->match >= 0) { ok = false; break; } /* two ways to match: ambiguous */
                    lf->next = -1;
                    nd->match = op->nl++;
                    break;
                }
                lf->next = -2 - (pc + 1); /* resolved below */
                for (int k = nd->leaf0; k < op->nl; k++) {
                    if (op->leaves[k].next == -1) continue;
                    if (xr_overlap(re, &c->ins[op->leaves[k].inst], in)) { ok = false; break; }
                }
                if (!ok) break;
                op->nl++;
                break;
            }
        }
        if (!ok) break;
        nd = &op->nodes[node];
        nd->nleaf = op->nl - nd->leaf0;
        for (int k = nd->leaf0; k < op->nl; k++) {
            xr_leaf *lf = &op->leaves[k];
            if (lf->next == -1) continue;
            int target = -2 - lf->next;
            lf->next = xr_op_node(op, target, queue, &nq);
            nd = &op->nodes[node];
            xr_inst *in = &c->ins[lf->inst];
            for (int b = 0; b < 128; b++) if (xr_consumes(re, in, (uint32_t)b)) nd->ascii[b] = (int16_t)k;
        }
    }
    if (ok)
        for (int i = 0; i < op->nn; i++) {
            xr_opnode *nd = &op->nodes[i];
            for (int b = 0; b < 128; b++) {
                int k = nd->ascii[b];
                if (k < 0) nd->tr[b] = -1;
                else if (op->leaves[k].nact || op->leaves[k].asserts) nd->tr[b] = (k << 1) | 1;
                else nd->tr[b] = (int32_t)((size_t)op->leaves[k].next * sizeof(xr_opnode)) << 1; /* the next node's byte offset */
            }
        }
    free(queue); free(st); free(pathacts); free(seen);
    if (!ok) { xr_onepass_free(op); return NULL; }
    re->ends_eot = any_match && all_eot;
    return op;
}
static inline bool xr_op_apply(xr_onepass *op, xr_leaf *lf, int32_t *caps, int32_t pos, int prev, int next) {
    if (lf->asserts) for (int k = 0; k < 6; k++) if ((lf->asserts >> k) & 1 && !xr_assert_ok(k, prev, next)) return false;
    const int32_t *a = op->acts + lf->act;
    for (int k = 0; k < lf->nact; k++, a += 2) {
        if (a[0] >= 0) caps[a[0]] = pos;
        else for (int j = -1 - a[0]; j <= a[1]; j++) caps[j] = -1;
    }
    return true;
}
/* the groups of the match from `start`, which ends at `end` (end = n with ends_eot: then it may not match) */
static bool xr_onepass_run(xr_re *re, const unsigned char *s, size_t n, size_t start, size_t end, int32_t *caps) {
    xr_onepass *op = re->op;
    int node = op->node_of[re->entry_a];
    size_t pos = start;
    int prev = xr_ctx_before(s, start);
    for (;;) {
        xr_opnode *nd = &op->nodes[node];
        /* the hot loop: ASCII bytes whose way does no work */
        const char *base = (const char *)op->nodes;
        while (pos < end && s[pos] < 128) {
            int32_t t = nd->tr[s[pos]];
            if (t < 0) return false;
            if (t & 1) break;
            xr_opnode *nx = (xr_opnode *)(base + (t >> 1));
            pos++;
            if (nx == nd) { /* a run in the same place (\d+, [^\]]+): only the byte test per character */
                while (pos < end && s[pos] < 128 && nd->tr[s[pos]] == t) pos++;
                continue;
            }
            nd = nx;
        }
        node = (int)(nd - op->nodes);
        if (pos > start) prev = xr_ctx_before(s, pos);
        if (pos >= end) {
            if (nd->match < 0) return false;
            return xr_op_apply(op, &op->leaves[nd->match], caps, (int32_t)pos, prev, pos < n ? ({ size_t l; xr_ctx(xr_utf8(s + pos, n - pos, &l)); }) : XC_EDGE);
        }
        unsigned b = s[pos];
        size_t len = 1;
        int li;
        uint32_t c = b;
        if (b < 128) li = nd->ascii[b];
        else {
            c = xr_utf8(s + pos, n - pos, &len);
            li = -1;
            for (int k = nd->leaf0; k < nd->leaf0 + nd->nleaf; k++)
                if (op->leaves[k].next >= 0 && xr_consumes(re, &re->fwd.ins[op->leaves[k].inst], c)) { li = k; break; }
        }
        if (li < 0) return false;
        int next = xr_ctx(c);
        if (!xr_op_apply(op, &op->leaves[li], caps, (int32_t)pos, prev, next)) return false;
        node = op->leaves[li].next;
        prev = next;
        pos += len;
    }
}
static inline xr_onepass *xr_get_onepass(xr_re *re) {
    if (!re->op_tried) { re->op_tried = true; re->op = xr_onepass_build(re); }
    return re->op;
}

/* Captures by bounded backtracking (RE2's BitState): a depth-first walk in priority order that never visits
   an (instruction, position) pair twice, so it stays linear; the first match it reaches is the leftmost-first
   one. Used when prog x span fits the bitmap (short matches); xr_captures (Pike) otherwise. */
static bool xr_bitstate(xr_re *re, const unsigned char *s, size_t n, size_t start, size_t end, int32_t *out) {
    int np = re->fwd.n;
    size_t span = end - start + 1;
    size_t bits = (size_t)np * span;
    if (bits > (256u << 13)) return false; /* 256 KB of bitmap: leave long matches to the Pike VM */
    static XS_TLS struct { uint64_t *vis; size_t vcap; int32_t *job; size_t jcap; } w;
    size_t words = (bits + 63) / 64;
    if (words > w.vcap) { free(w.vis); w.vcap = words * 2; w.vis = malloc(sizeof(uint64_t) * w.vcap); }
    memset(w.vis, 0, sizeof(uint64_t) * words);
    int nsave = 2 * (re->ngroups + 1);
    int32_t caps[2 * 64 + 2];
    int32_t *cp = nsave <= (int)(sizeof caps / sizeof caps[0]) ? caps : malloc(sizeof(int32_t) * (size_t)nsave);
    for (int k = 0; k < nsave; k++) cp[k] = -1;
    size_t jn = 0;
#define XR_PUSH(a, b) do { if (jn + 2 > w.jcap) { w.jcap = w.jcap ? w.jcap * 2 : 256; w.job = realloc(w.job, sizeof(int32_t) * w.jcap); } w.job[jn++] = (a); w.job[jn++] = (b); } while (0)
    /* a job is (pc, pos) to explore, or (-1 - slot, old value) to restore a capture when backing out */
    const uint8_t *join = re->join;
    XR_PUSH(re->entry_a, (int32_t)start);
    bool found = false;
    while (jn > 0) {
        int32_t pos = w.job[--jn], pc = w.job[--jn];
        if (pc < 0) { cp[-1 - pc] = pos; continue; }
        for (;;) { /* follow the preferred branch inline, pushing the others */
            if (join[pc]) { /* only where paths meet can one come back: straight runs are walked once each */
                size_t bit = (size_t)pc * span + (size_t)(pos - (int32_t)start);
                if (w.vis[bit >> 6] & (1ull << (bit & 63))) break;
                w.vis[bit >> 6] |= 1ull << (bit & 63);
            }
            xr_inst *in = &re->fwd.ins[pc];
            bool go = false;
            switch (in->op) {
            case XR_JMP: pc = in->x; go = true; break;
            case XR_SPLIT: XR_PUSH(in->y, pos); pc = in->x; go = true; break;
            case XR_SAVE: XR_PUSH(-1 - in->x, cp[in->x]); cp[in->x] = pos; pc++; go = true; break;
            case XR_RESET:
                for (int k = in->x; k <= in->y; k++) { XR_PUSH(-1 - k, cp[k]); cp[k] = -1; }
                pc++; go = true; break;
            case XR_ASSERT: {
                int prev = xr_ctx_before(s, (size_t)pos);
                size_t l;
                int next = (size_t)pos < n ? xr_ctx(xr_utf8(s + pos, n - (size_t)pos, &l)) : XC_EDGE;
                if (xr_assert_ok(in->x, prev, next)) { pc++; go = true; }
                break;
            }
            case XR_MATCH:
                memcpy(out, cp, sizeof(int32_t) * (size_t)nsave);
                found = true;
                jn = 0;
                break;
            default: { /* consuming */
                if ((size_t)pos >= end) break;
                size_t l = 1;
                uint32_t c = s[pos] < 0x80 ? s[pos] : xr_utf8(s + pos, n - (size_t)pos, &l);
                if (xr_consumes(re, in, c)) { pos += (int32_t)l; pc++; go = true; }
            }
            }
            if (!go || found) break;
        }
        if (found) break;
    }
#undef XR_PUSH
    if (cp != caps) free(cp);
    return found;
}

/* The leftmost match at or after byte `from`: [*ms, *me) in bytes; false if none. sticky: only at `from`. */
/* C+ L prefilter: the leftmost match is found at the first L whose run of C (at least lit_min long) starts a
   match. A match contains an L (the first after its run), and a start later in the same run reads the same
   suffix, so trying each L once, from the start of its run, finds the leftmost. */
static bool xr_lit_find(xr_re *re, const unsigned char *s, size_t n, size_t from, int64_t *ms, int64_t *me) {
    size_t pos = from;
    while (pos < n) {
        const unsigned char *hit = memchr(s + pos, re->lit, n - pos);
        if (!hit) return false;
        size_t p = (size_t)(hit - s), st = p;
        while (st > from && s[st - 1] < 128 && ((re->lit_cls[s[st - 1] >> 5] >> (s[st - 1] & 31)) & 1)) st--;
        if (p - st >= (size_t)re->lit_min) {
            int64_t end = xr_search_end(re, s, n, st, true, false, NULL);
            if (end >= 0) { *ms = (int64_t)st; *me = end; return true; }
        }
        pos = p + 1;
    }
    return false;
}
/* the length of the character of C at i (0: not in C) */
static inline size_t xr_run_at(xr_re *re, const unsigned char *s, size_t n, size_t i) {
    uint8_t t = re->run_tab[s[i]];
    if (t < 2) return t;
    size_t l;
    uint32_t c = xr_utf8(s + i, n - i, &l);
    return xr_class_has(&re->cls[re->run_cls], c) ? l : 0;
}
static bool xr_run_find(xr_re *re, const unsigned char *s, size_t n, size_t from, bool sticky, int64_t *ms, int64_t *me) {
    size_t i = from;
    for (;;) {
        if (!sticky) {
            while (i < n) {
                if (!re->run_tab[s[i]]) { i++; continue; }
                if (xr_run_at(re, s, n, i)) break;
                i++;
                while (i < n && (s[i] & 0xC0) == 0x80) i++; /* the rest of a character not in C */
            }
        }
        size_t st = i, cnt = 0, l;
        while (i < n && (l = xr_run_at(re, s, n, i))) { i += l; cnt++; }
        if (cnt >= (size_t)re->run_min && cnt > 0) { *ms = (int64_t)st; *me = (int64_t)i; return true; }
        if (sticky || i >= n) return false;
    }
}
/* fixed-length alternatives: the alternative that matches at p (the lowest one), or -1 */
static inline int xr_fx_at(const xr_re *re, const unsigned char *s, size_t n, size_t p) {
    uint32_t alive = re->fx_m[0][s[p]];
    size_t i = 1, mx = (size_t)re->fx_max;
    for (; alive && i < mx && p + i < n; i++) alive &= re->fx_m[i][s[p + i]];
    for (; alive && i < mx; i++) { /* past the end: only the alternatives already complete */
        uint32_t done = 0;
        for (int a = 0; a < re->fx_n; a++) if (re->fx_len[a] <= i) done |= 1u << a;
        alive &= done;
    }
    return alive ? __builtin_ctz(alive) : -1;
}
#if XR_TEDDY_SSSE3
static int xr_ssse3_ = -1;
static bool xr_has_ssse3(void) {
    if (xr_ssse3_ < 0) { unsigned a, b, c, d; xr_ssse3_ = __get_cpuid(1, &a, &b, &c, &d) && (c & bit_SSSE3) ? 1 : 0; }
    return xr_ssse3_ == 1;
}
#endif
#if XR_TEDDY_NEON || XR_TEDDY_SSSE3
/* Teddy: the first match at or after `from` among the positions whose 16-byte blocks fit before the end (the
   last ones are left to the scalar loop: *done says how far this went) */
#if XR_TEDDY_SSSE3
__attribute__((target("ssse3")))
#endif
static bool xr_td_find(xr_re *re, const unsigned char *s, size_t n, size_t from, size_t *done, int64_t *ms, int64_t *me) {
    size_t K = (size_t)re->td_k, p = from;
#if XR_TEDDY_NEON
    const uint8x16_t nib = vdupq_n_u8(0x0f);
    uint8x16_t lo[4], hi[4];
    for (size_t i = 0; i < K; i++) { lo[i] = vld1q_u8(re->td_lo[i]); hi[i] = vld1q_u8(re->td_hi[i]); }
    for (; p + 16 + K - 1 <= n; p += 16) {
        uint8x16_t m = vdupq_n_u8(0xff);
        for (size_t i = 0; i < K; i++) {
            uint8x16_t v = vld1q_u8(s + p + i);
            m = vandq_u8(m, vandq_u8(vqtbl1q_u8(lo[i], vandq_u8(v, nib)), vqtbl1q_u8(hi[i], vshrq_n_u8(v, 4))));
        }
        uint64_t bits = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(vtstq_u8(m, m)), 4)), 0);
        while (bits) {
            size_t q = p + (size_t)(__builtin_ctzll(bits) >> 2);
            int a = xr_fx_at(re, s, n, q);
            if (a >= 0) { *ms = (int64_t)q; *me = (int64_t)(q + re->fx_len[a]); return true; }
            bits &= ~((uint64_t)0xF << (__builtin_ctzll(bits) & ~3));
        }
    }
#else
    const __m128i nib = _mm_set1_epi8(0x0f), zero = _mm_setzero_si128();
    __m128i lo[4], hi[4];
    for (size_t i = 0; i < K; i++) { lo[i] = _mm_loadu_si128((const __m128i *)re->td_lo[i]); hi[i] = _mm_loadu_si128((const __m128i *)re->td_hi[i]); }
    for (; p + 16 + K - 1 <= n; p += 16) {
        __m128i m = _mm_set1_epi8((char)0xff);
        for (size_t i = 0; i < K; i++) {
            __m128i v = _mm_loadu_si128((const __m128i *)(s + p + i));
            __m128i l = _mm_shuffle_epi8(lo[i], _mm_and_si128(v, nib));
            __m128i h = _mm_shuffle_epi8(hi[i], _mm_and_si128(_mm_srli_epi16(v, 4), nib));
            m = _mm_and_si128(m, _mm_and_si128(l, h));
        }
        unsigned bits = ~(unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(m, zero)) & 0xFFFFu;
        while (bits) {
            size_t q = p + (size_t)__builtin_ctz(bits);
            int a = xr_fx_at(re, s, n, q);
            if (a >= 0) { *ms = (int64_t)q; *me = (int64_t)(q + re->fx_len[a]); return true; }
            bits &= bits - 1;
        }
    }
#endif
    *done = p;
    return false;
}
#endif
static bool xr_fx_find(xr_re *re, const unsigned char *s, size_t n, size_t from, bool sticky, int64_t *ms, int64_t *me) {
    const uint32_t *m0 = re->fx_m[0];
#if XR_TEDDY_NEON || XR_TEDDY_SSSE3
#if XR_TEDDY_SSSE3
    if (!sticky && re->td_k && xr_has_ssse3())
#else
    if (!sticky && re->td_k)
#endif
    {
        size_t done = from;
        if (xr_td_find(re, s, n, from, &done, ms, me)) return true;
        from = done; /* the rest, fewer than 16 + K positions, below */
        for (size_t p = from; p < n; p++) {
            int a = m0[s[p]] ? xr_fx_at(re, s, n, p) : -1;
            if (a >= 0) { *ms = (int64_t)p; *me = (int64_t)(p + re->fx_len[a]); return true; }
        }
        return false;
    }
#endif
    for (size_t p = from; p < n; p++) {
        if (!sticky) {
            if (re->pf_k) { /* (every alternative is at least pf_k long: the prefix table sees each match) */
                int64_t q = xr_pf_next(re, s, n, p);
                if (q < 0) return false;
                p = (size_t)q;
            } else {
                while (p < n && !m0[s[p]]) p++;
                if (p == n) return false;
            }
        }
        int a = xr_fx_at(re, s, n, p);
        if (a >= 0) { *ms = (int64_t)p; *me = (int64_t)(p + re->fx_len[a]); return true; }
        if (sticky) return false;
    }
    return false;
}
/* P C{min,} E: the leftmost P whose first E after it is at least min characters away. When no E follows a P,
   none follows any later P either: the search ends */
/* memchr for what is usually near (the next delimiter a few bytes on): the first 16 bytes looked at here, no call */
static inline __attribute__((always_inline)) const unsigned char *xr_memchr_near(const unsigned char *s, int c, size_t n) {
    size_t k = n < 16 ? n : 16;
    for (size_t i = 0; i < k; i++) if (s[i] == c) return s + i;
    return n > 16 ? memchr(s + 16, c, n - 16) : NULL;
}
static bool xr_dl_find(xr_re *re, const unsigned char *s, size_t n, size_t from, bool sticky, int64_t *ms, int64_t *me) {
    size_t np = (size_t)re->dl_np;
    for (size_t p = from; p + np < n; p++) {
        if (!sticky) {
            const unsigned char *h = xr_memchr_near(s + p, re->dl_p[0], n - p);
            if (!h) return false;
            p = (size_t)(h - s);
            if (p + np >= n) return false;
        }
        if (memcmp(s + p, re->dl_p, np) == 0) {
            const unsigned char *q = xr_memchr_near(s + p + np, re->dl_end, n - p - np);
            if (!q) return false;
            size_t body = (size_t)(q - s) - p - np, chars = body;
            if (body < (size_t)re->dl_min * 4) { /* characters, not bytes (UTF-8): only counted when it could matter */
                chars = 0;
                for (size_t i = p + np; i < (size_t)(q - s); i++) chars += (s[i] & 0xC0) != 0x80;
            }
            if (chars >= (size_t)re->dl_min) { *ms = (int64_t)p; *me = (int64_t)(q - s) + 1; return true; }
        }
        if (sticky) return false;
    }
    return false;
}
static bool xr_find(xr_re *re, const char *str, size_t n, size_t from, bool sticky, int64_t *ms, int64_t *me) {
    const unsigned char *s = (const unsigned char *)str;
    if (from > n) return false;
    if (re->run) return xr_run_find(re, s, n, from, sticky, ms, me);
    if (re->fx_n) return xr_fx_find(re, s, n, from, sticky, ms, me);
    if (re->dl_np) return xr_dl_find(re, s, n, from, sticky, ms, me);
    if (re->anchored_start && from > 0 && !(re->flags & XF_M)) return false;
    if (re->lit >= 0 && !sticky && !re->anchored_start && !re->uses_ctx) return xr_lit_find(re, s, n, from, ms, me);
    bool anchored = sticky || re->anchored_start;
    size_t idle = from;
    int64_t end = xr_search_end(re, s, n, from, anchored, false, &idle);
    if (end < 0) return false;
    if (idle < from || idle > (size_t)end) idle = from; /* (cannot happen; keep the bound sound) */
    int64_t start = anchored ? (int64_t)from : xr_search_start(re, s, n, idle, (size_t)end);
    if (start < 0) start = (int64_t)from; /* cannot happen; keep the result well formed */
    *ms = start;
    *me = end;
    return true;
}
static bool xr_test(xr_re *re, const char *str, size_t n, size_t from, bool sticky) {
    if (from > n) return false;
    if (re->anchored_start && from > 0 && !(re->flags & XF_M)) return false;
    if (re->run) { int64_t a, b; return xr_run_find(re, (const unsigned char *)str, n, from, sticky, &a, &b); }
    if (re->fx_n) { int64_t a, b; return xr_fx_find(re, (const unsigned char *)str, n, from, sticky, &a, &b); }
    if (re->dl_np) { int64_t a, b; return xr_dl_find(re, (const unsigned char *)str, n, from, sticky, &a, &b); }
    if (re->lit >= 0 && !sticky && !re->anchored_start && !re->uses_ctx) { int64_t a, b; return xr_lit_find(re, (const unsigned char *)str, n, from, &a, &b); }
    return xr_search_end(re, (const unsigned char *)str, n, from, sticky || re->anchored_start, true, NULL) >= 0;
}

#endif /* XR_CORE_ONLY */

/* ---------- xisto binding: RegExp, its match arrays, and the string methods that take one ---------- */
#ifdef XS_RT_REGEX

typedef struct xs_RegExp {
    xs_hdr hdr;
    xs_str *f_source;
    xs_str *f_flags;
    int64_t f_lastIndex;
    bool f_global, f_ignoreCase, f_multiline, f_sticky, f_dotAll, f_unicode, f_hasIndices;
    xr_re *re;
} xs_RegExp;
typedef struct xs_RegExpGroups {
    xs_hdr hdr;
    xr_re *re;
    int n;
    xs_str **items; /* per group, NULL when it did not take part */
} xs_RegExpGroups;
typedef struct xs_RegExpExecArray {
    xs_hdr hdr;
    int64_t f_index;  /* -1: a match() with flag g (all the matched strings, no positions) */
    xs_str *f_input;
    int64_t f_length;
    xs_RegExpGroups *f_groups;
    xs_str **items;   /* made when first read (m[i], printing): a match usually reads a few of its groups */
    int32_t *caps;    /* byte offsets per group, -1 when it did not take part; NULL: items are all made */
    bool inline_store; /* items and caps live in the same allocation, after the struct */
} xs_RegExpExecArray;

static void xr_release_re(xr_re *re) { if (re && --re->rc == 0) xr_free(re); }
static void xs_RegExp_free(void *p) {
    xs_RegExp *r = p;
    xs_release(r->f_source);
    xs_release(r->f_flags);
    xr_release_re(r->re);
    xs_obj_free(p);
}
static void xs_RegExp_print(void *p) {
    xs_RegExp *r = p;
    fputc('/', XS_OUT);
    fwrite(r->f_source->data, 1, (size_t)r->f_source->len, XS_OUT);
    fputc('/', XS_OUT);
    fwrite(r->f_flags->data, 1, (size_t)r->f_flags->len, XS_OUT);
}
static void xs_RegExpGroups_free(void *p) {
    xs_RegExpGroups *g = p;
    for (int i = 0; i < g->n; i++) xs_release(g->items[i]);
    free(g->items);
    xr_release_re(g->re);
    xs_obj_free(p);
}
static void xs_RegExpGroups_print(void *p) {
    xs_RegExpGroups *g = p;
    xs_insp fr;
    if (!xs_insp_begin(&fr, p, "Object", NULL)) return;
    for (int i = 1; i < g->n; i++) {
        if (!g->re->names[i]) continue;
        xs_insp_entry(&fr);
        fputs(g->re->names[i], XS_OUT);
        fputs(": ", XS_OUT);
        if (g->items[i]) xs_print_str_quoted(g->items[i]); else fputs("undefined", XS_OUT);
    }
    xs_insp_end(&fr, "[Object: null prototype] {", "}", false, false, false);
}
static void xs_RegExpExecArray_free(void *p) {
    xs_RegExpExecArray *m = p;
    for (int64_t i = 0; i < m->f_length; i++) xs_release(m->items[i]);
    if (!m->inline_store) { free(m->items); free(m->caps); }
    xs_release(m->f_input);
    xs_release(m->f_groups);
    xs_obj_free(p);
}
static xs_str *xr_item(xs_RegExpExecArray *m, int64_t i) {
    if (!m->items[i] && m->caps && m->caps[2 * i] >= 0)
        m->items[i] = xs_str_new(m->f_input->data + m->caps[2 * i], (size_t)(m->caps[2 * i + 1] - m->caps[2 * i]));
    return m->items[i];
}
static xs_str *xr_item(xs_RegExpExecArray *m, int64_t i);
static void xs_RegExpExecArray_print(void *p) {
    xs_RegExpExecArray *m = p;
    xs_insp fr;
    if (!xs_insp_begin(&fr, p, "Array", m->f_length || m->f_index >= 0 ? NULL : "[]")) return;
    for (int64_t i = 0; i < m->f_length; i++) {
        xs_insp_entry(&fr);
        if (xr_item(m, i)) xs_print_str_quoted(m->items[i]); else fputs("undefined", XS_OUT);
    }
    if (m->f_index >= 0) {
        xs_insp_entry(&fr);
        fprintf(XS_OUT, "index: %lld", (long long)m->f_index);
        xs_insp_entry(&fr);
        fputs("input: ", XS_OUT);
        xs_print_str_quoted(m->f_input);
        xs_insp_entry(&fr);
        fputs("groups: ", XS_OUT);
        if (m->f_groups) xs_RegExpGroups_print(m->f_groups); else fputs("undefined", XS_OUT);
    }
    xs_insp_end(&fr, "[", "]", true, false, false);
}

static xs_str *xr_empty_str(void) {
    static xs_str *e;
    if (!e) e = xs_str_immortal("", 0);
    return e;
}
XS_NOINLINE void xr_throw(const char *name, const char *msg) {
    xs_Error *e = xs_Error_new(xs_str_new(msg, strlen(msg)));
    xs_release(e->f_message); /* xs_Error_new retained it */
    xs_release(e->f_name);
    e->f_name = xs_str_new(name, strlen(name));
    xs_throw(e);
}

static xs_RegExp *xr_wrap(xr_re *re, xs_str *source, xs_str *flags) {
    xs_RegExp *r = xs_obj_alloc(sizeof *r, xs_RegExp_free, xs_RegExp_print);
    r->re = re;
    r->f_source = source;
    r->f_flags = flags;
    int f = re->flags;
    r->f_global = f & XF_G;
    r->f_ignoreCase = f & XF_I;
    r->f_multiline = f & XF_M;
    r->f_sticky = f & XF_Y;
    r->f_dotAll = f & XF_S;
    r->f_unicode = f & XF_U;
    r->f_hasIndices = f & XF_D;
    return r;
}
/* flags in canonical order, as RegExp.prototype.flags gives them */
static xs_str *xr_flags_str(int f) {
    char b[8];
    int n = 0;
    if (f & XF_D) b[n++] = 'd';
    if (f & XF_G) b[n++] = 'g';
    if (f & XF_I) b[n++] = 'i';
    if (f & XF_M) b[n++] = 'm';
    if (f & XF_S) b[n++] = 's';
    if (f & XF_U) b[n++] = 'u';
    if (f & XF_Y) b[n++] = 'y';
    return xs_str_new(b, (size_t)n);
}
/* new RegExp(source, flags) */
XS_NOINLINE xs_RegExp *xs_RegExp_new(xs_str *src, xs_str *flags) {
    const char *err = NULL;
    int f = flags ? xr_parse_flags(flags->data, (size_t)flags->len, &err) : 0;
    if (f < 0) {
        char msg[256];
        snprintf(msg, sizeof msg, "Invalid flags supplied to RegExp constructor '%.100s'", flags->data);
        xr_throw("SyntaxError", msg);
    }
    xr_re *re = xr_compile(src->data, (size_t)src->len, f, &err);
    if (!re) {
        char msg[512];
        snprintf(msg, sizeof msg, "Invalid regular expression: /%.200s/%.10s: %s", src->data, flags ? flags->data : "", err);
        xr_throw("SyntaxError", msg);
    }
    return xr_wrap(re, xs_str_len(src) >= 0 ? xs_retain(src) : src, xr_flags_str(f));
}
/* a /literal/: the pattern is compiled once per thread (checked by the compiler, so it compiles); each
   evaluation is a new RegExp object, as in JS (its lastIndex is its own) */
XS_NOINLINE xs_RegExp *xs_RegExp_lit(xr_re **slot, const char *src, int64_t n, const char *flags) {
    if (!*slot) {
        const char *err = NULL;
        int f = xr_parse_flags(flags, strlen(flags), &err);
        *slot = xr_compile(src, (size_t)n, f, &err);
        if (!*slot) xs_panic("invalid regular expression literal");
        (*slot)->rc = INT32_MAX / 2; /* lives as long as the thread */
    }
    (*slot)->rc++;
    return xr_wrap(*slot, xs_str_new(src, (size_t)n), xr_flags_str((*slot)->flags));
}

/* byte offset <-> code point index: ASCII strings are their own index */
static inline bool xr_ascii(xs_str *s) { return xs_str_len(s) == s->len; }
static inline int64_t xr_cp(xs_str *s, int64_t b) { return xr_ascii(s) ? b : xs_str_cp_at_byte(s, b); }
static inline int64_t xr_byte(xs_str *s, int64_t i) { return xr_ascii(s) ? (i < s->len ? i : s->len) : xs_str_byte_at(s, i); }
static inline int64_t xr_next_char(xs_str *s, int64_t b) { /* the byte after the character at b */
    if (b >= s->len) return b + 1;
    b++;
    while (b < s->len && (s->data[b] & 0xC0) == 0x80) b++;
    return b;
}

/* a match: [ms, me) bytes, with the groups when the pattern has some */
typedef struct { int64_t ms, me; int32_t caps[64]; int32_t *more; } xr_m;
static bool xr_match_at(xs_RegExp *r, xs_str *s, int64_t from, bool sticky, xr_m *m, bool want_caps) {
    m->more = NULL;
    xr_re *re = r->re;
    if (want_caps && re->ngroups && 2 * (re->ngroups + 1) <= 64 && xr_get_onepass(re) && (sticky || re->anchored_start) && re->ends_eot) {
        /* anchored at both ends and one-pass: a single walk decides the match and its groups */
        if (re->anchored_start && from > 0) return false;
        for (int k = 0; k < 2 * (re->ngroups + 1); k++) m->caps[k] = -1;
        if (!xr_onepass_run(re, (const unsigned char *)s->data, (size_t)s->len, (size_t)from, (size_t)s->len, m->caps)) return false;
        m->ms = m->caps[0];
        m->me = m->caps[1];
        return true;
    }
    if (want_caps && re->ngroups && (sticky || re->anchored_start) && 2 * (re->ngroups + 1) <= 64 &&
        (size_t)re->fwd.n * (size_t)(s->len - from + 1) <= (256u << 13)) {
        /* anchored: one depth-first walk finds the match and its groups, no DFA passes */
        if (re->anchored_start && from > 0) return false;
        for (int k = 0; k < 2 * (re->ngroups + 1); k++) m->caps[k] = -1;
        if (!xr_bitstate(re, (const unsigned char *)s->data, (size_t)s->len, (size_t)from, (size_t)s->len, m->caps)) return false;
        m->ms = m->caps[0];
        m->me = m->caps[1];
        return true;
    }
    if (!xr_find(r->re, s->data, (size_t)s->len, (size_t)from, sticky, &m->ms, &m->me)) return false;
    if (want_caps && r->re->ngroups) {
        int ns = 2 * (r->re->ngroups + 1);
        int32_t *c = ns <= 64 ? m->caps : (m->more = malloc(sizeof(int32_t) * (size_t)ns));
        for (int k = 0; k < ns; k++) c[k] = -1;
        if (!(xr_get_onepass(r->re) && xr_onepass_run(r->re, (const unsigned char *)s->data, (size_t)s->len, (size_t)m->ms, (size_t)m->me, c)) &&
            !xr_bitstate(r->re, (const unsigned char *)s->data, (size_t)s->len, (size_t)m->ms, (size_t)m->me, c))
            xr_captures(r->re, (const unsigned char *)s->data, (size_t)s->len, (size_t)m->ms, (size_t)m->me, c);
    } else {
        m->caps[0] = (int32_t)m->ms;
        m->caps[1] = (int32_t)m->me;
    }
    return true;
}
static inline int32_t *xr_caps(xr_m *m) { return m->more ? m->more : m->caps; }
static xs_str *xr_group_str(xs_str *s, xr_m *m, int g) {
    int32_t *c = xr_caps(m);
    if (c[2 * g] < 0) return NULL;
    return xs_str_new(s->data + c[2 * g], (size_t)(c[2 * g + 1] - c[2 * g]));
}
static xs_RegExpExecArray *xr_exec_array(xs_RegExp *r, xs_str *s, xr_m *m) {
    int n = r->re->ngroups + 1;
    size_t base = (sizeof(xs_RegExpExecArray) + 7) & ~(size_t)7;
    xs_RegExpExecArray *a = xs_obj_alloc(base + sizeof(xs_str *) * (size_t)n + sizeof(int32_t) * 2 * (size_t)n, xs_RegExpExecArray_free, xs_RegExpExecArray_print);
    a->inline_store = true;
    a->items = (xs_str **)((char *)a + base);
    memset(a->items, 0, sizeof(xs_str *) * (size_t)n);
    a->caps = (int32_t *)(a->items + n);
    int32_t *c = xr_caps(m);
    a->caps[0] = (int32_t)m->ms;
    a->caps[1] = (int32_t)m->me;
    for (int k = 2; k < 2 * n; k++) a->caps[k] = c[k];
    a->f_length = n;
    a->f_index = xr_cp(s, m->ms);
    a->f_input = xs_retain(s);
    if (r->re->names) for (int g = 0; g < n; g++) xr_item(a, g);
    if (r->re->names) {
        xs_RegExpGroups *gr = xs_obj_alloc(sizeof *gr, xs_RegExpGroups_free, xs_RegExpGroups_print);
        gr->re = r->re;
        r->re->rc++;
        gr->n = n;
        gr->items = malloc(sizeof(xs_str *) * (size_t)n);
        for (int g = 0; g < n; g++) gr->items[g] = a->items[g] ? xs_retain(a->items[g]) : NULL;
        a->f_groups = gr;
    }
    free(m->more);
    m->more = NULL;
    return a;
}

/* re.lastIndex as a byte offset (global/sticky); false (and lastIndex = 0) when it is past the end */
static bool xr_start(xs_RegExp *r, xs_str *s, int64_t *from) {
    if (!(r->re->flags & (XF_G | XF_Y))) { *from = 0; return true; }
    if (r->f_lastIndex < 0) r->f_lastIndex = 0;
    if (r->f_lastIndex > xs_str_len(s)) { r->f_lastIndex = 0; return false; }
    *from = xr_byte(s, r->f_lastIndex);
    return true;
}
XS_NOINLINE bool xs_RegExp_test(xs_RegExp *r, xs_str *s) {
    int64_t from;
    if (!xr_start(r, s, &from)) return false;
    bool lg = r->re->flags & (XF_G | XF_Y);
    if (!lg) return xr_test(r->re, s->data, (size_t)s->len, 0, false);
    xr_m m;
    if (!xr_match_at(r, s, from, r->re->flags & XF_Y, &m, false)) { r->f_lastIndex = 0; return false; }
    r->f_lastIndex = xr_cp(s, m.me);
    return true;
}
XS_NOINLINE xs_RegExpExecArray *xs_RegExp_exec(xs_RegExp *r, xs_str *s) {
    int64_t from;
    if (!xr_start(r, s, &from)) return NULL;
    xr_m m;
    if (!xr_match_at(r, s, from, r->re->flags & XF_Y, &m, true)) {
        if (r->re->flags & (XF_G | XF_Y)) r->f_lastIndex = 0;
        return NULL;
    }
    if (r->re->flags & (XF_G | XF_Y)) r->f_lastIndex = xr_cp(s, m.me);
    return xr_exec_array(r, s, &m);
}
XS_NOINLINE xs_str *xs_RegExp_toString(xs_RegExp *r) {
    xs_str *a = xs_str_concat(r->f_source, r->f_flags);
    xs_str *o = xs_str_alloc(a->len + 2);
    o->data[0] = '/';
    memcpy(o->data + 1, r->f_source->data, (size_t)r->f_source->len);
    o->data[1 + r->f_source->len] = '/';
    memcpy(o->data + 2 + r->f_source->len, r->f_flags->data, (size_t)r->f_flags->len);
    o->data[o->len] = 0;
    xs_release(a);
    return o;
}

/* m[i]: the group's text ("" when it did not take part or past the end, where JS has undefined) */
static inline xs_str *xs_RegExpExecArray_at(xs_RegExpExecArray *m, int64_t i) {
    if ((uint64_t)i >= (uint64_t)m->f_length) return xr_empty_str();
    xs_str *v = xr_item(m, i);
    return v ? v : xr_empty_str();
}
/* m.groups.name */
XS_NOINLINE xs_str *xs_RegExpGroups_get(xs_RegExpGroups *g, xs_str *name) {
    for (int i = 1; i < g->n; i++)
        if (g->re->names[i] && strcmp(g->re->names[i], name->data) == 0) return g->items[i] ? g->items[i] : xr_empty_str();
    return xr_empty_str();
}

/* s.match(re): like exec without g; with g, every matched string (null when none) */
XS_NOINLINE xs_RegExpExecArray *xs_str_match_re(xs_str *s, xs_RegExp *r) {
    if (!(r->re->flags & XF_G)) return xs_RegExp_exec(r, s);
    r->f_lastIndex = 0;
    xs_RegExpExecArray *a = NULL;
    int64_t cap = 0, from = 0;
    xr_m m;
    while (from <= s->len && xr_match_at(r, s, from, r->re->flags & XF_Y, &m, false)) {
        if (!a) {
            a = xs_obj_alloc(sizeof *a, xs_RegExpExecArray_free, xs_RegExpExecArray_print);
            a->f_index = -1;
            a->f_input = xs_retain(s);
        }
        if (a->f_length == cap) { cap = cap ? cap * 2 : 8; a->items = realloc(a->items, sizeof(xs_str *) * (size_t)cap); }
        a->items[a->f_length++] = xs_str_new(s->data + m.ms, (size_t)(m.me - m.ms));
        from = m.me > m.ms ? m.me : xr_next_char(s, m.me);
    }
    return a;
}
static void xs_print_RegExpExecArray_(void *p) { xs_RegExpExecArray_print(p); }
/* s.match(re) ?? []: the match as the string[] it is in JavaScript (null stays null) */
XS_NOINLINE xs_arr_ref *xs_RegExpExecArray_strings(xs_RegExpExecArray *a) {
    if (!a) return NULL;
    xs_arr_ref *out = xs_arr_ref_new((size_t)(a->f_length ? a->f_length : 1), xs_print_str_quoted_);
    for (int64_t i = 0; i < a->f_length; i++) xs_arr_ref_push(out, a->items[i] ? xs_retain(a->items[i]) : xs_str_new("", 0));
    return out;
}
/* s.matchAll(re): every match, with groups and index (the flag g is required, as in JS) */
XS_NOINLINE xs_arr_ref *xs_str_match_all_re(xs_str *s, xs_RegExp *r) {
    if (!(r->re->flags & XF_G)) xr_throw("TypeError", "String.prototype.matchAll called with a non-global RegExp argument");
    xs_arr_ref *a = xs_arr_ref_new(8, xs_print_RegExpExecArray_);
    int64_t from = xr_byte(s, r->f_lastIndex > 0 ? r->f_lastIndex : 0);
    xr_m m;
    while (from <= s->len && xr_match_at(r, s, from, r->re->flags & XF_Y, &m, true)) {
        int64_t next = m.me > m.ms ? m.me : xr_next_char(s, m.me);
        xs_arr_ref_push(a, xr_exec_array(r, s, &m));
        from = next;
    }
    return a;
}
XS_NOINLINE int64_t xs_str_search_re(xs_str *s, xs_RegExp *r) {
    xr_m m;
    if (!xr_match_at(r, s, 0, r->re->flags & XF_Y, &m, false)) return -1;
    return xr_cp(s, m.ms);
}

typedef struct { char *d; size_t n, cap; } xr_buf;
XS_NOINLINE void xr_grow(xr_buf *b, size_t n) {
    b->cap = (b->n + n + 1) * 2;
    b->d = realloc(b->d, b->cap);
    if (!b->d) xs_panic("out of memory");
}
/* replace() puts the text between matches piece by piece, and the pieces are short (12 bytes on average in
   regex-redux, 3.5M per pass): up to 16 bytes go as two fixed-size copies that overlap, not a memcpy call */
static inline __attribute__((always_inline)) void xr_put(xr_buf *b, const char *p, size_t n) {
    if (__builtin_expect(b->n + n + 1 > b->cap, 0)) xr_grow(b, n);
    char *d = b->d + b->n;
    if (n >= 8 && n <= 16) { memcpy(d, p, 8); memcpy(d + n - 8, p + n - 8, 8); }
    else if (n >= 4 && n < 8) { memcpy(d, p, 4); memcpy(d + n - 4, p + n - 4, 4); }
    else if (n < 4) { for (size_t i = 0; i < n; i++) d[i] = p[i]; }
    else memcpy(d, p, n);
    b->n += n;
}
/* m.join(sep): the texts, "" for groups that did not take part (as JS joins undefined) */
XS_NOINLINE xs_str *xs_RegExpExecArray_join(xs_RegExpExecArray *m, xs_str *sep) {
    xr_buf b = {0};
    xr_put(&b, "", 0);
    for (int64_t i = 0; i < m->f_length; i++) {
        if (i) xr_put(&b, sep->data, (size_t)sep->len);
        xs_str *v = xr_item(m, i);
        if (v) xr_put(&b, v->data, (size_t)v->len);
    }
    xs_str *r = xs_str_new(b.d ? b.d : "", b.n);
    free(b.d);
    return r;
}
/* the replacement text with $$, $&, $`, $', $n, $nn and $<name> expanded */
static void xr_expand(xr_buf *b, xs_RegExp *r, xs_str *s, xr_m *m, xs_str *rep) {
    int32_t *c = xr_caps(m);
    int ng = r->re->ngroups;
    const char *p = rep->data, *e = rep->data + rep->len;
    while (p < e) {
        const char *d = memchr(p, '$', (size_t)(e - p));
        if (!d) { xr_put(b, p, (size_t)(e - p)); break; }
        xr_put(b, p, (size_t)(d - p));
        p = d + 1;
        if (p >= e) { xr_put(b, "$", 1); break; }
        char k = *p;
        if (k == '$') { xr_put(b, "$", 1); p++; }
        else if (k == '&') { xr_put(b, s->data + m->ms, (size_t)(m->me - m->ms)); p++; }
        else if (k == '`') { xr_put(b, s->data, (size_t)m->ms); p++; }
        else if (k == '\'') { xr_put(b, s->data + m->me, (size_t)(s->len - m->me)); p++; }
        else if (k >= '0' && k <= '9') {
            int g = k - '0', used = 1;
            if (p + 1 < e && p[1] >= '0' && p[1] <= '9' && g * 10 + (p[1] - '0') <= ng && g * 10 + (p[1] - '0') > 0) { g = g * 10 + (p[1] - '0'); used = 2; }
            if (g >= 1 && g <= ng) {
                if (c[2 * g] >= 0) xr_put(b, s->data + c[2 * g], (size_t)(c[2 * g + 1] - c[2 * g]));
                p += used;
            } else xr_put(b, "$", 1);
        } else if (k == '<' && r->re->names) {
            const char *close = memchr(p, '>', (size_t)(e - p));
            if (!close) { xr_put(b, "$", 1); continue; }
            for (int g = 1; g <= ng; g++) {
                const char *nm = r->re->names[g];
                if (nm && strlen(nm) == (size_t)(close - p - 1) && memcmp(nm, p + 1, (size_t)(close - p - 1)) == 0) {
                    if (c[2 * g] >= 0) xr_put(b, s->data + c[2 * g], (size_t)(c[2 * g + 1] - c[2 * g]));
                }
            }
            p = close + 1;
        } else xr_put(b, "$", 1);
    }
}
/* the replacement a callback gives: (match, p1, ..., pn) => string, called with as many as it declares */
static void xr_call_fn(xr_buf *b, xs_str *s, xr_m *m, xs_closure *cl, int arity, int ng) {
    xs_str *a[8] = {0};
    int32_t *c = xr_caps(m);
    for (int g = 0; g < arity && g < 8; g++) {
        if (g <= ng && c[2 * g] >= 0) a[g] = xs_str_new(s->data + c[2 * g], (size_t)(c[2 * g + 1] - c[2 * g]));
        else a[g] = xr_empty_str();
    }
    if (arity == 0) a[0] = NULL;
    typedef xs_str *(*F)(void *, ...);
    xs_str *r;
    switch (arity) {
    case 0: r = ((xs_str *(*)(void *))cl->fn)(cl); break;
    case 1: r = ((xs_str *(*)(void *, xs_str *))cl->fn)(cl, a[0]); break;
    case 2: r = ((xs_str *(*)(void *, xs_str *, xs_str *))cl->fn)(cl, a[0], a[1]); break;
    case 3: r = ((xs_str *(*)(void *, xs_str *, xs_str *, xs_str *))cl->fn)(cl, a[0], a[1], a[2]); break;
    case 4: r = ((xs_str *(*)(void *, xs_str *, xs_str *, xs_str *, xs_str *))cl->fn)(cl, a[0], a[1], a[2], a[3]); break;
    case 5: r = ((xs_str *(*)(void *, xs_str *, xs_str *, xs_str *, xs_str *, xs_str *))cl->fn)(cl, a[0], a[1], a[2], a[3], a[4]); break;
    case 6: r = ((xs_str *(*)(void *, xs_str *, xs_str *, xs_str *, xs_str *, xs_str *, xs_str *))cl->fn)(cl, a[0], a[1], a[2], a[3], a[4], a[5]); break;
    case 7: r = ((xs_str *(*)(void *, xs_str *, xs_str *, xs_str *, xs_str *, xs_str *, xs_str *, xs_str *))cl->fn)(cl, a[0], a[1], a[2], a[3], a[4], a[5], a[6]); break;
    default: r = ((xs_str *(*)(void *, xs_str *, xs_str *, xs_str *, xs_str *, xs_str *, xs_str *, xs_str *, xs_str *))cl->fn)(cl, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]); break;
    }
    (void)sizeof(F);
    for (int g = 0; g < arity && g < 8; g++) xs_release(a[g]);
    xr_put(b, r->data, (size_t)r->len);
    xs_release(r);
}
static xs_str *xr_replace(xs_str *s, xs_RegExp *r, xs_str *rep, xs_closure *cl, int arity, bool all) {
    bool g = r->re->flags & XF_G;
    if (all && !g) xr_throw("TypeError", "replaceAll must be called with a global RegExp");
    int64_t from = 0;
    if (!g) { if (!xr_start(r, s, &from)) return xs_retain(s); }
    else r->f_lastIndex = 0;
    bool want = cl ? arity > 1 : (rep->len && memchr(rep->data, '$', (size_t)rep->len) != NULL);
    xr_buf b = {0};
    xr_put(&b, "", 0);
    b.n = offsetof(xs_str, data); /* the string's header goes in front: the buffer becomes the result */
    if (b.cap < b.n + (size_t)s->len + 1) { b.cap = b.n + (size_t)s->len + 64; b.d = realloc(b.d, b.cap); }
    int64_t last = 0;
    xr_m m;
    bool any = false;
    while (from <= s->len && xr_match_at(r, s, from, r->re->flags & XF_Y, &m, want)) {
        any = true;
        xr_put(&b, s->data + last, (size_t)(m.ms - last));
        if (cl) xr_call_fn(&b, s, &m, cl, arity, r->re->ngroups);
        else if (want) xr_expand(&b, r, s, &m, rep);
        else xr_put(&b, rep->data, (size_t)rep->len);
        free(m.more);
        last = m.me;
        if (!g) { if (r->re->flags & XF_Y) r->f_lastIndex = xr_cp(s, m.me); break; }
        from = m.me > m.ms ? m.me : xr_next_char(s, m.me);
    }
    if (!any) { free(b.d); if (!g && (r->re->flags & XF_Y)) r->f_lastIndex = 0; return xs_retain(s); }
    xr_put(&b, s->data + last, (size_t)(s->len - last));
    xr_put(&b, "", 1); /* room for the NUL */
    return xs_str_adopt(b.d, b.n - 1 - offsetof(xs_str, data));
}
XS_NOINLINE xs_str *xs_str_replace_re(xs_str *s, xs_RegExp *r, xs_str *rep) { return xr_replace(s, r, rep, NULL, 0, false); }
XS_NOINLINE xs_str *xs_str_replace_all_re(xs_str *s, xs_RegExp *r, xs_str *rep) { return xr_replace(s, r, rep, NULL, 0, true); }
XS_NOINLINE xs_str *xs_str_replace_re_fn(xs_str *s, xs_RegExp *r, xs_closure *cl, int64_t arity) { return xr_replace(s, r, NULL, cl, (int)arity, false); }
XS_NOINLINE xs_str *xs_str_replace_all_re_fn(xs_str *s, xs_RegExp *r, xs_closure *cl, int64_t arity) { return xr_replace(s, r, NULL, cl, (int)arity, true); }

/* s.split(re, limit): the pieces between matches, with the groups of each separator in between */
XS_NOINLINE xs_arr_ref *xs_str_split_re(xs_str *s, xs_RegExp *r, int64_t limit) {
    xs_arr_ref *a = xs_arr_ref_new(8, xs_print_str_quoted_);
    uint64_t lim = limit < 0 ? UINT64_MAX : (uint64_t)limit;
    if (lim == 0) return a;
    xr_m m;
    if (s->len == 0) {
        if (!xr_match_at(r, s, 0, true, &m, false)) xs_arr_ref_push(a, xs_retain(s));
        return a;
    }
    int64_t p = 0, q = 0;
    while (q < s->len) {
        if (!xr_match_at(r, s, q, false, &m, true)) break;
        if (m.ms >= s->len) { free(m.more); break; }
        if (m.me == p || m.me == m.ms && m.ms == p) { free(m.more); q = xr_next_char(s, m.ms); continue; }
        xs_arr_ref_push(a, xs_str_new(s->data + p, (size_t)(m.ms - p)));
        if (a->len == lim) { free(m.more); return a; }
        for (int g = 1; g <= r->re->ngroups; g++) {
            xs_str *gs = xr_group_str(s, &m, g);
            xs_arr_ref_push(a, gs ? gs : xr_empty_str());
            if (a->len == lim) { free(m.more); return a; }
        }
        free(m.more);
        p = m.me;
        q = m.me > m.ms ? p : xr_next_char(s, p);
    }
    xs_arr_ref_push(a, xs_str_new(s->data + p, (size_t)(s->len - p)));
    return a;
}

#endif /* XS_RT_REGEX */
#endif
