/* Literal arguments of a function called once (between the checker and prune.c).

   A constructor that takes an options object is usually written once for every option and called with a few of
   them, written as literals: new Intl.NumberFormat("pt-BR", { style: "currency", currency: "BRL" }). When the
   function has a single call in the whole program and a parameter receives a literal there (a string, a number, a
   boolean, null, or an object literal of those that the function only reads), each read of it in the body is that
   literal; comparisons, ??, !, &&, ||, ?: and if over literals are decided here, and so are the const locals they
   give. The branches that cannot run are not compiled, and what only they call (another currency display's symbol
   table, another locale's data) is not reached by prune.c: the program carries the code of the options it passes.

   Only what is certain is folded: a parameter that is assigned, captured in a cell or a parameter property keeps
   its reads; an object parameter (or the const that holds it, as `const o = options ?? {}`) used in any other way
   than reading its fields, testing it against null or ?? keeps them too; closures are not entered. A function
   reachable in any other way (a value, a dispatched method, a base constructor) has no single call. */
#include "xisto.h"
#include <math.h>


/* ---- calls ---- */
static Func **fc_work;
static int fc_nwork, fc_capwork;
static void fc_reach(Func *f) {
    if (!f || f->fold_seen) return;
    f->fold_seen = true;
    if (fc_nwork == fc_capwork) { fc_capwork = fc_capwork ? fc_capwork * 2 : 64; fc_work = realloc(fc_work, sizeof(Func *) * (size_t)fc_capwork); }
    fc_work[fc_nwork++] = f;
    for (int i = 0; i < f->noverrides; i++) fc_reach(f->overrides[i]);
}
static void fc_note(Func *f, Expr *call) {
    if (!f) return;
    if (call && f->fold_ncalls == 0) f->fold_call = call;
    f->fold_ncalls += call ? 1 : 1000;
    fc_reach(f);
}
static void fc_stmt(Stmt *s);
static void fc_expr(Expr *e) {
    if (!e) return;
    switch (e->kind) {
    case E_CALL: if (e->func) fc_note(e->func, e); break;
    case E_MEMBER:
        if (e->func) fc_note(e->func, e->is_call && !e->virt && !e->super_call ? e : NULL); /* (a method as a value: any call) */
        break;
    case E_NEW_OBJ:
        if (e->cls && e->cls->ctor) fc_note(e->cls->ctor, e);
        if (e->cls && !e->cls->fold_seen) { /* (its field initializers run with it) */
            e->cls->fold_seen = true;
            for (int k = 0; k < e->cls->nfields; k++) fc_expr(e->cls->fields[k].init);
        }
        break;
    case E_SUPER_CALL: if (e->cls && e->cls->ctor) fc_note(e->cls->ctor, NULL); break;
    case E_FUNC_REF: if (e->func) fc_note(e->func, NULL); break;
    case E_ARROW: if (e->arrow && e->arrow->fn) fc_stmt(e->arrow->fn->body); break;
    default: break;
    }
    fc_expr(e->lhs);
    fc_expr(e->rhs);
    for (int i = 0; i < e->args.len; i++) fc_expr(e->args.items[i]);
}
static void fc_stmt(Stmt *s) {
    if (!s) return;
    fc_expr(s->init); fc_expr(s->expr); fc_expr(s->cond); fc_expr(s->for_update);
    fc_stmt(s->for_init); fc_stmt(s->body); fc_stmt(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) fc_stmt(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) { fc_expr(s->cases[i].value); fc_stmt(s->cases[i].body); }
}
static void fc_func(Func *f) {
    if (!f) return;
    for (int i = 0; i < f->nparams; i++) fc_expr(f->params[i].dflt);
    fc_stmt(f->body);
}

/* ---- literals ---- */
static bool is_lit(const Expr *e) { return e && (e->kind == E_STRING || e->kind == E_NUMBER || e->kind == E_BOOL || e->kind == E_NULL); }
static bool objlit_ok(const Expr *e) {
    if (!e || e->kind != E_OBJLIT || !e->keys || e->spread) return false;
    for (int i = 0; i < e->args.len; i++) if (!is_lit(e->args.items[i]) || !e->keys[i]) return false;
    return true;
}
/* v in the place of like: like's type and position; a string keeps its own literal, anything else like's lit_id (a
   template part's text for null, which the checker put on the expression it replaces) */
