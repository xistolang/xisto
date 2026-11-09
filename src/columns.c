/* columns: a local array of objects of a class made only of numbers becomes one Float64Array per field.

   const bodies: Body[] = [new Body(...), ...];   advance(bodies, 0.01);
   function advance(bodies: Body[], dt: number) { ... bodies[i].vx -= dx * mj; ... }

   is rewritten, before the checker sees it, as

   const bodies__col_x = new Float64Array(5); ...; bodies__col_x[0] = ...; ...   advance(bodies__col_x, ..., 0.01);
   function advance(bodies__col_x: Float64Array, ..., dt: number) { ... bodies__col_vx[i] -= dx * mj; ... }

   so a few positions and velocities live in the frame of the function that runs the steps (the f64-local rule),
   in registers across the unrolled loops, as the columns a person would write by hand (nbody.ts on an M4: 1.62
   -> 1.23 s of CPU, the hand-written columns of nbody-4 take 1.25). The same doubles: the fields hold doubles and
   so do the columns, with the same operations in the same order.

   It applies only where the objects cannot be told apart from their columns:
   - the class: fields that are all `number` (no initializers), no base class, no subclasses, no methods, a
     constructor whose body only assigns each field one of its parameters;
   - the array: a `const` initialized with an array literal of `new C(...)` (at most 64), in a top-level function;
     it is used only as `X.length`, `X[i].field` (read or written), `for (const b of X)` and `const b = X[i]` with b
     used only as `b.field`, and as an argument of top-level functions whose every call passes it (from that
     function), where the parameter is used the same way (and passed on to no other function);
   - every index is a literal in range or the variable of a `for (let i = A; i < X.length; i++)` (or `< K`, or `<`
     a const holding X.length) that the loop body does not assign, A a non-negative literal or such a variable plus
     a non-negative literal: no access can fall outside the array, where the object version would throw.
   Nothing else may refer to the array: no other use, no closure capturing it. XISTO_NO_COLUMNS=1 turns it off. */
#include "xisto.h"

#define COL_MAX 64

typedef struct {
    Class *cls;
    int k;                 /* elements */
    int *param_field;      /* constructor parameter j -> field index (-1: not stored) */
    /* the columns: one per field, or (pad4, x86 with AVX2) one of 4 lanes for each trio Px Py Pz (x y z, 0) */
    int ncols;
    const char **csuf;     /* column c's name suffix */
    int *cw;               /* column c's lanes per element: 1, or 4 */
    int *fcol, *flane;     /* field fi -> its column, and its lane there (-1: a column of its own) */
} Shape;

bool xs_pad4; /* main.c: the target has 4-lane double vectors (AVX2) and XISTO_NO_PAD4 is not set */

/* names in scope while walking a function: what each one is */
enum { N_OTHER, N_ARRAY, N_ELEM, N_SAFE, N_LEN };
typedef struct {
    const char *name;
    int kind;
    const char *ix;        /* N_ELEM: the index variable that replaces it */
} Name;

typedef struct {
    Program *prog;
    const char *arr;       /* the array (or parameter) name */
    Shape *sh;
    bool rewrite;
    bool ok;
    const char *why;
    Name *names;
    int nnames, cap;
    int in_arrow;
    /* calls that pass the array: function and argument position */
    Func **calls;
    int *callpos;
    int ncalls;
    Func *cur;             /* the function being walked */
    bool callee;           /* walking a callee: passing the array on is not allowed */
} Walk;

static int fresh_id;

static void push_name(Walk *w, const char *name, int kind, const char *ix) {
    if (w->nnames == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 32;
        w->names = realloc(w->names, sizeof(Name) * (size_t)w->cap);
    }
    w->names[w->nnames++] = (Name){name, kind, ix};
}
static Name *lookup(Walk *w, const char *name) {
    if (!name) return NULL;
    for (int i = w->nnames - 1; i >= 0; i--)
        if (strcmp(w->names[i].name, name) == 0) return &w->names[i];
    return NULL;
}
static int kind_of(Walk *w, const char *name) {
    Name *n = lookup(w, name);
    return n ? n->kind : N_OTHER;
}
static void fail(Walk *w, const char *why) {
    if (w->ok) w->why = why;
    w->ok = false;
}

