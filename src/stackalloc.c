/* Stack allocation: an object that provably does not outlive the call that makes it lives in that call's frame.

   `const v = new C(...)` (or a `let` never assigned again) in a function gets its storage in the frame when
   every use of v is
   - a field read or write (v.f, v.f = x), or a method call whose `this` does not escape;
   - an argument to a parameter that does not escape (of a function or method called directly, never through
     a function value or dispatch);
   - a comparison (v === w, v !== null).
   A parameter (or `this`) does not escape when its uses inside its function obey the same rules: the facts
   are the greatest fixpoint over the program (optimistic, then refuted until nothing changes), so recursion
   and mutual calls are fine. Anything else (return, store in a field or array, capture, cast, print,
   template, reassignment) is an escape.
   C must be a plain class (no base, no subclasses, no region, not part of a cycle) whose constructor only
   assigns fields (this.f = e, e without `this`) and whose field initializers do not mention `this`: the
   object is complete when the constructor returns and nothing else saw it.

   The object keeps its header and reference count: retains and releases, the cleanup stack of exceptions
   (which releases while the frames are alive) and the scope-end release work as before; when the count
   reaches zero its fields are released and xs_obj_free leaves the memory (XS_STACK). The C compiler, seeing
   the whole object in the frame, usually keeps its fields in registers and never stores the header at all.
   Functions with try (setjmp), async functions and generators are left alone. XISTO_NO_STACK=1 disables it. */
#include "xisto.h"

#include <string.h>

static const char THIS_NAME[] = "this";
static Program *sp;

static bool is_ref(Expr *e, const char *nm) {
    if (!e) return false;
    if (nm == THIS_NAME) return e->kind == E_THIS;
    return e->kind == E_IDENT && e->cname && strcmp(e->cname, nm) == 0;
}

static bool param_escapes(Func *f, int i) { return !f->sa_param_esc || f->sa_param_esc[i]; }
static bool this_escapes(Func *f) { return !f->sa_param_esc || f->sa_this_esc; }

