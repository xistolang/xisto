/* xisto: distribution of a pair loop over columns (runs after columns.c, before the checker)

   A loop over the pairs of a small fixed set held as columns (columns.c),
       for (let i = 0; i < K; i++) for (let j = i + 1; j < K; j++) { const a = E1; const b = E2; ...; stores }
   where some of the consts are pure (they read only column elements at i or j that the loop never writes, values
   from outside the loop it never writes, literals and earlier pure consts, through + - * / and Math.sqrt) and
   take long (a sqrt or a division), becomes two passes:
       pass 1: for every pair, those consts, each into a small array (one slot per pair)
       pass 2: the original loop, each such const read back from its slot.
   Every value is computed by the same expression from the same operands: the same doubles. What changes is the
   order the instructions reach the processor: the ten square roots and divisions of n-body's step no longer sit
   each behind its pair's six velocity updates, so a core with a small out-of-order window (Graviton2) overlaps
   them, as the hand-written #4 does with its three passes over the pairs.
   Why it is exact beyond the doubles: the pure consts cannot throw (columns.c proved every index in range) and
   have no effect, and the loop's stores cannot change what they read (the arrays and names they read are not
   written in the loop). Off with XISTO_NO_DISTRIBUTE=1. */
#include "xisto.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int dist_id;

static bool is_ident(Expr *e, const char *name) { return e && e->kind == E_IDENT && e->str && (!name || strcmp(e->str, name) == 0); }
static bool int_lit(Expr *e, long long *v) {
    if (!e || e->kind != E_NUMBER || !e->is_int_lit) return false;
    *v = e->ival;
    return true;
}
static Expr *mk(ExprKind k, Pos pos) {
    Expr *e = xs_alloc(sizeof *e);
    e->kind = k;
    e->pos = pos;
    e->field = -1;
    return e;
}
static Expr *mk_ident(const char *name, Pos pos) {
    Expr *e = mk(E_IDENT, pos);
    e->str = (char *)name;
    return e;
}
static Expr *mk_int(long long v, Pos pos) {
    Expr *e = mk(E_NUMBER, pos);
    e->is_int_lit = true;
    e->ival = v;
    e->num = (double)v;
    return e;
}
static Stmt *mk_stmt(StmtKind k, Pos pos) {
    Stmt *s = xs_alloc(sizeof *s);
    s->kind = k;
    s->pos = pos;
    return s;
}
static Stmt *mk_var(const char *name, Expr *init, bool is_const, Pos pos) {
    Stmt *s = mk_stmt(S_VAR, pos);
    s->name = (char *)name;
    s->init = init;
    s->is_const = is_const;
    return s;
}
static Stmt *mk_expr_stmt(Expr *e) {
    Stmt *s = mk_stmt(S_EXPR, e->pos);
    s->expr = e;
    return s;
}
static Expr *mk_assign(Expr *lhs, Expr *rhs) {
    Expr *a = mk(E_ASSIGN, lhs->pos);
    a->op = TK_ASSIGN;
    a->lhs = lhs;
    a->rhs = rhs;
    return a;
}
static Expr *mk_index(const char *arr, const char *ix, Pos pos) {
    Expr *e = mk(E_INDEX, pos);
    e->lhs = mk_ident(arr, pos);
    e->rhs = mk_ident(ix, pos);
    return e;
}
static Expr *mk_inc(const char *name, Pos pos) {
    Expr *e = mk(E_POSTFIX, pos);
    e->op = TK_PLUSPLUS;
    e->lhs = mk_ident(name, pos);
    return e;
}

/* names in a small set */
typedef struct { const char **v; int n, cap; } Names;
static void add(Names *s, const char *x) {
    for (int i = 0; i < s->n; i++) if (strcmp(s->v[i], x) == 0) return;
    if (s->n == s->cap) {
        s->cap = s->cap ? 2 * s->cap : 8;
        s->v = realloc(s->v, s->cap * sizeof *s->v);
    }
    s->v[s->n++] = x;
}
static bool has(Names *s, const char *x) {
    for (int i = 0; i < s->n; i++) if (strcmp(s->v[i], x) == 0) return true;
    return false;
}