static bool is_ident(Expr *e) { return e && e->kind == E_IDENT && e->str; }
static bool is_int_lit(Expr *e, long long *v) {
    if (!e || e->kind != E_NUMBER || !e->is_int_lit) return false;
    if (v) *v = e->ival;
    return true;
}
static int field_index(Class *c, const char *name) {
    for (int i = 0; i < c->nfields; i++)
        if (strcmp(c->fields[i].name, name) == 0) return i;
    return -1;
}
static char *col_name(const char *arr, const char *suf) { return xs_sprintf("%s__col_%s", arr, suf); }

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
static Type *f64_type(void) {
    Class *c = xs_alloc(sizeof *c);
    c->name = "Float64Array";
    return ty_object_of(c);
}

/* the class can be held as columns */
static Shape *shape_of(Program *p, const char *cname) {
    Class *c = NULL;
    for (int i = 0; i < p->nclasses; i++) {
        if (strcmp(p->classes[i]->name, cname) != 0) continue;
        if (c) return NULL; /* two classes of that name (modules): not this rule */
        c = p->classes[i];
    }
    if (!c || c->builtin || c->is_interface || c->base_name || c->nsubs || c->ntparams || c->nmethods || c->nsmethods ||
        c->nsfields || c->data_only || !c->ctor || c->nfields == 0 || c->nfields > 16 || c->decos.len)
        return NULL;
    for (int i = 0; i < p->nclasses; i++) /* a subclass names it as its base */
        if (p->classes[i]->base_name && strcmp(p->classes[i]->base_name, cname) == 0) return NULL;
    for (int i = 0; i < c->nfields; i++) {
        Field *f = &c->fields[i];
        if (!f->type || f->type->kind != TY_NUMBER || f->init || f->optional || f->decos.len) return NULL;
    }
    Func *k = c->ctor;
    if (!k->body || k->body->kind != S_BLOCK || k->is_async) return NULL;
    Shape *sh = xs_alloc(sizeof *sh);
    sh->cls = c;
    sh->param_field = xs_alloc(sizeof(int) * (size_t)(k->nparams + 1));
    for (int j = 0; j < k->nparams; j++) {
        Param *pr = &k->params[j];
        if (!pr->type || pr->type->kind != TY_NUMBER || pr->is_prop || pr->dflt || pr->decos.len) return NULL;
        sh->param_field[j] = -1;
    }
    bool *set = xs_alloc(sizeof(bool) * (size_t)c->nfields);
    for (int i = 0; i < k->body->stmts.len; i++) {
        Stmt *s = k->body->stmts.items[i];
        if (s->kind != S_EXPR || !s->expr || s->expr->kind != E_ASSIGN || s->expr->op != TK_ASSIGN) return NULL;
        Expr *l = s->expr->lhs, *r = s->expr->rhs;
        if (!l || l->kind != E_MEMBER || l->is_call || !l->lhs || l->lhs->kind != E_THIS || !is_ident(r)) return NULL;
        int fi = field_index(c, l->str);
        int pj = -1;
        for (int j = 0; j < k->nparams; j++)
            if (strcmp(k->params[j].name, r->str) == 0) pj = j;
        if (fi < 0 || pj < 0 || set[fi] || sh->param_field[pj] >= 0) return NULL;
        set[fi] = true;
        sh->param_field[pj] = fi;
    }
    for (int i = 0; i < c->nfields; i++)
        if (!set[i]) return NULL;
    int nf = c->nfields;
    sh->csuf = xs_alloc(sizeof(char *) * (size_t)nf);
    sh->cw = xs_alloc(sizeof(int) * (size_t)nf);
    sh->fcol = xs_alloc(sizeof(int) * (size_t)nf);
    sh->flane = xs_alloc(sizeof(int) * (size_t)nf);
    for (int i = 0; i < nf; i++) sh->fcol[i] = -1;
    /* pad4: fields P+"x", P+"y", P+"z" share one column, 4 lanes per element (the fourth stays 0) */
    for (int i = 0; xs_pad4 && i < nf; i++) {
        const char *n = c->fields[i].name;
        size_t L = strlen(n);
        if (sh->fcol[i] >= 0 || L == 0 || n[L - 1] != 'x') continue;
        char *ny = xs_sprintf("%s", n), *nz = xs_sprintf("%s", n);
        ny[L - 1] = 'y';
        nz[L - 1] = 'z';
        int iy = field_index(c, ny), iz = field_index(c, nz);
        if (iy < 0 || iz < 0 || sh->fcol[iy] >= 0 || sh->fcol[iz] >= 0) continue;
        char *pre = xs_sprintf("%.*s", (int)(L - 1), n);
        int col = sh->ncols++;
        sh->csuf[col] = xs_sprintf("c4_%sxyz", pre);
        sh->cw[col] = 4;
        sh->fcol[i] = sh->fcol[iy] = sh->fcol[iz] = col;
        sh->flane[i] = 0;
        sh->flane[iy] = 1;
        sh->flane[iz] = 2;
    }
    for (int i = 0; i < nf; i++) {
        if (sh->fcol[i] >= 0) continue;
        int col = sh->ncols++;
        sh->csuf[col] = c->fields[i].name;
        sh->cw[col] = 1;
        sh->fcol[i] = col;
        sh->flane[i] = -1;
    }
    return sh;
}