static Expr *lit_put(const Expr *v, const Expr *like) {
    Expr *c = xs_alloc(sizeof(Expr));
    *c = *v;
    c->pos = like->pos;
    if (like->type) c->type = like->type;
    if (v->kind != E_STRING) c->lit_id = like->lit_id;
    return c;
}
static Expr *mk_bool(bool b, Expr *like) {
    Expr *c = xs_alloc(sizeof(Expr));
    c->kind = E_BOOL;
    c->pos = like->pos;
    c->boolean = b;
    c->type = like->type;
    c->field = -1;
    c->lit_id = like->lit_id;
    return c;
}

/* a number literal for e (of e's type): an int keeps its integer value, a result outside the safe range is left alone */
static Expr *mk_num_(double v, Expr *like) {
    Expr *c = xs_alloc(sizeof(Expr));
    c->kind = E_NUMBER;
    c->pos = like->pos;
    c->num = v;
    c->type = like->type;
    c->field = -1;
    c->lit_id = like->lit_id;
    if (v == (double)(long long)v && !(v == 0 && signbit(v)) && v > -9007199254740992.0 && v < 9007199254740992.0) { c->ival = (long long)v; c->is_int_lit = true; }
    return c;
}
#define mk_num(v, like) (((like)->type && ((like)->type->kind == TY_INT || (like)->type->kind == TY_LITINT) && (!((v) == (double)(long long)(v) && (v) > -9007199254740992.0 && (v) < 9007199254740992.0) || ((v) == 0 && signbit(v)))) ? (like) : mk_num_((v), (like)))
/* ---- bindings: a name (C name) read as a literal, or as an object literal's fields ---- */
typedef struct { const char *cname; Expr *val; bool obj; } Bind;
static Bind *binds;
static int nbinds, capbinds;
static void bind(const char *cname, Expr *val, bool obj) {
    if (nbinds == capbinds) { capbinds = capbinds ? capbinds * 2 : 32; binds = realloc(binds, sizeof(Bind) * (size_t)capbinds); }
    binds[nbinds++] = (Bind){cname, val, obj};
}
static Bind *bound(const Expr *e) {
    if (!e || e->kind != E_IDENT || !e->cname) return NULL;
    for (int i = nbinds - 1; i >= 0; i--) if (!strcmp(binds[i].cname, e->cname)) return &binds[i];
    return NULL;
}

/* is the name only read (never assigned, ++/--)? and, for an object, only read through .field, ?? and === null,
   or held by a const (alias: that const's C name is added to names) */
