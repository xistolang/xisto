/* RegExp with backtracking: the patterns the linear engine (xisto_regex.h) refuses because they need it: backreferences
   (\1, \k<name>), lookahead longer than one character and lookbehind ((?= (?! (?<= (?<!). Every other pattern stays on the
   linear engine; this one joins a program only when the program has such a pattern (or builds one from text at run time).

   The pattern is parsed to a tree (classes and escapes by the linear engine's own parser, so both read a pattern alike) and
   compiled to a program for a backtracking machine: the order in which it tries the ways is JavaScript's (alternatives
   left to right, greedy quantifiers longest first, lazy shortest first, an iteration that matches nothing stops the loop,
   the groups of a quantified atom reset at each iteration). Lookaround runs its part on its own (atomic, as in JS); a
   positive one keeps the groups it set. Lookbehind tries the starts nearest to the position first, its part anchored to
   end there.

   Backtracking can take exponential time ((a+)+b against aaaa...c): a search that passes XB_MAX_STEPS steps stops with
   an error instead of running on (Node would run on).

   The compiled program lives in an xr_re like any other, marked with fwd.n == -1 (its instructions in fwd.ins, their
   count in rev.n, its classes in cls), so a RegExp object, its exec arrays and its groups are the linear engine's; only
   the search differs (xrb_search). This part needs only libc: the compiler includes it to check patterns. */
#ifndef XISTO_REGEX_BT_H
#define XISTO_REGEX_BT_H

#ifndef XB_MAX_STEPS
#define XB_MAX_STEPS 50000000L
#endif

enum { XB_CHAR, XB_CLASS, XB_ANY, XB_SPLIT, XB_JMP, XB_SAVE, XB_RESET, XB_MARK, XB_PROG, XB_ASSERT, XB_BREF, XB_LOOK, XB_MATCH };
enum { XL_AHEAD = 0, XL_BEHIND = 1, XL_NEG = 2 };
typedef struct { uint8_t op; int32_t x, y, z; } xb_inst; /* SPLIT x (first), y; LOOK x kind, y start, z after */

/* the tree */
enum { XT_EMPTY, XT_CHAR, XT_CLASS, XT_ANY, XT_CAT, XT_ALT, XT_REP, XT_GROUP, XT_ASSERT, XT_BREF, XT_NREF, XT_LOOK };
typedef struct { uint8_t kind; bool greedy; int32_t v; int min, max; int a; int next; int last; char *name; } xb_node;

typedef struct {
    xr_parser xp;   /* the linear engine's parser: its escapes and classes */
    xb_node *nd;
    int nn, cap;
    int total_groups;
    xb_inst *ins;
    int ni, icap;
    int nregs;
    const char *err;
} xb_parser;

static int xb_fail(xb_parser *p, const char *m) { if (!p->err) p->err = m; return -1; }
static int xb_new(xb_parser *p, int kind) {
    if (p->nn == p->cap) { p->cap = p->cap ? p->cap * 2 : 64; p->nd = realloc(p->nd, sizeof(xb_node) * (size_t)p->cap); }
    memset(&p->nd[p->nn], 0, sizeof(xb_node));
    p->nd[p->nn].kind = (uint8_t)kind;
    p->nd[p->nn].a = p->nd[p->nn].next = p->nd[p->nn].last = -1;
    return p->nn++;
}
static void xb_append(xb_parser *p, int list, int item) { /* (a CAT or ALT node: its children in order) */
    if (p->nd[list].a < 0) p->nd[list].a = item; else p->nd[p->nd[list].last].next = item;
    p->nd[list].last = item;
}
static inline bool xb_end(xb_parser *p) { return p->xp.i >= p->xp.n; }
static inline int xb_peek(xb_parser *p) { return p->xp.i < p->xp.n ? p->xp.s[p->xp.i] : -1; }