static void walk_expr(Walk *w, Expr *e);
static void walk_stmt(Walk *w, Stmt *s);

/* an index that cannot fall outside the array */
static bool safe_index(Walk *w, Expr *e) {
    long long v;
    if (is_int_lit(e, &v)) return v >= 0 && v < w->sh->k;
    return is_ident(e) && kind_of(w, e->str) == N_SAFE;
}

/* X.length, or a const holding it, or a literal no larger than the array */
static bool is_len_bound(Walk *w, Expr *e) {
    long long v;
    if (is_int_lit(e, &v)) return v <= w->sh->k;
    if (is_ident(e)) return kind_of(w, e->str) == N_LEN;
    return e && e->kind == E_MEMBER && !e->is_call && is_ident(e->lhs) && kind_of(w, e->lhs->str) == N_ARRAY &&
           strcmp(e->str, "length") == 0;
}

/* does the statement or expression assign the name (x = ..., x += ..., x++, ++x) */
static bool assigns_expr(Expr *e, const char *name);
static bool assigns_stmt(Stmt *s, const char *name) {
    if (!s) return false;
    if (s->kind == S_VAR && s->name && strcmp(s->name, name) == 0) return true; /* shadowed: treat as assigned */
    if (s->kind == S_FOROF && s->name && strcmp(s->name, name) == 0) return true;
    if (assigns_expr(s->init, name) || assigns_expr(s->expr, name) || assigns_expr(s->cond, name) ||
        assigns_expr(s->for_update, name) || assigns_stmt(s->for_init, name) || assigns_stmt(s->body, name) ||
        assigns_stmt(s->else_body, name))
        return true;
    for (int i = 0; i < s->stmts.len; i++)
        if (assigns_stmt(s->stmts.items[i], name)) return true;
    for (int i = 0; i < s->ncases; i++)
        if (assigns_expr(s->cases[i].value, name) || assigns_stmt(s->cases[i].body, name)) return true;
    return false;
}
static bool assigns_expr(Expr *e, const char *name) {
    if (!e) return false;
    if ((e->kind == E_ASSIGN || e->kind == E_POSTFIX) && is_ident(e->lhs) && strcmp(e->lhs->str, name) == 0) return true;
    if (e->kind == E_ARROW) return true; /* (conservative: a closure could) */
    if (assigns_expr(e->lhs, name) || assigns_expr(e->rhs, name)) return true;
    for (int i = 0; i < e->args.len; i++)
        if (assigns_expr(e->args.items[i], name)) return true;
    return false;
}

/* the loop variable of `for (let i = A; i < bound; i++)` when no access through it can leave the array */
static const char *safe_loop_var(Walk *w, Stmt *s) {
    Stmt *in = s->for_init;
    if (!in || in->kind != S_VAR || !in->name || in->is_const || !in->init) return NULL;
    long long v;
    Expr *a = in->init;
    bool start_ok = (is_int_lit(a, &v) && v >= 0) ||
                    (a->kind == E_BINARY && a->op == TK_PLUS && is_ident(a->lhs) && kind_of(w, a->lhs->str) == N_SAFE &&
                     is_int_lit(a->rhs, &v) && v >= 0);
    if (!start_ok) return NULL;
    Expr *c = s->cond;
    if (!c || c->kind != E_BINARY || c->op != TK_LT || !is_ident(c->lhs) || strcmp(c->lhs->str, in->name) != 0 ||
        !is_len_bound(w, c->rhs))
        return NULL;
    Expr *u = s->for_update;
    bool step = u && is_ident(u->lhs) && strcmp(u->lhs->str, in->name) == 0 &&
                ((u->kind == E_POSTFIX && u->op == TK_PLUSPLUS) ||
                 (u->kind == E_ASSIGN && u->op == TK_PLUS_ASSIGN && is_int_lit(u->rhs, &v) && v == 1));
    if (!step || assigns_stmt(s->body, in->name)) return NULL;
    return in->name;
}

