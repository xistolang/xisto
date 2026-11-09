/* pad4: the fourth lane of the x y z columns (x86 with AVX2).

   With xs_pad4, columns.c keeps the fields Px Py Pz of a class as one column of 4 lanes per element (lane 3 holds
   0). A computation written three times, once per coordinate, as natural code does

     const dx = X__col_c4_xyz[4*i+0] - X__col_c4_xyz[4*j+0];   (and dy with +1, dz with +2)
     V__col_c4_vxyz[4*i+0] -= dx * mj;                         (and the same with dy +1, dz +2)

   gets the same line once more for lane 3, right after the third:

     const dx__l3 = X__col_c4_xyz[4*i+3] - X__col_c4_xyz[4*j+3];
     V__col_c4_vxyz[4*i+3] -= dx__l3 * mj;

   so the four lines are four lanes of one vector operation, which the C compiler emits as one AVX2 instruction (as
   nbody-5 does by hand). Lane 3 is never read by the program: it is written only by these lines and starts at 0, so
   the program's results are the same doubles. A line is copied only when it is a const or an assignment to lane 0 of
   such a column, whose second and third copies are the same expression one and two lanes over, and its expression
   only reads (numbers, names, + - * /, indexes): its fourth copy cannot throw or change anything else.
   XISTO_NO_PAD4=1 turns it off (columns.c then keeps one column per field). */
#include "xisto.h"

typedef struct {
    const char *n[4]; /* the trio's names and its lane-3 name */
} Trio;

typedef struct {
    Trio *t;
    int len, cap;
    bool fail;
} Ctx;

static bool is_c4(Expr *e) { return e && e->kind == E_IDENT && e->str && strstr(e->str, "__col_c4_"); }

/* 4 * I + q: the lane q (else -1) */
static int lane_of(Expr *ix) {
    if (!ix || ix->kind != E_BINARY || ix->op != TK_PLUS || !ix->rhs || ix->rhs->kind != E_NUMBER || !ix->rhs->is_int_lit) return -1;
    Expr *m = ix->lhs;
    if (!m || m->kind != E_BINARY || m->op != TK_STAR || !m->lhs || m->lhs->kind != E_NUMBER || !m->lhs->is_int_lit || m->lhs->ival != 4) return -1;
    return (int)ix->rhs->ival;
}

static int trio_member(Ctx *c, const char *name, int *which) {
    for (int i = 0; i < c->len; i++)
        for (int k = 0; k < 4; k++)
            if (strcmp(c->t[i].n[k], name) == 0) { *which = k; return i; }
    return -1;
}

/* a copy of e with lane 0 moved to lane L (columns and trio names); NULL: not a pure read, or not lane 0 */
static Expr *shift(Ctx *c, Expr *e, int L, bool *touched) {
    if (!e) return NULL;
    Expr *r = xs_alloc(sizeof *r);
    *r = *e;
    switch (e->kind) {
    case E_NUMBER: return r;
    case E_IDENT: {
        int k, t = trio_member(c, e->str, &k);
        if (t < 0) return r;
        if (k != 0) return NULL;
        r->str = (char *)c->t[t].n[L];
        *touched = true;
        return r;
    }
    case E_UNARY:
        if (e->op != TK_MINUS) return NULL;
        return (r->lhs = shift(c, e->lhs, L, touched)) ? r : NULL;
    case E_BINARY:
        if (e->op != TK_PLUS && e->op != TK_MINUS && e->op != TK_STAR && e->op != TK_SLASH) return NULL;
        if (!(r->lhs = shift(c, e->lhs, L, touched)) || !(r->rhs = shift(c, e->rhs, L, touched))) return NULL;
        return r;
    case E_INDEX:
        if (is_c4(e->lhs)) {
            if (lane_of(e->rhs) != 0) return NULL;
            Expr *ix = xs_alloc(sizeof *ix), *q = xs_alloc(sizeof *q);
            *ix = *e->rhs;
            *q = *e->rhs->rhs;
            q->ival = L;
            q->num = L;
            ix->rhs = q;
            r->rhs = ix;
            *touched = true;
            return r;
        }
        if (!e->lhs || e->lhs->kind != E_IDENT) return NULL;
        return (r->rhs = shift(c, e->rhs, L, touched)) ? r : NULL;
    default: return NULL;
    }
}

static bool same(Expr *a, Expr *b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case E_NUMBER: return a->is_int_lit == b->is_int_lit && (a->is_int_lit ? a->ival == b->ival : a->num == b->num);
    case E_IDENT: return strcmp(a->str, b->str) == 0;
    case E_UNARY: return a->op == b->op && same(a->lhs, b->lhs);
    case E_BINARY: return a->op == b->op && same(a->lhs, b->lhs) && same(a->rhs, b->rhs);
    case E_INDEX: return same(a->lhs, b->lhs) && same(a->rhs, b->rhs);
    case E_ASSIGN: return a->op == b->op && same(a->lhs, b->lhs) && same(a->rhs, b->rhs);
    default: return false;
    }
}

/* the expression of a line, as one tree: const name = init, or the assignment */
static Expr *line_expr(Stmt *s, bool *is_var) {
    *is_var = false;
    if (s->kind == S_VAR && s->is_const && s->init && !s->for_head) { *is_var = true; return s->init; }
    if (s->kind == S_EXPR && s->expr && s->expr->kind == E_ASSIGN) return s->expr;
    return NULL;
}