static bool use_ok;
static const char **anames;
static int nanames;
static bool is_name(const Expr *e) {
    if (!e || e->kind != E_IDENT || !e->cname) return false;
    for (int i = 0; i < nanames; i++) if (!strcmp(anames[i], e->cname)) return true;
    return false;
}
/* x != null ? x : d (what the checker makes of x ?? d) */
static bool null_test_of(const Expr *t, const Expr *x) {
    return t && t->kind == E_BINARY && (t->op == TK_NEQ || t->op == TK_EQ) && t->lhs && t->rhs && t->rhs->kind == E_NULL &&
           t->lhs->kind == E_IDENT && x && x->kind == E_IDENT && t->lhs->cname && x->cname && !strcmp(t->lhs->cname, x->cname);
}
static bool null_pick(const Expr *e) {
    return e && e->kind == E_COND && e->args.len == 2 && is_name(e->args.items[0]) && e->lhs->op == TK_NEQ && null_test_of(e->lhs, e->args.items[0]);
}
static void uses_expr(Expr *e, bool obj, Expr *parent);
static void uses_stmt(Stmt *s, bool obj) {
    if (!s || !use_ok) return;
    if (obj && s->kind == S_VAR && s->is_const && !s->cell && s->init &&
        (is_name(s->init) || (s->init->kind == E_BINARY && s->init->op == TK_QQ && is_name(s->init->lhs)) || null_pick(s->init))) {
        anames = realloc(anames, sizeof(char *) * (size_t)(nanames + 1));
        anames[nanames++] = s->cname; /* const o = options ?? {}: o is the literal too */
        if (s->init->kind == E_BINARY) uses_expr(s->init->rhs, obj, s->init);
        else if (s->init->kind == E_COND) uses_expr(s->init->args.items[1], obj, s->init);
    } else {
        uses_expr(s->init, obj, NULL);
    }
    uses_expr(s->expr, obj, NULL); uses_expr(s->cond, obj, NULL); uses_expr(s->for_update, obj, NULL);
    uses_stmt(s->for_init, obj); uses_stmt(s->body, obj); uses_stmt(s->else_body, obj);
    for (int i = 0; i < s->stmts.len; i++) uses_stmt(s->stmts.items[i], obj);
    for (int i = 0; i < s->ncases; i++) { uses_expr(s->cases[i].value, obj, NULL); uses_stmt(s->cases[i].body, obj); }
}
static void uses_expr(Expr *e, bool obj, Expr *parent) {
    if (!e || !use_ok) return;
    if (is_name(e)) {
        if (!parent) { if (obj) use_ok = false; return; }
        if ((parent->kind == E_ASSIGN || parent->kind == E_POSTFIX || (parent->kind == E_UNARY && (parent->op == TK_PLUSPLUS || parent->op == TK_MINUSMINUS))) && parent->lhs == e) {
            use_ok = false;
            return;
        }
        if (!obj) return;
        if (parent->kind == E_MEMBER && parent->lhs == e && !parent->is_call && parent->field >= 0) return;
        if (parent->kind == E_BINARY && parent->lhs == e && parent->op == TK_QQ) return;
        if (parent->kind == E_COND && parent->args.len == 2 && parent->args.items[0] == e && parent->lhs && parent->lhs->op == TK_NEQ && null_test_of(parent->lhs, e)) return;
        if (parent->kind == E_BINARY && (parent->op == TK_EQ || parent->op == TK_NEQ) &&
            ((parent->lhs == e && parent->rhs && parent->rhs->kind == E_NULL) || (parent->rhs == e && parent->lhs && parent->lhs->kind == E_NULL)))
            return;
        use_ok = false;
        return;
    }
    if (e->kind == E_ARROW) { /* (a closure that names it: left alone) */
        if (e->arrow && e->arrow->fn) uses_stmt(e->arrow->fn->body, obj);
        return;
    }
    uses_expr(e->lhs, obj, e);
    uses_expr(e->rhs, obj, e);
    for (int i = 0; i < e->args.len; i++) uses_expr(e->args.items[i], obj, e);
}
static bool only_read(Func *f, const char *cname, bool obj) {
    use_ok = true;
    nanames = 0;
    anames = realloc(anames, sizeof(char *));
    anames[nanames++] = cname;
    uses_stmt(f->body, obj);
    return use_ok;
}