/* X[e].field and b.field: the column, indexed */
static void to_column(Expr *e, Shape *sh, const char *arr, Expr *index) {
    int fi = field_index(sh->cls, e->str);
    int col = sh->fcol[fi], lane = sh->flane[fi];
    e->kind = E_INDEX;
    e->lhs = mk_ident(col_name(arr, sh->csuf[col]), e->pos);
    if (lane >= 0) { /* 4 * index + lane */
        Expr *m = mk(E_BINARY, e->pos);
        m->op = TK_STAR;
        m->lhs = mk_int(4, e->pos);
        m->rhs = index;
        Expr *a = mk(E_BINARY, e->pos);
        a->op = TK_PLUS;
        a->lhs = m;
        a->rhs = mk_int(lane, e->pos);
        index = a;
    }
    e->rhs = index;
    e->str = NULL;
}

static void walk_list(Walk *w, ExprList *l) {
    for (int i = 0; i < l->len; i++) walk_expr(w, l->items[i]);
}

static void walk_expr(Walk *w, Expr *e) {
    if (!e || !w->ok) return;
    switch (e->kind) {
    case E_IDENT: {
        int k = kind_of(w, e->str);
        if (k == N_ARRAY) fail(w, "the array is used as a value");
        else if (k == N_ELEM) fail(w, "an element is used as a value");
        return;
    }
    case E_MEMBER:
        if (!e->is_call && is_ident(e->lhs) && kind_of(w, e->lhs->str) == N_ARRAY && strcmp(e->str, "length") == 0) {
            if (w->in_arrow) { fail(w, "a closure uses the array"); return; }
            if (w->rewrite) { Pos pos = e->pos; *e = *mk_int(w->sh->k, pos); }
            return;
        }
        if (!e->is_call && e->lhs && e->lhs->kind == E_INDEX && is_ident(e->lhs->lhs) && kind_of(w, e->lhs->lhs->str) == N_ARRAY) {
            if (w->in_arrow) { fail(w, "a closure uses the array"); return; }
            if (field_index(w->sh->cls, e->str) < 0) { fail(w, "not a field"); return; }
            Expr *ix = e->lhs->rhs;
            if (!safe_index(w, ix)) { fail(w, "an index not proved in range"); return; }
            if (w->rewrite) to_column(e, w->sh, w->arr, ix);
            return;
        }
        if (!e->is_call && is_ident(e->lhs) && kind_of(w, e->lhs->str) == N_ELEM) {
            if (w->in_arrow) { fail(w, "a closure uses an element"); return; }
            if (field_index(w->sh->cls, e->str) < 0) { fail(w, "not a field"); return; }
            if (w->rewrite) to_column(e, w->sh, w->arr, mk_ident(lookup(w, e->lhs->str)->ix, e->pos));
            return;
        }
        walk_expr(w, e->lhs);
        walk_list(w, &e->args);
        return;
    case E_CALL: {
        int pos = -1;
        for (int i = 0; i < e->args.len; i++) {
            Expr *a = e->args.items[i];
            if (is_ident(a) && kind_of(w, a->str) == N_ARRAY) {
                if (pos >= 0) { fail(w, "the array is passed twice"); return; }
                pos = i;
            }
        }
        if (pos >= 0) {
            if (w->callee || w->in_arrow) { fail(w, "the array is passed on"); return; }
            if (!is_ident(e->lhs) || e->ntargs) { fail(w, "the array is passed to something not a function"); return; }
            Func *f = NULL;
            for (int i = 0; i < w->prog->nfuncs; i++) {
                Func *g = w->prog->funcs[i];
                if (!g->cls && g->name && strcmp(g->name, e->lhs->str) == 0) {
                    if (f) { fail(w, "two functions of that name"); return; }
                    f = g;
                }
            }
            if (!f) { fail(w, "the array is passed to something not a function"); return; }
            if (w->rewrite) {
                ExprList args = {0};
                for (int i = 0; i < e->args.len; i++) {
                    if (i != pos) { expr_list_push(&args, e->args.items[i]); continue; }
                    for (int ci = 0; ci < w->sh->ncols; ci++)
                        expr_list_push(&args, mk_ident(col_name(w->arr, w->sh->csuf[ci]), e->args.items[i]->pos));
                }
                for (int i = 0; i < e->args.len; i++)
                    if (i != pos) walk_expr(w, e->args.items[i]);
                e->args = args;
                return;
            }
            w->calls = realloc(w->calls, sizeof(Func *) * (size_t)(w->ncalls + 1));
            w->callpos = realloc(w->callpos, sizeof(int) * (size_t)(w->ncalls + 1));
            w->calls[w->ncalls] = f;
            w->callpos[w->ncalls++] = pos;
            for (int i = 0; i < e->args.len; i++)
                if (i != pos) walk_expr(w, e->args.items[i]);
            return;
        }
        walk_expr(w, e->lhs);
        walk_list(w, &e->args);
        return;
    }
    case E_ASSIGN:
    case E_POSTFIX:
        if (is_ident(e->lhs) && kind_of(w, e->lhs->str) != N_OTHER) {
            int k = kind_of(w, e->lhs->str);
            if (k == N_ARRAY || k == N_ELEM) { fail(w, "the array or an element is reassigned"); return; }
        }
        walk_expr(w, e->lhs);
        walk_expr(w, e->rhs);
        walk_list(w, &e->args);
        return;
    case E_ARROW: {
        if (!e->arrow || !e->arrow->fn) return;
        int mark = w->nnames;
        for (int i = 0; i < e->arrow->fn->nparams; i++) push_name(w, e->arrow->fn->params[i].name, N_OTHER, NULL);
        w->in_arrow++;
        for (int i = 0; i < e->arrow->fn->param_decls.len; i++) walk_stmt(w, e->arrow->fn->param_decls.items[i]);
        walk_stmt(w, e->arrow->fn->body);
        w->in_arrow--;
        w->nnames = mark;
        return;
    }
    default:
        walk_expr(w, e->lhs);
        walk_expr(w, e->rhs);
        walk_list(w, &e->args);
        return;
    }
}

