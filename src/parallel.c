/* xisto:parallel: which calls can run their fn on the thread pool.

   A call of parallelMap, parallelFrom, parallelFill or parallelReduce runs fn on several threads at once only
   when the compiler can prove that the calls of fn are independent; otherwise it runs the module's own loop
   (the same output) and says why in a warning. The proof is conservative, by what fn's body and every
   function it calls (transitively) touch:

   - Values from outside fn (what the arrow captures, and globals) are shared. A shared number or boolean may
     be read; an array of numbers or booleans, or a typed array, may be read as x[i] and x.length, or passed
     to a function whose parameter obeys the same rule. Nothing else of a shared value is allowed: no write,
     no other use as a value (that would retain it from several threads at once), no method call.
   - Whatever fn makes is its own: objects, arrays and strings it creates may be changed freely, and its
     methods and constructors are checked the same way with `this` its own.
   - No calls with effects outside: console, process, timers, fetch, Date.now, Math.random, a function value
     (what it runs is not known here), a function of another package, await.

   The runtime side (runtime/xisto_rt.h, XS_PARALLEL) runs fn into a buffer on the pool and then replays the
   module's loop on the calling thread with the values made, so order, combine and errors are the loop's. */
#include "xisto.h"

#include <string.h>

typedef struct {
    const char **shared; /* parameter names (C) that hold a shared array in the function being checked */
    int nshared;
    bool env_local;      /* env->... are fn's own locals (a nested arrow), not captures from outside */
    bool this_local;     /* `this` is an object fn made (a method or constructor called on it) */
    int depth;
    const char *why;     /* the first reason it is not safe */
} PCtx;

static bool prim_type(Type *t) {
    return t && (t->kind == TY_NUMBER || t->kind == TY_INT || t->kind == TY_LITINT || t->kind == TY_BOOLEAN || (t->kind == TY_ENUM && t->en && !t->en->is_string));
}
static bool typed_array_type(Type *t) {
    if (!t || t->kind != TY_OBJECT || !t->cls || !t->cls->builtin || !t->cls->name) return false;
    static const char *const names[] = {"Float64Array", "Float32Array", "Uint8Array", "Int8Array", "Uint16Array", "Int16Array",
                                        "Uint32Array", "Int32Array", "Buffer", NULL};
    for (int i = 0; names[i]; i++) if (strcmp(t->cls->name, names[i]) == 0) return true;
    return false;
}
/* a shared value that may be read by index */
static bool prim_array_type(Type *t) { return t && ((t->kind == TY_ARRAY && prim_type(t->elem)) || typed_array_type(t)); }

static bool fail(PCtx *c, const char *why) {
    if (!c->why) c->why = why;
    return false;
}

/* 0: fn's own; 1: shared (captured or global); 2: a name without storage (a namespace such as Math) */
static int ident_kind(PCtx *c, Expr *e) {
    const char *n = e->cname;
    if (!n) return 2;
    for (int i = 0; i < c->nshared; i++) if (strcmp(c->shared[i], n) == 0) return 1;
    if (strncmp(n, "v_", 2) == 0) return 0;
    if (strncmp(n, "env->", 5) == 0) return c->env_local ? 0 : 1;
    return 1;
}
static bool is_shared_ident(PCtx *c, Expr *e) { return e && e->kind == E_IDENT && ident_kind(c, e) == 1; }

static bool par_expr(PCtx *c, Expr *e);
static bool par_stmt(PCtx *c, Stmt *s);
static bool par_func(PCtx *c, Func *f, const char **shared, int nshared, bool this_local, bool env_local);
static const char *par_kind(Expr *e);

/* the arguments of a call of user function f: a shared array may go to a parameter that only reads it */
static bool par_call_args(PCtx *c, Func *f, ExprList *args, bool this_local) {
    const char **sh = xs_alloc(sizeof(char *) * (size_t)(args->len + 1));
    int nsh = 0;
    for (int i = 0; i < args->len; i++) {
        Expr *a = args->items[i];
        if (is_shared_ident(c, a) && prim_array_type(a->type) && i < f->nparams && f->params[i].cname && !f->params[i].cell) {
            sh[nsh++] = f->params[i].cname;
            continue;
        }
        if (!par_expr(c, a)) return false;
    }
    return par_func(c, f, sh, nsh, this_local, false);
}