static bool esc_expr(Expr *e, const char *nm);
/* the arguments of a direct call to f: a bare v must go to a parameter of f that does not escape */
static bool esc_call_args(ExprList *args, Func *f, const char *nm) {
    for (int i = 0; i < args->len; i++) {
        Expr *a = args->items[i];
        if (is_ref(a, nm)) {
            if (!f || f->is_native || i >= f->nparams || param_escapes(f, i)) return true;
        } else if (esc_expr(a, nm)) return true;
    }
    return false;
}
static bool esc_expr(Expr *e, const char *nm) {
    if (!e) return false;
    switch (e->kind) {
    case E_IDENT: return e->cname && strcmp(e->cname, nm) == 0; /* used as a value: anything may happen to it */
    case E_THIS: return nm == THIS_NAME;
    case E_ARROW: /* a closure that captures it keeps it */
        if (e->arrow)
            for (int i = 0; i < e->arrow->ncaptures; i++) {
                Capture *c = &e->arrow->captures[i];
                if (nm == THIS_NAME ? c->is_this : (c->outer_cname && strcmp(c->outer_cname, nm) == 0) || (c->name && strcmp(c->name, nm) == 0)) return true;
            }
        return false;
    case E_MEMBER:
        if (is_ref(e->lhs, nm)) {
            if (e->func) { /* a method, getter or setter */
                if (e->virt || e->super_call || e->func->is_native || this_escapes(e->func)) return true;
            } else if (e->field < 0) return true; /* something built in */
            return esc_call_args(&e->args, e->func, nm);
        }
        if (esc_expr(e->lhs, nm)) return true;
        if (e->func) return esc_call_args(&e->args, e->func, nm);
        break;
    case E_CALL:
        if (esc_expr(e->lhs, nm)) return true;
        return esc_call_args(&e->args, e->func && !e->builtin.cfn ? e->func : NULL, nm);
    case E_BINARY:
        if ((e->op == TK_EQ || e->op == TK_NEQ) && (is_ref(e->lhs, nm) || is_ref(e->rhs, nm)))
            return (!is_ref(e->lhs, nm) && esc_expr(e->lhs, nm)) || (!is_ref(e->rhs, nm) && esc_expr(e->rhs, nm));
        break;
    case E_ASSIGN:
        if (is_ref(e->lhs, nm)) return true; /* assigned again */
        break;
    default: break;
    }
    if (esc_expr(e->lhs, nm) || esc_expr(e->rhs, nm)) return true;
    for (int i = 0; i < e->args.len; i++) if (esc_expr(e->args.items[i], nm)) return true;
    return false;
}
static bool esc_stmt(Stmt *s, const char *nm) {
    if (!s) return false;
    if (s->kind == S_VAR && s->cname && strcmp(s->cname, nm) == 0 && s->cell) return true;
    if (esc_expr(s->init, nm) || esc_expr(s->expr, nm) || esc_expr(s->cond, nm) || esc_expr(s->for_update, nm)) return true;
    if (esc_stmt(s->for_init, nm) || esc_stmt(s->body, nm) || esc_stmt(s->else_body, nm)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (esc_stmt(s->stmts.items[i], nm)) return true;
    for (int i = 0; i < s->ncases; i++) if (esc_expr(s->cases[i].value, nm) || esc_stmt(s->cases[i].body, nm)) return true;
    return false;
}

static bool stmt_has(Stmt *s, StmtKind k) {
    if (!s) return false;
    if (s->kind == k) return true;
    if (stmt_has(s->for_init, k) || stmt_has(s->body, k) || stmt_has(s->else_body, k)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (stmt_has(s->stmts.items[i], k)) return true;
    for (int i = 0; i < s->ncases; i++) if (stmt_has(s->cases[i].body, k)) return true;
    return false;
}
static bool expr_has_kind(Expr *e, ExprKind k) {
    if (!e) return false;
    if (e->kind == k) return true;
    if (expr_has_kind(e->lhs, k) || expr_has_kind(e->rhs, k)) return true;
    for (int i = 0; i < e->args.len; i++) if (expr_has_kind(e->args.items[i], k)) return true;
    return false;
}

/* ---- the functions whose parameters are looked at ---- */
static Func **fns;
static int nfns, cfns;
static void add_fn(Func *f) {
    if (!f || !f->body || !f->used || f->is_native || f->sa_param_esc) return;
    f->sa_param_esc = calloc((size_t)(f->nparams ? f->nparams : 1), sizeof(bool));
    f->sa_this_esc = false;
    if (f->is_async || f->is_gen) { /* their arguments live on in frames on the heap */
        for (int i = 0; i < f->nparams; i++) f->sa_param_esc[i] = true;
        f->sa_this_esc = true;
    }
    for (int i = 0; i < f->nparams; i++) if (f->params[i].cell || f->params[i].is_prop) f->sa_param_esc[i] = true;
    if (nfns == cfns) { cfns = cfns ? cfns * 2 : 256; fns = realloc(fns, sizeof(Func *) * (size_t)cfns); }
    fns[nfns++] = f;
}

/* a class whose objects may live in a frame */
static bool class_ok(Class *c, bool (*cyclic)(Class *)) {
    if (c->builtin || c->is_interface || c->base || c->nsubs || c->region || c->rdual || c->is_abstract || c->decos.len || cyclic(c)) return false;
    for (int i = 0; i < c->nfields; i++)
        if (c->fields[i].init && (expr_has_kind(c->fields[i].init, E_THIS) || expr_has_kind(c->fields[i].init, E_ARROW) || expr_has_kind(c->fields[i].init, E_REGEX))) return false;
    Func *ct = c->ctor;
    if (!ct) return true;
    if (ct->is_async || !ct->body) return false;
    for (int i = 0; i < ct->nparams; i++) if (ct->params[i].is_prop || ct->params[i].cell) return false;
    for (int k = 0; k < ct->body->stmts.len; k++) { /* this.f = e, nothing else */
        Stmt *st = ct->body->stmts.items[k];
        Expr *e = st->kind == S_EXPR ? st->expr : NULL;
        if (!e || e->kind != E_ASSIGN || !e->lhs || e->lhs->kind != E_MEMBER || !e->lhs->lhs || e->lhs->lhs->kind != E_THIS ||
            e->lhs->field < 0 || e->lhs->func || expr_has_kind(e->rhs, E_THIS) || expr_has_kind(e->rhs, E_ARROW) || expr_has_kind(e->rhs, E_REGEX) ||
            expr_has_kind(e->rhs, E_AWAIT))
            return false;
    }
    return true;
}

/* ---- strings that die with a stack object ----
   const v = new C(..., `v${i}`, ...): when the field the string goes to is only ever read without being kept
   (v.f.length, a comparison, a part of a template), in the function and in what v is handed to, the string dies
   with v and can live in the frame too (a buffer next to v's; a longer one goes to the heap as before). */
static bool fld_read(Expr *e, const char *nm, int fi) {
    return e && e->kind == E_MEMBER && !e->is_call && e->field == fi && !e->func && is_ref(e->lhs, nm);
}
static bool fld_stmt(Stmt *s, const char *nm, int fi, int depth);
static Func *fld_seen[16];
static int fld_nseen;
static bool fld_callee(Func *g, const char *pname, int fi, int depth) {
    if (!g || !g->body || depth > 4) return false;
    for (int i = 0; i < fld_nseen; i++) if (fld_seen[i] == g) return true; /* (being looked at: recursion) */
    if (fld_nseen == 16) return false;
    fld_seen[fld_nseen++] = g;
    bool ok = fld_stmt(g->body, pname, fi, depth + 1);
    fld_nseen--;
    return ok;
}
static bool fld_expr(Expr *e, Expr *parent, const char *nm, int fi, int depth) {
    if (!e) return true;
    if (fld_read(e, nm, fi)) {
        if (!parent) return false;
        if (parent->kind == E_MEMBER && !parent->is_call && parent->lhs == e && parent->str && strcmp(parent->str, "length") == 0) return true;
        if (parent->kind == E_BINARY && (parent->op == TK_EQ || parent->op == TK_NEQ || parent->op == TK_LT || parent->op == TK_LE ||
                                         parent->op == TK_GT || parent->op == TK_GE)) return true;
        if (parent->kind == E_TEMPLATE) return true;
        if (parent->kind == E_ASSIGN && parent->lhs == e) return true; /* v.f = other: the old value goes */
        return false;
    }
    /* v handed on: what the callee does with its field counts too */
    Func *g = (e->kind == E_CALL || (e->kind == E_MEMBER && e->is_call)) && !e->builtin.cfn ? e->func : NULL;
    if (e->kind == E_MEMBER && e->is_call && g && is_ref(e->lhs, nm) && !fld_callee(g, THIS_NAME, fi, depth)) return false;
    for (int i = 0; i < e->args.len; i++)
        if (is_ref(e->args.items[i], nm) && (!g || i >= g->nparams || !fld_callee(g, g->params[i].cname, fi, depth))) return false;
    if (!fld_expr(e->lhs, e, nm, fi, depth) || !fld_expr(e->rhs, e, nm, fi, depth)) return false;
    for (int i = 0; i < e->args.len; i++) if (!fld_expr(e->args.items[i], e, nm, fi, depth)) return false;
    return true;
}
static bool fld_stmt(Stmt *s, const char *nm, int fi, int depth) {
    if (!s) return true;
    if (!fld_expr(s->init, NULL, nm, fi, depth) || !fld_expr(s->expr, NULL, nm, fi, depth) || !fld_expr(s->cond, NULL, nm, fi, depth) ||
        !fld_expr(s->for_update, NULL, nm, fi, depth)) return false;
    if (!fld_stmt(s->for_init, nm, fi, depth) || !fld_stmt(s->body, nm, fi, depth) || !fld_stmt(s->else_body, nm, fi, depth)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!fld_stmt(s->stmts.items[i], nm, fi, depth)) return false;
    for (int i = 0; i < s->ncases; i++) if (!fld_expr(s->cases[i].value, NULL, nm, fi, depth) || !fld_stmt(s->cases[i].body, nm, fi, depth)) return false;
    return true;
}
/* the field the constructor stores parameter k in (this.f = k), or -1 */
static int ctor_field_of(Class *c, int k) {
    Func *ct = c->ctor;
    if (!ct || !ct->body || k >= ct->nparams) return -1;
    for (int i = 0; i < ct->body->stmts.len; i++) {
        Expr *a = ct->body->stmts.items[i]->kind == S_EXPR ? ct->body->stmts.items[i]->expr : NULL;
        if (a && a->kind == E_ASSIGN && a->rhs && a->rhs->kind == E_IDENT && a->rhs->cname && strcmp(a->rhs->cname, ct->params[k].cname) == 0)
            return a->lhs->field;
    }
    return -1;
}
static void mark_strings(Stmt *s, Func *f) {
    Expr *e = s->init;
    Class *c = e->cls;
    for (int k = 0; k < e->args.len && k < 32; k++) {
        Expr *a = e->args.items[k];
        if (a->kind != E_TEMPLATE || a->args.len > 3 || !a->type || a->type->kind != TY_STRING) continue; /* (short templates: xs_cat_n) */
        int fi = ctor_field_of(c, k);
        if (fi < 0 || c->fields[fi].type->kind != TY_STRING) continue;
        fld_nseen = 0;
        if (fld_stmt(f->body, s->cname, fi, 0)) s->stack_str |= 1u << k;
    }
}

static void mark_vars(Stmt *s, Func *f, bool (*cyclic)(Class *)) {
    if (!s) return;
    if (s->kind == S_VAR && s->init && s->init->kind == E_NEW_OBJ && s->init->cls && !s->cell && s->type && s->type->kind == TY_OBJECT &&
        s->type->cls == s->init->cls && class_ok(s->init->cls, cyclic) && !esc_stmt(f->body, s->cname)) {
        s->on_stack = true;
        s->init->cls->stack_at = true;
        if (!getenv("XISTO_NO_STACK_STR")) mark_strings(s, f);
    }
    mark_vars(s->for_init, f, cyclic);
    mark_vars(s->body, f, cyclic);
    mark_vars(s->else_body, f, cyclic);
    for (int i = 0; i < s->stmts.len; i++) mark_vars(s->stmts.items[i], f, cyclic);
    for (int i = 0; i < s->ncases; i++) mark_vars(s->cases[i].body, f, cyclic);
}

void stack_compute(Program *p, bool (*cyclic)(Class *)) {
    if (getenv("XISTO_NO_STACK")) return;
    sp = p;
    nfns = 0;
    for (int i = 0; i < p->nfuncs; i++) add_fn(p->funcs[i]);
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (!c->used || c->builtin) continue;
        add_fn(c->ctor);
        for (int k = 0; k < c->nmethods; k++) if (c->methods[k]->cls == c) add_fn(c->methods[k]);
    }
    for (bool changed = true; changed;) { /* refute until nothing changes */
        changed = false;
        for (int i = 0; i < nfns; i++) {
            Func *f = fns[i];
            for (int k = 0; k < f->nparams; k++)
                if (!f->sa_param_esc[k] && esc_stmt(f->body, f->params[k].cname)) { f->sa_param_esc[k] = true; changed = true; }
            if (f->cls && !f->is_ctor && !f->sa_this_esc && esc_stmt(f->body, THIS_NAME)) { f->sa_this_esc = true; changed = true; }
        }
    }
    for (int i = 0; i < nfns; i++) {
        Func *f = fns[i];
        if (f->is_async || f->is_gen || stmt_has(f->body, S_TRY) || stmt_has(f->body, S_YIELD)) continue;
        mark_vars(f->body, f, cyclic);
    }
    if (getenv("XISTO_STACK_WHY"))
        for (int i = 0; i < p->nclasses; i++) if (p->classes[i]->stack_at) fprintf(stderr, "xisto: %s objects on the stack\n", p->classes[i]->name);
}