/* the names an expression writes (x = ..., a[i] op= ..., x++); false when it does something this pass does not
   follow (a call other than Math.sqrt, a closure, an object) */
static bool writes(Expr *e, Names *w) {
    if (!e) return true;
    switch (e->kind) {
    case E_NUMBER: case E_IDENT: return true;
    case E_ASSIGN: case E_POSTFIX:
        if (is_ident(e->lhs, NULL)) add(w, e->lhs->str);
        else if (e->lhs && e->lhs->kind == E_INDEX && is_ident(e->lhs->lhs, NULL)) {
            add(w, e->lhs->lhs->str);
            if (!writes(e->lhs->rhs, w)) return false;
        } else return false;
        return writes(e->rhs, w);
    case E_UNARY:
        if (e->op == TK_PLUSPLUS || e->op == TK_MINUSMINUS) {
            if (!is_ident(e->lhs, NULL) && !(e->lhs && e->lhs->kind == E_INDEX && is_ident(e->lhs->lhs, NULL))) return false;
            add(w, is_ident(e->lhs, NULL) ? e->lhs->str : e->lhs->lhs->str);
        }
        return writes(e->lhs, w) && writes(e->rhs, w);
    case E_BINARY: case E_INDEX: return writes(e->lhs, w) && writes(e->rhs, w);
    case E_MEMBER:
        if (e->is_call && is_ident(e->lhs, "Math") && strcmp(e->str, "sqrt") == 0 && e->args.len == 1) return writes(e->args.items[0], w);
        return false;
    default: return false;
    }
}

typedef struct {
    const char *i, *j;  /* the loop variables */
    Names *w;           /* written in the loop */
    Names *pure;        /* pure consts so far */
    Names *local;       /* every name the loop body declares */
    bool slow;          /* a sqrt or a division seen in a pure const */
} Ctx;

/* the expression is pure in the sense above */
static bool pure(Ctx *c, Expr *e) {
    if (!e) return false;
    switch (e->kind) {
    case E_NUMBER: return true;
    case E_IDENT:
        if (strcmp(e->str, c->i) == 0 || strcmp(e->str, c->j) == 0) return false; /* (only as an index: a double here) */
        if (has(c->pure, e->str)) return true;
        return !has(c->w, e->str) && !has(c->local, e->str);
    case E_BINARY:
        if (e->op != TK_PLUS && e->op != TK_MINUS && e->op != TK_STAR && e->op != TK_SLASH) return false;
        if (e->op == TK_SLASH) c->slow = true;
        return pure(c, e->lhs) && pure(c, e->rhs);
    case E_UNARY: return e->op == TK_MINUS && !e->rhs && pure(c, e->lhs);
    case E_INDEX:
        return is_ident(e->lhs, NULL) && strstr(e->lhs->str, "__col_") && !has(c->w, e->lhs->str) && !has(c->local, e->lhs->str) &&
               (is_ident(e->rhs, c->i) || is_ident(e->rhs, c->j));
    case E_MEMBER:
        if (e->is_call && is_ident(e->lhs, "Math") && strcmp(e->str, "sqrt") == 0 && e->args.len == 1) {
            c->slow = true;
            return pure(c, e->args.items[0]);
        }
        return false;
    default: return false;
    }
}

static void uses_expr(Expr *e, Names *u) {
    if (!e) return;
    if (e->kind == E_IDENT && e->str) add(u, e->str);
    uses_expr(e->lhs, u);
    uses_expr(e->rhs, u);
    for (int i = 0; i < e->args.len; i++) uses_expr(e->args.items[i], u);
}

/* `let x = 0` / `let x = i + 1`, `x < B`, `x++`: the loop variable, or NULL */
static const char *loop_var(Stmt *s, const char *outer, Expr **bound) {
    if (!s || s->kind != S_FOR || !s->for_init || s->for_init->kind != S_VAR || s->for_init->is_const) return NULL;
    const char *v = s->for_init->name;
    long long k;
    Expr *in = s->for_init->init;
    if (outer) {
        if (!(in && in->kind == E_BINARY && in->op == TK_PLUS && is_ident(in->lhs, outer) && int_lit(in->rhs, &k) && k == 1)) return NULL;
    } else if (!int_lit(in, &k) || k != 0) return NULL;
    Expr *c = s->cond;
    if (!c || c->kind != E_BINARY || c->op != TK_LT || !is_ident(c->lhs, v)) return NULL;
    Expr *u = s->for_update;
    if (!u || !((u->kind == E_POSTFIX || u->kind == E_UNARY) && u->op == TK_PLUSPLUS && is_ident(u->lhs, v))) return NULL;
    *bound = c->rhs;
    return v;
}