/* the target of an assignment: only what fn owns */
static bool par_target(PCtx *c, Expr *t) {
    switch (t->kind) {
    case E_IDENT:
        if (ident_kind(c, t) == 1) return fail(c, xs_sprintf("it assigns '%s', which is shared by every call", t->str));
        return true;
    case E_THIS: return c->this_local ? true : fail(c, "it changes `this`, shared by every call");
    case E_INDEX:
        if (is_shared_ident(c, t->lhs)) return fail(c, xs_sprintf("it writes into '%s', which is shared by every call", t->lhs->str));
        return par_expr(c, t->lhs) && par_expr(c, t->rhs);
    case E_MEMBER:
        if (t->lhs && t->lhs->kind == E_THIS) return c->this_local ? true : fail(c, "it changes `this`, shared by every call");
        return par_expr(c, t->lhs);
    default: return par_expr(c, t);
    }
}

static bool ns_call_ok(PCtx *c, const char *ns, const char *member) {
    static const char *const ok[] = {"Math", "Number", "String", "Array", "JSON", "Object", "BigInt", "Uint8Array", "Float64Array",
                                     "Buffer", "Int32Array", "Uint32Array", "Float32Array", NULL};
    if (strcmp(ns, "Math") == 0 && member && strcmp(member, "random") == 0) return fail(c, "it calls Math.random(), whose sequence the order of the calls would change");
    for (int i = 0; ok[i]; i++) if (strcmp(ns, ok[i]) == 0) return true;
    return fail(c, xs_sprintf("it calls %s.%s(), which acts outside the function", ns, member ? member : "?"));
}

static bool user_func(Func *f) { return f && f->body && !f->is_native; }
static bool other_package(Func *f) {
    return f && f->mod && f->mod->file && strncmp(f->mod->file, "xisto:", 6) == 0 && strcmp(f->mod->file, "xisto:parallel") != 0;
}