static int xb_alt(xb_parser *p);
static int xb_count_groups(const unsigned char *s, size_t n) { /* the capture groups of the whole pattern (\N reads it) */
    int g = 0;
    bool cls = false;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\') { i++; continue; }
        if (cls) { if (s[i] == ']') cls = false; continue; }
        if (s[i] == '[') { cls = true; continue; }
        if (s[i] == '(') {
            if (i + 1 < n && s[i + 1] == '?') {
                if (i + 2 < n && s[i + 2] == '<' && i + 3 < n && s[i + 3] != '=' && s[i + 3] != '!') g++;
            } else g++;
        }
    }
    return g;
}

static int xb_atom(xb_parser *p) {
    xr_parser *xp = &p->xp;
    int c = xp->s[xp->i];
    switch (c) {
    case '(': {
        xp->i++;
        int group = -1, look = -1;
        char *name = NULL;
        if (xb_peek(p) == '?') {
            int c2 = xp->i + 1 < xp->n ? xp->s[xp->i + 1] : -1;
            if (c2 == ':') xp->i += 2;
            else if (c2 == '=' || c2 == '!') { look = XL_AHEAD | (c2 == '!' ? XL_NEG : 0); xp->i += 2; }
            else if (c2 == '<' && xp->i + 2 < xp->n && (xp->s[xp->i + 2] == '=' || xp->s[xp->i + 2] == '!')) {
                look = XL_BEHIND | (xp->s[xp->i + 2] == '!' ? XL_NEG : 0);
                xp->i += 3;
            } else if (c2 == '<') {
                xp->i += 2;
                size_t st = xp->i;
                while (!xb_end(p) && xp->s[xp->i] != '>') xp->i++;
                if (xb_end(p) || xp->i == st) return xb_fail(p, "invalid group name");
                name = malloc(xp->i - st + 1);
                memcpy(name, xp->s + st, xp->i - st);
                name[xp->i - st] = 0;
                xp->i++;
                group = ++xp->re->ngroups;
            } else return xb_fail(p, "invalid group");
        } else group = ++xp->re->ngroups;
        if (group > 0) {
            xr_re *re = xp->re;
            if (group >= xp->gcap) {
                int old = xp->gcap;
                xp->gcap = group * 2 + 4;
                re->names = realloc(re->names, sizeof(char *) * (size_t)xp->gcap);
                for (int k = old; k < xp->gcap; k++) re->names[k] = NULL;
            }
            if (name) {
                for (int k = 1; k < group; k++) if (re->names[k] && !strcmp(re->names[k], name)) { free(name); return xb_fail(p, "duplicate capture group name"); }
                re->names[group] = name;
            }
        }
        int in = xb_alt(p);
        if (in < 0) return -1;
        if (xb_peek(p) != ')') return xb_fail(p, "unterminated group");
        xp->i++;
        if (look >= 0) { int n = xb_new(p, XT_LOOK); p->nd[n].v = look; p->nd[n].a = in; return n; }
        if (group < 0) return in;
        int n = xb_new(p, XT_GROUP);
        p->nd[n].v = group;
        p->nd[n].a = in;
        return n;
    }
    case ')': return xb_fail(p, "unmatched ')'");
    case '[': {
        xp->i++;
        int k = xr_parse_class(xp);
        if (k < 0) return xb_fail(p, xp->err ? xp->err : "invalid class");
        int n = xb_new(p, XT_CLASS);
        p->nd[n].v = xp->nd[k].v;
        return n;
    }
    case '.': { xp->i++; int n = xb_new(p, XT_ANY); p->nd[n].v = (xp->flags & XF_S) ? 1 : 0; return n; }
    case '^': case '$': {
        xp->i++;
        int n = xb_new(p, XT_ASSERT);
        p->nd[n].v = c == '^' ? ((xp->flags & XF_M) ? XA_BOL : XA_BOT) : ((xp->flags & XF_M) ? XA_EOL : XA_EOT);
        return n;
    }
    case '\\': {
        xp->i++;
        if (xb_end(p)) return xb_fail(p, "\\ at end of pattern");
        int d = xp->s[xp->i];
        if (d >= '1' && d <= '9') { /* \N: a backreference when the pattern has N groups (else an identity escape, Annex B) */
            size_t st = xp->i;
            long v = 0;
            while (!xb_end(p) && xp->s[xp->i] >= '0' && xp->s[xp->i] <= '9' && v < 100000) v = v * 10 + (xp->s[xp->i++] - '0');
            if (v <= p->total_groups) { int n = xb_new(p, XT_BREF); p->nd[n].v = (int)v; return n; }
            if (xp->flags & XF_U) return xb_fail(p, "invalid escape: a backreference to a group the pattern does not have");
            xp->i = st;
            if (d >= '8') { xp->i++; int n = xb_new(p, XT_CHAR); p->nd[n].v = d; return n; }
            return xb_fail(p, "octal escapes are not supported");
        }
        if (d == 'k' && xb_peek(p) == 'k' && xp->i + 1 < xp->n && xp->s[xp->i + 1] == '<') {
            xp->i += 2;
            size_t st = xp->i;
            while (!xb_end(p) && xp->s[xp->i] != '>') xp->i++;
            if (xb_end(p) || xp->i == st) return xb_fail(p, "invalid named reference");
            int n = xb_new(p, XT_NREF);
            p->nd[n].name = malloc(xp->i - st + 1);
            memcpy(p->nd[n].name, xp->s + st, xp->i - st);
            p->nd[n].name[xp->i - st] = 0;
            xp->i++;
            return n;
        }
        uint32_t cp = 0;
        xr_rset set = {0};
        xr_re *re = xp->re;
        char **names = re->names;
        re->names = NULL; /* (the linear parser refuses \k when the pattern has names: \k without <...> is a letter here) */
        int k = xr_escape(xp, false, &cp, &set);
        re->names = names;
        if (k < 0) return xb_fail(p, xp->err ? xp->err : "invalid escape");
        if (k == 1) { int n = xb_new(p, XT_CLASS); p->nd[n].v = xr_class_new(xp, &set); return n; }
        if (k == 2) { int n = xb_new(p, XT_ASSERT); p->nd[n].v = (int32_t)cp; return n; }
        int n = xb_new(p, XT_CHAR);
        p->nd[n].v = (int32_t)cp;
        return n;
    }
    case '*': case '+': case '?': return xb_fail(p, "nothing to repeat");
    case '{': {
        int mn, mx;
        size_t end;
        if (xr_is_quant_brace(xp, xp->i, &mn, &mx, &end)) return xb_fail(p, "nothing to repeat");
        xp->i++;
        int n = xb_new(p, XT_CHAR);
        p->nd[n].v = '{';
        return n;
    }
    default: {
        uint32_t cp = xr_next_cp(xp);
        int n = xb_new(p, XT_CHAR);
        p->nd[n].v = (int32_t)cp;
        return n;
    }
    }
}