/* the statements of a block, in a scope of their own */
static void walk_block(Walk *w, StmtList *l) {
    int mark = w->nnames;
    for (int i = 0; i < l->len && w->ok; i++) walk_stmt(w, l->items[i]);
    w->nnames = mark;
}

static void walk_stmt(Walk *w, Stmt *s) {
    if (!s || !w->ok) return;
    switch (s->kind) {
    case S_VAR:
        if (s->name && kind_of(w, s->name) == N_ARRAY) { fail(w, "the array's name is declared again"); return; }
        if (s->is_const && s->name && s->init && s->init->kind == E_INDEX && is_ident(s->init->lhs) &&
            kind_of(w, s->init->lhs->str) == N_ARRAY) { /* const b = X[i] */
            if (w->in_arrow) { fail(w, "a closure uses the array"); return; }
            if (!safe_index(w, s->init->rhs)) { fail(w, "an index not proved in range"); return; }
            const char *ix = w->rewrite ? xs_sprintf("%s__ix%d", s->name, fresh_id++) : NULL;
            push_name(w, s->name, N_ELEM, ix);
            if (w->rewrite) { /* const b__ixN = i */
                s->name = (char *)ix;
                s->init = s->init->rhs;
                s->decl_type = NULL;
            }
            return;
        }
        if (s->is_const && s->name && is_len_bound(w, s->init) && s->init->kind == E_MEMBER) {
            walk_expr(w, s->init);
            push_name(w, s->name, N_LEN, NULL);
            return;
        }
        walk_expr(w, s->init);
        if (s->name) push_name(w, s->name, N_OTHER, NULL);
        return;
    case S_FOROF:
        if (s->name && kind_of(w, s->name) == N_ARRAY) { fail(w, "the array's name is declared again"); return; }
        if (is_ident(s->expr) && kind_of(w, s->expr->str) == N_ARRAY) { /* for (const b of X) */
            if (w->in_arrow) { fail(w, "a closure uses the array"); return; }
            int mark = w->nnames;
            const char *ix = w->rewrite ? xs_sprintf("%s__ix%d", s->name, fresh_id++) : NULL;
            push_name(w, s->name, N_ELEM, ix);
            walk_stmt(w, s->body);
            w->nnames = mark;
            if (w->rewrite) { /* for (let b__ixN = 0; b__ixN < K; b__ixN++) */
                Stmt *init = mk_stmt(S_VAR, s->pos);
                init->name = (char *)ix;
                init->init = mk_int(0, s->pos);
                Expr *cond = mk(E_BINARY, s->pos);
                cond->op = TK_LT;
                cond->lhs = mk_ident(ix, s->pos);
                cond->rhs = mk_int(w->sh->k, s->pos);
                Expr *up = mk(E_POSTFIX, s->pos);
                up->op = TK_PLUSPLUS;
                up->lhs = mk_ident(ix, s->pos);
                s->kind = S_FOR;
                s->for_init = init;
                s->cond = cond;
                s->for_update = up;
                s->expr = NULL;
                s->name = NULL;
                s->decl_type = NULL;
            }
            return;
        }
        walk_expr(w, s->expr);
        {
            int mark = w->nnames;
            if (s->name) push_name(w, s->name, N_OTHER, NULL);
            walk_stmt(w, s->body);
            w->nnames = mark;
        }
        return;
    case S_FOR: {
        int mark = w->nnames;
        const char *safe = w->in_arrow ? NULL : safe_loop_var(w, s);
        if (s->for_init) {
            if (safe) { walk_expr(w, s->for_init->init); push_name(w, safe, N_SAFE, NULL); }
            else walk_stmt(w, s->for_init);
        }
        walk_expr(w, s->cond);
        walk_expr(w, s->for_update);
        walk_stmt(w, s->body);
        w->nnames = mark;
        return;
    }
    case S_BLOCK:
        walk_block(w, &s->stmts);
        return;
    case S_TRY: {
        walk_stmt(w, s->body);
        int mark = w->nnames;
        if (s->name) push_name(w, s->name, N_OTHER, NULL);
        walk_stmt(w, s->else_body);
        w->nnames = mark;
        walk_stmt(w, s->for_init);
        return;
    }
    case S_SWITCH:
        walk_expr(w, s->expr);
        for (int i = 0; i < s->ncases; i++) {
            walk_expr(w, s->cases[i].value);
            walk_stmt(w, s->cases[i].body);
        }
        return;
    default:
        walk_expr(w, s->init);
        walk_expr(w, s->expr);
        walk_expr(w, s->cond);
        walk_stmt(w, s->for_init);
        walk_expr(w, s->for_update);
        walk_stmt(w, s->body);
        walk_stmt(w, s->else_body);
        return;
    }
}