static bool par_expr(PCtx *c, Expr *e) {
    if (!e) return true;
    switch (e->kind) {
    case E_NUMBER: case E_STRING: case E_BOOL: case E_NULL: case E_ENUM_VAL: case E_REGEX: return true;
    case E_IDENT: {
        int k = ident_kind(c, e);
        if (k != 1) return true;
        if (prim_type(e->type)) return true;
        if (prim_array_type(e->type)) return fail(c, xs_sprintf("it uses the array '%s' as a value (only %s[i] and %s.length can be shared)", e->str, e->str, e->str));
        return fail(c, xs_sprintf("it uses '%s' (%s), shared by every call: only numbers, booleans and arrays of them can be", e->str, ty_name(e->type)));
    }
    case E_THIS: return c->this_local ? true : fail(c, "it uses `this`, shared by every call");
    case E_INDEX:
        if (is_shared_ident(c, e->lhs) && prim_array_type(e->lhs->type)) return par_expr(c, e->rhs);
        return par_expr(c, e->lhs) && par_expr(c, e->rhs);
    case E_MEMBER:
        if (!e->is_call) {
            if (is_shared_ident(c, e->lhs) && prim_array_type(e->lhs->type) && e->str && strcmp(e->str, "length") == 0) return true;
            if (e->func && user_func(e->func)) return par_expr(c, e->lhs) && par_func(c, e->func, NULL, 0, true, false); /* a getter */
            return par_expr(c, e->lhs);
        }
        if (e->lhs && e->lhs->kind == E_IDENT && !e->lhs->cname && e->lhs->str) {
            if (!ns_call_ok(c, e->lhs->str, e->str)) return false;
            for (int i = 0; i < e->args.len; i++) if (!par_expr(c, e->args.items[i])) return false;
            return true;
        }
        if (e->func && !e->builtin.cfn) { /* a method of a user class, on an object fn owns */
            if (!user_func(e->func)) return fail(c, xs_sprintf("it calls the method '%s', whose body is not known here", e->str));
            if (!par_expr(c, e->lhs) || !par_call_args(c, e->func, &e->args, true)) return false;
            for (int i = 0; e->virt && i < e->func->noverrides; i++)
                if (!par_func(c, e->func->overrides[i], NULL, 0, true, false)) return false;
            return true;
        }
        if (!par_expr(c, e->lhs)) return false; /* a built-in method: on what fn owns */
        for (int i = 0; i < e->args.len; i++) if (!par_expr(c, e->args.items[i])) return false;
        return true;
    case E_CALL:
        if (e->func && !e->builtin.cfn) {
            if (par_kind(e)) { /* a parallel call inside fn: it runs as the loop there; its fn is checked as fn's own */
                for (int i = 0; i < e->args.len; i++) if (!par_expr(c, e->args.items[i])) return false;
                return true;
            }
            if (other_package(e->func)) return fail(c, xs_sprintf("it calls '%s' of %s, which may act outside the function", e->func->name, e->func->mod->file));
            if (!user_func(e->func)) return fail(c, xs_sprintf("it calls '%s', whose body is not known here", e->func->name));
            return par_call_args(c, e->func, &e->args, false);
        }
        if (e->builtin.cfn) {
            static const char *const ok[] = {"parseInt", "parseFloat", "Number", "String", "Boolean", "BigInt", "isNaN", "isFinite", NULL};
            const char *n = e->lhs && e->lhs->kind == E_IDENT ? e->lhs->str : NULL;
            bool known = false;
            for (int i = 0; n && ok[i]; i++) if (strcmp(n, ok[i]) == 0) known = true;
            if (!known) return fail(c, xs_sprintf("it calls %s(), which may act outside the function", n ? n : e->builtin.cfn));
            for (int i = 0; i < e->args.len; i++) if (!par_expr(c, e->args.items[i])) return false;
            return true;
        }
        return fail(c, "it calls a function value, whose body is not known here");
    case E_NEW_OBJ:
        if (e->cls && e->cls->builtin) {
            static const char *const ok[] = {"Error", "TypeError", "RangeError", "SyntaxError", "Array", "Uint8Array", "Float64Array",
                                             "Float32Array", "Int32Array", "Uint32Array", "Int8Array", "Int16Array", "Uint16Array",
                                             "Buffer", "TextEncoder", "TextDecoder", "RegExp", "Date", NULL};
            bool known = false;
            for (int i = 0; ok[i]; i++) if (e->cls->name && strcmp(e->cls->name, ok[i]) == 0) known = true;
            if (!known) return fail(c, xs_sprintf("it makes a %s, which acts outside the function", e->cls->name));
            for (int i = 0; i < e->args.len; i++) if (!par_expr(c, e->args.items[i])) return false;
            return true;
        }
        if (e->cls) {
            for (int i = 0; i < e->cls->nfields; i++) /* field initializers run in the constructor, with `this` the new object */
                if (e->cls->fields[i].init) {
                    PCtx in = *c;
                    in.this_local = true, in.env_local = false, in.shared = NULL, in.nshared = 0;
                    if (!par_expr(&in, e->cls->fields[i].init)) { c->why = in.why; return false; }
                }
            for (Class *k = e->cls; k; k = k->base) /* (its bases' constructors run too, through super) */
                if (k != e->cls && k->ctor && !par_func(c, k->ctor, NULL, 0, true, false)) return false;
            if (e->cls->ctor) return par_call_args(c, e->cls->ctor, &e->args, true);
        }
        for (int i = 0; i < e->args.len; i++) if (!par_expr(c, e->args.items[i])) return false;
        return true;
    case E_SUPER_CALL:
        for (int i = 0; i < e->args.len; i++) if (!par_expr(c, e->args.items[i])) return false;
        return true; /* (the base's constructor: checked with the class above) */
    case E_ASSIGN:
        return par_target(c, e->lhs) && par_expr(c, e->rhs);
    case E_POSTFIX:
        return par_target(c, e->lhs);
    case E_UNARY:
        if (e->op == TK_PLUSPLUS || e->op == TK_MINUSMINUS) return par_target(c, e->lhs);
        return par_expr(c, e->lhs);
    case E_ARROW: {
        Arrow *a = e->arrow;
        if (!a || !a->fn) return fail(c, "it makes a function whose body is not known");
        for (int i = 0; i < a->ncaptures; i++) {
            Capture *cp = &a->captures[i];
            if (cp->is_this && !c->this_local) return fail(c, "a function made inside it captures `this`, shared by every call");
            if (cp->is_this) continue;
            Expr id = {0};
            id.kind = E_IDENT;
            id.cname = cp->outer_cname;
            id.str = cp->name;
            id.type = cp->type;
            if (ident_kind(c, &id) == 1 && !prim_type(cp->type))
                return fail(c, xs_sprintf("a function made inside it captures '%s', shared by every call", cp->name));
        }
        PCtx in = *c;
        in.env_local = true, in.shared = NULL, in.nshared = 0;
        if (!par_stmt(&in, a->fn->body)) { c->why = in.why; return false; }
        return true;
    }
    case E_FUNC_REF: return fail(c, xs_sprintf("it uses the function '%s' as a value", e->func ? e->func->name : "?"));
    case E_AWAIT: return fail(c, "it waits (await): the calls would not be independent");
    default:
        if (!par_expr(c, e->lhs) || !par_expr(c, e->rhs)) return false;
        for (int i = 0; i < e->args.len; i++) if (!par_expr(c, e->args.items[i])) return false;
        return true;
    }
}