static int xb_seq(xb_parser *p) {
    int cat = xb_new(p, XT_CAT);
    while (!xb_end(p) && xb_peek(p) != '|' && xb_peek(p) != ')') {
        int a = xb_atom(p);
        if (a < 0) return -1;
        for (;;) { /* quantifiers */
            int q = xb_peek(p), mn, mx;
            size_t end = 0;
            if (q == '*') { mn = 0; mx = -1; p->xp.i++; }
            else if (q == '+') { mn = 1; mx = -1; p->xp.i++; }
            else if (q == '?') { mn = 0; mx = 1; p->xp.i++; }
            else if (q == '{' && xr_is_quant_brace(&p->xp, p->xp.i, &mn, &mx, &end)) {
                if (mx >= 0 && mx < mn) return xb_fail(p, "numbers out of order in {} quantifier");
                p->xp.i = end;
            } else break;
            int k = p->nd[a].kind;
            if (k == XT_ASSERT || (k == XT_LOOK && (p->nd[a].v & XL_BEHIND))) return xb_fail(p, "nothing to repeat");
            int r = xb_new(p, XT_REP);
            p->nd[r].min = mn;
            p->nd[r].max = mx;
            p->nd[r].greedy = true;
            if (xb_peek(p) == '?') { p->nd[r].greedy = false; p->xp.i++; }
            p->nd[r].a = a;
            a = r;
            break;
        }
        xb_append(p, cat, a);
    }
    return cat;
}
static int xb_alt(xb_parser *p) {
    int first = xb_seq(p);
    if (first < 0 || xb_peek(p) != '|') return first;
    int alt = xb_new(p, XT_ALT);
    xb_append(p, alt, first);
    while (xb_peek(p) == '|') {
        p->xp.i++;
        int s = xb_seq(p);
        if (s < 0) return -1;
        xb_append(p, alt, s);
    }
    return alt;
}