/* every reference to the function f anywhere: only direct calls from `from` passing `arr` at `pos` */
typedef struct { const char *fname; Func *from; const char *arr; int pos; bool ok; Func *cur; } Census;
static void census_expr(Census *c, Expr *e);
static void census_stmt(Census *c, Stmt *s) {
    if (!s || !c->ok) return;
    if ((s->kind == S_VAR || s->kind == S_FOROF || s->kind == S_TRY) && s->name && strcmp(s->name, c->fname) == 0) c->ok = false;
    census_expr(c, s->init);
    census_expr(c, s->expr);
    census_expr(c, s->cond);
    census_expr(c, s->for_update);
    census_stmt(c, s->for_init);
    census_stmt(c, s->body);
    census_stmt(c, s->else_body);
    for (int i = 0; i < s->stmts.len; i++) census_stmt(c, s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) {
        census_expr(c, s->cases[i].value);
        census_stmt(c, s->cases[i].body);
    }
}
static void census_expr(Census *c, Expr *e) {
    if (!e || !c->ok) return;
    if (e->kind == E_CALL && is_ident(e->lhs) && strcmp(e->lhs->str, c->fname) == 0) {
        Expr *a = c->pos < e->args.len ? e->args.items[c->pos] : NULL;
        if (c->cur != c->from || !is_ident(a) || strcmp(a->str, c->arr) != 0) { c->ok = false; return; }
        for (int i = 0; i < e->args.len; i++) census_expr(c, e->args.items[i]);
        return;
    }
    if (e->kind == E_IDENT && e->str && strcmp(e->str, c->fname) == 0) { c->ok = false; return; } /* as a value */
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn) {
        Func *saved = c->cur;
        c->cur = e->arrow->fn;
        for (int i = 0; i < e->arrow->fn->nparams; i++)
            if (strcmp(e->arrow->fn->params[i].name, c->fname) == 0) c->ok = false;
        for (int i = 0; i < e->arrow->fn->param_decls.len; i++) census_stmt(c, e->arrow->fn->param_decls.items[i]);
        census_stmt(c, e->arrow->fn->body);
        c->cur = saved;
        return;
    }
    census_expr(c, e->lhs);
    census_expr(c, e->rhs);
    for (int i = 0; i < e->args.len; i++) census_expr(c, e->args.items[i]);
}
static bool only_called_with(Program *p, Func *f, Func *from, const char *arr, int pos) {
    Census c = {f->name, from, arr, pos, true, NULL};
    for (int i = 0; i < p->nfuncs && c.ok; i++) {
        Func *g = p->funcs[i];
        c.cur = g;
        for (int j = 0; j < g->nparams; j++)
            if (strcmp(g->params[j].name, f->name) == 0) c.ok = false;
        for (int j = 0; j < g->param_decls.len; j++) census_stmt(&c, g->param_decls.items[j]);
        census_stmt(&c, g->body);
    }
    for (int i = 0; i < p->nclasses && c.ok; i++) { /* methods and constructors are not in p->funcs */
        Class *k = p->classes[i];
        for (int j = -1; j < k->nmethods && c.ok; j++) {
            Func *g = j < 0 ? k->ctor : k->methods[j];
            if (!g) continue;
            c.cur = g;
            for (int q = 0; q < g->nparams; q++)
                if (strcmp(g->params[q].name, f->name) == 0) c.ok = false;
            census_stmt(&c, g->body);
        }
    }
    c.cur = NULL;
    for (int i = 0; i < p->top.len && c.ok; i++) census_stmt(&c, p->top.items[i]);
    return c.ok;
}