static bool par_stmt(PCtx *c, Stmt *s) {
    if (!s) return true;
    if (s->kind == S_YIELD) return fail(c, "it yields: the calls would not be independent");
    if (!par_expr(c, s->init) || !par_expr(c, s->expr) || !par_expr(c, s->cond) || !par_expr(c, s->for_update)) return false;
    if (!par_stmt(c, s->for_init) || !par_stmt(c, s->body) || !par_stmt(c, s->else_body)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!par_stmt(c, s->stmts.items[i])) return false;
    for (int i = 0; i < s->ncases; i++) if (!par_expr(c, s->cases[i].value) || !par_stmt(c, s->cases[i].body)) return false;
    return true;
}

/* f's body, its parameters in `shared` holding shared arrays; remembered when none do */
static bool par_func(PCtx *c, Func *f, const char **shared, int nshared, bool this_local, bool env_local) {
    if (!user_func(f)) return fail(c, xs_sprintf("it calls '%s', whose body is not known here", f ? f->name : "?"));
    bool memo = nshared == 0;
    if (memo && f->par_state == 1) return true; /* recursion: the body being checked decides */
    if (memo && f->par_state == 2) return true;
    if (memo && f->par_state == 3) return fail(c, f->par_why);
    if (c->depth > 12) return fail(c, "its calls go too deep to check");
    if (memo) f->par_state = 1;
    PCtx in = {shared, nshared, env_local, this_local, c->depth + 1, NULL};
    bool ok = par_stmt(&in, f->body);
    if (!ok) {
        const char *why = f->name && f->name[0] != '<' && !strstr(f->name, "arrow") ? xs_sprintf("it calls '%s': %s", f->name, in.why) : in.why;
        if (memo) f->par_state = 3, f->par_why = why;
        return fail(c, why);
    }
    if (memo) f->par_state = 2;
    return true;
}