/* ---------- code ---------- */

static int xb_emit(xb_parser *p, int op, int x, int y) {
    if (p->ni >= 400000) { xb_fail(p, "regular expression too large"); return p->ni - 1; }
    if (p->ni == p->icap) { p->icap = p->icap ? p->icap * 2 : 64; p->ins = realloc(p->ins, sizeof(xb_inst) * (size_t)p->icap); }
    p->ins[p->ni] = (xb_inst){(uint8_t)op, x, y, 0};
    return p->ni++;
}
static void xb_groups_in(xb_parser *p, int n, int *lo, int *hi) { /* the capture groups inside a node */
    if (n < 0) return;
    if (p->nd[n].kind == XT_GROUP) {
        int g = p->nd[n].v;
        if (*lo < 0 || g < *lo) *lo = g;
        if (g > *hi) *hi = g;
    }
    if (p->nd[n].kind == XT_CAT || p->nd[n].kind == XT_ALT) { for (int c = p->nd[n].a; c >= 0; c = p->nd[c].next) xb_groups_in(p, c, lo, hi); }
    else xb_groups_in(p, p->nd[n].a, lo, hi);
}
static void xb_gen(xb_parser *p, int n);
static void xb_iteration(xb_parser *p, int body, int glo, int ghi, bool check) { /* one iteration of a quantified atom */
    int reg = -1;
    if (check) { reg = p->nregs++; xb_emit(p, XB_MARK, reg, 0); }
    if (glo > 0) xb_emit(p, XB_RESET, 2 * glo, 2 * ghi + 1);
    xb_gen(p, body);
    if (check) xb_emit(p, XB_PROG, reg, 0);
}
static void xb_gen(xb_parser *p, int n) {
    if (p->err) return;
    xb_node *d = &p->nd[n];
    switch (d->kind) {
    case XT_EMPTY: return;
    case XT_CHAR: xb_emit(p, XB_CHAR, d->v, 0); return;
    case XT_CLASS: xb_emit(p, XB_CLASS, d->v, 0); return;
    case XT_ANY: xb_emit(p, XB_ANY, d->v, 0); return;
    case XT_ASSERT: xb_emit(p, XB_ASSERT, d->v, 0); return;
    case XT_BREF: xb_emit(p, XB_BREF, d->v, 0); return;
    case XT_NREF: {
        int g = -1;
        for (int k = 1; k <= p->xp.re->ngroups; k++) if (p->xp.re->names && p->xp.re->names[k] && !strcmp(p->xp.re->names[k], d->name)) g = k;
        if (g < 0) { xb_fail(p, "invalid named reference: no group of that name"); return; }
        xb_emit(p, XB_BREF, g, 0);
        return;
    }
    case XT_CAT: for (int c = d->a; c >= 0; c = p->nd[c].next) xb_gen(p, c); return;
    case XT_ALT: {
        int *jumps = NULL, nj = 0;
        for (int c = d->a; c >= 0; c = p->nd[c].next) {
            if (p->nd[c].next >= 0) {
                int sp = xb_emit(p, XB_SPLIT, 0, 0);
                p->ins[sp].x = p->ni;
                xb_gen(p, c);
                jumps = realloc(jumps, sizeof(int) * (size_t)(nj + 1));
                jumps[nj++] = xb_emit(p, XB_JMP, 0, 0);
                p->ins[sp].y = p->ni;
            } else xb_gen(p, c);
        }
        for (int k = 0; k < nj; k++) p->ins[jumps[k]].x = p->ni;
        free(jumps);
        return;
    }
    case XT_GROUP:
        xb_emit(p, XB_SAVE, 2 * d->v, 0);
        xb_gen(p, d->a);
        xb_emit(p, XB_SAVE, 2 * d->v + 1, 0);
        return;
    case XT_LOOK: {
        int lk = xb_emit(p, XB_LOOK, d->v, 0);
        p->ins[lk].y = p->ni;
        xb_gen(p, d->a);
        xb_emit(p, XB_MATCH, 0, 0);
        p->ins[lk].z = p->ni;
        return;
    }
    case XT_REP: {
        int glo = -1, ghi = -1;
        xb_groups_in(p, d->a, &glo, &ghi);
        int body = d->a;
        for (int k = 0; k < d->min && !p->err; k++) xb_iteration(p, body, glo, ghi, false);
        if (d->max < 0) { /* L: split body, out; body; jmp L */
            int L = xb_emit(p, XB_SPLIT, 0, 0);
            int b = p->ni;
            xb_iteration(p, body, glo, ghi, true);
            xb_emit(p, XB_JMP, L, 0);
            p->ins[L].x = d->greedy ? b : p->ni;
            p->ins[L].y = d->greedy ? p->ni : b;
            return;
        }
        int *outs = NULL, no = 0;
        for (int k = d->min; k < d->max && !p->err; k++) {
            int sp = xb_emit(p, XB_SPLIT, 0, 0);
            int b = p->ni;
            outs = realloc(outs, sizeof(int) * (size_t)(no + 1));
            outs[no++] = sp;
            p->ins[sp].x = b; /* (patched below: greedy takes the body first) */
            xb_iteration(p, body, glo, ghi, true);
        }
        for (int k = 0; k < no; k++) {
            int b = outs[k] + 1;
            p->ins[outs[k]].x = d->greedy ? b : p->ni;
            p->ins[outs[k]].y = d->greedy ? p->ni : b;
        }
        free(outs);
        return;
    }
    }
}