static bool plain_function(Func *f) {
    return f && !f->cls && f->body && !f->is_async && !f->is_gen && !f->ntparams && !f->template_of && !f->exported &&
           !f->is_native && !f->is_default && !f->decos.len && f->mod && !f->mod->builtin;
}

/* the declaration of X, as columns: const X__col_f = new Float64Array(K) per field, then each element's
   constructor arguments in order, each into its field's column (an argument no field keeps is still evaluated) */
static void declare_columns(StmtList *out, Stmt *decl, Shape *sh) {
    Class *c = sh->cls;
    Pos pos = decl->pos;
    (void)c;
    for (int ci = 0; ci < sh->ncols; ci++) {
        Stmt *v = mk_stmt(S_VAR, pos);
        v->name = col_name(decl->name, sh->csuf[ci]);
        v->is_const = true;
        Expr *n = mk(E_NEW_OBJ, pos);
        n->str = "Float64Array";
        expr_list_push(&n->args, mk_int((long long)sh->k * sh->cw[ci], pos));
        v->init = n;
        stmt_list_push(out, v);
    }
    Expr *lit = decl->init;
    for (int k = 0; k < lit->args.len; k++) {
        Expr *ne = lit->args.items[k];
        for (int j = 0; j < ne->args.len; j++) {
            Stmt *st = mk_stmt(S_EXPR, ne->args.items[j]->pos);
            int fi = sh->param_field[j];
            if (fi < 0) st->expr = ne->args.items[j];
            else {
                Expr *ix = mk(E_INDEX, ne->args.items[j]->pos);
                ix->lhs = mk_ident(col_name(decl->name, sh->csuf[sh->fcol[fi]]), pos);
                ix->rhs = mk_int(sh->flane[fi] >= 0 ? 4LL * k + sh->flane[fi] : k, pos);
                Expr *as = mk(E_ASSIGN, ne->args.items[j]->pos);
                as->op = TK_ASSIGN;
                as->lhs = ix;
                as->rhs = ne->args.items[j];
                st->expr = as;
            }
            stmt_list_push(out, st);
        }
    }
}

/* the parameter `pos` of f, used as the array: legal, and (rewrite) replaced by its columns */
static bool param_ok(Program *p, Func *f, int pos, Shape *sh, bool rewrite) {
    Param *pr = &f->params[pos];
    if (!pr->type || pr->type->kind != TY_ARRAY || !pr->type->elem || pr->type->elem->kind != TY_OBJECT ||
        !pr->type->elem->cls || strcmp(pr->type->elem->cls->name, sh->cls->name) != 0 || pr->dflt || pr->is_prop ||
        pr->decos.len || f->param_decls.len)
        return false;
    Walk w = {.prog = p, .arr = pr->name, .sh = sh, .rewrite = rewrite, .ok = true, .callee = true, .cur = f};
    for (int j = 0; j < f->nparams; j++) push_name(&w, f->params[j].name, j == pos ? N_ARRAY : N_OTHER, NULL);
    walk_stmt(&w, f->body);
    free(w.names);
    if (!w.ok) return false;
    if (rewrite) {
        int nf = sh->ncols;
        Param *ps = xs_alloc(sizeof(Param) * (size_t)(f->nparams - 1 + nf));
        int n = 0;
        for (int j = 0; j < f->nparams; j++) {
            if (j != pos) { ps[n++] = f->params[j]; continue; }
            for (int fi = 0; fi < nf; fi++) {
                Param q = {0};
                q.name = col_name(pr->name, sh->csuf[fi]);
                q.type = f64_type();
                ps[n++] = q;
            }
        }
        f->params = ps;
        f->nparams = n;
    }
    return true;
}