static bool mentions(Expr *e, const char *cname);
static bool mentions_s(Stmt *s, const char *cname) {
    if (!s) return false;
    if (mentions(s->init, cname) || mentions(s->expr, cname) || mentions(s->cond, cname) || mentions(s->for_update, cname)) return true;
    if (mentions_s(s->for_init, cname) || mentions_s(s->body, cname) || mentions_s(s->else_body, cname)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (mentions_s(s->stmts.items[i], cname)) return true;
    for (int i = 0; i < s->ncases; i++) if (mentions(s->cases[i].value, cname) || mentions_s(s->cases[i].body, cname)) return true;
    return false;
}
static bool mentions(Expr *e, const char *cname) {
    if (!e) return false;
    if (e->kind == E_IDENT && e->cname && strcmp(e->cname, cname) == 0) return true;
    if (e->kind == E_ARROW && e->arrow) {
        for (int i = 0; i < e->arrow->ncaptures; i++)
            if (e->arrow->captures[i].outer_cname && strcmp(e->arrow->captures[i].outer_cname, cname) == 0) return true;
        if (e->arrow->fn && mentions_s(e->arrow->fn->body, cname)) return true;
    }
    if (mentions(e->lhs, cname) || mentions(e->rhs, cname)) return true;
    for (int i = 0; i < e->args.len; i++) if (mentions(e->args.items[i], cname)) return true;
    return false;
}

/* the template a call of xisto:parallel instantiates: "parallelMap"... (NULL: not one of them) */
static const char *par_kind(Expr *e) {
    if (e->kind != E_CALL || !e->func || !e->func->mod || !e->func->mod->file || strcmp(e->func->mod->file, "xisto:parallel") != 0) return NULL;
    Func *t = e->func->template_of ? e->func->template_of : e->func;
    static const char *const names[] = {"parallelMap", "parallelFrom", "parallelFill", "parallelReduce", NULL};
    for (int i = 0; names[i]; i++) if (t->name && strcmp(t->name, names[i]) == 0) return names[i];
    return NULL;
}

static bool result_ok(Type *t) { return prim_type(t) || ty_is_heap(t); }

/* can this call run on the pool? (why: the reason when not) */
static bool par_site(Expr *e, const char *kind, const char **why) {
    Func *inst = e->func;
    (void)kind;
    int fi = 1; /* fn is the second parameter of all four */
    if (e->args.len <= fi) { *why = "the call has no fn"; return false; }
    Expr *fn = e->args.items[fi];
    Type *ft = inst->params[fi].type;
    if (!ft || ft->kind != TY_FUNC) { *why = "fn is not a function"; return false; }
    if (!result_ok(ft->ret)) { *why = xs_sprintf("fn returns %s; the pool takes numbers, booleans, strings, arrays and objects", ty_name(ft->ret)); return false; }
    if (strcmp(kind, "parallelMap") == 0) {
        Type *items = inst->params[0].type;
        if (!items || items->kind != TY_ARRAY || !prim_type(items->elem)) {
            *why = xs_sprintf("the items are %s: the pool splits arrays of numbers or booleans (an array of indices works: parallelFrom)", ty_name(items));
            return false;
        }
    }
    PCtx c = {NULL, 0, false, false, 0, NULL};
    if (fn->kind == E_ARROW && fn->arrow && fn->arrow->fn) {
        for (int i = 0; i < fn->arrow->ncaptures; i++)
            if (fn->arrow->captures[i].is_this) { *why = "fn captures `this`, shared by every call"; return false; }
        PCtx in = c;
        if (!par_stmt(&in, fn->arrow->fn->body)) { *why = xs_sprintf("fn is not independent: %s", in.why); return false; }
    } else if (fn->kind == E_FUNC_REF && fn->func) {
        if (!par_func(&c, fn->func, NULL, 0, false, false)) { *why = xs_sprintf("fn is not independent: %s", c.why); return false; }
    } else {
        *why = "fn is not written at the call (an arrow or a function's name), so what it does is not known here";
        return false;
    }
    if (strcmp(kind, "parallelFill") == 0) {
        Expr *arr = e->args.items[0];
        if (arr->kind == E_IDENT && arr->cname && mentions(fn, arr->cname)) {
            *why = xs_sprintf("fn reads '%s', the array being filled", arr->str);
            return false;
        }
    }
    return true;
}

static Program *par_prog;
static const char *par_file, *par_src;
static void par_scan_e(Expr *e);
static void par_scan_s(Stmt *s) {
    if (!s) return;
    par_scan_e(s->init), par_scan_e(s->expr), par_scan_e(s->cond), par_scan_e(s->for_update);
    par_scan_s(s->for_init), par_scan_s(s->body), par_scan_s(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) par_scan_s(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) par_scan_e(s->cases[i].value), par_scan_s(s->cases[i].body);
}
static void par_scan_e(Expr *e) {
    if (!e) return;
    const char *kind = par_kind(e);
    static Expr **seen;
    static int nseen, capseen;
    bool again = false;
    for (int i = 0; kind && i < nseen; i++) if (seen[i] == e) again = true;
    if (kind && !again) {
        if (nseen == capseen) {
            capseen = capseen ? capseen * 2 : 16;
            Expr **n = xs_alloc(sizeof(Expr *) * (size_t)capseen);
            if (nseen) memcpy(n, seen, sizeof(Expr *) * (size_t)nseen);
            seen = n;
        }
        seen[nseen++] = e;
        const char *why = NULL;
        if (par_site(e, kind, &why)) {
            e->par_native = true;
            e->func->par_native = true;
            par_prog->par_native = true;
        } else {
            xs_file_name = par_file, xs_source = par_src;
            xs_warn(e->pos, "it runs as a plain loop, with the same result; see xisto:parallel for what fn may do",
                    "%s on one thread: %s", kind, why);
        }
    }
    par_scan_e(e->lhs), par_scan_e(e->rhs);
    for (int i = 0; i < e->args.len; i++) par_scan_e(e->args.items[i]);
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn) par_scan_s(e->arrow->fn->body);
}

void par_analyze(Program *p) {
    static Program *done;
    if (done == p) return; /* (compiled again for another output: the same answers, the warnings once) */
    done = p;
    par_prog = p;
    for (int i = 0; i < p->nfuncs; i++) {
        Func *f = p->funcs[i];
        if (!f->used || !f->body || (f->mod && f->mod->builtin) || (f->mod && f->mod->file && strcmp(f->mod->file, "xisto:parallel") == 0)) continue;
        par_file = f->mod ? f->mod->file : NULL, par_src = f->mod ? f->mod->src : NULL;
        par_scan_s(f->body);
    }
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (!c->used || c->builtin) continue;
        par_file = c->mod ? c->mod->file : NULL, par_src = c->mod ? c->mod->src : NULL;
        for (int k = 0; k < c->nmethods; k++) if (c->methods[k]->used && c->methods[k]->cls == c) par_scan_s(c->methods[k]->body);
        if (c->ctor) par_scan_s(c->ctor->body);
    }
    for (int i = 0; i < p->top.len; i++) {
        Stmt *s = p->top.items[i];
        par_file = s->mod ? s->mod->file : NULL, par_src = s->mod ? s->mod->src : NULL;
        par_scan_s(s);
    }
}