/* ---- folding ---- */
static bool truth(const Expr *e, bool *out) {
    if (!e) return false;
    if (e->kind == E_BOOL) { *out = e->boolean; return true; }
    return false;
}
static Expr *fold_expr(Expr *e);
static void fold_list(ExprList *l) { for (int i = 0; i < l->len; i++) l->items[i] = fold_expr(l->items[i]); }
static Expr *fold_expr(Expr *e) {
    if (!e) return e;
    if (e->kind == E_ARROW) return e; /* (closures keep their reads) */
    if ((e->kind == E_MEMBER || e->kind == E_INDEX) && bound(e->lhs) && !bound(e->lhs)->obj && bound(e->lhs)->val->kind == E_NULL) {
        e->rhs = fold_expr(e->rhs); /* (x.f where x is null: code a test before it keeps from running; left alone) */
        fold_list(&e->args);
        return e;
    }
    Bind *b = bound(e);
    if (b && !b->obj) return lit_put(b->val, e);
    if (e->kind == E_MEMBER && !e->is_call && e->field >= 0 && e->str) {
        Bind *ob = bound(e->lhs);
        if (ob && ob->obj) {
            Expr *o = ob->val;
            for (int i = 0; i < o->args.len; i++)
                if (!strcmp(o->keys[i], e->str)) return lit_put(o->args.items[i], e);
            Expr *n = xs_alloc(sizeof(Expr)); /* a field the literal leaves out: null (undefined) */
            n->kind = E_NULL; n->pos = e->pos; n->type = e->type; n->field = -1; n->lit_id = e->lit_id;
            return n;
        }
    }
    if (e->kind == E_BINARY && e->op == TK_QQ) {
        Bind *ob = bound(e->lhs);
        if (ob && ob->obj) { e->rhs = fold_expr(e->rhs); return e; } /* (options ?? {}: the object, read through its binding) */
    }
    if (e->kind == E_BINARY && (e->op == TK_EQ || e->op == TK_NEQ)) {
        Bind *ob = bound(e->lhs), *ob2 = bound(e->rhs);
        if ((ob && ob->obj && e->rhs && e->rhs->kind == E_NULL) || (ob2 && ob2->obj && e->lhs && e->lhs->kind == E_NULL))
            return mk_bool(e->op == TK_NEQ, e);
    }
    e->lhs = fold_expr(e->lhs);
    e->rhs = fold_expr(e->rhs);
    fold_list(&e->args);
    bool t;
    switch (e->kind) {
    case E_BINARY:
        if (e->op == TK_QQ && is_lit(e->lhs)) {
            Expr *r = e->lhs->kind == E_NULL ? e->rhs : e->lhs;
            return is_lit(r) ? lit_put(r, e) : r;
        }
        if ((e->op == TK_EQ || e->op == TK_NEQ) && is_lit(e->lhs) && is_lit(e->rhs)) {
            const Expr *a = e->lhs, *c = e->rhs;
            bool eq;
            if (a->kind == E_NULL || c->kind == E_NULL) eq = a->kind == c->kind;
            else if (a->kind != c->kind) { if (e->loose) return e; eq = false; }
            else if (a->kind == E_STRING) eq = a->str && c->str && !strcmp(a->str, c->str);
            else if (a->kind == E_NUMBER) eq = a->num == c->num;
            else eq = a->boolean == c->boolean;
            return mk_bool(e->op == TK_EQ ? eq : !eq, e);
        }
        if (e->lhs && e->rhs && e->lhs->kind == E_NUMBER && e->rhs->kind == E_NUMBER) {
            double a = e->lhs->num, c = e->rhs->num;
            switch (e->op) {
            case TK_LT: return mk_bool(a < c, e);
            case TK_LE: return mk_bool(a <= c, e);
            case TK_GT: return mk_bool(a > c, e);
            case TK_GE: return mk_bool(a >= c, e);
            case TK_PLUS: return mk_num(a + c, e);
            case TK_MINUS: return mk_num(a - c, e);
            case TK_STAR: return mk_num(a * c, e);
            default: break;
            }
        }
        if ((e->op == TK_AND || e->op == TK_OR) && e->type && e->type->kind == TY_BOOLEAN && truth(e->lhs, &t)) {
            if (e->op == TK_AND) return t ? e->rhs : mk_bool(false, e);
            return t ? mk_bool(true, e) : e->rhs;
        }
        if ((e->op == TK_AND || e->op == TK_OR) && e->type && e->type->kind == TY_BOOLEAN && truth(e->rhs, &t) &&
            e->lhs && (is_lit(e->lhs) || e->lhs->kind == E_IDENT)) { /* (x && false, x || true: x has no effect to keep) */
            if (e->op == TK_AND && !t) return mk_bool(false, e);
            if (e->op == TK_OR && t) return mk_bool(true, e);
            return e->lhs;
        }
        break;
    case E_UNARY:
        if (e->op == TK_NOT && truth(e->lhs, &t)) return mk_bool(!t, e);
        if (e->op == TK_MINUS && e->lhs && e->lhs->kind == E_NUMBER) return mk_num(-e->lhs->num, e);
        break;
    case E_COND:
        if (e->args.len == 2 && truth(e->lhs, &t)) {
            Expr *r = t ? e->args.items[0] : e->args.items[1];
            return is_lit(r) ? lit_put(r, e) : r;
        }
        break;
    default: break;
    }
    return e;
}
static Stmt empty_block = {.kind = S_BLOCK};
static void fold_stmt(Stmt *s) {
    if (!s) return;
    s->init = fold_expr(s->init);
    s->expr = fold_expr(s->expr);
    s->cond = fold_expr(s->cond);
    s->for_update = fold_expr(s->for_update);
    bool t;
    if (s->kind == S_IF && truth(s->cond, &t)) {
        Stmt *taken = t ? s->body : s->else_body;
        Pos pos = s->pos;
        Module *m = s->mod;
        if (taken) *s = *taken;
        else { *s = empty_block; s->pos = pos; s->mod = m; }
        fold_stmt(s);
        return;
    }
    if (s->kind == S_VAR && s->is_const && !s->cell && is_lit(s->init) && s->cname) bind(s->cname, s->init, false);
    fold_stmt(s->for_init);
    fold_stmt(s->body);
    fold_stmt(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) fold_stmt(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) { s->cases[i].value = fold_expr(s->cases[i].value); fold_stmt(s->cases[i].body); }
}