static Expr *shift_line(Ctx *c, Expr *e, bool is_var, int L, bool *touched) {
    if (is_var) return shift(c, e, L, touched);
    /* an assignment: its target lane 0 of a column, its value a pure read */
    if (!e->lhs || e->lhs->kind != E_INDEX || !is_c4(e->lhs->lhs)) return NULL;
    Expr *r = xs_alloc(sizeof *r);
    *r = *e;
    if (!(r->lhs = shift(c, e->lhs, L, touched)) || !(r->rhs = shift(c, e->rhs, L, touched))) return NULL;
    return r;
}

static void walk_stmt(Ctx *c, Stmt *s);
static void walk_list(Ctx *c, StmtList *l) {
    for (int i = 0; i + 2 < l->len; i++) {
        Stmt *a = l->items[i], *b = l->items[i + 1], *d = l->items[i + 2];
        bool va, vb, vd;
        Expr *ea = line_expr(a, &va), *eb = line_expr(b, &vb), *ed = line_expr(d, &vd);
        if (!ea || !eb || !ed || va != vb || va != vd) continue;
        bool touched = false;
        Expr *s1 = shift_line(c, ea, va, 1, &touched);
        if (!s1 || !touched || !same(s1, eb)) continue;
        Expr *s2 = shift_line(c, ea, va, 2, &touched);
        if (!s2 || !same(s2, ed)) continue;
        if (va) { /* a trio of consts: its names, and the lane-3 one */
            if (c->len == c->cap) {
                c->cap = c->cap ? 2 * c->cap : 8;
                Trio *nt = xs_alloc(sizeof(Trio) * (size_t)c->cap); if (c->len) memcpy(nt, c->t, sizeof(Trio) * (size_t)c->len); c->t = nt;
            }
            Trio *t = &c->t[c->len];
            t->n[0] = a->name;
            t->n[1] = b->name;
            t->n[2] = d->name;
            t->n[3] = xs_sprintf("%s__l3", a->name);
            int k;
            if (trio_member(c, a->name, &k) >= 0 || trio_member(c, b->name, &k) >= 0 || trio_member(c, d->name, &k) >= 0)
                continue; /* a name already in a trio (another scope): leave it */
            c->len++;
        }
        Expr *s3 = shift_line(c, ea, va, 3, &touched);
        if (!s3) { if (va) c->len--; continue; }
        Stmt *n = xs_alloc(sizeof *n);
        *n = *a;
        if (va) {
            n->name = (char *)c->t[c->len - 1].n[3];
            n->init = s3;
            n->cname = NULL;
        } else n->expr = s3;
        stmt_list_push(l, NULL);
        for (int j = l->len - 1; j > i + 3; j--) l->items[j] = l->items[j - 1];
        l->items[i + 3] = n;
        i += 3;
    }
    for (int i = 0; i < l->len; i++) walk_stmt(c, l->items[i]);
}

static void walk_stmt(Ctx *c, Stmt *s) {
    if (!s) return;
    if (s->kind == S_BLOCK) walk_list(c, &s->stmts);
    else if (s->kind == S_IF) {
        walk_stmt(c, s->body);
        walk_stmt(c, s->else_body);
    } else if (s->kind == S_FOR || s->kind == S_WHILE || s->kind == S_FOROF) walk_stmt(c, s->body);
}

/* a lane-3 const no padded line reads (energy() pads dx dy dz but sums their squares): dropped again */
static bool mentions_e(Expr *e, const char *n) {
    if (!e) return false;
    if (e->kind == E_IDENT && e->str && strcmp(e->str, n) == 0) return true;
    if (mentions_e(e->lhs, n) || mentions_e(e->rhs, n)) return true;
    for (int i = 0; i < e->args.len; i++) if (mentions_e(e->args.items[i], n)) return true;
    return false;
}
static bool mentions_s(Stmt *s, const char *n) {
    if (!s) return false;
    if (mentions_e(s->init, n) || mentions_e(s->expr, n) || mentions_e(s->cond, n) || mentions_e(s->for_update, n)) return true;
    if (mentions_s(s->for_init, n) || mentions_s(s->body, n) || mentions_s(s->else_body, n)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (mentions_s(s->stmts.items[i], n)) return true;
    for (int i = 0; i < s->ncases; i++) if (mentions_s(s->cases[i].body, n)) return true;
    return false;
}
static void drop_unused(Stmt *root, Stmt *s, const char *n) {
    if (!s) return;
    if (s->kind == S_BLOCK) {
        for (int i = 0; i < s->stmts.len; i++) {
            Stmt *d = s->stmts.items[i];
            if (d->kind == S_VAR && d->name && strcmp(d->name, n) == 0) {
                d->name = NULL; /* (so the search below does not count the declaration itself) */
                bool used = mentions_s(root, n);
                d->name = (char *)n;
                if (!used) {
                    for (int j = i; j + 1 < s->stmts.len; j++) s->stmts.items[j] = s->stmts.items[j + 1];
                    s->stmts.len--;
                }
                return;
            }
            drop_unused(root, d, n);
        }
    } else if (s->kind == S_IF) {
        drop_unused(root, s->body, n);
        drop_unused(root, s->else_body, n);
    } else if (s->kind == S_FOR || s->kind == S_WHILE || s->kind == S_FOROF) drop_unused(root, s->body, n);
}

void pad4_program(Program *p) {
    if (!xs_pad4) return;
    for (int i = 0; i < p->nfuncs; i++) {
        Func *f = p->funcs[i];
        if (!f || !f->body || !f->mod || f->mod->builtin || f->ntparams || f->template_of || f->is_gen) continue;
        Ctx c = {0};
        walk_stmt(&c, f->body);
        for (int t = 0; t < c.len; t++) drop_unused(f->body, f->body, c.t[t].n[3]);
    }
}