/* the pattern compiled for backtracking, in an xr_re marked fwd.n == -1; NULL with *err when the pattern is invalid */
static xr_re *xrb_compile(const char *pat, size_t n, int flags, const char **err) {
    xb_parser p;
    memset(&p, 0, sizeof p);
    xr_re *re = calloc(1, sizeof(xr_re));
    re->rc = 1;
    re->flags = flags;
    p.xp.s = (const unsigned char *)pat;
    p.xp.n = n;
    p.xp.flags = flags;
    p.xp.re = re;
    p.total_groups = xb_count_groups((const unsigned char *)pat, n);
    int root = xb_alt(&p);
    if (root >= 0 && !xb_end(&p)) xb_fail(&p, xb_peek(&p) == ')' ? "unmatched ')'" : "unexpected character");
    if (root >= 0 && !p.err) { xb_gen(&p, root); xb_emit(&p, XB_MATCH, 0, 0); }
    for (int i = 0; i < p.nn; i++) free(p.nd[i].name);
    free(p.nd);
    free(p.xp.nd);
    if (p.err || root < 0) {
        *err = p.err ? p.err : "invalid regular expression";
        free(p.ins);
        if (re->names) for (int i = 0; i < p.xp.gcap; i++) free(re->names[i]);
        free(re->names);
        re->names = NULL;
        re->ngroups = 0;
        for (int i = 0; i < re->ncls; i++) free(re->cls[i].r);
        free(re->cls);
        free(re);
        return NULL;
    }
    if (re->names) { /* (named groups: the array covers 0..ngroups, as the linear engine's) */
        bool any = false;
        for (int i = 1; i <= re->ngroups && i < p.xp.gcap; i++) if (re->names[i]) any = true;
        if (!any) { free(re->names); re->names = NULL; }
        else if (p.xp.gcap < re->ngroups + 1) {
            re->names = realloc(re->names, sizeof(char *) * (size_t)(re->ngroups + 1));
            for (int i = p.xp.gcap; i <= re->ngroups; i++) re->names[i] = NULL;
        }
    }
    re->fwd.n = -1;
    re->fwd.ins = (xr_inst *)(void *)p.ins; /* (freed as the linear engine frees its program) */
    re->rev.n = p.ni;
    re->rev.cap = p.nregs;
    return re;
}