/* the candidate declarations directly in a block of f's body (blocks nested in it too) */
static void try_block(Program *p, Func *f, Stmt *blk);
static void try_decl(Program *p, Func *f, Stmt *blk, int at) {
    Stmt *d = blk->stmts.items[at];
    if (d->kind != S_VAR || !d->is_const || !d->name || !d->init || d->init->kind != E_ARRAY_LIT || d->init->spread) return;
    Expr *lit = d->init;
    if (lit->args.len == 0 || lit->args.len > COL_MAX) return;
    const char *cname = NULL;
    for (int i = 0; i < lit->args.len; i++) {
        Expr *ne = lit->args.items[i];
        if (ne->kind != E_NEW_OBJ || !ne->str || ne->ntargs || (cname && strcmp(cname, ne->str) != 0)) return;
        cname = ne->str;
    }
    if (d->decl_type && !(d->decl_type->kind == TY_ARRAY && d->decl_type->elem && d->decl_type->elem->kind == TY_OBJECT &&
                          d->decl_type->elem->cls && strcmp(d->decl_type->elem->cls->name, cname) == 0))
        return;
    Shape *sh = shape_of(p, cname);
    if (!sh) return;
    sh->k = lit->args.len;
    for (int i = 0; i < lit->args.len; i++)
        if (lit->args.items[i]->args.len != sh->cls->ctor->nparams) return;
    /* the uses after the declaration, in this block (the name is not visible before it) */
    Walk w = {.prog = p, .arr = d->name, .sh = sh, .ok = true, .cur = f};
    for (int j = 0; j < f->nparams; j++) push_name(&w, f->params[j].name, N_OTHER, NULL);
    push_name(&w, d->name, N_ARRAY, NULL);
    for (int i = at + 1; i < blk->stmts.len && w.ok; i++) walk_stmt(&w, blk->stmts.items[i]);
    for (int i = 0; i < lit->args.len && w.ok; i++) walk_list(&w, &lit->args.items[i]->args); /* (the arguments cannot use it) */
    if (!w.ok) { if (getenv("XISTO_COLUMNS_WHY")) fprintf(stderr, "columns: %s: %s\n", d->name, w.why); free(w.names); return; }
    /* each function it is passed to: every call passes it from here, and the parameter is used the same way */
    for (int i = 0; i < w.ncalls; i++) {
        Func *g = w.calls[i];
        if (!plain_function(g) || g == f || w.callpos[i] >= g->nparams || !only_called_with(p, g, f, d->name, w.callpos[i]) ||
            !param_ok(p, g, w.callpos[i], sh, false)) {
            if (getenv("XISTO_COLUMNS_WHY")) fprintf(stderr, "columns: %s: passed to %s, which cannot take columns\n", d->name, g->name);
            free(w.names);
            return;
        }
    }
    /* rewrite: the callees (each once), the uses, the declaration */
    Func **done = NULL;
    int ndone = 0;
    for (int i = 0; i < w.ncalls; i++) {
        bool seen = false;
        for (int j = 0; j < ndone; j++) seen |= done[j] == w.calls[i];
        if (seen) continue;
        param_ok(p, w.calls[i], w.callpos[i], sh, true);
        done = realloc(done, sizeof(Func *) * (size_t)(ndone + 1));
        done[ndone++] = w.calls[i];
    }
    free(done);
    free(w.names);
    Walk r = {.prog = p, .arr = d->name, .sh = sh, .ok = true, .rewrite = true, .cur = f};
    for (int j = 0; j < f->nparams; j++) push_name(&r, f->params[j].name, N_OTHER, NULL);
    push_name(&r, d->name, N_ARRAY, NULL);
    for (int i = at + 1; i < blk->stmts.len; i++) walk_stmt(&r, blk->stmts.items[i]);
    free(r.names);
    StmtList out = {0};
    for (int i = 0; i < at; i++) stmt_list_push(&out, blk->stmts.items[i]);
    declare_columns(&out, d, sh);
    for (int i = at + 1; i < blk->stmts.len; i++) stmt_list_push(&out, blk->stmts.items[i]);
    blk->stmts = out;
    if (getenv("XISTO_COLUMNS_WHY")) fprintf(stderr, "columns: %s in %s: %d %s as columns\n", d->name, f->name, sh->k, sh->cls->name);
}
static void try_block(Program *p, Func *f, Stmt *blk) {
    if (!blk || blk->kind != S_BLOCK) return;
    for (int i = 0; i < blk->stmts.len; i++) {
        Stmt *s = blk->stmts.items[i];
        if (s->kind == S_VAR) try_decl(p, f, blk, i);
        else if (s->kind == S_BLOCK) try_block(p, f, s);
    }
}

void columns_program(Program *p) {
    if (getenv("XISTO_NO_COLUMNS")) return;
    int n = p->nfuncs; /* (only the functions of the program as parsed) */
    for (int i = 0; i < n; i++) {
        Func *f = p->funcs[i];
        if (!f || f->cls || !f->body || !f->mod || f->mod->builtin || f->ntparams || f->template_of || f->is_gen) continue;
        try_block(p, f, f->body);
    }
}