/* the value of the bound: a literal, or a const of the function holding one */
static bool const_int(StmtList *body, const char *name, long long *v) {
    for (int i = 0; i < body->len; i++) {
        Stmt *s = body->items[i];
        if (s->kind == S_VAR && s->is_const && strcmp(s->name, name) == 0 && int_lit(s->init, v)) return true;
        if (s->kind == S_BLOCK && const_int(&s->stmts, name, v)) return true;
    }
    return false;
}

static bool bound_value(Func *f, Expr *b, long long *v) {
    if (int_lit(b, v)) return true;
    return is_ident(b, NULL) && f->body && const_int(&f->body->stmts, b->str, v);
}

static bool same_bound(Expr *a, Expr *b) {
    long long x, y;
    if (int_lit(a, &x) && int_lit(b, &y)) return x == y;
    return is_ident(a, NULL) && is_ident(b, a->str);
}

static Stmt *try_loop(Func *f, Stmt *outer) {
    Expr *b1, *b2;
    const char *i = loop_var(outer, NULL, &b1);
    if (!i) return NULL;
    Stmt *inner = outer->body;
    if (inner && inner->kind == S_BLOCK && inner->stmts.len == 1) inner = inner->stmts.items[0];
    const char *j = loop_var(inner, i, &b2);
    if (!j || !same_bound(b1, b2)) return NULL;
    long long k;
    if (!bound_value(f, b1, &k) || k < 2) return NULL;
    long long pairs = k * (k - 1) / 2;
    if (pairs > 64) return NULL; /* (the slots live in the frame: f64-local takes at most 64 doubles) */
    Stmt *body = inner->body;
    if (!body || body->kind != S_BLOCK) return NULL;
    StmtList *l = &body->stmts;
    Names w = {0}, pureset = {0}, local = {0};
    for (int s = 0; s < l->len; s++) {
        Stmt *st = l->items[s];
        if (st->kind == S_VAR) {
            add(&local, st->name);
            if (!writes(st->init, &w)) return NULL;
        } else if (st->kind == S_EXPR) {
            if (!writes(st->expr, &w)) return NULL;
        } else return NULL;
    }
    if (has(&w, i) || has(&w, j)) return NULL;
    Ctx c = {i, j, &w, &pureset, &local, false};
    bool *hoist = xs_alloc((size_t)l->len * sizeof *hoist);
    int nh = 0;
    for (int s = 0; s < l->len; s++) {
        Stmt *st = l->items[s];
        if (st->kind == S_VAR && st->is_const && !st->decl_type && pure(&c, st->init)) {
            hoist[s] = true;
            add(&pureset, st->name);
            nh++;
        }
    }
    if (!nh || nh == l->len || !c.slow) return NULL;
    /* the pure consts the rest of the body reads: each gets its slots */
    Names need = {0};
    for (int s = 0; s < l->len; s++) {
        if (hoist[s]) continue;
        Stmt *st = l->items[s];
        uses_expr(st->kind == S_VAR ? st->init : st->expr, &need);
    }
    int id = ++dist_id;
    Pos pos = outer->pos;
    const char *p = xs_sprintf("__dist%d_p", id);
    Stmt *blk = mk_stmt(S_BLOCK, pos);
    for (int s = 0; s < l->len; s++) {
        if (!hoist[s] || !has(&need, l->items[s]->name)) continue;
        Expr *n = mk(E_NEW_OBJ, pos);
        n->str = "Float64Array";
        expr_list_push(&n->args, mk_int(pairs, pos));
        stmt_list_push(&blk->stmts, mk_var(xs_sprintf("__dist%d_%s", id, l->items[s]->name), n, true, pos));
    }
    stmt_list_push(&blk->stmts, mk_var(p, mk_int(0, pos), false, pos));
    /* pass 1 */
    Stmt *body1 = mk_stmt(S_BLOCK, pos);
    for (int s = 0; s < l->len; s++) {
        if (!hoist[s]) continue;
        Stmt *st = l->items[s];
        stmt_list_push(&body1->stmts, mk_var(st->name, st->init, true, st->pos));
        if (has(&need, st->name))
            stmt_list_push(&body1->stmts, mk_expr_stmt(mk_assign(mk_index(xs_sprintf("__dist%d_%s", id, st->name), p, st->pos), mk_ident(st->name, st->pos))));
    }
    stmt_list_push(&body1->stmts, mk_expr_stmt(mk_inc(p, pos)));
    Stmt *in1 = mk_stmt(S_FOR, pos);
    in1->for_init = mk_var(j, mk(E_BINARY, pos), false, pos);
    in1->for_init->init->op = TK_PLUS;
    in1->for_init->init->lhs = mk_ident(i, pos);
    in1->for_init->init->rhs = mk_int(1, pos);
    in1->cond = mk(E_BINARY, pos);
    in1->cond->op = TK_LT;
    in1->cond->lhs = mk_ident(j, pos);
    in1->cond->rhs = int_lit(b1, &k) ? mk_int(k, pos) : mk_ident(b1->str, pos);
    in1->for_update = mk_inc(j, pos);
    in1->body = body1;
    Stmt *out1 = mk_stmt(S_FOR, pos);
    out1->for_init = mk_var(i, mk_int(0, pos), false, pos);
    out1->cond = mk(E_BINARY, pos);
    out1->cond->op = TK_LT;
    out1->cond->lhs = mk_ident(i, pos);
    out1->cond->rhs = int_lit(b1, &k) ? mk_int(k, pos) : mk_ident(b1->str, pos);
    out1->for_update = mk_inc(i, pos);
    Stmt *ob = mk_stmt(S_BLOCK, pos);
    stmt_list_push(&ob->stmts, in1);
    out1->body = ob;
    stmt_list_push(&blk->stmts, out1);
    stmt_list_push(&blk->stmts, mk_expr_stmt(mk_assign(mk_ident(p, pos), mk_int(0, pos))));
    /* pass 2: the original loop, the pure consts read back */
    StmtList rest = {0};
    for (int s = 0; s < l->len; s++) {
        Stmt *st = l->items[s];
        if (hoist[s]) {
            if (!has(&need, st->name)) continue;
            st->init = mk_index(xs_sprintf("__dist%d_%s", id, st->name), p, st->pos);
        }
        stmt_list_push(&rest, st);
    }
    stmt_list_push(&rest, mk_expr_stmt(mk_inc(p, pos)));
    body->stmts = rest;
    stmt_list_push(&blk->stmts, outer);
    if (getenv("XISTO_DISTRIBUTE_WHY")) fprintf(stderr, "distribute: pair loop over %s, %s in %s: %d consts in pass 1, %lld pairs\n", i, j, f->name, nh, pairs);
    return blk;
}

static void walk_list(Func *f, StmtList *l);
static void walk_stmt(Func *f, Stmt **sp) {
    Stmt *s = *sp;
    if (!s) return;
    if (s->kind == S_FOR) {
        Stmt *r = try_loop(f, s);
        if (r) {
            *sp = r;
            return;
        }
    }
    if (s->kind == S_BLOCK) walk_list(f, &s->stmts);
    else if (s->kind == S_IF) {
        walk_stmt(f, &s->body);
        walk_stmt(f, &s->else_body);
    } else if (s->kind == S_FOR || s->kind == S_WHILE || s->kind == S_FOROF) walk_stmt(f, &s->body);
}
static void walk_list(Func *f, StmtList *l) {
    for (int i = 0; i < l->len; i++) walk_stmt(f, &l->items[i]);
}

void distribute_program(Program *p) {
    if (getenv("XISTO_NO_DISTRIBUTE")) return;
    for (int i = 0; i < p->nfuncs; i++) {
        Func *f = p->funcs[i];
        if (!f || !f->body || !f->mod || f->mod->builtin || f->ntparams || f->template_of || f->is_gen) continue;
        walk_stmt(f, &f->body);
    }
}