/* ---------- matching ---------- */

static uint32_t xb_fold(uint32_t c) { /* simple case folding over the blocks the linear engine folds */
    if (c >= 'A' && c <= 'Z') return c + 32;
    if ((c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xDE)) return c + 32;
    if ((c >= 0x391 && c <= 0x3A1) || (c >= 0x3A3 && c <= 0x3AB)) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}
static inline bool xb_nl(uint32_t c) { return c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029; }
static inline bool xb_word(uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
static inline size_t xb_back(const unsigned char *s, size_t pos) { /* the start of the character before pos */
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && (s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

typedef struct {
    const xr_re *re;
    const xb_inst *ins;
    const unsigned char *s;
    size_t n;
    int ncaps, nregs;
    long steps;
    /* the backtracking stack: (pc, pos) and a copy of the groups and loop marks below each */
    int32_t *st;
    size_t sn, scap;
} xb_run;

static bool xb_assert(xb_run *r, int kind, size_t pos) {
    const unsigned char *s = r->s;
    size_t n = r->n, l;
    uint32_t prev = pos > 0 ? xr_utf8(s + xb_back(s, pos), n - xb_back(s, pos), &l) : 0;
    uint32_t cur = pos < n ? xr_utf8(s + pos, n - pos, &l) : 0;
    switch (kind) {
    case XA_BOT: return pos == 0;
    case XA_EOT: return pos == n;
    case XA_BOL: return pos == 0 || xb_nl(prev);
    case XA_EOL: return pos == n || xb_nl(cur);
    case XA_WB: return (pos > 0 && xb_word(prev)) != (pos < n && xb_word(cur));
    case XA_NWB: return (pos > 0 && xb_word(prev)) == (pos < n && xb_word(cur));
    default: return false;
    }
}
static void xb_push(xb_run *r, int pc, size_t pos, const int32_t *caps, const int32_t *regs) {
    size_t need = (size_t)(2 + r->ncaps + r->nregs);
    if (r->sn + need > r->scap) { r->scap = (r->sn + need) * 2 + 64; r->st = realloc(r->st, sizeof(int32_t) * r->scap); }
    int32_t *e = r->st + r->sn;
    e[0] = pc;
    e[1] = (int32_t)pos;
    memcpy(e + 2, caps, sizeof(int32_t) * (size_t)r->ncaps);
    if (r->nregs) memcpy(e + 2 + r->ncaps, regs, sizeof(int32_t) * (size_t)r->nregs);
    r->sn += need;
}

/* runs the program from pc at pos; true when it reaches MATCH (at end, when end >= 0); the groups are in caps;
   -1: too many steps */
static int xb_exec(xb_run *r, int pc, size_t pos, long end, int32_t *caps, int32_t *regs, size_t *out) {
    const xb_inst *ins = r->ins;
    const unsigned char *s = r->s;
    size_t n = r->n;
    bool icase = r->re->flags & XF_I;
    size_t base = r->sn;
    size_t frame = (size_t)(2 + r->ncaps + r->nregs);
    for (;;) {
        if (++r->steps > XB_MAX_STEPS) { r->sn = base; return -1; }
        const xb_inst *in = &ins[pc];
        size_t l;
        switch (in->op) {
        case XB_CHAR: {
            if (pos >= n) goto fail;
            uint32_t c = xr_utf8(s + pos, n - pos, &l);
            if (c != (uint32_t)in->x && !(icase && xb_fold(c) == xb_fold((uint32_t)in->x))) goto fail;
            pos += l;
            pc++;
            continue;
        }
        case XB_CLASS: {
            if (pos >= n) goto fail;
            uint32_t c = xr_utf8(s + pos, n - pos, &l);
            const xr_class *k = &r->re->cls[in->x];
            if (!xr_class_has(k, c) && !(icase && xr_class_has(k, xb_fold(c)))) goto fail;
            pos += l;
            pc++;
            continue;
        }
        case XB_ANY: {
            if (pos >= n) goto fail;
            uint32_t c = xr_utf8(s + pos, n - pos, &l);
            if (!in->x && xb_nl(c)) goto fail;
            pos += l;
            pc++;
            continue;
        }
        case XB_SPLIT: xb_push(r, in->y, pos, caps, regs); pc = in->x; continue;
        case XB_JMP: pc = in->x; continue;
        case XB_SAVE: caps[in->x] = (int32_t)pos; pc++; continue;
        case XB_RESET: for (int k = in->x; k <= in->y && k < r->ncaps; k++) caps[k] = -1; pc++; continue;
        case XB_MARK: regs[in->x] = (int32_t)pos; pc++; continue;
        case XB_PROG: if (regs[in->x] == (int32_t)pos) goto fail; pc++; continue; /* (an iteration that matched nothing) */
        case XB_ASSERT: if (!xb_assert(r, in->x, pos)) goto fail; pc++; continue;
        case XB_BREF: {
            int g = in->x;
            if (2 * g + 1 >= r->ncaps || caps[2 * g] < 0 || caps[2 * g + 1] < 0) { pc++; continue; } /* (an unset group: empty) */
            size_t a = (size_t)caps[2 * g], b = (size_t)caps[2 * g + 1], len = b - a;
            if (!icase) {
                if (pos + len > n || memcmp(s + a, s + pos, len) != 0) goto fail;
                pos += len;
            } else {
                size_t q = pos, i = a;
                while (i < b) {
                    size_t l1, l2;
                    if (q >= n) goto fail;
                    uint32_t c1 = xr_utf8(s + i, n - i, &l1), c2 = xr_utf8(s + q, n - q, &l2);
                    if (xb_fold(c1) != xb_fold(c2)) goto fail;
                    i += l1;
                    q += l2;
                }
                pos = q;
            }
            pc++;
            continue;
        }
        case XB_LOOK: {
            int kind = in->x;
            int32_t *c2 = malloc(sizeof(int32_t) * (size_t)(r->ncaps + r->nregs + 1));
            int32_t *g2 = c2 + r->ncaps;
            int got = 0;
            size_t dummy;
            if (!(kind & XL_BEHIND)) {
                memcpy(c2, caps, sizeof(int32_t) * (size_t)r->ncaps);
                if (r->nregs) memcpy(g2, regs, sizeof(int32_t) * (size_t)r->nregs);
                got = xb_exec(r, in->y, pos, -1, c2, g2, &dummy);
            } else { /* the starts nearest to pos first, the part anchored to end at pos */
                size_t q = pos;
                for (;;) {
                    memcpy(c2, caps, sizeof(int32_t) * (size_t)r->ncaps);
                    if (r->nregs) memcpy(g2, regs, sizeof(int32_t) * (size_t)r->nregs);
                    got = xb_exec(r, in->y, q, (long)pos, c2, g2, &dummy);
                    if (got != 0 || q == 0) break;
                    q = xb_back(s, q);
                }
            }
            if (got < 0) { free(c2); r->sn = base; return -1; }
            bool ok = (got == 1) != ((kind & XL_NEG) != 0);
            if (ok && got == 1) memcpy(caps, c2, sizeof(int32_t) * (size_t)r->ncaps); /* (a positive one keeps its groups) */
            free(c2);
            if (!ok) goto fail;
            pc = in->z;
            continue;
        }
        case XB_MATCH:
            if (end >= 0 && (long)pos != end) goto fail;
            *out = pos;
            r->sn = base;
            return 1;
        }
    fail:
        if (r->sn == base) return 0;
        r->sn -= frame;
        {
            int32_t *e = r->st + r->sn;
            pc = e[0];
            pos = (size_t)e[1];
            memcpy(caps, e + 2, sizeof(int32_t) * (size_t)r->ncaps);
            if (r->nregs) memcpy(regs, e + 2 + r->ncaps, sizeof(int32_t) * (size_t)r->nregs);
        }
    }
}

/* the leftmost match at or after from (only at from when sticky): 1 with caps[0..2 * (ngroups + 1)) filled, 0 none,
   -1 when the search passed XB_MAX_STEPS */
__attribute__((unused)) static int xrb_search(const xr_re *re, const char *data, size_t n, size_t from, bool sticky, int32_t *caps) {
    xb_run r;
    memset(&r, 0, sizeof r);
    r.re = re;
    r.ins = (const xb_inst *)(const void *)re->fwd.ins;
    r.s = (const unsigned char *)data;
    r.n = n;
    r.ncaps = 2 * (re->ngroups + 1);
    r.nregs = re->rev.cap;
    int32_t *regs = calloc((size_t)(r.nregs + 1), sizeof(int32_t));
    int res = 0;
    for (size_t at = from; at <= n;) {
        for (int k = 0; k < r.ncaps; k++) caps[k] = -1;
        size_t end;
        int got = xb_exec(&r, 0, at, -1, caps, regs, &end);
        if (got < 0) { res = -1; break; }
        if (got) { caps[0] = (int32_t)at; caps[1] = (int32_t)end; res = 1; break; }
        if (sticky || at >= n) break;
        at++;
        while (at < n && (r.s[at] & 0xC0) == 0x80) at++;
    }
    free(regs);
    free(r.st);
    return res;
}

/* ---------- the RegExp glue (the runtime: xr_match_at and the compile of xisto_regex.h call these) ---------- */
#ifndef XR_CORE_ONLY
static xr_re *xr_compile_any(const char *src, size_t n, int f, const char **err) {
    xr_re *re = xr_compile(src, n, f, err);
    if (re) return re;
    if (*err && strstr(*err, "backtracking")) { /* (what only backtracking can match: this engine) */
        const char *e2 = NULL;
        re = xrb_compile(src, n, f, &e2);
        if (re) return re;
        *err = e2;
    }
    return NULL;
}
static bool xrb_match_at(xs_RegExp *r, xs_str *s, int64_t from, bool sticky, xr_m *m, bool want_caps) {
    (void)want_caps;
    m->more = NULL;
    xr_re *re = r->re;
    int ns = 2 * (re->ngroups + 1);
    int32_t *c = ns <= 64 ? m->caps : (m->more = malloc(sizeof(int32_t) * (size_t)ns));
    int got = xrb_search(re, s->data, (size_t)s->len, (size_t)from, sticky, c);
    if (got <= 0) {
        free(m->more);
        m->more = NULL;
        if (got < 0) {
            char msg[300];
            snprintf(msg, sizeof msg, "Maximum backtracking steps exceeded in regular expression /%.200s/: the pattern backtracks too much on this text", r->f_source ? r->f_source->data : "");
            xr_throw("RangeError", msg);
        }
        return false;
    }
    m->ms = c[0];
    m->me = c[1];
    return true;
}
#endif
#endif