/* the parameters of f that its one call gives a literal: bound, and f's body folded */
static int nfolded;
static void fold_func(Func *f) {
    if (!f->body || f->fold_ncalls != 1 || !f->fold_call || f->is_async || f->is_gen || f->virt_target || f->noverrides || f->ntparams) return;
    Expr *call = f->fold_call;
    if (call->kind == E_MEMBER && call->virt) return;
    nbinds = 0;
    for (int i = 0; i < f->nparams; i++) {
        Param *p = &f->params[i];
        if (p->rest || p->is_prop || p->cell || !p->cname) continue;
        Expr *a = i < call->args.len ? call->args.items[i] : p->dflt;
        if (!a) continue;
        if (is_lit(a)) {
            if (only_read(f, p->cname, false)) bind(p->cname, a, false);
        } else if (objlit_ok(a)) {
            if (only_read(f, p->cname, true)) {
                bind(p->cname, a, true);
                for (int k = 1; k < nanames; k++) bind(anames[k], a, true);
            }
        }
    }
    if (!nbinds) return; /* (no literal argument: its const locals alone are not worth a pass) */
    nfolded++;
    fold_stmt(f->body);
}

void fold_program(Program *p) {
    if (xs_debug || getenv("XISTO_NO_FOLD")) return; /* (--debug: a function edited while the program runs keeps every read) */
    /* the calls in the code the top level reaches (a function the program does not reach does not count its calls) */
    for (int i = 0; i < p->top.len; i++) fc_stmt(p->top.items[i]);
    for (int i = 0; i < p->nclasses; i++) /* (classes the checker made used: generic instances, literals) */
        if (p->classes[i]->used && !p->classes[i]->builtin && !p->classes[i]->fold_seen) {
            p->classes[i]->fold_seen = true;
            for (int k = 0; k < p->classes[i]->nfields; k++) fc_expr(p->classes[i]->fields[k].init);
        }
    while (fc_nwork > 0) fc_func(fc_work[--fc_nwork]);
    /* plain functions and constructors only: a method can also be called by name through an unknown */
    /* (rounds: a call's arguments become literals when its caller is folded, callers first or not) */
    for (int round = 0; round < 4; round++) {
        for (int i = 0; i < p->nclasses; i++) {
            Class *c = p->classes[i];
            if (c->builtin || c->base || c->nsubs) continue; /* (a constructor in a hierarchy is also reached by super()) */
            if (c->ctor) fold_func(c->ctor);
        }
        for (int i = 0; i < p->nfuncs; i++) if (!p->funcs[i]->cls) fold_func(p->funcs[i]);
    }
    if (getenv("XISTO_SHOW_FOLD")) fprintf(stderr, "fold: %d functions folded\n", nfolded);
}
