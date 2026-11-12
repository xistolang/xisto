/* C emitter with compiler-inserted reference counting (see docs/poc2.md for the rules). */
#include "xisto.h"
#include <ctype.h>
#include <math.h>

/* reference-counted in the generated code: heap types plus string enums (their values are immortal literal
   strings, so retain/release cost nothing, and one rule keeps arrays, fields and locals consistent) */
static bool cref(Type *t) { return ty_is_heap(t) || (t->kind == TY_ENUM && t->en->is_string); }
/* does a value of this type take part in reference counting? Objects of a region class (region.c) do not: they
   are freed with their region, so their retains and releases are dropped (kept when XISTO_REGION_CHECK=1 at
   build time, which checks at run time that the counts come back to zero) */
static int region_checked = -1;
/* a class whose objects have no header (nohdr_compute): nothing ever counts, prints or frees them one by one */
static bool nohdr_ty(Type *t) {
    Type *u = t && t->kind == TY_NULLABLE ? t->elem : t;
    return u && u->kind == TY_OBJECT && u->cls && u->cls->nohdr;
}
/* a reference whose holder retains and releases it: every heap type but the header-less ones */
static bool crc(Type *t) { return cref(t) && !nohdr_ty(t); }
static bool rcnt(Type *t) {
    if (!cref(t)) return false;
    if (nohdr_ty(t)) return false;
    Type *u = t->kind == TY_NULLABLE ? t->elem : t;
    if (u->kind == TY_OBJECT && u->cls && u->cls->region) {
        if (region_checked < 0) region_checked = getenv("XISTO_REGION_CHECK") && strcmp(getenv("XISTO_REGION_CHECK"), "1") == 0;
        return region_checked;
    }
    return true;
}
/* T | null of a primitive holds a sentinel (runtime: XS_NULL_INT, XS_NULL_BOOL, xs_null_num) */
static bool null_prim(Type *t) { return t->kind == TY_NULLABLE && !cref(t->elem); }
static bool type_has_tparam(Type *t) {
    if (!t) return false;
    if (t->kind == TY_TPARAM) return true;
    if (type_has_tparam(t->elem) || type_has_tparam(t->key) || (t->kind == TY_FUNC && type_has_tparam(t->ret))) return true;
    for (int i = 0; i < t->nparams; i++) if (type_has_tparam(t->params[i])) return true;
    return false;
}
static const char *null_value(Type *t) {
    if (!null_prim(t)) return "NULL";
    switch (t->elem->kind) {
    case TY_NUMBER: return "xs_null_num()";
    case TY_BOOLEAN: return "XS_NULL_BOOL";
    default: return "XS_NULL_INT";
    }
}
/* C condition "expr is not null" for a value of nullable type t */
static char *not_null(Type *t, const char *expr) {
    if (!null_prim(t)) return xs_sprintf("((%s) != NULL)", expr);
    switch (t->elem->kind) {
    case TY_NUMBER: return xs_sprintf("(!xs_isnull_num(%s))", expr);
    case TY_BOOLEAN: return xs_sprintf("((%s) != XS_NULL_BOOL)", expr);
    default: return xs_sprintf("((%s) != XS_NULL_INT)", expr);
    }
}
static bool int_like(Type *t) { return t->kind == TY_INT || (t->kind == TY_ENUM && !t->en->is_string); }
/* the element type that picks an array's C struct: (number | null)[] and (int | null)[] are number[] and int[]
   holding the null sentinel (xs_null_num / XS_NULL_INT), as a T | null variable does */
static Type *arr_ek(Type *elem) {
    if (elem->kind == TY_NULLABLE && (elem->elem->kind == TY_NUMBER || int_like(elem->elem))) return elem->elem;
    return elem;
}

bool xs_profile;
bool xs_debug;

/* ---------- debugger hooks (--debug; runtime/xisto_dbg.h, docs/debugger.md) ----------
   One point per statement: XS_DBG(point) before it, and the table of points (file, line, column) at the end. */
static Buf dbg_tab;
static int ndbg;
static Module *cur_mod;
static Func *cur_func;
static int dbg_point(Pos pos) {
    Module *m = cur_func && cur_func->mod ? cur_func->mod : cur_mod;
    if (ndbg) buf_puts(&dbg_tab, ",\n");
    buf_puts(&dbg_tab, "    {");
    const char *path = m && m->path ? m->path : "";
    buf_putc(&dbg_tab, '"');
    for (const char *c = path; *c; c++) {
        if (*c == '"' || *c == '\\') buf_putc(&dbg_tab, '\\');
        buf_putc(&dbg_tab, *c);
    }
    buf_printf(&dbg_tab, "\", %d, %d}", pos.line, pos.col);
    return ndbg++;
}
static void emit_dbg_enter(Func *f);
static void emit_dbg_finish(void);
static void dbg_params(Func *f, bool arrow_env, Arrow *a);
static void emit_dbg_hook(Stmt *s);
static const char *dbg_func_name(Func *f) {
    if (!f) return "(top level)";
    if (f->is_ctor && f->cls) return xs_sprintf("new %s", f->cls->name);
    if (f->name && strcmp(f->name, "arrow") == 0 && !f->cls) return "<anonymous>";
    if (f->cls) return xs_sprintf("%s.%s", f->cls->name, f->name);
    return f->name;
}
static bool *lit_used; /* literals referenced by emitted code: only those are declared */
static int lit_ref(int id) { if (lit_used) lit_used[id] = true; return id; }
static Buf *out;
static int indent;
static Program *prog;
/* a literal's C name comes from its text, not its number: a new literal elsewhere renames nothing (the --dev
   units cached by their text survive an edit). Two texts with the same 64-bit hash would not compile. */
static char *lit_cname(int id) {
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char *c = (const unsigned char *)prog->lits[id]; *c; c++) { h ^= *c; h *= 1099511628211ull; }
    return xs_sprintf("lit_%016llx", (unsigned long long)h);
}
static char *lit_nm(int id) { return lit_cname(lit_ref(id)); }
/* cur_func: function being emitted (NULL for main), declared with the debugger hooks above */
static bool in_ctor;
static bool ctor_cln; /* the constructor registered self on the cleanup stack: its returns take it off */
static bool in_init;   /* emitting Class_init (inheritance): a return leaves without a value */
static Class *inits_after_super; /* the class whose own field initializers follow its super(...) */
static void emit_own_field_inits(Class *c);
static bool used_ar;        /* the current statement pushed something onto the autorelease pool */

/* ---------- stack-trace sites (runtime/xisto_rt.h, "stack traces") ----------
   Every call gets a site and an XS_POST label right after it; operations that fail inline (integer
   overflow, array index, int conversion) get a site passed as an argument. */
static Buf site_tab;        /* initializers of xs_sitetab; index 0 is a placeholder */
static int nsites;
/* cur_mod: module of the code being emitted (top level, arrows), declared with the debugger hooks above */
static Class *cur_class;    /* class whose field initializers are being emitted */
static Expr *bracketing;    /* the call being labelled right now: emit_expr must not label it twice */

static void site_str(Buf *b, const char *s) {
    buf_putc(b, '"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') buf_printf(b, "\\%c", *s);
        else if ((unsigned char)*s < 32) buf_printf(b, "\\%03o", (unsigned char)*s);
        else buf_putc(b, *s);
    }
    buf_putc(b, '"');
}
static bool inlined_once(Func *f);
static bool stmt_has_try(Stmt *s);
static int cur_rg; /* rg-hoist: the dual region being emitted (definition and notes below, with the region versions) */
static bool rg_hoist_on; /* (defined with cur_rg below) */
static const char *rg_call_name(Func *f);
static bool rg_inline_new(Expr *e);
static void mark_init_stores(Class *c, Func *ctor);
static Func *clone_func;    /* the function being emitted as a per-call-site copy ... */
static int clone_inl;       /* ... for this call site */
static const char *force_storage;
static int call1_site(Func *f);
/* a site in function f (NULL: top level, or the field initializers of cls) of module m */
static int new_site_in(Pos pos, Func *f, Class *cls, Module *m) {
    if (!nsites) { buf_puts(&site_tab, "    {0}"); nsites = 1; }
    const char *fn = "";
    if (f && f->name && strcmp(f->name, "arrow") == 0 && !f->cls) fn = ""; /* anonymous, printed without a name as in Node */
    else if (f && f->is_ctor && f->cls) fn = xs_sprintf("new %s", f->cls->name);
    else if (f && f->cls) fn = xs_sprintf("%s.%s", f->cls->name, f->name);
    else if (f) fn = f->name;
    else if (cls) fn = xs_sprintf("new %s", cls->name);
    if (f && f->mod) m = f->mod;
    else if (!f && cls) m = cls->mod;
    /* a function inlined into its only caller: its frames are followed by that call, statically */
    int inl = f && f == clone_func ? clone_inl : f && inlined_once(f) ? call1_site(f) : 0;
    Buf e = {0};
    buf_puts(&e, ",\n    {");
    site_str(&e, fn);
    buf_puts(&e, ", ");
    site_str(&e, m && m->file ? xs_display_path(m->file) : "?");
    if (inl) buf_printf(&e, ", &xs_sitetab[%d]", inl);
    else buf_puts(&e, ", 0");
    buf_printf(&e, ", %d, %d}", pos.line, pos.col);
    buf_puts(&site_tab, e.data);
    free(e.data);
    return nsites++;
}
static int new_site(Pos pos) { return new_site_in(pos, cur_func, cur_class, cur_mod); }
/* ---- functions called from exactly one place ----
   They are inlined (always_inline) even when they can fail: each of their sites points (inl) at that one
   call, so the frame the inlining removed is put back when a trace is printed. */
static void count_calls_stmt(Stmt *s, Func *in, Module *m);
static void count_call(Func *f, Expr *e, Func *in, Module *m) {
    if (!f || !f->body) return;
    if (f->ncalls < 0) return;
    if (m == NULL) { f->ncalls = -1; return; } /* class field initializers: not a function frame */
    if (++f->ncalls == 1) { f->call1 = e; f->call1_in = in; f->call1_mod = m; }
}
static void count_calls_expr(Expr *e, Func *in, Module *m) {
    if (!e) return;
    switch (e->kind) {
    case E_FUNC_REF: if (e->func) e->func->ncalls = -1; break; /* a value: called from anywhere */
    case E_CALL: if (e->func && !e->builtin.cfn) count_call(e->func, e, in, m); break;
    case E_MEMBER: if (e->is_call && e->func && e->cls && !e->cls->builtin && !e->builtin.cfn) count_call(e->func, e, in, m); break;
    case E_NEW_OBJ: if (!e->cls->builtin && e->cls->ctor) count_call(e->cls->ctor, e, in, m); break;
    case E_ARROW:
        count_calls_stmt(e->arrow->fn->body, e->arrow->fn, m);
        return;
    default: break;
    }
    count_calls_expr(e->lhs, in, m);
    count_calls_expr(e->rhs, in, m);
    for (int i = 0; i < e->args.len; i++) count_calls_expr(e->args.items[i], in, m);
}
static void count_calls_stmt(Stmt *s, Func *in, Module *m) {
    if (!s) return;
    count_calls_expr(s->init, in, m);
    count_calls_expr(s->expr, in, m);
    count_calls_expr(s->cond, in, m);
    count_calls_expr(s->for_update, in, m);
    count_calls_stmt(s->for_init, in, m);
    count_calls_stmt(s->body, in, m);
    count_calls_stmt(s->else_body, in, m);
    for (int i = 0; i < s->stmts.len; i++) count_calls_stmt(s->stmts.items[i], in, m);
    for (int i = 0; i < s->ncases; i++) { count_calls_expr(s->cases[i].value, in, m); count_calls_stmt(s->cases[i].body, in, m); }
}
/* a class's toJSON(): string (bson's ObjectId): JSON.stringify writes what it returns, as a string */
static Func *json_tojson(Class *c) {
    for (Class *k = c; k; k = k->base)
        for (int i = 0; i < k->nmethods; i++) {
            Func *f = k->methods[i];
            if (strcmp(f->name, "toJSON") == 0 && f->nparams == 0 && f->ret && f->ret->kind == TY_STRING && f->used && f->body) return f;
        }
    return NULL;
}
static void count_calls(Program *p) {
    for (int i = 0; i < p->nfuncs; i++) { p->funcs[i]->ncalls = 0; p->funcs[i]->call1 = NULL; p->funcs[i]->call1_site = 0; p->funcs[i]->fail_state = 0; p->funcs[i]->clone_state = 0; }
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (c->ctor) { c->ctor->ncalls = 0; c->ctor->call1 = NULL; c->ctor->call1_site = 0; c->ctor->fail_state = 0; }
        for (int k = 0; k < c->nmethods; k++) { c->methods[k]->ncalls = 0; c->methods[k]->call1 = NULL; c->methods[k]->call1_site = 0; c->methods[k]->fail_state = 0; c->methods[k]->clone_state = 0; }
    }
    for (int i = 0; i < p->nfuncs; i++) if (!p->funcs[i]->mod->builtin && !p->funcs[i]->is_native && p->funcs[i]->used) count_calls_stmt(p->funcs[i]->body, p->funcs[i], p->funcs[i]->mod);
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (!c->used) continue;
        for (int k = 0; k < c->nfields; k++) count_calls_expr(c->fields[k].init, NULL, NULL);
        if (c->ctor) count_calls_stmt(c->ctor->body, c->ctor, c->mod);
        for (int k = 0; k < c->nmethods; k++) if (c->methods[k]->used && c->methods[k]->cls == c) count_calls_stmt(c->methods[k]->body, c->methods[k], c->mod);
    }
    for (int i = 0; i < p->top.len; i++) count_calls_stmt(p->top.items[i], NULL, p->top.items[i]->mod);
    for (int i = 0; i < p->nclasses; i++) { /* toJSON(): also called by the JSON writer of its class */
        Func *tj = p->classes[i]->used ? json_tojson(p->classes[i]) : NULL;
        if (tj) tj->ncalls = -1;
    }
}

/* ---- typed-array parameters of a fixed length ----
   A Float64Array/Uint8Array never changes its length, so a parameter has length N when every call of its function
   passes an array of length N: a local `const a = new Float64Array(N)` of the caller, or a parameter of the caller
   that has length N itself (to a fixed point). Then its indexing is checked against N like the caller's local is
   (bce_fixed_n), and with the ranges of the indices the checks go: nbody-5's advance indexes p[4 * PI[q] + k].
   The calls are visited the way count_calls visits them; a function with a call this pass did not see (ncalls
   differs), or used as a value, keeps every parameter unknown. XISTO_NO_FIXPARAM=1 turns it off. */
static int64_t bce_fixed_of(Stmt *s);
static bool fixp_typed(Type *t) {
    return t && t->kind == TY_OBJECT && t->cls && t->cls->builtin &&
           (strcmp(t->cls->cname, "xs_Float64Array") == 0 || strcmp(t->cls->cname, "xs_Uint8Array") == 0);
}
static int64_t fixp_local(Stmt *s, const char *c) {
    if (!s) return 0;
    if (s->kind == S_VAR && s->cname && strcmp(s->cname, c) == 0) return bce_fixed_of(s);
    int64_t v;
    if ((v = fixp_local(s->for_init, c)) || (v = fixp_local(s->body, c)) || (v = fixp_local(s->else_body, c))) return v;
    for (int i = 0; i < s->stmts.len; i++) if ((v = fixp_local(s->stmts.items[i], c))) return v;
    for (int i = 0; i < s->ncases; i++) if ((v = fixp_local(s->cases[i].body, c))) return v;
    return 0;
}
static Program *fixp_prog;
static int64_t fixp_arg(Expr *a, Func *in) {
    if (!a || a->kind != E_IDENT || !a->cname) return 0;
    if (in) {
        for (int i = 0; i < in->nparams; i++)
            if (in->params[i].cname && strcmp(in->params[i].cname, a->cname) == 0) return in->fixparam ? (in->fixparam[i] > 0 ? in->fixparam[i] : 0) : 0;
        return fixp_local(in->body, a->cname);
    }
    for (int i = 0; i < fixp_prog->top.len; i++) { int64_t v = fixp_local(fixp_prog->top.items[i], a->cname); if (v) return v; }
    return 0;
}
static int *fixp_seen; static Func **fixp_fn; static int fixp_n;
static void fixp_call(Func *f, Expr *e, Func *in) {
    if (!f || !f->body || !f->fixparam) return;
    for (int k = 0; k < fixp_n; k++) if (fixp_fn[k] == f) { fixp_seen[k]++; break; }
    for (int i = 0; i < f->nparams; i++) {
        if (!fixp_typed(f->params[i].type)) continue;
        int64_t n = i < e->args.len ? fixp_arg(e->args.items[i], in) : 0;
        if (f->fixparam[i] == -1) f->fixparam[i] = n;
        else if (f->fixparam[i] != n) f->fixparam[i] = 0;
    }
}
static void fixp_stmt(Stmt *s, Func *in);
static void fixp_expr(Expr *e, Func *in) {
    if (!e) return;
    switch (e->kind) {
    case E_CALL: if (e->func && !e->builtin.cfn) fixp_call(e->func, e, in); break;
    case E_MEMBER: if (e->is_call && e->func && e->cls && !e->cls->builtin && !e->builtin.cfn) fixp_call(e->func, e, in); break;
    case E_NEW_OBJ: if (!e->cls->builtin && e->cls->ctor) fixp_call(e->cls->ctor, e, in); break;
    case E_ARROW: fixp_stmt(e->arrow->fn->body, e->arrow->fn); return;
    default: break;
    }
    fixp_expr(e->lhs, in);
    fixp_expr(e->rhs, in);
    for (int i = 0; i < e->args.len; i++) fixp_expr(e->args.items[i], in);
}
static void fixp_stmt(Stmt *s, Func *in) {
    if (!s) return;
    fixp_expr(s->init, in); fixp_expr(s->expr, in); fixp_expr(s->cond, in); fixp_expr(s->for_update, in);
    fixp_stmt(s->for_init, in); fixp_stmt(s->body, in); fixp_stmt(s->else_body, in);
    for (int i = 0; i < s->stmts.len; i++) fixp_stmt(s->stmts.items[i], in);
    for (int i = 0; i < s->ncases; i++) { fixp_expr(s->cases[i].value, in); fixp_stmt(s->cases[i].body, in); }
}
static void fixparam_compute(Program *p) {
    fixp_prog = p;
    fixp_n = 0;
    free(fixp_fn); free(fixp_seen);
    fixp_fn = calloc((size_t)(p->nfuncs + 1), sizeof(Func *));
    fixp_seen = calloc((size_t)(p->nfuncs + 1), sizeof(int));
    for (int i = 0; i < p->nfuncs; i++) {
        Func *f = p->funcs[i];
        f->fixparam = NULL;
        if (xs_debug || getenv("XISTO_NO_FIXPARAM") || f->mod->builtin || f->is_native || !f->used || !f->body || f->escapes || f->ncalls <= 0 || f->is_gen || f->is_async) continue;
        bool any = false;
        for (int k = 0; k < f->nparams; k++) if (fixp_typed(f->params[k].type) && !f->params[k].cell) any = true;
        if (!any) continue;
        f->fixparam = calloc((size_t)f->nparams, sizeof(int64_t));
        for (int k = 0; k < f->nparams; k++) f->fixparam[k] = fixp_typed(f->params[k].type) && !f->params[k].cell ? -1 : 0;
        fixp_fn[fixp_n++] = f;
    }
    if (!fixp_n) return;
    for (int round = 0; round < 4; round++) {
        int64_t *before = calloc((size_t)fixp_n * 16, sizeof(int64_t));
        for (int k = 0; k < fixp_n; k++) { fixp_seen[k] = 0; for (int i = 0; i < fixp_fn[k]->nparams && i < 16; i++) before[k * 16 + i] = fixp_fn[k]->fixparam[i]; }
        /* each round starts from the previous answer: unset (-1) parameters count as "any" */
        for (int i = 0; i < p->nfuncs; i++) if (!p->funcs[i]->mod->builtin && !p->funcs[i]->is_native && p->funcs[i]->used) fixp_stmt(p->funcs[i]->body, p->funcs[i]);
        for (int i = 0; i < p->nclasses; i++) {
            Class *c = p->classes[i];
            if (!c->used) continue;
            for (int k = 0; k < c->nfields; k++) fixp_expr(c->fields[k].init, NULL);
            if (c->ctor) fixp_stmt(c->ctor->body, c->ctor);
            for (int k = 0; k < c->nmethods; k++) if (c->methods[k]->used && c->methods[k]->cls == c) fixp_stmt(c->methods[k]->body, c->methods[k]);
        }
        for (int i = 0; i < p->top.len; i++) fixp_stmt(p->top.items[i], NULL);
        bool same = true;
        for (int k = 0; k < fixp_n; k++) for (int i = 0; i < fixp_fn[k]->nparams && i < 16; i++) if (before[k * 16 + i] != fixp_fn[k]->fixparam[i]) same = false;
        free(before);
        if (same) break;
    }
    for (int k = 0; k < fixp_n; k++) {
        Func *f = fixp_fn[k];
        for (int i = 0; i < f->nparams; i++) if (f->fixparam[i] < 0 || fixp_seen[k] != f->ncalls) f->fixparam[i] = 0;
    }
}
static bool inlined_once(Func *f) {
    if (getenv("XISTO_NO_TRACE")) return false;
    if (f->ncalls != 1 || !f->call1 || f->is_async || !f->body || stmt_has_try(f->body) || f->virt_target || f->is_gen) return false;
    /* not recursive, directly or through other functions inlined into their only caller */
    int steps = 0; /* (a cycle among its callers that does not reach f: two functions that only call each other) */
    for (Func *g = f->call1_in; g; g = g->ncalls == 1 ? g->call1_in : NULL) if (g == f || ++steps > 256) return false;
    return true;
}
static int call1_site(Func *f) {
    if (!f->call1_site) {
        Func *in = f->call1_in;
        f->call1_site = -1; /* guard: the caller's own inl is created below */
        int id = new_site_in(f->call1->pos, in, NULL, f->call1_mod);
        f->call1_site = id;
    }
    return f->call1_site > 0 ? f->call1_site : 0;
}

/* Can a call to this function end in an error? Only those functions can be frames of a stack trace, so
   only they must stay out of line (XS_FN); the others (field setters, number math, allocation) are
   XS_FI and the C compiler inlines them as before. Recursion counts as "can fail" (conservative). */
static bool func_fails(Func *f);
static bool class_init_fails(Class *c);
static bool is_math_call(Expr *e);
static bool stmt_fails(Stmt *s);
static bool expr_fails(Expr *e) {
    if (!e) return false;
    switch (e->kind) {
    case E_CALL:
        if (e->func && e->func->body && !e->builtin.cfn && !(e->lhs && e->lhs->type && e->lhs->type->kind == TY_FUNC && !e->func)) {
            if (func_fails(e->func)) return true;
            break;
        }
        return true; /* built-ins and closure values */
    case E_NEW_OBJ:
        if (e->cls->builtin) return true;
        if (e->cls->ctor ? func_fails(e->cls->ctor) : class_init_fails(e->cls)) return true;
        break;
    case E_MEMBER:
        if (e->is_call) {
            if (is_math_call(e)) {
                const char *m = e->str;
                bool int_args = e->args.len > 0 && e->args.items[0]->type->kind == TY_INT;
                if (strcmp(m, "round") == 0 || strcmp(m, "floor") == 0 || strcmp(m, "ceil") == 0 || strcmp(m, "trunc") == 0 || (strcmp(m, "abs") == 0 && int_args)) return true;
                break;
            }
            if (e->func && e->func->body && e->cls && !e->cls->builtin && !e->builtin.cfn) {
                if (func_fails(e->func)) return true;
                break;
            }
            return true;
        }
        break;
    case E_INDEX: case E_TEMPLATE: case E_AWAIT: return true;
    case E_BINARY:
        if (e->lhs->type && e->lhs->type->kind == TY_INT && (e->op == TK_PLUS || e->op == TK_MINUS || e->op == TK_STAR || e->op == TK_PERCENT)) return true;
        break;
    case E_UNARY:
        if (e->op == TK_MINUS && e->type && e->type->kind == TY_INT) return true;
        break;
    case E_POSTFIX:
        if (e->type && e->type->kind == TY_INT) return true;
        break;
    case E_ASSIGN:
        if (e->type && e->type->kind == TY_INT && e->op != TK_ASSIGN) return true;
        break;
    case E_ARROW: return false; /* creating a closure; its body is another function */
    default: break;
    }
    if (expr_fails(e->lhs) || expr_fails(e->rhs)) return true;
    for (int i = 0; i < e->args.len; i++) if (expr_fails(e->args.items[i])) return true;
    return false;
}
static bool stmt_fails(Stmt *s) {
    if (!s) return false;
    switch (s->kind) {
    case S_THROW: case S_TRY: case S_FOROF: return true;
    case S_SWITCH:
        if (expr_fails(s->expr)) return true;
        for (int i = 0; i < s->ncases; i++) if (expr_fails(s->cases[i].value) || stmt_fails(s->cases[i].body)) return true;
        return false;
    default: break;
    }
    if (expr_fails(s->init) || expr_fails(s->expr) || expr_fails(s->cond) || expr_fails(s->for_update)) return true;
    if (stmt_fails(s->for_init) || stmt_fails(s->body) || stmt_fails(s->else_body)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (stmt_fails(s->stmts.items[i])) return true;
    return false;
}
static bool class_init_fails(Class *c) {
    for (int i = 0; i < c->nfields; i++) if (expr_fails(c->fields[i].init)) return true;
    return false;
}
static bool func_fails(Func *f) {
    if (f->fail_state == 2) return false;
    if (f->fail_state == 1 || f->fail_state == 3 || f->is_async || !f->body) return true;
    f->fail_state = 1;
    bool fails = stmt_fails(f->body) || (f->is_ctor && f->cls && class_init_fails(f->cls));
    f->fail_state = fails ? 3 : 2;
    return fails;
}
/* the C storage of a user function: out of line only when it can be a frame of a stack trace */
/* a constructor that only stores its arguments into a few fields (binarytrees' TreeNode): inlined at every
   `new`, so the allocation and the stores sit in the caller (Xeon: -4%; it cannot fail, so no frame is lost) */
static bool small_ctor(Func *f) {
    if (!f->is_ctor || !f->cls || f->cls->nfields > 4 || func_fails(f)) return false;
    if (!f->body || f->body->stmts.len > f->cls->nfields) return false;
    for (int i = 0; i < f->body->stmts.len; i++) {
        Stmt *st = f->body->stmts.items[i];
        if (st->kind != S_EXPR || st->expr->kind != E_ASSIGN || st->expr->op != TK_ASSIGN) return false;
        if (!st->expr->lhs || st->expr->lhs->kind != E_MEMBER || st->expr->lhs->lhs->kind != E_THIS) return false;
        if (st->expr->rhs->kind != E_IDENT && st->expr->rhs->kind != E_NUMBER && st->expr->rhs->kind != E_NULL) return false;
    }
    return true;
}
static const char *fn_storage(Func *f) {
    if (force_storage) return force_storage;
    if (small_ctor(f)) return "XS_FII";
    return !func_fails(f) ? "XS_FI" : inlined_once(f) ? "XS_FA" : "XS_FN";
}

/* ---- small functions that can fail, called from several places: one inlined copy per call site ----
   Each copy's sites point (inl) at its own call, so the trace stays exact and the C compiler inlines as it
   would without stack traces. Only small leaves (no arrows, no calls to user functions that can fail). */
static Buf clone_fns;
static int nclones;
static int body_nodes_e(Expr *e) {
    if (!e) return 0;
    if (e->kind == E_ARROW) return 1000;
    int n = 1 + body_nodes_e(e->lhs) + body_nodes_e(e->rhs);
    for (int i = 0; i < e->args.len; i++) n += body_nodes_e(e->args.items[i]);
    return n;
}
static int body_nodes(Stmt *s) {
    if (!s) return 0;
    int n = 1 + body_nodes_e(s->init) + body_nodes_e(s->expr) + body_nodes_e(s->cond) + body_nodes_e(s->for_update)
            + body_nodes(s->for_init) + body_nodes(s->body) + body_nodes(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) n += body_nodes(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) n += body_nodes_e(s->cases[i].value) + body_nodes(s->cases[i].body);
    return n;
}
static bool calls_failing_user_e(Expr *e) {
    if (!e) return false;
    if ((e->kind == E_CALL || (e->kind == E_MEMBER && e->is_call)) && e->func && e->func->body && !e->builtin.cfn && func_fails(e->func)) return true;
    if (e->kind == E_NEW_OBJ && !e->cls->builtin && (e->cls->ctor ? func_fails(e->cls->ctor) : class_init_fails(e->cls))) return true;
    if (calls_failing_user_e(e->lhs) || calls_failing_user_e(e->rhs)) return true;
    for (int i = 0; i < e->args.len; i++) if (calls_failing_user_e(e->args.items[i])) return true;
    return false;
}
static bool calls_failing_user(Stmt *s) {
    if (!s) return false;
    if (calls_failing_user_e(s->init) || calls_failing_user_e(s->expr) || calls_failing_user_e(s->cond) || calls_failing_user_e(s->for_update)) return true;
    if (calls_failing_user(s->for_init) || calls_failing_user(s->body) || calls_failing_user(s->else_body)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (calls_failing_user(s->stmts.items[i])) return true;
    for (int i = 0; i < s->ncases; i++) if (calls_failing_user_e(s->cases[i].value) || calls_failing_user(s->cases[i].body)) return true;
    return false;
}
static bool cloned_per_site(Func *f) {
    if (f->clone_state) return f->clone_state == 1;
    bool ok = !getenv("XISTO_NO_TRACE") && f->body && !f->is_ctor && !f->is_async && !f->virt_target && !f->is_gen && !f->deco_stub && f->ncalls >= 2 && func_fails(f)
              && !stmt_has_try(f->body) && body_nodes(f->body) <= 80 && !calls_failing_user(f->body);
    f->clone_state = ok ? 1 : 2;
    return ok;
}
static void emit_func_body(Func *f);
static void emit_signature(Func *f);
static char *emit_clone(Func *f, int csite);
static int fx_find(const char *c);
static void ix_prove(Stmt *body);
static bool ix_arr_ok(Expr *a);
static bool ix_val(Expr *e);
static bool fx_decl[32];
static void emit_promise(Expr *e);

/* ", XS_S(n)": the site argument of the checked operations */
static void site_arg(Pos pos) { buf_printf(out, ", XS_S(%d)", new_site(pos)); }

/* ---------- scope tracking for releases ---------- */

typedef enum { SC_BLOCK, SC_FUNC, SC_LOOP_BODY, SC_SWITCH, SC_TRY } ScopeKind;
typedef struct EScope {
    char **names;
    Type **types;      /* the locals' types: an object of a class without subclasses is freed by a direct call */
    int n, cap;
    bool func_root;
    ScopeKind kind;
    struct EScope *parent;
    struct ICtx *inl;  /* a yield block of an inlined generator: the context whose consumer loop it is */
    int cont_id;       /* ... and the label a continue of that loop jumps to */
    Stmt *fin;         /* SC_TRY of a try with finally: what a return/break/continue leaving it runs first */
} EScope;

/* ---- generators inlined into their for...of ----
   for (const x of g(a)) body, with g a generator the emitter can see, becomes g's body in place, each
   `yield v` becoming { x = v; body }: no stream, no coroutine. The generator's locals are renamed (g<id>_name)
   so the loop body still sees the caller's; break/continue of the loop and the generator's return jump by goto,
   releasing the scopes they leave. A stream parameter bound to another inlinable generator call, iterated once,
   is inlined too (evens(naturals(n)) becomes two nested loops). */
typedef struct Prep {
    Func *f;
    int id;
    const char **dparam; /* stream parameters bound to a generator call, inlined where they are iterated */
    struct Prep **dprep;
    int nd;
} Prep;
typedef struct ICtx {
    Func *f;
    int id;
    const char **from, **to;
    int n, cap;
    Stmt *loop;            /* the consumer for...of */
    struct ICtx *parent;   /* the context the consumer loop is emitted in */
    EScope *root;
    Prep *prep;
} ICtx;
static ICtx *ictx;
typedef struct { bool ok; int64_t lo, hi; } Rng; /* integer ranges (see rng_of) */
typedef struct BceGArr { const char *name; int64_t n; bool has_rng; int64_t lo, hi; bool pow2; } BceGArr; /* a constant array (see garr_collect) */
static BceGArr *garr_find(const char *c);
static Rng rng_of(Expr *e);
static bool pow2_of(Expr *e, int depth);
static bool rv_valid; /* (the range tables, below) */
static Func *rv_owner;
static bool nn_expr(Expr *e);
static bool may_nz(Expr *e); /* XB-087: an int expression that may be JavaScript's -0 */
static int nz_mute; /* emitting where a zero's sign cannot be seen (x + 1, x / 2 + Number(i)): no -0 work */
static void emit_nzd(Expr *e); /* ... and its value as a double, -0 kept */
static const char **nzv; /* int locals that keep their -0 in a double beside them (<cname>_z), of nzv_owner */
static int nnzv;
static Func *nzv_owner;
typedef struct NzvSet { Func *owner; const char **names; int n; struct NzvSet *next; } NzvSet;
static NzvSet *nzv_sets; /* every function's (a clone emitted inside another function recomputes its own) */
static bool nzv_has(const char *c) {
    for (NzvSet *z = nzv_sets; z && c; z = z->next)
        if (z->owner == cur_func) { for (int i = 0; i < z->n; i++) if (strcmp(z->names[i], c) == 0) return true; return false; }
    return false;
}
static int byte_hoist_find(Expr *a);
static bool bce_safe(Expr *e); /* an index the loop already checks (see bce_enter) */
/* u8reg: byte arrays a loop keeps in vector registers (see u8reg_loop) */
typedef struct { int kind; Expr *a, *src, *lo, *hi, *ca, *cb; Stmt *loop; } U8Idiom; /* kind 1 copy a[j] = src[j], 2 reverse, 3 shift a[j] = a[j+1], 4 trade (a buf, src the table, ca/cb the cursors) */
static bool match_u8_idiom(Stmt *s, U8Idiom *m);
static bool u8_is_index_on(Expr *e, const char *c);
static bool lane_loop(Stmt *s); /* (XB-085) */
static bool ctr_inc_free(Expr *e); /* (XB-089) */
static bool bce_writes(Stmt *s, const char *n);
static bool same_simple(Expr *a, Expr *b);
static bool bce_pure(Expr *e);
static bool stmt_mentions_name(Stmt *s, const char *c);
typedef struct { const char *cname; int64_t n; bool i64; } U8Reg; /* i64: an int[] proved to hold bytes (n: at run time) */
static U8Reg u8regs[8];
static int nu8regs;
static U8Reg *u8reg_find(Expr *e) {
    if (!e || e->kind != E_IDENT || !e->cname) return NULL;
    for (int k = 0; k < nu8regs; k++) if (strcmp(u8regs[k].cname, e->cname) == 0) return &u8regs[k];
    return NULL;
}
static bool i64reg(Expr *e) { U8Reg *r = u8reg_find(e); return r && r->i64; }
/* the register's length as the helpers take it: a literal, or the int[]'s (XS_V16_ARR: its error message) */
static const char *rgn(U8Reg *r) {
    static char b[4][96];
    static int k;
    k = (k + 1) & 3;
    if (r->i64) snprintf(b[k], sizeof b[k], "nr_%s", r->cname);
    else snprintf(b[k], sizeof b[k], "INT64_C(%lld)", (long long)r->n);
    return b[k];
}
static void emit_u8reg_name(Expr *e) { buf_printf(out, "r_%s", e->cname); }
static void emit_bce_index(Expr *i);
static int inl_ids;
/* a generator compiled as a state machine (emit_gen_sm): its locals, parameters and self live in the frame */
static const char *tmpl_into; /* the next short template's buffer (a string on the stack, stackalloc.c) */
static bool sm_mode;
static const char **sm_names;
static int sm_n, sm_states;
static const char *ren(const char *name) {
    if (sm_mode && name) {
        for (int i = 0; i < sm_n; i++) if (strcmp(sm_names[i], name) == 0) return xs_sprintf("fr_->%s", name);
    }
    if (!ictx || !name) return name;
    for (int i = 0; i < ictx->n; i++) if (strcmp(ictx->from[i], name) == 0) return ictx->to[i];
    return name;
}

static EScope *escope;
static void emit_releases(EScope *s);
static void emit_block(Stmt *b, bool func_root);
static Stmt *blk_prev_stmt; /* in the block loops: the statement before the one being emitted, in the same block */
static bool dctr_is(Expr *e);
static Stmt *blk_cur; /* the block whose statement is being emitted, and its index (dctr) */
static int blk_cur_i;
static void nl(void);

static void epush(bool func_root) {
    EScope *s = xs_alloc(sizeof *s);
    s->func_root = func_root;
    s->kind = func_root ? SC_FUNC : SC_BLOCK;
    s->parent = escope;
    escope = s;
}

/* Releases the locals of every scope a break/continue abandons: up to the loop body (or the switch, for break). */
/* Leaving a try block by return/break/continue must also pop its handler, or a later throw would
   longjmp into a frame that no longer exists. */
static void emit_block(Stmt *b, bool func_root);
static void emit_try_pop_if(EScope *s) {
    if (s->kind == SC_TRY) { nl(); buf_puts(out, "xs_try_pop();"); }
    if (s->kind == SC_TRY && s->fin) { /* leaving a try (or its catch) that has a finally: the finally runs here */
        Stmt *f = s->fin;
        s->fin = NULL; /* a jump inside the finally itself does not run it again */
        nl();
        emit_block(f, false);
        s->fin = f;
    }
}
static Stmt *pending_fin; /* the finally of the try whose body emit_block_kind opens next */

static void emit_releases_to_jump(bool is_break) {
    for (EScope *s = escope; s; s = s->parent) {
        emit_releases(s);
        if (s->kind == SC_LOOP_BODY || (is_break && s->kind == SC_SWITCH) || s->func_root) break;
        emit_try_pop_if(s);
    }
}

static void dbg_scope_end(void *sc);
static void epop(void) { dbg_scope_end(escope); escope = escope->parent; }

/* programs that can catch (a try, or a server) register their reference-counted locals on the cleanup stack,
   so an exception releases them (runtime: xs_cln_push) */
static bool use_cln;
static bool spine_wide_on(void) { return !getenv("XISTO_NO_SPINE_DEFER"); }
static bool prog_try; /* a try somewhere in the program: an error may be caught and execution go on */
static bool expr_has_try(Expr *e);
static bool stmt_has_try_deep(Stmt *s) {
    if (!s) return false;
    if (s->kind == S_TRY) return true;
    if (expr_has_try(s->init) || expr_has_try(s->expr) || expr_has_try(s->cond) || expr_has_try(s->for_update)) return true;
    if (stmt_has_try_deep(s->for_init) || stmt_has_try_deep(s->body) || stmt_has_try_deep(s->else_body)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (stmt_has_try_deep(s->stmts.items[i])) return true;
    for (int i = 0; i < s->ncases; i++) if (stmt_has_try_deep(s->cases[i].body)) return true;
    return false;
}
static bool expr_has_try(Expr *e) {
    if (!e) return false;
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn && stmt_has_try_deep(e->arrow->fn->body)) return true;
    if (expr_has_try(e->lhs) || expr_has_try(e->rhs)) return true;
    for (int i = 0; i < e->args.len; i++) if (expr_has_try(e->args.items[i])) return true;
    return false;
}
static bool program_has_try(Program *p) {
    for (int i = 0; i < p->top.len; i++) if (stmt_has_try_deep(p->top.items[i])) return true;
    for (int i = 0; i < p->nfuncs; i++) if (p->funcs[i]->body && stmt_has_try_deep(p->funcs[i]->body)) return true;
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (c->ctor && stmt_has_try_deep(c->ctor->body)) return true;
        for (int k = 0; k < c->nmethods; k++) if (c->methods[k]->body && stmt_has_try_deep(c->methods[k]->body)) return true;
        for (int f = 0; f < c->nfields; f++) if (expr_has_try(c->fields[f].init)) return true;
    }
    return false;
}
static void etrack(const char *cname, Type *t) {
    if (!rcnt(t)) return;
    cname = ren(cname);
    EScope *s = escope;
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 4;
        s->names = realloc(s->names, sizeof(char *) * (size_t)s->cap);
        s->types = realloc(s->types, sizeof(Type *) * (size_t)s->cap);
    }
    s->types[s->n] = t;
    s->names[s->n++] = (char *)cname;
    if (use_cln) buf_printf(out, " xs_cln_push((void **)&%s);", cname);
}

static void nl(void) {
    buf_putc(out, '\n');
    for (int i = 0; i < indent; i++) buf_puts(out, "    ");
}

/* the class whose free function a value of type t certainly has (no subclasses, not a region's), or NULL */
static Class *exact_class(Type *t) {
    if (t && t->kind == TY_NULLABLE) t = t->elem;
    if (!t || t->kind != TY_OBJECT || !t->cls) return NULL;
    Class *c = t->cls;
    return !c->builtin && !c->nsubs && !c->region && !c->is_interface && c->used && !c->is_abstract ? c : NULL;
}
/* the free function a value of type t certainly has: its exact class's, or xs_str_free for every string (the
   immortal literals share it and never reach zero); NULL when only the value's type knows */
static const char *known_free(Type *t) {
    Type *u = t && t->kind == TY_NULLABLE ? t->elem : t;
    if (u && u->kind == TY_STRING) return "xs_str_free";
    Class *c = exact_class(t);
    return c ? xs_sprintf("%s_free", c->cname) : NULL;
}
/* does the constructor write every field before anything reads it? A field with an initializer, or one of the
   opening `this.f = v` stores (init_store for references: a plain store, nothing released). Then a stack object
   needs no clearing first (objects.ts: 5.25 -> 5.0 ns per iteration); otherwise it starts as {0}. */
static bool ctor_sets_every_field(Class *c) {
    Func *ct = c->ctor;
    for (int k = 0; k < c->nfields; k++) {
        if (c->fields[k].init) continue;
        bool set = false;
        for (int i = 0; ct && ct->body && i < ct->body->stmts.len && !set; i++) {
            Expr *a = ct->body->stmts.items[i]->kind == S_EXPR ? ct->body->stmts.items[i]->expr : NULL;
            if (!a || a->kind != E_ASSIGN || a->op != TK_ASSIGN || !a->lhs || a->lhs->kind != E_MEMBER || a->lhs->field != k) continue;
            if (!cref(c->fields[k].type)) set = true;
            else set = a->builtin.cfn && strcmp(a->builtin.cfn, "init_store") == 0;
        }
        if (!set) return false;
    }
    return true;
}
/* the locals emitted on the stack (stackalloc.c): their release can skip the free when nothing it holds needs one */
static const char **stack_vars;
static int nstack_vars;
static bool is_stack_var(const char *n) { for (int i = 0; i < nstack_vars; i++) if (strcmp(stack_vars[i], n) == 0) return true; return false; }
static void emit_releases(EScope *s) {
    for (int i = s->n - 1; i >= 0; i--) {
        nl();
        Class *sc = s->types ? exact_class(s->types[i]) : NULL;
        if (sc && is_stack_var(s->names[i])) {
            /* the frame's object, still only ours, holding nothing or strings of the frame that are only its own:
               nothing to free, only the counts (objects.ts: 6.5 -> 5.3 ns per iteration); else as always */
            const char *v = s->names[i];
            Buf cond = {0}, cnt = {0};
            buf_printf(&cond, "(%s->hdr.cls & XS_STACK) && %s->hdr.rc == 1", v, v);
            buf_puts(&cnt, "1");
            bool simple = true;
            for (int k = 0; k < sc->nfields; k++) {
                Type *ft = sc->fields[k].type;
                if (!cref(ft)) continue;
                if (ft->kind != TY_STRING) { simple = false; break; }
                const char *f = sc->fields[k].cname;
                buf_printf(&cond, " && (!%s->%s || ((%s->%s->hdr.cls & XS_STACK) && %s->%s->hdr.rc == 1))", v, f, v, f, v, f);
                buf_printf(&cnt, " + (%s->%s != NULL)", v, f);
            }
            if (simple) {
                buf_printf(out, "if (__builtin_expect(%s, 1)) xs_live -= %s; else xs_release_as(%s, %s_free);", cond.data, cnt.data, v, sc->cname);
                continue;
            }
        }
        const char *kf = s->types ? known_free(s->types[i]) : NULL;
        if (kf) buf_printf(out, "xs_release_as(%s, %s);", s->names[i], kf);
        else buf_printf(out, "xs_release(%s);", s->names[i]);
    }
    if (use_cln && s->n) { nl(); buf_printf(out, "xs_cln_n -= %d;", s->n); }
}

/* Releases every local of the scopes being abandoned by a return. */
static void emit_releases_to_root(void) {
    for (EScope *s = escope; s; s = s->parent) {
        emit_releases(s);
        if (s->func_root) break;
        emit_try_pop_if(s);
    }
}

/* Locals of a function that contains a try live across setjmp: they are declared volatile so a value
   changed inside the try is the one seen after a longjmp (C 7.13.2.1). */
static bool vol_locals;
static bool stmt_has_try(Stmt *s) {
    if (!s) return false;
    if (s->kind == S_TRY) return true;
    if (stmt_has_try(s->body) || stmt_has_try(s->else_body) || stmt_has_try(s->for_init)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (stmt_has_try(s->stmts.items[i])) return true;
    for (int i = 0; i < s->ncases; i++) if (stmt_has_try(s->cases[i].body)) return true;
    return false;
}

static void emit_local_decl(Type *t, const char *name);

/* ---------- types ---------- */

static const char *ctype(Type *t) {
    switch (t->kind) {
    case TY_NUMBER: return "double";
    case TY_INT: case TY_LITINT: return "int64_t";
    case TY_BOOLEAN: return "bool";
    case TY_STRING: return "xs_str *";
    case TY_VOID: return "void";
    case TY_ARRAY: return arr_ek(t->elem)->kind == TY_NUMBER ? "xs_arr_num *" : int_like(arr_ek(t->elem)) ? "xs_arr_int *" : t->elem->kind == TY_BOOLEAN ? "xs_arr_bool *" : "xs_arr_ref *";
    case TY_OBJECT: return xs_sprintf("%s *", t->cls->cname);
    case TY_FUNC: return "xs_closure *";
    case TY_ENUM: return t->en->is_string ? "xs_str *" : "int64_t";
    case TY_NULLABLE: return t->elem->kind == TY_BOOLEAN ? "int8_t" : ctype(t->elem);
    case TY_JSON: return "xs_str *";
    case TY_UNKNOWN: case TY_UNION: return "void *";
    case TY_NULL: return "void *";
    case TY_MAP: case TY_SET: return "xs_map *";
    case TY_STREAM: return "xs_stream *";
    case TY_CHAN: return "xs_chan *";
    case TY_PROMISE: return "xs_promise *";
    }
    return "?";
}

/* "double v_x", "xs_str *v_s", "xs_closure *v_h" */
static void emit_decl(Type *t, const char *name) {
    const char *c = ctype(t);
    buf_printf(out, "%s%s%s", c, c[strlen(c) - 1] == '*' ? "" : " ", name);
}

static void emit_local_decl(Type *t, const char *name) {
    name = ren(name);
    if (!vol_locals) { emit_decl(t, name); return; }
    const char *c = ctype(t);
    if (c[strlen(c) - 1] == '*') buf_printf(out, "%svolatile %s", c, name); /* the pointer itself is volatile */
    else buf_printf(out, "volatile %s %s", c, name);
}

/* Calls a closure value: ((R (*)(void *, P...))cl->fn)(cl, args...) */
static void emit_closure_call_prefix(Type *ft) {
    buf_printf(out, "((%s (*)(void *", ctype(ft->ret));
    for (int i = 0; i < ft->nparams; i++) buf_printf(out, ", %s", ctype(ft->params[i]));
    buf_puts(out, "))");
}

static Buf arrow_fns;     /* arrow function bodies and env structs, emitted before the code that creates them */
static void jam_try(Arrow *a); /* (XB-083) */
static bool jam_used;       /* a parallelFill pool variant asked for xs_jam8_of */
static Buf jam_tab;         /* its cases */
static Buf tramp_fns;     /* trampolines turning top-level functions into closure values */
static int ntramps;
static char **tramp_names;

static const char *arr_suffix(Type *arr) { return arr_ek(arr->elem)->kind == TY_NUMBER ? "num" : int_like(arr_ek(arr->elem)) ? "int" : arr->elem->kind == TY_BOOLEAN ? "bool" : "ref"; }
static bool arr_is_ref(Type *arr) { return cref(arr->elem); }
/* console.log of an array: (number | null)[] and (int | null)[] print their nulls as null */
static const char *arr_print_suffix(Type *arr) {
    if (arr->elem->kind == TY_NULLABLE && arr_ek(arr->elem) != arr->elem) return arr_ek(arr->elem)->kind == TY_NUMBER ? "num_n" : "int_n";
    return arr_suffix(arr);
}
/* a heap cell holding one variable that a closure shares (captured and reassigned): xs_cell_<C type> */
static Buf cell_fns;
static char **cell_made;
static Program *cyc_prog;
static bool type_cyclic(Type *t);
static bool class_cyclic(Class *c);
static bool arr_or_map_cyclic(Type *t);
static int ncell;
static const char *cell_struct(Type *t) {
    Buf n = {0};
    buf_puts(&n, "xs_cell_");
    for (const char *c = ctype(t); *c; c++) buf_putc(&n, isalnum((unsigned char)*c) ? *c : '_');
    for (int i = 0; i < ncell; i++) if (strcmp(cell_made[i], n.data) == 0) return cell_made[i];
    cell_made = realloc(cell_made, sizeof(char *) * (size_t)(ncell + 1));
    cell_made[ncell++] = n.data;
    buf_printf(&cell_fns, "typedef struct { xs_hdr hdr; %s v; } %s;\n", ctype(t), n.data);
    buf_printf(&cell_fns, "static void %s_free(void *p) { %sxs_obj_free(p); }\n", n.data, crc(t) ? xs_sprintf("xs_release(((%s *)p)->v); ", n.data) : "");
    buf_printf(&cell_fns, "static void %s_print(void *p) { (void)p; fputs(\"[cell]\", XS_OUT); }\n", n.data);
    if (type_cyclic(t)) { /* a shared variable holding something cyclic: a cell can close a cycle */
        buf_printf(&cell_fns, "static void %s_visit(void *p, xs_visit_cb cb, void *arg) { cb((void **)&((%s *)p)->v, arg); }\n", n.data, n.data);
        buf_printf(&cell_fns, "static const xs_type XS_TYPE_SEC %s_type = {%s_free, %s_print, %s_visit};\n", n.data, n.data, n.data, n.data);
    } else {
        buf_printf(&cell_fns, "static const xs_type XS_TYPE_SEC %s_type = {%s_free, %s_print, NULL};\n", n.data, n.data, n.data);
    }
    buf_printf(&cell_fns, "static %s *%s_new(void) { return xs_obj_alloc_t(sizeof(%s), &%s_type); }\n", n.data, n.data, n.data, n.data);
    return n.data;
}
static const char *arr_struct(Type *arr) { return arr_is_ref(arr) ? "xs_arr_ref" : arr_ek(arr->elem)->kind == TY_NUMBER ? "xs_arr_num" : int_like(arr_ek(arr->elem)) ? "xs_arr_int" : "xs_arr_bool"; }
static const char *elem_printer(Type *elem) {
    if (elem->kind == TY_NULLABLE) /* (T | null)[]: null prints as null */
        return elem->elem->kind == TY_STRING || (elem->elem->kind == TY_ENUM && elem->elem->en->is_string) ? "xs_print_str_quoted_or_null_" : "xs_print_obj_or_null";
    switch (elem->kind) {
    case TY_STRING: return "xs_print_str_quoted_";
    case TY_ENUM: return elem->en->is_string ? "xs_print_str_quoted_" : "0";
    case TY_OBJECT: return "xs_print_obj";
    case TY_ARRAY: return xs_sprintf("xs_print_arr_%s", arr_print_suffix(elem));
    case TY_MAP: case TY_SET: return "xs_print_map";
    case TY_FUNC: return "xs_print_obj"; /* a closure prints itself */
    case TY_UNION: case TY_UNKNOWN: return "xs_print_any_quoted"; /* strings quoted inside arrays, as Node */
    default: return "0";
    }
}

/* value kind code for the Map runtime: 0 num, 1 int, 2 bool, 3 heap pointer */
static int val_kind(Type *t) {
    switch (ty_strip_null(t)->kind) {
    case TY_NUMBER: return 0;
    case TY_INT: return 1;
    case TY_BOOLEAN: return 2;
    case TY_ENUM: return t->en->is_string ? 3 : 1;
    default: return 3;
    }
}

static const char *map_val_printer(Type *v) {
    switch (v->kind) {
    case TY_NUMBER: return "xs_print_num_v";
    case TY_INT: return "xs_print_int_v";
    case TY_BOOLEAN: return "xs_print_bool_v";
    case TY_STRING: return "xs_print_str_quoted_";
    case TY_ENUM: return v->en->is_string ? "xs_print_str_quoted_" : "xs_print_int_v";
    default: return elem_printer(v);
    }
}

/* ---------- ownership ---------- */

/* a literal's element that may throw (a call in it): the elements made before it must not leak then */
static bool lit_may_throw(Expr *e) {
    if (!e) return false;
    switch (e->kind) {
    case E_ARROW: case E_FUNC_REF: return false;
    case E_CALL: case E_NEW_OBJ: case E_AWAIT: case E_SPAWN: return true;
    case E_MEMBER: if (e->is_call) return true; break;
    default: break;
    }
    if (lit_may_throw(e->lhs) || lit_may_throw(e->rhs)) return true;
    for (int i = 0; i < e->args.len; i++) if (lit_may_throw(e->args.items[i])) return true;
    return false;
}

static bool is_fresh(Expr *e) {
    if (!rcnt(e->type)) return false;
    switch (e->kind) {
    case E_CALL: case E_NEW_OBJ: case E_ARRAY_LIT: case E_NEW_ARRAY: case E_TEMPLATE: case E_NEW_MAP: case E_ARROW: case E_OBJLIT: case E_REGEX: case E_SPAWN: return true;
    case E_FUNC_REF: return false; /* immortal */
    case E_MEMBER:
        if (!e->is_call) return false;
        if (e->lhs->type && e->lhs->type->kind == TY_STREAM) return cref(e->type); /* every stream op returns a new owned value */
        if (e->lhs->type && e->lhs->type->kind == TY_CHAN) return cref(e->type); /* receive hands over ownership */
        if (e->cls && e->cls->builtin && strcmp(e->cls->name, "StatementSync") == 0) return true;
        if (e->builtin.cfn && (strcmp(e->builtin.cfn, "get") == 0 || strcmp(e->builtin.cfn, "set") == 0 || strcmp(e->builtin.cfn, "add") == 0 ||
                               strcmp(e->builtin.cfn, "find") == 0 || strcmp(e->builtin.cfn, "sort") == 0 || strcmp(e->builtin.cfn, "reverse") == 0)) return false; /* borrowed */
        return true; /* every other method/static call returns an owned reference */
    case E_BINARY: return e->op == TK_PLUS; /* string concatenation */
    case E_AWAIT: return is_fresh(e->lhs);
    case E_UPCAST: return is_fresh(e->lhs);
    case E_AS:
        if (e->op == TK_NOT && !e->elem_type) return is_fresh(e->lhs); /* x!: the same value */
        if (e->op == 0 && e->lhs->type && e->lhs->type->kind == TY_UNKNOWN) return is_fresh(e->lhs); /* f() as T: a reference passes its ownership on */
        return false;
    case E_COND:
        if (e->builtin.cfn && (strcmp(e->builtin.cfn, "or_once") == 0 || strcmp(e->builtin.cfn, "qq_once") == 0)) return true; /* always owned: see emit_or_once */
        if (e->builtin.cfn && strcmp(e->builtin.cfn, "optchain") == 0) /* owned when the rest is, or when a was (the rest then retains) */
            return is_fresh(e->args.items[0]) || is_fresh(e->lhs);
        return is_fresh(e->args.items[0]) || is_fresh(e->args.items[1]);
    default: return false;
    }
}

/* ---- inheritance ---- */
static Func *emit_find_method(Class *c, const char *name) {
    for (int i = 0; i < c->nmethods; i++) if (strcmp(c->methods[i]->name, name) == 0) return c->methods[i];
    return NULL;
}
static Func *emit_eff_ctor(Class *c) {
    for (; c; c = c->base) if (c->ctor) return c->ctor;
    return NULL;
}
static bool is_sub(Class *sub, Class *base) {
    for (Class *c = sub; c; c = c->base) if (c == base) return true;
    return false;
}
static bool in_hierarchy(Class *c) { return !c->builtin && (c->base || c->nsubs); }
/* the object v (void *) is a c or a subclass of it: compare its free function with each used class below c */
static Buf isa_fns;
static char **isa_made;
static int nisa;
static const char *isa_fn(Class *c) {
    char *name = xs_sprintf("%s_isa", c->cname);
    for (int i = 0; i < nisa; i++) if (strcmp(isa_made[i], name) == 0) return name;
    isa_made = realloc(isa_made, sizeof(char *) * (size_t)(nisa + 1));
    isa_made[nisa++] = name;
    buf_printf(&isa_fns, "static bool %s(void *v) { void (*f)(void *) = v ? xs_type_of(v)->free : NULL; return f && (0", name);
    for (int i = 0; i < prog->nclasses; i++) {
        Class *k = prog->classes[i];
        if (k->used && !k->builtin && is_sub(k, c)) buf_printf(&isa_fns, " || f == %s_free", k->cname);
    }
    buf_puts(&isa_fns, "); }\n");
    return name;
}
/* unknown as I, I an interface with methods (XB-126): the object itself when it is an I, else an object of a class
   that has an adapter for I (or of a subclass of that class) wrapped in a new adapter; always an owned reference */
static char **asif_made;
static int nasif;
static const char *asif_fn(Class *ifc, bool nullable) {
    char *name = xs_sprintf("%s_asif%s", ifc->cname, nullable ? "_n" : "");
    for (int i = 0; i < nasif; i++) if (strcmp(asif_made[i], name) == 0) return name;
    asif_made = realloc(asif_made, sizeof(char *) * (size_t)(nasif + 1));
    asif_made[nasif++] = name;
    const char *isa = isa_fn(ifc);
    Buf b = {0};
    buf_printf(&b, "static %s *%s(void *v) {\n    if (!v) { %s }\n    if (%s(v)) return xs_retain(v);\n    void (*f)(void *) = xs_type_of(v)->free;\n",
               ifc->cname, name, nullable ? "return NULL;" : xs_sprintf("xs_as_fail(\"%s\"); return NULL;", ifc->name), isa);
    for (int i = 0; i < ifc->nadapters; i++) {
        Class *a = ifc->adapters[i], *src = a->adapter_of;
        if (!a->used) continue;
        bool consumed = a->ctor && a->ctor->nparams && a->ctor->params[0].consumed;
        for (int k = 0; k < prog->nclasses; k++) {
            Class *c = prog->classes[k];
            if (!c->used || c->builtin || c->is_abstract || !is_sub(c, src)) continue;
            buf_printf(&b, "    if (f == %s_free) return (%s *)%s_new(%s(%s *)v%s);\n", c->cname, ifc->cname, a->cname,
                       consumed ? "xs_retain(" : "", src->cname, consumed ? ")" : "");
        }
    }
    buf_printf(&b, "    xs_as_fail(\"%s\");\n    return NULL;\n}\n", ifc->name);
    buf_puts(&isa_fns, b.data);
    free(b.data);
    return name;
}

/* a method that some subclass overrides, called through its class: chooses by the object's class */
static Buf disp_fns;
static char **disp_made;
static int ndisp;
static const char *dispatcher(Func *m) {
    char *name = xs_sprintf("%s__v", m->cname);
    for (int i = 0; i < ndisp; i++) if (strcmp(disp_made[i], name) == 0) return name;
    disp_made = realloc(disp_made, sizeof(char *) * (size_t)(ndisp + 1));
    disp_made[ndisp++] = name;
    Buf b = {0};
    Buf *saved = out;
    out = &b;
    buf_puts(out, "static ");
    emit_decl(m->ret, name);
    buf_printf(out, "(%s *self", m->cls->cname);
    /* its own argument names: an abstract method's parameters get no C names (it has no body to check) */
    for (int i = 0; i < m->nparams; i++) { buf_puts(out, ", "); emit_decl(m->params[i].type, xs_sprintf("a%d_", i)); }
    buf_puts(out, ") {\n    void (*f_)(void *) = xs_type_of(self)->free;\n");
    Buf args = {0};
    for (int i = 0; i < m->nparams; i++) buf_printf(&args, ", a%d_", i);
    bool is_void = m->ret->kind == TY_VOID;
    for (int i = 0; i < prog->nclasses; i++) {
        Class *k = prog->classes[i];
        if (!k->used || k->builtin || k->is_abstract || !is_sub(k, m->cls)) continue;
        Func *impl = emit_find_method(k, m->name);
        /* (a method of the same name with other parameters, or one overriding such a method, is not m's: the nearest that is) */
        for (Class *u = k->base; impl && impl != m && u; u = u->base) {
            if (impl->shadow && impl->bridge) { impl = impl->bridge; break; } /* (other parameters: through its bridge) */
            bool mine = false;
            for (int j = 0; j < m->noverrides && !mine; j++) mine = m->overrides[j] == impl;
            if (mine) break;
            impl = emit_find_method(u, m->name);
        }
        if (!impl || impl == m || impl->is_abstract || !impl->used) continue;
        if (is_void) buf_printf(out, "    if (f_ == %s_free) { %s((%s *)self%s); return; }\n", k->cname, impl->cname, impl->cls->cname, args.data ? args.data : "");
        else buf_printf(out, "    if (f_ == %s_free) return %s((%s *)self%s);\n", k->cname, impl->cname, impl->cls->cname, args.data ? args.data : "");
    }
    if (m->is_abstract) buf_printf(out, "    xs_panic(\"abstract method %s.%s called\");\n%s", m->cls->name, m->name, is_void ? "" : "    __builtin_unreachable();\n");
    else buf_printf(out, "    %s%s(self%s);\n", is_void ? "" : "return ", m->cname, args.data ? args.data : "");
    buf_puts(out, "}\n");
    free(args.data);
    out = saved;
    buf_puts(&disp_fns, b.data);
    free(b.data);
    return name;
}

static void emit_expr(Expr *e);

/* The caller takes ownership of the result (+1). */
static void emit_owned(Expr *e) {
    if (!rcnt(e->type) || is_fresh(e) || e->kind == E_NULL || (e->kind == E_IDENT && e->builtin.cfn && strcmp(e->builtin.cfn, "consumed") == 0)) {
        emit_expr(e);
        return;
    }
    buf_puts(out, "xs_retain(");
    emit_expr(e);
    buf_puts(out, ")");
}

/* The result is only used during the current statement. */
static void emit_borrowed(Expr *e) {
    if (!is_fresh(e)) {
        emit_expr(e);
        return;
    }
    used_ar = true;
    buf_puts(out, "xs_ar(");
    emit_expr(e);
    buf_puts(out, ")");
}

/* ---------- expressions ---------- */

static void emit_borrowed(Expr *e);
static void emit_expr(Expr *e);
/* [a, ...xs, b]: a new array, then each element pushed (a spread source's elements in order: an array, or the
   bytes / numbers of a Uint8Array / Float64Array; strings, Sets and Maps were turned into arrays by the checker) */
static void emit_spread_lit(Expr *e) {
    static int nspread;
    int id = nspread++;
    const char *as = arr_struct(e->type);
    bool ref = arr_is_ref(e->type);
    if (ref) buf_printf(out, "({ xs_arr_ref *sr%d_ = xs_arr_ref_new(%d, %s); ", id, e->args.len, elem_printer(e->type->elem));
    else buf_printf(out, "({ %s *sr%d_ = %s_new(0, 0); ", as, id, as);
    for (int i = 0; i < e->args.len; i++) {
        Expr *a = e->args.items[i];
        if (a->kind == E_UNARY && a->op == TK_ELLIPSIS) {
            Type *st = a->lhs->type;
            bool typed = st->kind == TY_OBJECT; /* Uint8Array / Float64Array */
            buf_printf(out, "{ %s *ss_ = ", typed ? st->cls->cname : arr_struct(st));
            emit_borrowed(a->lhs);
            buf_printf(out, "; for (int64_t i_ = 0; i_ < (int64_t)ss_->%s; i_++) ", typed ? "f_length" : "len");
            if (ref) buf_printf(out, "xs_arr_ref_push(sr%d_, xs_retain_(ss_->data[i_])); } ", id);
            else buf_printf(out, "%s_push(sr%d_, ss_->data[i_]); } ", as, id);
            continue;
        }
        if (ref) { buf_printf(out, "xs_arr_ref_push(sr%d_, (void *)", id); emit_owned(a); buf_puts(out, "); "); }
        else { buf_printf(out, "%s_push(sr%d_, ", as, id); emit_expr(a); buf_puts(out, "); "); }
    }
    buf_printf(out, "sr%d_; })", id);
}
/* an operand of a bitwise operator as uint32_t (ToInt32, then its bits) */
static void emit_u32(Expr *e) {
    if (e->kind == E_BINARY && (e->op == TK_PLUS || e->op == TK_MINUS) && e->type->kind == TY_INT && !xs_debug &&
        (e->lhs->type->kind == TY_INT || e->lhs->type->kind == TY_LITINT) && (e->rhs->type->kind == TY_INT || e->rhs->type->kind == TY_LITINT) &&
        rng_of(e).ok) {
        /* (a + b) >> 1, (h - k) & m: only the low 32 bits are kept, and those of the exact sum are those of the
           sum mod 2^32, so no overflow check — when the sum provably stays a safe integer: past 2^53 JS rounds the
           sum before the 32-bit operator and the low bits differ (XB-075); without the proof the checked sum is used */
        buf_puts(out, "((uint32_t)(");
        emit_u32(e->lhs);
        buf_puts(out, e->op == TK_PLUS ? " + " : " - ");
        emit_u32(e->rhs);
        buf_puts(out, "))");
        return;
    }
    if (e->type->kind == TY_NUMBER) { buf_puts(out, "(uint32_t)xs_toi32("); emit_expr(e); buf_puts(out, ")"); }
    else { buf_puts(out, "(uint32_t)("); emit_expr(e); buf_puts(out, ")"); }
}
/* a & b, a << b, a >>> b ...: JS's 32-bit semantics, the result as int64_t */
static void emit_bitop(int op, Expr *l, Expr *r) {
    switch (op) {
    case TK_SHL: buf_puts(out, "((int64_t)(int32_t)("); emit_u32(l); buf_puts(out, " << ("); emit_u32(r); buf_puts(out, " & 31u)))"); return;
    case TK_SHR: buf_puts(out, "((int64_t)((int32_t)"); emit_u32(l); buf_puts(out, " >> ("); emit_u32(r); buf_puts(out, " & 31u)))"); return;
    case TK_USHR: buf_puts(out, "((int64_t)("); emit_u32(l); buf_puts(out, " >> ("); emit_u32(r); buf_puts(out, " & 31u)))"); return;
    default:
        buf_puts(out, "((int64_t)(int32_t)(");
        emit_u32(l);
        buf_puts(out, op == TK_AMP ? " & " : op == TK_PIPE ? " | " : " ^ ");
        emit_u32(r);
        buf_puts(out, "))");
    }
}
static void emit_expr(Expr *e);
/* the operands of a chain of string concatenations, left to right */
static void cat_leaves(Expr *e, Expr ***v, int *n, int *cap) {
    if (e->kind == E_BINARY && e->op == TK_PLUS && e->type && e->type->kind == TY_STRING) {
        cat_leaves(e->lhs, v, n, cap);
        cat_leaves(e->rhs, v, n, cap);
        return;
    }
    if (*n == *cap) { *cap = *cap ? *cap * 2 : 8; *v = realloc(*v, sizeof(Expr *) * (size_t)*cap); }
    (*v)[(*n)++] = e;
}
/* one part of xs_cat: String(i) / String(x) are written in place instead of making a string first */
static void emit_cat_part(Expr *p) {
    if (p->kind == E_CALL && p->builtin.cfn && p->args.len == 1) {
        const char *f = p->builtin.cfn;
        int k = strcmp(f, "xs_str_from_int") == 0 ? 1 : strcmp(f, "xs_str_from_num") == 0 ? 2 : strcmp(f, "xs_str_from_bool") == 0 ? 3 : 0;
        if (k) {
            buf_printf(out, "{%d, .%s = ", k, k == 1 ? "i" : k == 2 ? "d" : "b");
            emit_expr(p->args.items[0]);
            buf_puts(out, "}");
            return;
        }
    }
    buf_puts(out, "{0, .s = ");
    emit_borrowed(p);
    buf_puts(out, "}");
}

static void emit_c_string(const char *s) {
    buf_putc(out, '"');
    unsigned char prev = 0;
    for (; *s; prev = (unsigned char)*s, s++) {
        unsigned char c = (unsigned char)*s;
        if (c == 0xC0 && (unsigned char)s[1] == 0x80) { buf_puts(out, "\\000"); s++; continue; } /* NUL (xs_lit_len) */
        switch (c) {
        case '"': buf_puts(out, "\\\""); break;
        case '\\': buf_puts(out, "\\\\"); break;
        case '\n': buf_puts(out, "\\n"); break;
        case '\t': buf_puts(out, "\\t"); break;
        case '\r': buf_puts(out, "\\r"); break;
        case '?': buf_puts(out, prev == '?' ? "\\?" : "?"); break; /* "??/" "??=" ... are trigraphs in C11: a '?' after a '?' goes out as \?, so no two meet */
        default:
            if (c < 32 || c == 127) buf_printf(out, "\\%03o", c);
            else buf_putc(out, (char)c);
        }
    }
    buf_putc(out, '"');
}

static void emit_number(double v) {
    if (isnan(v)) { buf_puts(out, "NAN"); return; }
    if (isinf(v)) {
        buf_puts(out, v > 0 ? "INFINITY" : "(-INFINITY)");
        return;
    }
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%.17g", v);
    buf_puts(out, tmp);
    if (!strpbrk(tmp, ".eEn")) buf_puts(out, ".0");
}

static void emit_args_borrowed(ExprList *l, bool leading_comma) {
    for (int i = 0; i < l->len; i++) {
        if (i || leading_comma) buf_puts(out, ", ");
        emit_borrowed(l->items[i]);
    }
}

static const char *binop(TokKind op) {
    switch (op) {
    case TK_PLUS: return "+";
    case TK_MINUS: return "-";
    case TK_STAR: return "*";
    case TK_SLASH: return "/";
    case TK_LT: return "<";
    case TK_LE: return "<=";
    case TK_GT: return ">";
    case TK_GE: return ">=";
    case TK_EQ: return "==";
    case TK_NEQ: return "!=";
    case TK_AND: return "&&";
    case TK_OR: return "||";
    case TK_ASSIGN: return "=";
    case TK_PLUS_ASSIGN: return "+=";
    case TK_MINUS_ASSIGN: return "-=";
    case TK_STAR_ASSIGN: return "*=";
    case TK_SLASH_ASSIGN: return "/=";
    default: return "?";
    }
}

static bool is_f64_type(Type *t) { return t && t->kind == TY_OBJECT && t->cls && t->cls->builtin && strcmp(t->cls->cname, "xs_Float64Array") == 0; }
/* Address of a variable, field or array slot. */
static void emit_lvalue_ptr(Expr *e) {
    switch (e->kind) {
    case E_IDENT:
        buf_printf(out, "&%s", ren(e->cname));
        return;
    case E_MEMBER:
        buf_puts(out, "&(");
        emit_borrowed(e->lhs);
        buf_printf(out, ")->%s", e->cls->fields[e->field].cname);
        return;
    default: /* E_INDEX */
        if (is_f64_type(e->lhs->type)) { /* f[i] on a Float64Array: its data, as a number[]'s */
            bool safe = bce_safe(e);
            buf_puts(out, safe ? "(&(" : "xs_f64_at_s(");
            emit_borrowed(e->lhs);
            buf_puts(out, safe ? ")->data[" : ", ");
            if (safe && e->rhs->kind == E_BINARY && (e->rhs->op == TK_PLUS || e->rhs->op == TK_MINUS)) {
                buf_puts(out, "(");
                emit_expr(e->rhs->lhs);
                buf_puts(out, e->rhs->op == TK_PLUS ? ") + (" : ") - (");
                emit_expr(e->rhs->rhs);
                buf_puts(out, ")");
            } else emit_expr(e->rhs);
            if (safe) buf_puts(out, "])");
            else { site_arg(e->pos); buf_puts(out, ")"); }
            return;
        }
        if (bce_safe(e)) { /* within the array: see bce_enter */
            BceGArr *g = e->lhs->kind == E_IDENT ? garr_find(e->lhs->cname) : NULL;
            if (g && g->has_rng) { /* a constant table: its C copy, so the entry is a constant where the index is (nbody-3: x[PI[p]]) */
                buf_printf(out, "(&%s_c[", e->lhs->cname);
                emit_bce_index(e->rhs);
                buf_puts(out, "])");
                return;
            }
            if (!arr_is_ref(e->lhs->type) && e->lhs->kind == E_IDENT) { /* its data in a C local (fix-hoist): no reload after stores */
                int k = fx_find(e->lhs->cname);
                if (k >= 0 && fx_decl[k]) {
                    buf_printf(out, "(&%s_fxd[", ren(e->lhs->cname));
                    emit_bce_index(e->rhs);
                    buf_puts(out, "])");
                    return;
                }
            }
            buf_printf(out, arr_is_ref(e->lhs->type) ? "((%s*)&(" : "(&(", ctype(e->type));
            emit_borrowed(e->lhs);
            buf_puts(out, ")->data[");
            emit_bce_index(e->rhs);
            buf_puts(out, "])");
            return;
        }
        if (!arr_is_ref(e->lhs->type) && e->lhs->kind == E_IDENT) {
            int k = fx_find(e->lhs->cname);
            if (k >= 0 && fx_decl[k]) {
                static int fxn;
                int id = fxn++;
                const char *nm = ren(e->lhs->cname);
                buf_printf(out, "({ int64_t fxi%d_ = (", id);
                emit_expr(e->rhs);
                buf_printf(out, "); (uint64_t)fxi%d_ < (uint64_t)%s_fxl ? &%s_fxd[fxi%d_] : xs_at_%s_s(%s, fxi%d_", id, nm, nm, id, arr_suffix(e->lhs->type), nm, id);
                site_arg(e->pos);
                buf_puts(out, "); })");
                return;
            }
        }
        if (arr_is_ref(e->lhs->type)) buf_printf(out, "((%s*)xs_at_ref_s(", ctype(e->type));
        else buf_printf(out, "xs_at_%s_s(", arr_suffix(e->lhs->type));
        emit_borrowed(e->lhs);
        buf_puts(out, ", ");
        emit_expr(e->rhs);
        site_arg(e->pos);
        buf_puts(out, arr_is_ref(e->lhs->type) ? "))" : ")");
    }
}

static void emit_lvalue(Expr *e) {
    if (e->kind == E_IDENT) {
        buf_puts(out, ren(e->cname));
        return;
    }
    buf_puts(out, "(*");
    emit_lvalue_ptr(e);
    buf_puts(out, ")");
}

static const char *printer_for(Type *t) {
    switch (t->kind) {
    case TY_NUMBER: return "xs_print_num";
    case TY_INT: case TY_LITINT: return "xs_print_int";
    case TY_BOOLEAN: return "xs_print_bool";
    case TY_STRING: case TY_JSON: return "xs_print_str";
    case TY_ARRAY: return xs_sprintf("xs_print_arr_%s", arr_print_suffix(t));
    case TY_OBJECT: return "xs_print_obj";
    case TY_ENUM: return t->en->is_string ? "xs_print_str" : "xs_print_int";
    case TY_MAP: case TY_SET: return "xs_print_map";
    case TY_STREAM: return "xs_print_stream";
    case TY_CHAN: return "xs_print_obj";
    case TY_PROMISE: return "xs_print_obj";
    case TY_NULLABLE: return "xs_print_obj_or_null";
    case TY_UNION: return "xs_print_obj_or_null";
    default: return "xs_print_obj";
    }
}

static void emit_print_arg(Expr *a) {
    if (a->type->kind == TY_NULLABLE && a->type->absent && !null_prim(a->type)) { /* x?: T left out: undefined, as Node prints it */
        Buf b = {0};
        Buf *saved = out;
        out = &b;
        emit_borrowed(a);
        out = saved;
        buf_printf(out, "({ %s v_ = %s; if (%s) %s(v_); else fputs(\"undefined\", XS_OUT); })", ctype(a->type), b.data, not_null(a->type, "v_"),
                   a->type->elem->kind == TY_STRING ? "xs_print_str_or_null" : "xs_print_obj_or_null");
        free(b.data);
        return;
    }
    if (a->type->kind == TY_NULLABLE || a->type->kind == TY_NULL) {
        /* strings inside nullable print raw at top level, like Node */
        if (null_prim(a->type)) {
            Buf b = {0};
            Buf *saved = out;
            out = &b;
            emit_expr(a);
            out = saved;
            buf_printf(out, "({ %s v_ = %s; if (%s) %s(v_); else fputs(\"%s\", XS_OUT); })", ctype(a->type), b.data, not_null(a->type, "v_"), printer_for(a->type->elem),
                       a->type->absent ? "undefined" : "null");
            free(b.data);
            return;
        }
        if (a->type->kind == TY_NULLABLE && a->type->elem->kind == TY_STRING) buf_puts(out, "xs_print_str_or_null(");
        else buf_puts(out, "xs_print_obj_or_null(");
        emit_borrowed(a);
        buf_puts(out, ")");
        return;
    }
    if (a->type->kind == TY_STREAM) {
        buf_puts(out, "xs_print_stream(");
        emit_borrowed(a);
        buf_puts(out, ")");
        return;
    }
    if (a->type->kind == TY_MAP || a->type->kind == TY_SET) {
        buf_puts(out, "xs_print_map(");
        emit_borrowed(a);
        buf_puts(out, ")");
        return;
    }
    switch (a->type->kind) {
    case TY_NUMBER: buf_puts(out, "xs_print_num("); break;
    case TY_INT: case TY_LITINT: buf_puts(out, "xs_print_int("); break;
    case TY_BOOLEAN: buf_puts(out, "xs_print_bool("); break;
    case TY_STRING: buf_puts(out, "xs_print_str("); break;
    case TY_ARRAY: buf_printf(out, "xs_print_arr_%s(", arr_print_suffix(a->type)); break;
    case TY_OBJECT: buf_puts(out, "xs_print_obj("); break;
    case TY_ENUM: buf_puts(out, a->type->en->is_string ? "xs_print_str(" : "xs_print_int("); break;
    case TY_UNION: case TY_UNKNOWN: buf_puts(out, "xs_print_obj_or_null("); break;
    case TY_PROMISE: buf_puts(out, "xs_print_obj("); break;
    default: break;
    }
    emit_borrowed(a);
    buf_puts(out, ")");
}

/* ---------- JSON: per-type serializer and parser, generated on demand ---------- */

static Buf json_fns;          /* generated helper functions, placed before the program */
static Buf json_protos;       /* their prototypes, placed before all of them (a helper may call one made later) */
static Type **json_types;     /* types that already have helpers */
static int njson, cap_json;

static char *type_tag(Type *t) {
    switch (t->kind) {
    case TY_NUMBER: return "num";
    case TY_INT: return "int";
    case TY_BOOLEAN: return "bool";
    case TY_STRING: return "str";
    case TY_JSON: return "json";
    case TY_NULLABLE: return xs_sprintf("null_%s", type_tag(t->elem));
    case TY_ENUM: return t->en->is_string ? "str" : "int";
    case TY_ARRAY: return xs_sprintf("arr_%s", type_tag(t->elem));
    case TY_OBJECT: return xs_sprintf("obj_%s", t->cls->cname);
    case TY_MAP: return xs_sprintf("rec_%s_%s", type_tag(t->key), type_tag(t->elem));
    default: return "x";
    }
}

static void ensure_json(Type *t);
static const char *where_elem(int i) { return xs_sprintf("[%d]", i); }

/* JSON.stringify of an unknown (a field of that type too): xs_json_unknown, generated after the program, looks at
   the value's type at run time: a string, a boxed number or boolean, an array, or an object of one of the
   program's classes (each class JSON can write gets its json_put_ function then) */
static bool json_unknown_used;
static void json_emit_value(Buf *b, Type *t, const char *expr) { /* appends into xs_sb *sb */
    switch (t->kind) {
    case TY_NUMBER: buf_printf(b, "xs_sb_num(sb, %s);", expr); break;
    case TY_INT: buf_printf(b, "xs_sb_int(sb, %s);", expr); break;
    case TY_BOOLEAN: buf_printf(b, "xs_sb_bool(sb, %s);", expr); break;
    case TY_STRING: buf_printf(b, "xs_sb_str(sb, %s);", expr); break;
    case TY_JSON: buf_printf(b, "{ xs_str *j_ = %s; xs_sb_put(sb, j_->data, (size_t)j_->len); }", expr); break; /* embedded as is */
    case TY_ENUM: buf_printf(b, t->en->is_string ? "xs_sb_str(sb, %s);" : "xs_sb_int(sb, %s);", expr); break;
    case TY_UNION: /* (primitives only): held as an unknown is */
    case TY_UNKNOWN: json_unknown_used = true; buf_printf(b, "xs_json_unknown(sb, %s);", expr); break; /* by its type at run time */
    case TY_OBJECT:
        if (t->cls->builtin && strcmp(t->cls->name, "Date") == 0) { buf_printf(b, "xs_sb_date(sb, %s);", expr); break; }
        if (t->cls->builtin && strcmp(t->cls->name, "Buffer") == 0) { buf_printf(b, "xs_sb_buf(sb, %s);", expr); break; }
        if (t->cls->builtin && strcmp(t->cls->name, "bigint") == 0) { buf_printf(b, "(void)(%s); xs_panic(\"TypeError: Do not know how to serialize a BigInt\");", expr); break; }
        ensure_json(t);
        buf_printf(b, "json_put_%s(sb, %s);", type_tag(t), expr);
        break;
    case TY_NULLABLE:
        buf_printf(b, "if (!%s) xs_sb_put(sb, \"null\", 4); else { ", not_null(t, expr));
        json_emit_value(b, t->elem, t->elem->kind == TY_BOOLEAN ? xs_sprintf("(bool)(%s)", expr) : expr);
        buf_puts(b, " }");
        break;
    default: ensure_json(t); buf_printf(b, "json_put_%s(sb, %s);", type_tag(t), expr); break;
    }
}

static void json_emit_read(Buf *b, Type *t, const char *jv, const char *where) { /* yields an owned value expression */
    switch (t->kind) {
    case TY_NUMBER: buf_printf(b, "xs_json_num(%s, \"%s\")", jv, where); break;
    case TY_INT: buf_printf(b, "xs_json_int(%s, \"%s\")", jv, where); break;
    case TY_BOOLEAN: buf_printf(b, "xs_json_bool(%s, \"%s\")", jv, where); break;
    case TY_STRING: buf_printf(b, "xs_json_str(%s, \"%s\")", jv, where); break;
    case TY_JSON: buf_printf(b, "xs_json_text(%s)", jv); break; /* any value, kept as its JSON text (an object[] read back) */
    case TY_UNION: /* (primitives only, json_ok): held as an unknown is */
    case TY_UNKNOWN: buf_printf(b, "xs_jv_any(%s)", jv); (void)where; break; /* a value of its own kind: number, string, boolean, null, unknown[] or Record */
    case TY_OBJECT:
        if (t->cls->builtin && strcmp(t->cls->name, "Date") == 0) { buf_printf(b, "xs_json_date(%s, \"%s\")", jv, where); break; }
        if (t->cls->builtin && strcmp(t->cls->name, "Buffer") == 0) { buf_printf(b, "xs_json_buf(%s, \"%s\")", jv, where); break; }
        if (t->cls->builtin && strcmp(t->cls->name, "bigint") == 0) { buf_printf(b, "xs_json_big(%s, \"%s\")", jv, where); break; }
        ensure_json(t);
        buf_printf(b, "json_get_%s(%s, \"%s\")", type_tag(t), jv, where);
        break;
    case TY_NULLABLE:
        buf_printf(b, "((%s)->kind == XJ_NULL ? %s : ", jv, null_value(t));
        json_emit_read(b, t->elem, jv, where);
        buf_puts(b, ")");
        break;
    case TY_ENUM:
        if (t->en->is_string) {
            buf_printf(b, "({ xs_str *s_ = xs_json_str(%s, \"%s\"); xs_str *r_ = NULL; ", jv, where);
            for (int i = 0; i < t->en->nmembers; i++) buf_printf(b, "if (!r_ && xs_str_eq(s_, %s)) r_ = %s; ", lit_nm(t->en->members[i].lit_id), lit_nm(t->en->members[i].lit_id));
            buf_printf(b, "xs_release(s_); if (!r_) xs_panicf(\"JSON: '%%s' is not a valid %s\", \"%s\"); r_; })", t->en->name, where);
        } else {
            buf_printf(b, "xs_json_int(%s, \"%s\")", jv, where);
        }
        break;
    default: ensure_json(t); buf_printf(b, "json_get_%s(%s, \"%s\")", type_tag(t), jv, where); break;
    }
}

static void ensure_json(Type *t) {
    for (int i = 0; i < njson; i++) if (ty_equal(json_types[i], t)) return;
    { /* two types the checker keeps apart can share a tag (and so a C function name): one set of helpers */
        char *tg = type_tag(t);
        for (int i = 0; i < njson; i++) if (strcmp(type_tag(json_types[i]), tg) == 0) return;
    }
    if (njson == cap_json) { cap_json = cap_json ? cap_json * 2 : 8; json_types = realloc(json_types, sizeof(Type *) * (size_t)cap_json); }
    json_types[njson++] = t;
    Buf local = {0};
    Buf *b = &local;
    char *tag = type_tag(t);
    if (t->kind == TY_MAP) { /* a Record<string, V>: an object, its keys in insertion order */
        int vk = val_kind(t->elem);
        const char *vexpr = vk == 3 ? xs_sprintf("((%s)e->val.p)", ctype(t->elem)) : vk == 0 ? "e->val.d" : vk == 1 ? "e->val.i" : "(bool)e->val.i";
        buf_printf(&json_protos, "static void json_put_%s(xs_sb *sb, xs_map *m);\nstatic xs_map *json_get_%s(xs_jv *v, const char *where);\n", tag, tag);
        buf_printf(b, "static void json_put_%s(xs_sb *sb, xs_map *m) {\n    xs_sb_put(sb, \"{\", 1);\n    bool first = true;\n    xs_map_entry *e;\n"
                      "    XS_MAP_EACH(m, e) {\n        if (!first) xs_sb_put(sb, \",\", 1);\n        first = false;\n        xs_sb_str(sb, e->skey);\n        xs_sb_put(sb, \":\", 1);\n        ", tag);
        json_emit_value(b, t->elem, vexpr);
        buf_puts(b, "\n    }\n    xs_sb_put(sb, \"}\", 1);\n}\n");
        buf_printf(b, "static xs_map *json_get_%s(xs_jv *v, const char *where) {\n    if (v->kind != XJ_OBJ) xs_json_obj(v, where);\n"
                      "    xs_map *m = xs_map_rec(xs_map_new(1, 1, %d, xs_print_str_quoted_, %s));\n    for (size_t i = 0; i < v->n; i++) {\n        xs_str *k_ = xs_str_new(v->keys[i], strlen(v->keys[i]));\n        xs_map_set_s(m, k_, ",
                   tag, vk, map_val_printer(t->elem));
        buf_puts(b, vk == 3 ? "XS_VPTR(" : vk == 0 ? "XS_VNUM(" : vk == 1 ? "XS_VINT(" : "XS_VBOOL(");
        json_emit_read(b, t->elem, "v->items[i]", "object value");
        buf_puts(b, "));\n        xs_release(k_); /* the map keeps its own reference */\n    }\n    return m;\n}\n");
        buf_puts(&json_fns, local.data);
        return;
    } else if (t->kind == TY_ARRAY) {
        const char *st = arr_struct(t);
        buf_printf(&json_protos, "static void json_put_%s(xs_sb *sb, %s *a);\nstatic %s *json_get_%s(xs_jv *v, const char *where);\n", tag, st, st, tag);
        buf_printf(b, "static void json_put_%s(xs_sb *sb, %s *a) {\n    xs_sb_put(sb, \"[\", 1);\n    for (size_t i = 0; i < a->len; i++) {\n        if (i) xs_sb_put(sb, \",\", 1);\n        ", tag, st);
        json_emit_value(b, t->elem, arr_is_ref(t) ? xs_sprintf("((%s)a->data[i])", ctype(t->elem)) : "a->data[i]");
        buf_puts(b, "\n    }\n    xs_sb_put(sb, \"]\", 1);\n}\n");
        buf_printf(b, "static %s *json_get_%s(xs_jv *v, const char *where) {\n    xs_json_arr(v, where);\n", st, tag);
        if (arr_is_ref(t)) buf_printf(b, "    %s *a = xs_arr_ref_new(v->n, %s);\n    for (size_t i = 0; i < v->n; i++) xs_arr_ref_push(a, ", st, elem_printer(t->elem));
        else buf_printf(b, "    %s *a = %s_new(0, 0);\n    for (size_t i = 0; i < v->n; i++) %s_push(a, ", st, st, st);
        json_emit_read(b, t->elem, "v->items[i]", "array element");
        buf_puts(b, ");\n    return a;\n}\n");
        buf_puts(&json_fns, local.data);
        return;
    }
    Class *c = t->cls;
    if (c->is_tuple) { /* [A, B]: a JSON array of exactly that many elements */
        buf_printf(&json_protos, "static void json_put_%s(xs_sb *sb, %s *o);\nstatic %s *json_get_%s(xs_jv *v, const char *where);\n", tag, c->cname, c->cname, tag);
        buf_printf(b, "static void json_put_%s(xs_sb *sb, %s *o) {\n    xs_sb_put(sb, \"[\", 1);\n", tag, c->cname);
        for (int i = 0; i < c->nfields; i++) {
            if (i) buf_puts(b, "    xs_sb_put(sb, \",\", 1);\n");
            buf_puts(b, "    ");
            json_emit_value(b, c->fields[i].type, xs_sprintf("o->%s", c->fields[i].cname));
            buf_puts(b, "\n");
        }
        buf_puts(b, "    xs_sb_put(sb, \"]\", 1);\n}\n");
        buf_printf(b, "static %s *json_get_%s(xs_jv *v, const char *where) {\n    xs_json_arr(v, where);\n"
                      "    if (v->n != %d) xs_panicf(\"JSON: '%%s' must be an array of %d elements\", where);\n    %s *o = xs_obj_alloc_t(sizeof(%s), &%s_type);\n",
                   c->cname, tag, c->nfields, c->nfields, c->cname, c->cname, c->cname);
        for (int i = 0; i < c->nfields; i++) {
            buf_printf(b, "    o->%s = ", c->fields[i].cname);
            json_emit_read(b, c->fields[i].type, xs_sprintf("v->items[%d]", i), where_elem(i));
            buf_puts(b, ";\n");
        }
        buf_puts(b, "    return o;\n}\n");
        buf_puts(&json_fns, local.data);
        return;
    }
    buf_printf(&json_protos, "static void json_put_%s(xs_sb *sb, %s *o);\nstatic %s *json_get_%s(xs_jv *v, const char *where);\n", tag, c->cname, c->cname, tag);
    Func *tj = json_tojson(c);
    if (tj) /* toJSON(): string: its result, as a JSON string */
        buf_printf(b, "static void json_put_%s(xs_sb *sb, %s *o) {\n    xs_str *t_ = %s((%s *)o);\n    xs_sb_str(sb, t_);\n    xs_release(t_);\n    if (0) {\n",
                   tag, c->cname, tj->cname, tj->cls->cname);
    buf_printf(b, tj ? "    xs_sb_put(sb, \"{\", 1);\n    bool first_ = true; (void)first_;\n" : "static void json_put_%s(xs_sb *sb, %s *o) {\n    xs_sb_put(sb, \"{\", 1);\n    bool first_ = true; (void)first_;\n", tag, c->cname);
    for (int i = 0; i < c->nfields; i++) {
        Field *f = &c->fields[i];
        if (ty_is_undef(f->type)) continue; /* r: undefined: JSON.stringify leaves it out */
        /* optional fields that are null are left out, like undefined in JSON.stringify */
        if (f->optional) buf_printf(b, "    if (%s) {\n", not_null(f->type, xs_sprintf("o->%s", f->cname)));
        buf_printf(b, "    if (!first_) xs_sb_put(sb, \",\", 1);\n    first_ = false;\n    xs_sb_put(sb, \"\\\"%s\\\":\", %zu);\n    ", f->name, strlen(f->name) + 3);
        /* (x?: unknown is unknown, which holds null already: no nullable wrapper around it) */
        Type *ft = f->optional && f->type->kind == TY_NULLABLE ? f->type->elem : f->type;
        json_emit_value(b, ft, f->optional && ft->kind == TY_BOOLEAN && f->type->kind == TY_NULLABLE ? xs_sprintf("(bool)o->%s", f->cname) : xs_sprintf("o->%s", f->cname));
        buf_puts(b, "\n");
        if (f->optional) buf_puts(b, "    }\n");
    }
    buf_puts(b, tj ? "    xs_sb_put(sb, \"}\", 1);\n    }\n}\n" : "    xs_sb_put(sb, \"}\", 1);\n}\n");
    /* parse: allocate the object without running the constructor, then fill every field from the JSON */
    buf_printf(b, "static %s *json_get_%s(xs_jv *v, const char *where) {\n", c->cname, tag);
    if (tj && c->nfields == 1 && c->fields[0].type->kind == TY_STRING) /* a value written by its toJSON() as one string (ObjectId's hex) reads back from it */
        buf_printf(b, "    if (v->kind == XJ_STR) { %s *s_ = xs_obj_alloc_t(sizeof(%s), &%s_type); s_->%s = xs_str_new(v->str, v->slen); return s_; }\n",
                   c->cname, c->cname, c->cname, c->fields[0].cname);
    buf_printf(b, "    if (v->kind != XJ_OBJ) xs_panicf(\"JSON: '%%s' must be an object\", where);\n    %s *o = xs_obj_alloc_t(sizeof(%s), &%s_type);\n", c->cname, c->cname, c->cname);
    for (int i = 0; i < c->nfields; i++) {
        Field *f = &c->fields[i];
        buf_printf(b, "    o->%s = ", f->cname);
        if (f->optional) { /* absent or null: null */
            buf_printf(b, "({ xs_jv *fv_ = xs_json_field_opt(v, \"%s\"); fv_ && fv_->kind != XJ_NULL ? ", f->name);
            json_emit_read(b, f->type->kind == TY_NULLABLE ? f->type->elem : f->type, "fv_", f->name);
            buf_printf(b, " : %s; })", null_value(f->type));
        } else if (c->data_only && (f->type->kind == TY_STRING || f->type->kind == TY_INT || f->type->kind == TY_NUMBER || f->type->kind == TY_BOOLEAN || f->type->kind == TY_ARRAY)) {
            /* a DTO (filled from JSON, checked by its validators): an absent field reads as empty, as class-validator
               would then report it ("x should not be empty"), instead of failing the whole parse */
            const char *zero = f->type->kind == TY_STRING ? "xs_str_new(\"\", 0)" : f->type->kind == TY_BOOLEAN ? "false"
                             : f->type->kind == TY_ARRAY ? (arr_is_ref(f->type) ? xs_sprintf("xs_arr_ref_new(0, %s)", elem_printer(f->type->elem)) : xs_sprintf("%s_new(0, 0)", arr_struct(f->type))) : "0";
            buf_printf(b, "({ xs_jv *fv_ = xs_json_field_opt(v, \"%s\"); fv_ && fv_->kind != XJ_NULL ? ", f->name);
            json_emit_read(b, f->type, "fv_", f->name);
            buf_printf(b, " : %s; })", zero);
        } else if (c->data_only && f->type->kind == TY_OBJECT && !f->type->cls->builtin) {
            /* an object of a class in a data class (a TypeORM entity's relation not loaded): absent reads as null,
               as the field is undefined in JavaScript */
            buf_printf(b, "({ xs_jv *fv_ = xs_json_field_opt(v, \"%s\"); fv_ && fv_->kind != XJ_NULL ? ", f->name);
            json_emit_read(b, f->type, "fv_", f->name);
            buf_puts(b, " : NULL; })");
        } else {
            json_emit_read(b, f->type, xs_sprintf("xs_json_field(v, \"%s\")", f->name), f->name);
        }
        buf_puts(b, ";\n");
    }
    buf_puts(b, "    return o;\n}\n");
    buf_puts(&json_fns, local.data);
}

/* x.kind on a union whose members all have a literal kind: the member classes with that literal (NULL: not that case) */
static bool union_lit_field(Expr *e) {
    if (e->kind != E_MEMBER || !e->builtin.cfn || strcmp(e->builtin.cfn, "union_field") != 0) return false;
    Type *u = e->elem_type;
    for (int i = 0; i < u->nparams; i++) {
        Field *fd = NULL;
        Class *c = u->params[i]->cls;
        for (int k = 0; c && k < c->nfields; k++) if (strcmp(c->fields[k].name, e->str) == 0) fd = &c->fields[k];
        if (!fd || !fd->type->tname) return false;
    }
    return true;
}
/* (f == A_free || f == B_free): the members of u whose literal field is lit */
static void emit_union_class_test(Type *u, const char *field, const char *lit, const char *fvar) {
    buf_puts(out, "(0");
    for (int i = 0; i < u->nparams; i++) {
        Class *c = u->params[i]->cls;
        for (int k = 0; k < c->nfields; k++)
            if (strcmp(c->fields[k].name, field) == 0 && strcmp(c->fields[k].type->tname, lit) == 0) buf_printf(out, " || %s == %s_free", fvar, c->cname);
    }
    buf_puts(out, ")");
}

static void emit_member(Expr *e) {
    Expr *base = e->lhs;
    if (e->builtin.cfn && strcmp(e->builtin.cfn, "union_field") == 0) { /* s.kind on a union: by the object's class */
        Type *u = e->elem_type;
        static int nuf;
        int id = nuf++;
        buf_printf(out, "({ void *uo%d_ = (void *)(", id);
        emit_borrowed(base);
        buf_printf(out, "); void (*uf%d_)(void *) = xs_type_of(uo%d_)->free; ", id, id);
        for (int i = 0; i < u->nparams; i++) {
            Class *c = u->params[i]->cls;
            if (i < u->nparams - 1) buf_printf(out, "uf%d_ == %s_free ? ((%s *)uo%d_)->f_%s : ", id, c->cname, c->cname, id, e->str);
            else buf_printf(out, "((%s *)uo%d_)->f_%s; })", c->cname, id, e->str);
        }
        return;
    }
    /* namespaces and static calls */
    if (base->kind == E_IDENT && !base->cname && e->func == NULL && strcmp(base->str, "Math") == 0) {
        if (!e->is_call) {
            const char *c = e->str;
            buf_puts(out, !strcmp(c, "PI") ? "3.141592653589793" : !strcmp(c, "E") ? "2.718281828459045" : !strcmp(c, "LN2") ? "0.6931471805599453"
                        : !strcmp(c, "LN10") ? "2.302585092994046" : !strcmp(c, "LOG2E") ? "1.4426950408889634" : !strcmp(c, "LOG10E") ? "0.4342944819032518"
                        : !strcmp(c, "SQRT2") ? "1.4142135623730951" : "0.7071067811865476");
            return;
        }
        const char *fn = e->str;
        if (strcmp(fn, "imul") == 0) {
            buf_puts(out, "((int64_t)(int32_t)(");
            emit_u32(e->args.items[0]);
            buf_puts(out, " * ");
            emit_u32(e->args.items[1]);
            buf_puts(out, "))");
            return;
        }
        if (strcmp(fn, "clz32") == 0) {
            buf_puts(out, "({ uint32_t cz_ = ");
            emit_u32(e->args.items[0]);
            buf_puts(out, "; (int64_t)(cz_ ? __builtin_clz(cz_) : 32); })");
            return;
        }
        if (strcmp(fn, "sign") == 0) fn = "xs_sign";
        else if (strcmp(fn, "cbrt") == 0) fn = "xs_cbrt"; /* (V8's, not the C library's: glibc's is off by an ulp) */
        else if (strcmp(fn, "fround") == 0) fn = "xs_fround";
        bool int_args = e->args.len > 0 && e->args.items[0]->type->kind == TY_INT;
        if (strcmp(fn, "min") == 0 || strcmp(fn, "max") == 0) { /* left to right, then folded: NaN wins, -0 < 0 */
            bool mn = fn[1] == 'i';
            if (e->args.len == 0) { buf_puts(out, mn ? "INFINITY" : "(-INFINITY)"); return; }
            const char *ty = int_args ? "int64_t" : "double", *f2 = int_args ? (mn ? "xs_imin" : "xs_imax") : (mn ? "xs_fmin" : "xs_fmax");
            buf_puts(out, "({ ");
            for (int k = 0; k < e->args.len; k++) { buf_printf(out, "%s mm%d_ = ", ty, k); emit_borrowed(e->args.items[k]); buf_puts(out, "; "); }
            buf_printf(out, "%s mr_ = mm0_; ", ty);
            for (int k = 1; k < e->args.len; k++) buf_printf(out, "mr_ = %s(mr_, mm%d_); ", f2, k);
            buf_puts(out, "mr_; })");
            return;
        }
        bool sited = true; /* the int conversions fail inline: they take a site instead of brackets */
        if (strcmp(fn, "abs") == 0) { fn = int_args ? "xs_iabs_s" : "fabs"; sited = int_args; }
        else if (strcmp(fn, "min") == 0) { fn = int_args ? "xs_imin" : "fmin"; sited = false; }
        else if (strcmp(fn, "max") == 0) { fn = int_args ? "xs_imax" : "fmax"; sited = false; }
        else if (strcmp(fn, "round") == 0) fn = "xs_round_int_s";
        else if (strcmp(fn, "floor") == 0) fn = "xs_floor_int_s";
        else if (strcmp(fn, "ceil") == 0) fn = "xs_ceil_int_s";
        else if (strcmp(fn, "trunc") == 0) fn = "xs_trunc_int_s";
        else if (strcmp(fn, "random") == 0) { fn = "xs_random"; sited = false; }
        else sited = false;
        buf_printf(out, "%s(", fn);
        emit_args_borrowed(&e->args, false);
        if (sited) site_arg(e->pos);
        buf_puts(out, ")");
        return;
    }
    if (base->kind == E_IDENT && !base->cname && e->builtin.cfn && strncmp(e->builtin.cfn, "promise_", 8) == 0) {
        emit_promise(e);
        return;
    }
    if (base->kind == E_IDENT && !base->cname && e->builtin.cfn && !e->cls && e->func == NULL && strcmp(base->str, "Math") != 0) {
        const char *fn = e->builtin.cfn;
        if (strcmp(fn, "json_stringify") == 0) {
            Expr *a = e->args.items[0];
            buf_printf(out, "({ xs_sb sb_ = {0}; xs_sb *sb = &sb_; %s ja_ = ", ctype(a->type)); /* the borrowed argument, read once
                                   (a T | null is tested, then written: one placeholder replaced would miss the second read) */
            emit_borrowed(a);
            buf_puts(out, "; ");
            json_emit_value(out, a->type, "ja_");
            buf_puts(out, " xs_sb_finish(sb); })");
            return;
        }
        if (strcmp(fn, "json_parse") == 0) {
            /* a generated function, not inlined: whatever fails while parsing returns to one labelled call */
            static int nparse;
            int id = nparse++;
            Buf h = {0};
            if (e->type->kind == TY_JSON) /* into `object` (held as JSON text): parsed as JSON.parse would, and written back
                                             as JSON.stringify writes the value ("\u00e7" comes out as "ç", spacing goes) */
                buf_printf(&h, "XS_NOINLINE xs_str *json_parse_%d(xs_str *s) { xs_jv *jv_ = xs_json_parse(s); xs_str *r_ = xs_json_text(jv_", id);
            else {
                buf_printf(&h, "XS_NOINLINE %s json_parse_%d(xs_str *s) { %sxs_jv *jv_ = xs_json_parse(s); %s r_ = ", ctype(e->type), id,
                           e->type->kind == TY_NULLABLE ? "if (!s) return NULL; " : "", ctype(e->type)); /* (object | null into T | null: null is null) */
                json_emit_read(&h, e->type, "jv_", "value");
            }
            if (e->type->kind == TY_JSON) buf_puts(&h, ")");
            buf_puts(&h, "; xs_jv_free(jv_); return r_; }\n");
            buf_puts(&json_fns, h.data);
            free(h.data);
            buf_printf(out, "json_parse_%d(", id);
            emit_borrowed(e->args.items[0]);
            buf_puts(out, ")");
            return;
        }
        if (strcmp(fn, "xs_str_from_char_codes") == 0 || strcmp(fn, "xs_str_from_code_points") == 0) {
            buf_printf(out, "%s(%d, (double[]){", fn, e->args.len);
            for (int k = 0; k < e->args.len; k++) {
                if (k) buf_puts(out, ", ");
                buf_puts(out, "(double)(");
                emit_borrowed(e->args.items[k]);
                buf_puts(out, ")");
            }
            buf_puts(out, e->args.len ? "})" : "0})");
            return;
        }
        if (!nz_mute && strcmp(fn, "xs_to_number") == 0 && e->args.len == 1 && may_nz(e->args.items[0])) { emit_nzd(e->args.items[0]); return; } /* XB-087 */
        buf_printf(out, "%s(", fn); /* Number.isNaN, Date.now, process.argv ... */
        emit_args_borrowed(&e->args, false);
        buf_puts(out, ")");
        return;
    }
    if (base->kind == E_IDENT && !base->cname && strcmp(base->str, "console") == 0) {
        /* JavaScript evaluates all arguments before printing any of them */
        buf_puts(out, "({ ");
        for (int i = 0; i < e->args.len; i++) {
            Expr *a = e->args.items[i];
            Type *t = a->type->kind == TY_NULL ? ty_nullable_of(ty_string) : a->type;
            bool nzp = a->type->kind == TY_INT && may_nz(a); /* XB-087: console.log(-0) prints -0 */
            if (nzp) t = ty_number;
            emit_decl(t, xs_sprintf("la%d_", i));
            buf_puts(out, " = ");
            if (nzp) emit_nzd(a); else emit_borrowed(a);
            buf_puts(out, "; ");
        }
        buf_puts(out, "xs_out_drain(); "); /* (XB-091: stdout written behind goes out first) */
        bool to_err = strcmp(e->str, "error") == 0 || strcmp(e->str, "warn") == 0; /* (as Node: error, warn -> stderr) */
        if (to_err) buf_puts(out, "fflush(stdout); FILE *pf_ = xs_out_f; xs_out_f = stderr; ");
        for (int i = 0; i < e->args.len; i++) {
            Expr *a = e->args.items[i];
            if (i) buf_puts(out, "xs_print_sep(); ");
            Expr tmpv = *a;
            tmpv.kind = E_IDENT;
            tmpv.cname = xs_sprintf("la%d_", i);
            if (a->type->kind == TY_INT && may_nz(a)) tmpv.type = ty_number;
            emit_print_arg(&tmpv);
            buf_puts(out, "; ");
        }
        buf_puts(out, to_err ? "xs_print_nl(); xs_out_f = pf_; })" : "xs_print_nl(); })");
        return;
    }
    if (base->kind == E_IDENT && !base->cname && e->cls && e->cls->builtin && e->func && e->field < 0 && e->builtin.cfn && !e->builtin.self_first) {
        /* static method of a built-in class */
        buf_printf(out, "%s(", e->builtin.cfn);
        emit_args_borrowed(&e->args, false);
        buf_puts(out, ")");
        return;
    }
    if (base->kind == E_NEW_ARRAY) {
        if (arr_is_ref(e->type)) { /* n copies of the same reference */
            buf_printf(out, "xs_arr_ref_fill(xs_arr_ref_new(0, %s), ", elem_printer(e->type->elem));
            emit_expr(base->lhs);
            buf_puts(out, ", ");
            emit_borrowed(e->args.items[0]);
            buf_puts(out, ")");
            return;
        }
        buf_printf(out, "%s_new(", arr_struct(e->type));
        emit_expr(base->lhs);
        buf_puts(out, ", ");
        emit_expr(e->args.items[0]);
        buf_puts(out, ")");
        return;
    }
    if (base->type->kind == TY_CHAN) {
        Type *el = base->type->elem;
        const char *op = e->builtin.cfn;
        int k = val_kind(el);
        if (strcmp(op, "size") == 0) { buf_puts(out, "((int64_t)("); emit_borrowed(base); buf_puts(out, ")->len)"); return; }
        if (strcmp(op, "close") == 0) { buf_puts(out, "xs_chan_close("); emit_borrowed(base); buf_puts(out, ")"); return; }
        if (strcmp(op, "send") == 0) {
            buf_puts(out, "xs_chan_send(");
            emit_borrowed(base);
            buf_puts(out, ", ");
            if (k == 3) { buf_puts(out, "XS_VPTR("); emit_owned(e->args.items[0]); buf_puts(out, ")"); }
            else { buf_puts(out, k == 0 ? "XS_VNUM(" : k == 1 ? "XS_VINT(" : "XS_VBOOL("); emit_expr(e->args.items[0]); buf_puts(out, ")"); }
            buf_puts(out, ")");
            return;
        }
        /* receive: the value is owned by the receiver */
        buf_puts(out, k == 0 ? "xs_chan_receive(" : k == 1 ? "xs_chan_receive(" : k == 2 ? "(xs_chan_receive(" : "((");
        if (k == 3) buf_printf(out, "%s)xs_chan_receive(", ctype(el));
        emit_borrowed(base);
        buf_puts(out, k == 0 ? ").d" : k == 1 ? ").i" : k == 2 ? ").i != 0)" : ").p)");
        return;
    }
    if (base->type->kind == TY_STREAM) {
        Type *elem = base->type->elem;
        const char *op = e->builtin.cfn;
        int ek = val_kind(elem);
        if (strcmp(op, "map") == 0) {
            Type *rt = e->type->elem;
            buf_printf(out, "xs_stream_map(");
            emit_borrowed(base);
            buf_puts(out, ", ");
            emit_borrowed(e->args.items[0]);
            buf_printf(out, ", %d, %s)", val_kind(rt), cref(rt) ? elem_printer(rt) : "0");
            return;
        }
        if (strcmp(op, "filter") == 0 || strcmp(op, "forEach") == 0) {
            buf_printf(out, "xs_stream_%s(", op);
            emit_borrowed(base);
            buf_puts(out, ", ");
            emit_borrowed(e->args.items[0]);
            buf_puts(out, ")");
            return;
        }
        if (strcmp(op, "batch") == 0) {
            buf_printf(out, "xs_stream_batch(");
            emit_borrowed(base);
            buf_puts(out, ", ");
            emit_expr(e->args.items[0]);
            buf_printf(out, ", %s)", cref(elem) ? elem_printer(elem) : "0");
            return;
        }
        if (strcmp(op, "take") == 0) {
            buf_puts(out, "xs_stream_take(");
            emit_borrowed(base);
            buf_puts(out, ", ");
            emit_expr(e->args.items[0]);
            buf_puts(out, ")");
            return;
        }
        if (strcmp(op, "reduce") == 0) {
            Type *acc = e->type;
            int ak = val_kind(acc);
            if (ak == 3) buf_printf(out, "((%s)", ctype(acc));
            buf_printf(out, "xs_stream_reduce_%s(", ak == 0 ? "num" : ak == 1 ? "int" : ak == 2 ? "bool" : "ptr");
            emit_borrowed(base);
            buf_puts(out, ", ");
            emit_borrowed(e->args.items[0]);
            buf_puts(out, ", ");
            if (ak == 3) { buf_printf(out, "(void *)"); emit_owned(e->args.items[1]); } else emit_expr(e->args.items[1]);
            buf_puts(out, ak == 3 ? "))" : ")");
            return;
        }
        if (strcmp(op, "toArray") == 0) {
            buf_printf(out, "(%s)xs_stream_to_array(", ctype(e->type));
            emit_borrowed(base);
            buf_printf(out, ", %s)", cref(elem) ? elem_printer(elem) : "0");
            return;
        }
        if (strcmp(op, "count") == 0) {
            buf_puts(out, "xs_stream_count(");
            emit_borrowed(base);
            buf_puts(out, ")");
            return;
        }
        buf_puts(out, "xs_stream_write_lines("); /* writeLines */
        emit_borrowed(base);
        buf_puts(out, ", ");
        emit_borrowed(e->args.items[0]);
        buf_puts(out, ")");
        (void)ek;
        return;
    }
    if (base->type->kind == TY_SET && base->type->elem->kind == TY_UNKNOWN) { /* Set<unknown> (xs_uset_*: SameValueZero) */
        const char *op = e->builtin.cfn;
        if (strcmp(op, "size") == 0) { buf_puts(out, "((int64_t)("); emit_borrowed(base); buf_puts(out, ")->count)"); return; }
        if (strcmp(op, "clear") == 0) { buf_puts(out, "xs_map_clear("); emit_borrowed(base); buf_puts(out, ")"); return; }
        if (strcmp(op, "values") == 0) {
            buf_puts(out, "(xs_arr_ref *)xs_map_values(");
            emit_borrowed(base);
            buf_printf(out, ", %s)", elem_printer(ty_unknown));
            return;
        }
        buf_printf(out, "xs_uset_%s(", strcmp(op, "has") == 0 ? "has" : strcmp(op, "delete") == 0 ? "delete" : "add");
        emit_borrowed(base);
        buf_puts(out, ", ");
        emit_borrowed(e->args.items[0]);
        buf_puts(out, ")");
        return;
    }
    if (base->type->kind == TY_MAP || base->type->kind == TY_SET) {
        Type *t = base->type;
        bool is_map = t->kind == TY_MAP;
        Type *k = is_map ? t->key : t->elem;
        Type *v = t->elem;
        const char *op = e->builtin.cfn;
        const char *ks = k->kind == TY_STRING ? "s" : "i";
        if (strcmp(op, "size") == 0) { buf_puts(out, "((int64_t)("); emit_borrowed(base); buf_puts(out, ")->count)"); return; }
        if (strcmp(op, "clear") == 0) { buf_puts(out, "xs_map_clear("); emit_borrowed(base); buf_puts(out, ")"); return; }
        if (strcmp(op, "entries") == 0) { /* a tuple per live entry, in insertion order */
            Class *tc = e->type->elem->cls;
            buf_printf(out, "({ xs_map *em_ = ");
            emit_borrowed(base);
            buf_printf(out, "; xs_arr_ref *er_ = xs_arr_ref_new(em_->count, %s); xs_map_entry *ee_; XS_MAP_EACH(em_, ee_) { %s *o_ = %s_new(); ",
                       elem_printer(e->type->elem), tc->cname, tc->cname);
            buf_printf(out, "o_->f__0 = %s; ", k->kind == TY_STRING ? "xs_retain(ee_->skey)" : "ee_->ikey");
            switch (val_kind(v)) {
            case 0: buf_puts(out, "o_->f__1 = ee_->val.d; "); break;
            case 1: buf_puts(out, "o_->f__1 = ee_->val.i; "); break;
            case 2: buf_puts(out, "o_->f__1 = ee_->val.i != 0; "); break;
            default: buf_printf(out, "o_->f__1 = (%s)xs_retain_(ee_->val.p); ", ctype(v)); break;
            }
            buf_puts(out, "xs_arr_ref_push(er_, o_); } er_; })");
            return;
        }
        if (strcmp(op, "keys") == 0 || strcmp(op, "values") == 0) {
            Type *et = strcmp(op, "keys") == 0 ? k : (is_map ? v : k);
            buf_printf(out, "(%s)xs_map_%s(", ctype(ty_array_of(et)), strcmp(op, "keys") == 0 || !is_map ? "keys" : "values");
            emit_borrowed(base);
            buf_printf(out, ", %s)", arr_is_ref(ty_array_of(et)) ? elem_printer(et) : "0");
            return;
        }
        if (strcmp(op, "has") == 0 || strcmp(op, "delete") == 0) {
            buf_printf(out, "xs_map_%s_%s(", op, ks);
            emit_borrowed(base);
            buf_puts(out, ", ");
            emit_borrowed(e->args.items[0]);
            buf_puts(out, ")");
            return;
        }
        if (strcmp(op, "get") == 0) {
            int vk = val_kind(v);
            buf_printf(out, vk == 0 ? "xs_map_get_num_%s(" : vk == 1 ? "xs_map_get_int_%s(" : vk == 2 ? "xs_map_get_bool_%s(" : "((%s)xs_map_get_ptr_%s(", vk == 3 ? ctype(v) : ks, ks);
            emit_borrowed(base);
            buf_puts(out, ", ");
            emit_borrowed(e->args.items[0]);
            buf_puts(out, vk == 3 ? "))" : ")");
            return;
        }
        /* set(k, v) / add(v): returns the map itself (borrowed) */
        buf_printf(out, "xs_map_set_%s(", ks);
        emit_borrowed(base);
        buf_puts(out, ", ");
        emit_borrowed(e->args.items[0]);
        buf_puts(out, ", ");
        if (is_map) {
            int vk = val_kind(v);
            if (vk == 3) { buf_puts(out, "XS_VPTR("); emit_owned(e->args.items[1]); buf_puts(out, ")"); }
            else { buf_printf(out, vk == 0 ? "XS_VNUM(" : vk == 1 ? "XS_VINT(" : "XS_VBOOL("); emit_expr(e->args.items[1]); buf_puts(out, ")"); }
        } else {
            buf_puts(out, "XS_VINT(0)");
        }
        buf_puts(out, ")");
        return;
    }
    if (base->type->kind == TY_ARRAY) {
        Type *at = base->type;
        if (!e->is_call) { /* .length, with its bound told to the compiler (see xs_len_bound) */
            buf_puts(out, "xs_len_bound((int64_t)(");
            emit_borrowed(base);
            buf_puts(out, ")->len)");
            return;
        }
        const char *op = e->builtin.cfn;
        {
            /* callback-based methods: the runtime loops, calling the closure */
            static const char *cbm[] = {"forEach", "map", "filter", "find", "findIndex", "some", "every"};
            bool is_cb = false;
            for (int i = 0; i < 7; i++) if (strcmp(op, cbm[i]) == 0) is_cb = true;
            if (is_cb) {
                Type *elem = at->elem;
                const char *sfx = arr_suffix(at);
                int arity = e->field;
                if (strcmp(op, "map") == 0) {
                    Type *rt = e->type->elem;
                    buf_printf(out, "(%s)xs_arr_map_%s_%s(", ctype(e->type), sfx, arr_suffix(e->type));
                    emit_borrowed(base);
                    buf_puts(out, ", ");
                    emit_borrowed(e->args.items[0]);
                    buf_printf(out, ", %d, %s)", arity, arr_is_ref(e->type) ? elem_printer(rt) : "0");
                    return;
                }
                if (strcmp(op, "find") == 0) buf_printf(out, "((%s)xs_arr_find_%s(", ctype(elem), sfx);
                else buf_printf(out, "xs_arr_%s_%s(", op, sfx);
                emit_borrowed(base);
                buf_puts(out, ", ");
                emit_borrowed(e->args.items[0]);
                buf_printf(out, ", %d)%s", arity, strcmp(op, "find") == 0 ? ")" : "");
                return;
            }
        }
        if (strcmp(op, "indexOf") == 0 || strcmp(op, "includes") == 0) {
            buf_printf(out, "%sxs_arr_index_of_%s(", strcmp(op, "includes") == 0 ? "0 <= " : "", at->elem->kind == TY_STRING || (at->elem->kind == TY_ENUM && at->elem->en->is_string) || (at->elem->kind == TY_NULLABLE && at->elem->elem->kind == TY_STRING) ? "str" : arr_suffix(at));
            emit_borrowed(base);
            buf_puts(out, ", ");
            emit_borrowed(e->args.items[0]);
            buf_puts(out, ")");
            return;
        }
        if (strcmp(op, "join") == 0) {
            bool nnum = at->elem->kind == TY_NULLABLE && (at->elem->elem->kind == TY_NUMBER || at->elem->elem->kind == TY_INT); /* (number | null)[]: "" for null */
            buf_printf(out, "xs_arr_join_%s%s(", at->elem->kind == TY_STRING || (at->elem->kind == TY_ENUM && at->elem->en->is_string) || (at->elem->kind == TY_NULLABLE && at->elem->elem->kind == TY_STRING) ? "str" : arr_suffix(at), nnum ? "_n" : "");
            emit_borrowed(base);
            buf_puts(out, ", ");
            if (e->args.len) emit_borrowed(e->args.items[0]); else buf_puts(out, "NULL");
            buf_puts(out, ")");
            return;
        }
        if (strcmp(op, "sort") == 0) {
            buf_printf(out, "(%s)xs_arr_sort_%s(", ctype(at), arr_suffix(at));
            emit_borrowed(base);
            buf_puts(out, ", ");
            emit_borrowed(e->args.items[0]);
            buf_printf(out, ", %d)", e->field);
            return;
        }
        if (strcmp(op, "reverse") == 0) {
            buf_printf(out, "(%s)xs_arr_reverse_%s(", ctype(at), arr_suffix(at));
            emit_borrowed(base);
            buf_puts(out, ")");
            return;
        }
        if (strcmp(op, "slice") == 0) {
            buf_printf(out, "(%s)xs_arr_slice_%s(", ctype(at), arr_suffix(at));
            emit_borrowed(base);
            buf_puts(out, ", ");
            if (e->args.len > 0) emit_expr(e->args.items[0]); else buf_puts(out, "0");
            buf_puts(out, ", ");
            if (e->args.len > 1) emit_expr(e->args.items[1]); else buf_puts(out, "INT64_MAX");
            buf_puts(out, ")");
            return;
        }
        if (strcmp(op, "push") == 0) {
            buf_printf(out, "%s_push(", arr_struct(at));
            emit_borrowed(base);
            buf_puts(out, ", ");
            if (arr_is_ref(at)) emit_owned(e->args.items[0]); else emit_expr(e->args.items[0]);
            buf_puts(out, ")");
            return;
        }
        if (strcmp(op, "unshift") == 0) {
            buf_printf(out, "%s_unshift(", arr_struct(at));
            emit_borrowed(base);
            buf_puts(out, ", ");
            if (arr_is_ref(at)) emit_owned(e->args.items[0]); else emit_expr(e->args.items[0]);
            buf_puts(out, ")");
            return;
        }
        if (strcmp(op, "pop") == 0 || strcmp(op, "shift") == 0) {
            if (arr_is_ref(at)) buf_printf(out, "((%s)xs_arr_ref_%s(", ctype(at->elem), op);
            else buf_printf(out, "%s_%s(", arr_struct(at), op);
            emit_borrowed(base);
            buf_puts(out, arr_is_ref(at) ? "))" : ")");
            return;
        }
        buf_printf(out, "%s_splice1(", arr_struct(at)); /* splice(i, 1) */
        emit_borrowed(base);
        buf_puts(out, ", ");
        emit_expr(e->args.items[0]);
        buf_puts(out, ")");
        return;
    }
    if (base->type->kind == TY_STRING && e->builtin.cfn && !getenv("XISTO_NO_CHAIN_FUSE")) {
        /* s.slice(a, b) followed by .replaceAll("<one ASCII character>", "") and/or .toUpperCase()/.toLowerCase(),
           in either order: one runtime pass over the source range (an ASCII s; anything else runs the calls as
           written, inside the same function) */
        Expr *cur = e, *src = NULL, *sl = NULL, *pat = NULL;
        int mode = 0, steps = 0;
        bool case_first = false, ok = true;
        while (ok && cur->kind == E_MEMBER && cur->is_call && cur->builtin.cfn && cur->lhs->type && cur->lhs->type->kind == TY_STRING) {
            const char *f = cur->builtin.cfn;
            if ((!strcmp(f, "xs_str_upper") || !strcmp(f, "xs_str_lower")) && !mode) {
                mode = !strcmp(f, "xs_str_upper") ? 1 : 2;
                if (pat) case_first = true; /* the replaceAll seen first is the outer call: case mapped inside it */
                steps++;
                cur = cur->lhs;
            } else if (!strcmp(f, "xs_str_replace_all") && !pat && cur->args.len == 2 && cur->args.items[0]->kind == E_STRING &&
                       cur->args.items[1]->kind == E_STRING && strlen(cur->args.items[0]->str) == 1 &&
                       (unsigned char)cur->args.items[0]->str[0] < 0x80 && cur->args.items[1]->str[0] == 0) {
                pat = cur->args.items[0];
                if (mode) case_first = false; /* case call outside the replaceAll */
                steps++;
                cur = cur->lhs;
            } else if (!strcmp(f, "xs_str_slice") && cur->args.len == 2) {
                sl = cur;
                src = cur->lhs;
                break;
            } else ok = false;
        }
        if (ok && sl && steps >= 1 && (pat || mode)) {
            static int nfuse;
            int id = nfuse++;
            bool own = is_fresh(src);
            buf_printf(out, "({ xs_str *fs%d_ = ", id);
            if (own) emit_expr(src); else emit_borrowed(src);
            buf_printf(out, "; xs_str *fr%d_ = xs_str_chain_fused(fs%d_, ", id, id);
            emit_expr(sl->args.items[0]);
            buf_puts(out, ", ");
            emit_expr(sl->args.items[1]);
            buf_puts(out, ", ");
            if (pat) emit_borrowed(pat); else buf_puts(out, "NULL");
            buf_printf(out, ", %d, %s); ", mode, case_first ? "true" : "false");
            if (own) buf_printf(out, "xs_release_as(fs%d_, xs_str_free); ", id);
            buf_printf(out, "fr%d_; })", id);
            return;
        }
    }
    if (base->type->kind == TY_STRING && e->builtin.cfn && is_fresh(base) && !getenv("XISTO_NO_CHAIN_FREE") &&
        (!strcmp(e->builtin.cfn, "xs_str_replace_re") || !strcmp(e->builtin.cfn, "xs_str_replace_all_re") ||
         !strcmp(e->builtin.cfn, "xs_str_replace") || !strcmp(e->builtin.cfn, "xs_str_replace_all") ||
         !strcmp(e->builtin.cfn, "xs_str_upper") || !strcmp(e->builtin.cfn, "xs_str_lower"))) {
        /* s.replace(...).replace(...): each intermediate string is released as soon as the next one is made,
           not at the end of the statement (regex-redux: a chain of five over 50 MB peaked at 273 MB on the
           Xeon, now ~100). These functions return an owned string (a retained s when nothing matched). */
        static int nchain;
        int id = nchain++;
        buf_printf(out, "({ xs_str *cr%d_ = ", id);
        emit_expr(base);
        /* replaceAll that does not grow, toUpperCase/toLowerCase of ASCII: in the temporary's own buffer when
           nothing else holds it (rc 1 at run time; the slice + replaceAll + toUpperCase of k-nucleotide's 125 MB
           made two copies of it, now none) */
        const char *fn = e->builtin.cfn;
        bool mv = !getenv("XISTO_NO_STR_INPLACE");
        if (mv && !strcmp(fn, "xs_str_replace_all")) fn = "xs_str_replace_all_mv";
        else if (mv && !strcmp(fn, "xs_str_upper")) fn = "xs_str_upper_mv";
        else if (mv && !strcmp(fn, "xs_str_lower")) fn = "xs_str_lower_mv";
        buf_printf(out, "; xs_str *cq%d_ = %s(cr%d_", id, fn, id);
        emit_args_borrowed(&e->args, true);
        buf_printf(out, "); xs_release_as(cr%d_, xs_str_free); cq%d_; })", id, id);
        return;
    }
    if (base->type->kind == TY_STRING || ((base->type->kind == TY_INT || base->type->kind == TY_NUMBER) && e->builtin.cfn)) {
        buf_printf(out, "%s(", e->builtin.cfn);
        emit_borrowed(base);
        emit_args_borrowed(&e->args, true);
        buf_puts(out, ")");
        return;
    }
    if (e->cls && e->cls->builtin && strcmp(e->cls->name, "StatementSync") == 0 && e->builtin.cfn) {
        /* bound parameters as a compound literal of xs_sqarg */
        Buf args = {0};
        Buf *saved = out;
        out = &args;
        buf_printf(out, "%d, (xs_sqarg[]){", e->args.len);
        if (e->args.len == 0) buf_puts(out, "{0}");
        for (int i = 0; i < e->args.len; i++) {
            Expr *a = e->args.items[i];
            if (i) buf_puts(out, ", ");
            switch (a->type->kind) {
            case TY_INT: buf_puts(out, "{1, .i = "); emit_expr(a); buf_puts(out, "}"); break;
            case TY_BOOLEAN: buf_puts(out, "{4, .i = "); emit_expr(a); buf_puts(out, "}"); break;
            case TY_NUMBER: buf_puts(out, "{2, .d = "); emit_expr(a); buf_puts(out, "}"); break;
            case TY_ENUM:
                if (a->type->en->is_string) { buf_puts(out, "{3, .s = "); emit_expr(a); buf_puts(out, "}"); }
                else { buf_puts(out, "{1, .i = "); emit_expr(a); buf_puts(out, "}"); }
                break;
            case TY_NULL: buf_puts(out, "{0}"); break;
            default: buf_puts(out, "{3, .s = "); emit_borrowed(a); buf_puts(out, "}"); break;
            }
        }
        buf_puts(out, "}");
        out = saved;
        const char *op = e->builtin.cfn;
        if (strcmp(op, "run") == 0) {
            buf_puts(out, "xs_StatementSync_run(");
            emit_borrowed(base);
            buf_printf(out, ", %s)", args.data);
            return;
        }
        bool all = strcmp(op, "all") == 0;
        Type *row = e->type->elem; /* T[] or T | null */
        /* rows are converted one at a time; the intermediate row lives in a reused arena block. A generated
           function, not inlined: a failing statement or row returns to one labelled call (stack traces). */
        static int nrows;
        int id = nrows++;
        Buf h = {0};
        buf_printf(&h, "XS_NOINLINE %s sql_rows_%d(xs_StatementSync *st_, int n_, xs_sqarg *a_) { xs_sqlite_start(st_, n_, a_); xs_jv *jv_; xs_json_lenient = true; ",
                   all ? "xs_arr_ref *" : ctype(e->type), id);
        if (all) {
            buf_printf(&h, "xs_arr_ref *r_ = xs_arr_ref_new(16, %s); while ((jv_ = xs_sqlite_step_row(st_))) xs_arr_ref_push(r_, ", elem_printer(row));
            json_emit_read(&h, row, "jv_", "row");
            buf_puts(&h, ");");
        } else {
            buf_printf(&h, "%s r_ = NULL; if ((jv_ = xs_sqlite_step_row(st_))) { r_ = ", ctype(row));
            json_emit_read(&h, row, "jv_", "row");
            buf_puts(&h, "; sqlite3_reset(st_->st); }");
        }
        buf_puts(&h, " xs_json_lenient = false; xs_jarena_reset(); return r_; }\n");
        buf_puts(&json_fns, h.data);
        free(h.data);
        buf_printf(out, "sql_rows_%d(", id);
        emit_borrowed(base);
        buf_printf(out, ", %s)", args.data);
        return;
    }
    /* object */
    if (e->field >= 0 && e->cls->builtin && (strcmp(e->cls->cname, "xs_Uint8Array") == 0 || strcmp(e->cls->cname, "xs_Float64Array") == 0)) {
        buf_puts(out, e->cls->cname[3] == 'U' ? "xs_u8_len(" : "xs_f64_len("); /* bytes.length: with its bound told to the compiler */
        emit_borrowed(base);
        buf_puts(out, ")");
        return;
    }
    if (e->field >= 0) {
        buf_puts(out, "(");
        emit_borrowed(base);
        buf_printf(out, ")->%s", e->cls->fields[e->field].cname);
        return;
    }
    if (!e->cls->builtin && e->func->cls && e->func->cls != e->cls) { /* inherited (or dispatched): the receiver as the method's class */
        buf_printf(out, "%s((%s *)(", e->virt ? dispatcher(e->func) : e->deco_orig && e->func->deco_stub ? xs_sprintf("%s__orig", e->func->cname) : e->func->cname,
                   e->func->cls->cname);
        emit_borrowed(base);
        buf_puts(out, ")");
        emit_args_borrowed(&e->args, true);
        buf_puts(out, ")");
        return;
    }
    buf_printf(out, "%s(", e->cls->builtin ? e->builtin.cfn : e->virt ? dispatcher(e->func)
                          : e->deco_orig && e->func->deco_stub ? xs_sprintf("%s__orig", e->func->cname) : e->func->cname);
    emit_borrowed(base);
    emit_args_borrowed(&e->args, true);
    buf_puts(out, ")");
}

/* Promise.all / race: a task group (runtime/xisto_net.h). The result is built from the tasks' values, which
   are owned (references move into the array). */
static void emit_promise(Expr *e) {
    const char *fn = e->builtin.cfn;
    if (strcmp(fn, "promise_all_arr") == 0 || strcmp(fn, "promise_race_arr") == 0) { /* an array of promises kept (XB-106) */
        static int npa;
        int id = npa++;
        Type *t = e->elem_type;
        int k = t->kind == TY_VOID ? 4 : val_kind(t);
        const char *sf = k == 0 ? "d" : k == 1 ? "i" : k == 2 ? "b" : k == 3 ? "p" : "v";
        buf_printf(out, "({ xs_arr_ref *pa%d_ = ", id);
        emit_borrowed(e->args.items[0]);
        buf_puts(out, "; ");
        if (fn[8] == 'r') {
            buf_printf(out, "xs_promise *pw%d_ = xs_promise_race_arr(pa%d_); %s pr%d_ = (%s)xs_promise_await_%s(pw%d_); xs_release(pw%d_); pr%d_; })",
                       id, id, ctype(t), id, ctype(t), sf, id, id, id);
            return;
        }
        if (k == 4) { buf_printf(out, "for (size_t i_ = 0; i_ < pa%d_->len; i_++) xs_promise_await_v(pa%d_->data[i_]); })", id, id); return; }
        Type *at = e->type;
        if (arr_is_ref(at))
            buf_printf(out, "xs_arr_ref *pr%d_ = xs_arr_ref_new(pa%d_->len, %s); ", id, id, elem_printer(t));
        else
            buf_printf(out, "%s *pr%d_ = %s_new(0, 0); ", arr_struct(at), id, arr_struct(at));
        buf_printf(out, "for (size_t i_ = 0; i_ < pa%d_->len; i_++) %s_push(pr%d_, xs_promise_await_%s(pa%d_->data[i_])); pr%d_; })",
                   id, arr_is_ref(at) ? "xs_arr_ref" : arr_struct(at), id, sf, id, id);
        return;
    }
    if (strcmp(fn, "promise_all_tuple") == 0) { /* elements of different types: each task its own kind, a tuple of them */
        static int npt;
        int id = npt++;
        Class *tc = e->type->cls;
        buf_printf(out, "({ xs_tgroup *pt%d_ = xs_par_new(%d, 0); ", id, e->args.len);
        for (int i = 0; i < e->args.len; i++) {
            buf_printf(out, "pt%d_->t[%d].kind = %d; pt%d_->t[%d].cl = ", id, i, val_kind(tc->fields[i].type), id, i);
            emit_owned(e->args.items[i]);
            buf_puts(out, "; ");
        }
        buf_printf(out, "xs_par_run(pt%d_); xs_par_check(pt%d_); %s *tr%d_ = %s_new(); ", id, id, tc->cname, id, tc->cname);
        for (int i = 0; i < e->args.len; i++) {
            Type *ft = tc->fields[i].type;
            int k = val_kind(ft);
            buf_printf(out, "tr%d_->%s = (%s)xs_par_take(pt%d_, %d).%s%s; ", id, tc->fields[i].cname, ctype(ft), id, i,
                       k == 0 ? "d" : k == 3 ? "p" : "i", k == 2 ? " != 0" : "");
        }
        buf_printf(out, "xs_par_unref(pt%d_); tr%d_; })", id, id);
        return;
    }
    bool race = strstr(fn, "race") != NULL, map = strstr(fn, "_map") != NULL;
    Type *t = e->elem_type;
    int k = val_kind(t);
    const char *vf = k == 0 ? "d" : k == 3 ? "p" : "i";
    Type *at = race ? ty_array_of(t) : e->type;
    static int npg;
    int id = npg++;
    buf_printf(out, "({ xs_tgroup *pg%d_; ", id);
    if (map) {
        Expr *call = e->args.items[0]; /* xs.map(f) */
        if (!call->lhs || call->kind != E_MEMBER) /* XB-171: Promise.all(xs.map(f)) kept as a promise (.then on it): the spawn evaluated the map already */
            xs_error(XS9902, e->pos, "keep Promise.all(xs.map(f)) inside an async function and keep that function's promise", "internal: Promise.all(xs.map(f)) kept as a promise is not supported yet");
        Type *src = call->lhs->type;
        int ak = val_kind(src->elem);
        buf_printf(out, "%s *ps%d_ = ", arr_struct(src), id);
        emit_borrowed(call->lhs);
        buf_printf(out, "; xs_closure *pc%d_ = ", id);
        emit_borrowed(call->args.items[0]);
        buf_printf(out, "; pg%d_ = xs_par_new((int)ps%d_->len, %d); ", id, id, race ? 1 : 0);
        buf_printf(out, "for (size_t i_ = 0; i_ < ps%d_->len; i_++) { xs_task *t_ = &pg%d_->t[i_]; t_->cl = xs_retain(pc%d_); t_->kind = %d; t_->has_arg = true; t_->argkind = %d; t_->arg.%s = ",
                   id, id, id, k, ak, ak == 0 ? "d" : ak == 3 ? "p" : "i");
        buf_printf(out, ak == 3 ? "(void *)ps%d_->data[i_]; } " : "ps%d_->data[i_]; } ", id);
    } else {
        buf_printf(out, "pg%d_ = xs_par_new(%d, %d); ", id, e->args.len, race ? 1 : 0);
        for (int i = 0; i < e->args.len; i++) {
            buf_printf(out, "pg%d_->t[%d].kind = %d; pg%d_->t[%d].cl = ", id, i, k, id, i);
            emit_owned(e->args.items[i]);
            buf_puts(out, "; ");
        }
    }
    buf_printf(out, "xs_par_run(pg%d_); xs_par_check(pg%d_); ", id, id);
    if (race) {
        buf_printf(out, "%s pr%d_ = (%s)xs_par_take(pg%d_, pg%d_->winner).%s%s; ", ctype(t), id, ctype(t), id, id, vf, k == 2 ? " != 0" : "");
    } else if (arr_is_ref(at)) {
        buf_printf(out, "xs_arr_ref *pr%d_ = xs_arr_ref_new((size_t)pg%d_->n, %s); for (int i_ = 0; i_ < pg%d_->n; i_++) xs_arr_ref_push(pr%d_, xs_par_take(pg%d_, i_).p); ",
                   id, id, elem_printer(t), id, id, id);
    } else {
        buf_printf(out, "%s *pr%d_ = %s_new(0, 0); for (int i_ = 0; i_ < pg%d_->n; i_++) %s_push(pr%d_, xs_par_take(pg%d_, i_).%s%s); ",
                   arr_struct(at), id, arr_struct(at), id, arr_struct(at), id, id, vf, k == 2 ? " != 0" : "");
    }
    buf_printf(out, "xs_par_unref(pg%d_); pr%d_; })", id, id);
}

/* An async call kept as a promise (XB-106): the callee and its arguments are evaluated here, as JavaScript
   does, into a block on the heap; the call itself runs on a coroutine of its own (runtime/xisto_net.h,
   xs_promise_spawn), which starts at once and settles the promise when the call returns or throws. The call is
   emitted again in a function of its own, each argument read from the block. */
static void emit_decl(Type *t, const char *name);
static bool sp_plain(Expr *a) {
    return a->kind == E_NUMBER || a->kind == E_STRING || a->kind == E_BOOL || a->kind == E_NULL || a->kind == E_FUNC_REF ||
           (a->kind == E_IDENT && !a->cname) || !a->type || a->type->kind == TY_LITINT;
}
static int sp_kind(Type *t) { return !t || t->kind == TY_VOID || t->kind == TY_NULL ? 4 : val_kind(t); }
static void emit_spawn(Expr *e) {
    static int nsp;
    int id = nsp++;
    Expr *c = e->lhs;
    Expr **slot[128];
    int n = 0;
    bool named = c->kind == E_CALL && (c->func || c->builtin.cfn);
    if (c->lhs && !named && !sp_plain(c->lhs) && n < 128) slot[n++] = &c->lhs;
    for (int i = 0; i < c->args.len && n < 128; i++) if (!sp_plain(c->args.items[i])) slot[n++] = &c->args.items[i];
    Expr *orig[128];
    Expr *fake = xs_alloc(sizeof(Expr) * (size_t)(n ? n : 1));
    for (int i = 0; i < n; i++) {
        orig[i] = *slot[i];
        fake[i].kind = E_IDENT; fake[i].pos = orig[i]->pos; fake[i].type = orig[i]->type; fake[i].field = -1;
        fake[i].cname = xs_sprintf("s_->o%d", i);
    }
    Buf h = {0};
    Buf *saved = out;
    int saved_indent = indent;
    bool saved_ar = used_ar;
    out = &h;
    indent = 1;
    buf_puts(out, "typedef struct { ");
    for (int i = 0; i < n; i++) { emit_decl(orig[i]->type, xs_sprintf("o%d", i)); buf_puts(out, "; "); }
    buf_printf(out, "char u_; } xs_sp%d;\n", id);
    buf_printf(out, "static void xs_sp%d_free(void *a_) { xs_sp%d *s_ = a_; (void)s_; ", id, id);
    for (int i = 0; i < n; i++) if (crc(orig[i]->type)) buf_printf(out, "xs_release(s_->o%d); ", i);
    buf_puts(out, "free(a_); }\n");
    buf_printf(out, "static xs_val xs_sp%d_run(void *a_) {\n    xs_sp%d *s_ = a_; (void)s_;\n    xs_val r_ = {0};\n    size_t ar_mark = xs_ar_mark(); (void)ar_mark;\n    ", id, id);
    for (int i = 0; i < n; i++) *slot[i] = &fake[i];
    int k = sp_kind(c->type);
    if (k == 4) emit_expr(c);
    else {
        buf_printf(out, "r_.%s = ", k == 0 ? "d" : k == 3 ? "p" : "i");
        if (k == 3 && crc(c->type)) emit_owned(c); else emit_expr(c);
    }
    for (int i = 0; i < n; i++) *slot[i] = orig[i];
    buf_puts(out, ";\n    xs_ar_drain(ar_mark);\n    return r_;\n}\n");
    out = saved;
    indent = saved_indent;
    used_ar = saved_ar;
    buf_puts(&clone_fns, h.data);
    free(h.data);
    buf_printf(out, "({ xs_sp%d *sp%d_ = malloc(sizeof *sp%d_); ", id, id, id);
    for (int i = 0; i < n; i++) {
        buf_printf(out, "sp%d_->o%d = ", id, i);
        if (crc(orig[i]->type)) emit_owned(orig[i]); else emit_expr(orig[i]);
        buf_puts(out, "; ");
    }
    buf_printf(out, "xs_promise_spawn(%d, xs_sp%d_run, xs_sp%d_free, sp%d_); })", k, id, id, id);
}

/* emits f's copy for the call whose site is csite (into clone_fns) and returns its C name */
static char *emit_clone(Func *f, int csite) {
    char *name = xs_sprintf("%s_k%d", f->cname, csite);
    if (++nclones > 20000) return NULL;
    Buf *saved_out = out;
    EScope *saved_scope = escope;
    Func *saved_func = cur_func, *saved_cf = clone_func;
    int saved_indent = indent, saved_ci = clone_inl;
    bool saved_ctor = in_ctor, saved_ar = used_ar, saved_vol = vol_locals;
    Class *saved_class = cur_class;
    Module *saved_mod = cur_mod;
    Expr *saved_b = bracketing;
    Buf b = {0};
    out = &b;
    indent = 0;
    escope = NULL;
    in_ctor = false;
    bracketing = NULL;
    clone_func = f;
    clone_inl = csite;
    char *cname = f->cname;
    f->cname = name;
    force_storage = "XS_FA";
    buf_putc(out, '\n');
    emit_signature(f);
    force_storage = NULL;
    f->cname = cname;
    emit_func_body(f);
    buf_puts(&clone_fns, b.data);
    free(b.data);
    out = saved_out; escope = saved_scope; cur_func = saved_func; clone_func = saved_cf; indent = saved_indent; clone_inl = saved_ci;
    in_ctor = saved_ctor; used_ar = saved_ar; vol_locals = saved_vol; cur_class = saved_class; cur_mod = saved_mod; bracketing = saved_b;
    return name;
}

/* any (checker.c, the dynamic path): each class's fields by name, offset and kind, for x.f / x[k] on an any. A field
   of a class type is written only with an object of that class (or a subclass): its isa function says. */
static const char *dyn_ref(Class *c) { return xs_any_used ? xs_sprintf(", .dyn = &%s_dyn", c->cname) : ""; }
static const char *dyn_isa(Program *p, Type *t, Buf *fns) {
    static char **made;
    static int nmade;
    const char *name = NULL;
    Buf body = {0};
    switch (t->kind) {
    case TY_OBJECT:
        if (t->cls->builtin) {
            if (strcmp(t->cls->name, "Date") != 0) return NULL;
            name = "xs_dynisa_Date";
            buf_puts(&body, "return xs_type_of(v)->free == xs_Date_free;");
            break;
        }
        name = xs_sprintf("xs_dynisa_%s", t->cls->cname);
        buf_puts(&body, "void (*f_)(void *) = xs_type_of(v)->free; return 0");
        for (int i = 0; i < p->nclasses; i++) {
            Class *k = p->classes[i];
            if (k->used && !k->builtin && is_sub(k, t->cls)) buf_printf(&body, " || f_ == %s_free", k->cname);
        }
        buf_puts(&body, ";");
        break;
    case TY_ARRAY:
        name = xs_sprintf("xs_dynisa_%s", arr_struct(t));
        buf_printf(&body, "return xs_type_of(v)->free == %s_free;", arr_struct(t));
        break;
    case TY_MAP: case TY_SET: name = "xs_dynisa_map"; buf_puts(&body, "return xs_type_of(v)->free == xs_map_free;"); break;
    case TY_FUNC: name = "xs_dynisa_fn"; buf_puts(&body, "return xs_type_of(v)->print == xs_print_closure;"); break;
    default: return NULL;
    }
    for (int i = 0; i < nmade; i++) if (strcmp(made[i], name) == 0) { free(body.data); return name; }
    made = realloc(made, sizeof(char *) * (size_t)(nmade + 1));
    made[nmade++] = (char *)name;
    buf_printf(fns, "static bool %s(void *v) { %s }\n", name, body.data);
    free(body.data);
    return name;
}
static void emit_dyn_tables(Program *p) {
    for (int i = 0; i < p->nclasses; i++)
        if (p->classes[i]->used && !p->classes[i]->builtin) buf_printf(out, "static void %s_free(void *p);\n", p->classes[i]->cname);
    Buf fns = {0}, tabs = {0};
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (!c->used || c->builtin) continue;
        if (c->nfields) buf_printf(&tabs, "static const xs_dyn_field %s_dynf[] = {\n", c->cname);
        for (int k = 0; k < c->nfields; k++) {
            Field *f = &c->fields[k];
            Type *t = f->type;
            bool nul = t->kind == TY_NULLABLE;
            Type *b = nul ? t->elem : t;
            const char *kind, *isa = NULL;
            if (b->kind == TY_NUMBER) kind = nul ? "XD_NNUM" : "XD_NUM";
            else if (int_like(b) || b->kind == TY_LITINT) kind = nul ? "XD_NINT" : "XD_INT";
            else if (b->kind == TY_BOOLEAN) kind = nul ? "XD_NBOOL" : "XD_BOOL";
            else if (b->kind == TY_STRING || b->kind == TY_ENUM) kind = "XD_STR";
            else if (b->kind == TY_UNKNOWN || b->kind == TY_UNION) kind = "XD_ANY";
            else { kind = "XD_REF"; isa = dyn_isa(p, b, &fns); }
            buf_printf(&tabs, "    {\"%s\", %zu, (uint32_t)offsetof(%s, %s), %s, %d, %d, %s, ", f->name, strlen(f->name), c->cname, f->cname, kind,
                       nul || f->optional, f->optional, isa ? isa : "NULL");
            { Buf *so = out; out = &tabs; emit_c_string(ty_name(b)); out = so; } /* (a literal type's name has its quotes: 'A' is "A") */
            buf_puts(&tabs, "},\n");
        }
        if (c->nfields) buf_puts(&tabs, "};\n");
        int nm = 0; /* its methods' names, its bases' too (typeof x.toJSON === "function" through an any) */
        for (Class *k = c; k; k = k->base)
            for (int j = 0; j < k->nmethods; j++) {
                if (!nm) buf_printf(&tabs, "static const char *const %s_dynm[] = {", c->cname);
                buf_printf(&tabs, "%s\"%s\"", nm ? ", " : "", k->methods[j]->name);
                nm++;
            }
        if (nm) buf_puts(&tabs, "};\n");
        buf_printf(&tabs, "static const xs_dyn_cls %s_dyn = {\"%s\", %d, %s, %d, %s, %d};\n", c->cname, c->name, c->nfields, c->nfields ? xs_sprintf("%s_dynf", c->cname) : "NULL",
                   nm, nm ? xs_sprintf("%s_dynm", c->cname) : "NULL", c->is_interface ? 1 : 0);
    }
    if (fns.data) buf_puts(out, fns.data);
    if (tabs.data) buf_puts(out, tabs.data);
    free(fns.data);
    free(tabs.data);
}
/* C expression: `expr` (an unknown) checked and converted to t, as `expr as t` */
static bool fu_dyn; /* from an any: an int may come as a number with no fraction (JavaScript has only numbers) */
static char *from_unknown(Type *t, const char *expr) {
    bool nul = t->kind == TY_NULLABLE;
    Type *b = nul ? t->elem : t;
    const char *what = ty_name(t);
    const char *as_int = fu_dyn ? "xs_dyn_as_int" : "xs_as_int";
    switch (b->kind) {
    case TY_INT: return xs_sprintf("%s%s(%s)", as_int, nul ? "_or_null" : "", expr);
    case TY_ENUM:
        if (!b->en->is_string) return xs_sprintf("%s%s(%s)", as_int, nul ? "_or_null" : "", expr);
        return xs_sprintf("((xs_str *)xs_as_ref(%s, xs_str_free, %d, \"%s\"))", expr, nul, what);
    case TY_STRING: return xs_sprintf("((xs_str *)xs_as_ref(%s, xs_str_free, %d, \"%s\"))", expr, nul, what);
    case TY_NUMBER: return xs_sprintf("xs_as_num%s(%s)", nul ? "_or_null" : "", expr);
    case TY_BOOLEAN: return xs_sprintf("xs_as_bool%s(%s)", nul ? "_or_null" : "", expr);
    case TY_OBJECT: {
        if (b->cls->view_of) { /* a view of the request (getRequest<LocalRequest>()): the request itself, checked as one */
            Type *r = ty_object_of(b->cls->view_of);
            return xs_sprintf("((%s)%s)", ctype(b), from_unknown(nul ? ty_nullable_of(r) : r, expr));
        }
        if (in_hierarchy(b->cls)) return xs_sprintf("((%s)xs_as_isa((void *)(%s), %s, %d, \"%s\"))", ctype(b), expr, isa_fn(b->cls), nul, what);
        const char *fr = strcmp(b->cls->name, "Error") == 0 ? "xs_Error_free" : xs_sprintf("%s_free", b->cls->cname);
        if (strcmp(b->cls->cname, "xs_Uint8Array") == 0) fr = "xs_u8_free"; /* (Buffer too: the same free, another print) */
        else if (strcmp(b->cls->cname, "xs_Float64Array") == 0) fr = "xs_f64_free";
        return xs_sprintf("((%s)xs_as_ref(%s, %s, %d, \"%s\"))", ctype(b), expr, fr, nul, what);
    }
    case TY_ARRAY: {
        Type *el = b->elem->kind == TY_NULLABLE ? b->elem->elem : b->elem;
        if (el->kind == TY_OBJECT && !in_hierarchy(el->cls) && !el->cls->builtin) /* each element checked too */
            return xs_sprintf("((%s)xs_as_arr_of(%s, %s_free, %d, %d, \"%s\"))", ctype(b), expr, el->cls->cname, b->elem->kind == TY_NULLABLE, nul, what);
        if (!arr_is_ref(b) && (b->elem->kind == TY_NUMBER || b->elem->kind == TY_INT || b->elem->kind == TY_BOOLEAN)) /* (from JSON: unknowns, copied) */
            return xs_sprintf("((%s)xs_as_arr_prim(%s, %d, %d, \"%s\"))", ctype(b), expr, b->elem->kind == TY_NUMBER ? 0 : b->elem->kind == TY_INT ? 1 : 2, nul, what);
        const char *fr = arr_is_ref(b) ? "xs_arr_ref_free" : xs_sprintf("%s_free", arr_struct(b));
        return xs_sprintf("((%s)xs_as_ref(%s, %s, %d, \"%s\"))", ctype(b), expr, fr, nul, what);
    }
    case TY_MAP: case TY_SET:
        if (b->kind == TY_MAP && b->record && b->key->kind == TY_STRING && b->elem->kind == TY_UNKNOWN) /* (an object of a class too: its fields) */
            return xs_sprintf("((xs_map *)xs_as_record(%s, %d, \"%s\"))", expr, nul, what);
        return xs_sprintf("((xs_map *)xs_as_ref(%s, xs_map_free, %d, \"%s\"))", expr, nul, what);
    case TY_FUNC: return xs_sprintf("((xs_closure *)xs_as_closure(%s, %d))", expr, nul);
    case TY_JSON: case TY_UNKNOWN: return xs_sprintf("(%s)", expr);
    default: return xs_sprintf("NULL");
    }
}
/* C expression: a value of type t as an unknown (primitives boxed) */
static char *to_unknown_c(Type *t, const char *expr) {
    bool nul = t->kind == TY_NULLABLE;
    Type *b = nul ? t->elem : t;
    if (b->kind == TY_INT || (b->kind == TY_ENUM && !b->en->is_string)) return xs_sprintf("xs_box_int%s(%s)", nul ? "_or_null" : "", expr);
    if (b->kind == TY_NUMBER) return xs_sprintf("xs_box_num%s(%s)", nul ? "_or_null" : "", expr);
    if (b->kind == TY_BOOLEAN) return xs_sprintf("xs_box_bool%s(%s)", nul ? "_or_null" : "", expr);
    return xs_sprintf("((void *)(%s))", expr);
}
/* a class already being checked counts as capable: recursive types (parent?: Article) read and write through
   the named json_get_obj_ / json_put_obj_ functions, so the cycle needs no further proof */
static Class *json_capable_stack[64];
static int json_capable_depth;
static bool json_capable(Type *t) {
    switch (t->kind) {
    case TY_NUMBER: case TY_INT: case TY_BOOLEAN: case TY_STRING: case TY_ENUM: case TY_JSON: case TY_UNKNOWN: case TY_UNION: return true;
    case TY_ARRAY: case TY_NULLABLE: return json_capable(t->elem);
    case TY_MAP: return t->record && t->key && t->key->kind == TY_STRING && json_capable(t->elem); /* (a Record<string, V>: an object) */
    case TY_OBJECT: {
        if (t->cls->builtin) return strcmp(t->cls->name, "Date") == 0; /* (a Date: its ISO text) */
        for (int i = 0; i < json_capable_depth; i++) if (json_capable_stack[i] == t->cls) return true;
        if (json_capable_depth == 64) return false;
        json_capable_stack[json_capable_depth++] = t->cls;
        bool ok = true;
        for (int i = 0; ok && i < t->cls->nfields; i++) ok = json_capable(t->cls->fields[i].type);
        json_capable_depth--;
        return ok;
    }
    default: return false;
    }
}
/* __reflect_decoder(T) / __reflect_encoder(T): a static closure per type, JSON <-> unknown */
static char **refl_made;
static int nrefl;
static void emit_reflect_codec(Type *t, bool dec) {
    const char *name = xs_sprintf("refl_%s_%s", dec ? "dec" : "enc", t->kind == TY_VOID ? "void" : type_tag(t));
    bool have = false;
    for (int i = 0; i < nrefl; i++) if (strcmp(refl_made[i], name) == 0) have = true;
    if (!have) {
        refl_made = realloc(refl_made, sizeof(char *) * (size_t)(nrefl + 1));
        refl_made[nrefl++] = (char *)name;
        Buf b = {0};
        if (dec) {
            buf_printf(&b, "static void *%s_fn(void *env_, xs_str *json) {\n    (void)env_;\n", name);
            if (t->kind == TY_VOID || !json_capable(t)) {
                buf_printf(&b, "    (void)json;\n    xs_panic(\"TypeError: %s cannot be read from JSON\");\n    return NULL;\n}\n", ty_name(t));
            } else {
                buf_printf(&b, "    xs_jv *jv_ = xs_json_parse(json);\n    %s r_ = ", ctype(t));
                json_emit_read(&b, t, "jv_", "value");
                buf_printf(&b, ";\n    xs_jv_free(jv_);\n    return %s;\n}\n", to_unknown_c(t, "r_"));
            }
        } else {
            buf_printf(&b, "static xs_str *%s_fn(void *env_, void *v) {\n    (void)env_;\n", name);
            if (t->kind == TY_VOID) {
                buf_puts(&b, "    (void)v;\n    return xs_str_new(\"\", 0);\n}\n");
            } else if (!json_capable(t)) {
                buf_printf(&b, "    (void)v;\n    xs_panic(\"TypeError: %s cannot be written as JSON\");\n    return NULL;\n}\n", ty_name(t));
            } else {
                buf_printf(&b, "    %s x_ = %s;\n    xs_sb sb_ = {0}; xs_sb *sb = &sb_;\n    ", ctype(t), from_unknown(t, "v"));
                json_emit_value(&b, t, "x_");
                buf_puts(&b, "\n    return xs_sb_finish(sb);\n}\n");
            }
        }
        buf_printf(&b, "static xs_closure %s_cl = {{XS_IMMORTAL, 0}, (void *)%s_fn};\n", name, name);
        buf_puts(&tramp_fns, b.data);
        free(b.data);
    }
    buf_printf(out, "xs_cl_static(&%s_cl)", name);
}

/* Math.* never calls back into user code and its failing forms take a site: no brackets, so the C compiler
   keeps full freedom in numeric loops */
static bool is_math_call(Expr *e) {
    return e->kind == E_MEMBER && e->lhs->kind == E_IDENT && !e->lhs->cname && e->func == NULL && strcmp(e->lhs->str, "Math") == 0;
}
static bool needs_brackets(Expr *e) {
    if (e == bracketing) return false;
    if (e->kind != E_CALL && e->kind != E_NEW_OBJ && !(e->kind == E_MEMBER && e->is_call && !is_math_call(e))) return false;
    if (e->kind == E_CALL && e->builtin.cfn && (strcmp(e->builtin.cfn, "xs_str_truthy") == 0 || strncmp(e->builtin.cfn, "__reflect_", 10) == 0)) return false; /* not calls */
    /* a user function that cannot fail is never a frame: no label, and the C compiler may inline it */
    if (e->kind == E_CALL && e->func && e->func->body && !e->builtin.cfn && !func_fails(e->func)) return false;
    if (e->kind == E_NEW_OBJ && !e->cls->builtin && !(e->cls->ctor ? func_fails(e->cls->ctor) : class_init_fails(e->cls))) return false;
    if (e->kind == E_MEMBER && e->func && e->func->body && e->cls && !e->cls->builtin && !e->builtin.cfn && !func_fails(e->func)) return false;
    return true;
}
static void emit_expr_raw(Expr *e);
static void emit_expr_(Expr *e);
/* a boolean that is cheap to compute and cannot fail or have an effect: comparisons, !, && and || of
   primitive names, literals and floating-point + - * (integer arithmetic is checked, so it is left out);
   *budget bounds the work evaluated without the short circuit */
static bool flat_prim(Type *t) { return t && (t->kind == TY_NUMBER || t->kind == TY_INT || t->kind == TY_BOOLEAN); }
static bool flat_val(Expr *e, int *budget) {
    if (!e || !e->type || --*budget < 0) return false;
    switch (e->kind) {
    case E_NUMBER: case E_BOOL: return true;
    case E_IDENT: return flat_prim(e->type) && !e->builtin.cfn;
    case E_UNARY:
        if (e->op == TK_NOT && !e->elem_type) return e->type->kind == TY_BOOLEAN && flat_val(e->lhs, budget);
        if (e->op == TK_MINUS) return e->type->kind == TY_NUMBER && flat_val(e->lhs, budget);
        return false;
    case E_BINARY:
        if (e->builtin.cfn || !flat_prim(e->lhs->type) || !flat_prim(e->rhs->type)) return false;
        switch (e->op) {
        case TK_LT: case TK_LE: case TK_GT: case TK_GE: case TK_EQ: case TK_NEQ: break;
        case TK_AND: case TK_OR: if (e->type->kind != TY_BOOLEAN || e->lhs->type->kind != TY_BOOLEAN || e->rhs->type->kind != TY_BOOLEAN) return false; break;
        case TK_PLUS: case TK_MINUS: case TK_STAR:
            if (e->type->kind != TY_NUMBER || e->lhs->type->kind != TY_NUMBER || e->rhs->type->kind != TY_NUMBER) return false;
            break;
        default: return false;
        }
        return flat_val(e->lhs, budget) && flat_val(e->rhs, budget);
    default: return false;
    }
}
static bool flat_bool(Expr *e, int *budget) { return e && e->type && e->type->kind == TY_BOOLEAN && flat_val(e, budget); }
/* arrays and maps of cyclic values, where they are made: their visit lets the collector see through them */
static bool may_nz(Expr *e);
static bool nz_sure(Expr *e) { /* certainly not -0 */
    if (!e || !e->type) return false;
    if (e->type->kind == TY_INT) return !may_nz(e);
    if (e->kind == E_NUMBER) return !(e->num == 0 && signbit(e->num));
    if (e->kind == E_CALL && e->builtin.cfn && strcmp(e->builtin.cfn, "xs_to_number") == 0 && e->args.len == 1) return !may_nz(e->args.items[0]);
    if (e->kind == E_BINARY && e->type->kind == TY_NUMBER && e->op == TK_PLUS) return nz_sure(e->lhs) || nz_sure(e->rhs);
    return false;
}

static bool nz_passes(Expr *e) {
    if (!e || !e->type) return false;
    if (e->kind == E_BINARY && e->type->kind == TY_NUMBER && (e->op == TK_PLUS || e->op == TK_MINUS || e->op == TK_STAR || e->op == TK_SLASH)) return true;
    if (e->kind == E_UNARY && e->op == TK_MINUS && e->type->kind == TY_NUMBER) return true;
    if (e->kind == E_CALL && e->builtin.cfn && strcmp(e->builtin.cfn, "xs_to_number") == 0) return true;
    return false;
}
static void emit_expr_m(Expr *e);
static void emit_expr(Expr *e) {
    if (!nz_mute || nz_passes(e)) { emit_expr_m(e); return; }
    int saved = nz_mute;
    nz_mute = 0;
    emit_expr_m(e);
    nz_mute = saved;
}
static void emit_expr_m(Expr *e) {
    if (e->type && cyc_prog && (e->type->kind == TY_ARRAY || e->type->kind == TY_MAP) &&
        (e->kind == E_ARRAY_LIT || e->kind == E_NEW_MAP || e->kind == E_NEW_ARRAY || (e->kind == E_MEMBER && e->is_call) || e->kind == E_CALL) &&
        arr_or_map_cyclic(e->type)) {
        buf_puts(out, e->type->kind == TY_ARRAY ? "((" : "((");
        buf_printf(out, "%s *)xs_cyc_%s(", e->type->kind == TY_ARRAY ? arr_struct(e->type) : "xs_map", e->type->kind == TY_ARRAY ? "arr" : "map");
        emit_expr_(e);
        buf_puts(out, "))");
        return;
    }
    emit_expr_(e);
}
static void emit_expr_(Expr *e) {
    if (!needs_brackets(e)) { emit_expr_raw(e); return; }
    Func *callee = e->kind == E_NEW_OBJ ? (e->cls->builtin ? NULL : e->cls->ctor) : e->func;
    int id = callee && callee->body && inlined_once(callee) && callee->call1 == e ? call1_site(callee) : new_site(e->pos);
    bool user_call = callee && callee->body && !e->builtin.cfn && e->kind != E_NEW_OBJ && !(e->kind == E_MEMBER && (!e->cls || e->cls->builtin));
    /* (inside a region's code, a callee with a region version calls that version: it has no per-site clone) */
    bool rg_version = rg_hoist_on && cur_rg && callee && callee->rg_hoist == cur_rg && callee->rg_used;
    char *clone = user_call && !e->par_native && !rg_version && cloned_per_site(callee) ? emit_clone(callee, id) : NULL;
    Expr *saved_b = bracketing;
    bool is_void = !e->type || e->type->kind == TY_VOID;
    buf_puts(out, is_void ? "({ " : "({ __auto_type xr_ = ");
    bracketing = e;
    char *cname = callee ? callee->cname : NULL;
    if (clone) callee->cname = clone;
    emit_expr_raw(e);
    if (clone) callee->cname = cname;
    bracketing = saved_b;
    buf_printf(out, is_void ? "; XS_POST(%d); })" : "; XS_POST(%d); xr_; })", id);
}
static void emit_expr_raw(Expr *e) {
    switch (e->kind) {
    case E_NUMBER:
        if (e->type->kind == TY_INT) {
            if (e->ival == (-9223372036854775807LL - 1)) buf_puts(out, "INT64_MIN");
            else buf_printf(out, "INT64_C(%lld)", e->ival);
        }
        else emit_number(e->num);
        return;
    case E_STRING: buf_printf(out, "%s", lit_nm(e->lit_id)); return;
    case E_BOOL: buf_puts(out, e->boolean ? "true" : "false"); return;
    case E_IDENT: buf_puts(out, ren(e->cname)); return;
    case E_FUNC_REF: {
        /* a static immortal closure whose code forwards to the function */
        Func *f = e->func;
        bool have = false;
        for (int i = 0; i < ntramps; i++) if (strcmp(tramp_names[i], f->cname) == 0) have = true;
        if (!have) {
            tramp_names = realloc(tramp_names, sizeof(char *) * (size_t)(ntramps + 1));
            tramp_names[ntramps++] = f->cname;
            Buf *b = &tramp_fns;
            buf_printf(b, "static %s%s%s_tramp(void *env_", ctype(f->ret), ctype(f->ret)[strlen(ctype(f->ret)) - 1] == '*' ? "" : " ", f->cname);
            for (int i = 0; i < f->nparams; i++) buf_printf(b, ", %s%s%s", ctype(f->params[i].type), ctype(f->params[i].type)[strlen(ctype(f->params[i].type)) - 1] == '*' ? "" : " ", f->params[i].cname);
            buf_printf(b, ") { (void)env_; %s%s(", f->ret->kind == TY_VOID ? "" : "return ", f->cname);
            for (int i = 0; i < f->nparams; i++) buf_printf(b, "%s%s", i ? ", " : "", f->params[i].cname);
            buf_printf(b, "); }\nstatic xs_closure %s_cl = {{XS_IMMORTAL, 0}, (void *)%s_tramp};\n", f->cname, f->cname);
        }
        buf_printf(out, "xs_cl_static(&%s_cl)", f->cname);
        return;
    }
    case E_THIS: buf_puts(out, ren(e->cname ? e->cname : "self")); return;
    case E_AWAIT: emit_expr(e->lhs); return;
    case E_SPAWN: emit_spawn(e); return;
    case E_NULL: buf_puts(out, e->type ? null_value(e->type) : "NULL"); return;
    case E_UPCAST: /* a subclass as its base: the same object */
        buf_printf(out, "((%s)(", ctype(e->type));
        emit_expr(e->lhs);
        buf_puts(out, "))");
        return;
    case E_SUPER_CALL: /* the base's initializer on this object */
        buf_printf(out, "%s_init((%s *)self", e->cls->cname, e->cls->cname);
        emit_args_borrowed(&e->args, true);
        buf_puts(out, ")");
        return;
    case E_AS: { /* unknown as T: the object's free function says what it is */
        Buf b = {0};
        Buf *saved = out;
        out = &b;
        emit_expr(e->lhs);
        out = saved;
        if (e->op == 0 && e->lhs->type && e->lhs->type->kind == TY_UNKNOWN && !cref(e->type) && is_fresh(e->lhs)) {
            /* f() as number: unbox the fresh box, then let it go */
            static int nub;
            int id = nub++;
            fu_dyn = ty_is_any(e->lhs->type);
            buf_printf(out, "({ void *ub%d_ = %s; %s uv%d_ = %s; xs_release(ub%d_); uv%d_; })", id, b.data, ctype(e->type), id, from_unknown(e->elem_type, xs_sprintf("ub%d_", id)), id, id);
            fu_dyn = false;
            free(b.data);
            return;
        }
        if (e->op == TK_DOT) { /* a union narrowed to one member: the test already proved it */
            Type *t = e->type;
            Type *lt = e->lhs->type;
            if (t->kind == TY_OBJECT && is_error_class(t->cls) && lt && lt->kind == TY_OBJECT && lt->cls->builtin && strcmp(lt->cls->name, "Error") == 0) {
                buf_printf(out, "((%s)xs_Error_thrown(%s))", ctype(t), b.data); /* catch (e) { if (e instanceof X) e.code } */
                free(b.data);
                return;
            }
            if (t->kind == TY_NUMBER) buf_printf(out, "xs_as_num(%s)", b.data);
            else if (t->kind == TY_INT || (t->kind == TY_ENUM && !t->en->is_string)) buf_printf(out, "xs_as_int(%s)", b.data);
            else if (t->kind == TY_BOOLEAN) buf_printf(out, "xs_as_bool(%s)", b.data);
            else if (null_prim(t) && t->elem->kind == TY_NUMBER) buf_printf(out, "xs_as_num_or_null(%s)", b.data); /* number | null left of a union */
            else if (null_prim(t) && (t->elem->kind == TY_INT || (t->elem->kind == TY_ENUM && !t->elem->en->is_string))) buf_printf(out, "xs_as_int_or_null(%s)", b.data);
            else if (null_prim(t) && t->elem->kind == TY_BOOLEAN) buf_printf(out, "xs_as_bool_or_null(%s)", b.data);
            else buf_printf(out, "((%s)(%s))", ctype(t), b.data);
            free(b.data);
            return;
        }
        if (e->op == TK_NOT && !e->elem_type) { /* x! or (T | null) as T: a TypeError when it is null */
            Type *nt = e->lhs->type;
            buf_printf(out, "({ %s nn_ = %s; if (!%s) xs_panic(\"TypeError: the value is null\"); nn_; })", ctype(nt), b.data, not_null(nt, "nn_"));
            free(b.data);
            return;
        }
        fu_dyn = e->lhs->type && ty_is_any(e->lhs->type);
        buf_puts(out, from_unknown(e->elem_type, b.data));
        fu_dyn = false;
        free(b.data);
        return;
    }
    case E_OBJLIT: {
        Class *c = e->cls;
        if (c->ctor && !c->region && !c->rdual && !c->nohdr) /* (a class with a constructor taking a value of its own type, built
                                                                 from a literal: the object without running it) */
            buf_printf(out, "({ %s *o_ = xs_obj_alloc_t(sizeof(%s), &%s_type);", c->cname, c->cname, c->cname);
        else
        buf_printf(out, "({ %s *o_ = %s_new();", c->cname, c->cname);
        for (int i = 0; i < e->args.len; i++) {
            Field *fd = NULL;
            for (int k = 0; k < c->nfields; k++) if (strcmp(c->fields[k].name, e->keys[i]) == 0) fd = &c->fields[k];
            buf_puts(out, " ");
            if (cref(fd->type)) {
                buf_printf(out, "xs_assign(o_->%s, ", fd->cname);
                emit_owned(e->args.items[i]);
                buf_puts(out, ");");
            } else {
                buf_printf(out, "o_->%s = ", fd->cname);
                emit_expr(e->args.items[i]);
                buf_puts(out, ";");
            }
        }
        buf_puts(out, " o_; })");
        return;
    }
    case E_ARROW: {
        Arrow *a = e->arrow;
        Func *f = a->fn;
        /* env struct + free + body, emitted into the arrow buffer with a fresh emitter state */
        Buf *saved_out = out;
        EScope *saved_scope = escope;
        Func *saved_func = cur_func;
        int saved_indent = indent;
        bool saved_ctor = in_ctor, saved_ar = used_ar;
        Buf body = {0};
        out = &body;
        indent = 0;
        escope = NULL;
        cur_func = f;
        in_ctor = false;
        bool saved_vol = vol_locals;
        int saved_rg = cur_rg;
        cur_rg = 0; /* a closure runs when it runs: generic calls */
        vol_locals = stmt_has_try(f->body);
        buf_printf(out, "typedef struct { xs_closure base;");
        for (int i = 0; i < a->ncaptures; i++) {
            Capture *c = &a->captures[i];
            buf_puts(out, " ");
            if (c->boxed) buf_printf(out, "%s *c_%s", cell_struct(c->type), c->name); /* the shared cell */
            else emit_decl(c->type, c->is_this ? "self" : xs_sprintf("c_%s", c->name));
            buf_puts(out, ";");
        }
        buf_printf(out, " } %s_env;\n", f->cname);
        buf_printf(out, "static void %s_free(void *p) {\n    %s_env *env = p; (void)env;\n", f->cname, f->cname);
        for (int i = 0; i < a->ncaptures; i++) {
            Capture *c = &a->captures[i];
            if (crc(c->type) || c->is_this || c->boxed) buf_printf(out, "    xs_release(env->%s);\n", c->is_this ? "self" : xs_sprintf("c_%s", c->name));
        }
 buf_puts(out, "    xs_obj_free(p);\n}\n");
        bool env_cyc = false; /* a closure that captures something cyclic can be part of a cycle itself */
        for (int i = 0; i < a->ncaptures; i++) {
            Capture *c = &a->captures[i];
            if (c->is_this ? class_cyclic(f->cls ? f->cls : cur_class ? cur_class : NULL) || type_cyclic(c->type) : type_cyclic(c->type)) env_cyc = true;
        }
        if (env_cyc) {
            buf_printf(out, "static void %s_visit(void *p, xs_visit_cb cb, void *arg) {\n    %s_env *env = p; (void)env;\n", f->cname, f->cname);
            for (int i = 0; i < a->ncaptures; i++) {
                Capture *c = &a->captures[i];
                if (cref(c->type) || c->is_this || c->boxed) buf_printf(out, "    cb((void **)&env->%s, arg);\n", c->is_this ? "self" : xs_sprintf("c_%s", c->name));
            }
            buf_puts(out, "}\n");
        }
        buf_printf(out, "static const xs_type XS_TYPE_SEC %s_type = {%s_free, xs_print_closure, %s};\n", f->cname, f->cname, env_cyc ? xs_sprintf("%s_visit", f->cname) : "NULL");
        buf_printf(out, "XS_FN %s%s%s(%s_env *env", ctype(f->ret), ctype(f->ret)[strlen(ctype(f->ret)) - 1] == '*' ? "" : " ", f->cname, f->cname);
        for (int i = 0; i < f->nparams; i++) {
            buf_puts(out, ", ");
            emit_decl(f->params[i].type, f->params[i].cname);
        }
        buf_puts(out, ") {");
        indent++;
        nl();
        buf_puts(out, "(void)env; size_t ar_mark = xs_ar_mark(); (void)ar_mark;");
        emit_dbg_enter(f);
        dbg_params(f, true, a);
        nl();
        emit_block(f->body, true);
        emit_dbg_finish();
        indent--;
        nl();
        buf_puts(out, "}\n");
        if (!a->emitted) buf_puts(&arrow_fns, body.data); /* (once: the same arrow can be reached twice, as in a finally) */
        a->emitted = true;
        free(body.data);
        jam_try(a);
        out = saved_out;
        escope = saved_scope;
        cur_func = saved_func;
        vol_locals = saved_vol;
        cur_rg = saved_rg;
        indent = saved_indent;
        in_ctor = saved_ctor;
        used_ar = saved_ar;
        /* creation: allocate the env and copy the captures (retaining references) */
        buf_printf(out, "({ %s_env *cl_ = xs_obj_alloc_t(sizeof(%s_env), &%s_type); cl_->base.fn = (void *)%s;", f->cname, f->cname, f->cname, f->cname);
        for (int i = 0; i < a->ncaptures; i++) {
            Capture *c = &a->captures[i];
            const char *field = c->is_this ? "self" : xs_sprintf("c_%s", c->name);
            if (crc(c->type) || c->is_this || c->boxed) buf_printf(out, " cl_->%s = xs_retain(%s);", field, c->outer_cname);
            else buf_printf(out, " cl_->%s = %s;", field, c->outer_cname);
        }
        buf_puts(out, " (xs_closure *)cl_; })");
        return;
    }
    case E_NEW_MAP: {
        Type *t = e->type;
        if (t->kind == TY_NULLABLE) t = t->elem; /* (a Record literal where Record | null goes keeps the slot's type) */
        if (t->kind == TY_CHAN) {
            buf_printf(out, "xs_chan_new(%d, ", val_kind(t->elem));
            if (e->args.len) emit_expr(e->args.items[0]); else buf_puts(out, "0");
            buf_puts(out, ")");
            return;
        }
        bool is_map = t->kind == TY_MAP;
        Type *k = is_map ? t->key : t->elem;
        if (!is_map && k->kind == TY_UNKNOWN) { /* new Set<unknown>(xs) */
            if (!e->args.len) { buf_puts(out, "xs_uset_new()"); return; }
            Type *at = e->args.items[0]->type;
            buf_printf(out, "({ xs_map *nm_ = xs_uset_new(); %s *na_ = ", arr_struct(at));
            emit_borrowed(e->args.items[0]);
            buf_puts(out, "; for (size_t ni_ = 0; ni_ < na_->len; ni_++) xs_uset_add(nm_, na_->data[ni_]); nm_; })");
            return;
        }
        if (is_map && t->record) buf_puts(out, "xs_map_rec(");
        if (e->args.len == 1) buf_puts(out, "({ xs_map *nm_ = ");
        buf_printf(out, "xs_map_new(%d, %d, %d, %s, %s)", is_map ? 1 : 0, k->kind == TY_STRING ? 1 : 0, is_map ? val_kind(t->elem) : 1,
                   k->kind == TY_STRING ? "xs_print_str_quoted_" : "xs_print_int_v", is_map ? map_val_printer(t->elem) : "0");
        if (e->args.len == 1) { /* filled from the array, in order (a repeated key keeps the last value, as in JS) */
            Type *at = e->args.items[0]->type;
            const char *ks = k->kind == TY_STRING ? "s" : "i";
            buf_printf(out, "; %s *na_ = ", arr_struct(at));
            emit_borrowed(e->args.items[0]);
            buf_puts(out, "; for (size_t ni_ = 0; ni_ < na_->len; ni_++) { ");
            if (is_map) {
                Class *tc = at->elem->cls;
                buf_printf(out, "%s *ne_ = (%s *)na_->data[ni_]; xs_map_set_%s(nm_, ne_->f__0, ", tc->cname, tc->cname, ks);
                int vk = val_kind(t->elem);
                buf_puts(out, vk == 3 ? "XS_VPTR(xs_retain_(ne_->f__1))" : vk == 0 ? "XS_VNUM(ne_->f__1)" : vk == 1 ? "XS_VINT(ne_->f__1)" : "XS_VBOOL(ne_->f__1)");
                buf_puts(out, "); ");
            } else {
                buf_printf(out, "xs_map_set_%s(nm_, %sna_->data[ni_], XS_VINT(0)); ", ks, k->kind == TY_STRING ? "(xs_str *)" : "");
            }
            buf_puts(out, "} nm_; })");
        }
        if (is_map && t->record) buf_puts(out, ")");
        return;
    }
    case E_ENUM_VAL:
        if (e->en->is_string) buf_printf(out, "%s", lit_nm(e->en->members[e->field].lit_id));
        else buf_printf(out, "INT64_C(%lld)", e->en->members[e->field].ival);
        return;
    case E_TEMPLATE:
        if (tmpl_into && e->args.len <= 3) { /* a string that dies with a stack object: in its buffer (stackalloc.c) */
            buf_printf(out, "xs_cat_into(%s, sizeof %s, %d, (xs_part[]){", tmpl_into, tmpl_into, e->args.len);
            tmpl_into = NULL; /* (not the templates inside its parts) */
        } else
        buf_printf(out, "%s(%d, (xs_part[]){", e->args.len <= 3 ? "xs_cat_n" : "xs_cat", e->args.len); /* short: inlined, kinds known */
        for (int i = 0; i < e->args.len; i++) {
            Expr *part = e->args.items[i];
            if (i) buf_puts(out, ", ");
            if (part->type->kind == TY_NULLABLE) { /* ${x} with x: T | null: null (or undefined for x?: T) when it is */
                Type *pt = part->type, *el = pt->elem;
                Buf b = {0};
                Buf *saved = out;
                out = &b;
                if (cref(el)) emit_borrowed(part); else emit_expr(part);
                out = saved;
                const char *f = el->kind == TY_STRING || (el->kind == TY_ENUM && el->en->is_string) ? "0, .s" : el->kind == TY_NUMBER ? "2, .d" : el->kind == TY_BOOLEAN ? "3, .b" : "1, .i";
                buf_printf(out, "({ %s tn_ = %s; %s ? (xs_part){%s = tn_} : (xs_part){0, .s = %s}; })", ctype(pt), b.data, not_null(pt, "tn_"), f, lit_nm(part->lit_id));
                free(b.data);
                continue;
            }
            switch (part->type->kind) {
            case TY_STRING: emit_cat_part(part); break;
            case TY_INT: buf_puts(out, "{1, .i = "); emit_expr(part); buf_puts(out, "}"); break;
            case TY_NUMBER: buf_puts(out, "{2, .d = "); emit_expr(part); buf_puts(out, "}"); break;
            case TY_BOOLEAN: buf_puts(out, "{3, .b = "); emit_expr(part); buf_puts(out, "}"); break;
            case TY_ENUM:
                if (part->type->en->is_string) { buf_puts(out, "{0, .s = "); emit_expr(part); buf_puts(out, "}"); }
                else { buf_puts(out, "{1, .i = "); emit_expr(part); buf_puts(out, "}"); }
                break;
            default: break;
            }
        }
        buf_puts(out, "})");
        return;
        for (int i = 0; i < e->args.len; i++) {
            Expr *part = e->args.items[i];
            (void)part;
        }
        buf_puts(out, ")");
        return;
    case E_COND: {
        if (e->builtin.cfn && strcmp(e->builtin.cfn, "optchain") == 0) { /* a?.rest: a once in a constant, the rest only when it is not null */
            static int noc;
            int id = noc++;
            Expr *base = e->lhs, *rest = e->args.items[0];
            Type *bt = base->type;
            bool bown = rcnt(bt) && is_fresh(base); /* a new value: released after the rest has read it */
            buf_printf(out, "({ %s %s = ", ctype(bt), e->cname);
            emit_expr(base);
            buf_puts(out, "; ");
            if (e->type->kind == TY_VOID) {
                buf_printf(out, "if (%s) { ", not_null(bt, e->cname));
                emit_expr(rest);
                buf_puts(out, "; }");
                if (bown) buf_printf(out, " xs_release(%s);", e->cname);
                buf_puts(out, " })");
                return;
            }
            bool own = is_fresh(e);
            buf_printf(out, "%s ocr%d_; if (%s) ocr%d_ = ", ctype(e->type), id, not_null(bt, e->cname), id);
            if (own) emit_owned(rest); else emit_expr(rest);
            buf_printf(out, "; else ocr%d_ = %s;", id, null_value(e->type));
            if (bown) buf_printf(out, " xs_release(%s);", e->cname);
            buf_printf(out, " ocr%d_; })", id);
            return;
        }
        if (e->builtin.cfn && (strcmp(e->builtin.cfn, "or_once") == 0 || strcmp(e->builtin.cfn, "qq_once") == 0)) { /* f() || d, f() ?? d: f() once, d only when needed; owned */
            bool qq = strcmp(e->builtin.cfn, "qq_once") == 0; /* ??: only null takes the default ("" and 0 are values) */
            static int nor;
            int id = nor++;
            Type *lt = e->lhs->type;
            bool ref = rcnt(e->type);
            buf_printf(out, "({ %s oo%d_ = ", ctype(lt), id);
            if (ref) emit_owned(e->lhs); else emit_expr(e->lhs);
            const char *v = xs_sprintf("oo%d_", id);
            const char *test = qq ? not_null(lt, v) : ty_is_any(lt) ? xs_sprintf("xs_dyn_truthy(%s)", v) : lt->kind == TY_STRING ? xs_sprintf("xs_str_truthy(%s)", v)
                             : lt->kind == TY_INT ? xs_sprintf("(%s != 0)", v) : lt->kind == TY_NUMBER ? xs_sprintf("(%s != 0 && %s == %s)", v, v, v) /* (0 and NaN take the default) */
                             : lt->kind == TY_BOOLEAN ? v : lt->kind == TY_UNKNOWN ? xs_sprintf("xs_dyn_truthy(%s)", v)
                             : lt->elem && lt->elem->kind == TY_STRING ? xs_sprintf("(%s && xs_str_truthy(%s))", v, v) : not_null(lt, v);
            buf_printf(out, "; %s or%d_; if (%s) or%d_ = oo%d_; else { %sor%d_ = ", ctype(e->type), id, test, id, id, ref ? xs_sprintf("xs_release(oo%d_); ", id) : "", id);
            if (ref) emit_owned(e->args.items[0]); else emit_expr(e->args.items[0]);
            buf_printf(out, "; } or%d_; })", id);
            return;
        }
        bool fresh = is_fresh(e);
        buf_puts(out, "(");
        emit_expr(e->lhs);
        buf_puts(out, " ? ");
        if (fresh) emit_owned(e->args.items[0]); else emit_expr(e->args.items[0]);
        buf_puts(out, " : ");
        if (fresh) emit_owned(e->args.items[1]); else emit_expr(e->args.items[1]);
        buf_puts(out, ")");
        return;
    }
    case E_UNARY:
        if (e->op == TK_IDENT) { /* typeof on an unknown or a nullable primitive */
            if (strcmp(e->builtin.cfn, "xs_typeof") == 0) { buf_puts(out, "xs_typeof("); emit_borrowed(e->lhs); buf_puts(out, ")"); return; /* (typeof g(): the fresh value released) */ }
            Buf b = {0};
            Buf *saved = out;
            out = &b;
            emit_expr(e->lhs);
            out = saved;
            buf_printf(out, "(%s ? xs_typeof_lit(\"%s\") : xs_typeof_lit(\"object\"))", not_null(e->lhs->type, b.data), e->str);
            free(b.data);
            return;
        }
        if (e->op == TK_TILDE) { buf_puts(out, "((int64_t)(int32_t)~"); emit_u32(e->lhs); buf_puts(out, ")"); return; }
        if (e->op == TK_MINUS && e->type->kind == TY_INT) {
            buf_puts(out, "xs_ineg_s(");
            emit_expr(e->lhs);
            site_arg(e->pos);
            buf_puts(out, ")");
            return;
        }
        buf_puts(out, "(");
        buf_puts(out, e->op == TK_NOT ? "!" : e->op == TK_MINUS ? "-" : "");
        emit_expr(e->lhs);
        buf_puts(out, ")");
        return;
    case E_BINARY:
        if (tk_bitop(e->op)) { emit_bitop(e->op, e->lhs, e->rhs); return; }
        if ((e->op == TK_AND || e->op == TK_OR) && e->type->kind == TY_BOOLEAN && !xs_debug) {
            int budget = 64;
            if (flat_bool(e->lhs, &budget) && flat_bool(e->rhs, &budget)) {
                /* both sides cheap and unable to fail: evaluated both, as & or |, so that a chain of
                   comparisons is one value the C compiler can vectorize (mandelbrot's "all eight escaped") */
                buf_puts(out, "(");
                emit_expr(e->lhs);
                buf_puts(out, e->op == TK_AND ? " & " : " | ");
                emit_expr(e->rhs);
                buf_puts(out, ")");
                return;
            }
        }
        if (e->op == TK_STARSTAR) {
            buf_puts(out, e->type->kind == TY_INT ? "xs_ipow_s(" : "pow(");
            emit_expr(e->lhs);
            buf_puts(out, ", ");
            emit_expr(e->rhs);
            if (e->type->kind == TY_INT) site_arg(e->pos);
            buf_puts(out, ")");
            return;
        }
        if (e->type->kind == TY_STRING) { /* concatenation: a whole chain a + b + c in one allocation */
            Expr **leaves = NULL;
            int nl = 0, cap = 0;
            cat_leaves(e, &leaves, &nl, &cap);
            buf_printf(out, "%s(%d, (xs_part[]){", nl <= 3 ? "xs_cat_n" : "xs_cat", nl);
            for (int i = 0; i < nl; i++) {
                if (i) buf_puts(out, ", ");
                emit_cat_part(leaves[i]);
            }
            buf_puts(out, "})");
            free(leaves);
            return;
        }
        if (e->lhs->type->kind == TY_INT && e->type->kind == TY_INT && (e->op == TK_PLUS || e->op == TK_MINUS || e->op == TK_STAR) && rng_of(e).ok) {
            buf_puts(out, "((int64_t)(");   /* cannot overflow (integer ranges above) */
            emit_expr(e->lhs);
            buf_puts(out, e->op == TK_PLUS ? ") + (int64_t)(" : e->op == TK_MINUS ? ") - (int64_t)(" : ") * (int64_t)(");
            emit_expr(e->rhs);
            buf_puts(out, "))");
            return;
        }
        if (e->lhs->type->kind == TY_INT && e->type->kind == TY_INT && e->op == TK_PERCENT && !getenv("XISTO_NO_POW2_MOD")) {
            Rng b = rng_of(e->rhs);
            if (b.ok && b.lo > 0 && pow2_of(e->rhs, 0)) { /* x % 2^n: a mask for x >= 0 (JS keeps the sign of x), no zero to check */
                /* JS keeps the sign of x: r = x & (d - 1), minus d when x < 0 and r != 0 (no division, which the C
                   compiler would otherwise compute on both sides of a select: knucleotide-4 3,4 cpu-s with it) */
                bool nn = nn_expr(e->lhs); /* never negative: the mask alone */
                buf_puts(out, "({ int64_t ma_ = (");
                emit_expr(e->lhs);
                buf_puts(out, "), mb_ = (");
                emit_expr(e->rhs);
                buf_puts(out, nn ? "); ma_ & (mb_ - 1); })" : "), mr_ = ma_ & (mb_ - 1); mr_ - ((ma_ >> 63) & (mr_ != 0 ? mb_ : 0)); })");
                return;
            }
        }
        if (e->lhs->type->kind == TY_INT && (e->op == TK_PLUS || e->op == TK_MINUS || e->op == TK_STAR || e->op == TK_PERCENT || e->op == TK_SLASH)) {
            const char *fn = e->op == TK_PLUS ? "xs_iadd" : e->op == TK_MINUS ? "xs_isub" : e->op == TK_STAR ? "xs_imul"
                           : e->op == TK_PERCENT ? "xs_imod" : "xs_idiv";
            /* both operands never negative (XB-075): a sum or product can only cross the top of the safe range, one
               comparison; a difference of two of them stays inside it, no test at all */
            bool nn2 = (e->op == TK_PLUS || e->op == TK_MINUS || e->op == TK_STAR) && !getenv("XISTO_NO_NONNEG") && rv_valid &&
                       rv_owner == cur_func && !ictx && nn_expr(e->lhs) && nn_expr(e->rhs);
            if (nn2 && e->op == TK_MINUS) {
                buf_puts(out, "((int64_t)(");
                emit_expr(e->lhs);
                buf_puts(out, ") - (int64_t)(");
                emit_expr(e->rhs);
                buf_puts(out, "))");
                return;
            }
            if (e->op == TK_SLASH && !nz_mute && (may_nz(e->lhs) || may_nz(e->rhs))) { /* XB-087: 1 / (x * -3) is -Infinity at x = 0 */
                buf_puts(out, "(");
                emit_nzd(e->lhs);
                buf_puts(out, " / ");
                emit_nzd(e->rhs);
                buf_puts(out, ")");
                return;
            }
            if (nn2) fn = e->op == TK_PLUS ? "xs_iadd_nn" : "xs_imul_nn";
            buf_printf(out, "%s%s(", fn, e->op == TK_SLASH ? "" : "_s");
            emit_expr(e->lhs);
            buf_puts(out, ", ");
            emit_expr(e->rhs);
            if (e->op != TK_SLASH) site_arg(e->pos);
            buf_puts(out, ")");
            return;
        }
        if (e->op == TK_PERCENT) {
            buf_puts(out, "xs_fmod(");
            emit_expr(e->lhs);
            buf_puts(out, ", ");
            emit_expr(e->rhs);
            buf_puts(out, ")");
            return;
        }
        if (e->builtin.cfn && strcmp(e->builtin.cfn, "nullable_eq") == 0) { /* T | null === T */
            Expr *n = e->lhs->type->kind == TY_NULLABLE ? e->lhs : e->rhs, *v = n == e->lhs ? e->rhs : e->lhs;
            bool eq = e->op == TK_EQ;
            if (n->type->elem->kind == TY_STRING || (n->type->elem->kind == TY_ENUM && n->type->elem->en->is_string)) {
                buf_puts(out, eq ? "xs_str_eq_null(" : "!xs_str_eq_null(");
                emit_borrowed(n);
                buf_puts(out, ", ");
                emit_borrowed(v);
                buf_puts(out, ")");
                return;
            }
            /* the sentinel never equals a value it stands in for: a plain comparison */
            buf_puts(out, "(");
            emit_expr(n);
            buf_puts(out, eq ? " == " : " != ");
            emit_expr(v);
            buf_puts(out, ")");
            return;
        }
        if ((e->op == TK_EQ || e->op == TK_NEQ) && e->builtin.cfn && strcmp(e->builtin.cfn, "ref_eq") == 0) { /* the same object */
            buf_puts(out, "((void *)(");
            emit_borrowed(e->lhs);
            buf_printf(out, ") %s (void *)(", e->op == TK_EQ ? "==" : "!=");
            emit_borrowed(e->rhs);
            buf_puts(out, "))");
            return;
        }
        if ((e->op == TK_EQ || e->op == TK_NEQ) && ((union_lit_field(e->lhs) && e->rhs->kind == E_STRING) || (union_lit_field(e->rhs) && e->lhs->kind == E_STRING))) {
            /* s.kind === "rect" on a discriminated union: the object's class, not the string */
            Expr *f = union_lit_field(e->lhs) ? e->lhs : e->rhs, *lit = f == e->lhs ? e->rhs : e->lhs;
            static int nuc;
            int id = nuc++;
            buf_printf(out, "({ void (*ucf%d_)(void *) = xs_type_of((", id);
            emit_borrowed(f->lhs);
            buf_printf(out, "))->free; %s", e->op == TK_NEQ ? "!" : "");
            emit_union_class_test(f->elem_type, f->str, lit->str, xs_sprintf("ucf%d_", id));
            buf_puts(out, "; })");
            return;
        }
        if (e->op == TK_IDENT) { /* instanceof: the same identity test as `as` */
            Class *c = e->rhs->cls;
            Type *lt = e->lhs->type;
            bool l_err = lt->kind == TY_OBJECT && lt->cls->builtin && strcmp(lt->cls->name, "Error") == 0;
            if (lt->kind == TY_OBJECT && is_error_class(lt->cls) && strcmp(c->name, "Error") == 0 && c->builtin) {
                buf_puts(out, "((void *)("); /* an object of a class that extends Error is an Error */
                emit_expr(e->lhs);
                buf_puts(out, ") != NULL)");
                return;
            }
            if (l_err && is_error_class(c)) { /* a caught Error: the class of the object it was thrown as */
                if (in_hierarchy(c)) buf_printf(out, "%s(xs_Error_thrown((void *)(", isa_fn(c));
                else buf_printf(out, "xs_is_a(xs_Error_thrown((void *)(");
                emit_expr(e->lhs);
                if (in_hierarchy(c)) buf_puts(out, ")))");
                else buf_printf(out, ")), %s_free)", c->cname);
                return;
            }
            if (c->ntparams && !c->generic_of) { /* x instanceof Box (generic): an object of any of its instances */
                static int ngi;
                int id = ngi++;
                buf_printf(out, "({ void *gi%d_ = (void *)(", id);
                emit_expr(e->lhs);
                buf_printf(out, "); void (*gf%d_)(void *) = gi%d_ ? xs_type_of(gi%d_)->free : NULL; (false", id, id, id);
                for (int i = 0; i < prog->nclasses; i++)
                    if (prog->classes[i]->generic_of == c && prog->classes[i]->used) buf_printf(out, " || gf%d_ == %s_free", id, prog->classes[i]->cname);
                buf_puts(out, "); })");
                return;
            }
            if (in_hierarchy(c)) {
                buf_printf(out, "%s((void *)(", isa_fn(c));
                emit_expr(e->lhs);
                buf_puts(out, "))");
                return;
            }
            buf_puts(out, "xs_is_a(");
            emit_expr(e->lhs);
            buf_printf(out, ", %s)", strcmp(c->name, "Error") == 0 ? "xs_Error_free" : c->builtin && strcmp(c->name, "Date") == 0 ? "xs_Date_free" :
                       c->builtin && (strcmp(c->name, "Uint8Array") == 0 || strcmp(c->name, "Buffer") == 0) ? "xs_u8_free" :
                       c->builtin ? "NULL" : xs_sprintf("%s_free", c->cname));
            return;
        }
        if ((e->op == TK_EQ || e->op == TK_NEQ) && (null_prim(e->lhs->type) || null_prim(e->rhs->type)) &&
            (e->lhs->type->kind == TY_NULL || e->rhs->type->kind == TY_NULL)) { /* x === null on a sentinel */
            Expr *v = e->lhs->type->kind == TY_NULL ? e->rhs : e->lhs;
            Buf b = {0};
            Buf *saved = out;
            out = &b;
            emit_expr(v);
            out = saved;
            buf_printf(out, "%s%s", e->op == TK_EQ ? "!" : "", not_null(v->type, b.data));
            free(b.data);
            return;
        }
        if ((e->op == TK_EQ || e->op == TK_NEQ) && (e->lhs->type->kind == TY_NULL || e->rhs->type->kind == TY_NULL || e->lhs->type->kind == TY_NULLABLE)) {
            buf_puts(out, "(");
            emit_borrowed(e->lhs);
            buf_printf(out, " %s ", binop(e->op));
            emit_borrowed(e->rhs);
            buf_puts(out, ")");
            return;
        }
        if ((e->op == TK_EQ || e->op == TK_NEQ) && (e->lhs->type->kind == TY_STRING || (e->lhs->type->kind == TY_ENUM && e->lhs->type->en->is_string))) {
            buf_puts(out, e->op == TK_EQ ? "xs_str_eq(" : "!xs_str_eq(");
            emit_borrowed(e->lhs);
            buf_puts(out, ", ");
            emit_borrowed(e->rhs);
            buf_puts(out, ")");
            return;
        }
        if (e->type->kind == TY_NUMBER && (e->op == TK_PLUS || e->op == TK_MINUS)) { /* XB-087: a + y with y never -0 is
                                                                                 never -0, whatever the sign of a zero a */
            int saved = nz_mute;
            buf_puts(out, "(");
            nz_mute = saved || (e->op == TK_PLUS && nz_sure(e->rhs));
            emit_expr(e->lhs);
            buf_printf(out, " %s ", binop(e->op));
            nz_mute = saved || nz_sure(e->lhs);
            emit_expr(e->rhs);
            nz_mute = saved;
            buf_puts(out, ")");
            return;
        }
        buf_puts(out, "(");
        emit_expr(e->lhs);
        buf_printf(out, " %s ", binop(e->op));
        emit_expr(e->rhs);
        buf_puts(out, ")");
        return;
    case E_ASSIGN:
        if (e->lhs->kind == E_INDEX && !e->lhs->builtin.cfn && e->op == TK_ASSIGN && i64reg(e->lhs->lhs)) { /* an int[] in a register: a lane (the value is a byte, u8_int_proof) */
            U8Reg *rg = u8reg_find(e->lhs->lhs);
            bool nc = bce_safe(e->lhs);
            buf_puts(out, "({ int64_t vv_ = ");
            emit_expr(e->rhs);
            buf_puts(out, "; ");
            emit_u8reg_name(e->lhs->lhs);
            buf_puts(out, nc ? " = xs_v16_set_nc(" : " = xs_v16_set(");
            emit_u8reg_name(e->lhs->lhs);
            buf_puts(out, ", ");
            if (nc) emit_bce_index(e->lhs->rhs); else emit_expr(e->lhs->rhs);
            buf_puts(out, ", vv_");
            if (!nc) { buf_printf(out, ", %s", rgn(rg)); site_arg(e->pos); }
            buf_puts(out, "); vv_; })");
            return;
        }
        if (e->lhs->kind == E_INDEX && e->lhs->builtin.cfn && strcmp(e->lhs->builtin.cfn, "xs_u8_get") == 0) { /* b[i] = v, b[i] += v: the low 8 bits */
            bool nc = bce_safe(e->lhs); /* within the bytes: no check */
            U8Reg *rg = u8reg_find(e->lhs->lhs);
            if (rg && e->op == TK_ASSIGN) { /* in a register (u8reg_loop): a lane blend, no byte store */
                buf_puts(out, "({ int64_t vv_ = ");
                emit_expr(e->rhs);
                buf_puts(out, "; ");
                emit_u8reg_name(e->lhs->lhs);
                buf_puts(out, nc ? " = xs_v16_set_nc(" : " = xs_v16_set(");
                emit_u8reg_name(e->lhs->lhs);
                buf_puts(out, ", ");
                if (nc) emit_bce_index(e->lhs->rhs); else emit_expr(e->lhs->rhs);
                buf_puts(out, ", vv_");
                if (!nc) { buf_printf(out, ", %s", rgn(rg)); site_arg(e->pos); }
                buf_puts(out, "); vv_; })");
                return;
            }
            if (e->op == TK_ASSIGN) {
                buf_puts(out, nc ? "xs_u8_set_nc(" : "xs_u8_set(");
                emit_borrowed(e->lhs->lhs);
                buf_puts(out, ", ");
                if (nc) emit_bce_index(e->lhs->rhs); else emit_expr(e->lhs->rhs);
                buf_puts(out, ", ");
                emit_expr(e->rhs);
                if (!nc) site_arg(e->pos);
                buf_puts(out, ")");
                return;
            }
            buf_puts(out, nc ? "({ xs_byte *bp_ = xs_u8_at_nc(" : "({ xs_byte *bp_ = xs_u8_ptr(");
            emit_borrowed(e->lhs->lhs);
            buf_puts(out, ", ");
            if (nc) emit_bce_index(e->lhs->rhs); else { emit_expr(e->lhs->rhs); site_arg(e->pos); }
            if (tk_bitop_of_assign(e->op)) { /* b[i] ^= k */
                Expr cur = {0};
                cur.kind = E_IDENT; cur.type = ty_int; cur.cname = "(int64_t)*bp_";
                buf_puts(out, "); int64_t bv_ = ");
                emit_bitop(tk_bitop_of_assign(e->op), &cur, e->rhs);
                buf_puts(out, "; *bp_ = (xs_byte)bv_; bv_; })");
                return;
            }
            buf_printf(out, "); int64_t bv_ = (int64_t)*bp_ %s (", e->op == TK_PLUS_ASSIGN ? "+" : e->op == TK_MINUS_ASSIGN ? "-" : "*");
            emit_expr(e->rhs);
            buf_puts(out, "); *bp_ = (xs_byte)bv_; bv_; })");
            return;
        }
        if (cref(e->type) && e->op == TK_PLUS_ASSIGN && e->lhs->kind != E_IDENT) {
            /* s[i++] += t, f().name += t: the slot's address is taken once (after the right side, so a right
               side that grows the array cannot leave it stale) and the index/object expressions run once */
            buf_puts(out, "({ xs_str *rhs_ = ");
            emit_borrowed(e->rhs);
            buf_puts(out, "; xs_str **slot_ = (xs_str **)");
            emit_lvalue_ptr(e->lhs);
            buf_puts(out, "; xs_assign(*slot_, xs_str_concat(*slot_, rhs_)); })");
            return;
        }
        if (rcnt(e->type) && e->op == TK_ASSIGN && e->builtin.cfn && strcmp(e->builtin.cfn, "init_store") == 0) {
            /* a constructor's first store to a field without initializer: the slot is still the zero the
               allocation wrote, nothing to release */
            buf_puts(out, "(");
            emit_lvalue(e->lhs);
            buf_puts(out, " = ");
            emit_owned(e->rhs);
            buf_puts(out, ")");
            return;
        }
        if (rcnt(e->type) && e->op == TK_ASSIGN && known_free(e->lhs->type)) { /* the old value's free is known */
            buf_puts(out, "xs_assign_as(");
            emit_lvalue(e->lhs);
            buf_puts(out, ", ");
            emit_owned(e->rhs);
            buf_printf(out, ", %s)", known_free(e->lhs->type));
            return;
        }
        if (rcnt(e->type)) {
            buf_puts(out, "xs_assign(");
            emit_lvalue(e->lhs);
            buf_puts(out, ", ");
            if (e->op == TK_PLUS_ASSIGN) { /* s += t */
                buf_puts(out, "xs_str_concat(");
                emit_lvalue(e->lhs);
                buf_puts(out, ", ");
                emit_borrowed(e->rhs);
                buf_puts(out, ")");
            } else {
                emit_owned(e->rhs);
            }
            buf_puts(out, ")");
            return;
        }
        if (tk_bitop_of_assign(e->op)) { /* x &= m: the slot read and written once */
            Expr cur = {0};
            cur.kind = E_IDENT; cur.type = ty_int; cur.cname = "(*bs_)";
            buf_puts(out, "({ int64_t *bs_ = ");
            emit_lvalue_ptr(e->lhs);
            buf_puts(out, "; *bs_ = ");
            emit_bitop(tk_bitop_of_assign(e->op), &cur, e->rhs);
            buf_puts(out, "; })");
            return;
        }
        if (e->type->kind == TY_INT && e->op == TK_PLUS_ASSIGN && dctr_is(e)) { /* (dctr: the test waits for the loop's end) */
            buf_puts(out, "(");
            emit_lvalue(e->lhs);
            buf_puts(out, " += 1)");
            return;
        }
        if (e->type->kind == TY_INT && e->op != TK_ASSIGN) {
            const char *fn = e->op == TK_PLUS_ASSIGN ? "xs_iadd_to" : e->op == TK_MINUS_ASSIGN ? "xs_isub_to" : "xs_imul_to";
            buf_printf(out, "%s_s(", fn);
            emit_lvalue_ptr(e->lhs);
            buf_puts(out, ", ");
            emit_expr(e->rhs);
            site_arg(e->pos);
            buf_puts(out, ")");
            return;
        }
        buf_puts(out, "(");
        emit_lvalue(e->lhs);
        buf_printf(out, " %s ", binop(e->op));
        emit_expr(e->rhs);
        buf_puts(out, ")");
        return;
    case E_POSTFIX:
        if (e->lhs->kind == E_INDEX && e->lhs->builtin.cfn && strcmp(e->lhs->builtin.cfn, "xs_u8_get") == 0) { /* b[i]++ */
            bool nc = bce_safe(e->lhs); /* within the bytes: no check */
            buf_puts(out, nc ? "({ xs_byte *bp_ = xs_u8_at_nc(" : "({ xs_byte *bp_ = xs_u8_ptr(");
            emit_borrowed(e->lhs->lhs);
            buf_puts(out, ", ");
            if (nc) emit_bce_index(e->lhs->rhs); else { emit_expr(e->lhs->rhs); site_arg(e->pos); }
            buf_printf(out, "); int64_t bv_ = *bp_; *bp_ = (xs_byte)(bv_ %s 1); bv_; })", e->op == TK_PLUSPLUS ? "+" : "-");
            return;
        }
        if (e->type->kind == TY_INT && e->builtin.cfn && strcmp(e->builtin.cfn, "counted_inc") == 0) {
            buf_puts(out, "(");   /* the i++ of `for (...; i < n; i++)`: cannot overflow (see S_FOR) */
            emit_lvalue(e->lhs);
            buf_puts(out, "++)");
            return;
        }
        if (e->type->kind == TY_INT && e->op == TK_PLUSPLUS && e->kind == E_POSTFIX && dctr_is(e)) { /* (dctr: the test waits for the loop's end) */
            buf_puts(out, "(");
            emit_lvalue(e->lhs);
            buf_puts(out, "++)");
            return;
        }
        if (e->type->kind == TY_INT && ctr_inc_free(e)) { /* a counter the loop's bound covers (ctr_scan) */
            buf_puts(out, "((*");
            emit_lvalue_ptr(e->lhs);
            buf_puts(out, ")++)");
            return;
        }
        if (e->type->kind == TY_INT) {
            buf_puts(out, e->op == TK_PLUSPLUS ? "xs_ipostinc_s(" : "xs_ipostdec_s(");
            emit_lvalue_ptr(e->lhs);
            site_arg(e->pos);
            buf_puts(out, ")");
            return;
        }
        buf_puts(out, "(");
        emit_lvalue(e->lhs);
        buf_puts(out, e->op == TK_PLUSPLUS ? "++)" : "--)");
        return;
    case E_CALL:
        if (e->builtin.cfn && !e->lhs && strcmp(e->builtin.cfn, "xs_promise_await_p") == 0 && e->type && ty_is_heap(e->type) && !e->func) {
            /* (await p).length: the awaited object as its type (the runtime gives a void *) */
            static Expr *await_cast;
            if (await_cast != e) {
                Expr *was = await_cast;
                await_cast = e;
                buf_printf(out, "((%s)", ctype(e->type));
                emit_expr(e);
                buf_puts(out, ")");
                await_cast = was;
                return;
            }
        }
        if (!nz_mute && e->builtin.cfn && strcmp(e->builtin.cfn, "xs_to_number") == 0 && e->args.len == 1 && may_nz(e->args.items[0])) { emit_nzd(e->args.items[0]); return; } /* XB-087 */
        if (e->builtin.cfn && strcmp(e->builtin.cfn, "__iface_as") == 0) { /* XB-126 */
            Type *t = e->type->kind == TY_NULLABLE ? e->type->elem : e->type;
            buf_printf(out, "%s(", asif_fn(t->cls, e->type->kind == TY_NULLABLE));
            emit_borrowed(e->args.items[0]);
            buf_puts(out, ")");
            return;
        }
        if (e->builtin.cfn && (strcmp(e->builtin.cfn, "__reflect_decoder") == 0 || strcmp(e->builtin.cfn, "__reflect_encoder") == 0)) {
            emit_reflect_codec(e->elem_type, e->builtin.cfn[10] == 'd');
            return;
        }
        if (e->builtin.cfn && strcmp(e->builtin.cfn, "xs_fetch") == 0 && e->args.len == 2) {
            Expr *init = e->args.items[1];
            Expr *method = NULL, *body = NULL, *headers = NULL, *timeout = NULL, *abort = NULL;
            for (int i = 0; i < init->args.len; i++) {
                const char *k = init->keys[i];
                if (strcmp(k, "method") == 0) method = init->args.items[i];
                else if (strcmp(k, "body") == 0) body = init->args.items[i];
                else if (strcmp(k, "headers") == 0) headers = init->args.items[i];
                else if (init->args.items[i]->kind == E_MEMBER && !init->args.items[i]->is_call) abort = init->args.items[i]; /* signal.__flag */
                else timeout = init->args.items[i]->args.items[0];
            }
            if (headers && headers->kind != E_OBJLIT) { /* headers from a Record<string, string> */
                buf_puts(out, "xs_fetch_hmap(");
                emit_borrowed(e->args.items[0]);
                buf_puts(out, ", ");
                if (method) emit_borrowed(method); else buf_puts(out, "NULL");
                buf_puts(out, ", ");
                if (body) emit_borrowed(body); else buf_puts(out, "NULL");
                buf_puts(out, ", ");
                emit_borrowed(headers);
                buf_puts(out, ", ");
                if (timeout) emit_expr(timeout); else buf_puts(out, "-1");
                buf_puts(out, ", ");
                if (abort) emit_borrowed(abort); else buf_puts(out, "NULL");
                buf_puts(out, ")");
                return;
            }
            buf_puts(out, "xs_fetch_full(");
            emit_borrowed(e->args.items[0]);
            buf_puts(out, ", ");
            if (method) emit_borrowed(method); else buf_puts(out, "NULL");
            buf_puts(out, ", ");
            if (body) emit_borrowed(body); else buf_puts(out, "NULL");
            buf_printf(out, ", %d, (xs_str *[]){", headers ? headers->args.len * 2 : 0);
            if (!headers || headers->args.len == 0) buf_puts(out, "NULL");
            for (int h = 0; headers && h < headers->args.len; h++) {
                int id = -1;
                for (int k = 0; k < prog->nlits; k++) if (strcmp(prog->lits[k], headers->keys[h]) == 0) id = k;
                buf_printf(out, "%s%s, ", h ? ", " : "", lit_nm(id));
                emit_borrowed(headers->args.items[h]);
            }
            buf_puts(out, "}, ");
            if (timeout) emit_expr(timeout); else buf_puts(out, "-1");
            buf_puts(out, ", ");
            if (abort) emit_borrowed(abort); else buf_puts(out, "NULL");
            buf_puts(out, ")");
            return;
        }
        if (e->opt_call && e->lhs && e->lhs->type && e->lhs->type->kind == TY_NULLABLE && e->lhs->type->elem->kind == TY_FUNC) {
            /* x.f?.(args): the function once; called when it is not null (otherwise the value is null) */
            static int noc;
            int id = noc++;
            Type *ft = e->lhs->type->elem;
            bool v = e->type->kind == TY_VOID;
            buf_printf(out, "({ xs_closure *oc%d_ = (xs_closure *)", id);
            emit_borrowed(e->lhs);
            buf_puts(out, "; ");
            if (!v) buf_printf(out, "%s ocr%d_ = %s; ", ctype(e->type), id, null_value(e->type));
            buf_printf(out, "if (oc%d_) ", id);
            if (!v) buf_printf(out, "ocr%d_ = ", id);
            emit_closure_call_prefix(ft);
            buf_printf(out, "oc%d_->fn)(oc%d_", id, id);
            emit_args_borrowed(&e->args, true);
            buf_puts(out, ");");
            if (!v) buf_printf(out, " ocr%d_;", id);
            buf_puts(out, " })");
            return;
        }
        if (e->lhs && e->lhs->type && e->lhs->type->kind == TY_FUNC && !e->func && !e->builtin.cfn &&
            e->lhs->kind != E_IDENT && e->lhs->kind != E_THIS && !(e->lhs->kind == E_MEMBER && !e->lhs->is_call)) {
            /* (() => ...)(), f(a)(b): the function value once, in a temporary */
            static int ncc;
            int id = ncc++;
            buf_printf(out, "({ xs_closure *cc%d_ = (xs_closure *)", id);
            emit_borrowed(e->lhs);
            buf_puts(out, "; ");
            emit_closure_call_prefix(e->lhs->type);
            buf_printf(out, "cc%d_->fn)(cc%d_", id, id);
            emit_args_borrowed(&e->args, true);
            buf_puts(out, "); })");
            return;
        }
        if (e->lhs && e->lhs->type && e->lhs->type->kind == TY_FUNC && !e->func && !e->builtin.cfn) { /* calling a closure value */
            emit_closure_call_prefix(e->lhs->type);
            buf_puts(out, "((xs_closure *)");
            emit_borrowed(e->lhs);
            buf_puts(out, ")->fn)(");
            emit_borrowed(e->lhs);
            emit_args_borrowed(&e->args, true);
            buf_puts(out, ")");
            return;
        }
        if (e->par_native) buf_printf(out, "%s_par(", e->func->cname); /* xisto:parallel on the pool */
        else buf_printf(out, "%s(", e->builtin.cfn ? e->builtin.cfn : rg_call_name(e->func));
        emit_args_borrowed(&e->args, false);
        buf_puts(out, ")");
        return;
    case E_INDEX:
        if (!e->builtin.cfn && i64reg(e->lhs)) { /* an int[] in a register for this loop (u8reg_loop): a lane */
            U8Reg *rg = u8reg_find(e->lhs);
            bool nc = bce_safe(e);
            buf_puts(out, nc ? "xs_v16_get_nc(" : "xs_v16_get(");
            emit_u8reg_name(e->lhs);
            buf_puts(out, ", ");
            if (nc) emit_bce_index(e->rhs); else emit_expr(e->rhs);
            if (!nc) { buf_printf(out, ", %s", rgn(rg)); site_arg(e->pos); }
            buf_puts(out, ")");
            return;
        }
        if (e->builtin.cfn) { /* m[i] on a RegExp match (the group's text, borrowed), b[i] on bytes */
            U8Reg *rg = strcmp(e->builtin.cfn, "xs_u8_get") == 0 ? u8reg_find(e->lhs) : NULL;
            if (rg) { /* the array is in a register for this loop (u8reg_loop) */
                bool nc = bce_safe(e);
                buf_puts(out, nc ? "xs_v16_get_nc(" : "xs_v16_get(");
                emit_u8reg_name(e->lhs);
                buf_puts(out, ", ");
                if (nc) emit_bce_index(e->rhs); else emit_expr(e->rhs);
                if (!nc) { buf_printf(out, ", %s", rgn(rg)); site_arg(e->pos); }
                buf_puts(out, ")");
                return;
            }
            if (strcmp(e->builtin.cfn, "xs_u8_get") == 0 && bce_safe(e) && byte_hoist_find(e->lhs) >= 0) { /* the loop's local data pointer */
                buf_printf(out, "((int64_t)bh%d_[", byte_hoist_find(e->lhs));
                emit_bce_index(e->rhs);
                buf_puts(out, "])");
                return;
            }
            if (strcmp(e->builtin.cfn, "xs_u8_get") == 0 && bce_safe(e)) { /* within the bytes: see bce_safe */
                buf_puts(out, "((int64_t)*xs_u8_at_nc(");
                emit_borrowed(e->lhs);
                buf_puts(out, ", ");
                emit_bce_index(e->rhs);
                buf_puts(out, "))");
                return;
            }
            buf_printf(out, "%s(", e->builtin.cfn);
            emit_borrowed(e->lhs);
            buf_puts(out, ", ");
            emit_expr(e->rhs);
            if (strcmp(e->builtin.cfn, "xs_u8_get") == 0) site_arg(e->pos);
            buf_puts(out, ")");
            return;
        }
        emit_lvalue(e);
        return;
    case E_SEQ: /* a for loop's updates, in order (C's comma operator) */
        buf_puts(out, "(");
        for (int i = 0; i < e->args.len; i++) {
            if (i) buf_puts(out, ", ");
            bool val = i == e->args.len - 1 && e->type && e->type->kind != TY_VOID; /* (const r = <void call>: then null) */
            buf_puts(out, val ? "(" : "(void)(");
            emit_expr(e->args.items[i]);
            buf_puts(out, ")");
        }
        buf_puts(out, ")");
        return;
    case E_REGEX: { /* a new RegExp per evaluation, the compiled pattern shared (per thread) */
        buf_puts(out, "xs_RegExp_lit(({ static XS_TLS xr_re *rx_; &rx_; }), ");
        emit_c_string(e->str);
        buf_printf(out, ", %zu, ", strlen(e->str));
        emit_c_string(e->rflags);
        buf_puts(out, ")");
        return;
    }
    case E_MEMBER:
        emit_member(e);
        return;
    case E_NEW_OBJ:
        if (e->cls->builtin) {
            buf_printf(out, "%s(", e->builtin.cfn);
            emit_args_borrowed(&e->args, false);
        } else {
            if (rg_inline_new(e)) return;
            buf_printf(out, "%s_new(", e->cls->cname);
            Func *ct = e->cls->ctor;
            for (int i = 0; i < e->args.len; i++) { /* a consumed parameter takes an owned reference: a new value moves in */
                if (i) buf_puts(out, ", ");
                if (ct && i < ct->nparams && ct->params[i].consumed) emit_owned(e->args.items[i]);
                else emit_borrowed(e->args.items[i]);
            }
        }
        buf_puts(out, ")");
        return;
    case E_NEW_ARRAY:
        return; /* unreachable: the checker rejects it */
    case E_ARRAY_LIT:
        if (e->spread) { emit_spread_lit(e); return; }
        if (arr_is_ref(e->type)) { /* xs_arr_ref_lit(printer, n, owned...) */
            Type *et = e->type->elem && e->type->elem->kind == TY_NULLABLE ? e->type->elem->elem : e->type->elem;
            (void)et;
            if (e->region && e->args.len == 0) { /* [] of region objects, made during an iteration (region.c) */
                buf_printf(out, "xs_arr_ref_rnew(&xs_rg_%d, %s)", e->region, elem_printer(e->type->elem));
                return;
            }
            if (e->region) /* an array of region objects made during an iteration lives in the region too */
                buf_printf(out, "xs_arr_ref_rlit(&xs_rg_%d, %s, %d", e->region, elem_printer(e->type->elem), e->args.len);
            {
            int last_call = -1; /* [new P(), f()]: when a later element throws, the ones made before it are held by the arena
                                   (freed by the catch), and the array takes them once every element is there */
            for (int i = 0; i < e->args.len && !e->region; i++) if (lit_may_throw(e->args.items[i])) last_call = i;
            bool held = false;
            static int nlt;
            int base = nlt;
            for (int i = 0; i < last_call && !held; i++) held = is_fresh(e->args.items[i]) || (rcnt(e->args.items[i]->type) && e->args.items[i]->kind != E_NULL);
            for (int i = 0; held && i <= last_call; i++) { /* (in order, each into its own temporary: C does not order arguments) */
                if (i == 0) buf_puts(out, "({ ");
                Expr *x = e->args.items[i];
                if (i < last_call && is_fresh(x)) {
                    buf_printf(out, "__auto_type lt%d_ = xs_ar(", base + i);
                    emit_owned(x);
                    buf_puts(out, "); ");
                } else if (i < last_call && rcnt(x->type) && x->kind != E_NULL) { /* (borrowed now, retained by the array below) */
                    buf_printf(out, "__auto_type lt%d_ = ", base + i);
                    emit_expr(x);
                    buf_puts(out, "; ");
                } else {
                    buf_printf(out, "__auto_type lt%d_ = ", base + i);
                    emit_owned(e->args.items[i]);
                    buf_puts(out, "; ");
                }
            }
            if (held) nlt += e->args.len;
            if (!e->region) buf_printf(out, "xs_arr_ref_lit(%s, %d", elem_printer(e->type->elem), e->args.len);
            for (int i = 0; i < e->args.len; i++) {
                buf_puts(out, ", ");
                if (held && i < last_call && (is_fresh(e->args.items[i]) || (rcnt(e->args.items[i]->type) && e->args.items[i]->kind != E_NULL)))
                    buf_printf(out, "xs_retain(lt%d_)", base + i);
                else if (held && i <= last_call) buf_printf(out, "lt%d_", base + i);
                else emit_owned(e->args.items[i]);
            }
            buf_puts(out, ")");
            if (held) buf_puts(out, "; })");
            return;
            }
        }
        buf_printf(out, "%s_lit(%d, (%s[]){", arr_struct(e->type), e->args.len,
                   arr_ek(e->type->elem)->kind == TY_NUMBER ? "double" : int_like(arr_ek(e->type->elem)) ? "int64_t" : "bool");
        if (e->args.len == 0) buf_puts(out, "0");
        for (int i = 0; i < e->args.len; i++) {
            if (i) buf_puts(out, ", ");
            emit_expr(e->args.items[i]);
        }
        buf_puts(out, "})");
        return;
    }
}

/* ---------- statements ---------- */

static void emit_stmt(Stmt *s);
static void emit_prof_leave(void);

static void emit_dbg_hook(Stmt *s) {
    if (xs_debug && s->kind != S_BLOCK) buf_printf(out, "XS_DBG(%d); ", dbg_point(s->pos));
}

/* ---- debugger: variables. Each function gets a table {name, type, live points} and an array of slots that
   receive &local when the local is declared; the agent shows a local only while the frame's point is in its
   range, so it never reads a dead stack slot. The table is known once the body is emitted: it is inserted at
   the start of the body then. ---- */
typedef struct { const char *name; Type *t; int from, to; void *scope; int mode; } DbgVar; /* mode: 0 borrowed, 1 owned, 2 read-only */
typedef struct DbgFn { DbgVar *vars; int n; Buf *buf; size_t at; Func *f; struct DbgFn *up; Buf init; } DbgFn;
static DbgFn *dbgfn;
static Buf dbg_decls, dbg_fns;   /* type descriptors: forward declarations, then definitions */
static char **dbg_made;
static int ndbg_made;
static const char *dbg_type(Type *t);

static void emit_dbg_enter(Func *f) {
    if (!xs_debug) return;
    DbgFn *d = xs_alloc(sizeof *d);
    d->buf = out;
    d->at = out->len;
    d->f = f;
    d->up = dbgfn;
    dbgfn = d;
}
/* a local whose address is taken now (the declaration was just emitted); live from the next statement */
static int dbg_local_at(const char *name, Type *t, const char *addr, Buf *where, int mode) {
    if (!xs_debug || !dbgfn || !t || !name) return -1;
    DbgFn *d = dbgfn;
    d->vars = realloc(d->vars, sizeof(DbgVar) * (size_t)(d->n + 1));
    d->vars[d->n] = (DbgVar){name, t, ndbg, -1, escope, mode};
    buf_printf(where, " dbgs_[%d] = (void *)&%s;", d->n, addr);
    return d->n++;
}
static int dbg_local(const char *name, Type *t, const char *addr, bool is_const) { return dbg_local_at(name, t, addr, out, is_const ? 2 : 1); }
static void dbg_close(int k) { if (k >= 0 && dbgfn) dbgfn->vars[k].to = ndbg - 1; }
/* the C scope s ends: its locals stop being visible */
static void dbg_scope_end(void *sc) {
    if (!xs_debug || !dbgfn) return;
    for (int i = 0; i < dbgfn->n; i++) if (dbgfn->vars[i].scope == sc && dbgfn->vars[i].to < 0) dbgfn->vars[i].to = ndbg - 1;
}
/* parameters, this and captures: in scope for the whole body (registered before it is emitted) */
static void dbg_params(Func *f, bool arrow_env, Arrow *a) {
    if (!xs_debug || !dbgfn) return;
    if (f && f->cls) dbg_local_at("this", ty_object_of(f->cls), "self", &dbgfn->init, 2);
    if (arrow_env && a)
        for (int i = 0; i < a->ncaptures; i++) {
            Capture *c = &a->captures[i];
            dbg_local_at(c->is_this ? "this" : c->name, c->type, c->is_this ? "env->self" : xs_sprintf(c->boxed ? "env->c_%s->v" : "env->c_%s", c->name), &dbgfn->init, c->boxed ? 1 : 2); /* a copy is read-only; a cell is shared */
        }
    for (int i = 0; f && i < f->nparams; i++) dbg_local_at(f->params[i].name, f->params[i].type, f->params[i].cname, &dbgfn->init, 0); /* borrowed */
    for (int i = 0; i < dbgfn->n; i++) { dbgfn->vars[i].from = ndbg; dbgfn->vars[i].scope = NULL; }
}
static void buf_insert(Buf *b, size_t at, const char *text) {
    size_t n = strlen(text);
    if (!n) return;
    buf_puts(b, text); /* grow */
    memmove(b->data + at + n, b->data + at, b->len - n - at);
    memcpy(b->data + at, text, n);
}
static void emit_dbg_finish(void) {
    if (!xs_debug || !dbgfn) return;
    DbgFn *d = dbgfn;
    Buf h = {0};
    buf_printf(&h, "\n    XS_DBG_ENTER(\"%s\");", dbg_func_name(d->f));
    if (d->n) {
        buf_puts(&h, " static const xs_dbg_var dbgv_[] = {");
        for (int i = 0; i < d->n; i++) {
            DbgVar *v = &d->vars[i];
            if (v->to < 0) v->to = ndbg - 1;
            buf_printf(&h, "%s{\"%s\", %s, %d, %d, %d}", i ? ", " : "", v->name, dbg_type(v->t), v->from, v->to, v->mode);
        }
        buf_printf(&h, "}; void *dbgs_[%d] = {0}; dbgf_.vars = dbgv_; dbgf_.slots = dbgs_; dbgf_.nvars = %d;", d->n, d->n);
        if (d->init.data) buf_puts(&h, d->init.data);
    }
    buf_insert(d->buf, d->at, h.data);
    free(h.data);
    free(d->init.data);
    free(d->vars);
    dbgfn = d->up;
    free(d);
}

/* a name for the type, unique per shape, for the descriptor */
static char *dbg_tag(Type *t) {
    switch (t->kind) {
    case TY_NUMBER: return "num";
    case TY_INT: case TY_LITINT: return "int";
    case TY_BOOLEAN: return "bool";
    case TY_STRING: return "str";
    case TY_JSON: return "json";
    case TY_NULLABLE: return xs_sprintf("null_%s", dbg_tag(t->elem));
    case TY_ENUM: return xs_sprintf("en_%s", t->en->name);
    case TY_ARRAY: return xs_sprintf("arr_%s", dbg_tag(t->elem));
    case TY_OBJECT: return xs_sprintf("obj_%s", t->cls->cname);
    case TY_MAP: return xs_sprintf("map_%s_%s", dbg_tag(t->key), dbg_tag(t->elem));
    case TY_SET: return xs_sprintf("set_%s", dbg_tag(t->elem));
    case TY_FUNC: return "fn";
    case TY_STREAM: return "stream";
    case TY_CHAN: return "chan";
    case TY_PROMISE: return "promise";
    default: return "other";
    }
}
static void dbg_quote(Buf *b, const char *s) {
    buf_putc(b, '"');
    for (; *s; s++) { if (*s == '"' || *s == '\\') buf_putc(b, '\\'); buf_putc(b, *s); }
    buf_putc(b, '"');
}
/* "&dbgt_TAG": how the debugger shows (and expands) a value of type t; generated once per type */
static const char *dbg_type(Type *t) {
    char *tag = dbg_tag(t);
    for (int i = 0; i < ndbg_made; i++) if (strcmp(dbg_made[i], tag) == 0) return xs_sprintf("&dbgt_%s", tag);
    dbg_made = realloc(dbg_made, sizeof(char *) * (size_t)(ndbg_made + 1));
    dbg_made[ndbg_made++] = tag;
    buf_printf(&dbg_decls, "static const xs_dbg_type dbgt_%s;\n", tag);
    const char *ct = ctype(t);
    Buf b = {0};
    /* render: what console.log would print, strings quoted (as Node shows them inside objects) */
    buf_printf(&b, "static void dbgr_%s(void *p) { ", tag);
    Type *base = t->kind == TY_NULLABLE ? t->elem : t;
    if (t->kind == TY_NULLABLE && null_prim(t)) {
        buf_printf(&b, "%s v = *(%s *)p; if (%s) %s(v); else fputs(\"null\", XS_OUT);", ct, ct, not_null(t, "v"), printer_for(t->elem));
    } else {
        switch (base->kind) {
        case TY_NUMBER: case TY_INT: case TY_LITINT: case TY_BOOLEAN:
            buf_printf(&b, "%s(*(%s *)p);", printer_for(base), ct);
            break;
        case TY_ENUM:
            if (base->en->is_string) buf_puts(&b, "void *v = *(void **)p; if (v) xs_print_str_quoted_(v); else fputs(\"null\", XS_OUT);");
            else buf_printf(&b, "xs_print_int(*(%s *)p);", ct);
            break;
        case TY_STRING: case TY_JSON:
            buf_puts(&b, "void *v = *(void **)p; if (v) xs_print_str_quoted_(v); else fputs(\"null\", XS_OUT);");
            break;
        case TY_ARRAY: case TY_OBJECT: case TY_MAP: case TY_SET: case TY_STREAM: case TY_CHAN:
            if (base->kind == TY_OBJECT && base->cls->builtin && strcmp(base->cls->name, "Error") != 0) {
                buf_printf(&b, "fputs(*(void **)p ? \"%s {}\" : \"null\", XS_OUT);", base->cls->name);
                break;
            }
            buf_printf(&b, "void *v = *(void **)p; if (v) %s(v); else fputs(\"null\", XS_OUT);", printer_for(base));
            break;
        case TY_FUNC: buf_puts(&b, "fputs(\"[Function]\", XS_OUT);"); break;
        default: buf_puts(&b, "fputs(*(void **)p ? \"[object]\" : \"null\", XS_OUT);"); break;
        }
    }
    buf_puts(&b, " }\n");
    /* children: the fields of an object, the elements of an array (a nullable one shares them) */
    const char *count = "NULL", *child = "NULL";
    if (base->kind == TY_OBJECT && !base->cls->builtin && base->cls->nfields > 0) {
        Class *c = base->cls;
        Buf k = {0};
        for (int i = 0; i < c->nfields; i++) {
            const char *fd = dbg_type(c->fields[i].type); /* may generate other descriptors */
            buf_printf(&k, "    case %d: ch->name = ", i);
            dbg_quote(&k, c->fields[i].name);
            buf_printf(&k, "; ch->type = %s; ch->addr = &o->%s; break;\n", fd, c->fields[i].cname);
        }
        buf_printf(&b, "static int dbgn_%s(void *p) { return *(void **)p ? %d : 0; }\n", tag, c->nfields);
        buf_printf(&b, "static void dbgk_%s(void *p, int i, xs_dbg_child *ch) {\n    %s *o = *(%s **)p;\n    switch (i) {\n%s    }\n}\n", tag, c->cname, c->cname, k.data);
        free(k.data);
        count = xs_sprintf("dbgn_%s", tag);
        child = xs_sprintf("dbgk_%s", tag);
    } else if (base->kind == TY_OBJECT && base->cls->builtin && strcmp(base->cls->name, "Error") == 0) {
        const char *sd = dbg_type(ty_string);
        buf_printf(&b, "static int dbgn_%s(void *p) { return *(void **)p ? 2 : 0; }\n", tag);
        buf_printf(&b, "static void dbgk_%s(void *p, int i, xs_dbg_child *ch) { xs_Error *o = *(xs_Error **)p; ch->type = %s; "
                       "if (i == 0) { ch->name = \"name\"; ch->addr = &o->f_name; } else { ch->name = \"message\"; ch->addr = &o->f_message; } }\n", tag, sd);
        count = xs_sprintf("dbgn_%s", tag);
        child = xs_sprintf("dbgk_%s", tag);
    } else if (base->kind == TY_ARRAY) {
        const char *ed = dbg_type(base->elem);
        const char *as = arr_struct(base);
        buf_printf(&b, "static int dbgn_%s(void *p) { %s *a = *(%s **)p; return a ? (a->len > 100000 ? 100000 : (int)a->len) : 0; }\n", tag, as, as);
        buf_printf(&b, "static void dbgk_%s(void *p, int i, xs_dbg_child *ch) { %s *a = *(%s **)p; ch->name = NULL; ch->index = i; ch->type = %s; ch->addr = &a->data[i]; }\n", tag, as, as, ed);
        count = xs_sprintf("dbgn_%s", tag);
        child = xs_sprintf("dbgk_%s", tag);
    }
    /* set: primitives, strings, enums and null for T | null of those; objects, arrays and maps are not editable
       (replacing one would bypass ownership) */
    const char *setter = "NULL";
    {
        Buf w = {0};
        bool ok = true;
        bool nullable = t->kind == TY_NULLABLE;
        Type *e = base;
        if (nullable) {
            buf_puts(&w, "if (xs_dbg_is_null(text)) { ");
            if (null_prim(t)) {
                if (e->kind == TY_NUMBER) buf_printf(&w, "*(%s *)p = xs_null_num(); ", ct);
                else if (e->kind == TY_BOOLEAN) buf_printf(&w, "*(%s *)p = XS_NULL_BOOL; ", ct);
                else buf_printf(&w, "*(%s *)p = XS_NULL_INT; ", ct);
            } else if (e->kind == TY_STRING || (e->kind == TY_ENUM && e->en->is_string)) buf_puts(&w, "xs_dbg_str_store(p, NULL, mode); ");
            else ok = false;
            buf_puts(&w, "return NULL; } ");
        }
        switch (e->kind) {
        case TY_INT: case TY_LITINT:
            buf_printf(&w, "int64_t v; const char *er = xs_dbg_parse_int(text, &v); if (er) return er; %s*(%s *)p = v; return NULL;", nullable ? "if (v == XS_NULL_INT) return \"out of range\"; " : "", ct);
            break;
        case TY_NUMBER:
            buf_printf(&w, "double v; const char *er = xs_dbg_parse_num(text, &v); if (er) return er; *(%s *)p = v; return NULL;", ct);
            break;
        case TY_BOOLEAN:
            buf_printf(&w, "bool v; const char *er = xs_dbg_parse_bool(text, &v); if (er) return er; *(%s *)p = v; return NULL;", ct);
            break;
        case TY_STRING:
            buf_puts(&w, "char *sv; size_t n; const char *er = xs_dbg_parse_str(text, &sv, &n); if (er) return er; xs_dbg_str_store(p, xs_str_new(sv, n), mode); free(sv); return NULL;");
            break;
        case TY_ENUM: {
            Enum *en = e->en;
            if (en->is_string) {
                buf_puts(&w, "char *sv; size_t n; const char *er = xs_dbg_parse_str(text, &sv, &n); if (er) return er; static const char *const m[] = {");
                for (int i = 0; i < en->nmembers; i++) { if (i) buf_puts(&w, ", "); dbg_quote(&w, en->members[i].sval); }
                buf_printf(&w, "}; for (int i = 0; i < %d; i++) if (strcmp(m[i], sv) == 0) { xs_dbg_str_store(p, xs_str_new(sv, n), mode); free(sv); return NULL; } free(sv); return \"not a member of %s\";", en->nmembers, en->name);
            } else {
                buf_puts(&w, "int64_t v; const char *er = xs_dbg_parse_int(text, &v); if (er) return er; static const int64_t m[] = {");
                for (int i = 0; i < en->nmembers; i++) buf_printf(&w, "%s%lldLL", i ? ", " : "", en->members[i].ival);
                buf_printf(&w, "}; for (int i = 0; i < %d; i++) if (m[i] == v) { *(%s *)p = v; return NULL; } return \"not a member of %s\";", en->nmembers, ct, en->name);
            }
            break;
        }
        default: ok = false; break;
        }
        if (ok) {
            buf_printf(&b, "static const char *dbgw_%s(void *p, const char *text, int mode) { (void)mode; %s }\n", tag, w.data);
            setter = xs_sprintf("dbgw_%s", tag);
        }
        free(w.data);
    }
    buf_printf(&b, "static const xs_dbg_type dbgt_%s = { ", tag);
    dbg_quote(&b, ty_name(t));
    buf_printf(&b, ", dbgr_%s, %s, %s, %s };\n", tag, count, child, setter);
    buf_puts(&dbg_fns, b.data);
    free(b.data);
    return xs_sprintf("&dbgt_%s", tag);
}

static int region_reset_pending; /* a region loop's body: rewinds the region before each iteration */
static bool *region_dual;        /* per region: it has dual classes (xs_rg_on_N says where they are made) */
/* ---- does the int local x hold a[lo] (lo a literal) right before statement `target`? ----
   A forward walk of the function body: `x = a[lo]` syncs; any other write to x, a write to a's lane lo (a store
   at an index that is not a literal other than lo, an idiom whose range reaches lo), or x captured by an arrow,
   loses it. Branches meet (both must be synced), loops run twice (the body's end and its continues feed the head,
   breaks feed the exit). Conservative: anything not understood is "not known". */
static Expr *rot_a; static int64_t rot_lo; static const char *rot_x; static Stmt *rot_target; static int rot_found, rot_state_at;
static const char *rot_y; /* state 2: a[lo] was stored from the local rot_y (unchanged since), so x = rot_y syncs */
#define ROT_TOP 3          /* no path has reached the point yet (the meet's identity) */
static int rot_meet(int a, int b) { return a == ROT_TOP ? b : b == ROT_TOP ? a : a == b ? a : 0; }
/* the least value of a local int that is only declared with int literals and only grown (+= a literal >= 0, ++):
   INT64_MIN when it is written any other way */
static int64_t rot_cmin_v;
static bool rot_cmin_ok;
static void rot_cmin_e(Expr *e, const char *n) {
    if (!e || !rot_cmin_ok) return;
    if (e->kind == E_ARROW) { if (e->arrow && e->arrow->fn && stmt_mentions_name(e->arrow->fn->body, n)) rot_cmin_ok = false; return; }
    if ((e->kind == E_ASSIGN || e->kind == E_POSTFIX || e->kind == E_UNARY) && e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname && strcmp(e->lhs->cname, n) == 0) {
        bool grow = (e->kind == E_ASSIGN && e->op == TK_PLUS_ASSIGN && e->rhs && e->rhs->kind == E_NUMBER && e->rhs->is_int_lit && e->rhs->ival >= 0) ||
                    (e->kind == E_POSTFIX && e->op == TK_PLUSPLUS) || (e->kind == E_UNARY && e->op == TK_PLUSPLUS);
        if (e->kind == E_UNARY && e->op != TK_PLUSPLUS && e->op != TK_MINUSMINUS) grow = true; /* (not a write: -x, !x) */
        if (!grow) { rot_cmin_ok = false; return; }
    }
    rot_cmin_e(e->lhs, n); rot_cmin_e(e->rhs, n);
    for (int i = 0; i < e->args.len; i++) rot_cmin_e(e->args.items[i], n);
}
static void rot_cmin_s(Stmt *s, const char *n) {
    if (!s || !rot_cmin_ok) return;
    if (s->kind == S_VAR && s->cname && strcmp(s->cname, n) == 0) {
        if (s->cell || !s->init || s->init->kind != E_NUMBER || !s->init->is_int_lit) { rot_cmin_ok = false; return; }
        if (s->init->ival < rot_cmin_v) rot_cmin_v = s->init->ival;
    }
    rot_cmin_e(s->init, n); rot_cmin_e(s->expr, n); rot_cmin_e(s->cond, n); rot_cmin_e(s->for_update, n);
    rot_cmin_s(s->for_init, n); rot_cmin_s(s->body, n); rot_cmin_s(s->else_body, n);
    for (int i = 0; i < s->stmts.len; i++) rot_cmin_s(s->stmts.items[i], n);
    for (int i = 0; i < s->ncases; i++) rot_cmin_s(s->cases[i].body, n);
}
static bool rot_idx_above(Expr *i) { /* an index provably above lo: a local counter that starts above it and only grows */
    if (!i || i->kind != E_IDENT || !i->cname || strncmp(i->cname, "v_", 2) != 0 || !cur_func || !cur_func->body) return false;
    if (i->type && i->type->kind != TY_INT) return false;
    rot_cmin_v = INT64_MAX; rot_cmin_ok = true;
    rot_cmin_s(cur_func->body, i->cname);
    return rot_cmin_ok && rot_cmin_v != INT64_MAX && rot_cmin_v > rot_lo;
}
static int rot_expr(Expr *e, int st); /* the state after evaluating e */
static int rot_stmt(Stmt *s, int st, int *brk, int *cont);
static bool rot_idx_is_other_literal(Expr *i) { return i && i->kind == E_NUMBER && i->is_int_lit && i->ival != rot_lo; }
static int rot_expr(Expr *e, int st) {
    if (!e) return st;
    if (e->kind == E_ARROW) return (e->arrow && e->arrow->fn && (stmt_mentions_name(e->arrow->fn->body, rot_x) || (rot_y && stmt_mentions_name(e->arrow->fn->body, rot_y)))) ? 0 : st;
    if ((e->kind == E_ASSIGN || e->kind == E_POSTFIX || e->kind == E_UNARY) && e->lhs) {
        Expr *t = e->lhs;
        if (t->kind == E_IDENT && t->cname && strcmp(t->cname, rot_x) == 0) { /* x = ... */
            if (e->kind != E_ASSIGN || e->op != TK_ASSIGN) return 0;
            int before = rot_expr(e->rhs, st);
            if (before == 2 && rot_y && e->rhs->kind == E_IDENT && e->rhs->cname && strcmp(e->rhs->cname, rot_y) == 0) return 1; /* x = y, a[lo] holds y */
            return u8_is_index_on(e->rhs, rot_a->cname) && e->rhs->rhs->kind == E_NUMBER && e->rhs->rhs->is_int_lit && e->rhs->rhs->ival == rot_lo ? 1 : 0;
        }
        if (rot_y && t->kind == E_IDENT && t->cname && strcmp(t->cname, rot_y) == 0) { /* y written: a[lo] no longer known to hold it */
            st = rot_expr(e->rhs, st);
            return st == 2 ? 0 : st;
        }
        if (u8_is_index_on(t, rot_a->cname)) { /* a[i] = ... */
            st = rot_expr(t->rhs, st); st = rot_expr(e->rhs, st);
            Expr *ix = t->rhs;
            if (ix && ix->kind == E_NUMBER && ix->is_int_lit && ix->ival == rot_lo && e->kind == E_ASSIGN && e->op == TK_ASSIGN && e->rhs && e->rhs->kind == E_IDENT && e->rhs->cname) {
                if (strcmp(e->rhs->cname, rot_x) == 0) return 1;                              /* a[lo] = x */
                if (!rot_y || strcmp(rot_y, e->rhs->cname) == 0) { rot_y = e->rhs->cname; return 2; } /* a[lo] = y */
                return 0;
            }
            return rot_idx_is_other_literal(ix) || rot_idx_above(ix) ? st : 0;
        }
    }
    st = rot_expr(e->lhs, st); st = rot_expr(e->rhs, st);
    for (int i = 0; i < e->args.len; i++) st = rot_expr(e->args.items[i], st);
    return st;
}
static int rot_loop(Stmt *s, int st) {
    int head = st, exit = st;
    for (int pass = 0; pass < 3; pass++) {
        int brk = ROT_TOP, cont = ROT_TOP;
        int h = head;
        if (s->kind == S_FOR && pass == 0) h = rot_stmt(s->for_init, h, NULL, NULL);
        h = rot_expr(s->cond, h);
        if (s->kind == S_FOROF) h = rot_expr(s->expr, h);
        int end = rot_stmt(s->body, h, &brk, &cont);
        end = rot_expr(s->for_update, end);
        head = rot_meet(rot_meet(st, end), cont);
        exit = rot_meet(rot_meet(head, end), brk);
    }
    return exit;
}
static int rot_stmt(Stmt *s, int st, int *brk, int *cont) {
    if (!s) return st;
    if (s == rot_target) { rot_found = 1; rot_state_at = rot_meet(rot_state_at, st); }
    switch (s->kind) {
    case S_VAR:
        st = rot_expr(s->init, st);
        if (st == 2 && rot_y && s->cname && strcmp(s->cname, rot_y) == 0) return 0;
        if (s->cname && strcmp(s->cname, rot_x) == 0)
            return s->init && u8_is_index_on(s->init, rot_a->cname) && s->init->rhs->kind == E_NUMBER && s->init->rhs->is_int_lit && s->init->rhs->ival == rot_lo ? 1 : 0;
        return st;
    case S_EXPR: return rot_expr(s->expr, st);
    case S_IF: { int c = rot_expr(s->cond, st); return rot_meet(rot_stmt(s->body, c, brk, cont), s->else_body ? rot_stmt(s->else_body, c, brk, cont) : c); }
    case S_BLOCK: for (int i = 0; i < s->stmts.len; i++) st = rot_stmt(s->stmts.items[i], st, brk, cont); return st;
    case S_FOR: case S_WHILE: case S_FOROF: {
        U8Idiom m; /* an idiom's range reaching lo writes it; otherwise the loop is walked */
        if (s->kind == S_FOR && match_u8_idiom(s, &m) && m.a && m.a->cname && strcmp(m.a->cname, rot_a->cname) == 0) {
            if (m.kind == 3 || m.kind == 2) return (m.lo->kind == E_NUMBER && m.lo->ival > rot_lo) ? st : 0; /* [lo'..] */
            return (m.lo->kind == E_NUMBER && m.lo->ival > rot_lo) ? st : 0;
        }
        return rot_loop(s, st);
    }
    case S_SWITCH: { int c = rot_expr(s->expr, st), r = c; for (int i = 0; i < s->ncases; i++) r = rot_meet(r, rot_stmt(s->cases[i].body, c, brk, cont)); return r; }
    case S_BREAK: if (brk) *brk = rot_meet(*brk, st); return st;
    case S_CONTINUE: if (cont) *cont = rot_meet(*cont, st); return st;
    case S_RETURN: case S_THROW: return rot_expr(s->expr, st);
    case S_TRY: return 0;
    default: return 0;
    }
}
static bool u8_rot_ok(Stmt *target, Expr *a, Expr *lo, Expr *x) {
    if (!cur_func || !cur_func->body || !x->cname || lo->kind != E_NUMBER || !lo->is_int_lit) return false;
    rot_a = a; rot_lo = lo->ival; rot_x = x->cname; rot_target = target; rot_found = 0; rot_state_at = ROT_TOP; rot_y = NULL;
    int brk = ROT_TOP, cont = ROT_TOP;
    rot_stmt(cur_func->body, 0, &brk, &cont); /* x starts unknown */
    return rot_found && rot_state_at == 1;
}
/* `for (j = lo; j <= K; j++) a[j] = a[j + 1]; a[K + 1] = v;` with a in a register (fannkuch-redux's next
   permutation): one call, the shuffle and the lane (xs_v16_shl_put). b->stmts[*i] is the loop; on a match the
   pair is emitted and *i skips the store. */
/* `a[L - 1] = y; for (j = L; j < E; j++) a[j] = a[j + 1]; a[E] = x;` (or j <= K and a[K + 1]) with a in a register and
   y holding a[L] (a[L] itself, or a const read from it right before): the three together move a[L..E] down one
   place and put x at E, one shuffle and one lane (xs_v16_shl_put on [L - 1, E - 1]); a rotation of a[L - 1..E], one
   shuffle, when x is known to hold a[L - 1] before the first store (u8_rot_ok). Both when the shift runs at least
   once (E >= L). (fannkuch-redux's next permutation as one writes it first.) When
   E < L the statements run as written. The checks: a rotation of [L - 1, E] is in range exactly when E is, as the
   statements' own tests. XISTO_NO_ROT3=1 keeps them. */
static bool emit_u8_rot3(Stmt *b, int *i) {
    if (*i + 2 >= b->stmts.len || xs_debug || sm_mode || ictx || getenv("XISTO_NO_U8_IDIOM") || getenv("XISTO_NO_ROT3")) return false;
    Stmt *s0 = b->stmts.items[*i], *lp = b->stmts.items[*i + 1], *st = b->stmts.items[*i + 2];
    Expr *e0 = s0->kind == S_EXPR ? s0->expr : NULL;
    if (!e0 || e0->kind != E_ASSIGN || e0->op != TK_ASSIGN || !e0->lhs || e0->lhs->kind != E_INDEX || !e0->lhs->lhs || e0->lhs->lhs->kind != E_IDENT || !e0->lhs->lhs->cname) return false;
    Expr *a = e0->lhs->lhs;
    U8Reg *rg = u8reg_find(a);
    if (!rg || !e0->lhs->rhs || e0->lhs->rhs->kind != E_NUMBER || !e0->lhs->rhs->is_int_lit) return false;
    int64_t L1 = e0->lhs->rhs->ival; /* L - 1 */
    U8Idiom m;
    if (lp->kind != S_FOR || !match_u8_idiom(lp, &m) || m.kind != 3 || !m.a || !m.a->cname || strcmp(m.a->cname, a->cname) != 0) return false;
    if (!m.lo || m.lo->kind != E_NUMBER || !m.lo->is_int_lit || m.lo->ival != L1 + 1) return false;
    bool le = lp->cond->op == TK_LE;
    if (!le && lp->cond->op != TK_LT) return false;
    /* y holds a[L]: a[L] itself, or a const read from a[L] right before s0 with nothing between */
    Expr *y = e0->rhs;
    bool y_ok = y && u8_is_index_on(y, a->cname) && y->rhs->kind == E_NUMBER && y->rhs->is_int_lit && y->rhs->ival == L1 + 1;
    if (!y_ok && y && y->kind == E_IDENT && y->cname && *i > 0) {
        Stmt *d = b->stmts.items[*i - 1];
        y_ok = d->kind == S_VAR && d->cname && strcmp(d->cname, y->cname) == 0 && d->init && u8_is_index_on(d->init, a->cname) &&
               d->init->rhs->kind == E_NUMBER && d->init->rhs->is_int_lit && d->init->rhs->ival == L1 + 1;
    }
    if (!y_ok) return false;
    /* the store after the loop: a[E] = x (j < E) or a[K + 1] = x (j <= K) */
    Expr *as = st->kind == S_EXPR ? st->expr : NULL;
    if (!as || as->kind != E_ASSIGN || as->op != TK_ASSIGN || !u8_is_index_on(as->lhs, a->cname) || !as->rhs || as->rhs->kind != E_IDENT || !as->rhs->cname) return false;
    Expr *ix = as->lhs->rhs;
    if (le) { if (!ix || ix->kind != E_BINARY || ix->op != TK_PLUS || !same_simple(ix->lhs, m.hi) || !ix->rhs || ix->rhs->kind != E_NUMBER || ix->rhs->num != 1) return false; }
    else if (!same_simple(ix, lp->cond->rhs) || !bce_pure(ix)) return false;
    Expr lo1 = {0}; lo1.kind = E_NUMBER; lo1.is_int_lit = true; lo1.ival = L1; lo1.num = (double)L1; lo1.type = ty_int;
    const char *fn = u8_rot_ok(s0, a, &lo1, as->rhs) ? "xs_v16_rot" : "xs_v16_shl_put";
    int site = new_site(lp->pos);
    nl();
    emit_dbg_hook(s0);
    /* hi of the rotation: E - 1 (j < E) or K */
    buf_puts(out, "if ((");
    if (le) emit_expr(m.hi); else { buf_puts(out, "("); emit_expr(ix); buf_puts(out, ") - 1"); }
    buf_printf(out, ") >= INT64_C(%lld)) { ", (long long)L1);
    emit_u8reg_name(a); buf_printf(out, " = %s(", fn); emit_u8reg_name(a); buf_printf(out, ", INT64_C(%lld), ", (long long)L1);
    if (le) emit_expr(m.hi); else { buf_puts(out, "("); emit_expr(ix); buf_puts(out, ") - 1"); }
    buf_puts(out, ", "); emit_expr(as->rhs); buf_printf(out, ", %s, XS_S(%d)); } else ", rgn(rg), site);
    buf_puts(out, "{");
    indent++;
    for (int k = 0; k < 3; k++) { nl(); emit_stmt(b->stmts.items[*i + k]); }
    indent--;
    nl();
    buf_puts(out, "}");
    *i += 2;
    return true;
}
static bool emit_u8_shl_put(Stmt *b, int *i) {
    if (emit_u8_rot3(b, i)) return true;
    if (*i + 1 >= b->stmts.len || xs_debug || sm_mode || ictx || getenv("XISTO_NO_U8_IDIOM")) return false;
    Stmt *lp = b->stmts.items[*i], *st = b->stmts.items[*i + 1];
    U8Idiom m;
    if (lp->kind != S_FOR || !match_u8_idiom(lp, &m) || m.kind != 3 || lp->cond->op != TK_LE) return false;
    U8Reg *rg = u8reg_find(m.a);
    if (!rg) return false;
    Expr *as = st->kind == S_EXPR ? st->expr : NULL;
    if (!as || as->kind != E_ASSIGN || as->op != TK_ASSIGN || !u8_is_index_on(as->lhs, m.a->cname)) return false;
    Expr *ix = as->lhs->rhs; /* K + 1 */
    if (!ix || ix->kind != E_BINARY || ix->op != TK_PLUS || !same_simple(ix->lhs, m.hi) || !ix->rhs || ix->rhs->kind != E_NUMBER || ix->rhs->num != 1) return false;
    if (!bce_pure(as->rhs) && as->rhs->kind != E_IDENT) return false;
    int site = new_site(lp->pos);
    nl();
    emit_dbg_hook(lp);
    /* x known to hold a[lo] here: the pair is a rotation, one shuffle (u8_rot_ok) */
    const char *fn = as->rhs->kind == E_IDENT && m.lo->kind == E_NUMBER && u8_rot_ok(lp, m.a, m.lo, as->rhs) ? "xs_v16_rot" : "xs_v16_shl_put";
    emit_u8reg_name(m.a); buf_printf(out, " = %s(", fn); emit_u8reg_name(m.a); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", "); emit_expr(m.hi); buf_puts(out, ", ");
    emit_expr(as->rhs); buf_printf(out, ", %s, XS_S(%d));", rgn(rg), site);
    *i += 1;
    return true;
}
static void emit_block_kind(Stmt *b, ScopeKind kind) {
    buf_puts(out, "{");
    indent++;
    epush(false);
    escope->kind = kind;
    if (kind == SC_TRY) { escope->fin = pending_fin; pending_fin = NULL; }
    if (kind == SC_LOOP_BODY && region_reset_pending) {
        nl();
        buf_printf(out, region_dual[region_reset_pending] ? "xs_region_reset(&xs_rg_%d); xs_rg_on_%d = true;" : "xs_region_reset(&xs_rg_%d);", region_reset_pending, region_reset_pending);
        region_reset_pending = 0;
    }
    for (int i = 0; i < b->stmts.len; i++) {
        if (emit_u8_shl_put(b, &i)) continue;
        nl();
        emit_dbg_hook(b->stmts.items[i]);
        blk_prev_stmt = i > 0 ? b->stmts.items[i - 1] : NULL;
        { Stmt *sb = blk_cur; int si = blk_cur_i; blk_cur = b; blk_cur_i = i; emit_stmt(b->stmts.items[i]); blk_cur = sb; blk_cur_i = si; }
    }
    emit_releases(escope);
    epop();
    indent--;
    nl();
    buf_puts(out, "}");
}

static void emit_block(Stmt *b, bool func_root) {
    buf_puts(out, "{");
    indent++;
    epush(func_root);
    for (int i = 0; func_root && cur_func && i < cur_func->nparams; i++) { /* parameters a closure shares: into cells */
        Param *pm = &cur_func->params[i];
        if (!pm->cell) continue;
        const char *cs = cell_struct(pm->type);
        nl();
        buf_printf(out, "%s *%s = %s_new(); (%s->v) = %s%s%s;", cs, pm->cell, cs, pm->cell, crc(pm->type) ? "xs_retain(" : "", pm->cname, crc(pm->type) ? ")" : "");
        etrack(pm->cell, ty_unknown);
    }
    Class *after_super = func_root ? inits_after_super : NULL;
    if (func_root) inits_after_super = NULL;
    for (int i = 0; i < b->stmts.len; i++) {
        if (!after_super && emit_u8_shl_put(b, &i)) continue;
        nl();
        emit_dbg_hook(b->stmts.items[i]);
        blk_prev_stmt = i > 0 ? b->stmts.items[i - 1] : NULL;
        { Stmt *sb = blk_cur; int si = blk_cur_i; blk_cur = b; blk_cur_i = i; emit_stmt(b->stmts.items[i]); blk_cur = sb; blk_cur_i = si; }
        if (i == 0 && after_super) emit_own_field_inits(after_super); /* super(...) ran: now the class's own fields */
    }
    emit_releases(escope);
    epop();
    indent--;
    nl();
    buf_puts(out, "}");
}

static void drain_if_needed(void) {
    if (used_ar) {
        nl();
        buf_puts(out, "xs_ar_drain(ar_mark);");
        used_ar = false;
    }
}

static int dbg_local(const char *name, Type *t, const char *addr, bool is_const);
/* `const l = this.left` where left is never written after construction: the object stays in that field while
   `this` lives (all of this method), so the local borrows it: no retain, no release */
static bool mut_has(Class *c, const char *name);
/* an object that certainly outlives the local being declared: `this` outside a constructor (the constructor may
   still store the field), or a parameter of reference type (the checker refuses to reassign those, and the
   caller's reference keeps the object alive for the whole call). A nullable base reaches here through x! */
static const char *md_tuple[16]; /* the entry tuples of the map_direct loops being emitted (for (const [k, v] of map)) */
static int nmd_tuple;
static bool stable_base(Expr *b) {
    while (b && b->kind == E_AS && b->op == TK_NOT && !b->elem_type) b = b->lhs;
    if (!b) return false;
    /* XB-088: the entry of a map the loop holds and its body cannot change: its key and value live through the
       iteration (for (const [c, p] of table): c borrows the key, no retain, no release) */
    if (b->kind == E_IDENT && b->cname && !getenv("XISTO_NO_MD_BORROW"))
        for (int i = 0; i < nmd_tuple; i++) if (strcmp(md_tuple[i], b->cname) == 0) return true;
    if (!cur_func) return false;
    if (b->kind == E_THIS) return cur_func->cls && !cur_func->is_ctor;
    if (b->kind == E_IDENT && b->cname && !b->builtin.cfn)
        for (int i = 0; i < cur_func->nparams; i++) {
            Param *pm = &cur_func->params[i];
            if (pm->cname && !pm->cell && strcmp(pm->cname, b->cname) == 0) return cref(pm->type);
        }
    return false;
}
/* `const x = base.field` where the field is only written during construction and base outlives the scope: x
   borrows the field's reference (no retain, no release, no cleanup slot). Every use that keeps x retains it as
   it would any other local (binarytrees-2: 0,65 -> 0,42 s on the M4, 4,90 -> 3,17 s on the Graviton2). */
static bool borrowable_init(Stmt *s) {
    Expr *e = s->init;
    if (!s->is_const || s->cell || !e || !cref(s->type) || xs_debug || getenv("XISTO_NO_BORROW")) return false;
    if (e->kind != E_MEMBER || e->is_call || e->field < 0 || !e->cls || e->cls->builtin) return false;
    if (e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname && !getenv("XISTO_NO_MD_BORROW")) /* XB-088: a map entry's tuple (stable_base) */
        for (int i = 0; i < nmd_tuple; i++) if (strcmp(md_tuple[i], e->lhs->cname) == 0) return true;
    if (e->cls->is_interface) return false;
    if (!stable_base(e->lhs)) return false;
    return !mut_has(e->cls, e->cls->fields[e->field].name);
}
/* Reuse (XB-036): x = x.m(args) where m only returns new C(...) of x's own exact class and cannot fail. When x
   holds the only reference (rc 1) the old object would die right after the new one is made: its fields are
   rewritten in place instead, with no allocation and no free (an immutable-update style, state = state.with(v),
   costs what a mutation costs). The new values are all computed first, from the old object, so nothing is
   half-written if one fails; with other references the usual new object is made. XISTO_NO_REUSE=1 disables. */
static int nreuse;
static bool reuse_assign(Expr *e) {
    if (xs_debug || xs_profile || getenv("XISTO_NO_REUSE")) return false;
    if (!e || e->kind != E_ASSIGN || e->op != TK_ASSIGN || !e->lhs || e->lhs->kind != E_IDENT || !e->lhs->cname) return false;
    Class *c = exact_class(e->lhs->type);
    if (!c || e->lhs->type->kind != TY_OBJECT || in_hierarchy(c) || c->is_tuple || class_cyclic(c)) return false;
    Expr *call = e->rhs;
    if (!call || call->kind != E_MEMBER || !call->is_call || !call->func || call->virt || call->super_call || call->builtin.cfn) return false;
    if (!call->lhs || call->lhs->kind != E_IDENT || !call->lhs->cname || strcmp(call->lhs->cname, e->lhs->cname) != 0) return false;
    Func *m = call->func;
    if (m->cls != c || m->is_async || m->is_gen || !m->body || m->body->stmts.len != 1 || func_fails(m)) return false;
    Stmt *r = m->body->stmts.items[0];
    if (r->kind != S_RETURN || !r->expr || r->expr->kind != E_NEW_OBJ || r->expr->cls != c) return false;
    for (int i = 0; i < m->nparams; i++) if (m->params[i].cell || call->args.len <= i) return false;
    if (call->args.len != m->nparams) return false;
    Expr *ne = r->expr;
    /* the constructor: this.f = <parameter> for every field, each field once, no field initializers */
    Func *ct = c->ctor;
    if (!ct || !ct->body || ct->nparams != ne->args.len || class_init_fails(c)) return false;
    int *from = xs_alloc(sizeof(int) * (size_t)(c->nfields ? c->nfields : 1));
    for (int k = 0; k < c->nfields; k++) { from[k] = -1; if (c->fields[k].init) return false; }
    for (int i = 0; i < ct->nparams; i++) if (ct->params[i].cell || ct->params[i].is_prop) return false;
    for (int k = 0; k < ct->body->stmts.len; k++) {
        Stmt *st = ct->body->stmts.items[k];
        Expr *a = st->kind == S_EXPR ? st->expr : NULL;
        if (!a || a->kind != E_ASSIGN || a->op != TK_ASSIGN || !a->lhs || a->lhs->kind != E_MEMBER || !a->lhs->lhs || a->lhs->lhs->kind != E_THIS ||
            a->lhs->field < 0 || a->lhs->field >= c->nfields || !a->rhs || a->rhs->kind != E_IDENT || !a->rhs->cname) return false;
        int pi = -1;
        for (int i = 0; i < ct->nparams; i++) if (strcmp(ct->params[i].cname, a->rhs->cname) == 0) pi = i;
        if (pi < 0 || from[a->lhs->field] >= 0) return false;
        from[a->lhs->field] = pi;
    }
    for (int k = 0; k < c->nfields; k++) if (from[k] < 0) return false;
    int id = nreuse++;
    const char *x = ren(e->lhs->cname);
    buf_printf(out, "{ %s *self = %s; ", c->cname, x); /* m's body reads this as self */
    for (int i = 0; i < m->nparams; i++) {
        emit_decl(m->params[i].type, m->params[i].cname);
        buf_puts(out, " = ");
        emit_borrowed(call->args.items[i]);
        buf_puts(out, "; ");
    }
    for (int j = 0; j < ne->args.len; j++) {
        emit_decl(ct->params[j].type, xs_sprintf("ru%d_%d", id, j));
        buf_puts(out, " = ");
        emit_borrowed(ne->args.items[j]);
        buf_puts(out, "; ");
    }
    buf_puts(out, "if (self->hdr.rc == 1) { ");
    for (int k = 0; k < c->nfields; k++) {
        Field *fd = &c->fields[k];
        if (cref(fd->type)) {
            const char *kf = known_free(fd->type);
            buf_printf(out, "{ void *ro_ = (void *)self->%s; self->%s = xs_retain(ru%d_%d); ", fd->cname, fd->cname, id, from[k]);
            if (kf) buf_printf(out, "xs_release_as(ro_, %s); } ", kf);
            else buf_puts(out, "xs_release(ro_); } ");
        } else buf_printf(out, "self->%s = ru%d_%d; ", fd->cname, id, from[k]);
    }
    buf_printf(out, "} else { %s = %s_new(", x, c->cname);
    for (int j = 0; j < ne->args.len; j++) {
        if (j) buf_puts(out, ", ");
        if (ct->params[j].consumed && crc(ct->params[j].type)) buf_printf(out, "xs_retain(ru%d_%d)", id, j);
        else buf_printf(out, "ru%d_%d", id, j);
    }
    buf_printf(out, "); xs_release_as(self, %s_free); } }", c->cname);
    return true;
}

/* ---------- Float64Array locals in the frame ----------
   `const v = new Float64Array(N)`, N a literal <= 64 (a frame array of 512 bytes at most), in a function whose body only indexes v (v[i], v.length) or
   passes it to user functions that, by the same rule, only index that parameter: v is a double[N] in the C
   frame (xs_f64_on_frame) instead of an object on the heap, and the C compiler promotes it to registers (SROA)
   once the loops unroll: n-body's advance with its 11 arrays local, M4 2,40 -> 1,23 s (= Rust). Anything else
   (stored, returned, captured, iterated, a method called on it, a builtin receiving it) keeps the heap object.
   XISTO_NO_F64_LOCAL=1 turns it off. */
static bool f64_mentions_stmt(Stmt *s, const char *cname);
static bool f64_mentions(Expr *e, const char *cname) {
    if (!e) return false;
    if (e->kind == E_IDENT) return e->cname && strcmp(e->cname, cname) == 0;
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn && f64_mentions_stmt(e->arrow->fn->body, cname)) return true;
    if (f64_mentions(e->lhs, cname) || f64_mentions(e->rhs, cname)) return true;
    for (int i = 0; i < e->args.len; i++) if (f64_mentions(e->args.items[i], cname)) return true;
    return false;
}
static bool f64_mentions_stmt(Stmt *s, const char *cname) {
    if (!s) return false;
    if (f64_mentions(s->init, cname) || f64_mentions(s->expr, cname) || f64_mentions(s->cond, cname) || f64_mentions(s->for_update, cname)) return true;
    if (f64_mentions_stmt(s->for_init, cname) || f64_mentions_stmt(s->body, cname) || f64_mentions_stmt(s->else_body, cname)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (f64_mentions_stmt(s->stmts.items[i], cname)) return true;
    for (int i = 0; i < s->ncases; i++) if (f64_mentions_stmt(s->cases[i].body, cname)) return true;
    return false;
}
static bool f64_plain_stmt(Stmt *s, const char *cname, int depth);
static bool f64_plain_param(Func *f, int i, int depth) {
    if (depth > 3 || !f || !f->body || i >= f->nparams || f->params[i].cell || !f->params[i].cname) return false;
    return f64_plain_stmt(f->body, f->params[i].cname, depth);
}
static bool f64_is(Expr *e, const char *cname) { return e && e->kind == E_IDENT && e->cname && strcmp(e->cname, cname) == 0; }
static bool f64_plain_expr(Expr *e, const char *cname, int depth) {
    if (!e) return true;
    if (e->kind == E_IDENT) return !f64_is(e, cname); /* a bare use: it escapes */
    if (e->kind == E_INDEX && f64_is(e->lhs, cname)) return f64_plain_expr(e->rhs, cname, depth);
    if (e->kind == E_MEMBER && !e->is_call && f64_is(e->lhs, cname) && e->str && strcmp(e->str, "length") == 0) return true;
    if (e->kind == E_ARROW) { /* captured by a closure: the closure's copy would hold it past the frame */
        if (!e->arrow) return false;
        for (int i = 0; i < e->arrow->ncaptures; i++)
            if (e->arrow->captures[i].outer_cname && strcmp(e->arrow->captures[i].outer_cname, cname) == 0) return false;
        return !e->arrow->fn || !f64_mentions_stmt(e->arrow->fn->body, cname);
    }
    if (e->kind == E_CALL && e->func && e->func->body && !e->builtin.cfn) {
        if (!f64_plain_expr(e->lhs, cname, depth)) return false;
        for (int i = 0; i < e->args.len; i++) {
            if (f64_is(e->args.items[i], cname)) { if (!f64_plain_param(e->func, i, depth + 1)) return false; }
            else if (!f64_plain_expr(e->args.items[i], cname, depth)) return false;
        }
        return true;
    }
    if (!f64_plain_expr(e->lhs, cname, depth) || !f64_plain_expr(e->rhs, cname, depth)) return false;
    for (int i = 0; i < e->args.len; i++) if (!f64_plain_expr(e->args.items[i], cname, depth)) return false;
    return true;
}
static bool f64_plain_stmt(Stmt *s, const char *cname, int depth) {
    if (!s) return true;
    if (s->kind == S_FOROF && f64_mentions(s->expr, cname)) return false;
    if (!f64_plain_expr(s->init, cname, depth) || !f64_plain_expr(s->expr, cname, depth) || !f64_plain_expr(s->cond, cname, depth) || !f64_plain_expr(s->for_update, cname, depth)) return false;
    if (!f64_plain_stmt(s->for_init, cname, depth) || !f64_plain_stmt(s->body, cname, depth) || !f64_plain_stmt(s->else_body, cname, depth)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!f64_plain_stmt(s->stmts.items[i], cname, depth)) return false;
    for (int i = 0; i < s->ncases; i++) if (!f64_plain_stmt(s->cases[i].body, cname, depth)) return false;
    return true;
}
static int64_t bce_fixed_of(Stmt *s);
static int64_t f64_local_n(Stmt *s) {
    if (xs_debug || sm_mode || ictx || !cur_func || !cur_func->body || !is_f64_type(s->type) || getenv("XISTO_NO_F64_LOCAL")) return 0;
    int64_t n = bce_fixed_of(s);
    if (n <= 0 || n > 64) return 0;
    return f64_plain_stmt(cur_func->body, s->cname, 0) ? n : 0;
}
static void emit_var(Stmt *s) {
    if (sm_mode) { /* a local of a state-machine generator: a field of the frame, assigned (emit_gen_sm checked it is primitive) */
        buf_printf(out, "%s = ", ren(s->cname));
        emit_expr(s->init);
        buf_puts(out, ";");
        return;
    }
    int64_t fn_ = f64_local_n(s);
    if (fn_ > 0) { /* a Float64Array in the frame: a double[N] the C compiler keeps in registers */
        buf_printf(out, "double sd_%s[%lld] = {0}; xs_Float64Array so_%s; xs_Float64Array *%s = xs_f64_on_frame(&so_%s, sd_%s, %lld);",
                   s->cname, (long long)fn_, s->cname, s->cname, s->cname, s->cname, (long long)fn_);
        dbg_local(s->name, s->type, s->cname, s->is_const);
        return;
    }
    if (borrowable_init(s)) {
        emit_local_decl(s->type, s->cname);
        buf_puts(out, " = ");
        emit_expr(s->init);
        buf_puts(out, ";");
        dbg_local(s->name, s->type, s->cname, s->is_const);
        return;
    }
    if (s->cell) { /* shared with a closure: the value lives in a cell, released with the scope */
        const char *cs = cell_struct(s->type);
        buf_printf(out, "%s *%s = %s_new(); %s = ", cs, s->cell, cs, s->cname);
        emit_owned(s->init);
        buf_puts(out, ";");
        etrack(s->cell, ty_unknown);
        dbg_local(s->name, s->type, s->cname, s->is_const);
        return;
    }
    if (s->on_stack) { /* stackalloc.c: the object's memory is this frame's */
        Expr *e = s->init;
        Class *c = e->cls;
        Func *ct = c->ctor;
        for (int i = 0; i < e->args.len && i < 32; i++) /* strings that die with it: a buffer each, next to it */
            if (s->stack_str & (1u << i)) buf_printf(out, "_Alignas(16) char ss%d_%s[96]; ", i, s->cname);
        stack_vars = realloc(stack_vars, sizeof(char *) * (size_t)(nstack_vars + 1));
        stack_vars[nstack_vars++] = s->cname;
        buf_printf(out, ctor_sets_every_field(c) ? "%s st_%s; " : "%s st_%s = {0}; ", c->cname, s->cname);
        emit_local_decl(s->type, s->cname);
        buf_printf(out, " = %s_new_at(xs_obj_on_stack(&st_%s, sizeof(%s), &%s_type)", c->cname, s->cname, c->cname, c->cname);
        for (int i = 0; i < e->args.len; i++) {
            buf_puts(out, ", ");
            if (i < 32 && (s->stack_str & (1u << i))) tmpl_into = xs_sprintf("ss%d_%s", i, s->cname);
            if (ct && i < ct->nparams && ct->params[i].consumed) emit_owned(e->args.items[i]);
            else emit_borrowed(e->args.items[i]);
            tmpl_into = NULL;
        }
        buf_puts(out, ");");
        etrack(s->cname, s->type);
        return;
    }
    emit_local_decl(s->type, s->cname);
    buf_puts(out, " = ");
    emit_owned(s->init);
    buf_puts(out, ";");
    etrack(s->cname, s->type);
    dbg_local(s->name, s->type, s->cname, s->is_const);
}

/* Emits e into a side buffer; *ar says whether it pushed onto the autorelease pool. */
static char *emit_expr_to_str(Expr *e, bool *ar) {
    Buf *saved = out, side = {0};
    out = &side;
    used_ar = false;
    emit_expr(e);
    *ar = used_ar;
    out = saved;
    return side.data ? side.data : strdup("");
}

/* ---- generators inlined into their for...of (see ICtx) ---- */
static bool expr_has_arrow(Expr *e) {
    if (!e) return false;
    if (e->kind == E_ARROW) return true;
    if (expr_has_arrow(e->lhs) || expr_has_arrow(e->rhs)) return true;
    for (int i = 0; i < e->args.len; i++) if (expr_has_arrow(e->args.items[i])) return true;
    return false;
}
typedef struct { int yields; bool bad; } GenScan;
static void gscan(Stmt *s, GenScan *g) {
    if (!s) return;
    if (s->kind == S_TRY || s->cell || (s->kind == S_RETURN && s->expr)) g->bad = true;
    if (s->kind == S_YIELD) g->yields++;
    if (expr_has_arrow(s->init) || expr_has_arrow(s->expr) || expr_has_arrow(s->cond) || expr_has_arrow(s->for_update)) g->bad = true;
    gscan(s->for_init, g);
    gscan(s->body, g);
    gscan(s->else_body, g);
    for (int i = 0; i < s->stmts.len; i++) gscan(s->stmts.items[i], g);
    for (int i = 0; i < s->ncases; i++) { if (expr_has_arrow(s->cases[i].value)) g->bad = true; gscan(s->cases[i].body, g); }
}
static bool stmt_has_arrow(Stmt *s) {
    if (!s) return false;
    if (expr_has_arrow(s->init) || expr_has_arrow(s->expr) || expr_has_arrow(s->cond) || expr_has_arrow(s->for_update)) return true;
    if (stmt_has_arrow(s->for_init) || stmt_has_arrow(s->body) || stmt_has_arrow(s->else_body)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (stmt_has_arrow(s->stmts.items[i])) return true;
    for (int i = 0; i < s->ncases; i++) if (expr_has_arrow(s->cases[i].value) || stmt_has_arrow(s->cases[i].body)) return true;
    return false;
}
static bool gen_inline_ok(Func *f) {
    if (!f || !f->is_gen || !f->body || xs_debug || xs_profile || getenv("XISTO_NO_GEN_INLINE")) return false;
    for (int i = 0; i < f->nparams; i++) if (f->params[i].cell) return false;
    GenScan g = {0};
    gscan(f->body, &g);
    return !g.bad && g.yields <= 2;
}
/* the generator a for...of iterable calls, when it can be inlined there */
static Func *inline_gen_target(Expr *e, Stmt *loop) {
    Func *f = NULL;
    if (e->kind == E_CALL && e->func && e->func->is_gen && !e->func->cls && !e->builtin.cfn) f = e->func;
    else if (e->kind == E_MEMBER && e->is_call && e->func && e->func->is_gen && e->func->cls && !e->virt && !e->super_call && !e->builtin.cfn &&
             e->cls && !e->cls->builtin && !(e->lhs->kind == E_IDENT && !e->lhs->cname)) f = e->func;
    if (!f || !gen_inline_ok(f) || e->args.len != f->nparams) return NULL;
    /* each yield copies the loop body: twice only when that body makes no closure (emitted once per copy) */
    GenScan g = {0};
    gscan(f->body, &g);
    if (g.yields > 1 && loop && stmt_has_arrow(loop->body)) return NULL;
    int depth = 0;
    for (ICtx *c = ictx; c; c = c->parent, depth++) if (c->f == f) return NULL;
    return depth < 6 ? f : NULL;
}
static int count_ident_e(Expr *e, const char *n) {
    if (!e) return 0;
    int k = e->kind == E_IDENT && e->cname && strcmp(e->cname, n) == 0;
    k += count_ident_e(e->lhs, n) + count_ident_e(e->rhs, n);
    for (int i = 0; i < e->args.len; i++) k += count_ident_e(e->args.items[i], n);
    return k;
}
static int count_ident_s(Stmt *s, const char *n) {
    if (!s) return 0;
    int k = count_ident_e(s->init, n) + count_ident_e(s->expr, n) + count_ident_e(s->cond, n) + count_ident_e(s->for_update, n);
    k += count_ident_s(s->for_init, n) + count_ident_s(s->body, n) + count_ident_s(s->else_body, n);
    for (int i = 0; i < s->stmts.len; i++) k += count_ident_s(s->stmts.items[i], n);
    for (int i = 0; i < s->ncases; i++) k += count_ident_e(s->cases[i].value, n) + count_ident_s(s->cases[i].body, n);
    return k;
}
/* is n iterated by a for...of outside any loop (so at most once)? */
static bool forof_top(Stmt *s, const char *n, int depth) {
    if (!s) return false;
    if (s->kind == S_FOROF && depth == 0 && s->expr->kind == E_IDENT && s->expr->cname && strcmp(s->expr->cname, n) == 0) return true;
    int d = depth + (s->kind == S_WHILE || s->kind == S_FOR || s->kind == S_FOROF);
    if (forof_top(s->body, n, d) || forof_top(s->else_body, n, depth) || forof_top(s->for_init, n, depth)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (forof_top(s->stmts.items[i], n, depth)) return true;
    for (int i = 0; i < s->ncases; i++) if (forof_top(s->cases[i].body, n, depth)) return true;
    return false;
}
static void emit_owned(Expr *e);
/* Evaluates the call's receiver and arguments into the inlined generator's parameters (g<id>_name), as the
   call would: a stream argument that is itself an inlinable generator call is prepared now and inlined later. */
static Prep *gen_prepare(Expr *call) {
    Func *f = inline_gen_target(call, NULL);
    Prep *p = xs_alloc(sizeof *p);
    p->f = f;
    p->id = ++inl_ids;
    if (f->cls) {
        const char *nm = xs_sprintf("g%d_self", p->id);
        nl();
        buf_printf(out, "%s *%s = (%s *)", f->cls->cname, nm, f->cls->cname);
        emit_owned(call->lhs);
        buf_puts(out, ";");
        etrack(nm, ty_object_of(f->cls));
    }
    p->dparam = xs_alloc(sizeof(char *) * (size_t)(f->nparams + 1));
    p->dprep = xs_alloc(sizeof(Prep *) * (size_t)(f->nparams + 1));
    for (int i = 0; i < f->nparams; i++) {
        Param *pm = &f->params[i];
        Expr *a = call->args.items[i];
        if (pm->type->kind == TY_STREAM && inline_gen_target(a, NULL) && count_ident_s(f->body, pm->cname) == 1 && forof_top(f->body, pm->cname, 0)) {
            p->dparam[p->nd] = pm->cname;
            p->dprep[p->nd++] = gen_prepare(a);
            continue;
        }
        const char *nm = xs_sprintf("g%d_%s", p->id, pm->cname);
        nl();
        emit_local_decl(pm->type, nm);
        buf_puts(out, " = ");
        emit_owned(a);
        buf_puts(out, ";");
        etrack(nm, pm->type);
    }
    return p;
}
static void ictx_add(ICtx *c, const char *from) {
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 8;
        c->from = realloc(c->from, sizeof(char *) * (size_t)c->cap);
        c->to = realloc(c->to, sizeof(char *) * (size_t)c->cap);
    }
    c->from[c->n] = from;
    c->to[c->n++] = xs_sprintf("g%d_%s", c->id, from);
}
static void ictx_collect(ICtx *c, Stmt *s) {
    if (!s) return;
    if ((s->kind == S_VAR || s->kind == S_FOROF) && s->cname) ictx_add(c, s->cname);
    ictx_collect(c, s->for_init);
    ictx_collect(c, s->body);
    ictx_collect(c, s->else_body);
    for (int i = 0; i < s->stmts.len; i++) ictx_collect(c, s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) ictx_collect(c, s->cases[i].body);
}
/* the generator's body in place of the loop (inside the block opened for it, whose scope is the root) */
static void emit_gen_inline(Stmt *loop, Prep *p) {
    ICtx *c = xs_alloc(sizeof *c);
    c->f = p->f;
    c->id = p->id;
    c->loop = loop;
    c->parent = ictx;
    c->root = escope;
    c->prep = p;
    if (p->f->cls) ictx_add(c, "self");
    for (int i = 0; i < p->f->nparams; i++) ictx_add(c, p->f->params[i].cname);
    ictx_collect(c, p->f->body);
    ictx = c;
    nl();
    emit_block(p->f->body, false);
    ictx = c->parent;
}
/* releases every scope from the current one up to `to` (included, or not), popping try handlers on the way */
static void emit_releases_upto(EScope *to, bool incl) {
    for (EScope *s = escope; s; s = s->parent) {
        if (s == to && !incl) break;
        emit_releases(s);
        emit_try_pop_if(s);
        if (s == to) break;
    }
}
static EScope *jump_target(bool is_break) {
    for (EScope *s = escope; s; s = s->parent)
        if (s->kind == SC_LOOP_BODY || (is_break && s->kind == SC_SWITCH) || s->func_root) return s;
    return NULL;
}

/* `for (...; i < n; i++)` whose body never writes i: when the update runs, i < n held at the start of the
   iteration, so i + 1 <= n <= 2^53 - 1 (every int is a safe integer) and the increment needs no overflow check. Without the check (a
   branch to an error) the C compiler can vectorize the loop. */
static bool writes_name(Expr *e, const char *c);
static bool stmt_writes_name(Stmt *s, const char *c) {
    if (!s) return false;
    if (writes_name(s->init, c) || writes_name(s->expr, c) || writes_name(s->cond, c) || writes_name(s->for_update, c)) return true;
    if (stmt_writes_name(s->for_init, c) || stmt_writes_name(s->body, c) || stmt_writes_name(s->else_body, c)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (stmt_writes_name(s->stmts.items[i], c)) return true;
    for (int i = 0; i < s->ncases; i++) if (writes_name(s->cases[i].value, c) || stmt_writes_name(s->cases[i].body, c)) return true;
    return false;
}
static bool writes_name(Expr *e, const char *c) {
    if (!e) return false;
    if ((e->kind == E_ASSIGN || e->kind == E_POSTFIX || (e->kind == E_UNARY && (e->op == TK_PLUSPLUS || e->op == TK_MINUSMINUS))) &&
        e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname && strcmp(e->lhs->cname, c) == 0) return true;
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn && stmt_writes_name(e->arrow->fn->body, c)) return true;
    if (writes_name(e->lhs, c) || writes_name(e->rhs, c)) return true;
    for (int i = 0; i < e->args.len; i++) if (writes_name(e->args.items[i], c)) return true;
    return false;
}
static void mark_counted_inc(Stmt *s) {
    Expr *u = s->for_update, *c = s->cond;
    if (!u || !c || u->kind != E_POSTFIX || u->op != TK_PLUSPLUS || !u->type || u->type->kind != TY_INT) return;
    if (!u->lhs || u->lhs->kind != E_IDENT || !u->lhs->cname || u->builtin.cfn) return;
    if (c->kind != E_BINARY || c->op != TK_LT || !c->lhs || c->lhs->kind != E_IDENT || !c->lhs->cname || strcmp(c->lhs->cname, u->lhs->cname) != 0) return;
    if (stmt_writes_name(s->body, u->lhs->cname) || writes_name(c->rhs, u->lhs->cname)) return;
    u->builtin.cfn = "counted_inc";
}
/* ---- integer ranges (the start of xisto's own optimizer, XB-006) ----
   A local int variable gets the union of the ranges of every value assigned to it; an expression's range
   follows from its operands. When `a + b`, `a - b` or `a * b` provably fits in 64 bits, it is emitted without
   the overflow check: a branch to an error path that also keeps the C compiler from vectorizing the loop.
   Conservative: a compound assignment, ++/-- (other than a counted loop's), a closure capturing the variable,
   or a range it cannot bound make the variable unknown. Kept per function (cur_func): local C names repeat
   between functions. */
#define RNG_LEN ((int64_t)1 << 40) /* the bound of every length (xs_len_bound) */
static const char **rv_name;
static Rng *rv_rng;
static bool *rv_nn; /* the variable is never negative (rv_compute: a greatest fixpoint over its definitions) */
static int rv_n, rv_cap;
static Func *rv_owner;
static bool rv_valid;
typedef struct { const char *name; int64_t n; } BceFix;
static BceFix cig[64]; /* the top-level `const N: int = literal` (garr_collect): N has that one value */
static int ncig;
static bool cig_find(const char *c, int64_t *v) {
    if (!c) return false;
    for (int i = 0; i < ncig; i++) if (strcmp(cig[i].name, c) == 0) { *v = cig[i].n; return true; }
    return false;
}
static Rng rng_none(void) { return (Rng){false, 0, 0}; }
#define RNG_SAFE INT64_C(9007199254740991) /* an int is a safe integer of JS (XB-075): a result outside is checked */
static Rng rng_mk(__int128 lo, __int128 hi) {
    if (lo < -RNG_SAFE || hi > RNG_SAFE) return rng_none();
    return (Rng){true, (int64_t)lo, (int64_t)hi};
}
static Rng rng_union(Rng a, Rng b) { if (!a.ok || !b.ok) return rng_none(); return (Rng){true, a.lo < b.lo ? a.lo : b.lo, a.hi > b.hi ? a.hi : b.hi}; }
static int rv_find(const char *c) { for (int i = 0; i < rv_n; i++) if (strcmp(rv_name[i], c) == 0) return i; return -1; }
static Rng rng_of(Expr *e) {
    if (e && e->rng_set) return (Rng){true, e->rng_lo, e->rng_hi};
    if (!e || !e->type || !rv_valid || rv_owner != cur_func || ictx) return rng_none();
    switch (e->kind) {
    case E_NUMBER: if (e->is_int_lit) return (Rng){true, e->ival, e->ival}; return rng_none();
    case E_IDENT: {
        int k = e->cname ? rv_find(e->cname) : -1;
        if (k >= 0) return rv_rng[k];
        int64_t v;
        if (e->type->kind == TY_INT && cig_find(e->cname, &v)) return (Rng){true, v, v}; /* a const global with one value */
        return rng_none();
    }
    case E_MEMBER:
        if (!e->is_call && e->str && strcmp(e->str, "length") == 0) return (Rng){true, 0, RNG_LEN};
        return rng_none();
    case E_INDEX:
        if (e->builtin.cfn && strcmp(e->builtin.cfn, "xs_u8_get") == 0) return (Rng){true, 0, 255};
        if (!e->builtin.cfn && e->lhs && e->lhs->kind == E_IDENT && e->type->kind == TY_INT) { /* T[p]: the entries of a constant table */
            BceGArr *g = garr_find(e->lhs->cname);
            if (g && g->has_rng) return (Rng){true, g->lo, g->hi};
        }
        return rng_none();
    case E_UNARY:
        if (e->op == TK_MINUS) { Rng a = rng_of(e->lhs); if (a.ok) return rng_mk(-(__int128)a.hi, -(__int128)a.lo); }
        return rng_none();
    case E_COND: return e->args.len == 2 ? rng_union(rng_of(e->args.items[0]), rng_of(e->args.items[1])) : rng_none();
    case E_BINARY: {
        if (e->type->kind != TY_INT) return rng_none();
        Rng a = rng_of(e->lhs), b = rng_of(e->rhs);
        const Rng i32 = {true, INT32_MIN, INT32_MAX};
        switch (e->op) {
        case TK_PLUS: return a.ok && b.ok ? rng_mk((__int128)a.lo + b.lo, (__int128)a.hi + b.hi) : rng_none();
        case TK_MINUS: return a.ok && b.ok ? rng_mk((__int128)a.lo - b.hi, (__int128)a.hi - b.lo) : rng_none();
        case TK_STAR: {
            if (!a.ok || !b.ok) return rng_none();
            __int128 c[4] = {(__int128)a.lo * b.lo, (__int128)a.lo * b.hi, (__int128)a.hi * b.lo, (__int128)a.hi * b.hi};
            __int128 lo = c[0], hi = c[0];
            for (int i = 1; i < 4; i++) { if (c[i] < lo) lo = c[i]; if (c[i] > hi) hi = c[i]; }
            return rng_mk(lo, hi);
        }
        case TK_AMP: /* int32 semantics: with a non-negative operand below 2^31 the result is within [0, it] */
            if (b.ok && b.lo >= 0 && b.hi <= INT32_MAX) return (Rng){true, 0, b.hi};
            if (a.ok && a.lo >= 0 && a.hi <= INT32_MAX) return (Rng){true, 0, a.hi};
            return i32;
        case TK_SHR:
            if (a.ok && a.lo >= 0 && a.hi <= INT32_MAX && b.ok && b.lo == b.hi && b.lo >= 0 && b.lo < 32) return (Rng){true, 0, a.hi >> b.lo};
            return i32;
        case TK_PIPE: case TK_CARET: case TK_SHL: return i32;
        case TK_USHR: return (Rng){true, 0, (int64_t)UINT32_MAX};
        case TK_PERCENT: /* |x % b| < b: with b in [lo, hi], lo > 0, the result is within (-hi, hi) */
            if (b.ok && b.lo > 0) return a.ok && a.lo >= 0 ? (Rng){true, 0, b.hi - 1} : (Rng){true, -(b.hi - 1), b.hi - 1};
            return rng_none();
        default: return rng_none();
        }
    }
    default: return rng_none();
    }
}
/* the variables' ranges: definitions collected, then a few rounds of union until nothing moves */
typedef struct { const char *name; Expr *def; Stmt *loop; bool top; } RvDef;
static RvDef *rv_defs;
static int rv_ndefs, rv_cdefs;
static void rv_def(const char *name, Expr *def, Stmt *loop, bool top) {
    if (rv_ndefs == rv_cdefs) { rv_cdefs = rv_cdefs ? rv_cdefs * 2 : 64; rv_defs = realloc(rv_defs, sizeof(RvDef) * (size_t)rv_cdefs); }
    rv_defs[rv_ndefs++] = (RvDef){name, def, loop, top};
}
/* is e certainly a positive power of two? a literal, an entry of a constant table of powers of two, or an int
   local every definition of which is one (knucleotide-4: `const top = TOPS[k - 1]`) */
static bool pow2_of(Expr *e, int depth) {
    if (!e || depth > 4) return false;
    if (e->kind == E_NUMBER) return e->is_int_lit && e->ival > 0 && (e->ival & (e->ival - 1)) == 0;
    if (e->kind == E_INDEX && !e->builtin.cfn && e->lhs && e->lhs->kind == E_IDENT) { BceGArr *g = garr_find(e->lhs->cname); return g && g->has_rng && g->pow2; }
    if (e->kind == E_IDENT && e->cname && rv_valid && rv_owner == cur_func && !ictx) {
        bool any = false;
        for (int i = 0; i < rv_ndefs; i++) {
            if (strcmp(rv_defs[i].name, e->cname) != 0) continue;
            if (rv_defs[i].top || !rv_defs[i].def || !pow2_of(rv_defs[i].def, depth + 1)) return false;
            any = true;
        }
        return any;
    }
    return false;
}
static void rv_scan_stmt(Stmt *s, bool decls);
static void rv_scan_expr(Expr *e) {
    if (!e) return;
    if (e->kind == E_ASSIGN && e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname)
        rv_def(e->lhs->cname, e->op == TK_ASSIGN ? e->rhs : NULL, NULL, e->op != TK_ASSIGN);
    if ((e->kind == E_POSTFIX || (e->kind == E_UNARY && (e->op == TK_PLUSPLUS || e->op == TK_MINUSMINUS))) && e->lhs && e->lhs->kind == E_IDENT &&
        e->lhs->cname && !(e->builtin.cfn && strcmp(e->builtin.cfn, "counted_inc") == 0))
        rv_def(e->lhs->cname, NULL, NULL, true);
    if (e->kind == E_ARROW && e->arrow) { /* captured: written who knows where */
        for (int i = 0; i < e->arrow->ncaptures; i++) if (e->arrow->captures[i].outer_cname) rv_def(e->arrow->captures[i].outer_cname, NULL, NULL, true);
        if (e->arrow->fn) rv_scan_stmt(e->arrow->fn->body, false);
    }
    rv_scan_expr(e->lhs);
    rv_scan_expr(e->rhs);
    for (int i = 0; i < e->args.len; i++) rv_scan_expr(e->args.items[i]);
}
static void rv_scan_stmt(Stmt *s, bool decls) {
    if (!s) return;
    if (s->kind == S_VAR && s->type && s->type->kind == TY_INT && s->cname && !s->cell && decls) {
        if (rv_find(s->cname) < 0) {
            if (rv_n == rv_cap) { rv_cap = rv_cap ? rv_cap * 2 : 64; rv_name = realloc(rv_name, sizeof(char *) * (size_t)rv_cap); rv_rng = realloc(rv_rng, sizeof(Rng) * (size_t)rv_cap); rv_nn = realloc(rv_nn, sizeof(bool) * (size_t)rv_cap); }
            rv_name[rv_n] = s->cname;
            rv_rng[rv_n++] = rng_none();
        }
        rv_def(s->cname, s->init, NULL, !s->init);
    }
    if (s->kind == S_FOR) {
        mark_counted_inc(s);
        if (s->for_update && s->for_update->builtin.cfn && strcmp(s->for_update->builtin.cfn, "counted_inc") == 0)
            rv_def(s->for_update->lhs->cname, NULL, s, false); /* the counter: up to the bound */
    }
    if (s->kind == S_FOROF && s->cname) rv_def(s->cname, NULL, NULL, true);
    rv_scan_expr(s->init);
    rv_scan_expr(s->expr);
    rv_scan_expr(s->cond);
    rv_scan_expr(s->for_update);
    rv_scan_stmt(s->for_init, decls);
    rv_scan_stmt(s->body, decls);
    rv_scan_stmt(s->else_body, decls);
    for (int i = 0; i < s->stmts.len; i++) rv_scan_stmt(s->stmts.items[i], decls);
    for (int i = 0; i < s->ncases; i++) { rv_scan_expr(s->cases[i].value); rv_scan_stmt(s->cases[i].body, decls); }
}
/* `const t = new Uint8Array(256)` or `new Float64Array(8)`: a length that is known (a typed array's length never
   changes, and a const is never assigned again), so t[x] needs no check when x's range is within it (bce_safe): the
   function's own (bce_fix_l, for bce_fix_owner) and the globals' (bce_fix_g, every function sees them); bce_fixed_n */
static BceFix bce_fix_l[32], bce_fix_g[32];
static int nbce_fix_l, nbce_fix_g;
static Func *bce_fix_owner;
/* fixed local arrays (any length): `const X = new Array<int>(N).fill(v)` that its function only indexes keeps its
   data and length for its whole life, so the emitted code reads them from C locals (X_fxd, X_fxl) set at the
   declaration. The check stays (and its failure goes through the usual call, with its message); the C compiler
   no longer reloads X->data and X->len after every store to an element (an int64 store may alias them).
   fannkuchredux.ts, count[]: Graviton2 1,74 -> 1,68 u. XISTO_NO_FIX_HOIST=1: off */
static const char *fx_l[32];
static bool fx_decl[32];
static int nfx_l;
static Func *fx_owner;
static int fx_find(const char *c) {
    if (!c || fx_owner != cur_func) return -1;
    for (int i = 0; i < nfx_l; i++) if (strcmp(fx_l[i], c) == 0) return i;
    return -1;
}
static int64_t bce_fixed_of(Stmt *s) {
    if (s->kind != S_VAR || !s->is_const || !s->cname || s->cell || !s->init) return 0;
    Expr *e = s->init;
    if (e->kind != E_NEW_OBJ || !e->builtin.cfn || (strcmp(e->builtin.cfn, "xs_u8_new") != 0 && strcmp(e->builtin.cfn, "xs_f64_new") != 0) || e->args.len != 1) return 0;
    Expr *n = e->args.items[0];
    int64_t v;
    if (n->kind == E_IDENT && n->cname && cig_find(n->cname, &v)) return v > 0 ? v : 0; /* new Float64Array(N), N a top-level const int */
    return n->kind == E_NUMBER && n->is_int_lit && n->ival > 0 ? n->ival : 0;
}
static void bce_fix_scan(Stmt *s) {
    if (!s) return;
    int64_t n = bce_fixed_of(s);
    if (n && nbce_fix_l < 32) bce_fix_l[nbce_fix_l++] = (BceFix){s->cname, n};
    bce_fix_scan(s->for_init);
    bce_fix_scan(s->body);
    bce_fix_scan(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) bce_fix_scan(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) bce_fix_scan(s->cases[i].body);
}
/* Top-level `const T: int[] = [0, 0, 1, 2]` and `const x: number[] = [...]` / `new Array<number>(N).fill(v)` that the
   whole program only indexes (`T[i]`, `T.length`; never passed, stored, iterated or resized): their length is
   the literal's, so `x[i]` needs no check when i's range is within it (bce_safe), and a table of int literals
   that is never written gives `T[p]` the range of its elements (rng_of): `const i = PI[p]; x[i]` is unchecked
   when every entry of PI is below x's length (nbody-3). */
static BceGArr bce_garr[32];
static int nbce_garr;
static bool garr_expr(Expr *e, Expr *parent, bool target, const char *c, bool elem_w);
static bool garr_stmt(Stmt *s, const char *c, bool elem_w) {
    if (!s) return true;
    if (!garr_expr(s->init, NULL, false, c, elem_w) || !garr_expr(s->expr, NULL, false, c, elem_w) || !garr_expr(s->cond, NULL, false, c, elem_w) ||
        !garr_expr(s->for_update, NULL, false, c, elem_w)) return false;
    if (!garr_stmt(s->for_init, c, elem_w) || !garr_stmt(s->body, c, elem_w) || !garr_stmt(s->else_body, c, elem_w)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!garr_stmt(s->stmts.items[i], c, elem_w)) return false;
    for (int i = 0; i < s->ncases; i++) if (!garr_expr(s->cases[i].value, NULL, false, c, elem_w) || !garr_stmt(s->cases[i].body, c, elem_w)) return false;
    return true;
}
/* target: e is the left side of an assignment or ++/-- (or inside the index of one) */
static bool garr_expr(Expr *e, Expr *parent, bool target, const char *c, bool elem_w) {
    if (!e) return true;
    if (e->kind == E_IDENT && e->cname && strcmp(e->cname, c) == 0) {
        bool as_base = parent && parent->kind == E_INDEX && parent->lhs == e && !parent->builtin.cfn;
        bool as_len = parent && parent->kind == E_MEMBER && !parent->is_call && parent->lhs == e && parent->str && strcmp(parent->str, "length") == 0;
        if (as_len) return true;
        if (!as_base) return false;
        return elem_w || !target;
    }
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn && !garr_stmt(e->arrow->fn->body, c, elem_w)) return false;
    bool writes = e->kind == E_ASSIGN || e->kind == E_POSTFIX || (e->kind == E_UNARY && (e->op == TK_PLUSPLUS || e->op == TK_MINUSMINUS));
    if (!garr_expr(e->lhs, e, target || writes, c, elem_w) || !garr_expr(e->rhs, e, target && e->kind != E_ASSIGN, c, elem_w)) return false;
    for (int i = 0; i < e->args.len; i++) if (!garr_expr(e->args.items[i], e, false, c, elem_w)) return false;
    return true;
}
static bool garr_program_ok(Program *p, const char *c, bool elem_w) {
    for (int i = 0; i < p->nfuncs; i++) if (p->funcs[i]->body && !garr_stmt(p->funcs[i]->body, c, elem_w)) return false;
    for (int i = 0; i < p->nclasses; i++) {
        Class *k = p->classes[i];
        for (int f = 0; f < k->nfields; f++) if (!garr_expr(k->fields[f].init, NULL, false, c, elem_w)) return false;
        if (k->ctor && k->ctor->body && !garr_stmt(k->ctor->body, c, elem_w)) return false;
        for (int m = 0; m < k->nmethods; m++) if (k->methods[m]->body && !garr_stmt(k->methods[m]->body, c, elem_w)) return false;
    }
    for (int i = 0; i < p->top.len; i++) {
        Stmt *s = p->top.items[i];
        if (s->kind == S_VAR && s->cname && strcmp(s->cname, c) == 0) { if (!garr_expr(s->init, NULL, false, c, elem_w)) return false; continue; }
        if (!garr_stmt(s, c, elem_w)) return false;
    }
    return true;
}
static BceGArr *garr_find(const char *c) {
    if (!c) return NULL;
    for (int i = 0; i < nbce_garr; i++) if (strcmp(bce_garr[i].name, c) == 0) return &bce_garr[i];
    return NULL;
}
static int64_t bce_fixed_n(const char *c) {
    if (xs_debug || getenv("XISTO_NO_BCE")) return 0;
    if (bce_fix_owner == cur_func && rv_owner == cur_func)
        for (int i = 0; i < nbce_fix_l; i++) if (strcmp(bce_fix_l[i].name, c) == 0) return bce_fix_l[i].n;
    for (int i = 0; i < nbce_fix_g; i++) if (strcmp(bce_fix_g[i].name, c) == 0) return bce_fix_g[i].n;
    BceGArr *g = garr_find(c);
    return g ? g->n : 0;
}
/* A local `const X: int[] = new Array<int>(N).fill(v)` (or number[]), N a literal or a top-level const int, that
   its function only indexes (`X[i]`, `X[i] = v`, `X[i]++`, `X.length`; never passed, stored, aliased, iterated,
   pushed or resized): its length stays N, so `X[i]` needs no check when i's range is within it (bce_safe).
   collections.ts: `tally[key]++` with `key = (...) & mask`, mask below the table's 65536 entries. */
static bool garr_stmt(Stmt *s, const char *c, bool elem_w);
static void bce_fix_arr_scan(Stmt *s, Stmt **all, int n) {
    if (!s || nbce_fix_l >= 32) return;
    if (s->kind == S_VAR && s->is_const && s->cname && !s->cell && s->init && s->type && s->type->kind == TY_ARRAY &&
        s->type->elem && (s->type->elem->kind == TY_INT || s->type->elem->kind == TY_NUMBER) && !getenv("XISTO_NO_ARR_FIX")) {
        Expr *e = s->init;
        if (e->kind == E_MEMBER && e->is_call && e->str && strcmp(e->str, "fill") == 0 && e->args.len == 1 && e->lhs && e->lhs->kind == E_NEW_ARRAY && e->lhs->lhs) {
            Expr *len = e->lhs->lhs;
            int64_t v = 0;
            if (len->kind == E_NUMBER && len->is_int_lit) v = len->ival;
            else if (len->kind == E_IDENT && len->cname && !cig_find(len->cname, &v)) v = 0;
            bool only = true;
            for (int i = 0; i < n && only; i++) only = garr_stmt(all[i], s->cname, true);
            if (only && v > 0) bce_fix_l[nbce_fix_l++] = (BceFix){s->cname, v};
            if (only && nfx_l < 32 && !xs_debug && !getenv("XISTO_NO_FIX_HOIST")) { fx_decl[nfx_l] = false; fx_l[nfx_l++] = s->cname; }
        }
    }
    bce_fix_arr_scan(s->for_init, all, n);
    bce_fix_arr_scan(s->body, all, n);
    bce_fix_arr_scan(s->else_body, all, n);
    for (int i = 0; i < s->stmts.len; i++) bce_fix_arr_scan(s->stmts.items[i], all, n);
    for (int i = 0; i < s->ncases; i++) bce_fix_arr_scan(s->cases[i].body, all, n);
}
/* e is never negative, given the variables rv_nn marks: literals, bytes, ranges at or above 0, and + * % of such
   values (int + and * either trap on overflow or are proven not to overflow, so they never wrap below zero) */
static bool nn_expr(Expr *e) {
    if (!e || !e->type || e->type->kind != TY_INT) return false;
    Rng r = rng_of(e);
    if (r.ok && r.lo >= 0) return true;
    switch (e->kind) {
    case E_IDENT: { int k = e->cname ? rv_find(e->cname) : -1; return k >= 0 && rv_nn[k]; }
    case E_BINARY:
        if (e->op == TK_PLUS || e->op == TK_STAR) return nn_expr(e->lhs) && nn_expr(e->rhs);
        if (e->op == TK_PERCENT) { Rng b = rng_of(e->rhs); return b.ok && b.lo > 0 && nn_expr(e->lhs); }
        return false;
    case E_COND: return e->args.len == 2 && nn_expr(e->args.items[0]) && nn_expr(e->args.items[1]);
    default: return false;
    }
}
/* XB-087: JavaScript's -0. An int slot (variable, field, element) holds 0 for it, but an expression can still produce
   one: x * -3 at x = 0, -x, x % 2 of a negative x, Math.trunc(-0.5). Where the value is seen as a number (1 / v,
   Number(v), console.log), such an expression is computed as a double with the sign kept; the int result and its
   range check are the same. */
static bool nz_nn(Expr *e) {
    if (!e) return false;
    if (e->kind == E_NUMBER) return e->num >= 0;
    return rv_valid && rv_owner == cur_func && !ictx && nn_expr(e);
}
static bool nz_nonzero(Expr *e) { return e && e->kind == E_NUMBER && e->num != 0; }
static bool nzm(Expr *e, const char *fn) {
    return e && e->kind == E_MEMBER && e->is_call && e->lhs && e->lhs->kind == E_IDENT && !e->lhs->cname && e->func == NULL &&
           e->lhs->str && strcmp(e->lhs->str, "Math") == 0 && e->str && strcmp(e->str, fn) == 0;
}
static bool may_nz(Expr *e) {
    if (!e || !e->type || e->type->kind != TY_INT || getenv("XISTO_NO_NEGZERO")) return false;
    switch (e->kind) {
    case E_IDENT: return !ictx && nzv_has(e->cname);
    case E_UNARY: return e->op == TK_MINUS && !nz_nonzero(e->lhs);
    case E_BINARY:
        if (e->lhs->type->kind != TY_INT) return false;
        switch (e->op) {
        case TK_STAR:
            if (e->lhs->kind == E_NUMBER && e->lhs->num > 0) return may_nz(e->rhs); /* 2 * y: -0 only from a -0 y */
            if (e->rhs->kind == E_NUMBER && e->rhs->num > 0) return may_nz(e->lhs);
            return !(nz_nn(e->lhs) && nz_nn(e->rhs)) || may_nz(e->lhs) || may_nz(e->rhs);
        case TK_PERCENT: return !nz_nn(e->lhs) || may_nz(e->lhs);
        case TK_PLUS: return may_nz(e->lhs) && may_nz(e->rhs);
        case TK_MINUS: return may_nz(e->lhs);
        default: return false;
        }
    case E_COND: return e->args.len == 2 && (may_nz(e->args.items[0]) || may_nz(e->args.items[1]));
    case E_MEMBER:
        if (nzm(e, "trunc") || nzm(e, "ceil") || nzm(e, "round") || nzm(e, "floor"))
            return e->args.len == 1 && e->args.items[0]->type->kind == TY_NUMBER;
        if (nzm(e, "min") || nzm(e, "max")) {
            for (int k = 0; k < e->args.len; k++) if (may_nz(e->args.items[k])) return true;
            return false;
        }
        return false;
    default: return false;
    }
}
static void emit_nzd(Expr *e) {
    static int nzn;
    if (!may_nz(e)) { buf_puts(out, "((double)("); emit_expr(e); buf_puts(out, "))"); return; }
    int id = nzn++;
    switch (e->kind) {
    case E_IDENT: buf_printf(out, "%s_z", e->cname); return;
    case E_UNARY: buf_puts(out, "(-"); emit_nzd(e->lhs); buf_puts(out, ")"); return; /* (inside the safe range both ways) */
    case E_COND:
        buf_puts(out, "(");
        emit_expr(e->lhs);
        buf_puts(out, " ? ");
        emit_nzd(e->args.items[0]);
        buf_puts(out, " : ");
        emit_nzd(e->args.items[1]);
        buf_puts(out, ")");
        return;
    case E_BINARY: { /* in doubles: integers of the safe range give the exact result, and the range test on it is
                        the int's (a sum or product past 2^53 - 1 rounds to 2^53 or more); a zero keeps its JS sign */
        if (!may_nz(e->lhs) && !may_nz(e->rhs) && (e->op == TK_STAR || e->op == TK_PERCENT)) {
            /* integer operands (no -0 in them): the int operation as always, the sign looked at only for a zero */
            buf_printf(out, "({ int64_t za%d_ = ", id);
            emit_expr(e->lhs);
            buf_printf(out, ", zb%d_ = ", id);
            emit_expr(e->rhs);
            buf_printf(out, ", zr%d_ = %s(za%d_, zb%d_", id, e->op == TK_STAR ? "xs_imul_s" : "xs_imod_s", id, id);
            site_arg(e->pos);
            if (e->op == TK_STAR) buf_printf(out, "); __builtin_expect(zr%d_ != 0, 1) ? (double)zr%d_ : (double)za%d_ * (double)zb%d_; })", id, id, id, id);
            else buf_printf(out, "); __builtin_expect(zr%d_ != 0, 1) ? (double)zr%d_ : (za%d_ < 0 ? -0.0 : 0.0); })", id, id, id);
            return;
        }
        const char *fn = e->op == TK_STAR ? "xs_dmul_s" : e->op == TK_PERCENT ? "xs_dmod_s" : e->op == TK_PLUS ? "xs_dadd_s" : "xs_dsub_s";
        buf_printf(out, "%s(", fn);
        emit_nzd(e->lhs);
        buf_puts(out, ", ");
        emit_nzd(e->rhs);
        site_arg(e->pos);
        buf_puts(out, ")");
        return;
    }
    case E_MEMBER:
        if (nzm(e, "min") || nzm(e, "max")) {
            bool mn = e->str[1] == 'i';
            buf_puts(out, "({ ");
            for (int k = 0; k < e->args.len; k++) { buf_printf(out, "double zm%d_%d_ = ", id, k); emit_nzd(e->args.items[k]); buf_puts(out, "; "); }
            buf_printf(out, "double zr%d_ = zm%d_0_; ", id, id);
            for (int k = 1; k < e->args.len; k++) buf_printf(out, "zr%d_ = %s(zr%d_, zm%d_%d_); ", id, mn ? "xs_fmin" : "xs_fmax", id, id, k);
            buf_printf(out, "zr%d_; })", id);
            return;
        }
        buf_printf(out, "xs_%s_d_s(", e->str); /* Math.trunc / ceil / round / floor: the double, range-checked */
        emit_expr(e->args.items[0]);
        site_arg(e->pos);
        buf_puts(out, ")");
        return;
    default: buf_puts(out, "((double)("); emit_expr(e); buf_puts(out, "))"); return;
    }
}
/* XB-087: the int locals initialized with a value that may be -0 and never written again (const, or a let nothing
   assigns, increments or captures): they keep the double beside them, so 1 / y and console.log(y) see the -0 */
static bool nzv_written(Stmt *s, const char *c, int in_arrow);
static bool nzv_written_e(Expr *e, const char *c, int in_arrow) {
    if (!e) return false;
    bool me = e->lhs && e->lhs->kind == E_IDENT && e->lhs->str && strcmp(e->lhs->str, c) == 0; /* (by name: any write to a
                                                                                                   variable of that name) */
    if ((e->kind == E_ASSIGN || e->kind == E_POSTFIX) && me) return true;
    if (e->kind == E_UNARY && (e->op == TK_PLUSPLUS || e->op == TK_MINUSMINUS) && me) return true;
    if (in_arrow && e->kind == E_IDENT && e->str && strcmp(e->str, c) == 0) return true; /* captured */
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn && nzv_written(e->arrow->fn->body, c, 1)) return true;
    if (nzv_written_e(e->lhs, c, in_arrow) || nzv_written_e(e->rhs, c, in_arrow)) return true;
    for (int i = 0; i < e->args.len; i++) if (nzv_written_e(e->args.items[i], c, in_arrow)) return true;
    return false;
}
static bool nzv_written(Stmt *s, const char *c, int in_arrow) {
    if (!s) return false;
    if (nzv_written_e(s->init, c, in_arrow) || nzv_written_e(s->expr, c, in_arrow) || nzv_written_e(s->cond, c, in_arrow) || nzv_written_e(s->for_update, c, in_arrow)) return true;
    if (nzv_written(s->for_init, c, in_arrow) || nzv_written(s->body, c, in_arrow) || nzv_written(s->else_body, c, in_arrow)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (nzv_written(s->stmts.items[i], c, in_arrow)) return true;
    for (int i = 0; i < s->ncases; i++) if (nzv_written_e(s->cases[i].value, c, in_arrow) || nzv_written(s->cases[i].body, c, in_arrow)) return true;
    return false;
}
static Stmt **nzv_root; static int nzv_nroot;
static void nzv_scan(Stmt *s) {
    if (!s) return;
    if (s->kind == S_VAR && s->cname && s->name && s->init && s->type && s->type->kind == TY_INT && !nzv_has(s->cname) &&
        strspn(s->cname, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == strlen(s->cname) && may_nz(s->init)) {
        bool w = false;
        for (int i = 0; i < nzv_nroot && !w; i++) w = nzv_written(nzv_root[i], s->name, 0);
        if (!w) {
            nzv = realloc(nzv, sizeof *nzv * (size_t)(nnzv + 1)); nzv[nnzv++] = s->cname;
            for (NzvSet *q = nzv_sets; q; q = q->next) if (q->owner == nzv_owner) { q->names = nzv; q->n = nnzv; }
        }
    }
    nzv_scan(s->for_init); nzv_scan(s->body); nzv_scan(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) nzv_scan(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) nzv_scan(s->cases[i].body);
}
static void nzv_compute(Func *owner, Stmt **stmts, int n) {
    NzvSet *z = NULL;
    for (NzvSet *q = nzv_sets; q; q = q->next) if (q->owner == owner) z = q;
    if (!z) { z = calloc(1, sizeof *z); z->owner = owner; z->next = nzv_sets; nzv_sets = z; }
    z->n = 0;
    nzv = NULL; nnzv = 0; nzv_owner = owner;
    if (getenv("XISTO_NO_NEGZERO") || (owner && (owner->is_gen || owner->is_async))) return;
    nzv_root = stmts; nzv_nroot = n;
    Func *saved = cur_func; cur_func = owner; /* (may_nz reads the ranges of this function, and the set being built) */
    z->names = NULL;
    for (int round = 0; round < 3; round++) {
        int before = nnzv;
        for (int i = 0; i < n; i++) nzv_scan(stmts[i]);
        z->names = nzv; z->n = nnzv;
        if (nnzv == before) break;
    }
    cur_func = saved;
}
/* top-level code (main): only its own locals; a top-level variable functions also use is not one of them */
static bool rv_main_only(const char *c);
/* Counters in a local int[] (XB-089): `const X = new Array<int>(N).fill(K)` (K a literal >= 0) and, later in the
   same block, the one counted loop `for (let j = A; j < B; j++)` that writes it, only by statements `X[e]++` at its
   own level (c of them, none in a nested loop); everywhere else X is only read. Every element is then at most
   K + c * (B - A) when the loop ends, so the increments add without the range test, and one test after the loop
   (on that bound: the elements are scanned only when it passes 2^53 - 1) raises the error at the first one's site.
   As the Map counters' deferred test (loop_hoist_enter): no try in the program, X a local, no return in the loop. */
typedef struct { Stmt *loop; const char *x; int64_t k; int c; Expr *first; bool armed; } CtrLoop;
static CtrLoop ctr_loops[8];
static int nctr_loops;
static Expr *ctr_inc_e[32];
static int ctr_inc_l[32], nctr_inc;
static bool ctr_expr_mentions(Expr *e, const char *x);
static bool ctr_stmt_mentions(Stmt *s, const char *x) {
    if (!s) return false;
    if ((s->kind == S_VAR && s->cname && strcmp(s->cname, x) == 0) || ctr_expr_mentions(s->init, x) || ctr_expr_mentions(s->expr, x) ||
        ctr_expr_mentions(s->cond, x) || ctr_expr_mentions(s->for_update, x)) return true;
    if (ctr_stmt_mentions(s->for_init, x) || ctr_stmt_mentions(s->body, x) || ctr_stmt_mentions(s->else_body, x)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (ctr_stmt_mentions(s->stmts.items[i], x)) return true;
    for (int i = 0; i < s->ncases; i++) if (ctr_expr_mentions(s->cases[i].value, x) || ctr_stmt_mentions(s->cases[i].body, x)) return true;
    return false;
}
static bool ctr_expr_mentions(Expr *e, const char *x) {
    if (!e) return false;
    if (e->kind == E_IDENT && e->cname && strcmp(e->cname, x) == 0) return true;
    if (e->kind == E_ARROW && e->arrow) { /* a closure that captures it */
        for (int i = 0; i < e->arrow->ncaptures; i++) if (e->arrow->captures[i].outer_cname && strcmp(e->arrow->captures[i].outer_cname, x) == 0) return true;
        if (e->arrow->fn && ctr_stmt_mentions(e->arrow->fn->body, x)) return true;
    }
    if (ctr_expr_mentions(e->lhs, x) || ctr_expr_mentions(e->rhs, x)) return true;
    for (int i = 0; i < e->args.len; i++) if (ctr_expr_mentions(e->args.items[i], x)) return true;
    return false;
}
/* `X[e]++` as a statement, e not mentioning X */
static bool ctr_is_inc(Stmt *s, const char *x) {
    if (!s || s->kind != S_EXPR || !s->expr) return false;
    Expr *e = s->expr;
    if (e->kind != E_POSTFIX || e->op != TK_PLUSPLUS || !e->type || e->type->kind != TY_INT) return false;
    Expr *ix = e->lhs;
    return ix && ix->kind == E_INDEX && !ix->builtin.cfn && ix->lhs && ix->lhs->kind == E_IDENT && ix->lhs->cname &&
           strcmp(ix->lhs->cname, x) == 0 && !ctr_expr_mentions(ix->rhs, x);
}
/* some `X[e]++` statement in s */
static bool ctr_has_inc(Stmt *s, const char *x) {
    if (!s) return false;
    if (ctr_is_inc(s, x)) return true;
    if (ctr_has_inc(s->for_init, x) || ctr_has_inc(s->body, x) || ctr_has_inc(s->else_body, x)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (ctr_has_inc(s->stmts.items[i], x)) return true;
    for (int i = 0; i < s->ncases; i++) if (ctr_has_inc(s->cases[i].body, x)) return true;
    return false;
}
/* the loop's body: X only in `X[e]++` statements, none inside a nested loop; counts them, notes them */
static bool ctr_body_ok(Stmt *s, const char *x, int *c, Expr **incs, int *ni, bool nested) {
    if (!s) return true;
    if (ctr_is_inc(s, x)) {
        if (nested || *ni >= 8) return false;
        incs[(*ni)++] = s->expr;
        (*c)++;
        return true;
    }
    if (s->kind == S_EXPR || s->kind == S_RETURN || s->kind == S_THROW || s->kind == S_YIELD) return !ctr_expr_mentions(s->expr, x);
    if (s->kind == S_VAR) return !ctr_expr_mentions(s->init, x);
    if (ctr_expr_mentions(s->cond, x) || ctr_expr_mentions(s->for_update, x) || ctr_expr_mentions(s->expr, x)) return false;
    bool loop = s->kind == S_FOR || s->kind == S_WHILE || s->kind == S_FOROF;
    if (loop && ctr_stmt_mentions(s, x)) return false;
    if (!ctr_body_ok(s->for_init, x, c, incs, ni, nested) || !ctr_body_ok(s->body, x, c, incs, ni, nested) || !ctr_body_ok(s->else_body, x, c, incs, ni, nested)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!ctr_body_ok(s->stmts.items[i], x, c, incs, ni, nested)) return false;
    for (int i = 0; i < s->ncases; i++) if (ctr_expr_mentions(s->cases[i].value, x) || !ctr_body_ok(s->cases[i].body, x, c, incs, ni, nested)) return false;
    return true;
}
/* outside the loop: X only read (garr_expr with no element writes), the loop itself skipped */
static bool garr_expr(Expr *e, Expr *parent, bool target, const char *c, bool elem_w);
static bool ctr_reads_only(Stmt *s, const char *x, Stmt *skip) {
    if (!s || s == skip) return true;
    if (!garr_expr(s->init, NULL, false, x, false) || !garr_expr(s->expr, NULL, false, x, false) || !garr_expr(s->cond, NULL, false, x, false) ||
        !garr_expr(s->for_update, NULL, false, x, false)) return false;
    if (!ctr_reads_only(s->for_init, x, skip) || !ctr_reads_only(s->body, x, skip) || !ctr_reads_only(s->else_body, x, skip)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!ctr_reads_only(s->stmts.items[i], x, skip)) return false;
    for (int i = 0; i < s->ncases; i++) if (!garr_expr(s->cases[i].value, NULL, false, x, false) || !ctr_reads_only(s->cases[i].body, x, skip)) return false;
    return true;
}
static bool bce_pure(Expr *e);
static bool stmt_has_return(Stmt *s);
/* a counted loop for (let j = A; j < B; j++): A pure and B a name or a number, none of their names written in it */
static bool ctr_counted(Stmt *f) {
    if (!f || f->kind != S_FOR || !f->for_init || f->for_init->kind != S_VAR || !f->for_init->cname || !f->for_init->init) return false;
    const char *j = f->for_init->cname;
    Expr *u = f->for_update, *c = f->cond;
    if (!u || u->kind != E_POSTFIX || u->op != TK_PLUSPLUS || !u->lhs || u->lhs->kind != E_IDENT || !u->lhs->cname || strcmp(u->lhs->cname, j) != 0) return false;
    if (!c || c->kind != E_BINARY || c->op != TK_LT || !c->lhs || c->lhs->kind != E_IDENT || !c->lhs->cname || strcmp(c->lhs->cname, j) != 0) return false;
    Expr *b = c->rhs, *a = f->for_init->init;
    if (!b || !b->type || b->type->kind != TY_INT || !a->type || a->type->kind != TY_INT || !bce_pure(a)) return false;
    if (b->kind == E_IDENT) { if (!b->cname || bce_writes(f->body, b->cname)) return false; }
    else if (b->kind != E_NUMBER) return false;
    if (bce_writes(f->body, j)) return false;
    /* A's names: not written in the loop */
    Expr *st[16]; int ns = 0; st[ns++] = a;
    while (ns) {
        Expr *e = st[--ns];
        if (!e) continue;
        if (e->kind == E_IDENT && e->cname && bce_writes(f->body, e->cname)) return false;
        if (ns + 2 < 16) { st[ns++] = e->lhs; st[ns++] = e->rhs; } else return false;
    }
    return true;
}
static void ctr_scan(Stmt *s, Stmt **all, int n) {
    if (!s) return;
    for (int i = 0; i < s->stmts.len; i++) {
        Stmt *d = s->stmts.items[i];
        if (nctr_loops >= 8 || !d || d->kind != S_VAR || !d->is_const || !d->cname || d->cell || d->cname[0] != 'v' || !d->init || !d->type ||
            d->type->kind != TY_ARRAY || !d->type->elem || d->type->elem->kind != TY_INT) continue;
        Expr *e = d->init;
        if (e->kind != E_MEMBER || !e->is_call || !e->str || strcmp(e->str, "fill") != 0 || e->args.len != 1 || !e->lhs || e->lhs->kind != E_NEW_ARRAY) continue;
        Expr *kv = e->args.items[0];
        if (kv->kind != E_NUMBER || !kv->is_int_lit || kv->ival < 0) continue;
        Stmt *loop = NULL; /* the one later sibling that mentions X: a counted loop */
        bool bad = false;
        for (int k = i + 1; k < s->stmts.len && !bad; k++) {
            Stmt *t = s->stmts.items[k];
            if (ctr_counted(t) && ctr_has_inc(t->body, d->cname)) { if (loop) bad = true; else loop = t; }
        }
        if (getenv("XISTO_CTR_WHY")) fprintf(stderr, "ctr %s loop=%d bad=%d\n", d->cname, loop != NULL, bad);
        if (bad || !loop || stmt_has_return(loop->body)) continue;
        int c = 0, ni = 0;
        Expr *incs[8];
        if (!ctr_body_ok(loop->body, d->cname, &c, incs, &ni, false) || c == 0) { if (getenv("XISTO_CTR_WHY")) fprintf(stderr, "ctr %s body c=%d\n", d->cname, c); continue; }
        if (ctr_expr_mentions(loop->cond, d->cname) || ctr_expr_mentions(loop->for_update, d->cname) || ctr_stmt_mentions(loop->for_init, d->cname)) continue;
        bool reads = true;
        for (int k = 0; k < n && reads; k++) reads = ctr_reads_only(all[k], d->cname, loop);
        if (!reads) { if (getenv("XISTO_CTR_WHY")) fprintf(stderr, "ctr %s reads\n", d->cname); continue; }
        int li = nctr_loops++;
        ctr_loops[li] = (CtrLoop){loop, d->cname, kv->ival, c, incs[0], false};
        for (int q = 0; q < ni && nctr_inc < 32; q++) { ctr_inc_e[nctr_inc] = incs[q]; ctr_inc_l[nctr_inc++] = li; }
    }
    ctr_scan(s->for_init, all, n);
    ctr_scan(s->body, all, n);
    ctr_scan(s->else_body, all, n);
    for (int i = 0; i < s->stmts.len; i++) ctr_scan(s->stmts.items[i], all, n);
    for (int i = 0; i < s->ncases; i++) ctr_scan(s->cases[i].body, all, n);
}
static void ctr_compute(Stmt **stmts, int n) {
    nctr_loops = nctr_inc = 0;
    if (xs_debug || sm_mode || prog_try || getenv("XISTO_NO_ARR_COUNTERS")) return;
    for (int i = 0; i < n; i++) ctr_scan(stmts[i], stmts, n);
}
/* an increment the loop's bound covers, while that loop is being emitted */
static bool ctr_inc_free(Expr *e) {
    for (int i = 0; i < nctr_inc; i++) if (ctr_inc_e[i] == e) return ctr_loops[ctr_inc_l[i]].armed;
    return false;
}
static int ctr_loop_of(Stmt *s) {
    for (int i = 0; i < nctr_loops; i++) if (ctr_loops[i].loop == s) return i;
    return -1;
}
/* the range one definition gives its variable k (rv_compute) */
static Rng rv_def_rng(int d, int k) {
    Rng x;
    if (rv_defs[d].loop) { /* i from its start below the bound: [start, bound - 1] */
        Rng b = rng_of(rv_defs[d].loop->cond->rhs);
        x = b.ok ? (Rng){true, -RNG_SAFE, b.hi - 1} : rng_none();
        /* a counter declared by the loop itself is only read inside it, where it is below the bound: its
           start does not widen the top (j = i + 1 in `for (let j = i + 1; j < N; j++)` stays under N) */
        Stmt *fi = rv_defs[d].loop->for_init;
        bool own = fi && fi->kind == S_VAR && fi->cname && strcmp(fi->cname, rv_name[k]) == 0;
        if (x.ok) { x.lo = INT64_MAX; for (int e = 0; e < rv_ndefs; e++) if (strcmp(rv_defs[e].name, rv_name[k]) == 0 && rv_defs[e].def) { Rng s0 = rng_of(rv_defs[e].def); if (!s0.ok) { x = rng_none(); break; } if (s0.lo < x.lo) x.lo = s0.lo; if (s0.hi > x.hi && !own) x.hi = s0.hi; } }
        if (x.ok && own) { Rng s0 = rng_of(fi->init); if (!s0.ok) x = rng_none(); else if (s0.lo < x.lo) x.lo = s0.lo; }
        if (x.ok && x.lo == INT64_MAX) x = rng_none();
        if (x.ok && x.lo > x.hi) x.lo = x.hi; /* (a loop that never runs: nothing reads it) */
    } else x = rng_of(rv_defs[d].def);
    return x;
}
/* the definition only copies another tracked variable whose range is not known (yet) */
static bool rv_copy_pending(Expr *def, int k) {
    if (!def || def->kind != E_IDENT || !def->cname) return false;
    int j = rv_find(def->cname);
    return j >= 0 && j != k && !rv_rng[j].ok;
}
static void rv_compute(Func *owner, Stmt **stmts, int n) {
    rv_n = rv_ndefs = 0;
    rv_owner = owner;
    rv_valid = false;
    nbce_fix_l = 0;
    bce_fix_owner = owner;
    nfx_l = 0;
    fx_owner = owner;
    ctr_compute(stmts, n);
    for (int i = 0; i < n; i++) bce_fix_scan(stmts[i]);
    for (int i = 0; i < n; i++) bce_fix_arr_scan(stmts[i], stmts, n);
    if (owner && owner->fixparam) /* typed-array parameters every call passes with one length (fixparam_compute) */
        for (int i = 0; i < owner->nparams && nbce_fix_l < 32; i++)
            if (owner->fixparam[i] > 0 && owner->params[i].cname) bce_fix_l[nbce_fix_l++] = (BceFix){owner->params[i].cname, owner->fixparam[i]};
    if (xs_debug || getenv("XISTO_NO_RANGES")) return;
    for (int i = 0; i < n; i++) rv_scan_stmt(stmts[i], true);
    if (!owner) { int k = 0; for (int i = 0; i < rv_n; i++) if (rv_main_only(rv_name[i])) { rv_name[k] = rv_name[i]; rv_rng[k++] = rv_rng[i]; } rv_n = k; }
    bool *top = calloc((size_t)(rv_n ? rv_n : 1), sizeof(bool));
    for (int d = 0; d < rv_ndefs; d++) { int k = rv_find(rv_defs[d].name); if (k >= 0 && rv_defs[d].top) top[k] = true; }
    /* a counter the loop declares (`for (let j = i + 1; ...)`): its start is folded into the loop's own definition
       (below the bound), so the declaration is not a definition of its own */
    for (int d = 0; d < rv_ndefs; d++) {
        Stmt *lp = rv_defs[d].loop, *fi = lp ? lp->for_init : NULL;
        if (!fi || fi->kind != S_VAR || !fi->cname || strcmp(fi->cname, rv_defs[d].name) != 0) continue;
        for (int e = 0; e < rv_ndefs; e++) if (e != d && !rv_defs[e].loop && rv_defs[e].def == fi->init && strcmp(rv_defs[e].name, fi->cname) == 0) rv_defs[e].def = NULL, rv_defs[e].name = "";
    }
    rv_valid = true; /* rng_of reads the table while it is being computed */
    /* optimistic: a definition that only copies a variable not known yet (`key = start` where `const start = key`)
       is left out until that variable is known; the ranges found are then checked to be inductive (every definition
       within its variable's range), and a variable that fails knows nothing, until nothing fails */
    bool optimistic = !getenv("XISTO_NO_RANGE_CYCLES");
    for (int round = 0; round < 6; round++) {
        bool moved = false;
        for (int k = 0; k < rv_n; k++) {
            if (top[k]) { rv_rng[k] = rng_none(); continue; }
            Rng r = {false, 0, 0};
            bool first = true, ok = true;
            for (int d = 0; d < rv_ndefs && ok; d++) {
                if (strcmp(rv_defs[d].name, rv_name[k]) != 0) continue;
                Rng x = rv_def_rng(d, k);
                if (!x.ok && optimistic && rv_copy_pending(rv_defs[d].def, k)) continue;
                if (!x.ok) { ok = false; break; }
                r = first ? x : rng_union(r, x);
                first = false;
            }
            Rng nr = ok && !first ? r : rng_none();
            if (nr.ok != rv_rng[k].ok || nr.lo != rv_rng[k].lo || nr.hi != rv_rng[k].hi) moved = true;
            rv_rng[k] = nr;
        }
        if (!moved) break;
        if (round == 5) for (int k = 0; k < rv_n; k++) rv_rng[k] = rng_none(); /* not settled: know nothing */
    }
    for (bool failed = optimistic; failed;) { /* inductive: each definition within its variable's range */
        failed = false;
        for (int k = 0; k < rv_n; k++) {
            if (!rv_rng[k].ok) continue;
            for (int d = 0; d < rv_ndefs; d++) {
                if (strcmp(rv_defs[d].name, rv_name[k]) != 0) continue;
                Rng x = rv_def_rng(d, k);
                if (!x.ok || x.lo < rv_rng[k].lo || x.hi > rv_rng[k].hi) { rv_rng[k] = rng_none(); failed = true; break; }
            }
        }
    }
    /* never negative: start from every variable with known definitions and drop the ones a definition can make
       negative, until nothing moves (`key = (key % top) * 4 + seq[j]` from `key = 0`: the key of k-nucleotide). A
       counted loop's counter only goes up from its start, so the start decides */
    for (int k = 0; k < rv_n; k++) rv_nn[k] = !top[k] && !getenv("XISTO_NO_NONNEG");
    for (bool moved = true; moved;) {
        moved = false;
        for (int k = 0; k < rv_n; k++) {
            if (!rv_nn[k]) continue;
            bool any = false;
            for (int d = 0; d < rv_ndefs; d++) {
                if (strcmp(rv_defs[d].name, rv_name[k]) != 0 || rv_defs[d].loop) continue;
                if (!rv_defs[d].def || !nn_expr(rv_defs[d].def)) { rv_nn[k] = false; moved = true; break; }
                any = true;
            }
            if (rv_nn[k] && !any) { rv_nn[k] = false; moved = true; }
        }
    }
    free(top);
}

/* ---- index checks the loop already makes (XB-006) ----
   In `for (...; i < a.length; i++)` (a counted loop, i never written in the body), a[i] is within a when i
   starts at 0 or more and nothing in the body can change a's length: a appears only as a[...] or a.length, and
   the body calls no user code, makes no closure and calls no method that resizes an array (another name for
   the same array could otherwise shrink it). Such a[i] is read and written without the check. */
typedef struct { const char *i, *a; bool guarded; Expr *idx; } BceLoop; /* guarded: a versioned loop's guard proved i >= 0;
   idx: that exact index expression (a[i + E], a[E - i]) is within a */
static BceLoop bce_loops[16];
static int nbce;
/* a is a Float64Array or a Uint8Array in a local of its own: its length never changes, and nothing but an
   assignment to a in the body can make a name another array, so calls and other uses of a do not matter
   (bce_ok). (Calls are allowed only when the names the guard reads are plain locals too, which code called
   cannot change.) */
static bool bce_fixed_len;
static bool bce_bytes(Type *t) { return t && t->kind == TY_OBJECT && t->cls && t->cls->builtin && t->cls->cname && strcmp(t->cls->cname, "xs_Uint8Array") == 0; }
static bool bce_body_ok(Expr *e, Expr *parent, const char *a);
static bool bce_stmt_ok(Stmt *s, const char *a) {
    if (!s) return true;
    if (s->kind == S_YIELD || s->kind == S_TRY) return false;
    if (!bce_body_ok(s->init, NULL, a) || !bce_body_ok(s->expr, NULL, a) || !bce_body_ok(s->cond, NULL, a) || !bce_body_ok(s->for_update, NULL, a)) return false;
    if (s->kind == S_FOROF && s->expr && s->expr->kind == E_IDENT && s->expr->cname && strcmp(s->expr->cname, a) == 0) return false;
    if (!bce_stmt_ok(s->for_init, a) || !bce_stmt_ok(s->body, a) || !bce_stmt_ok(s->else_body, a)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!bce_stmt_ok(s->stmts.items[i], a)) return false;
    for (int i = 0; i < s->ncases; i++) if (!bce_body_ok(s->cases[i].value, NULL, a) || !bce_stmt_ok(s->cases[i].body, a)) return false;
    return true;
}
static bool bce_body_ok(Expr *e, Expr *parent, const char *a) {
    if (!e) return true;
    switch (e->kind) {
    case E_ARROW: case E_AWAIT: return false;
    case E_CALL: /* user code: a function, or a closure value (which may change a captured limit, or the array) */
        if ((e->func || (e->lhs && e->lhs->type && e->lhs->type->kind == TY_FUNC && !e->builtin.cfn)) && !bce_fixed_len) return false;
        break;
    case E_NEW_OBJ: if (e->cls && !e->cls->builtin && !bce_fixed_len) return false; break;
    case E_MEMBER:
        if (e->is_call && e->func && !bce_fixed_len) return false;
        if (e->is_call && e->str && !bce_fixed_len) {
            static const char *const resize[] = {"push", "pop", "shift", "unshift", "splice", "fill", "sort", "reverse", "copyWithin", "set", "delete", "clear"};
            for (int i = 0; i < 12; i++) if (strcmp(e->str, resize[i]) == 0) return false;
        }
        break;
    case E_IDENT:
        if (e->cname && strcmp(e->cname, a) == 0 && !bce_fixed_len) {
            bool as_base = parent && parent->kind == E_INDEX && parent->lhs == e;
            bool as_len = parent && parent->kind == E_MEMBER && !parent->is_call && parent->lhs == e && parent->str && strcmp(parent->str, "length") == 0;
            if (!as_base && !as_len) return false;
        }
        break;
    default: break;
    }
    if (e->kind == E_ASSIGN && e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname && strcmp(e->lhs->cname, a) == 0) return false;
    if (!bce_body_ok(e->lhs, e, a) || !bce_body_ok(e->rhs, e, a)) return false;
    for (int i = 0; i < e->args.len; i++) if (!bce_body_ok(e->args.items[i], e, a)) return false;
    return true;
}
/* the indexable types the checks are dropped for: arrays, Float64Array (the same ->len and ->data) and
   Uint8Array (bytes: ->f_length, xs_u8_at_nc) */
static bool bce_indexable(Type *t) { return t && (t->kind == TY_ARRAY || is_f64_type(t) || bce_bytes(t)); }
static bool bce_plain_local(const char *cname) { return cname && strncmp(cname, "v_", 2) == 0; } /* (not a global, nor a cell) */
static bool bce_ok(Stmt *body, const char *a, Type *t, bool calls_ok) {
    bce_fixed_len = calls_ok && (is_f64_type(t) || bce_bytes(t)) && bce_plain_local(a);
    bool ok = bce_stmt_ok(body, a);
    bce_fixed_len = false;
    return ok;
}
static void bce_enter(Stmt *s) {
    Expr *u = s->for_update, *c = s->cond;
    if (nbce == 16 || xs_debug || sm_mode || getenv("XISTO_NO_BCE")) return; /* (a generator: the consumer runs between yields) */
    if (!u || !u->builtin.cfn || strcmp(u->builtin.cfn, "counted_inc") != 0) return;
    Expr *len = c->rhs;
    if (!len || len->kind != E_MEMBER || len->is_call || !len->str || strcmp(len->str, "length") != 0) return;
    if (!len->lhs || len->lhs->kind != E_IDENT || !len->lhs->cname || !bce_indexable(len->lhs->type)) return;
    const char *a = len->lhs->cname;
    if (!bce_ok(s->body, a, len->lhs->type, true)) return;
    bce_loops[nbce++] = (BceLoop){u->lhs->cname, a};
}
/* Loop versioning, for the counted loops bce_enter does not cover: `for (...; i < LIM; i++)` with LIM a literal
   or a name the body does not write, and a[i] for arrays a the body cannot resize (bce_stmt_ok). The loop is
   emitted twice: without the checks of those a[i], run when LIM <= a.length for each of them (so every i the
   loop reaches is within each a), and as it was otherwise, which fails where it failed. */
static bool bce_writes(Stmt *s, const char *n);
static bool bce_writes_e(Expr *e, const char *n) {
    if (!e) return false;
    if ((e->kind == E_ASSIGN || e->kind == E_POSTFIX || (e->kind == E_UNARY && (e->op == TK_PLUSPLUS || e->op == TK_MINUSMINUS))) &&
        e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname && strcmp(e->lhs->cname, n) == 0) return true;
    if (e->kind == E_ARROW) return true;
    if (bce_writes_e(e->lhs, n) || bce_writes_e(e->rhs, n)) return true;
    for (int i = 0; i < e->args.len; i++) if (bce_writes_e(e->args.items[i], n)) return true;
    return false;
}
static bool bce_writes(Stmt *s, const char *n) {
    if (!s) return false;
    if (bce_writes_e(s->init, n) || bce_writes_e(s->expr, n) || bce_writes_e(s->cond, n) || bce_writes_e(s->for_update, n)) return true;
    if (bce_writes(s->for_init, n) || bce_writes(s->body, n) || bce_writes(s->else_body, n)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (bce_writes(s->stmts.items[i], n)) return true;
    for (int i = 0; i < s->ncases; i++) if (bce_writes(s->cases[i].body, n)) return true;
    return false;
}
/* the length of an array, a Float64Array or bytes (a Uint8Array: its length never changes), as C */
static Type *bce_arr_t[8]; /* bce_version_guard: the types of the arrays in arrs */
static const char *bce_len_c(const char *a, Type *t) { return xs_sprintf(bce_bytes(t) ? "(%s)->f_length" : "(int64_t)(%s)->len", ren(a)); }
/* the same pure expression (bce_pure: names, numbers, + and -) */
static bool bce_same(Expr *a, Expr *b) {
    if (!a || !b || a->kind != b->kind) return false;
    if (a->kind == E_NUMBER) return a->is_int_lit && b->is_int_lit && a->ival == b->ival;
    if (a->kind == E_IDENT) return a->cname && b->cname && strcmp(a->cname, b->cname) == 0;
    if (a->kind == E_BINARY) return a->op == b->op && bce_same(a->lhs, b->lhs) && bce_same(a->rhs, b->rhs);
    return false;
}
/* a[i + E], a[E + i], a[i - E], a[E - i] with E pure and not written in the loop (bce_version_guard) */
typedef struct { const char *a; Expr *idx, *E; int sign; Type *t; } BceAff; /* index = sign * i + E (sign -1 or 1, 2: i - E) */
static BceAff bce_aff[16];
static int nbce_aff;
static bool bce_pure(Expr *e);
static Stmt *bce_cur_body;
static void bce_affine_e(Expr *e, const char *i) {
    if (!e) return;
    if (e->kind == E_INDEX && e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname && bce_indexable(e->lhs->type) &&
        e->rhs && e->rhs->kind == E_BINARY && (e->rhs->op == TK_PLUS || e->rhs->op == TK_MINUS) && nbce_aff < 16) {
        Expr *l = e->rhs->lhs, *r = e->rhs->rhs, *E = NULL;
        int sign = 0;
        bool li = l && l->kind == E_IDENT && l->cname && strcmp(l->cname, i) == 0;
        bool ri = r && r->kind == E_IDENT && r->cname && strcmp(r->cname, i) == 0;
        if (li && !ri) { E = r; sign = 1; }                                   /* i + E, i - E */
        else if (ri && !li) { E = l; sign = e->rhs->op == TK_PLUS ? 1 : -1; } /* E + i, E - i */
        bool inv = E && bce_pure(E);
        if (inv && E->kind == E_IDENT) inv = !bce_writes(bce_cur_body, E->cname);
        if (inv && E->kind == E_BINARY) inv = false; /* (names only, or numbers: what the guard reads) */
        if (inv && strcmp(e->lhs->cname, i) != 0) bce_aff[nbce_aff++] = (BceAff){e->lhs->cname, e->rhs, E, li && e->rhs->op == TK_MINUS ? 2 : sign, e->lhs->type};
    }
    bce_affine_e(e->lhs, i);
    bce_affine_e(e->rhs, i);
    for (int k = 0; k < e->args.len; k++) bce_affine_e(e->args.items[k], i);
}
static void bce_affine(Stmt *s, const char *i) {
    if (!s) return;
    bce_affine_e(s->init, i); bce_affine_e(s->expr, i); bce_affine_e(s->cond, i); bce_affine_e(s->for_update, i);
    bce_affine(s->for_init, i); bce_affine(s->body, i); bce_affine(s->else_body, i);
    for (int k = 0; k < s->stmts.len; k++) bce_affine(s->stmts.items[k], i);
    for (int k = 0; k < s->ncases; k++) bce_affine(s->cases[k].body, i);
}
static void bce_arrays_e(Expr *e, const char *i, const char **arrs, Type **ts, int *na) {
    if (!e) return;
    if (e->kind == E_INDEX && e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname && bce_indexable(e->lhs->type) &&
        e->rhs && e->rhs->kind == E_IDENT && e->rhs->cname && strcmp(e->rhs->cname, i) == 0) {
        bool seen = false;
        for (int k = 0; k < *na; k++) if (strcmp(arrs[k], e->lhs->cname) == 0) seen = true;
        if (!seen && *na < 8) { ts[*na] = e->lhs->type; arrs[(*na)++] = e->lhs->cname; }
    }
    bce_arrays_e(e->lhs, i, arrs, ts, na);
    bce_arrays_e(e->rhs, i, arrs, ts, na);
    for (int k = 0; k < e->args.len; k++) bce_arrays_e(e->args.items[k], i, arrs, ts, na);
}
static void bce_arrays(Stmt *s, const char *i, const char **arrs, Type **ts, int *na) {
    if (!s) return;
    bce_arrays_e(s->init, i, arrs, ts, na); bce_arrays_e(s->expr, i, arrs, ts, na); bce_arrays_e(s->cond, i, arrs, ts, na); bce_arrays_e(s->for_update, i, arrs, ts, na);
    bce_arrays(s->for_init, i, arrs, ts, na); bce_arrays(s->body, i, arrs, ts, na); bce_arrays(s->else_body, i, arrs, ts, na);
    for (int k = 0; k < s->stmts.len; k++) bce_arrays(s->stmts.items[k], i, arrs, ts, na);
    for (int k = 0; k < s->ncases; k++) bce_arrays(s->cases[k].body, i, arrs, ts, na);
}
/* no calls, assignments or checks that could fail: numbers, names and + - * of them */
static bool bce_pure(Expr *e) {
    if (!e) return false;
    if (e->kind == E_NUMBER) return e->is_int_lit;
    if (e->kind == E_IDENT) return e->type && e->type->kind == TY_INT;
    if (e->kind == E_BINARY && (e->op == TK_PLUS || e->op == TK_MINUS)) return bce_pure(e->lhs) && bce_pure(e->rhs);
    return false;
}
/* a name declared inside the loop's body (a const, a for...of variable, a caught error): the guard before the
   loop cannot read it */
static bool bce_declares(Stmt *s, const char *n) {
    if (!s || !n) return false;
    if ((s->kind == S_VAR || s->kind == S_FOROF || s->kind == S_TRY) && s->cname && strcmp(s->cname, n) == 0) return true;
    if (bce_declares(s->for_init, n) || bce_declares(s->body, n) || bce_declares(s->else_body, n)) return true;
    for (int k = 0; k < s->stmts.len; k++) if (bce_declares(s->stmts.items[k], n)) return true;
    for (int k = 0; k < s->ncases; k++) if (bce_declares(s->cases[k].body, n)) return true;
    return false;
}
static int bce_versions; /* versioned loops around the one being emitted (two levels at most: 4 copies) */
/* the guard of a versioned loop ("" when the loop is not one), and its arrays in arrs */
static char *bce_version_guard(Stmt *s, const char **arrs, int *na) {
    *na = 0;
    Expr *u = s->for_update, *c = s->cond;
    if (nbce + 8 > 16 - 4 || bce_versions >= 2 || xs_debug || sm_mode || ictx || getenv("XISTO_NO_BCE")) return NULL;
    if (!u || !u->builtin.cfn || strcmp(u->builtin.cfn, "counted_inc") != 0 || !u->lhs || !u->lhs->cname) return NULL;
    if (!c || c->kind != E_BINARY || (c->op != TK_LT && c->op != TK_LE) || !c->lhs || c->lhs->kind != E_IDENT || !c->lhs->cname || strcmp(c->lhs->cname, u->lhs->cname) != 0) return NULL;
    bool le = c->op == TK_LE; /* i <= LIM: the last i is LIM */
    Expr *lim = c->rhs;
    if (!lim || !lim->type || lim->type->kind != TY_INT) return NULL;
    if (lim->kind == E_IDENT) { if (!lim->cname || bce_writes(s->body, lim->cname)) return NULL; }
    else if (lim->kind != E_NUMBER) return NULL;
    const char *i = u->lhs->cname;
    /* the first value: a pure expression of names and numbers (checked >= 0 by the guard), from the declaration */
    Stmt *fi = s->for_init;
    if (!fi || fi->kind != S_VAR || !fi->cname || strcmp(fi->cname, i) != 0 || !fi->init || !bce_pure(fi->init)) return NULL;
    const char *cand[8];
    Type *ct[8];
    int nc = 0;
    bce_arrays(s->body, i, cand, ct, &nc);
    bool plain = lim->kind == E_NUMBER || bce_plain_local(lim->cname);
    Rng ir = rng_of(u->lhs); /* the counter's range: a[i] on an array of a known length within it needs no version (bce_safe) */
    for (int k = 0; k < nc; k++) {
        int64_t fixed = bce_fixed_n(cand[k]);
        if (fixed > 0 && ir.ok && ir.lo >= 0 && ir.hi < fixed) continue;
        if (bce_declares(s->body, cand[k])) continue;
        if (bce_ok(s->body, cand[k], ct[k], plain) && strcmp(cand[k], i) != 0) { bce_arr_t[*na] = ct[k]; arrs[(*na)++] = cand[k]; }
    }
    nbce_aff = 0;
    bce_cur_body = s->body;
    bce_affine(s->body, i);
    int keep = 0;
    for (int k = 0; k < nbce_aff; k++) {
        Expr *E = bce_aff[k].E;
        if (bce_declares(s->body, bce_aff[k].a) || (E->kind == E_IDENT && bce_declares(s->body, E->cname))) continue;
        if (bce_ok(s->body, bce_aff[k].a, bce_aff[k].t, plain && (E->kind == E_NUMBER || bce_plain_local(E->cname)))) bce_aff[keep++] = bce_aff[k];
    }
    nbce_aff = keep;
    if (!*na && !nbce_aff) return NULL;
    bool ar = false, ar2 = false;
    char *l = emit_expr_to_str(lim, &ar);
    char *st = emit_expr_to_str(fi->init, &ar2);
    if (ar || ar2) return NULL;
    Buf g = {0};
    /* i runs over [st, last]: last = LIM - 1 (i < LIM) or LIM (i <= LIM); the loop runs only when st <= last */
    char *last = le ? xs_sprintf("(%s)", l) : xs_sprintf("((%s) - 1)", l);
    buf_printf(&g, "(%s) >= 0", st);
    for (int k = 0; k < *na; k++) buf_printf(&g, " && %s < %s", last, bce_len_c(arrs[k], bce_arr_t[k]));
    for (int k = 0; k < nbce_aff; k++) {
        bool dup = false; /* a[p0 - i] read and written: the same clause once */
        for (int j = 0; j < k && !dup; j++) dup = strcmp(bce_aff[j].a, bce_aff[k].a) == 0 && bce_aff[j].sign == bce_aff[k].sign && bce_same(bce_aff[j].E, bce_aff[k].E);
        if (dup) continue;
        bool ar3 = false;
        char *E = emit_expr_to_str(bce_aff[k].E, &ar3);
        const char *A = bce_len_c(bce_aff[k].a, bce_aff[k].t);
        if (bce_aff[k].sign == 1) buf_printf(&g, " && (%s) + (%s) >= 0 && %s + (%s) < %s", st, E, last, E, A);        /* i + E */
        else if (bce_aff[k].sign == 2) buf_printf(&g, " && (%s) - (%s) >= 0 && %s - (%s) < %s", st, E, last, E, A); /* i - E */
        else buf_printf(&g, " && (%s) - %s >= 0 && (%s) - (%s) < %s", E, last, E, st, A);                         /* E - i */
    }
    return g.data;
}
/* a[i] that needs no check: the index and the array of an enclosing safe counted loop, i >= 0 */
static bool bce_safe(Expr *e) {
    if (!e || e->kind != E_INDEX || !e->lhs || e->lhs->kind != E_IDENT || !e->rhs || ictx) return false;
    if (e->lhs->cname && bce_indexable(e->lhs->type)) { /* an array of a known length, an index whose range is within it */
        int64_t n = bce_fixed_n(e->lhs->cname);
        Rng r = n > 0 ? rng_of(e->rhs) : rng_none();
        if (r.ok && r.lo >= 0 && r.hi < n) return true;
    }
    for (int k = nbce - 1; k >= 0; k--) /* an affine index a versioned loop's guard proved within the array */
        if (bce_loops[k].idx == e->rhs && strcmp(bce_loops[k].a, e->lhs->cname) == 0) return true;
    if (e->rhs->kind != E_IDENT) return false;
    for (int k = nbce - 1; k >= 0; k--)
        if (!bce_loops[k].idx && strcmp(bce_loops[k].i, e->rhs->cname) == 0 && strcmp(bce_loops[k].a, e->lhs->cname) == 0) {
            if (bce_loops[k].guarded) return true;
            Rng r = rng_of(e->rhs);
            return r.ok && r.lo >= 0;
        }
    return false;
}

/* the index of an a[i] bce_safe allows, unchecked */
static void emit_bce_index(Expr *i) {
    if (i->kind == E_BINARY && (i->op == TK_PLUS || i->op == TK_MINUS) && i->type && i->type->kind == TY_INT) {
        /* an affine index the guard proved within the array (or a sum whose range is known): no overflow check */
        buf_puts(out, "(");
        emit_expr(i->lhs);
        buf_puts(out, i->op == TK_PLUS ? ") + (" : ") - (");
        emit_expr(i->rhs);
        buf_puts(out, ")");
    } else emit_expr(i);
}

/* a for statement (S_FOR); emit_stmt_ marks its counted increment and its checked-index loops first */
static void emit_block_kind(Stmt *b, ScopeKind kind);
static void emit_var(Stmt *s);
static bool reuse_assign(Expr *e);
static void emit_new_at_signature(Class *c);
static char *emit_expr_to_str(Expr *e, bool *made_ar);
static void drain_if_needed(void);
static void emit_for_(Stmt *s) {
    buf_puts(out, "{");
    indent++;
    epush(false);
    if (s->for_init) {
        nl();
        int saved_rg = cur_rg;
        cur_rg = 0; /* (runs before the first iteration: the region flag is off) */
        if (s->for_init->kind == S_VAR) emit_var(s->for_init);
        else { emit_expr(s->for_init->expr); buf_puts(out, ";"); }
        cur_rg = saved_rg;
        drain_if_needed();
    }
    nl();
    {
        bool cond_ar = false, upd_ar = false;
        char *cond = s->cond ? emit_expr_to_str(s->cond, &cond_ar) : "";
        char *upd = s->for_update ? emit_expr_to_str(s->for_update, &upd_ar) : "";
        if (!cond_ar && !upd_ar) {
            buf_printf(out, "for (; %s; %s) ", cond, upd);
            emit_block_kind(s->body, SC_LOOP_BODY);
        } else { /* drain what the condition and the update made, once per iteration */
            buf_printf(out, "for (;; %s) { bool cond_ = (%s); xs_ar_drain(ar_mark); if (!cond_) break; ", upd, s->cond ? cond : "1");
            emit_block_kind(s->body, SC_LOOP_BODY);
            buf_puts(out, " }");
        }
        used_ar = false;
    }
    emit_releases(escope);
    epop();
    indent--;
    nl();
    buf_puts(out, "}");
    return;
}


/* ---------- region versions (rg-hoist) ----------
   A dual class (region.c: made inside its loop and outside it too) tests xs_rg_on_N at every `new`: a TLS load
   and a branch per object, in binarytrees once per node inside bottomUpTree, where the flag cannot change. A
   function whose dual allocations all belong to region N (directly or through what it calls) gets a second
   version f_rN, compiled as if the flag were known true: its `new C` allocates from the region directly, and its
   calls to such functions go to their _rN versions. The loop body of region N calls the _rN versions; everything
   else keeps the generic ones. Only entered while an iteration of loop N runs, so the flag is true by
   construction. XISTO_NO_RG_HOIST=1 disables (measuring knob). */
static int cur_rg;        /* the dual region whose loop body (or _rN version) is being emitted, 0 otherwise */
static bool rg_hoist_on;
static Program *rg_prog;
static int rg_combine(int a, int b) { return a == 0 ? b : b == 0 ? a : a == b ? a : -1; }
static bool rg_user_call(Expr *e) { return e && e->kind == E_CALL && e->func && !e->builtin.cfn && e->func->body && !(e->lhs && e->lhs->type && e->lhs->type->kind == TY_FUNC && !e->func); }
static void rg_scan_stmt(Stmt *s, int *acc, bool final);
static void rg_scan_expr(Expr *e, int *acc, bool final) {
    if (!e) return;
    if (e->kind == E_NEW_OBJ && !e->cls->builtin && e->cls->rdual) *acc = rg_combine(*acc, e->cls->rdual);
    if (rg_user_call(e)) *acc = rg_combine(*acc, final ? e->func->rg_hoist : e->func->rg_hoist);
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn) rg_scan_stmt(e->arrow->fn->body, acc, final); /* (what a closure makes counts; it is never hoisted itself) */
    rg_scan_expr(e->lhs, acc, final);
    rg_scan_expr(e->rhs, acc, final);
    for (int i = 0; i < e->args.len; i++) rg_scan_expr(e->args.items[i], acc, final);
}
static void rg_scan_stmt(Stmt *s, int *acc, bool final) {
    if (!s) return;
    rg_scan_expr(s->init, acc, final);
    rg_scan_expr(s->expr, acc, final);
    rg_scan_expr(s->cond, acc, final);
    rg_scan_expr(s->for_update, acc, final);
    rg_scan_stmt(s->for_init, acc, final);
    rg_scan_stmt(s->body, acc, final);
    rg_scan_stmt(s->else_body, acc, final);
    for (int i = 0; i < s->stmts.len; i++) rg_scan_stmt(s->stmts.items[i], acc, final);
    for (int i = 0; i < s->ncases; i++) { rg_scan_expr(s->cases[i].value, acc, final); rg_scan_stmt(s->cases[i].body, acc, final); }
}
static bool rg_candidate(Func *f) {
    return f->body && !f->is_ctor && !f->is_async && !f->is_gen && !f->virt_target && !f->cls && f->ncalls >= 0 && !stmt_has_try(f->body) && f->used && !f->mod->builtin && !f->is_native;
}
/* marks f_rN as reached: f's own calls to region-N functions follow */
static void rg_mark_stmt(Stmt *s, int N);
static void rg_mark_expr(Expr *e, int N) {
    if (!e) return;
    if (rg_user_call(e) && e->func->rg_hoist == N && !e->func->rg_used) { e->func->rg_used = true; rg_mark_stmt(e->func->body, N); }
    if (e->kind == E_ARROW) return; /* a closure runs when it runs: generic calls */
    rg_mark_expr(e->lhs, N); rg_mark_expr(e->rhs, N);
    for (int i = 0; i < e->args.len; i++) rg_mark_expr(e->args.items[i], N);
}
static void rg_mark_stmt(Stmt *s, int N) {
    if (!s) return;
    rg_mark_expr(s->init, N); rg_mark_expr(s->expr, N); rg_mark_expr(s->cond, N); rg_mark_expr(s->for_update, N);
    rg_mark_stmt(s->for_init, N); rg_mark_stmt(s->body, N); rg_mark_stmt(s->else_body, N);
    for (int i = 0; i < s->stmts.len; i++) rg_mark_stmt(s->stmts.items[i], N);
    for (int i = 0; i < s->ncases; i++) { rg_mark_expr(s->cases[i].value, N); rg_mark_stmt(s->cases[i].body, N); }
}
/* finds the dual region loops and marks what their bodies reach */
static void rg_loops_stmt(Stmt *s);
static void rg_loops_expr(Expr *e) {
    if (!e) return;
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn) rg_loops_stmt(e->arrow->fn->body);
    rg_loops_expr(e->lhs); rg_loops_expr(e->rhs);
    for (int i = 0; i < e->args.len; i++) rg_loops_expr(e->args.items[i]);
}
static void rg_loops_stmt(Stmt *s) {
    if (!s) return;
    if ((s->kind == S_FOR || s->kind == S_WHILE || s->kind == S_FOROF) && s->region && region_dual[s->region]) { rg_mark_stmt(s->body, s->region); rg_mark_expr(s->cond, s->region); rg_mark_expr(s->for_update, s->region); }
    rg_loops_expr(s->init); rg_loops_expr(s->expr); rg_loops_expr(s->cond); rg_loops_expr(s->for_update);
    rg_loops_stmt(s->for_init); rg_loops_stmt(s->body); rg_loops_stmt(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) rg_loops_stmt(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) { rg_loops_expr(s->cases[i].value); rg_loops_stmt(s->cases[i].body); }
}
static void rg_compute(Program *p) {
    rg_prog = p;
    rg_hoist_on = !xs_debug && !xs_profile && !sm_mode && region_count > 0 && !getenv("XISTO_NO_RG_HOIST");
    for (int i = 0; i < p->nfuncs; i++) { p->funcs[i]->rg_hoist = 0; p->funcs[i]->rg_used = false; }
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (c->ctor) { c->ctor->rg_hoist = -1; c->ctor->rg_used = false; }
        for (int k = 0; k < c->nmethods; k++) { c->methods[k]->rg_hoist = -1; c->methods[k]->rg_used = false; }
    }
    if (!rg_hoist_on) return;
    bool any = false;
    for (int i = 0; i < p->nclasses; i++) any |= p->classes[i]->rdual != 0;
    if (!any) { rg_hoist_on = false; return; }
    for (int i = 0; i < p->nfuncs; i++) if (!rg_candidate(p->funcs[i])) p->funcs[i]->rg_hoist = -1;
    for (bool changed = true; changed;) { /* fixpoint over the call graph: what each function makes, with what it calls */
        changed = false;
        for (int i = 0; i < p->nfuncs; i++) {
            Func *f = p->funcs[i];
            if (f->rg_hoist == -1) continue;
            int acc = 0;
            rg_scan_stmt(f->body, &acc, false);
            if (acc != f->rg_hoist) { f->rg_hoist = acc; changed = true; }
        }
    }
    for (int i = 0; i < p->nfuncs; i++) if (p->funcs[i]->used && !p->funcs[i]->mod->builtin && p->funcs[i]->body) rg_loops_stmt(p->funcs[i]->body);
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (!c->used) continue;
        if (c->ctor) rg_loops_stmt(c->ctor->body);
        for (int k = 0; k < c->nmethods; k++) if (c->methods[k]->body) rg_loops_stmt(c->methods[k]->body);
    }
    for (int i = 0; i < p->top.len; i++) rg_loops_stmt(p->top.items[i]);
}
static char *rg_name(Func *f) { return xs_sprintf("%s_r%d", f->cname, f->rg_hoist); }
/* the callee's name at this call: its region version inside region cur_rg's code, else its own */
static const char *rg_call_name(Func *f) {
    if (rg_hoist_on && cur_rg && f && f->rg_hoist == cur_rg && f->rg_used) return rg_name(f);
    return f->cname;
}
/* `new C(args)` of a dual class of the region being emitted, with a small constructor (only `this.f = param`
   stores, every field set, reference parameters handed over): the allocation from the region and the stores,
   in place. Returns false when the generic C_new must be used. */
static bool small_ctor(Func *f);
static bool rg_inline_new(Expr *e) {
    Class *c = e->cls;
    if (!rg_hoist_on || !cur_rg || c->builtin || c->rdual != cur_rg || !c->ctor || !small_ctor(c->ctor) || c->stack_at) return false;
    Func *ct = c->ctor;
    for (int k = 0; k < c->nfields; k++) if (c->fields[k].init) return false;
    mark_init_stores(c, ct);
    if (!ctor_sets_every_field(c)) return false;
    int src[8]; /* per field: the parameter index, or -1 */
    for (int k = 0; k < c->nfields; k++) src[k] = -1;
    for (int i = 0; i < ct->body->stmts.len; i++) {
        Expr *a = ct->body->stmts.items[i]->expr;
        int f = a->lhs->field;
        if (f < 0 || f >= c->nfields || a->rhs->kind != E_IDENT) return false;
        int pi = -1;
        for (int j = 0; j < ct->nparams; j++) if (ct->params[j].cname && a->rhs->cname && strcmp(ct->params[j].cname, a->rhs->cname) == 0) pi = j;
        if (pi < 0 || (cref(c->fields[f].type) && !ct->params[pi].consumed)) return false;
        src[f] = pi;
    }
    for (int k = 0; k < c->nfields; k++) if (src[k] < 0) return false;
    if (e->args.len != ct->nparams) return false;
    buf_puts(out, "({ ");
    for (int i = 0; i < e->args.len; i++) { /* the arguments first, in order, as the call would */
        emit_decl(ct->params[i].type, xs_sprintf("ra%d_", i));
        buf_puts(out, " = ");
        if (ct->params[i].consumed) emit_owned(e->args.items[i]); else emit_borrowed(e->args.items[i]);
        buf_puts(out, "; ");
    }
    if (c->nohdr) buf_printf(out, "%s *ro_ = xs_region_alloc_nh(&xs_rg_%d, sizeof(%s));", c->cname, c->rdual, c->cname);
    else buf_printf(out, "%s *ro_ = xs_region_alloc_dual_nz(&xs_rg_%d, sizeof(%s), &%s_type);", c->cname, c->rdual, c->cname, c->cname);
    for (int k = 0; k < c->nfields; k++) buf_printf(out, " ro_->%s = ra%d_;", c->fields[k].cname, src[k]);
    buf_puts(out, " ro_; })");
    return true;
}
/* f's region version: the same body, emitted with cur_rg = N (into clone_fns, after the generic bodies) */
static void emit_rg_clone(Func *f) {
    Buf *saved_out = out;
    EScope *saved_scope = escope;
    Func *saved_func = cur_func;
    int saved_indent = indent, saved_rg = cur_rg;
    bool saved_ctor = in_ctor, saved_ar = used_ar, saved_vol = vol_locals;
    Class *saved_class = cur_class;
    Module *saved_mod = cur_mod;
    Expr *saved_b = bracketing;
    Buf b = {0};
    out = &b;
    indent = 0;
    escope = NULL;
    in_ctor = false;
    bracketing = NULL;
    cur_rg = f->rg_hoist;
    char *cname = f->cname;
    f->cname = rg_name(f);
    buf_putc(out, '\n');
    emit_signature(f);
    f->cname = cname;
    emit_func_body(f);
    buf_puts(&clone_fns, b.data);
    free(b.data);
    out = saved_out; escope = saved_scope; cur_func = saved_func; indent = saved_indent; cur_rg = saved_rg;
    in_ctor = saved_ctor; used_ar = saved_ar; vol_locals = saved_vol; cur_class = saved_class; cur_mod = saved_mod; bracketing = saved_b;
}

static void emit_stmt_(Stmt *s);
static void emit_stmt(Stmt *s) {
    if ((s->kind == S_FOR || s->kind == S_WHILE || s->kind == S_FOROF) && s->region) { /* region.c: the iterations' objects go in one step */
        buf_printf(out, "xs_region_enter(&xs_rg_%d);", s->region);
        nl();
        region_reset_pending = s->region;
        int saved_rg = cur_rg;
        if (region_dual[s->region] && !sm_mode && !ictx) cur_rg = s->region; /* its body calls the region versions */
        emit_stmt_(s);
        cur_rg = saved_rg;
        region_reset_pending = 0;
        nl();
        buf_printf(out, region_dual[s->region] ? "xs_rg_on_%d = false; xs_region_reset(&xs_rg_%d);" : "xs_region_reset(&xs_rg_%d);", s->region, s->region);
        return;
    }
    emit_stmt_(s);
}
/* The counting idiom on a Map<int, int>, `if (m.has(k)) m.set(k, m.get(k)! + d); else m.set(k, d);` (d pure: a
   number, an int name, their sums), as one probe: found, the value is added to in place (the checked add keeps
   its site); not found, the entry is put. k-nucleotide on the Xeon: three inlined calls with their barriers
   became one, -21% (7,92 -> 6,25 cpu-s). m and k are read twice, so both must be names or numbers. */
static bool same_simple(Expr *a, Expr *b) {
    if (!a || !b || a->kind != b->kind) return false;
    if (a->kind == E_IDENT) return a->cname && b->cname && strcmp(a->cname, b->cname) == 0;
    if (a->kind == E_NUMBER) return a->num == b->num;
    if (a->kind == E_THIS) return true;
    if (a->kind == E_MEMBER && !a->is_call && !b->is_call) return a->field == b->field && a->field >= 0 && same_simple(a->lhs, b->lhs);
    if (a->kind == E_BINARY) return a->op == b->op && same_simple(a->lhs, b->lhs) && same_simple(a->rhs, b->rhs);
    return false;
}
static Expr *single_expr_stmt(Stmt *b) {
    if (!b) return NULL;
    if (b->kind == S_BLOCK) return b->stmts.len == 1 ? single_expr_stmt(b->stmts.items[0]) : NULL;
    return b->kind == S_EXPR ? b->expr : NULL;
}
static Expr *map_call(Expr *e, const char *op, Expr *m, int nargs) { /* m.op(...) on the same map, or NULL */
    if (!e || e->kind != E_MEMBER || !e->is_call || !e->builtin.cfn || strcmp(e->builtin.cfn, op) != 0 || e->args.len != nargs) return NULL;
    if (!e->lhs || !e->lhs->type || e->lhs->type->kind != TY_MAP || !same_simple(e->lhs, m)) return NULL;
    return e;
}
static bool map_add_match(Stmt *s, Expr **pm, Expr **pk, Expr **pd, Expr **psum) {
    if (xs_debug || sm_mode || getenv("XISTO_NO_MAP_ADD") || !s || s->kind != S_IF) return false;
    Expr *has = s->cond;
    if (!has || has->kind != E_MEMBER || !has->is_call || !has->builtin.cfn || strcmp(has->builtin.cfn, "has") != 0 || has->args.len != 1) return false;
    Expr *m = has->lhs;
    if (!m || !m->type || m->type->kind != TY_MAP || !m->type->key || m->type->key->kind != TY_INT || val_kind(m->type->elem) != 1) return false;
    if (m->kind != E_IDENT && !(m->kind == E_MEMBER && !m->is_call && m->lhs && (m->lhs->kind == E_THIS || m->lhs->kind == E_IDENT))) return false;
    Expr *k = has->args.items[0];
    if (!k->type || k->type->kind != TY_INT || (k->kind != E_IDENT && k->kind != E_NUMBER)) return false;
    Expr *set1 = map_call(single_expr_stmt(s->body), "set", m, 2), *set2 = map_call(single_expr_stmt(s->else_body), "set", m, 2);
    if (!set1 || !set2 || !same_simple(set1->args.items[0], k) || !same_simple(set2->args.items[0], k)) return false;
    Expr *d = set2->args.items[1];
    if (!bce_pure(d)) return false;
    Expr *sum = set1->args.items[1];
    if (!sum || sum->kind != E_BINARY || sum->op != TK_PLUS || sum->builtin.cfn) return false;
    Expr *g = sum->lhs, *dd = sum->rhs;
    if (same_simple(dd, d)) { /* get + d */ } else if (same_simple(g, d)) { Expr *t = g; g = dd; dd = t; } else return false;
    while (g && g->kind == E_AS && g->op == TK_NOT && !g->elem_type) g = g->lhs; /* m.get(k)! */
    if (!map_call(g, "get", m, 1) || !same_simple(g->args.items[0], k)) return false;
    *pm = m; *pk = k; *pd = d; *psum = sum;
    return true;
}
/* maps whose fields a loop holds in locals (map_hoist_enter): the map's C name and the local's id */
static struct { const char *m; int id; bool defer; int site; } map_hoist[8]; /* defer: the adds test only 64 bits; site: the first such add (its range test runs after the loop) */
static int nmap_hoist, map_hoist_ids;
static int byte_hoist_find(Expr *a);
static int map_hoist_slot(Expr *m) {
    if (!m || m->kind != E_IDENT || !m->cname) return -1;
    for (int i = nmap_hoist - 1; i >= 0; i--) if (strcmp(map_hoist[i].m, m->cname) == 0) return i;
    return -1;
}
static int map_hoist_find(Expr *m) { int i = map_hoist_slot(m); return i >= 0 ? map_hoist[i].id : -1; }
static bool emit_map_add(Stmt *s) {
    Expr *m, *k, *d, *sum;
    if (!map_add_match(s, &m, &k, &d, &sum)) return false;
    int site = new_site(sum->pos), hc = map_hoist_find(m), hs = map_hoist_slot(m);
    /* a constant d >= 0 in a loop whose range test is deferred to its end (loop_hoist_enter) */
    bool dfr = hc >= 0 && map_hoist[hs].defer && d->kind == E_NUMBER && d->is_int_lit && d->num >= 0;
    if (dfr && !map_hoist[hs].site) map_hoist[hs].site = site;
    if (dfr) buf_printf(out, "xs_map_add_hcd(");
    else if (hc >= 0) buf_printf(out, "xs_map_add_hc(");
    else buf_puts(out, "xs_map_add_i(");
    emit_borrowed(m);
    if (hc >= 0) buf_printf(out, ", &mhc%d_", hc);
    buf_puts(out, ", "); emit_expr(k); buf_puts(out, ", "); emit_expr(d);
    buf_printf(out, ", XS_S(%d));", site);
    return true;
}
/* A loop whose body touches a local Map<int, int> only through the counting idiom, calls no user function and
   makes no closure (nothing else can reach the map while it runs): the table's fields are read into a local
   before the loop and again only after the idiom's slow path (xs_map_add_hc). The same loop's byte arrays read
   unchecked (a name not written in the loop: a Uint8Array's data never moves) get their data pointer in a local
   (byte_hoist): a store of the counter may alias the fields, so the C compiler reloads them on every iteration.
   k-nucleotide, Xeon: 5,2 -> 3,9 cpu-s with both. XISTO_NO_LOOP_HOIST=1 keeps the loads. */
static bool hoist_body_ok(Stmt *s);
static void emit_stmt_for_inner(Stmt *s);
static bool hoist_expr_ok(Expr *e) {
    if (!e) return true;
    if (e->kind == E_ARROW || e->kind == E_AWAIT) return false;
    if (e->kind == E_CALL) return false;
    if (e->kind == E_NEW_OBJ && e->cls && !e->cls->builtin) return false;
    if (e->kind == E_MEMBER && e->is_call && !(e->lhs && e->lhs->type && e->lhs->type->kind == TY_MAP) && !(e->cls && e->cls->builtin) && !e->builtin.cfn) return false;
    if (!hoist_expr_ok(e->lhs) || !hoist_expr_ok(e->rhs)) return false;
    for (int i = 0; i < e->args.len; i++) if (!hoist_expr_ok(e->args.items[i])) return false;
    return true;
}
static bool hoist_body_ok(Stmt *s) {
    if (!s) return true;
    if (s->kind == S_TRY || s->kind == S_YIELD || s->kind == S_RETURN) return false;
    if (!hoist_expr_ok(s->init) || !hoist_expr_ok(s->expr) || !hoist_expr_ok(s->cond) || !hoist_expr_ok(s->for_update)) return false;
    if (!hoist_body_ok(s->for_init) || !hoist_body_ok(s->body) || !hoist_body_ok(s->else_body)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!hoist_body_ok(s->stmts.items[i])) return false;
    for (int i = 0; i < s->ncases; i++) if (!hoist_body_ok(s->cases[i].body)) return false;
    return true;
}
/* occurrences of the name c in e/s, and how many of them are the counting idiom's (4 per idiom) */
static int name_uses_e(Expr *e, const char *c) {
    if (!e) return 0;
    int n = e->kind == E_IDENT && e->cname && strcmp(e->cname, c) == 0;
    n += name_uses_e(e->lhs, c) + name_uses_e(e->rhs, c);
    for (int i = 0; i < e->args.len; i++) n += name_uses_e(e->args.items[i], c);
    return n;
}
static int name_uses(Stmt *s, const char *c, int *idiom) {
    if (!s) return 0;
    Expr *m, *k, *d, *sum;
    if (map_add_match(s, &m, &k, &d, &sum) && m->kind == E_IDENT && strcmp(m->cname, c) == 0) {
        int n = name_uses_e(s->cond, c) + name_uses(s->body, c, idiom) + name_uses(s->else_body, c, idiom);
        *idiom += n;
        return n;
    }
    int n = name_uses_e(s->init, c) + name_uses_e(s->expr, c) + name_uses_e(s->cond, c) + name_uses_e(s->for_update, c);
    n += name_uses(s->for_init, c, idiom) + name_uses(s->body, c, idiom) + name_uses(s->else_body, c, idiom);
    for (int i = 0; i < s->stmts.len; i++) n += name_uses(s->stmts.items[i], c, idiom);
    for (int i = 0; i < s->ncases; i++) n += name_uses_e(s->cases[i].value, c) + name_uses(s->cases[i].body, c, idiom);
    return n;
}
/* reads of c.size in e/s: the hoisted table (xs_map_hc) leaves the map's own count up to date, so they may stay */
static int size_reads_e(Expr *e, const char *c) {
    if (!e) return 0;
    int n = e->kind == E_MEMBER && !e->is_call && e->str && strcmp(e->str, "size") == 0 && e->lhs && e->lhs->kind == E_IDENT &&
            e->lhs->cname && strcmp(e->lhs->cname, c) == 0 && e->lhs->type && e->lhs->type->kind == TY_MAP;
    n += size_reads_e(e->lhs, c) + size_reads_e(e->rhs, c);
    for (int i = 0; i < e->args.len; i++) n += size_reads_e(e->args.items[i], c);
    return n;
}
static int size_reads(Stmt *s, const char *c) {
    if (!s) return 0;
    int n = size_reads_e(s->init, c) + size_reads_e(s->expr, c) + size_reads_e(s->cond, c) + size_reads_e(s->for_update, c);
    n += size_reads(s->for_init, c) + size_reads(s->body, c) + size_reads(s->else_body, c);
    for (int i = 0; i < s->stmts.len; i++) n += size_reads(s->stmts.items[i], c);
    for (int i = 0; i < s->ncases; i++) n += size_reads_e(s->cases[i].value, c) + size_reads(s->cases[i].body, c);
    return n;
}
static void collect_idiom_maps(Stmt *s, const char **ms, int *nm) {
    if (!s) return;
    Expr *m, *k, *d, *sum;
    if (map_add_match(s, &m, &k, &d, &sum)) {
        if (m->kind == E_IDENT && m->cname && *nm < 8) { bool seen = false; for (int i = 0; i < *nm; i++) seen |= strcmp(ms[i], m->cname) == 0; if (!seen) ms[(*nm)++] = m->cname; }
        return;
    }
    collect_idiom_maps(s->for_init, ms, nm); collect_idiom_maps(s->body, ms, nm); collect_idiom_maps(s->else_body, ms, nm);
    for (int i = 0; i < s->stmts.len; i++) collect_idiom_maps(s->stmts.items[i], ms, nm);
    for (int i = 0; i < s->ncases; i++) collect_idiom_maps(s->cases[i].body, ms, nm);
}
/* byte arrays read in s (E_INDEX xs_u8_get on a name) */
static void collect_byte_reads_e(Expr *e, const char **as, int *na) {
    if (!e) return;
    if (e->kind == E_INDEX && e->builtin.cfn && strcmp(e->builtin.cfn, "xs_u8_get") == 0 && e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname && *na < 8) {
        bool seen = false;
        for (int i = 0; i < *na; i++) seen |= strcmp(as[i], e->lhs->cname) == 0;
        if (!seen) as[(*na)++] = e->lhs->cname;
    }
    collect_byte_reads_e(e->lhs, as, na); collect_byte_reads_e(e->rhs, as, na);
    for (int i = 0; i < e->args.len; i++) collect_byte_reads_e(e->args.items[i], as, na);
}
static void collect_byte_reads(Stmt *s, const char **as, int *na) {
    if (!s) return;
    collect_byte_reads_e(s->init, as, na); collect_byte_reads_e(s->expr, as, na); collect_byte_reads_e(s->cond, as, na); collect_byte_reads_e(s->for_update, as, na);
    collect_byte_reads(s->for_init, as, na); collect_byte_reads(s->body, as, na); collect_byte_reads(s->else_body, as, na);
    for (int i = 0; i < s->stmts.len; i++) collect_byte_reads(s->stmts.items[i], as, na);
    for (int i = 0; i < s->ncases; i++) collect_byte_reads(s->cases[i].body, as, na);
}
static bool declares(Stmt *s, const char *c) {
    if (!s) return false;
    if (s->kind == S_VAR && s->cname && strcmp(s->cname, c) == 0) return true;
    if (declares(s->for_init, c) || declares(s->body, c) || declares(s->else_body, c)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (declares(s->stmts.items[i], c)) return true;
    for (int i = 0; i < s->ncases; i++) if (declares(s->cases[i].body, c)) return true;
    return false;
}
static struct { const char *a; int id; } byte_hoist[8];
static int nbyte_hoist, byte_hoist_ids;
static int byte_hoist_find(Expr *a) {
    if (!a || a->kind != E_IDENT || !a->cname) return -1;
    for (int i = nbyte_hoist - 1; i >= 0; i--) if (strcmp(byte_hoist[i].a, a->cname) == 0) return byte_hoist[i].id;
    return -1;
}
/* emits the locals before loop s (inside an open brace the caller closes) and pushes them; returns how many it pushed */
/* a return somewhere in s (arrow bodies aside: loop_hoist loops make no closures) */
static bool stmt_has_return(Stmt *s) {
    if (!s) return false;
    if (s->kind == S_RETURN || s->kind == S_YIELD) return true;
    if (stmt_has_return(s->for_init) || stmt_has_return(s->body) || stmt_has_return(s->else_body)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (stmt_has_return(s->stmts.items[i])) return true;
    for (int i = 0; i < s->ncases; i++) if (stmt_has_return(s->cases[i].body)) return true;
    return false;
}
static int loop_hoist_enter(Stmt *s, int *nb) {
    *nb = 0;
    if (xs_debug || sm_mode || ictx || getenv("XISTO_NO_LOOP_HOIST") || !hoist_body_ok(s->body) || !hoist_expr_ok(s->cond) || !hoist_expr_ok(s->for_update)) return 0;
    const char *ms[8]; int nm = 0, pushed = 0;
    collect_idiom_maps(s->body, ms, &nm);
    for (int i = 0; i < nm && nmap_hoist < 8; i++) {
        int idiom = 0;
        int all = name_uses(s->body, ms[i], &idiom) + name_uses_e(s->cond, ms[i]) + name_uses_e(s->for_update, ms[i]);
        idiom += size_reads(s->body, ms[i]) + size_reads_e(s->cond, ms[i]);
        if (all != idiom || bce_writes(s, ms[i]) || declares(s, ms[i])) continue;
        int id = map_hoist_ids++;
        buf_printf(out, "xs_map_hc mhc%d_ = xs_map_hc_of(%s); ", id, ren(ms[i]));
        /* the range test of constant adds waits for the loop's end when nothing can see the map before it: no
           return leaves the loop past the test, no try in the program can catch an error raised in it, and the map
           is a local of this function (a failed request of a server drops it with the call) */
        map_hoist[nmap_hoist].defer = !prog_try && ms[i][0] == 'v' && !getenv("XISTO_NO_DEFER_RANGE") && !stmt_has_return(s->body); /* (a local: a server's request that fails takes it along) */
        map_hoist[nmap_hoist].site = 0;
        map_hoist[nmap_hoist].m = ms[i]; map_hoist[nmap_hoist++].id = id;
        pushed++;
    }
    const char *as[8]; int na = 0;
    collect_byte_reads(s->body, as, &na);
    collect_byte_reads_e(s->cond, as, &na);
    for (int i = 0; i < na && nbyte_hoist < 8; i++) {
        if (bce_writes(s, as[i]) || declares(s, as[i])) continue;
        int id = byte_hoist_ids++;
        buf_printf(out, "__typeof__((%s)->data) bh%d_ = (%s)->data; (void)bh%d_; ", ren(as[i]), id, ren(as[i]), id);
        byte_hoist[nbyte_hoist].a = as[i]; byte_hoist[nbyte_hoist++].id = id;
        (*nb)++;
    }
    return pushed;
}
/* Two loops over byte arrays that are one runtime call (bounds checked once; a 16-byte shuffle or a memmove):
     for (let i = LO; i < (S + 1) >> 1; i++) { const t = a[i]; a[i] = a[S - i]; a[S - i] = t; }  -> xs_u8_rev(a, LO, S - LO)
     for (let j = LO; j < HI; j++) dst[j] = src[j];                                              -> xs_u8_copy_range
   a, dst, src are names of Uint8Array; LO, S, HI pure (numbers, int names, their sums); the counter is not
   written in the body. XISTO_NO_U8_IDIOM=1 keeps the loops. */
/* text a body wants at its function's entry (emit_func_body puts it after ar_mark); only while the function's
   body goes to the same buffer */
static Buf *fn_prelude_out;
static size_t fn_prelude_pos;
static Buf fn_prelude_text;
static bool u8_name(Expr *e) { return e && e->kind == E_IDENT && e->cname && (bce_bytes(e->type) || i64reg(e)); }
/* a[i] on a byte array: a Uint8Array, or an int[] held in a register (u8reg, proved to hold bytes) */
static bool byte_ix(Expr *e) {
    return e && e->kind == E_INDEX && ((e->builtin.cfn && strcmp(e->builtin.cfn, "xs_u8_get") == 0) || (!e->builtin.cfn && i64reg(e->lhs)));
}
/* a + t or b - t, with a name a, the counter t */
static Expr *u8_cursor(Expr *e, const char *t, bool plus) {
    if (!e || e->kind != E_BINARY || e->op != (plus ? TK_PLUS : TK_MINUS)) return NULL;
    if (!e->lhs || e->lhs->kind != E_IDENT || !e->lhs->cname || !e->lhs->type || e->lhs->type->kind != TY_INT) return NULL;
    if (!e->rhs || e->rhs->kind != E_IDENT || !e->rhs->cname || strcmp(e->rhs->cname, t) != 0) return NULL;
    return e->lhs;
}
static bool u8_at(Expr *e, Expr *arr, Expr *idx) { /* arr[idx] */
    return e && e->kind == E_INDEX && e->builtin.cfn && strcmp(e->builtin.cfn, "xs_u8_get") == 0 && same_simple(e->lhs, arr) && same_simple(e->rhs, idx);
}
/* the trade of reverse-complement (kind 4): { const x = buf[a + t]; buf[a + t] = T[buf[b - t]]; buf[b - t] = T[x]; }
   with T a Uint8Array the body does not write (a global or a parameter: its context is set up at the function's
   entry), buf the same array on both sides, the cursors a and b names the body does not write */
static bool match_u8_trade(Expr *c, const char *t, Stmt *b, U8Idiom *m) {
    Stmt *d = b->stmts.items[0];
    if (d->kind != S_VAR || !d->is_const || !d->cname || d->cell || !d->init) return false;
    Expr *a1 = b->stmts.items[1]->kind == S_EXPR ? b->stmts.items[1]->expr : NULL, *a2 = b->stmts.items[2]->kind == S_EXPR ? b->stmts.items[2]->expr : NULL;
    if (!a1 || !a2 || a1->kind != E_ASSIGN || a2->kind != E_ASSIGN || a1->op != TK_ASSIGN || a2->op != TK_ASSIGN) return false;
    Expr *x = d->init;
    if (!x || x->kind != E_INDEX || !x->builtin.cfn || strcmp(x->builtin.cfn, "xs_u8_get") != 0 || !u8_name(x->lhs)) return false;
    Expr *buf = x->lhs, *ia = x->rhs, *a = u8_cursor(ia, t, true);
    if (!a || !u8_at(a1->lhs, buf, ia)) return false;
    Expr *r1 = a1->rhs;
    if (!r1 || r1->kind != E_INDEX || !r1->builtin.cfn || strcmp(r1->builtin.cfn, "xs_u8_get") != 0 || !u8_name(r1->lhs)) return false;
    Expr *T = r1->lhs, *inner = r1->rhs;
    if (!inner || inner->kind != E_INDEX || !inner->builtin.cfn || strcmp(inner->builtin.cfn, "xs_u8_get") != 0 || !same_simple(inner->lhs, buf)) return false;
    Expr *ib = inner->rhs, *bb = u8_cursor(ib, t, false);
    if (!bb || strcmp(bb->cname, a->cname) == 0 || strcmp(T->cname, buf->cname) == 0 || !u8_at(a2->lhs, buf, ib)) return false;
    Expr *r2 = a2->rhs;
    if (!r2 || r2->kind != E_INDEX || !r2->builtin.cfn || strcmp(r2->builtin.cfn, "xs_u8_get") != 0 || !same_simple(r2->lhs, T)) return false;
    if (!r2->rhs || r2->rhs->kind != E_IDENT || !r2->rhs->cname || strcmp(r2->rhs->cname, d->cname) != 0) return false;
    if (!bce_pure(c->rhs) || stmt_writes_name(b, a->cname) || stmt_writes_name(b, bb->cname) || stmt_writes_name(b, buf->cname) || stmt_writes_name(b, T->cname)) return false;
    m->kind = 4; m->a = buf; m->src = T; m->hi = c->rhs; m->ca = a; m->cb = bb;
    return true;
}
static bool u8_index_of(Expr *e, Expr *arr, Expr *idx) { /* arr[idx] as the program wrote it */
    return byte_ix(e) && same_simple(e->lhs, arr) && same_simple(e->rhs, idx);
}
static bool is_sub_of(Expr *e, Expr *S, const char *i) { /* S - i */
    return e && e->kind == E_BINARY && e->op == TK_MINUS && same_simple(e->lhs, S) && e->rhs && e->rhs->kind == E_IDENT && e->rhs->cname && strcmp(e->rhs->cname, i) == 0;
}
/* A local Uint8Array(N <= 16) a loop only indexes (get, set, the idioms below) lives in a vector register for
   the loop (u8reg_setup): xs_v16_* in xisto_rt.h. One list for the nested scopes being emitted. */
static Stmt *u8_prev_stmt(Stmt *s, Stmt *t) { /* the statement right before t in its block */
    if (!s) return NULL;
    for (int i = 0; i < s->stmts.len; i++) {
        if (s->stmts.items[i] == t) return i ? s->stmts.items[i - 1] : NULL;
        Stmt *r = u8_prev_stmt(s->stmts.items[i], t);
        if (r) return r;
    }
    Stmt *r;
    if ((r = u8_prev_stmt(s->for_init, t)) || (r = u8_prev_stmt(s->body, t)) || (r = u8_prev_stmt(s->else_body, t))) return r;
    for (int i = 0; i < s->ncases; i++) if ((r = u8_prev_stmt(s->cases[i].body, t))) return r;
    return NULL;
}
static Stmt *u8_const_decl(Stmt *s, const char *cname) { /* a const local's declaration, in s */
    if (!s) return NULL;
    if (s->kind == S_VAR && s->is_const && !s->cell && s->cname && strcmp(s->cname, cname) == 0) return s;
    Stmt *d;
    if ((d = u8_const_decl(s->for_init, cname)) || (d = u8_const_decl(s->body, cname)) || (d = u8_const_decl(s->else_body, cname))) return d;
    for (int i = 0; i < s->stmts.len; i++) if ((d = u8_const_decl(s->stmts.items[i], cname))) return d;
    for (int i = 0; i < s->ncases; i++) if ((d = u8_const_decl(s->cases[i].body, cname))) return d;
    return NULL;
}
static bool match_u8_idiom(Stmt *s, U8Idiom *m) {
    if (s->kind != S_FOR) return false;
    Stmt *fi = s->for_init;
    Expr *c = s->cond, *u = s->for_update;
    if (!fi || fi->kind != S_VAR || !fi->cname || !fi->init || fi->cell || !bce_pure(fi->init)) return false;
    if (!u || u->kind != E_POSTFIX || u->op != TK_PLUSPLUS || !u->lhs || u->lhs->kind != E_IDENT || !u->lhs->cname || strcmp(u->lhs->cname, fi->cname) != 0) return false;
    /* 2 * i < S (or i * 2 < S): for integers the same as i < (S + 1) >> 1, the reversal's own bound */
    Expr *twice_bound = NULL;
    if (c && c->kind == E_BINARY && c->op == TK_LT && c->lhs && c->lhs->kind == E_BINARY && c->lhs->op == TK_STAR) {
        Expr *k2 = c->lhs, *iv2 = NULL;
        if (k2->lhs && k2->lhs->kind == E_NUMBER && k2->lhs->num == 2) iv2 = k2->rhs;
        else if (k2->rhs && k2->rhs->kind == E_NUMBER && k2->rhs->num == 2) iv2 = k2->lhs;
        if (iv2 && iv2->kind == E_IDENT && iv2->cname && strcmp(iv2->cname, fi->cname) == 0 && iv2->type && iv2->type->kind == TY_INT) twice_bound = c->rhs;
    }
    if (!twice_bound && (!c || c->kind != E_BINARY || (c->op != TK_LT && c->op != TK_LE) || !c->lhs || c->lhs->kind != E_IDENT || !c->lhs->cname || strcmp(c->lhs->cname, fi->cname) != 0)) return false;
    const char *i = fi->cname;
    Stmt *b = s->body;
    if (!b || b->kind != S_BLOCK) return false;
    if (stmt_writes_name(b, i) || writes_name(c->rhs, i)) return false;
    Expr iv = {0}; iv.kind = E_IDENT; iv.cname = (char *)i; /* the counter, as an index */
    m->loop = s; m->lo = fi->init;
    if (twice_bound && b->stmts.len != 3) return false;
    if (b->stmts.len == 1) { /* copy, or shift */
        Expr *as = b->stmts.items[0]->kind == S_EXPR ? b->stmts.items[0]->expr : NULL;
        if (!as || as->kind != E_ASSIGN || as->op != TK_ASSIGN || !bce_pure(c->rhs)) return false;
        Expr *dst = as->lhs, *src = as->rhs;
        if (!byte_ix(dst) || !u8_name(dst->lhs) || !same_simple(dst->rhs, &iv)) return false;
        if (!byte_ix(src) || !u8_name(src->lhs)) return false;
        if (same_simple(src->rhs, &iv)) {
            if (c->op != TK_LT || strcmp(dst->lhs->cname, src->lhs->cname) == 0) return false;
            m->kind = 1; m->a = dst->lhs; m->src = src->lhs; m->hi = c->rhs;
            return true;
        }
        /* a[j] = a[j + 1], j <= K (or j < K: the last j is K - 1) */
        Expr *r = src->rhs;
        if (strcmp(dst->lhs->cname, src->lhs->cname) != 0 || !r || r->kind != E_BINARY || r->op != TK_PLUS || !same_simple(r->lhs, &iv) || !r->rhs || r->rhs->kind != E_NUMBER || r->rhs->num != 1) return false;
        m->kind = 3; m->a = dst->lhs; m->src = NULL; m->hi = c->rhs;
        return true;
    }
    if (b->stmts.len != 3 || c->op != TK_LT) return false;
    if (!twice_bound && match_u8_trade(c, i, b, m)) return true;
    /* reverse: cond is i < (S + 1) >> 1, or 2 * i < S */
    Expr *S = twice_bound;
    if (!S) {
        Expr *h = c->rhs;
        if (h && h->kind == E_IDENT && h->cname && cur_func && cur_func->body) { /* const half = (S + 1) >> 1, written before */
            Stmt *d = u8_const_decl(cur_func->body, h->cname);
            if (d && d->init && u8_prev_stmt(cur_func->body, s) == d) h = d->init; /* (right before the loop: S unchanged since) */
        }
        if (!h || h->kind != E_BINARY || h->op != TK_SHR || !h->rhs || h->rhs->kind != E_NUMBER || h->rhs->num != 1) return false;
        Expr *s1 = h->lhs;
        if (!s1 || s1->kind != E_BINARY || s1->op != TK_PLUS || !s1->rhs || s1->rhs->kind != E_NUMBER || s1->rhs->num != 1) return false;
        S = s1->lhs;
    }
    if (!bce_pure(S) || S->kind != E_IDENT) return false;
    Stmt *t = b->stmts.items[0];
    if (t->kind != S_VAR || !t->is_const || !t->cname || t->cell || !t->init) return false;
    Expr *a1 = b->stmts.items[1]->kind == S_EXPR ? b->stmts.items[1]->expr : NULL, *a2 = b->stmts.items[2]->kind == S_EXPR ? b->stmts.items[2]->expr : NULL;
    if (!a1 || !a2 || a1->kind != E_ASSIGN || a2->kind != E_ASSIGN || a1->op != TK_ASSIGN || a2->op != TK_ASSIGN) return false;
    Expr *ta = t->init;
    if (!byte_ix(ta) || !u8_name(ta->lhs) || !same_simple(ta->rhs, &iv)) return false;
    Expr *a = ta->lhs;
    if (!u8_index_of(a1->lhs, a, &iv) || !a1->rhs || a1->rhs->kind != E_INDEX || !same_simple(a1->rhs->lhs, a) || !is_sub_of(a1->rhs->rhs, S, i)) return false;
    if (!a2->lhs || a2->lhs->kind != E_INDEX || !same_simple(a2->lhs->lhs, a) || !is_sub_of(a2->lhs->rhs, S, i)) return false;
    if (!a2->rhs || a2->rhs->kind != E_IDENT || !a2->rhs->cname || strcmp(a2->rhs->cname, t->cname) != 0) return false;
    if (stmt_writes_name(b, S->cname) || stmt_writes_name(b, a->cname)) return false;
    m->kind = 2; m->a = a; m->src = NULL; m->hi = S;
    return true;
}
/* the reversal written with while (fannkuch's first program, byte arrays):
     while (2 * i < S) { const t = a[i]; a[i] = a[S - i]; a[S - i] = t; i += 1; }   (also i * 2, i++, i = i + 1)
   swaps i and S - i from the current i up: the same as reversing a[i..S - i], after which i is the first value
   with 2 * i >= S, (S + 1) >> 1. One xs_u8_rev, bounds checked once (an index out of range still throws, before
   any swap instead of after some: the program stops either way). a, S pure and not written; i an int local. */
static bool emit_u8_while_rev(Stmt *s) {
    if (xs_debug || sm_mode || ictx || getenv("XISTO_NO_U8_IDIOM")) return false;
    Expr *c = s->cond;
    if (!c || c->kind != E_BINARY || c->op != TK_LT || !bce_pure(c->rhs)) return false;
    Expr *two = c->lhs, *iv = NULL;
    if (!two || two->kind != E_BINARY || two->op != TK_STAR) return false;
    if (two->lhs && two->lhs->kind == E_NUMBER && two->lhs->num == 2) iv = two->rhs;
    else if (two->rhs && two->rhs->kind == E_NUMBER && two->rhs->num == 2) iv = two->lhs;
    if (!iv || iv->kind != E_IDENT || !iv->cname || !iv->type || iv->type->kind != TY_INT) return false;
    const char *i = iv->cname;
    Stmt *b = s->body;
    if (!b || b->kind != S_BLOCK || b->stmts.len != 4) return false;
    Expr *S = c->rhs;
    if (writes_name(S, i)) return false;
    Stmt *t = b->stmts.items[0];
    if (t->kind != S_VAR || !t->is_const || !t->cname || t->cell || !t->init) return false;
    Expr *a1 = b->stmts.items[1]->kind == S_EXPR ? b->stmts.items[1]->expr : NULL, *a2 = b->stmts.items[2]->kind == S_EXPR ? b->stmts.items[2]->expr : NULL;
    Expr *inc = b->stmts.items[3]->kind == S_EXPR ? b->stmts.items[3]->expr : NULL;
    if (!a1 || !a2 || !inc || a1->kind != E_ASSIGN || a2->kind != E_ASSIGN || a1->op != TK_ASSIGN || a2->op != TK_ASSIGN) return false;
    bool incok = (inc->kind == E_POSTFIX && inc->op == TK_PLUSPLUS && same_simple(inc->lhs, iv)) ||
                 (inc->kind == E_ASSIGN && inc->op == TK_PLUS_ASSIGN && same_simple(inc->lhs, iv) && inc->rhs && inc->rhs->kind == E_NUMBER && inc->rhs->num == 1) ||
                 (inc->kind == E_ASSIGN && inc->op == TK_ASSIGN && same_simple(inc->lhs, iv) && inc->rhs && inc->rhs->kind == E_BINARY && inc->rhs->op == TK_PLUS &&
                  same_simple(inc->rhs->lhs, iv) && inc->rhs->rhs && inc->rhs->rhs->kind == E_NUMBER && inc->rhs->rhs->num == 1);
    if (!incok) return false;
    Expr *ta = t->init;
    if (!ta || ta->kind != E_INDEX || !ta->builtin.cfn || strcmp(ta->builtin.cfn, "xs_u8_get") != 0 || !u8_name(ta->lhs) || !same_simple(ta->rhs, iv)) return false;
    Expr *a = ta->lhs;
    if (u8reg_find(a)) return false;
    if (!u8_index_of(a1->lhs, a, iv) || !a1->rhs || a1->rhs->kind != E_INDEX || !same_simple(a1->rhs->lhs, a) || !is_sub_of(a1->rhs->rhs, S, i)) return false;
    if (!a2->lhs || a2->lhs->kind != E_INDEX || !same_simple(a2->lhs->lhs, a) || !is_sub_of(a2->lhs->rhs, S, i)) return false;
    if (!a2->rhs || a2->rhs->kind != E_IDENT || !a2->rhs->cname || strcmp(a2->rhs->cname, t->cname) != 0) return false;
    if (S->kind == E_IDENT && stmt_writes_name(b, S->cname)) return false;
    if (stmt_writes_name(b, a->cname)) return false;
    int site = new_site(s->pos);
    buf_puts(out, "if (INT64_C(2) * ("); emit_expr(iv); buf_puts(out, ") < ("); emit_expr(S); buf_puts(out, ")) { xs_u8_rev(");
    emit_borrowed(a); buf_puts(out, ", "); emit_expr(iv); buf_puts(out, ", ("); emit_expr(S); buf_puts(out, ") - ("); emit_expr(iv);
    buf_printf(out, "), XS_S(%d)); ", site); emit_expr(iv); buf_puts(out, " = (("); emit_expr(S); buf_puts(out, ") + 1) >> 1; }");
    return true;
}
/* the loops `for (j = lo; j < hi; j++) dst[j] = src[j]`, the swap loop of a reversal, and `a[j] = a[j + 1]`
   on byte arrays become one runtime call each (xs_u8_copy_range, xs_u8_rev, xs_u8_shl), or one shuffle when
   the array is in a register. XISTO_NO_U8_IDIOM=1 keeps the loops. */
static const char *rev_head_name, *rev_head_n;
static Expr *rev_head_lo;
static int rev_head_site;
static bool rev_head_ix; /* the reversal is within the array (ix_prove): the read after it needs no check */
static Stmt *rev_head_stmt;
/* `for (let i = lo; i < hi; i++) D[i] = T[S[i]]` on byte arrays (a translation through a table): the runtime splits
   a large range over threads when the ranges are in bounds, T has 256 bytes and the arrays do not overlap other
   than D and S being the same bytes; otherwise (it says so) the loop runs as written. XISTO_NO_U8_MAP_PAR=1 keeps
   the loop. */
static bool emit_u8_map_par(Stmt *s) {
    if (xs_debug || sm_mode || ictx || getenv("XISTO_NO_U8_MAP_PAR") || s->kind != S_FOR) return false;
    Stmt *fi = s->for_init;
    Expr *c = s->cond, *u = s->for_update;
    if (!fi || fi->kind != S_VAR || !fi->cname || !fi->init || fi->cell || !bce_pure(fi->init)) return false;
    if (!u || u->kind != E_POSTFIX || u->op != TK_PLUSPLUS || !u->lhs || u->lhs->kind != E_IDENT || !u->lhs->cname || strcmp(u->lhs->cname, fi->cname) != 0) return false;
    if (!c || c->kind != E_BINARY || c->op != TK_LT || !c->lhs || c->lhs->kind != E_IDENT || !c->lhs->cname || strcmp(c->lhs->cname, fi->cname) != 0 || !bce_pure(c->rhs)) return false;
    Stmt *b = s->body;
    if (!b || b->kind != S_BLOCK || b->stmts.len != 1 || b->stmts.items[0]->kind != S_EXPR) return false;
    Expr *as = b->stmts.items[0]->expr;
    if (!as || as->kind != E_ASSIGN || as->op != TK_ASSIGN) return false;
    Expr iv = {0}; iv.kind = E_IDENT; iv.cname = fi->cname;
    Expr *dst = as->lhs, *tv = as->rhs;
    if (!byte_ix(dst) || !u8_name(dst->lhs) || !same_simple(dst->rhs, &iv)) return false;
    if (!byte_ix(tv) || !u8_name(tv->lhs)) return false;
    Expr *sv = tv->rhs;
    if (!byte_ix(sv) || !u8_name(sv->lhs) || !same_simple(sv->rhs, &iv)) return false;
    Expr *D = dst->lhs, *T = tv->lhs, *S = sv->lhs;
    if (u8reg_find(D) || u8reg_find(T) || u8reg_find(S)) return false;
    if (writes_name(c->rhs, fi->cname)) return false;
    buf_puts(out, "if (!xs_u8_map_par(");
    emit_borrowed(D); buf_puts(out, ", "); emit_borrowed(S); buf_puts(out, ", "); emit_borrowed(T); buf_puts(out, ", ");
    emit_expr(fi->init); buf_puts(out, ", "); emit_expr(c->rhs); buf_puts(out, ")) ");
    return false; /* (the loop follows: the fallback) */
}
/* ---- a while loop's counter with its range test at the loop's end (XISTO_NO_LOOP_CTR=1 keeps the test) ----
   while (...) { ...; c += 1; ... } where c is a local int declared with a literal in the same block before the loop
   (nothing between mentions it), `c += 1` (or c++) is the body's only mention of c at its own level and nothing else
   in the loop reads or writes c, and the body has no return, break, throw, try or yield, in a program without try:
   the increments add without the test, and one test after the loop raises the error at the increment's site. c can
   pass 2^53 - 1 only after 2^53 iterations of the loop, and nothing reads it in between (as the Map counters'
   deferred test, loop_hoist_enter). */
static Expr *dctr_e[8];
static int ndctr;
static bool dctr_has_exit(Stmt *s) {
    if (!s) return false;
    if (s->kind == S_RETURN || s->kind == S_BREAK || s->kind == S_THROW || s->kind == S_TRY || s->kind == S_YIELD) return true;
    if (dctr_has_exit(s->for_init) || dctr_has_exit(s->body) || dctr_has_exit(s->else_body)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (dctr_has_exit(s->stmts.items[i])) return true;
    for (int i = 0; i < s->ncases; i++) if (dctr_has_exit(s->cases[i].body)) return true;
    return false;
}
static bool ctr_stmt_mentions(Stmt *s, const char *x);
static bool ctr_expr_mentions(Expr *e, const char *x);
/* the increment statement's expression, and its variable, when s is `c += 1` / `c++` on a local int */
static Expr *dctr_inc(Stmt *s, const char **c) {
    if (!s || s->kind != S_EXPR || !s->expr) return NULL;
    Expr *e = s->expr;
    bool inc = (e->kind == E_ASSIGN && e->op == TK_PLUS_ASSIGN && e->rhs && e->rhs->kind == E_NUMBER && e->rhs->is_int_lit && e->rhs->ival == 1) ||
               (e->kind == E_POSTFIX && e->op == TK_PLUSPLUS);
    if (!inc || !e->type || e->type->kind != TY_INT || !e->lhs || e->lhs->kind != E_IDENT || !e->lhs->cname || e->lhs->cname[0] != 'v') return NULL;
    *c = e->lhs->cname;
    return e;
}
/* the deferred counters of while loop w (in block b at index wi): noted in dctr_e, their checks' sites in sites */
static int dctr_enter(Stmt *b, int wi, Stmt *w, Expr **found, int max) {
    if (xs_debug || sm_mode || ictx || prog_try || getenv("XISTO_NO_LOOP_CTR") || !b || !w || !w->body || w->body->kind != S_BLOCK) return 0;
    if (dctr_has_exit(w->body)) return 0;
    int n = 0;
    Stmt *body = w->body;
    for (int i = 0; i < body->stmts.len && n < max; i++) {
        const char *c = NULL;
        Expr *e = dctr_inc(body->stmts.items[i], &c);
        if (!e) continue;
        bool ok = !ctr_expr_mentions(w->cond, c);
        for (int k = 0; k < body->stmts.len && ok; k++) if (k != i && ctr_stmt_mentions(body->stmts.items[k], c)) ok = false;
        /* declared with an int literal earlier in b, nothing between it and the loop mentioning c */
        int d = -1;
        for (int k = 0; k < wi && ok; k++) {
            Stmt *t = b->stmts.items[k];
            if (t->kind == S_VAR && t->cname && strcmp(t->cname, c) == 0) d = k;
        }
        if (!ok || d < 0 || b->stmts.items[d]->cell || !b->stmts.items[d]->init || b->stmts.items[d]->init->kind != E_NUMBER ||
            !b->stmts.items[d]->init->is_int_lit || b->stmts.items[d]->init->ival < 0) continue;
        for (int k = d + 1; k < wi && ok; k++) if (ctr_stmt_mentions(b->stmts.items[k], c)) ok = false;
        if (!ok) continue;
        found[n++] = e;
    }
    for (int k = 0; k < n && ndctr < 8; k++) dctr_e[ndctr++] = found[k];
    return n;
}
static bool dctr_is(Expr *e) {
    for (int i = 0; i < ndctr; i++) if (dctr_e[i] == e) return true;
    return false;
}
/* ---- index checks of a while loop's counter (wib, XB-098) ----
   `let i = c; ...; while (... && i < E && ...) { ... i += 1; ... }` (in that block, c an int literal >= 0, nothing
   between the declaration and the loop writing i, one increment at the body's top level and no other write of i, E
   a literal or a name (+/- a literal) the loop does not write): i starts at c and only grows while i < E held, so
   everywhere in the condition and the body c <= i <= max(c, E). The loop is emitted twice: without the checks of
   a[i] (and of E's own arithmetic), run when c < a.length and E < a.length for each such a (the arrays the body
   cannot resize, bce_ok) and E is a safe integer; and as written otherwise, which fails where it failed.
   (fannkuch-redux's next permutation as one writes it first: count[i].) Off unless XISTO_WIB=1: measured on the
   Xeon E5-2686 v4 it gains nothing there (1.31-1.35 u against 1.31 u without, n = 11, one CPU): the guard runs once
   per permutation and the loop it covers rarely runs more than once. */
static bool wib_busy;
static bool wib_e_ok(Expr *e) { /* a literal, a name, or a name +/- a literal */
    if (!e || !e->type || e->type->kind != TY_INT) return false;
    if (e->kind == E_NUMBER) return e->is_int_lit && e->ival > -(INT64_C(1) << 52) && e->ival < (INT64_C(1) << 52);
    if (e->kind == E_IDENT) return e->cname && (strncmp(e->cname, "v_", 2) == 0 || e->cname[0] == 'g'); /* (a plain local or a global, not a cell) */
    if (e->kind == E_BINARY && (e->op == TK_PLUS || e->op == TK_MINUS))
        return (e->lhs->kind == E_IDENT && wib_e_ok(e->lhs) && e->rhs->kind == E_NUMBER && wib_e_ok(e->rhs)) ||
               (e->op == TK_PLUS && e->lhs->kind == E_NUMBER && wib_e_ok(e->lhs) && e->rhs->kind == E_IDENT && wib_e_ok(e->rhs));
    return false;
}
static void wib_e_c(Buf *g, Expr *e) { /* E as plain C (no overflow: names are safe integers, literals below 2^52) */
    if (e->kind == E_NUMBER) { buf_printf(g, "INT64_C(%lld)", (long long)e->ival); return; }
    if (e->kind == E_IDENT) { buf_puts(g, ren(e->cname)); return; }
    buf_puts(g, "("); wib_e_c(g, e->lhs); buf_puts(g, e->op == TK_PLUS ? " + " : " - "); wib_e_c(g, e->rhs); buf_puts(g, ")");
}
static Expr *wib_bound(Expr *c, const char *i) { /* the E of a top-level conjunct `i < E` */
    if (!c || c->kind != E_BINARY) return NULL;
    if (c->op == TK_AND) { Expr *e = wib_bound(c->lhs, i); return e ? e : wib_bound(c->rhs, i); }
    if (c->op == TK_LT && c->lhs && c->lhs->kind == E_IDENT && c->lhs->cname && strcmp(c->lhs->cname, i) == 0 && wib_e_ok(c->rhs)) return c->rhs;
    return NULL;
}
static bool wib_names_ok(Expr *e, Stmt *w) { /* E's names: not written by the loop; a global only when the body calls no user code */
    if (!e) return true;
    if (e->kind == E_IDENT) {
        if (bce_writes(w->body, e->cname) || bce_writes_e(w->cond, e->cname)) return false;
        if (!bce_plain_local(e->cname)) { bce_fixed_len = false; bool ok = bce_stmt_ok(w->body, "\x01") && bce_body_ok(w->cond, NULL, "\x01"); return ok; }
        return true;
    }
    return wib_names_ok(e->lhs, w) && wib_names_ok(e->rhs, w);
}
static void wib_arrays(Expr *e, const char *i, const char **arrs, Type **ts, int *na) {
    if (!e) return;
    if (e->kind == E_INDEX && e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname && e->rhs && e->rhs->kind == E_IDENT && e->rhs->cname &&
        strcmp(e->rhs->cname, i) == 0 && bce_indexable(e->lhs->type) && !e->builtin.cfn) {
        bool seen = false;
        for (int k = 0; k < *na; k++) if (strcmp(arrs[k], e->lhs->cname) == 0) seen = true;
        if (!seen && *na < 8) { ts[*na] = e->lhs->type; arrs[(*na)++] = e->lhs->cname; }
    }
    wib_arrays(e->lhs, i, arrs, ts, na); wib_arrays(e->rhs, i, arrs, ts, na);
    for (int k = 0; k < e->args.len; k++) wib_arrays(e->args.items[k], i, arrs, ts, na);
}
static void wib_arrays_s(Stmt *s, const char *i, const char **arrs, Type **ts, int *na) {
    if (!s) return;
    wib_arrays(s->init, i, arrs, ts, na); wib_arrays(s->expr, i, arrs, ts, na); wib_arrays(s->cond, i, arrs, ts, na); wib_arrays(s->for_update, i, arrs, ts, na);
    wib_arrays_s(s->for_init, i, arrs, ts, na); wib_arrays_s(s->body, i, arrs, ts, na); wib_arrays_s(s->else_body, i, arrs, ts, na);
    for (int k = 0; k < s->stmts.len; k++) wib_arrays_s(s->stmts.items[k], i, arrs, ts, na);
    for (int k = 0; k < s->ncases; k++) wib_arrays_s(s->cases[k].body, i, arrs, ts, na);
}
/* the guard (C) of while loop w in block b at index wi, its counter and bound in *ci and *E; NULL when it does not apply */
static char *wib_match(Stmt *b, int wi, Stmt *w, const char **ci, Expr **E, const char **arrs, int *na) {
    if (wib_busy || xs_debug || sm_mode || ictx || !getenv("XISTO_WIB") || getenv("XISTO_NO_BCE") || !b || !w->body || w->body->kind != S_BLOCK) return NULL;
    Stmt *body = w->body;
    const char *i = NULL;
    int inc = -1;
    for (int k = 0; k < body->stmts.len; k++) {
        const char *c = NULL;
        if (dctr_inc(body->stmts.items[k], &c) && wib_bound(w->cond, c)) { if (inc >= 0) return NULL; inc = k; i = c; }
    }
    if (!i) return NULL;
    for (int k = 0; k < body->stmts.len; k++) if (k != inc && bce_writes(body->stmts.items[k], i)) return NULL;
    if (bce_writes_e(w->cond, i)) return NULL;
    int d = -1;
    for (int k = 0; k < wi; k++) { Stmt *t = b->stmts.items[k]; if (t->kind == S_VAR && t->cname && strcmp(t->cname, i) == 0) d = k; }
    if (d < 0) return NULL;
    Stmt *dv = b->stmts.items[d];
    if (dv->cell || !dv->init || dv->init->kind != E_NUMBER || !dv->init->is_int_lit || dv->init->ival < 0 || dv->init->ival > RNG_LEN) return NULL;
    for (int k = d + 1; k < wi; k++) if (bce_writes(b->stmts.items[k], i)) return NULL;
    Expr *e = wib_bound(w->cond, i);
    if (!e || !wib_names_ok(e, w)) return NULL;
    Type *ts[8];
    int n = 0;
    wib_arrays(w->cond, i, arrs, ts, &n);
    wib_arrays_s(body, i, arrs, ts, &n);
    int keep = 0;
    for (int k = 0; k < n; k++) if (bce_ok(body, arrs[k], ts[k], false) && bce_body_ok(w->cond, NULL, arrs[k])) { ts[keep] = ts[k]; arrs[keep++] = arrs[k]; }
    if (!keep) return NULL;
    Buf g = {0};
    buf_puts(&g, "(");
    wib_e_c(&g, e);
    buf_puts(&g, ") >= -INT64_C(9007199254740991)");
    for (int k = 0; k < keep; k++) {
        int fk = fx_find(arrs[k]);
        const char *len = fk >= 0 && fx_decl[fk] ? xs_sprintf("%s_fxl", ren(arrs[k])) : bce_len_c(arrs[k], ts[k]); /* (a C local under fix-hoist) */
        buf_printf(&g, " && INT64_C(%lld) < %s && (", (long long)dv->init->ival, len);
        wib_e_c(&g, e);
        buf_printf(&g, ") < %s", len);
    }
    *ci = i; *E = e; *na = keep;
    return g.data;
}
static bool emit_u8_idiom(Stmt *s) {
    if (xs_debug || sm_mode || ictx || getenv("XISTO_NO_U8_IDIOM")) return false;
    U8Idiom m;
    if (!match_u8_idiom(s, &m)) return false;
    if (m.kind == 4) { /* xs_u8_trade with the table's context set up once, at the function's entry */
        if (getenv("XISTO_NO_U8_TRADE") || !fn_prelude_out || fn_prelude_out != out || !cur_func || u8reg_find(m.a) || u8reg_find(m.src)) return false;
        bool param = false; /* T is in scope at the entry: a parameter, or a global (the function's locals are v_*) */
        for (int k = 0; k < cur_func->nparams; k++) if (cur_func->params[k].cname && strcmp(cur_func->params[k].cname, m.src->cname) == 0) param = true;
        if (!param && strncmp(m.src->cname, "g", 1) != 0) return false;
        if (cur_func->body && stmt_writes_name(cur_func->body, m.src->cname)) return false;
        int site = new_site(s->pos);
        buf_printf(&fn_prelude_text, "\n    xs_u8_tr_ctx xs_trc%d_ = xs_u8_tr_setup(%s);", site, ren(m.src->cname));
        buf_printf(out, "xs_u8_trade(&xs_trc%d_, ", site); emit_borrowed(m.a); buf_puts(out, ", (");
        emit_expr(m.ca); buf_puts(out, ") + ("); emit_expr(m.lo); buf_puts(out, "), (");
        emit_expr(m.cb); buf_puts(out, ") - ("); emit_expr(m.lo); buf_puts(out, "), (");
        emit_expr(m.hi); buf_puts(out, ") - ("); emit_expr(m.lo); buf_printf(out, "), XS_S(%d));", site);
        return true;
    }
    int site = new_site(s->pos);
    U8Reg *ra = u8reg_find(m.a), *rs = m.kind == 1 ? u8reg_find(m.src) : NULL;
    if (m.kind == 1) {
        if (ra && rs) { emit_u8reg_name(m.a); buf_puts(out, " = xs_v16_copy_rr("); emit_u8reg_name(m.a); buf_puts(out, ", "); emit_u8reg_name(m.src); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", "); emit_expr(m.hi); buf_printf(out, ", %s, %s, XS_S(%d));", rgn(ra), rgn(rs), site); }
        else if (ra) { emit_u8reg_name(m.a); buf_puts(out, " = xs_v16_copy_in("); emit_u8reg_name(m.a); buf_puts(out, ", "); emit_borrowed(m.src); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", "); emit_expr(m.hi); buf_printf(out, ", %s, XS_S(%d));", rgn(ra), site); }
        else if (rs) { buf_puts(out, "xs_v16_copy_out("); emit_borrowed(m.a); buf_puts(out, ", "); emit_u8reg_name(m.src); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", "); emit_expr(m.hi); buf_printf(out, ", %s, XS_S(%d));", rgn(rs), site); }
        else { buf_puts(out, "xs_u8_copy_range("); emit_borrowed(m.a); buf_puts(out, ", "); emit_borrowed(m.src); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", "); emit_expr(m.hi); buf_printf(out, ", XS_S(%d));", site); }
        return true;
    }
    if (m.kind == 2) {
        /* the element the reversal brings to lo (old a[hi], or a[lo] when lo >= hi), read from the value before
           the shuffle: a read of a[lo] right after (k = perm[0] in a flip loop) takes it without waiting for the
           shuffle (rev_head below) */
        /* a[lo..hi] with lo the literal 0 and hi an index value of a (ix_prove): within a, nothing to check */
        bool ixr = false;
        if (ra && cur_func) {
            ix_prove(cur_func->body);
            ixr = ix_arr_ok(m.a) && m.lo && m.lo->kind == E_NUMBER && m.lo->is_int_lit && m.lo->ival == 0 && ix_val(m.hi);
        }
        if (ra && fn_prelude_out == out && !getenv("XISTO_NO_REV_HEAD")) {
            buf_printf(&fn_prelude_text, "\n    int64_t rh%d_ = 0; (void)rh%d_;", site, site);
            buf_printf(out, ixr ? "rh%d_ = xs_v16_rev_head_nc(" : "rh%d_ = xs_v16_rev_head(", site); emit_u8reg_name(m.a); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", "); emit_expr(m.hi); buf_printf(out, ", %s); ", rgn(ra));
            rev_head_name = m.a->cname;
            rev_head_lo = m.lo;
            rev_head_site = site;
            rev_head_n = rgn(ra);
            rev_head_ix = ixr;
            rev_head_stmt = s; /* for the next statement of the same block only */
        }
        if (ra && ixr) { emit_u8reg_name(m.a); buf_puts(out, " = xs_v16_rev_nc("); emit_u8reg_name(m.a); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", "); emit_expr(m.hi); buf_puts(out, ");"); }
        else if (ra) { emit_u8reg_name(m.a); buf_puts(out, " = xs_v16_rev("); emit_u8reg_name(m.a); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", ("); emit_expr(m.hi); buf_puts(out, ") - ("); emit_expr(m.lo); buf_printf(out, "), %s, XS_S(%d));", rgn(ra), site); }
        else { buf_puts(out, "xs_u8_rev("); emit_borrowed(m.a); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", ("); emit_expr(m.hi); buf_puts(out, ") - ("); emit_expr(m.lo); buf_printf(out, "), XS_S(%d));", site); }
        return true;
    }
    /* shift: the last j is K (<=) or K - 1 (<) */
    const char *last = s->cond->op == TK_LE ? "" : " - INT64_C(1)";
    if (ra) { emit_u8reg_name(m.a); buf_puts(out, " = xs_v16_shl("); emit_u8reg_name(m.a); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", ("); emit_expr(m.hi); buf_printf(out, ")%s, %s, XS_S(%d));", last, rgn(ra), site); }
    else { buf_puts(out, "xs_u8_shl("); emit_borrowed(m.a); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", ("); emit_expr(m.hi); buf_printf(out, ")%s, XS_S(%d));", last, site); }
    return true;
}
/* ---- u8reg: which byte arrays a loop may keep in registers ----
   Candidates: `const a = new Uint8Array(N)` (N literal <= 16) declared in the current function and, in the whole
   function, only ever indexed (a[i] read, a[i] = v, the idioms above): never passed, stored, returned, measured
   (.length), iterated, captured by an arrow. In the loop every use must be a read, a plain store or an idiom
   (a[i]++ and a[i] += v stay memory forms: such a loop is not promoted). */
static bool u8_use_ok(Expr *e, const char *c, bool in_loop);
static bool stmt_mentions_name(Stmt *s, const char *c);
static bool u8_stmt_ok(Stmt *s, const char *c, bool in_loop) {
    if (!s) return true;
    if (in_loop && s->kind == S_TRY) return false;
    if (in_loop && s->kind == S_FOR && !getenv("XISTO_NO_U8_IDIOM")) {
        U8Idiom m;
        if (match_u8_idiom(s, &m) && m.kind != 4) return u8_use_ok(m.lo, c, in_loop) && u8_use_ok(m.hi, c, in_loop); /* the idiom's own uses are fine */
    }
    if (s->kind == S_VAR && s->cname && strcmp(s->cname, c) == 0) return !in_loop; /* its declaration (outside the loop) */
    if (!u8_use_ok(s->init, c, in_loop) || !u8_use_ok(s->expr, c, in_loop) || !u8_use_ok(s->cond, c, in_loop) || !u8_use_ok(s->for_update, c, in_loop)) return false;
    if (!u8_stmt_ok(s->for_init, c, in_loop) || !u8_stmt_ok(s->body, c, in_loop) || !u8_stmt_ok(s->else_body, c, in_loop)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!u8_stmt_ok(s->stmts.items[i], c, in_loop)) return false;
    for (int i = 0; i < s->ncases; i++) if (!u8_use_ok(s->cases[i].value, c, in_loop) || !u8_stmt_ok(s->cases[i].body, c, in_loop)) return false;
    return true;
}
static bool int_arr_type(Type *t) { return t && t->kind == TY_ARRAY && t->elem && t->elem->kind == TY_INT; }
static bool u8_is_index_on(Expr *e, const char *c) {
    if (!e || e->kind != E_INDEX || !e->lhs || e->lhs->kind != E_IDENT || !e->lhs->cname || strcmp(e->lhs->cname, c) != 0) return false;
    if (e->builtin.cfn) return strcmp(e->builtin.cfn, "xs_u8_get") == 0;
    return int_arr_type(e->lhs->type); /* an int[] (u8reg: the ones proved to hold bytes) */
}
static bool u8_use_ok(Expr *e, const char *c, bool in_loop) {
    if (!e) return true;
    if (e->kind == E_IDENT) return !(e->cname && strcmp(e->cname, c) == 0); /* a bare use: it escapes */
    if (e->kind == E_ARROW) return e->arrow && e->arrow->fn && u8_stmt_ok(e->arrow->fn->body, c, in_loop) && !stmt_mentions_name(e->arrow->fn->body, c);
    if (u8_is_index_on(e, c)) return u8_use_ok(e->rhs, c, in_loop); /* a read */
    if ((e->kind == E_ASSIGN || e->kind == E_POSTFIX || e->kind == E_UNARY) && u8_is_index_on(e->lhs, c)) {
        if (in_loop && !(e->kind == E_ASSIGN && e->op == TK_ASSIGN)) return false; /* a[i]++, a[i] += v */
        return u8_use_ok(e->lhs->rhs, c, in_loop) && u8_use_ok(e->rhs, c, in_loop);
    }
    if (!u8_use_ok(e->lhs, c, in_loop) || !u8_use_ok(e->rhs, c, in_loop)) return false;
    for (int i = 0; i < e->args.len; i++) if (!u8_use_ok(e->args.items[i], c, in_loop)) return false;
    return true;
}
static bool expr_mentions_name(Expr *e, const char *c) {
    if (!e) return false;
    if (e->kind == E_IDENT) return e->cname && strcmp(e->cname, c) == 0;
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn && stmt_mentions_name(e->arrow->fn->body, c)) return true;
    if (expr_mentions_name(e->lhs, c) || expr_mentions_name(e->rhs, c)) return true;
    for (int i = 0; i < e->args.len; i++) if (expr_mentions_name(e->args.items[i], c)) return true;
    return false;
}
static bool stmt_mentions_name(Stmt *s, const char *c) {
    if (!s) return false;
    if (expr_mentions_name(s->init, c) || expr_mentions_name(s->expr, c) || expr_mentions_name(s->cond, c) || expr_mentions_name(s->for_update, c)) return true;
    if (stmt_mentions_name(s->for_init, c) || stmt_mentions_name(s->body, c) || stmt_mentions_name(s->else_body, c)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (stmt_mentions_name(s->stmts.items[i], c)) return true;
    for (int i = 0; i < s->ncases; i++) if (expr_mentions_name(s->cases[i].value, c) || stmt_mentions_name(s->cases[i].body, c)) return true;
    return false;
}
/* the fixed byte arrays declared in the function body, as candidates */
static void u8_candidates(Stmt *s, U8Reg *c, int *n) {
    if (!s) return;
    if (s->kind == S_VAR && *n < 8) {
        int64_t len = bce_fixed_of(s);
        if (len && len <= 16 && s->init->builtin.cfn && strcmp(s->init->builtin.cfn, "xs_u8_new") == 0) c[(*n)++] = (U8Reg){s->cname, len};
    }
    u8_candidates(s->for_init, c, n); u8_candidates(s->body, c, n); u8_candidates(s->else_body, c, n);
    for (int i = 0; i < s->stmts.len; i++) u8_candidates(s->stmts.items[i], c, n);
    for (int i = 0; i < s->ncases; i++) u8_candidates(s->cases[i].body, c, n);
}
/* ---- int[] that hold bytes: register candidates too ----
   `const a: int[] = new Array<int>(N).fill(K)` among the function body's statements, K a literal 0..255, only
   indexed (u8_stmt_ok, as for a Uint8Array), and every value stored into it a byte: a literal 0..255, a
   Uint8Array's element, such an int[]'s element, an int local whose every value is one of these, c ? x : y of
   two, or the store's own index (a[i] = i: below the length, which the register loop's guard keeps <= 16; the
   length never changes). Proved together, as a fixpoint: all assumed, then the ones with another store dropped
   until none is. XISTO_NO_INT_REG=1 turns it off. */
typedef struct { const char *cname; bool ok; int top; } BvName; /* top: the index of its body statement (arrays) */
static BvName bv_arr[16], bv_loc[256];
static int nbv_arr, nbv_loc;
static Stmt *bv_body; /* the body the proof is for */
static const char *i64_deny[16]; /* int[]s the loop being emitted keeps in memory (the guard's other branch) */
static int ni64_deny;
static BvName *bv_find(BvName *v, int n, const char *c) {
    for (int k = 0; k < n; k++) if (strcmp(v[k].cname, c) == 0) return &v[k];
    return NULL;
}
static bool bv_byte(Expr *e) {
    if (!e) return false;
    if (e->kind == E_NUMBER) return e->is_int_lit && e->ival >= 0 && e->ival <= 255;
    if (e->kind == E_INDEX && e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname) {
        if (e->builtin.cfn) return strcmp(e->builtin.cfn, "xs_u8_get") == 0;
        BvName *a = bv_find(bv_arr, nbv_arr, e->lhs->cname);
        return a && a->ok;
    }
    if (e->kind == E_IDENT && e->cname) { BvName *l = bv_find(bv_loc, nbv_loc, e->cname); return l && l->ok; }
    if (e->kind == E_COND) return e->args.len == 2 && bv_byte(e->args.items[0]) && bv_byte(e->args.items[1]);
    return false;
}
static void bv_locals(Stmt *s) {
    if (!s) return;
    if (s->kind == S_VAR && s->cname && !s->cell && s->type && s->type->kind == TY_INT && nbv_loc < 256) bv_loc[nbv_loc++] = (BvName){s->cname, true, 0};
    bv_locals(s->for_init); bv_locals(s->body); bv_locals(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) bv_locals(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) bv_locals(s->cases[i].body);
}
static void bv_drop(BvName *n, bool *changed) { if (n && n->ok) { n->ok = false; *changed = true; } }
static void bv_stmt(Stmt *s, bool *changed);
static void bv_expr(Expr *e, bool *changed) {
    if (!e) return;
    if (e->kind == E_ASSIGN || e->kind == E_POSTFIX || (e->kind == E_UNARY && (e->op == TK_PLUSPLUS || e->op == TK_MINUSMINUS))) {
        Expr *l = e->lhs;
        bool plain = e->kind == E_ASSIGN && e->op == TK_ASSIGN;
        if (l && l->kind == E_IDENT && l->cname) {
            BvName *n = bv_find(bv_loc, nbv_loc, l->cname);
            if (n && n->ok && (!plain || !bv_byte(e->rhs))) bv_drop(n, changed);
        } else if (l && l->kind == E_INDEX && !l->builtin.cfn && l->lhs && l->lhs->kind == E_IDENT && l->lhs->cname) {
            BvName *a = bv_find(bv_arr, nbv_arr, l->lhs->cname);
            if (a && a->ok && (!plain || !(bv_byte(e->rhs) || (bce_pure(e->rhs) && same_simple(e->rhs, l->rhs))))) bv_drop(a, changed);
        }
    }
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn) bv_stmt(e->arrow->fn->body, changed);
    bv_expr(e->lhs, changed); bv_expr(e->rhs, changed);
    for (int i = 0; i < e->args.len; i++) bv_expr(e->args.items[i], changed);
}
static void bv_stmt(Stmt *s, bool *changed) {
    if (!s) return;
    if (s->kind == S_VAR && s->cname) {
        BvName *n = bv_find(bv_loc, nbv_loc, s->cname);
        if (n && n->ok && !bv_byte(s->init)) bv_drop(n, changed);
    }
    bv_expr(s->init, changed); bv_expr(s->expr, changed); bv_expr(s->cond, changed); bv_expr(s->for_update, changed);
    bv_stmt(s->for_init, changed); bv_stmt(s->body, changed); bv_stmt(s->else_body, changed);
    for (int i = 0; i < s->stmts.len; i++) bv_stmt(s->stmts.items[i], changed);
    for (int i = 0; i < s->ncases; i++) { bv_expr(s->cases[i].value, changed); bv_stmt(s->cases[i].body, changed); }
}
/* `new Array<int>(N).fill(K)`, K a literal byte */
static bool bv_new_bytes(Expr *e) {
    if (!e || e->kind != E_MEMBER || !e->is_call || !e->str || strcmp(e->str, "fill") != 0 || !e->lhs || e->lhs->kind != E_NEW_ARRAY || !e->lhs->lhs) return false;
    if (e->args.len != 1) return false;
    Expr *k = e->args.items[0];
    return k && k->kind == E_NUMBER && k->is_int_lit && k->ival >= 0 && k->ival <= 255;
}
static void bv_prove(Stmt *body) {
    if (bv_body == body) return;
    bv_body = body;
    nbv_arr = nbv_loc = 0;
    if (!body || body->kind != S_BLOCK || getenv("XISTO_NO_INT_REG")) return;
    for (int i = 0; i < body->stmts.len && nbv_arr < 16; i++) {
        Stmt *d = body->stmts.items[i];
        if (d->kind != S_VAR || !d->is_const || !d->cname || d->cell || !int_arr_type(d->type) || !bv_new_bytes(d->init)) continue;
        if (!u8_stmt_ok(body, d->cname, false)) continue; /* escapes, or measured */
        bv_arr[nbv_arr++] = (BvName){d->cname, true, i};
    }
    if (!nbv_arr) return;
    bv_locals(body);
    bool changed = true;
    while (changed) { changed = false; bv_stmt(body, &changed); }
}
/* ---- int[]s that hold indices of themselves ----
   `const a: int[] = new Array<int>(L).fill(0)` among the function body's statements (L a literal or a top-level
   const: every such array has the same length), only indexed (u8_stmt_ok), and every value stored into one of them
   an index value: an element of one of them, an int local whose every value is one, c ? x : y of two, or the
   store's own index (a[i] = i: the store happens only when i < L). An index value is then in [0, L), and some
   element exists, so L >= 1. Fannkuch's permutations: perm1[i] = i, then only moves of their own elements;
   k = perm[0] can be no other than an index, so reversing perm[0..k] and reading perm[0] need no check.
   Proved as a fixpoint, like the bytes above. XISTO_NO_IX_PROOF=1 turns it off. */
static BvName ix_arr[16], ix_loc[256];
static int nix_arr, nix_loc;
static Stmt *ix_body;
static bool ix_val(Expr *e) {
    if (!e) return false;
    if (e->kind == E_INDEX && !e->builtin.cfn && e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname) {
        BvName *a = bv_find(ix_arr, nix_arr, e->lhs->cname);
        return a && a->ok;
    }
    if (e->kind == E_IDENT && e->cname) { BvName *l = bv_find(ix_loc, nix_loc, e->cname); return l && l->ok; }
    if (e->kind == E_COND) return e->args.len == 2 && ix_val(e->args.items[0]) && ix_val(e->args.items[1]);
    return false;
}
static void ix_stmt(Stmt *s, bool *changed);
static void ix_expr(Expr *e, bool *changed) {
    if (!e) return;
    if (e->kind == E_ASSIGN || e->kind == E_POSTFIX || (e->kind == E_UNARY && (e->op == TK_PLUSPLUS || e->op == TK_MINUSMINUS))) {
        Expr *l = e->lhs;
        bool plain = e->kind == E_ASSIGN && e->op == TK_ASSIGN;
        if (l && l->kind == E_IDENT && l->cname) {
            BvName *n = bv_find(ix_loc, nix_loc, l->cname);
            if (n && n->ok && (!plain || !ix_val(e->rhs))) bv_drop(n, changed);
        } else if (l && l->kind == E_INDEX && !l->builtin.cfn && l->lhs && l->lhs->kind == E_IDENT && l->lhs->cname) {
            BvName *a = bv_find(ix_arr, nix_arr, l->lhs->cname);
            if (a && a->ok && (!plain || !(ix_val(e->rhs) || (bce_pure(e->rhs) && same_simple(e->rhs, l->rhs))))) bv_drop(a, changed);
        }
    }
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn) ix_stmt(e->arrow->fn->body, changed);
    ix_expr(e->lhs, changed); ix_expr(e->rhs, changed);
    for (int i = 0; i < e->args.len; i++) ix_expr(e->args.items[i], changed);
}
static void ix_stmt(Stmt *s, bool *changed) {
    if (!s) return;
    if (s->kind == S_VAR && s->cname) {
        BvName *n = bv_find(ix_loc, nix_loc, s->cname);
        if (n && n->ok && !ix_val(s->init)) bv_drop(n, changed);
    }
    ix_expr(s->init, changed); ix_expr(s->expr, changed); ix_expr(s->cond, changed); ix_expr(s->for_update, changed);
    ix_stmt(s->for_init, changed); ix_stmt(s->body, changed); ix_stmt(s->else_body, changed);
    for (int i = 0; i < s->stmts.len; i++) ix_stmt(s->stmts.items[i], changed);
    for (int i = 0; i < s->ncases; i++) { ix_expr(s->cases[i].value, changed); ix_stmt(s->cases[i].body, changed); }
}
/* the length of `new Array<int>(L).fill(0)` when L is a literal or a top-level const (the same value for every
   array made with it), else NULL */
static Expr *ix_new_len(Expr *e) {
    if (!e || e->kind != E_MEMBER || !e->is_call || !e->str || strcmp(e->str, "fill") != 0 || !e->lhs || e->lhs->kind != E_NEW_ARRAY || !e->lhs->lhs) return NULL;
    if (e->args.len != 1 || !e->args.items[0] || e->args.items[0]->kind != E_NUMBER || !e->args.items[0]->is_int_lit || e->args.items[0]->ival != 0) return NULL;
    Expr *L = e->lhs->lhs;
    if (L->kind == E_NUMBER && L->is_int_lit) return L;
    if (L->kind != E_IDENT || !L->cname || !prog) return NULL;
    for (int i = 0; i < prog->top.len; i++) {
        Stmt *t = prog->top.items[i];
        if (t->kind == S_VAR && t->is_const && t->cname && strcmp(t->cname, L->cname) == 0) return L;
    }
    return NULL;
}
static void ix_prove(Stmt *body) {
    if (ix_body == body) return;
    ix_body = body;
    nix_arr = nix_loc = 0;
    if (!body || body->kind != S_BLOCK || getenv("XISTO_NO_IX_PROOF") || getenv("XISTO_NO_INT_REG")) return;
    Expr *len = NULL;
    for (int i = 0; i < body->stmts.len && nix_arr < 16; i++) {
        Stmt *d = body->stmts.items[i];
        if (d->kind != S_VAR || !d->is_const || !d->cname || d->cell || !int_arr_type(d->type)) continue;
        Expr *L = ix_new_len(d->init);
        if (!L || (len && !same_simple(L, len))) continue;
        if (!u8_stmt_ok(body, d->cname, false)) continue;
        len = L;
        ix_arr[nix_arr++] = (BvName){d->cname, true, i};
    }
    if (!nix_arr) return;
    nix_loc = 0;
    {   /* the body's int locals */
        Stmt *stack[512]; int sp = 0; stack[sp++] = body;
        while (sp) {
            Stmt *s = stack[--sp];
            if (!s) continue;
            if (s->kind == S_VAR && s->cname && !s->cell && s->type && s->type->kind == TY_INT && nix_loc < 256) ix_loc[nix_loc++] = (BvName){s->cname, true, 0};
            if (sp < 500) { stack[sp++] = s->for_init; stack[sp++] = s->body; stack[sp++] = s->else_body; }
            for (int i = 0; i < s->stmts.len && sp < 510; i++) stack[sp++] = s->stmts.items[i];
            for (int i = 0; i < s->ncases && sp < 510; i++) stack[sp++] = s->cases[i].body;
        }
    }
    bool changed = true;
    while (changed) { changed = false; ix_stmt(body, &changed); }
}
static bool ix_arr_ok(Expr *a) {
    if (!a || a->kind != E_IDENT || !a->cname) return false;
    BvName *n = bv_find(ix_arr, nix_arr, a->cname);
    return n && n->ok;
}
/* an int[] in a register pays off only where a copy, reversal or shift of it becomes one shuffle: element by
   element, a lane extract and a blend cost more than the load and store they replace */
static bool i64_idiom_in(Stmt *s, const char *c) {
    if (!s) return false;
    U8Idiom m;
    if (s->kind == S_FOR && match_u8_idiom(s, &m) && m.kind >= 1 && m.kind <= 3 &&
        ((m.a && m.a->cname && strcmp(m.a->cname, c) == 0) || (m.src && m.src->cname && strcmp(m.src->cname, c) == 0))) return true;
    if (i64_idiom_in(s->for_init, c) || i64_idiom_in(s->body, c) || i64_idiom_in(s->else_body, c)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (i64_idiom_in(s->stmts.items[i], c)) return true;
    for (int i = 0; i < s->ncases; i++) if (i64_idiom_in(s->cases[i].body, c)) return true;
    return false;
}
/* chosen[0..n): the arrays the loop may hold; the int[]s among them that no idiom uses are dropped (all held
   while the idioms are matched: a copy needs both sides in registers) */
static int i64_pays(Stmt *s, U8Reg *chosen, int n) {
    int base = nu8regs;
    for (int k = 0; k < n && nu8regs < 8; k++) u8regs[nu8regs++] = chosen[k];
    bool keep[8];
    for (int k = 0; k < n; k++) keep[k] = !chosen[k].i64 || i64_idiom_in(s->body, chosen[k].cname);
    nu8regs = base;
    int m = 0;
    for (int k = 0; k < n; k++) if (keep[k]) chosen[m++] = chosen[k];
    return m;
}
static bool stmt_contains(Stmt *s, Stmt *t) {
    if (!s) return false;
    if (s == t) return true;
    if (stmt_contains(s->for_init, t) || stmt_contains(s->body, t) || stmt_contains(s->else_body, t)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (stmt_contains(s->stmts.items[i], t)) return true;
    for (int i = 0; i < s->ncases; i++) if (stmt_contains(s->cases[i].body, t)) return true;
    return false;
}
/* The loop s: its registers are chosen, loaded before it, the loop is emitted (by emit_stmt_), the registers
   stored after. Returns false when there is nothing to promote. XISTO_NO_U8_REG=1 turns it off. With an int[]
   among them the loop is emitted twice: in registers when every proved int[] declared before it has at most 16
   elements (the bytes proof needs that of the ones it reads too), in memory otherwise. */
static bool u8reg_loop(Stmt *s) {
    if (xs_debug || sm_mode || ictx || !cur_func || !cur_func->body || stmt_has_try(cur_func->body) || getenv("XISTO_NO_U8_REG") || getenv("XISTO_NO_U8_IDIOM")) return false;
    if (s->kind != S_WHILE && s->kind != S_FOR) return false;
    U8Idiom m;
    if (s->kind == S_FOR && match_u8_idiom(s, &m)) return false; /* one shuffle already */
    U8Reg cand[8]; int nc = 0;
    u8_candidates(cur_func->body, cand, &nc);
    bv_prove(cur_func->body);
    int top = -1; /* the body statement holding s */
    if (nbv_arr && cur_func->body->kind == S_BLOCK)
        for (int i = 0; i < cur_func->body->stmts.len; i++) if (stmt_contains(cur_func->body->stmts.items[i], s)) { top = i; break; }
    for (int k = 0; k < nbv_arr && nc < 8; k++) {
        if (!bv_arr[k].ok || bv_arr[k].top >= top) continue;
        bool denied = false;
        for (int d = 0; d < ni64_deny; d++) if (strcmp(i64_deny[d], bv_arr[k].cname) == 0) denied = true;
        if (!denied) cand[nc++] = (U8Reg){bv_arr[k].cname, 0, true};
    }
    U8Reg chosen[8]; int nch = 0;
    bool any_i64 = false;
    for (int k = 0; k < nc && nu8regs + nch < 8; k++) {
        Expr iv = {0}; iv.kind = E_IDENT; iv.cname = (char *)cand[k].cname;
        if (u8reg_find(&iv)) continue; /* an outer loop holds it already */
        if (!stmt_mentions_name(s, cand[k].cname)) continue; /* not used here */
        if (!u8_stmt_ok(cur_func->body, cand[k].cname, false)) continue; /* escapes somewhere */
        if (!u8_stmt_ok(s, cand[k].cname, true)) continue; /* a use the register cannot serve */
        chosen[nch++] = cand[k];
    }
    nch = i64_pays(s, chosen, nch);
    for (int k = 0; k < nch; k++) if (chosen[k].i64) any_i64 = true;
    if (!nch) return false;
    int base = nu8regs;
    buf_puts(out, "{");
    indent++;
    if (any_i64) {
        nl();
        buf_puts(out, "if (1");
        for (int k = 0; k < nbv_arr; k++)
            if (bv_arr[k].ok && bv_arr[k].top < top) buf_printf(out, " && %s->len <= 16", ren(bv_arr[k].cname));
        buf_puts(out, ") {");
        indent++;
    }
    for (int k = 0; k < nch; k++) {
        u8regs[nu8regs++] = chosen[k];
        nl();
        if (chosen[k].i64) {
            buf_printf(out, "int64_t nr_%s = (int64_t)%s->len | XS_V16_ARR; ", chosen[k].cname, ren(chosen[k].cname));
            buf_printf(out, "xs_u8x16 r_%s = xs_v16_load_i64(%s->data, (int64_t)%s->len);", chosen[k].cname, ren(chosen[k].cname), ren(chosen[k].cname));
        } else
            buf_printf(out, "xs_u8x16 r_%s = xs_v16_load(%s->data, INT64_C(%lld));", chosen[k].cname, ren(chosen[k].cname), (long long)chosen[k].n);
    }
    nl();
    emit_stmt_(s);
    for (int k = 0; k < nch; k++) {
        nl();
        if (chosen[k].i64) buf_printf(out, "xs_v16_store_i64(%s->data, r_%s, (int64_t)%s->len);", ren(chosen[k].cname), chosen[k].cname, ren(chosen[k].cname));
        else buf_printf(out, "xs_v16_store(%s->data, r_%s, INT64_C(%lld));", ren(chosen[k].cname), chosen[k].cname, (long long)chosen[k].n);
    }
    nu8regs = base;
    if (any_i64) {
        indent--;
        nl();
        buf_puts(out, "} else {");
        indent++;
        int saved_deny = ni64_deny;
        for (int k = 0; k < nch; k++) if (chosen[k].i64 && ni64_deny < 16) i64_deny[ni64_deny++] = chosen[k].cname;
        nl();
        emit_stmt_(s);
        ni64_deny = saved_deny;
        indent--;
        nl();
        buf_puts(out, "}");
    }
    indent--;
    nl();
    buf_puts(out, "}");
    return true;
}
/* the loop, its counters' increments unchecked while it is emitted, then the test on their bound (ctr_scan) */
static void emit_stmt_for_inner(Stmt *s);
static void ctr_emit_for(Stmt *s) {
    int li = ctr_loop_of(s);
    if (li < 0) { emit_stmt_for_inner(s); return; }
    CtrLoop *cl = &ctr_loops[li];
    buf_puts(out, "{ ");
    cl->armed = true;
    emit_stmt_for_inner(s);
    cl->armed = false;
    buf_puts(out, " if (__builtin_expect((int64_t)(");
    emit_expr(s->cond->rhs);
    buf_puts(out, ") - (int64_t)(");
    emit_expr(s->for_init->init);
    buf_printf(out, ") > (XS_ISAFE_MAX - INT64_C(%lld)) / %d, 0)) xs_arr_int_check_range(%s", (long long)cl->k, cl->c, ren(cl->x));
    site_arg(cl->first->pos);
    buf_puts(out, "); }");
}

static void emit_stmt_for_inner(Stmt *s) { /* a for: bounds checks proved by the loop, or a versioned copy */
    int saved_nbce = nbce;
    bce_enter(s);
    if (nbce == saved_nbce) { /* not a[i] < a.length: maybe a loop to version (bce_version_guard) */
        const char *arrs[8];
        int na = 0;
        char *g = bce_version_guard(s, arrs, &na);
        if (g) {
            buf_printf(out, "if (%s) ", g);
            for (int k = 0; k < na; k++) bce_loops[nbce++] = (BceLoop){s->for_update->lhs->cname, arrs[k], true, NULL};
            int naff = nbce_aff;
            BceAff aff[16];
            memcpy(aff, bce_aff, sizeof aff);
            for (int k = 0; k < naff && nbce < 16; k++) bce_loops[nbce++] = (BceLoop){s->for_update->lhs->cname, aff[k].a, true, aff[k].idx};
            bce_versions++;
            emit_for_(s);
            nbce = saved_nbce;
            buf_puts(out, " else ");
            emit_for_(s);
            bce_versions--;
            return;
        }
    }
    emit_for_(s);
    nbce = saved_nbce;
    return;
    }
static bool same_simple(Expr *a, Expr *b);
static void emit_stmt_(Stmt *s) {
    used_ar = false;
    if (rev_head_stmt) {
        /* the statement right after a register reversal: `x = a[lo]` takes the element the reversal put there */
        Stmt *prev = rev_head_stmt;
        rev_head_stmt = NULL;
        if (blk_prev_stmt == prev) {
        Expr *e = s->kind == S_EXPR ? s->expr : NULL;
        if (e && e->kind == E_ASSIGN && e->op == TK_ASSIGN && e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname &&
            e->lhs->type && e->lhs->type->kind == TY_INT && !u8reg_find(e->lhs) && e->rhs && e->rhs->kind == E_INDEX &&
            e->rhs->lhs && e->rhs->lhs->kind == E_IDENT && e->rhs->lhs->cname && strcmp(e->rhs->lhs->cname, rev_head_name) == 0 &&
            same_simple(e->rhs->rhs, rev_head_lo) && bce_pure(rev_head_lo)) {
            int site = new_site(s->pos);
            if (rev_head_ix) buf_printf(out, "(%s = rh%d_);", ren(e->lhs->cname), rev_head_site); /* a[0]: some index value exists, so a is not empty */
            else { buf_printf(out, "(%s = xs_v16_head_after(rh%d_, ", ren(e->lhs->cname), rev_head_site); emit_expr(rev_head_lo); buf_printf(out, ", %s, XS_S(%d)));", rev_head_n, site); }
            drain_if_needed();
            return;
        }
        }
    }
    if (s->kind == S_FOR && lane_loop(s)) { drain_if_needed(); return; }
    if ((s->kind == S_WHILE || s->kind == S_FOR) && u8reg_loop(s)) { drain_if_needed(); return; }
    switch (s->kind) {
    case S_VAR:
        if (s->cname && s->init && !ictx && nzv_has(s->cname)) { /* XB-087: the double first, the int from it */
            buf_printf(out, "double %s_z = ", s->cname);
            emit_nzd(s->init);
            buf_puts(out, "; ");
            Expr *saved = s->init, tmp = *saved;
            tmp.kind = E_IDENT;
            tmp.cname = xs_sprintf("((int64_t)%s_z)", s->cname);
            s->init = &tmp;
            emit_var(s);
            s->init = saved;
            drain_if_needed();
            return;
        }
        emit_var(s);
        { int k = s->cname ? fx_find(s->cname) : -1;
          if (k >= 0 && s->type && s->type->elem) {
              const char *et = s->type->elem->kind == TY_INT ? "int64_t" : "double";
              buf_printf(out, " %s *%s_fxd = (%s)->data; int64_t %s_fxl = (int64_t)(%s)->len; (void)%s_fxd; (void)%s_fxl;", et, ren(s->cname), ren(s->cname), ren(s->cname), ren(s->cname), ren(s->cname), ren(s->cname));
              fx_decl[k] = true;
          } }
        drain_if_needed();
        return;
    case S_EXPR:
        if (reuse_assign(s->expr)) { drain_if_needed(); return; }
        if (is_fresh(s->expr)) {
            buf_puts(out, "xs_release(");
            emit_expr(s->expr);
            buf_puts(out, ");");
        } else {
            emit_expr(s->expr);
            buf_puts(out, ";");
        }
        drain_if_needed();
        return;
    case S_IF: {
        if (emit_map_add(s)) { drain_if_needed(); return; }
        /* temporaries of the condition are drained after the whole if, even when the branches drain nothing */
        buf_puts(out, "if (");
        emit_expr(s->cond);
        buf_puts(out, ") ");
        bool cond_ar = used_ar;
        emit_block(s->body, false);
        if (s->else_body) {
            buf_puts(out, " else ");
            if (s->else_body->kind == S_BLOCK) emit_block(s->else_body, false);
            else emit_stmt(s->else_body);
        }
        used_ar |= cond_ar;
        drain_if_needed();
        return;
    }
    case S_WHILE: {
        {
            const char *wi_i = NULL, *wi_a[8];
            Expr *wi_e = NULL;
            int wi_n = 0;
            char *g = blk_cur ? wib_match(blk_cur, blk_cur_i, s, &wi_i, &wi_e, wi_a, &wi_n) : NULL;
            if (g) {
                buf_printf(out, "if (%s) {", g);
                indent++;
                int saved = nbce;
                for (int k = 0; k < wi_n && nbce < 16; k++) bce_loops[nbce++] = (BceLoop){wi_i, wi_a[k], true, NULL};
                bool rs = wi_e->rng_set; long long rl = wi_e->rng_lo, rh = wi_e->rng_hi;
                if (wi_e->kind == E_BINARY) { wi_e->rng_set = true; wi_e->rng_lo = -RNG_SAFE; wi_e->rng_hi = RNG_LEN; } /* (the guard bounds it) */
                wib_busy = true;
                nl(); emit_stmt(s);
                wib_busy = false;
                wi_e->rng_set = rs; wi_e->rng_lo = rl; wi_e->rng_hi = rh;
                nbce = saved;
                indent--;
                nl(); buf_puts(out, "} else {");
                indent++;
                wib_busy = true;
                nl(); emit_stmt(s);
                wib_busy = false;
                indent--;
                nl(); buf_puts(out, "}");
                return;
            }
        }
        if (emit_u8_while_rev(s)) { drain_if_needed(); return; }
        Expr *dc[4];
        int saved_ndctr = ndctr;
        int ndc = blk_cur ? dctr_enter(blk_cur, blk_cur_i, s, dc, 4) : 0;
        /* a condition that makes temporaries is drained on every iteration, not after the loop */
        bool cond_ar;
        char *cond = emit_expr_to_str(s->cond, &cond_ar);
        if (!cond_ar) {
            buf_printf(out, "while (%s) ", cond);
            emit_block_kind(s->body, SC_LOOP_BODY);
        } else {
            buf_printf(out, "while (1) { bool cond_ = (%s); xs_ar_drain(ar_mark); if (!cond_) break; ", cond);
            emit_block_kind(s->body, SC_LOOP_BODY);
            buf_puts(out, " }");
        }
        ndctr = saved_ndctr;
        for (int k = 0; k < ndc; k++) {
            int site = new_site(dc[k]->pos);
            buf_printf(out, " xs_ichk_hi_s(%s, XS_S(%d));", ren(dc[k]->lhs->cname), site);
        }
        used_ar = false;
        return;
    }
    case S_THROW:
        /* release the locals of every scope the throw abandons, up to the enclosing try block (or the function) */
        buf_puts(out, "{");
        indent++;
        nl();
        buf_puts(out, "xs_Error *thrown_ = ");
        { int rg_ = cur_rg; cur_rg = 0; emit_owned(s->expr); cur_rg = rg_; } /* (evaluated once, before the iterations) */
        buf_puts(out, ";");
        for (EScope *sc = escope; sc; sc = sc->parent) {
            emit_releases(sc);
            if (sc->kind == SC_TRY || sc->func_root) break;
        }
        drain_if_needed();
        nl();
        buf_puts(out, "xs_throw(thrown_);");
        indent--;
        nl();
        buf_puts(out, "}");
        return;
    case S_TRY: {
        /* try { } [catch (e) { }] [finally { }]: a throw in the try (or in the catch, when there is a finally)
           lands in an else branch; the finally runs on every path and rethrows what is pending */
        bool fin = s->for_init != NULL, cat = s->else_body != NULL;
        buf_puts(out, "{");
        indent++;
        nl();
        buf_puts(out, "jmp_buf tb_; size_t tam_ = xs_ar_mark(); xs_Error *pend_ = NULL; (void)pend_; xs_try_push(&tb_);");
        nl();
        buf_puts(out, "if (_setjmp(tb_) == 0) {");
        indent++;
        nl();
        pending_fin = fin ? s->for_init : NULL;
        emit_block_kind(s->body, SC_TRY);
        nl();
        buf_puts(out, "xs_try_pop();");
        indent--;
        nl();
        buf_puts(out, "} else {");
        indent++;
        nl();
        buf_puts(out, "xs_ar_drain(tam_);");
        if (cat) {
            if (fin) {
                nl();
                buf_puts(out, "jmp_buf cb_; xs_try_push(&cb_);");
                nl();
                buf_puts(out, "if (_setjmp(cb_) == 0) {");
                indent++;
            }
            epush(false);
            if (fin) { escope->kind = SC_TRY; escope->fin = s->for_init; }
            nl();
            buf_printf(out, "xs_Error *%s = xs_caught();", s->cname);
            etrack(s->cname, s->type);
            dbg_local(s->name, s->type, s->cname, s->is_const);
            nl();
            emit_block(s->else_body, false);
            emit_releases(escope);
            epop();
            if (fin) {
                nl();
                buf_puts(out, "xs_try_pop();");
                indent--;
                nl();
                buf_puts(out, "} else {");
                nl();
                buf_puts(out, "    xs_ar_drain(tam_);");
                nl();
                buf_puts(out, "    pend_ = xs_caught();");
                nl();
                buf_puts(out, "}");
            }
        } else {
            nl();
            buf_puts(out, "pend_ = xs_caught();");
        }
        indent--;
        nl();
        buf_puts(out, "}");
        if (fin) {
            nl();
            emit_block(s->for_init, false);
            nl();
            buf_puts(out, "if (pend_) xs_throw(pend_);");
        }
        indent--;
        nl();
        buf_puts(out, "}");
        return;
    }
    case S_FOROF: {
        Type *at = s->expr->type;
        if (at->kind == TY_OBJECT && at->cls->builtin && (strcmp(at->cls->cname, "xs_Uint8Array") == 0 || is_f64_type(at))) { /* for (const b of bytes) */
            buf_puts(out, "{");
            indent++;
            epush(false);
            nl();
            buf_printf(out, "%s *it_ = xs_retain(", at->cls->cname);
            { int rg_ = cur_rg; cur_rg = 0; emit_borrowed(s->expr); cur_rg = rg_; } /* (evaluated once, before the iterations) */
            buf_puts(out, ");");
            etrack("it_", at);
            nl();
            buf_puts(out, "for (int64_t i_ = 0; i_ < it_->f_length; i_++) {");
            indent++;
            epush(false);
            escope->kind = SC_LOOP_BODY;
            nl();
            emit_local_decl(is_f64_type(at) ? ty_number : ty_int, s->cname);
            buf_puts(out, " = it_->data[i_];");
            dbg_local(s->name, s->type, s->cname, s->is_const);
            nl();
            emit_block(s->body, false);
            emit_releases(escope);
            epop();
            indent--;
            nl();
            buf_puts(out, "}");
            emit_releases(escope);
            epop();
            indent--;
            nl();
            buf_puts(out, "}");
            return;
        }
        if (at->kind == TY_STREAM) {
            Prep *dp = NULL;
            if (ictx && ictx->prep && s->expr->kind == E_IDENT && s->expr->cname)
                for (int i = 0; i < ictx->prep->nd; i++) if (strcmp(ictx->prep->dparam[i], s->expr->cname) == 0) dp = ictx->prep->dprep[i];
            if (dp || inline_gen_target(s->expr, s)) {
                buf_puts(out, "{");
                indent++;
                epush(false);
                Prep *p = dp ? dp : gen_prepare(s->expr);
                emit_gen_inline(s, p);
                emit_releases(escope);
                epop();
                indent--;
                nl();
                buf_printf(out, "} yb%d_: __attribute__((unused));", p->id);
                return;
            }
        }
        if (at->kind == TY_STREAM) { /* pull until the stream ends; each element is owned by the loop body */
            buf_puts(out, "{");
            indent++;
            epush(false);
            nl();
            buf_puts(out, "xs_stream *st_ = xs_retain(");
            { int rg_ = cur_rg; cur_rg = 0; emit_borrowed(s->expr); cur_rg = rg_; } /* (evaluated once, before the iterations) */
            buf_puts(out, ");");
            etrack("st_", at);
            nl();
            buf_puts(out, "xs_val sv_;");
            nl();
            buf_puts(out, "while (xs_stream_pull(st_, &sv_)) {");
            indent++;
            epush(false);
            escope->kind = SC_LOOP_BODY;
            nl();
            emit_local_decl(s->type, s->cname);
            {
                int k = val_kind(s->type);
                buf_printf(out, k == 0 ? " = sv_.d;" : k == 1 ? " = sv_.i;" : k == 2 ? " = sv_.i != 0;" : " = sv_.p;");
            }
            etrack(s->cname, s->type);
            dbg_local(s->name, s->type, s->cname, s->is_const);
            nl();
            emit_block(s->body, false);
            emit_releases(escope);
            epop();
            indent--;
            nl();
            buf_puts(out, "}");
            emit_releases(escope);
            epop();
            indent--;
            nl();
            buf_puts(out, "}");
            return;
        }
        if (s->map_direct) { /* for (const [k, v] of map), the map unchanged by the body: its entries in place */
            Expr *mb = s->expr->lhs;
            Type *mt = mb->type;
            Class *tc = s->type->cls;
            buf_puts(out, "{");
            indent++;
            epush(false);
            nl();
            buf_puts(out, "xs_map *em_ = xs_retain(");
            emit_borrowed(mb);
            buf_puts(out, ");");
            etrack("em_", mt);
            nl();
            buf_puts(out, "xs_map_entry *ee_;");
            nl();
            buf_puts(out, "XS_MAP_EACH(em_, ee_) {");
            indent++;
            epush(false);
            escope->kind = SC_LOOP_BODY;
            if (region_reset_pending) {
                nl();
                buf_printf(out, region_dual[region_reset_pending] ? "xs_region_reset(&xs_rg_%d); xs_rg_on_%d = true;" : "xs_region_reset(&xs_rg_%d);", region_reset_pending, region_reset_pending);
                region_reset_pending = 0;
            }
            /* the tuple only lends the entry's key and value: the map is held for the loop and the body cannot
               change it, so no counts move; it is never released (it is not a counted object here) */
            nl();
            buf_printf(out, "%s tt_;", tc->cname);
            nl();
            emit_local_decl(s->type, s->cname);
            buf_puts(out, " = &tt_;");
            nl();
            buf_printf(out, "tt_.f__0 = %s; ", mt->key->kind == TY_STRING ? "ee_->skey" : "ee_->ikey");
            switch (val_kind(mt->elem)) {
            case 0: buf_puts(out, "tt_.f__1 = ee_->val.d;"); break;
            case 1: buf_puts(out, "tt_.f__1 = ee_->val.i;"); break;
            case 2: buf_puts(out, "tt_.f__1 = ee_->val.i != 0;"); break;
            default: buf_printf(out, "tt_.f__1 = (%s)ee_->val.p;", ctype(mt->elem)); break;
            }
            dbg_local(s->name, s->type, s->cname, s->is_const);
            nl();
            if (nmd_tuple < 16) md_tuple[nmd_tuple++] = s->cname;
            emit_block(s->body, false);
            nmd_tuple--;
            emit_releases(escope);
            epop();
            indent--;
            nl();
            buf_puts(out, "}");
            emit_releases(escope);
            epop();
            indent--;
            nl();
            buf_puts(out, "}");
            return;
        }
        buf_puts(out, "{");
        indent++;
        epush(false);
        nl();
        buf_printf(out, "%s *it_ = xs_retain(", arr_struct(at));
        { int rg_ = cur_rg; cur_rg = 0; emit_borrowed(s->expr); cur_rg = rg_; } /* (evaluated once, before the iterations) */
        buf_puts(out, ");");
        etrack("it_", at);
        nl();
        buf_puts(out, "for (size_t i_ = 0; i_ < it_->len; i_++) {");
        indent++;
        epush(false);
        escope->kind = SC_LOOP_BODY;
        if (region_reset_pending) { /* a region for...of: rewound before each element (region.c) */
            nl();
            buf_printf(out, region_dual[region_reset_pending] ? "xs_region_reset(&xs_rg_%d); xs_rg_on_%d = true;" : "xs_region_reset(&xs_rg_%d);", region_reset_pending, region_reset_pending);
            region_reset_pending = 0;
        }
        nl();
        emit_local_decl(s->type, s->cname);
        if (rcnt(s->type)) buf_printf(out, " = xs_retain((%s)it_->data[i_]);", ctype(s->type));
        else buf_puts(out, " = it_->data[i_];");
        etrack(s->cname, s->type);
        dbg_local(s->name, s->type, s->cname, s->is_const);
        nl();
        emit_block(s->body, false);
        emit_releases(escope);
        epop();
        indent--;
        nl();
        buf_puts(out, "}");
        emit_releases(escope);
        epop();
        indent--;
        nl();
        buf_puts(out, "}");
        return;
    }
    case S_BREAK: {
        EScope *t = jump_target(true);
        if (t && t->inl) { /* leaves an inlined generator: its scopes too */
            emit_releases_upto(t->inl->root, true);
            nl();
            buf_printf(out, "goto yb%d_;", t->inl->id);
            return;
        }
    }
        emit_releases_to_jump(true);
        nl();
        buf_puts(out, "break;");
        return;
    case S_CONTINUE: {
        EScope *t = jump_target(false);
        if (t && t->inl) { /* back into the generator, after its yield */
            emit_releases_upto(t, false);
            nl();
            buf_printf(out, "goto yc%d_;", t->cont_id);
            return;
        }
    }
        emit_releases_to_jump(false);
        nl();
        buf_puts(out, "continue;");
        return;
    case S_SWITCH: {
        /* subject evaluated once; the matching case index drives a C switch, so fallthrough and break keep their meaning */
        Type *t = s->expr->type;
        int def = -1;
        for (int i = 0; i < s->ncases; i++) if (!s->cases[i].value) def = i;
        buf_puts(out, "{");
        indent++;
        epush(false);
        escope->kind = SC_SWITCH;
        nl();
        bool by_class = union_lit_field(s->expr); /* switch (s.kind) on a discriminated union: by the object's class */
        if (by_class) {
            buf_puts(out, "void (*uf_)(void *) = xs_type_of((");
            emit_borrowed(s->expr->lhs);
            buf_printf(out, "))->free;");
            nl();
            buf_printf(out, "int which_ = %d;", def);
            for (int i = 0, k = 0; i < s->ncases; i++) {
                if (!s->cases[i].value || s->cases[i].value->kind != E_STRING) continue;
                nl();
                buf_puts(out, k++ ? "else if " : "if ");
                emit_union_class_test(s->expr->elem_type, s->expr->str, s->cases[i].value->str, "uf_");
                buf_printf(out, " which_ = %d;", i);
            }
        } else {
        emit_decl(t, "sw_");
        buf_puts(out, " = ");
        emit_borrowed(s->expr);
        buf_puts(out, ";");
        nl();
        buf_printf(out, "int which_ = %d;", def);
        }
        for (int i = 0, k = 0; !by_class && i < s->ncases; i++) {
            if (!s->cases[i].value) continue;
            nl();
            buf_puts(out, k++ ? "else if (" : "if (");
            if (t->kind == TY_STRING || (t->kind == TY_ENUM && t->en->is_string)) {
                buf_puts(out, "xs_str_eq(sw_, ");
                emit_borrowed(s->cases[i].value);
                buf_puts(out, ")");
            } else {
                buf_puts(out, "sw_ == ");
                emit_expr(s->cases[i].value);
            }
            buf_printf(out, ") which_ = %d;", i);
        }
        nl();
        buf_puts(out, "switch (which_) {");
        for (int i = 0; i < s->ncases; i++) {
            nl();
            buf_printf(out, "case %d: ", i);
            emit_block_kind(s->cases[i].body, SC_BLOCK);
        }
        nl();
        buf_puts(out, "default: break;");
        nl();
        buf_puts(out, "}");
        epop();
        indent--;
        nl();
        buf_puts(out, "}");
        drain_if_needed();
        return;
    }
    case S_FOR: {
        if (emit_u8_idiom(s)) { drain_if_needed(); return; }
        emit_u8_map_par(s);
        mark_counted_inc(s);
        {
            size_t mark = out->len;
            buf_puts(out, "{ ");
            int nb = 0, nm = loop_hoist_enter(s, &nb);
            if (nm || nb) {
                ctr_emit_for(s);
                for (int i = nmap_hoist - nm; i < nmap_hoist; i++) /* the deferred range tests (see loop_hoist_enter) */
                    if (map_hoist[i].site) buf_printf(out, " xs_map_check_range(%s, XS_S(%d));", ren(map_hoist[i].m), map_hoist[i].site);
                nmap_hoist -= nm;
                nbyte_hoist -= nb;
                buf_puts(out, " }");
                return;
            }
            out->len = mark; /* nothing to hold: as before */
            if (out->data) out->data[mark] = 0;
        }
        ctr_emit_for(s);
        return;
    }
    case S_YIELD: { /* hand the value over; when the consumer is gone, leave as a return does */
        if (ictx) { /* inlined: the consumer's loop body, with its variable bound to the value */
            ICtx *c = ictx;
            Stmt *L = c->loop;
            Buf side = {0}, *saved = out;
            out = &side;
            used_ar = false;
            if (cref(s->expr->type)) emit_owned(s->expr); else emit_expr(s->expr);
            bool ar = used_ar;
            out = saved;
            ictx = c->parent;
            buf_puts(out, "{");
            indent++;
            epush(false);
            escope->kind = SC_LOOP_BODY;
            escope->inl = c;
            int cid = escope->cont_id = ++inl_ids;
            nl();
            emit_local_decl(L->type, L->cname);
            buf_printf(out, " = (%s)(%s);", ctype(L->type), side.data ? side.data : "");
            free(side.data);
            etrack(L->cname, L->type);
            nl();
            emit_block(L->body, false);
            nl();
            buf_printf(out, "yc%d_: __attribute__((unused));", cid);
            emit_releases(escope);
            epop();
            indent--;
            nl();
            buf_puts(out, "}");
            ictx = c;
            used_ar = ar;
            drain_if_needed();
            return;
        }
        if (sm_mode) { /* the value out, the place to resume in the frame, back to the consumer */
            int k = val_kind(s->expr->type);
            int st = ++sm_states;
            buf_printf(out, "out_->%s = ", k == 0 ? "d" : k == 3 ? "p" : "i");
            if (cref(s->expr->type)) emit_owned(s->expr); else emit_expr(s->expr);
            buf_printf(out, "; fr_->state_ = %d; return true; case %d:;", st, st);
            return;
        }
        int k = val_kind(s->expr->type);
        buf_printf(out, "{ xs_val yv_; yv_.%s = ", k == 0 ? "d" : k == 3 ? "p" : "i");
        if (cref(s->expr->type)) emit_owned(s->expr); else emit_expr(s->expr);
        buf_puts(out, "; if (xs_gen_yield(yv_)) {");
        indent++;
        emit_releases_to_root();
        nl();
        buf_puts(out, "return;");
        indent--;
        nl();
        buf_puts(out, "} }");
        drain_if_needed();
        return;
    }
    case S_RETURN:
        if (sm_mode) { buf_puts(out, "{ fr_->state_ = -1; return false; }"); return; } /* (no locals to release) */
        if (ictx) { /* the inlined generator ends: past its loop */
            emit_releases_upto(ictx->root, true);
            nl();
            buf_printf(out, "goto yb%d_;", ictx->id);
            return;
        }
        buf_puts(out, "{");
        indent++;
        if (s->expr) {
            nl();
            emit_decl(cur_func->ret, "ret_");
            buf_puts(out, " = ");
            emit_owned(s->expr);
            buf_puts(out, ";");
        }
        emit_releases_to_root();
        drain_if_needed();
        emit_prof_leave();
        nl();
        if (in_ctor) buf_puts(out, in_init ? "return;" : ctor_cln ? "xs_cln_n--; return self;" : "return self;");
        else if (s->expr) buf_puts(out, "return ret_;");
        else buf_puts(out, "return;");
        indent--;
        nl();
        buf_puts(out, "}");
        return;
    case S_BLOCK:
        emit_block(s, false);
        return;
    }
}

/* ---------- declarations ---------- */

static void emit_signature(Func *f) {
    if (f->run_once && strcmp(fn_storage(f), "XS_FA") != 0 && strcmp(fn_storage(f), "XS_FII") != 0) buf_puts(out, "XS_RUN_ONCE "); /* (always_inline wins) */
    if (f->is_ctor) {
        buf_printf(out, "%s %s *%s(", fn_storage(f), f->cls->cname, f->cname);
    } else {
        buf_printf(out, "%s ", fn_storage(f));
        emit_decl(f->ret, f->cname);
        buf_puts(out, "(");
    }
    bool first = true;
    if (f->cls && !f->is_ctor) {
        buf_printf(out, "%s *self", f->cls->cname);
        first = false;
    }
    for (int i = 0; i < f->nparams; i++) {
        if (!first) buf_puts(out, ", ");
        emit_decl(f->params[i].type, f->params[i].cname);
        first = false;
    }
    if (first) buf_puts(out, "void");
    buf_puts(out, ")");
}

/* ---- xisto:parallel: the pool version of an instance (parallel.c proved the call's fn independent) ----
   C_par runs fn for every index on the pool (xs_pool_run) into a buffer of slots, then runs the module's own loop,
   C itself, with a closure that hands back slot i, or throws the error of the failing index when the loop gets
   there: the array, the combining order and the error are the loop's. */
static void par_signature(Func *f) {
    buf_puts(out, "XS_NOINLINE ");
    emit_decl(f->ret, xs_sprintf("%s_par", f->cname));
    buf_puts(out, "(");
    for (int i = 0; i < f->nparams; i++) {
        if (i) buf_puts(out, ", ");
        emit_decl(f->params[i].type, f->params[i].cname);
    }
    buf_puts(out, ")");
}

/* ---- jam: eight rows of a dot product at a time (XB-083) ----
   A callback of parallelFill shaped like one row of a matrix-vector product,
       (i) => { let t = K; for (let j = 0; j < a.length; ++j) t += E; return t; }
   with E pure (numbers, the row i, the column j, a[j], captured numbers, Number(), + - * /, calls of functions
   whose body is one `return`), also gets NAME_x8(env, i, out): rows i..i+7 at once, each row's sum added in the
   same order as the callback adds it (the same doubles), but eight independent chains the processor overlaps and
   the vectorizer packs. Integer arithmetic in E runs in doubles: while every value stays within ±(2^53 − 1) that is
   the same integer, and a flag notes any value outside; then the eight rows are not trusted (false) and the pool
   runs them one by one through the callback, which checks each operation and reports the error where it is.
   XISTO_NO_JAM=1 turns it off. */
typedef struct { const char *c; char kind; const char *bound; bool nz, nn; } JV; /* (jam_expr) */
typedef struct { const char *name; JV v; } JamSub; /* an inlined function's parameter */
typedef struct {
    Arrow *a;
    const char *iname, *jname, *aname; /* row parameter, column variable, the array of the loop's bound */
    int lane;
    JamSub *subs;
    int nsubs;
    int depth;
    Buf *checks; /* lane 0 only: one test per integer operation, on a bound of its value over the eight rows and every column */
} Jam;
static const char *jam_dbl(double v) {
    char *s = xs_sprintf("%.17g", v);
    if (!strpbrk(s, ".eEn")) s = xs_sprintf("%s.0", s);
    return s;
}
static Capture *jam_capture(Arrow *a, const char *name) {
    for (int i = 0; i < a->ncaptures; i++) if (!a->captures[i].is_this && strcmp(a->captures[i].name, name) == 0) return &a->captures[i];
    return NULL;
}
static bool jam_is_int(Type *t) { return t && (t->kind == TY_INT || t->kind == TY_LITINT); }
static bool jam_is_num(Type *t) { return t && t->kind == TY_NUMBER; }
static bool jam_num_array(Type *t) { return t && ((t->kind == TY_ARRAY && jam_is_num(t->elem)) || is_f64_type(t)); }
/* E as C for one lane. kind 'i': an integer held in a double, with bound (a C expression no smaller than |E| on
   any of the eight rows and any column: |a ± b| ≤ A + B, |a·b| ≤ A·B), nz (never -0: the double is the integer
   as Number() gives it) and nn (never negative); kind 'n': a number. c NULL: E is not plain. */
static const char *jam_num_of(JV v) { return v.kind == 'i' && !v.nz ? xs_sprintf("(%s + 0.0)", v.c) : v.c; } /* (x + 0.0: -0 becomes 0) */
static JV jam_expr(Jam *J, Expr *e) {
    JV no = {0};
    if (!e || !e->type) return no;
    switch (e->kind) {
    case E_NUMBER:
        if (jam_is_int(e->type)) return (JV){jam_dbl((double)e->ival), 'i', jam_dbl(fabs((double)e->ival)), true, e->ival >= 0};
        if (jam_is_num(e->type)) return (JV){jam_dbl(e->num), 'n', NULL, false, false};
        return no;
    case E_IDENT: {
        if (!e->str) return no;
        for (int k = J->nsubs - 1; k >= 0; k--) if (strcmp(J->subs[k].name, e->str) == 0) return J->subs[k].v;
        if (J->depth > 0) return no; /* an inlined function sees its parameters only */
        if (strcmp(e->str, J->iname) == 0) return (JV){xs_sprintf("(double)(i_ + %d)", J->lane), 'i', "ib_", true, true}; /* (an index) */
        if (strcmp(e->str, J->jname) == 0) return (JV){"(double)j_", 'i', "jb_", true, true};
        Capture *c = jam_capture(J->a, e->str);
        if (!c || c->boxed) return no;
        if (jam_is_int(c->type)) return (JV){xs_sprintf("(double)env->c_%s", c->name), 'i', xs_sprintf("__builtin_fabs((double)env->c_%s)", c->name), true, false};
        if (jam_is_num(c->type)) return (JV){xs_sprintf("env->c_%s", c->name), 'n', NULL, false, false};
        return no;
    }
    case E_INDEX: /* a[j] on the array of the bound: in range */
        if (e->builtin.cfn && !is_f64_type(e->lhs->type)) return no;
        if (J->depth > 0 || !e->lhs || e->lhs->kind != E_IDENT || !e->rhs || e->rhs->kind != E_IDENT) return no;
        if (strcmp(e->lhs->str, J->aname) != 0 || strcmp(e->rhs->str, J->jname) != 0) return no;
        return (JV){"ad_[j_]", 'n', NULL, false, false};
    case E_UNARY: {
        if (e->op != TK_MINUS) return no;
        JV x = jam_expr(J, e->lhs);
        if (!x.c || (x.kind == 'i' && !jam_is_int(e->type))) return no;
        return (JV){xs_sprintf("(-%s)", x.c), x.kind, x.bound, false, false};
    }
    case E_BINARY: {
        if (e->builtin.cfn && strcmp(e->builtin.cfn, "xs_idiv") != 0 && strcmp(e->builtin.cfn, "xs_idiv_s") != 0) return no;
        if (e->op != TK_PLUS && e->op != TK_MINUS && e->op != TK_STAR && e->op != TK_SLASH) return no;
        JV x = jam_expr(J, e->lhs), y = jam_expr(J, e->rhs);
        if (!x.c || !y.c) return no;
        const char *op = e->op == TK_PLUS ? "+" : e->op == TK_MINUS ? "-" : e->op == TK_STAR ? "*" : "/";
        if (jam_is_int(e->type)) { /* an integer operation: the same value while within ±(2^53 − 1) */
            if (x.kind != 'i' || y.kind != 'i' || e->op == TK_SLASH) return no;
            const char *bd = xs_sprintf("(%s %s %s)", x.bound, e->op == TK_STAR ? "*" : "+", y.bound);
            /* 2^52: the bound itself is computed in doubles, a few roundings below the integer limit */
            if (J->checks) buf_printf(J->checks, "    if (!(%s <= 4503599627370496.0)) return false;\n", bd);
            /* -0 only from (-0) + (-0), (-0) - 0, or a zero product of opposite signs */
            bool nz = e->op == TK_PLUS ? (x.nz || y.nz) : e->op == TK_MINUS ? x.nz : (x.nz && y.nz && x.nn && y.nn);
            bool nn = e->op == TK_MINUS ? false : (x.nn && y.nn);
            return (JV){xs_sprintf("(%s %s %s)", x.c, op, y.c), 'i', bd, nz, nn};
        }
        if (!jam_is_num(e->type)) return no;
        return (JV){xs_sprintf("(%s %s %s)", jam_num_of(x), op, jam_num_of(y)), 'n', NULL, false, false};
    }
    case E_CALL: {
        if (e->lhs && e->lhs->kind == E_IDENT && e->lhs->str && strcmp(e->lhs->str, "Number") == 0 && e->args.len == 1 && e->builtin.cfn &&
            (strcmp(e->builtin.cfn, "xs_to_number") == 0 || strcmp(e->builtin.cfn, "xs_to_number_id") == 0)) {
            JV x = jam_expr(J, e->args.items[0]);
            if (!x.c) return no;
            return (JV){jam_num_of(x), 'n', NULL, false, false};
        }
        Func *f = e->func;
        if (!f || e->builtin.cfn || f->is_native || f->cls || f->is_async || !f->body || f->nparams != e->args.len || J->depth > 4) return no;
        if (f->body->kind != S_BLOCK || f->body->stmts.len != 1 || f->body->stmts.items[0]->kind != S_RETURN) return no;
        if (!jam_is_num(f->ret) && !jam_is_int(f->ret)) return no;
        JamSub *subs = calloc((size_t)(f->nparams ? f->nparams : 1), sizeof(JamSub));
        for (int k = 0; k < f->nparams; k++) {
            Param *p = &f->params[k];
            if (p->dflt || p->cell || (!jam_is_num(p->type) && !jam_is_int(p->type))) return no;
            JV x = jam_expr(J, e->args.items[k]);
            if (!x.c) return no;
            if (jam_is_num(p->type) && x.kind == 'i') x = (JV){jam_num_of(x), 'n', NULL, false, false};
            if (jam_is_int(p->type) && x.kind != 'i') return no;
            subs[k] = (JamSub){p->name, x};
        }
        Jam inner = *J;
        inner.subs = subs; /* the callee sees its own parameters only */
        inner.nsubs = f->nparams;
        inner.depth = J->depth + 1;
        JV x = jam_expr(&inner, f->body->stmts.items[0]->expr);
        if (!x.c) return no;
        if (jam_is_num(f->ret) && x.kind == 'i') x = (JV){jam_num_of(x), 'n', NULL, false, false};
        if (jam_is_int(f->ret) && x.kind != 'i') return no;
        return x;
    }
    default:
        return no;
    }
}
/* ---- lanes: eight iterations of a counted loop at once (XB-085) ----
   for (let x = A; x < B; x++) {
       let v1 = I1, v2 = I2, ...;                       (P: numbers, each from x and values the loop never writes)
       for (let i = I0; i < K && C; i++) { v = U; ... }  (the state: numbers only, C and U pure arithmetic of them)
       S                                                 (the rest: anything, reading the state's last values)
   }
   runs P for x .. x+7 together: the inner loop in vectors of eight lanes, a lane frozen (its values kept) from
   the iteration its own C is false, the loop left when every lane is; then S once per lane, in order, with that
   lane's x and state. Every lane does exactly the operations the loop does for its x (the same doubles); P
   writes nothing and cannot fail (its integer operations run in doubles under a bound checked before, as jam
   does: outside it, the plain loop runs), so computing it ahead changes nothing S can see. The last B − A mod 8
   iterations, and the loop when the bound check fails, run as written. XISTO_NO_LANES=1 turns it off. */
typedef struct { const char *cname, *name; bool state; } LaneVar;
static LaneVar lane_v[24];
static int nlane_v;
static Stmt *lane_body;
static const char *lane_x, *lane_xname, *lane_i;
static JamSub lane_subs[64];
static int nlane_subs;
static Stmt *lane_decl(Stmt *s, const char *cname) { /* the declaration of a local, in s */
    if (!s) return NULL;
    if (s->kind == S_VAR && s->cname && strcmp(s->cname, cname) == 0) return s;
    Stmt *d;
    if ((d = lane_decl(s->for_init, cname)) || (d = lane_decl(s->body, cname)) || (d = lane_decl(s->else_body, cname))) return d;
    for (int i = 0; i < s->stmts.len; i++) if ((d = lane_decl(s->stmts.items[i], cname))) return d;
    for (int i = 0; i < s->ncases; i++) if ((d = lane_decl(s->cases[i].body, cname))) return d;
    return NULL;
}
static bool lane_s_ok(Stmt *s, int loops, int brk) { /* S: no break of the loop, no try, no yield */
    if (!s) return true;
    if (s->kind == S_BREAK && brk == 0) return false;
    if (s->kind == S_TRY || s->kind == S_YIELD) return false;
    bool lp = s->kind == S_FOR || s->kind == S_WHILE || s->kind == S_FOROF;
    int l2 = loops + (lp ? 1 : 0), b2 = brk + (lp || s->kind == S_SWITCH ? 1 : 0);
    if (!lane_s_ok(s->for_init, l2, b2) || !lane_s_ok(s->body, l2, b2) || !lane_s_ok(s->else_body, l2, b2)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!lane_s_ok(s->stmts.items[i], l2, b2)) return false;
    for (int i = 0; i < s->ncases; i++) if (!lane_s_ok(s->cases[i].body, l2, b2)) return false;
    return true;
}
/* an identifier P reads that is not one of its own: a local or parameter the loop never writes (and no closure
   does: not a cell), or a const global */
static bool lane_inv(Expr *e) {
    if (!e->cname || !e->str || !e->type) return false;
    if (e->type->kind != TY_INT && e->type->kind != TY_NUMBER) return false;
    for (int k = 0; k < nlane_subs; k++) if (strcmp(lane_subs[k].name, e->str) == 0) return true; /* (known already) */
    bool found = false;
    for (int k = 0; k < cur_func->nparams; k++)
        if (cur_func->params[k].cname && strcmp(cur_func->params[k].cname, e->cname) == 0) { if (cur_func->params[k].cell) return false; found = true; }
    if (!found && cur_func->body) {
        Stmt *d = lane_decl(cur_func->body, e->cname);
        if (d) { if (d->cell) return false; found = true; }
    }
    if (found) {
        if (stmt_writes_name(lane_body, e->cname)) return false;
    } else {
        bool g = false;
        for (int i = 0; prog && i < prog->top.len; i++) {
            Stmt *t = prog->top.items[i];
            if (t->kind == S_VAR && t->cname && strcmp(t->cname, e->cname) == 0 && t->is_const && !t->cell) g = true;
        }
        if (!g) return false;
    }
    if (nlane_subs >= 64) return false;
    if (e->type->kind == TY_INT) lane_subs[nlane_subs++] = (JamSub){e->str, (JV){xs_sprintf("(double)%s", ren(e->cname)), 'i', xs_sprintf("__builtin_fabs((double)%s)", ren(e->cname)), true, false}};
    else lane_subs[nlane_subs++] = (JamSub){e->str, (JV){ren(e->cname), 'n', NULL, false, false}};
    return true;
}
/* every identifier in e is x, a lane variable, or an invariant (added to the substitutions) */
static bool lane_idents(Expr *e, bool allow_state) {
    if (!e) return true;
    if (e->kind == E_IDENT) {
        if (!e->str || !e->cname) return false;
        if (strcmp(e->cname, lane_x) == 0) return true;
        if (lane_i && strcmp(e->cname, lane_i) == 0) return false;
        for (int k = 0; k < nlane_v; k++) if (strcmp(lane_v[k].cname, e->cname) == 0) return allow_state || !lane_v[k].state;
        return lane_inv(e);
    }
    if (e->kind == E_ARROW || e->kind == E_ASSIGN || e->kind == E_POSTFIX) return false;
    if (e->kind == E_CALL && e->func) { /* jam inlines a one-return function: its parameters are its own */
        for (int i = 0; i < e->args.len; i++) if (!lane_idents(e->args.items[i], allow_state)) return false;
        return true;
    }
    if (!lane_idents(e->lhs, allow_state) || !lane_idents(e->rhs, allow_state)) return false;
    for (int i = 0; i < e->args.len; i++) if (!lane_idents(e->args.items[i], allow_state)) return false;
    return true;
}
static int lane_find(const char *cname) { for (int k = 0; k < nlane_v; k++) if (strcmp(lane_v[k].cname, cname) == 0) return k; return -1; }
/* jam_expr with the lane substitutions: in the init (one lane, l_) or in vectors (lz_*) */
static JV lane_jv(Expr *e, bool vec, Buf *checks) {
    JamSub subs[96];
    int n = 0;
    for (int k = 0; k < nlane_subs; k++) subs[n++] = lane_subs[k];
    subs[n++] = (JamSub){lane_xname, vec ? (JV){"lxd_", 'i', "xm_", true, false} : (JV){"(double)(lb_ + l_)", 'i', "xm_", true, false}};
    for (int k = 0; k < nlane_v; k++) subs[n++] = (JamSub){lane_v[k].name, (JV){xs_sprintf(vec ? "lz_%s" : "lv_%s[l_]", lane_v[k].cname), 'n', NULL, false, false}};
    Jam J = {0};
    J.iname = J.jname = J.aname = "";
    J.subs = subs;
    J.nsubs = n;
    J.depth = 1; /* identifiers through the substitutions only */
    J.checks = checks;
    return jam_expr(&J, e);
}
/* C, one term of the inner loop's condition, as a lane mask (vectors of all ones where true) */
static const char *lane_cond(Expr *c, Buf *checks) {
    if (!c || c->kind != E_BINARY) return NULL;
    const char *op = c->op == TK_LT ? "<" : c->op == TK_LE ? "<=" : c->op == TK_GT ? ">" : c->op == TK_GE ? ">=" : c->op == TK_EQ ? "==" : c->op == TK_NEQ ? "!=" : NULL;
    if (!op || !c->lhs || !c->rhs || !c->lhs->type || !c->rhs->type) return NULL;
    if ((c->lhs->type->kind != TY_NUMBER && !jam_is_int(c->lhs->type)) || (c->rhs->type->kind != TY_NUMBER && !jam_is_int(c->rhs->type))) return NULL;
    JV a = lane_jv(c->lhs, true, checks), b = lane_jv(c->rhs, true, checks);
    if (!a.c || !b.c) return NULL;
    /* a side without a lane value is one double: added to a zero vector, so the comparison is per lane */
    const char *as = jam_num_of(a), *bs = jam_num_of(b);
    bool av = strstr(as, "lz_") || strstr(as, "lxd_"), bv = strstr(bs, "lz_") || strstr(bs, "lxd_");
    /* a side already in lanes is compared as it is (x + 0.0 is not x for -0.0: the compiler would keep the add) */
    return xs_sprintf("((xs_i8)(%s%s%s %s %s%s%s))", av ? "(" : "((xs_f8){0} + ", as, ")", op, bv ? "(" : "((xs_f8){0} + ", bs, ")");
}
/* mask-only: the rest S reads the state only through the loop's own condition T (one term besides i < K). A lane
   whose T went false stopped there, so T is false at its last values; a lane that ran all K iterations was never
   stopped, so its values are the exact ones. S's T is then (the lane ran to the end) && T: no lane needs its values
   kept from the iteration it stopped (no blends), and the all-stopped exit can be tested every few iterations. */
static Expr *lane_T;
static Expr *lane_hits[32];
static int nlane_hits;
static bool lane_state_name(const char *cname) { int k = lane_find(cname); return k >= 0 && lane_v[k].state; }
static bool lane_only_T_e(Expr *e) {
    if (!e) return true;
    if (same_simple(e, lane_T)) { if (nlane_hits >= 32) return false; lane_hits[nlane_hits++] = e; return true; }
    if (e->kind == E_IDENT) return !(e->cname && lane_state_name(e->cname));
    if (e->kind == E_ARROW) return false;
    if (!lane_only_T_e(e->lhs) || !lane_only_T_e(e->rhs)) return false;
    for (int i = 0; i < e->args.len; i++) if (!lane_only_T_e(e->args.items[i])) return false;
    return true;
}
static bool lane_only_T_s(Stmt *s) {
    if (!s) return true;
    if (s->kind == S_VAR && s->cname && lane_state_name(s->cname)) return false;
    if (!lane_only_T_e(s->init) || !lane_only_T_e(s->expr) || !lane_only_T_e(s->cond) || !lane_only_T_e(s->for_update)) return false;
    if (!lane_only_T_s(s->for_init) || !lane_only_T_s(s->body) || !lane_only_T_s(s->else_body)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!lane_only_T_s(s->stmts.items[i])) return false;
    for (int i = 0; i < s->ncases; i++) if (!lane_only_T_e(s->cases[i].value) || !lane_only_T_s(s->cases[i].body)) return false;
    return true;
}
static bool lane_incr(Expr *u, const char *name) { /* x++, ++x, x += 1 */
    if (!u || !u->lhs || u->lhs->kind != E_IDENT || !u->lhs->cname || strcmp(u->lhs->cname, name) != 0) return false;
    if ((u->kind == E_POSTFIX || u->kind == E_UNARY) && u->op == TK_PLUSPLUS) return true;
    return u->kind == E_ASSIGN && u->op == TK_PLUS_ASSIGN && u->rhs && u->rhs->kind == E_NUMBER && u->rhs->is_int_lit && u->rhs->ival == 1;
}
static void lane_terms(Expr *c, Expr **t, int *n) { /* a && b && ...: its terms */
    if (c && c->kind == E_BINARY && c->op == TK_AND) { lane_terms(c->lhs, t, n); lane_terms(c->rhs, t, n); return; }
    if (*n < 8) t[(*n)++] = c; else *n = 99;
}
static bool lane_loop(Stmt *s) {
    if (xs_debug || sm_mode || ictx || !cur_func || !cur_func->body || getenv("XISTO_NO_LANES")) return false;
    if (s->kind != S_FOR || !s->for_init || !s->body || s->body->kind != S_BLOCK) return false;
    Stmt *fi = s->for_init;
    if (fi->kind != S_VAR || !fi->cname || !fi->name || fi->cell || !fi->init || !fi->type || fi->type->kind != TY_INT || !bce_pure(fi->init)) return false;
    Expr *c = s->cond;
    if (!c || c->kind != E_BINARY || c->op != TK_LT || !c->lhs || c->lhs->kind != E_IDENT || !c->lhs->cname || strcmp(c->lhs->cname, fi->cname) != 0) return false;
    Expr *B = c->rhs;
    if (!B || !B->type || B->type->kind != TY_INT || !bce_pure(B) || (B->kind != E_IDENT && !(B->kind == E_NUMBER && B->is_int_lit))) return false;
    if (B->kind == E_IDENT && stmt_writes_name(s->body, B->cname)) return false;
    if (!lane_incr(s->for_update, fi->cname)) return false;
    Stmt *b = s->body;
    int p = 0;
    while (p < b->stmts.len && b->stmts.items[p]->kind == S_VAR) p++;
    if (p == 0 || p > 16 || p >= b->stmts.len || b->stmts.items[p]->kind != S_FOR) return false;
    Stmt *L = b->stmts.items[p];
    if (stmt_writes_name(s->body, fi->cname)) return false;
    lane_body = s->body;
    lane_x = fi->cname;
    lane_xname = fi->name;
    lane_i = NULL;
    nlane_v = nlane_subs = 0;
    for (int k = 0; k < p; k++) {
        Stmt *d = b->stmts.items[k];
        if (!d->cname || !d->name || d->cell || !d->init || !d->type || d->type->kind != TY_NUMBER) return false;
        if (!lane_idents(d->init, true)) return false;
        lane_v[nlane_v++] = (LaneVar){d->cname, d->name, false};
    }
    /* the inner loop: for (let i = I0; i < K && C...; i++) { v = U; ... } */
    Stmt *li = L->for_init;
    if (!li || li->kind != S_VAR || !li->cname || li->cell || !li->init || li->init->kind != E_NUMBER || !li->init->is_int_lit) return false;
    if (!lane_incr(L->for_update, li->cname) || !L->body || L->body->kind != S_BLOCK || L->body->stmts.len == 0) return false;
    lane_i = li->cname;
    Expr *terms[8];
    int nt = 0;
    lane_terms(L->cond, terms, &nt);
    if (nt == 0 || nt > 8) return false;
    Expr *K = NULL;
    for (int k = 0; k < nt; k++) {
        Expr *t = terms[k];
        if (t && t->kind == E_BINARY && t->op == TK_LT && t->lhs && t->lhs->kind == E_IDENT && t->lhs->cname && strcmp(t->lhs->cname, lane_i) == 0) {
            if (K) return false;
            K = t->rhs;
            if (!K || !K->type || K->type->kind != TY_INT || !(K->kind == E_NUMBER ? K->is_int_lit : (K->kind == E_IDENT && lane_idents(K, false)))) return false;
            terms[k] = NULL;
        }
    }
    if (!K) return false;
    for (int k = 0; k < L->body->stmts.len; k++) {
        Stmt *u = L->body->stmts.items[k];
        Expr *a = u->kind == S_EXPR ? u->expr : NULL;
        if (!a || a->kind != E_ASSIGN || a->op != TK_ASSIGN || !a->lhs || a->lhs->kind != E_IDENT || !a->lhs->cname) return false;
        int v = lane_find(a->lhs->cname);
        if (v < 0) return false;
        lane_v[v].state = true;
    }
    for (int k = 0; k < L->body->stmts.len; k++) if (!lane_idents(L->body->stmts.items[k]->expr->rhs, true)) return false;
    for (int k = 0; k < nt; k++) if (terms[k] && !lane_idents(terms[k], true)) return false;
    for (int k = p + 1; k < b->stmts.len; k++) if (!lane_s_ok(b->stmts.items[k], 0, 0)) return false;
    /* the C of it, and the bound checks of its integer operations */
    Buf checks = {0};
    buf_puts(&checks, "");
    const char *init[16];
    for (int k = 0; k < p; k++) {
        JV v = lane_jv(b->stmts.items[k]->init, false, &checks);
        if (!v.c) return false;
        init[k] = jam_num_of(v);
    }
    const char *upd[16];
    int nu = L->body->stmts.len;
    if (nu > 16) return false;
    for (int k = 0; k < nu; k++) {
        JV v = lane_jv(L->body->stmts.items[k]->expr->rhs, true, &checks);
        if (!v.c) return false;
        upd[k] = jam_num_of(v);
    }
    const char *cnd[8];
    for (int k = 0; k < nt; k++) {
        cnd[k] = NULL;
        if (terms[k] && !(cnd[k] = lane_cond(terms[k], &checks))) return false;
    }
    JV kv = lane_jv(K, false, NULL);
    /* mask-only (above): one condition term besides i < K, and S reads the state only through it */
    bool monly = false;
    {
        int nc = 0, ci = -1;
        for (int k = 0; k < nt; k++) if (cnd[k]) { nc++; ci = k; }
        static int off = -1;
        if (off < 0) off = getenv("XISTO_NO_LANE_MASK") && *getenv("XISTO_NO_LANE_MASK") ? 1 : 0;
        if (nc == 1 && !off) {
            lane_T = terms[ci];
            nlane_hits = 0;
            monly = true;
            for (int k = p + 1; k < b->stmts.len && monly; k++) if (!lane_only_T_s(b->stmts.items[k])) monly = false;
            if (nlane_hits == 0) monly = false;
        }
    }
    /* emitted: the loop variable, the groups of eight, then the loop as written for the rest */
    const char *xc = ren(fi->cname);
    buf_puts(out, "{");
    indent++;
    nl();
    emit_stmt_(fi);
    nl();
    buf_puts(out, "typedef double xs_f8 __attribute__((vector_size(64)));");
    nl();
    buf_puts(out, "typedef int64_t xs_i8 __attribute__((vector_size(64)));");
    nl();
    buf_puts(out, "const double xm_ = __builtin_fmax(__builtin_fabs((double)("); emit_expr(B); buf_printf(out, ")) + 1.0, __builtin_fabs((double)%s)) + 8.0;", xc);
    nl();
    buf_puts(out, "bool lok_ = true;");
    if (checks.data && checks.len) {
        char *t = checks.data;
        Buf ck = {0};
        buf_puts(&ck, "");
        for (char *q = t; *q;) {
            char *r = strstr(q, "return false;");
            if (!r) { buf_puts(&ck, q); break; }
            buf_printf(&ck, "%.*slok_ = false;", (int)(r - q), q);
            q = r + strlen("return false;");
        }
        nl();
        buf_puts(out, "{ "); buf_puts(out, ck.data); buf_puts(out, " }");
    }
    nl();
    buf_printf(out, "if (lok_) for (; %s <= (", xc); emit_expr(B); buf_printf(out, ") - 8; %s += 8) {", xc);
    indent++;
    nl();
    buf_printf(out, "const int64_t lb_ = %s;", xc);
    for (int k = 0; k < nlane_v; k++) { nl(); buf_printf(out, "double lv_%s[8];", lane_v[k].cname); }
    nl();
    buf_puts(out, "for (int l_ = 0; l_ < 8; l_++) {");
    for (int k = 0; k < nlane_v; k++) buf_printf(out, " lv_%s[l_] = %s;", lane_v[k].cname, init[k]);
    buf_puts(out, " }");
    nl();
    buf_puts(out, "xs_f8 lxd_; for (int l_ = 0; l_ < 8; l_++) lxd_[l_] = (double)(lb_ + l_); (void)lxd_;");
    for (int k = 0; k < nlane_v; k++) { nl(); buf_printf(out, "xs_f8 lz_%s; memcpy(&lz_%s, lv_%s, 64);", lane_v[k].cname, lane_v[k].cname, lane_v[k].cname); }
    nl();
    buf_puts(out, "xs_i8 act_ = (xs_i8){0} - 1;");
    nl();
    static int lane_lbl = 0;
    int lbl = ++lane_lbl;
    if (monly) {
        /* four iterations between the all-stopped tests: a stopped lane changes nothing S can see */
        buf_printf(out, "int64_t i_ = INT64_C(%lld);", (long long)li->init->ival);
        nl();
        buf_printf(out, "for (; (double)(i_ + 4) <= %s; i_ += 4) {", jam_num_of(kv));
        indent++;
        for (int r = 0; r < 4; r++) {
            for (int k = 0; k < nt; k++) if (cnd[k]) { nl(); buf_printf(out, "act_ &= %s;", cnd[k]); }
            for (int k = 0; k < nu; k++) { nl(); buf_printf(out, "lz_%s = %s;", L->body->stmts.items[k]->expr->lhs->cname, upd[k]); }
        }
        nl();
        buf_printf(out, "{ xs_i8 r_ = act_ | __builtin_shufflevector(act_, act_, 4, 5, 6, 7, 0, 1, 2, 3); if ((r_[0] | r_[1] | r_[2] | r_[3]) == 0) goto lend%d_; }", lbl);
        indent--;
        nl();
        buf_puts(out, "}");
        nl();
        buf_printf(out, "for (; (double)i_ < %s; i_++) {", jam_num_of(kv));
        indent++;
        for (int k = 0; k < nt; k++) if (cnd[k]) { nl(); buf_printf(out, "act_ &= %s;", cnd[k]); }
        for (int k = 0; k < nu; k++) { nl(); buf_printf(out, "lz_%s = %s;", L->body->stmts.items[k]->expr->lhs->cname, upd[k]); }
        indent--;
        nl();
        buf_puts(out, "}");
        nl();
        buf_printf(out, "lend%d_:;", lbl);
    } else {
    buf_printf(out, "for (int64_t i_ = INT64_C(%lld); (double)i_ < %s; i_++) {", (long long)li->init->ival, jam_num_of(kv));
    indent++;
    for (int k = 0; k < nt; k++) if (cnd[k]) { nl(); buf_printf(out, "act_ &= %s;", cnd[k]); }
    nl();
    buf_puts(out, "{ xs_i8 r_ = act_ | __builtin_shufflevector(act_, act_, 4, 5, 6, 7, 0, 1, 2, 3); if ((r_[0] | r_[1] | r_[2] | r_[3]) == 0) break; }");
    for (int k = 0; k < nlane_v; k++) if (lane_v[k].state) { nl(); buf_printf(out, "const xs_f8 lo_%s = lz_%s;", lane_v[k].cname, lane_v[k].cname); }
    for (int k = 0; k < nu; k++) {
        nl();
        buf_printf(out, "lz_%s = %s;", L->body->stmts.items[k]->expr->lhs->cname, upd[k]);
    }
    for (int k = 0; k < nlane_v; k++)
        if (lane_v[k].state) { nl(); buf_printf(out, "lz_%s = (xs_f8)(((xs_i8)lz_%s & act_) | ((xs_i8)lo_%s & ~act_));", lane_v[k].cname, lane_v[k].cname, lane_v[k].cname); }
    indent--;
    nl();
    buf_puts(out, "}");
    }
    for (int k = 0; k < nlane_v; k++) if (lane_v[k].state) { nl(); buf_printf(out, "memcpy(lv_%s, &lz_%s, 64);", lane_v[k].cname, lane_v[k].cname); }
    nl();
    buf_puts(out, "for (int l_ = 0; l_ < 8; l_++) {");
    indent++;
    nl();
    buf_printf(out, "int64_t %s = lb_ + l_;", xc);
    for (int k = 0; k < nlane_v; k++) { nl(); buf_printf(out, "double %s = lv_%s[l_];", ren(lane_v[k].cname), lane_v[k].cname); }
    /* mask-only: each T of S becomes (lact_ && T) for this lane */
    Expr saved_hits[32];
    if (monly) {
        nl();
        buf_puts(out, "const bool lact_ = act_[l_] != 0; (void)lact_;");
        for (int h = 0; h < nlane_hits; h++) {
            Expr *m = lane_hits[h];
            saved_hits[h] = *m;
            Expr *orig = malloc(sizeof(Expr)), *id = calloc(1, sizeof(Expr));
            *orig = *m;
            id->kind = E_IDENT;
            id->cname = id->str = "lact_";
            id->type = m->type;
            id->pos = m->pos;
            m->kind = E_BINARY;
            m->op = TK_AND;
            m->lhs = id;
            m->rhs = orig;
            m->args = (ExprList){0};
        }
    }
    Stmt rest = *b;
    rest.stmts.items = b->stmts.items + p + 1;
    rest.stmts.len = b->stmts.len - p - 1;
    rest.stmts.cap = rest.stmts.len;
    nl();
    emit_stmt_(&rest);
    if (monly) for (int h = 0; h < nlane_hits; h++) *lane_hits[h] = saved_hits[h];
    indent--;
    nl();
    buf_puts(out, "}");
    indent--;
    nl();
    buf_puts(out, "}");
    /* the rest, as written */
    Stmt tail = *s;
    tail.for_init = NULL;
    nl();
    emit_stmt_(&tail);
    indent--;
    nl();
    buf_puts(out, "}");
    return true;
}
static bool jam_is_ident(Expr *e, const char *name) { return e && e->kind == E_IDENT && e->str && strcmp(e->str, name) == 0; }
static Stmt *jam_single(Stmt *s) {
    while (s && s->kind == S_BLOCK && s->stmts.len == 1) s = s->stmts.items[0];
    return s;
}
static void jam_try(Arrow *a) {
    static int off = -1;
    if (off < 0) off = getenv("XISTO_NO_JAM") && *getenv("XISTO_NO_JAM") ? 1 : 0;
    Func *f = a->fn;
    if (off || xs_debug || f->nparams != 1 || !jam_is_int(f->params[0].type) || !jam_is_num(f->ret) || f->params[0].cell) return;
    Stmt *b = f->body;
    if (!b || b->kind != S_BLOCK || b->stmts.len != 3) return;
    Stmt *tv = b->stmts.items[0], *loop = b->stmts.items[1], *ret = b->stmts.items[2];
    if (tv->kind != S_VAR || tv->cell || !jam_is_num(tv->type) || !tv->init || tv->init->kind != E_NUMBER) return;
    loop = jam_single(loop);
    if (!loop || loop->kind != S_FOR || ret->kind != S_RETURN || !jam_is_ident(ret->expr, tv->name)) return;
    Stmt *jv = loop->for_init;
    if (!jv || jv->kind != S_VAR || jv->cell || !jam_is_int(jv->type) || !jv->init || jv->init->kind != E_NUMBER || jv->init->ival != 0) return;
    Expr *c = loop->cond;
    if (!c || c->kind != E_BINARY || c->op != TK_LT || !jam_is_ident(c->lhs, jv->name)) return;
    Expr *len = c->rhs;
    if (!len || len->kind != E_MEMBER || len->is_call || !len->str || strcmp(len->str, "length") != 0 || !len->lhs || len->lhs->kind != E_IDENT) return;
    Capture *arr = jam_capture(a, len->lhs->str);
    if (!arr || arr->boxed || !jam_num_array(arr->type)) return;
    Expr *u = loop->for_update;
    bool step1 = u && (((u->kind == E_UNARY || u->kind == E_POSTFIX) && u->op == TK_PLUSPLUS && jam_is_ident(u->lhs, jv->name)) ||
                       (u->kind == E_ASSIGN && u->op == TK_PLUS_ASSIGN && jam_is_ident(u->lhs, jv->name) && u->rhs && u->rhs->kind == E_NUMBER && jam_is_int(u->rhs->type) && u->rhs->ival == 1));
    if (!step1) return;
    Stmt *st = jam_single(loop->body);
    if (!st || st->kind != S_EXPR || !st->expr || st->expr->kind != E_ASSIGN || st->expr->op != TK_PLUS_ASSIGN || !jam_is_ident(st->expr->lhs, tv->name)) return;
    Buf checks = {0};
    Jam J = {a, f->params[0].name, jv->name, arr->name, 0, NULL, 0, 0, &checks};
    const char *terms[8];
    for (int k = 0; k < 8; k++) {
        J.lane = k;
        J.checks = k == 0 ? &checks : NULL;
        JV x = jam_expr(&J, st->expr->rhs);
        if (!x.c) { free(checks.data); return; }
        terms[k] = jam_num_of(x);
    }
    const char *cn = f->cname;
    Buf *o = &arrow_fns;
    buf_printf(o, "static __attribute__((unused)) bool %s_x8(void *env_, int64_t i_, double *out_) { /* rows i_..i_+7 (jam, XB-083) */\n", cn);
    buf_printf(o, "    %s_env *env = env_;\n", cn);
    buf_printf(o, "    int64_t n_ = (int64_t)env->c_%s->len;\n    const double *ad_ = env->c_%s->data;\n", arr->name, arr->name);
    /* every integer operation within range for rows i_..i_+7 and columns 0..n_-1, or the rows go one by one */
    buf_puts(o, "    double ib_ = (double)(i_ + 7), jb_ = (double)(n_ > 0 ? n_ - 1 : 0);\n    (void)ib_; (void)jb_;\n");
    if (checks.data) buf_puts(o, checks.data);
    free(checks.data);
    const char *k0 = jam_dbl(tv->init->num);
    if (jam_is_int(tv->init->type)) k0 = jam_dbl((double)tv->init->ival);
    buf_printf(o, "    double t0_ = %s, t1_ = %s, t2_ = %s, t3_ = %s, t4_ = %s, t5_ = %s, t6_ = %s, t7_ = %s;\n", k0, k0, k0, k0, k0, k0, k0, k0);
    buf_puts(o, "    for (int64_t j_ = 0; j_ < n_; j_++) {\n");
    for (int k = 0; k < 8; k++) buf_printf(o, "        t%d_ += %s;\n", k, terms[k]);
    buf_puts(o, "    }\n");
    for (int k = 0; k < 8; k++) buf_printf(o, "    out_[%d] = t%d_;\n", k, k);
    buf_puts(o, "    return true;\n}\n");
    buf_printf(&jam_tab, "    if (fn == (void *)%s) return (void *)%s_x8;\n", cn, cn);
}

static void emit_par_variant(Func *f) {
    const char *kind = (f->template_of ? f->template_of : f)->name;
    bool map = strcmp(kind, "parallelMap") == 0, fill = strcmp(kind, "parallelFill") == 0;
    Type *ft = f->params[1].type;
    const char *rt = ctype(ft->ret), *cn = f->cname;
    const char *items_t = map ? ctype(f->params[0].type) : NULL, *item_t = map ? ctype(f->params[0].type->elem) : NULL;
    bool heap = ty_is_heap(ft->ret);
    bool jam = fill && ft->ret && ft->ret->kind == TY_NUMBER && ft->nparams == 1 && ft->params[0] && (ft->params[0]->kind == TY_INT || ft->params[0]->kind == TY_LITINT);
    buf_printf(out, "\ntypedef struct { xs_closure *fn; %s *slots;%s%s%s } %s_pctx;\n", rt, map ? " " : "", map ? xs_sprintf("%sitems;", items_t) : "", jam ? " bool (*jam)(void *, int64_t, double *);" : "", cn);
    buf_printf(out, "static void %s_pbody(void *c_, int64_t i) {\n    %s_pctx *c = c_;\n    c->slots[i] = ", cn, cn);
    emit_closure_call_prefix(ft);
    buf_printf(out, "c->fn->fn)(c->fn, %si);\n}\n", map ? "c->items->data[i], " : "");
    if (jam) { /* eight rows at once when the callback has a jam version (XB-083) */
        if (!jam_used) buf_puts(out, "static void *xs_jam8_of(void *fn);\n");
        jam_used = true;
        buf_printf(out, "static bool %s_pbody8(void *c_, int64_t i) {\n    %s_pctx *c = c_;\n    return c->jam(c->fn, i, c->slots + i);\n}\n", cn, cn);
    }
    buf_printf(out, "static %s %s_preplay(xs_pool_replay *r, %si) {\n", rt, cn, map ? xs_sprintf("%s item, int64_t ", item_t) : "int64_t ");
    if (map) buf_puts(out, "    (void)item;\n");
    buf_puts(out, "    if (i == atomic_load(&r->job->err_i)) xs_pool_fail(r, i);\n");
    buf_printf(out, "    %s v = ((%s *)r->slots)[i];\n", rt, rt);
    if (heap) buf_printf(out, "    ((%s *)r->slots)[i] = 0;\n", rt);
    buf_puts(out, "    return v;\n}\n");
    if (heap) buf_printf(out, "static void %s_pdrop(void *s) { xs_release(*(void **)s); }\n", cn);
    par_signature(f);
    const char *n = map || fill ? xs_sprintf("(int64_t)%s->len", f->params[0].cname) : f->params[0].cname;
    buf_printf(out, " {\n    int64_t n_ = %s;\n    if (!xs_pool_worth(n_)) return %s(", n, cn);
    for (int i = 0; i < f->nparams; i++) buf_printf(out, "%s%s", i ? ", " : "", f->params[i].cname);
    buf_printf(out, ");\n    %s_pctx c_ = {%s, calloc((size_t)n_, sizeof(%s))%s%s%s};\n", cn, f->params[1].cname, rt, map ? ", " : "", map ? f->params[0].cname : "",
               jam ? xs_sprintf(", (bool (*)(void *, int64_t, double *))xs_jam8_of(%s->fn)", f->params[1].cname) : "");
    buf_puts(out, "    if (!c_.slots) xs_panic(\"out of memory\");\n    xs_pool_job j_;\n");
    if (jam) buf_printf(out, "    xs_pool_run8(&j_, n_, %s_pbody, c_.jam ? %s_pbody8 : NULL, &c_);\n", cn, cn);
    else buf_printf(out, "    xs_pool_run(&j_, n_, %s_pbody, &c_);\n", cn);
    buf_printf(out, "    xs_pool_replay r_ = {{{XS_IMMORTAL, 0}, (void *)%s_preplay}, &j_, c_.slots, sizeof(%s), %s};\n", cn, rt, heap ? xs_sprintf("%s_pdrop", cn) : "NULL");
    bool is_void = !f->ret || f->ret->kind == TY_VOID;
    buf_printf(out, "    %s%s(", is_void ? "" : xs_sprintf("%s%sres_ = ", ctype(f->ret), ctype(f->ret)[strlen(ctype(f->ret)) - 1] == '*' ? "" : " "), cn);
    for (int i = 0; i < f->nparams; i++) buf_printf(out, "%s%s", i ? ", " : "", i == 1 ? "(xs_closure *)&r_" : f->params[i].cname);
    buf_printf(out, ");\n    free(r_.slots);\n%s}\n", is_void ? "" : "    return res_;\n");
}

static void emit_prof_enter(Func *f) {
    if (!xs_profile) return;
    nl();
    buf_printf(out, "static xs_prof prof_ = {\"%s%s%s\"}; uint64_t prof_t0_, prof_cm_; xs_prof_enter(&prof_, &prof_t0_, &prof_cm_);",
               f->cls ? f->cls->name : "", f->cls ? "." : "", f->name);
}

static void emit_prof_leave(void) {
    if (!xs_profile) return;
    nl();
    buf_puts(out, "xs_prof_leave(&prof_, prof_t0_, prof_cm_);");
}

/* a generator: its arguments (retained), its body (run on the generator's stack) and the function itself, which
   only makes the stream */
static Buf gen_fns;
/* ---- generators as state machines (XB-022) ----
   A generator used as a value (stored, passed, iterated elsewhere) ran on a stack of its own, a context switch
   per yield. When its body is plain (if, while, for, break, continue, return, yields as statements; locals of
   primitive types; no try, switch, for...of, closures or await) it becomes one pull function instead: the locals,
   parameters and `this` live in a frame, and each yield stores the value, records where to resume and returns;
   the next pull jumps back in through a switch over that state (the cases sit inside the loops, as in Duff's
   device). XISTO_NO_GEN_SM=1 keeps the coroutine. */
static bool sm_expr_ok(Expr *e) {
    if (!e) return true;
    if (e->kind == E_ARROW || e->kind == E_AWAIT) return false;
    if (!sm_expr_ok(e->lhs) || !sm_expr_ok(e->rhs)) return false;
    for (int i = 0; i < e->args.len; i++) if (!sm_expr_ok(e->args.items[i])) return false;
    return true;
}
static Stmt **sm_vars;
static int sm_nvars;
static void sm_add_name(const char *nm) {
    if (sm_n % 32 == 0) sm_names = realloc(sm_names, sizeof(char *) * (size_t)(sm_n + 32));
    sm_names[sm_n++] = nm;
}
static bool sm_stmt_ok(Stmt *s) {
    if (!s) return true;
    switch (s->kind) {
    case S_VAR:
        if (s->cell || !s->type || cref(s->type) || !sm_expr_ok(s->init)) return false;
        if (sm_nvars % 32 == 0) sm_vars = realloc(sm_vars, sizeof(Stmt *) * (size_t)(sm_nvars + 32));
        sm_vars[sm_nvars++] = s;
        sm_add_name(s->cname);
        return true;
    case S_EXPR: case S_YIELD: return sm_expr_ok(s->expr);
    case S_RETURN: return !s->expr;
    case S_BREAK: case S_CONTINUE: return true;
    case S_IF: return sm_expr_ok(s->cond) && sm_stmt_ok(s->body) && sm_stmt_ok(s->else_body);
    case S_WHILE: return sm_expr_ok(s->cond) && sm_stmt_ok(s->body);
    case S_FOR: return sm_stmt_ok(s->for_init) && sm_expr_ok(s->cond) && sm_expr_ok(s->for_update) && sm_stmt_ok(s->body);
    case S_BLOCK:
        for (int i = 0; i < s->stmts.len; i++) if (!sm_stmt_ok(s->stmts.items[i])) return false;
        return true;
    default: return false;
    }
}
static bool emit_gen_sm(Func *f) {
    if (xs_debug || xs_profile || getenv("XISTO_NO_GEN_SM")) return false;
    sm_n = sm_nvars = 0;
    for (int i = 0; i < f->nparams; i++) if (f->params[i].cell) return false;
    if (!sm_stmt_ok(f->body)) return false;
    for (int i = 0; i < f->nparams; i++) sm_add_name(f->params[i].cname);
    if (f->cls) sm_add_name("self");
    const char *n = f->cname;
    Type *elem = f->ret->elem;
    Buf a = {0};
    Buf *saved = out;
    out = &a;
    /* the frame: where to resume, this, the parameters, the locals */
    buf_puts(out, "typedef struct { int state_; ");
    if (f->cls) buf_printf(out, "%s *self; ", f->cls->cname);
    for (int i = 0; i < f->nparams; i++) { emit_decl(f->params[i].type, f->params[i].cname); buf_puts(out, "; "); }
    for (int i = 0; i < sm_nvars; i++) { emit_decl(sm_vars[i]->type, sm_vars[i]->cname); buf_puts(out, "; "); }
    buf_printf(out, "} %s_fr;\n", n);
    buf_printf(out, "static void %s_fr_free(void *p) { %s_fr *fr_ = p; (void)fr_; %s", n, n, f->cls ? "xs_release(fr_->self); " : "");
    for (int i = 0; i < f->nparams; i++) if (crc(f->params[i].type)) buf_printf(out, "xs_release(fr_->%s); ", f->params[i].cname);
    buf_puts(out, "free(p); }\n");
    buf_printf(out, "static bool %s_pull(xs_stream *st_, xs_val *out_) {\n    %s_fr *fr_ = st_->ext;\n    size_t ar_mark = xs_ar_mark(); (void)ar_mark;\n    switch (fr_->state_) {\n    case 0:;", n, n);
    int saved_indent = indent;
    indent = 1;
    bool saved_vol = vol_locals;
    vol_locals = false;
    sm_mode = true;
    sm_states = 0;
    emit_block(f->body, true);
    sm_mode = false;
    vol_locals = saved_vol;
    indent = saved_indent;
    buf_puts(out, "\n    }\n    fr_->state_ = -1;\n    return false;\n}\n");
    out = saved;
    buf_puts(&gen_fns, a.data);
    free(a.data);
    buf_printf(out, " {\n    %s_fr *fr_ = calloc(1, sizeof *fr_);\n    if (!fr_) xs_panic(\"out of memory\");", n);
    if (f->cls) buf_puts(out, "\n    fr_->self = xs_retain(self);");
    for (int i = 0; i < f->nparams; i++)
        buf_printf(out, crc(f->params[i].type) ? "\n    fr_->%s = xs_retain(%s);" : "\n    fr_->%s = %s;", f->params[i].cname, f->params[i].cname);
    const char *pe = elem_printer(elem);
    buf_printf(out, "\n    xs_stream *st_ = xs_stream_new(%s_pull, NULL, NULL, %d);\n    st_->print_elem = %s;\n    st_->ext = fr_;\n    st_->ext_free = %s_fr_free;\n    return st_;\n}\n",
               n, val_kind(elem), strcmp(pe, "0") == 0 ? "NULL" : pe, n);
    return true;
}
static void emit_gen(Func *f) {
    if (emit_gen_sm(f)) return;
    const char *n = f->cname;
    Type *elem = f->ret->elem;
    Buf a = {0};
    buf_puts(&a, "typedef struct { ");
    if (f->cls) buf_printf(&a, "%s *self; ", f->cls->cname);
    Buf *saved = out;
    out = &a;
    for (int i = 0; i < f->nparams; i++) { emit_decl(f->params[i].type, f->params[i].cname); buf_puts(out, "; "); }
    buf_printf(&a, "char unused_; } %s_args;\n", n);
    buf_printf(&a, "static void %s_args_free(void *p) { %s_args *a_ = p; (void)a_; %s", n, n, f->cls ? "xs_release(a_->self); " : "");
    for (int i = 0; i < f->nparams; i++) if (crc(f->params[i].type)) buf_printf(&a, "xs_release(a_->%s); ", f->params[i].cname);
    buf_puts(&a, "free(p); }\n");
    buf_printf(&a, "static void %s_body(void *p_) {\n    %s_args *a_ = p_; (void)a_;", n, n);
    if (f->cls) buf_printf(&a, "\n    %s *self = a_->self; (void)self;", f->cls->cname);
    for (int i = 0; i < f->nparams; i++) { buf_puts(&a, "\n    "); emit_decl(f->params[i].type, f->params[i].cname); buf_printf(&a, " = a_->%s;", f->params[i].cname); }
    int saved_indent = indent;
    indent = 1;
    vol_locals = stmt_has_try(f->body);
    nl();
    buf_puts(out, "size_t ar_mark = xs_ar_mark(); (void)ar_mark;");
    emit_dbg_enter(f);
    dbg_params(f, false, NULL);
    nl();
    emit_block(f->body, true);
    emit_dbg_finish();
    buf_puts(out, "\n}\n");
    indent = saved_indent;
    out = saved;
    buf_puts(&gen_fns, a.data);
    free(a.data);
    buf_printf(out, " {\n    %s_args *a_ = calloc(1, sizeof *a_);\n    if (!a_) xs_panic(\"out of memory\");", n);
    if (f->cls) buf_puts(out, "\n    a_->self = xs_retain(self);");
    for (int i = 0; i < f->nparams; i++)
        buf_printf(out, crc(f->params[i].type) ? "\n    a_->%s = xs_retain(%s);" : "\n    a_->%s = %s;", f->params[i].cname, f->params[i].cname);
    const char *pe = elem_printer(elem);
    buf_printf(out, "\n    return xs_gen_stream(%s_body, a_, %s_args_free, %d, %s);\n}\n", n, n, val_kind(elem), strcmp(pe, "0") == 0 ? "NULL" : pe);
}


/* ---------- recursion on the right spine (spine) ----------
   f(x) = `const a = x.p; ... if (<base>) return K; return c + f(A) + f(B);` with int results, K and c literals >= 0
   and two recursive calls: the second call is the last thing the function does besides the add, so the
   function can loop on B with an accumulator and recurse only on A. Every partial sum is a sum of node
   contributions >= 0, hence <= the total: the checked adds overflow exactly when the nested form would, and the
   nodes are visited in the same order (A's subtree whole, then B). Not for number (reassociation would change
   the rounding). The LLVM does this itself for the Rust item_check; the checked add (jo) stops it here.
   binarytrees: 2 calls + 2 returns per node become 1 + 1 (M4 serial 2,47 -> 1,72 s user). XISTO_NO_SPINE=1 disables. */
static bool spine_int_lit(Expr *e) { /* a literal that is a whole number >= 0 (written 1 or 1.0) */
    return e && e->kind == E_NUMBER && e->num >= 0 && e->num <= 1e9 && e->num == (double)(int64_t)e->num;
}
static bool spine_leaves(Expr *e, Func *f, Expr **calls, int *ncalls, int64_t *c) {
    if (!e) return false;
    TypeKind want = f->ret->kind; /* int, or number (spine_fn: the whole-number proof) */
    if (e->kind == E_BINARY && e->op == TK_PLUS && e->type && e->type->kind == want && !e->builtin.cfn)
        return spine_leaves(e->lhs, f, calls, ncalls, c) && spine_leaves(e->rhs, f, calls, ncalls, c);
    if (spine_int_lit(e) && (e->is_int_lit || want == TY_NUMBER)) { *c += (int64_t)e->num; return true; }
    if (e->kind == E_CALL && e->func == f && !e->builtin.cfn && e->args.len == 1) {
        if (*ncalls >= 2) return false;
        calls[(*ncalls)++] = e;
        return true;
    }
    return false;
}
static Stmt *spine_single(Stmt *b) {
    if (!b) return NULL;
    if (b->kind == S_BLOCK) return b->stmts.len == 1 ? spine_single(b->stmts.items[0]) : NULL;
    return b;
}
static bool spine_pure(Expr *e) { /* the base test: reads of the parameter and its fields, comparisons, null */
    if (!e) return true;
    switch (e->kind) {
    case E_IDENT: case E_NULL: case E_NUMBER: case E_THIS: return true;
    case E_MEMBER: return !e->is_call && spine_pure(e->lhs);
    case E_BINARY: case E_UNARY: return spine_pure(e->lhs) && spine_pure(e->rhs);
    case E_AS: return e->op == TK_NOT && !e->elem_type && spine_pure(e->lhs);
    default: return false;
    }
}
typedef struct { int ndecls; Stmt *decls[8]; Stmt *base; Stmt *ret; Expr *calls[2]; int64_t c; int64_t k; bool num; } Spine;
static bool spine_suppress; /* emitting the plain body of a number spine function (its exact fallback) */
static bool spine_fn(Func *f, Spine *sp) {
    if (xs_debug || xs_profile || getenv("XISTO_NO_SPINE")) return false;
    if (spine_suppress) return false;
    if (!f->body || f->is_async || f->is_gen || f->cls || f->nparams != 1 || !f->ret) return false;
    if (f->ret->kind != TY_INT && (f->ret->kind != TY_NUMBER || getenv("XISTO_NO_SPINE_NUM"))) return false;
    sp->num = f->ret->kind == TY_NUMBER;
    Type *pt = f->params[0].type;
    if (!pt || pt->kind != TY_OBJECT || !pt->cls || pt->cls->builtin || f->params[0].cell) return false;
    Stmt *b = f->body;
    if (b->kind != S_BLOCK || b->stmts.len < 2 || b->stmts.len > 10) return false;
    int i = 0;
    sp->ndecls = 0;
    while (i < b->stmts.len && b->stmts.items[i]->kind == S_VAR) {
        Stmt *d = b->stmts.items[i];
        if (!d->is_const || !borrowable_init(d) || sp->ndecls >= 8) return false;
        sp->decls[sp->ndecls++] = d;
        i++;
    }
    if (i + 2 != b->stmts.len) return false;
    Stmt *s_if = b->stmts.items[i], *s_ret = b->stmts.items[i + 1];
    if (s_if->kind != S_IF || s_if->else_body || !spine_pure(s_if->cond)) return false;
    Stmt *base = spine_single(s_if->body);
    if (!base || base->kind != S_RETURN || !base->expr || !spine_int_lit(base->expr) || (!sp->num && !base->expr->is_int_lit)) return false;
    if (s_ret->kind != S_RETURN || !s_ret->expr) return false;
    int n = 0;
    sp->c = 0;
    if (!spine_leaves(s_ret->expr, f, sp->calls, &n, &sp->c) || n != 2) return false;
    if (!spine_pure(sp->calls[0]->args.items[0]) || !spine_pure(sp->calls[1]->args.items[0])) return false;
    sp->base = s_if; sp->ret = s_ret; sp->k = (int64_t)base->expr->num;
    return true;
}
/* the loop of the spine (see spine_fn): every term is >= 0, so the sum only rises and a partial sum that left the
   safe range leaves the total out of it too; the inner version (NAME_sp, the recursion's target) tests only the
   64-bit overflow, and NAME tests the range once on the final value. binary-trees, Xeon: the range test per node
   (a 2^53 mask kept in a callee-saved register, a push and pop per call) was 7% of the CPU. */
static void emit_spine_loop(Func *f, Spine *sp, bool wide) {
    const char *add = wide ? "xs_iadd_w_s" : "xs_iadd_nn_s";
    buf_puts(out, " { /* recursion on the right spine: a loop on the second call (see spine_fn) */");
    indent++;
    nl();
    buf_puts(out, "size_t ar_mark = xs_ar_mark();");
    nl();
    buf_puts(out, "(void)ar_mark;");
    nl();
    buf_puts(out, "int64_t acc_ = INT64_C(0);");
    nl();
    buf_puts(out, "for (;;) {");
    indent++;
    epush(true);
    for (int i = 0; i < sp->ndecls; i++) { nl(); emit_var(sp->decls[i]); }
    nl();
    buf_puts(out, "if (");
    emit_expr(sp->base->cond);
    if (sp->k) buf_printf(out, ") return %s(acc_, INT64_C(%lld), XS_S(%d));", add, (long long)sp->k, new_site(sp->ret->pos));
    else buf_puts(out, ") return acc_;");
    nl();
    buf_printf(out, "acc_ = %s(acc_, ", add);
    if (sp->c) buf_printf(out, "%s(INT64_C(%lld), ", add, (long long)sp->c);
    emit_expr(sp->calls[0]);
    if (sp->c) buf_printf(out, ", XS_S(%d))", new_site(sp->ret->pos));
    buf_printf(out, ", XS_S(%d));", new_site(sp->ret->pos));
    nl();
    buf_printf(out, "%s = ", f->params[0].cname);
    emit_borrowed(sp->calls[1]->args.items[0]);
    buf_puts(out, ";");
    epop();
    indent--;
    nl();
    buf_puts(out, "}");
    indent--;
    nl();
    buf_puts(out, "}\n");
}
static void emit_spine(Func *f, Spine *sp) {
    if (!spine_wide_on()) { emit_spine_loop(f, sp, false); return; }
    char *cname = f->cname, *inner = xs_sprintf("%s_sp", f->cname);
    int site = new_site(sp->ret->pos);
    buf_printf(out, " { int64_t r_ = %s(%s); if (__builtin_expect(XS_IOUT_NN(r_), 0)) XS_FAIL(XS_IOVF_MSG, XS_S(%d)); return r_; }\n", inner, f->params[0].cname, site);
    /* the inner version, into clone_fns (its prototype is with the others): the recursive call is to it */
    Buf *saved_out = out;
    int saved_indent = indent;
    Buf b = {0};
    out = &b;
    indent = 0;
    f->cname = inner;
    buf_putc(out, '\n');
    emit_signature(f);
    emit_spine_loop(f, sp, true);
    f->cname = cname;
    buf_puts(&clone_fns, b.data);
    free(b.data);
    out = saved_out;
    indent = saved_indent;
}
/* the number spine (spine_fn with a number result): `if (base) return K; return c + f(A) + f(B)` with K, c whole
   numbers >= 0. Proof that the loop gives Node's double bit for bit: every term is a whole number >= 0, so every
   partial sum of the nested form is a whole number between 0 and the total; when the total is below 2^53 each of
   those sums is exact in a double, and exact sums do not depend on the order. So NAME_sp sums in int64 and gives
   up (-1) as soon as a partial sum would pass 2^53 - 1; then NAME computes the nested form in double as written
   (NAME_dbl, the plain body: the function only reads fields and calls itself, so computing again is safe).
   binarytrees.ts (itemCheck: number), M4: the simple program gets the loop of the int one. XISTO_NO_SPINE_NUM=1. */
static void emit_spine_num(Func *f, Spine *sp) {
    char *cname = f->cname, *inner = xs_sprintf("%s_sp", cname), *dbl = xs_sprintf("%s_dbl", cname);
    const char *pn = f->params[0].cname;
    buf_printf(out, " { int64_t r_ = %s(%s); if (__builtin_expect(r_ >= 0, 1)) return (double)r_; return %s(%s); }\n", inner, pn, dbl, pn);
    Buf *saved_out = out;
    int saved_indent = indent;
    Buf b = {0};
    out = &b;
    indent = 0;
    buf_printf(out, "\nXS_FN int64_t %s(", inner);
    emit_decl(f->params[0].type, pn);
    buf_puts(out, ") { /* the number spine in int64 (see emit_spine_num): -1 once a partial sum leaves 2^53 - 1 */");
    indent++;
    nl(); buf_puts(out, "size_t ar_mark = xs_ar_mark();"); nl(); buf_puts(out, "(void)ar_mark;");
    nl(); buf_puts(out, "const int64_t safe_ = (INT64_C(1) << 53) - 1;");
    nl(); buf_puts(out, "int64_t acc_ = INT64_C(0);");
    nl(); buf_puts(out, "for (;;) {");
    indent++;
    epush(true);
    for (int i = 0; i < sp->ndecls; i++) { nl(); emit_var(sp->decls[i]); }
    nl(); buf_puts(out, "if (");
    emit_expr(sp->base->cond);
    buf_printf(out, ") return acc_ > safe_ - INT64_C(%lld) ? -1 : acc_ + INT64_C(%lld);", (long long)sp->k, (long long)sp->k);
    nl(); buf_puts(out, "int64_t t_ = ");
    f->cname = inner;
    emit_expr(sp->calls[0]);
    f->cname = cname;
    buf_puts(out, ";");
    nl(); buf_printf(out, "if (t_ < 0 || t_ > safe_ - INT64_C(%lld) - acc_) return -1;", (long long)sp->c);
    nl(); buf_printf(out, "acc_ += t_ + INT64_C(%lld);", (long long)sp->c);
    nl(); buf_printf(out, "%s = ", pn);
    emit_borrowed(sp->calls[1]->args.items[0]);
    buf_puts(out, ";");
    epop();
    indent--;
    nl(); buf_puts(out, "}");
    indent--;
    nl(); buf_puts(out, "}\n");
    /* the exact fallback: the body as written, under NAME_dbl */
    f->cname = dbl;
    buf_putc(out, '\n');
    emit_signature(f);
    spine_suppress = true;
    emit_func_body(f);
    spine_suppress = false;
    f->cname = cname;
    buf_puts(&clone_fns, b.data);
    free(b.data);
    out = saved_out;
    indent = saved_indent;
}
static void emit_func_body(Func *f) {
    cur_func = f;
    if (f->mod) cur_mod = f->mod;
    if (f->body) { rv_compute(f, &f->body, 1); nzv_compute(f, &f->body, 1); }
    if (f->is_gen) { emit_gen(f); cur_func = NULL; return; }
    vol_locals = stmt_has_try(f->body);
    { Spine sp_; if (spine_fn(f, &sp_)) { if (sp_.num) emit_spine_num(f, &sp_); else emit_spine(f, &sp_); cur_func = NULL; return; } }
    buf_puts(out, " {");
    indent++;
    nl();
    buf_puts(out, "size_t ar_mark = xs_ar_mark();");
    nl();
    buf_puts(out, "(void)ar_mark;");
    if (f->mod && f->mod->file && strcmp(f->mod->file, "xisto:bytes") == 0 && f->name) { /* a big range on several threads (xs_u8_rev_par) */
        if (strcmp(f->name, "reverseAround") == 0 && f->nparams == 5) {
            nl(); buf_printf(out, "if (xs_u8_rev_par(%s, %s, %s, %s, %s)) return;", f->params[0].cname, f->params[1].cname, f->params[2].cname, f->params[3].cname, f->params[4].cname);
        } else if (strcmp(f->name, "reverseMapped") == 0 && f->nparams == 4) {
            nl(); buf_printf(out, "if (xs_u8_rev_par(%s, %s, -1, %s, %s)) return;", f->params[0].cname, f->params[1].cname, f->params[2].cname, f->params[3].cname);
        }
    }
    if (f->mod && f->mod->file && strcmp(f->mod->file, "xisto:io") == 0 && f->name && strcmp(f->name, "writeStdoutLentBegin") == 0 && f->nparams == 1) {
        nl(); buf_printf(out, "xs_stdout_lent_begin(%s);", f->params[0].cname); /* (the body is empty: in Node it does nothing) */
    }
    if (f->mod && f->mod->file && strcmp(f->mod->file, "xisto:io") == 0 && f->name && strcmp(f->name, "freshPagesCheap") == 0 && f->nparams == 0) {
        nl(); buf_puts(out, "return xs_fresh_pages_cheap();"); /* (the body's false is Node's answer) */
    }
    Buf *saved_pbuf = fn_prelude_out; size_t saved_ppos = fn_prelude_pos; Buf saved_ptext = fn_prelude_text;
    fn_prelude_out = out; fn_prelude_pos = out->len; fn_prelude_text = (Buf){0};
    emit_prof_enter(f);
    emit_dbg_enter(f);
    dbg_params(f, false, NULL);
    nl();
    emit_block(f->body, true);
    emit_dbg_finish();
    emit_prof_leave();
    indent--;
    nl();
    buf_puts(out, "}\n");
    if (fn_prelude_text.len) { /* what the body asked to have at the entry (emit_u8_idiom: the table setup) */
        size_t n = fn_prelude_text.len, pos = fn_prelude_pos;
        buf_puts(out, fn_prelude_text.data); /* (room) */
        memmove(out->data + pos + n, out->data + pos, out->len - n - pos);
        memcpy(out->data + pos, fn_prelude_text.data, n);
        free(fn_prelude_text.data);
    }
    fn_prelude_out = saved_pbuf; fn_prelude_pos = saved_ppos; fn_prelude_text = saved_ptext;
    cur_func = NULL;
}

/* the class's own fields (not the inherited ones): null sentinels, then initializers, in order */
static void emit_own_field_inits(Class *c) {
    for (int i = c->nbase_fields; i < c->nfields; i++)
        if (!c->fields[i].init && null_prim(c->fields[i].type)) { nl(); buf_printf(out, "self->%s = %s;", c->fields[i].cname, null_value(c->fields[i].type)); }
    for (int i = 0; i < c->nfields; i++) {
        Field *fd = &c->fields[i];
        if (!fd->init) continue;
        if (i < c->nbase_fields && fd->owner != c) continue; /* (an interface's field the class gives a value: its own) */
        used_ar = false;
        nl();
        if (i < c->nbase_fields && cref(fd->type) && c->base && !c->base->iface_base) { /* (a field the base's constructor set: replaced) */
            buf_printf(out, "xs_assign(self->%s, ", fd->cname);
            emit_owned(fd->init);
            buf_puts(out, ");");
        } else {
            buf_printf(out, "self->%s = ", fd->cname);
            emit_owned(fd->init);
            buf_puts(out, ";");
        }
        drain_if_needed();
    }
}
/* Class_new_at(self, ...): the constructor's body on memory the caller provides (the heap, or a frame) */
static void emit_new_at_signature(Class *c) {
    Func *ctor = c->ctor;
    buf_printf(out, "%s %s *%s_new_at(%s *self", ctor ? fn_storage(ctor) : class_init_fails(c) ? "XS_FN" : "XS_FI", c->cname, c->cname, c->cname);
    for (int i = 0; ctor && i < ctor->nparams; i++) { buf_puts(out, ", "); emit_decl(ctor->params[i].type, ctor->params[i].cname); }
    buf_puts(out, ")");
}
/* a class in a hierarchy: Class_init(self, ...) runs the base's init (super), the own field initializers and the
   constructor body; Class_new(...) allocates and calls it (abstract classes have no _new) */
static void emit_hier_ctor(Class *c) {
    Func *ctor = c->ctor, *ec = emit_eff_ctor(c);
    buf_printf(out, "static void %s_init(%s *self", c->cname, c->cname);
    for (int i = 0; ec && i < ec->nparams; i++) { buf_puts(out, ", "); emit_decl(ec->params[i].type, ec->params[i].cname); }
    buf_puts(out, ") {");
    indent++;
    cur_func = ctor;
    nl();
    buf_puts(out, "size_t ar_mark = xs_ar_mark();");
    nl();
    buf_puts(out, "(void)ar_mark;");
    if (!ctor && c->base) { /* the implicit constructor: super(...args) */
        nl();
        buf_printf(out, "%s_init((%s *)self", c->base->cname, c->base->cname);
        for (int i = 0; ec && i < ec->nparams; i++) buf_printf(out, ", %s", ec->params[i].cname);
        buf_puts(out, ");");
    }
    if (!c->base || !ctor) { cur_func = NULL; emit_own_field_inits(c); cur_func = ctor; }
    if (ctor) {
        vol_locals = stmt_has_try(ctor->body);
        in_ctor = true;
        in_init = true;
        emit_prof_enter(ctor);
        emit_dbg_enter(ctor);
        dbg_params(ctor, false, NULL);
        nl();
        if (c->base) inits_after_super = c;
        emit_block(ctor->body, true);
        inits_after_super = NULL;
        emit_dbg_finish();
        emit_prof_leave();
        in_ctor = false;
        in_init = false;
    }
    cur_func = NULL;
    indent--;
    nl();
    buf_puts(out, "}\n");
    if (c->is_abstract) return;
    buf_printf(out, "XS_FN %s *%s_new(", c->cname, c->cname);
    for (int i = 0; ec && i < ec->nparams; i++) { if (i) buf_puts(out, ", "); emit_decl(ec->params[i].type, ec->params[i].cname); }
    if (!ec || !ec->nparams) buf_puts(out, "void");
    buf_printf(out, ") {\n    %s *self = xs_obj_alloc_t(sizeof(%s), &%s_type);%s\n    %s_init(self", c->cname, c->cname, c->cname,
               use_cln ? " xs_cln_push((void **)&self); /* (an exception in a constructor releases self and its fields) */" : "", c->cname);
    for (int i = 0; ec && i < ec->nparams; i++) buf_printf(out, ", %s", ec->params[i].cname);
    buf_puts(out, use_cln ? ");\n    xs_cln_n--;\n    return self;\n}\n" : ");\n    return self;\n}\n");
}

/* ---- which types can be part of a reference cycle ----
   Over the whole program: a class is cyclic when its reference fields can lead back to it (through other
   classes, subclasses, arrays, maps, nullable). A function value or `unknown` can hold anything a closure
   captures or anything at all, so they lead to every class. Only cyclic types get a visit function and the
   XS_CYC bit; everything else costs the cycle collector nothing. */
static Program *cyc_prog;
static bool *cls_cyc;     /* by class index */
static bool cyc_done;
static int cls_index(Class *c) { for (int i = 0; i < cyc_prog->nclasses; i++) if (cyc_prog->classes[i] == c) return i; return -1; }
static void cyc_succ(Type *t, bool *out, bool *any) { /* the classes a value of type t may point to */
    if (!t) return;
    switch (t->kind) {
    case TY_OBJECT:
        if (t->cls->builtin) return;
        for (int i = 0; i < cyc_prog->nclasses; i++) {
            Class *k = cyc_prog->classes[i];
            for (Class *b = k; b; b = b->base) if (b == t->cls) { out[i] = true; break; }
        }
        return;
    case TY_ARRAY: case TY_NULLABLE: case TY_SET: cyc_succ(t->elem, out, any); return;
    case TY_MAP: cyc_succ(t->key, out, any); cyc_succ(t->elem, out, any); return;
    case TY_UNION: for (int i = 0; i < t->nparams; i++) cyc_succ(t->params[i], out, any); return;
    case TY_FUNC: case TY_UNKNOWN: case TY_STREAM: case TY_CHAN: case TY_PROMISE: *any = true; return;
    default: return;
    }
}
/* Fields written after construction. A reference stored only by its class's constructor, from values that
   already existed and without `this` in the value, can only point to an older object: such edges never close a
   cycle. Anything else (a setter, obj.f = x elsewhere, `this` escaping in the constructor) can. */
typedef struct { Class *c; const char *name; } MutField;
static MutField *mut_fields;
static int nmut, capmut;
static bool expr_has_this(Expr *e) {
    if (!e) return false;
    if (e->kind == E_THIS) return true;
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn) {
        for (int i = 0; e->arrow->fn->body && i < e->arrow->fn->body->stmts.len; i++) {
            Stmt *st = e->arrow->fn->body->stmts.items[i];
            if (expr_has_this(st->expr) || expr_has_this(st->init)) return true;
        }
        for (int i = 0; i < e->arrow->ncaptures; i++) if (e->arrow->captures[i].is_this) return true;
    }
    if (expr_has_this(e->lhs) || expr_has_this(e->rhs)) return true;
    for (int i = 0; i < e->args.len; i++) if (expr_has_this(e->args.items[i])) return true;
    return false;
}
static void mut_mark(Class *c, const char *name) {
    for (int i = 0; i < nmut; i++) if (mut_fields[i].c == c && strcmp(mut_fields[i].name, name) == 0) return;
    if (nmut == capmut) { capmut = capmut ? capmut * 2 : 32; mut_fields = realloc(mut_fields, sizeof(MutField) * (size_t)capmut); }
    mut_fields[nmut++] = (MutField){c, name};
}
static bool mut_has(Class *c, const char *name) { /* written after construction, in c or a base */
    for (Class *k = c; k; k = k->base)
        for (int i = 0; i < nmut; i++) if (mut_fields[i].c == k && strcmp(mut_fields[i].name, name) == 0) return true;
    return false;
}
static void mut_stmt(Stmt *s, Func *fn);
static void mut_expr(Expr *e, Func *fn) {
    if (!e) return;
    if (e->kind == E_ASSIGN && e->lhs && e->lhs->kind == E_MEMBER && e->lhs->field >= 0 && e->lhs->cls) {
        Expr *t = e->lhs;
        bool in_ctor = fn && fn->is_ctor && t->lhs && t->lhs->kind == E_THIS && !expr_has_this(e->rhs);
        if (!in_ctor) {
            Class *owner = t->cls;
            for (Class *k = t->cls; k; k = k->base) /* the class that declares the field */
                for (int f = k->nbase_fields; f < k->nfields; f++) if (strcmp(k->fields[f].name, t->cls->fields[t->field].name) == 0) owner = k;
            mut_mark(owner, t->cls->fields[t->field].name);
        }
    }
    if (fn && fn->is_ctor && e->kind == E_THIS) { /* `this` passed around in a constructor: it may be stored anywhere */
        /* handled conservatively below: a constructor that lets `this` escape marks all its class's fields */
    }
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn) mut_stmt(e->arrow->fn->body, e->arrow->fn);
    mut_expr(e->lhs, fn);
    mut_expr(e->rhs, fn);
    for (int i = 0; i < e->args.len; i++) mut_expr(e->args.items[i], fn);
}
static void mut_stmt(Stmt *s, Func *fn) {
    if (!s) return;
    mut_expr(s->init, fn);
    mut_expr(s->expr, fn);
    mut_expr(s->cond, fn);
    mut_expr(s->for_update, fn);
    mut_stmt(s->for_init, fn);
    mut_stmt(s->body, fn);
    mut_stmt(s->else_body, fn);
    for (int i = 0; i < s->stmts.len; i++) mut_stmt(s->stmts.items[i], fn);
    for (int i = 0; i < s->ncases; i++) { mut_expr(s->cases[i].value, fn); mut_stmt(s->cases[i].body, fn); }
}
/* does a constructor hand `this` to anything (a call argument, another object's field, a closure)? */
static bool this_escapes_expr(Expr *e, bool top_assign_ok) {
    if (!e) return false;
    if (e->kind == E_ASSIGN) {
        bool own_field = e->lhs && e->lhs->kind == E_MEMBER && e->lhs->lhs && e->lhs->lhs->kind == E_THIS;
        if (expr_has_this(e->rhs)) return true;
        if (!own_field && expr_has_this(e->lhs)) return false;
        return this_escapes_expr(e->rhs, false);
    }
    if (e->kind == E_MEMBER && e->lhs && e->lhs->kind == E_THIS && !e->is_call) return false; /* reading a field */
    if (e->kind == E_MEMBER && e->is_call && e->lhs && e->lhs->kind == E_THIS) return true;   /* this.m(...): m may store it */
    if (e->kind == E_THIS) return true;
    if (e->kind == E_ARROW) return expr_has_this(e);
    if (this_escapes_expr(e->lhs, false) || this_escapes_expr(e->rhs, false)) return true;
    for (int i = 0; i < e->args.len; i++) if (this_escapes_expr(e->args.items[i], false)) return true;
    (void)top_assign_ok;
    return false;
}
static bool this_escapes(Stmt *s) {
    if (!s) return false;
    if (this_escapes_expr(s->init, false) || this_escapes_expr(s->expr, true) || this_escapes_expr(s->cond, false) || this_escapes_expr(s->for_update, false)) return true;
    if (this_escapes(s->for_init) || this_escapes(s->body) || this_escapes(s->else_body)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (this_escapes(s->stmts.items[i])) return true;
    for (int i = 0; i < s->ncases; i++) if (this_escapes(s->cases[i].body)) return true;
    return false;
}
/* the edges of a field's type: through a container (array, map, set) every edge is mutable (push, set) */
static void cyc_succ2(Type *t, bool *out, bool *mout, bool *any, bool mutable_) {
    if (!t) return;
    switch (t->kind) {
    case TY_OBJECT:
        if (t->cls->builtin) return;
        for (int i = 0; i < cyc_prog->nclasses; i++) {
            Class *k = cyc_prog->classes[i];
            for (Class *b = k; b; b = b->base) if (b == t->cls) { out[i] = true; if (mutable_) mout[i] = true; break; }
        }
        return;
    case TY_NULLABLE: cyc_succ2(t->elem, out, mout, any, mutable_); return;
    case TY_ARRAY: case TY_SET: cyc_succ2(t->elem, out, mout, any, true); return;
    case TY_MAP: cyc_succ2(t->key, out, mout, any, true); cyc_succ2(t->elem, out, mout, any, true); return;
    case TY_UNION: for (int i = 0; i < t->nparams; i++) cyc_succ2(t->params[i], out, mout, any, mutable_); return;
    case TY_FUNC: case TY_UNKNOWN: case TY_STREAM: case TY_CHAN: case TY_PROMISE: *any = true; return;
    default: return;
    }
}
static void cyc_compute(Program *p) {
    if (cyc_done) return;
    cyc_done = true;
    cyc_prog = p;
    int n = p->nclasses;
    cls_cyc = calloc((size_t)(n ? n : 1), sizeof(bool));
    /* which fields are written after construction */
    nmut = 0;
    for (int i = 0; i < p->nfuncs; i++) if (p->funcs[i]->body) mut_stmt(p->funcs[i]->body, p->funcs[i]);
    for (int i = 0; i < n; i++) {
        Class *c = p->classes[i];
        for (int f = 0; f < c->nfields; f++) mut_expr(c->fields[f].init, NULL);
        if (c->ctor && c->ctor->body) {
            mut_stmt(c->ctor->body, c->ctor);
            if (this_escapes(c->ctor->body)) for (int f = 0; f < c->nfields; f++) mut_mark(c, c->fields[f].name);
        }
        for (int k = 0; k < c->nmethods; k++) if (c->methods[k]->body && c->methods[k]->cls == c) mut_stmt(c->methods[k]->body, c->methods[k]);
    }
    for (int i = 0; i < p->top.len; i++) mut_stmt(p->top.items[i], NULL);
    if (getenv("XISTO_NO_CYCLES")) return;
    size_t N = (size_t)(n ? n : 1);
    bool *adj = calloc(N * N, sizeof(bool)), *madj = calloc(N * N, sizeof(bool));
    for (int i = 0; i < n; i++) {
        Class *c = p->classes[i];
        if (c->builtin) continue;
        bool any = false;
        for (int f = 0; f < c->nfields; f++) {
            if (!cref(c->fields[f].type)) continue;
            bool fany = false;
            bool mut = c->is_interface || mut_has(c, c->fields[f].name); /* interfaces: fields set anywhere */
            cyc_succ2(c->fields[f].type, adj + (size_t)i * N, madj + (size_t)i * N, &fany, mut);
            if (fany) { any = true; if (mut) for (int j = 0; j < n; j++) if (!p->classes[j]->builtin) madj[(size_t)i * N + (size_t)j] = true; }
            if (mut) for (int j = 0; j < n; j++) if (adj[(size_t)i * N + (size_t)j]) madj[(size_t)i * N + (size_t)j] = true;
        }
        if (any) for (int j = 0; j < n; j++) if (!p->classes[j]->builtin) adj[(size_t)i * N + (size_t)j] = true;
    }
    /* reach = transitive closure of adj */
    bool *reach = malloc(N * N);
    memcpy(reach, adj, N * N);
    for (int k = 0; k < n; k++)
        for (int i = 0; i < n; i++)
            if (reach[(size_t)i * N + (size_t)k])
                for (int j = 0; j < n; j++) if (reach[(size_t)k * N + (size_t)j]) reach[(size_t)i * N + (size_t)j] = true;
    /* cyclic: some cycle through it contains an edge that can be written after construction */
    for (int c = 0; c < n; c++) {
        if (p->classes[c]->builtin) continue;
        for (int u = 0; u < n && !cls_cyc[c]; u++) {
            if (u != c && !reach[(size_t)c * N + (size_t)u]) continue;
            for (int v = 0; v < n; v++) {
                if (!madj[(size_t)u * N + (size_t)v]) continue;
                if (v == c || reach[(size_t)v * N + (size_t)c]) { cls_cyc[c] = true; break; }
            }
        }
    }
    free(adj); free(madj); free(reach);
    for (int c = 0; c < n; c++) if (p->classes[c]->region) cls_cyc[c] = false; /* freed with their region, never one by one */
}
static bool class_cyclic(Class *c) { int i = cls_index(c); return i >= 0 && cls_cyc[i]; }
/* a field holding an object of region `id` directly (its count is the region's business) */
static bool region_ref(Type *t, int id) {
    if (t && t->kind == TY_NULLABLE) t = t->elem;
    return t && t->kind == TY_OBJECT && t->cls && t->cls->region == id;
}
/* may a value of type t (directly or inside a container) be part of a cycle? */
static bool type_cyclic(Type *t) {
    if (!t) return false;
    switch (t->kind) {
    case TY_OBJECT: {
        if (t->cls->builtin) return false;
        for (int i = 0; i < cyc_prog->nclasses; i++)
            if (cls_cyc[i]) for (Class *b = cyc_prog->classes[i]; b; b = b->base) if (b == t->cls) return true;
        return false;
    }
    case TY_ARRAY: case TY_NULLABLE: case TY_SET: return type_cyclic(t->elem);
    case TY_MAP: return type_cyclic(t->key) || type_cyclic(t->elem);
    case TY_UNION: for (int i = 0; i < t->nparams; i++) if (type_cyclic(t->params[i])) return true; return false;
    case TY_FUNC: case TY_UNKNOWN: for (int i = 0; i < cyc_prog->nclasses; i++) if (cls_cyc[i]) return true; return false;
    default: return false;
    }
}
static bool arr_or_map_cyclic(Type *t) {
    if (t->kind == TY_ARRAY) return arr_is_ref(t) && type_cyclic(t->elem);
    if (t->kind == TY_MAP) return val_kind(t->elem) == 3 && type_cyclic(t->elem);
    return false;
}
/* the static type descriptor of a class: free, print and, when cyclic, visit */
static const char *class_type_ref(Class *c) { return xs_sprintf("&%s_type", c->cname); }

/* the leading this.f = v of fields without initializer: plain stores (see E_ASSIGN), marked before the
   allocation line is chosen (ctor_sets_every_field reads the marks) */
static void mark_init_stores(Class *c, Func *ctor) {
    if (xs_debug || !ctor || !ctor->body) return;
    bool *seen = calloc((size_t)(c->nfields ? c->nfields : 1), sizeof(bool));
    for (int k = 0; k < ctor->body->stmts.len; k++) {
        Stmt *st = ctor->body->stmts.items[k];
        Expr *e = st->kind == S_EXPR ? st->expr : NULL;
        if (!e || e->kind != E_ASSIGN || e->op != TK_ASSIGN || !e->lhs || e->lhs->kind != E_MEMBER || !e->lhs->lhs ||
            e->lhs->lhs->kind != E_THIS || e->lhs->field < 0 || e->lhs->field >= c->nfields || expr_has_this(e->rhs)) break;
        int f = e->lhs->field;
        if (seen[f] || c->fields[f].init) break;
        seen[f] = true;
        if (!e->builtin.cfn) e->builtin.cfn = "init_store";
    }
    free(seen);
}
static void emit_class(Class *c) {
    cur_class = c;
    cur_mod = c->mod;
    if (class_cyclic(c)) {
        buf_printf(out, "\nstatic void %s_visit(void *p, xs_visit_cb cb, void *arg) {\n    %s *self = p; (void)self;", c->cname, c->cname);
        for (int i = 0; i < c->nfields; i++) if (cref(c->fields[i].type)) buf_printf(out, "\n    cb((void **)&self->%s, arg);", c->fields[i].cname);
        buf_puts(out, "\n}\n");
    }
    if (c->region) { /* a region class: nothing happens when its count reaches zero; the region drops it */
        buf_printf(out, "\nstatic void %s_free(void *p) { (void)p; }", c->cname);
        bool holds = false, rrefs = false;
        for (int i = 0; i < c->nfields; i++) {
            if (!cref(c->fields[i].type)) continue;
            if (region_ref(c->fields[i].type, c->region)) rrefs = true; else holds = true;
        }
        if (holds) {
            buf_printf(out, "\nstatic void %s_drop(void *p) {\n    %s *self = p;", c->cname, c->cname);
            for (int i = 0; i < c->nfields; i++)
                if (cref(c->fields[i].type) && !region_ref(c->fields[i].type, c->region)) buf_printf(out, "\n    xs_release(self->%s);", c->fields[i].cname);
            buf_puts(out, "\n}");
        }
        if (rrefs) {
            buf_printf(out, "\nstatic void %s_rref(void *p) {\n    %s *self = p;", c->cname, c->cname);
            for (int i = 0; i < c->nfields; i++)
                if (cref(c->fields[i].type) && region_ref(c->fields[i].type, c->region))
                    buf_printf(out, "\n    if (self->%s) ((xs_hdr *)self->%s)->rc--;", c->fields[i].cname, c->fields[i].cname);
            buf_puts(out, "\n}");
        }
        buf_putc(out, '\n');
    } else if (c->nohdr) { /* no header: nothing counts its objects, so nothing frees one (nohdr_compute) */
        buf_printf(out, "\nstatic inline void %s_free(void *p) { (void)p; }", c->cname);
        buf_printf(out, "\nstatic void %s_rref(void *p) { (void)p; }\n", c->cname);
    } else {
    /* a hint: where the class is known (xs_release_as, a scope's end) the free is a few releases, worth inlining */
    buf_printf(out, "\nstatic inline void %s_free(void *p) {", c->cname);
    indent++;
    if (c->rdual) { /* made in the region: its rewind frees it (and its fields are numbers or the region's objects) */
        nl();
        buf_puts(out, "if (((xs_hdr *)p)->cls & XS_RGN) return;");
    }
    nl();
    buf_printf(out, "%s *self = p;", c->cname);
    nl();
    buf_puts(out, "(void)self;");
    for (int i = 0; i < c->nfields; i++) {
        if (!cref(c->fields[i].type)) continue;
        nl();
        Type *ft = c->fields[i].type->kind == TY_NULLABLE ? c->fields[i].type->elem : c->fields[i].type;
        Class *fc = ft->kind == TY_OBJECT ? ft->cls : NULL;
        if (fc && !fc->builtin && !fc->nsubs && !fc->region && !fc->is_interface && fc->used) /* its free is known */
            buf_printf(out, "xs_release_as(self->%s, %s_free);", c->fields[i].cname, fc->cname);
        else if (ft->kind == TY_STRING) buf_printf(out, "xs_release_as(self->%s, xs_str_free);", c->fields[i].cname);
        else buf_printf(out, "xs_release(self->%s);", c->fields[i].cname);
    }
    nl();
    buf_puts(out, "xs_obj_free(p);");
    indent--;
    nl();
    buf_puts(out, "}\n");
    if (c->rdual) { /* XISTO_REGION_CHECK: its references to the region's objects, taken away before the counts are checked */
        buf_printf(out, "static void %s_rref(void *p) {\n    %s *self = p; (void)self;", c->cname, c->cname);
        for (int i = 0; i < c->nfields; i++)
            if (cref(c->fields[i].type)) buf_printf(out, "\n    if (self->%s && (((xs_hdr *)self->%s)->cls & XS_RGN)) ((xs_hdr *)self->%s)->rc--;", c->fields[i].cname, c->fields[i].cname, c->fields[i].cname);
        buf_puts(out, "\n}\n");
    }
    }

    if (c->adapter_of) { /* an adapter (XB-126) prints as the object it holds: TypeScript has only that object */
        buf_printf(out, "static void %s_print(void *p) { xs_print_obj(((%s *)p)->%s); }\n", c->cname, c->cname, c->fields[0].cname);
    } else {
    /* Node-style printing: Name { a: 1, s: 'x' } */
    buf_printf(out, "static void %s_print(void *p) {", c->cname);
    indent++;
    nl();
    buf_printf(out, "%s *self = p;", c->cname);
    nl();
    buf_puts(out, "(void)self;");
    nl();
    /* util.inspect's layout (runtime: xs_insp_*): each field an entry, the container decides lines and depth */
    { /* an object whose fields are all optional and absent prints as an empty one */
        int always = 0, opt = 0;
        for (int i = 0; i < c->nfields; i++) { if (c->fields[i].optional) opt++; else always++; }
        const char *empty_s = c->is_interface ? "{}" : xs_sprintf("%s {}", c->name);
        if (always || !opt) {
            buf_printf(out, "xs_insp fr_; if (!xs_insp_begin(&fr_, p, \"%s\", %s)) return;", c->is_interface ? "Object" : c->name,
                       always ? "NULL" : xs_sprintf("\"%s\"", empty_s));
        } else {
            Buf cond = {0};
            for (int i = 0; i < c->nfields; i++) buf_printf(&cond, "%s%s", i ? " || " : "", not_null(c->fields[i].type, xs_sprintf("self->%s", c->fields[i].cname)));
            buf_printf(out, "xs_insp fr_; if (!xs_insp_begin(&fr_, p, \"%s\", (%s) ? NULL : \"%s\")) return;", c->is_interface ? "Object" : c->name, cond.data, empty_s);
        }
    }
    for (int i = 0; i < c->nfields; i++) {
        Field *fd = &c->fields[i];
        nl();
        if (fd->optional) buf_printf(out, "if (%s) {", not_null(fd->type, xs_sprintf("self->%s", fd->cname)));
        nl();
        if (c->is_tuple) buf_puts(out, "xs_insp_entry(&fr_);"); /* [ 'a', 1 ]: elements, no names */
        else buf_printf(out, "xs_insp_entry(&fr_); fputs(\"%s: \", XS_OUT);", fd->name);
        nl();
        switch (fd->type->kind) {
        case TY_NUMBER: buf_printf(out, "xs_print_num(self->%s);", fd->cname); break;
        case TY_INT: buf_printf(out, "xs_print_int(self->%s);", fd->cname); break;
        case TY_BOOLEAN: buf_printf(out, "xs_print_bool(self->%s);", fd->cname); break;
        case TY_STRING: buf_printf(out, "xs_print_str_quoted(self->%s);", fd->cname); break;
        case TY_ARRAY: buf_printf(out, "xs_print_arr_%s(self->%s);", arr_print_suffix(fd->type), fd->cname); break;
        case TY_OBJECT: buf_printf(out, "xs_print_obj(self->%s);", fd->cname); break;
        case TY_ENUM: buf_printf(out, fd->type->en->is_string ? "xs_print_str_quoted(self->%s);" : "xs_print_int(self->%s);", fd->cname); break;
        case TY_NULLABLE:
            buf_printf(out, "if (!%s) fputs(\"null\", XS_OUT); else %s(self->%s);", not_null(fd->type, xs_sprintf("self->%s", fd->cname)),
                       fd->type->elem->kind == TY_STRING ? "xs_print_str_quoted" : printer_for(fd->type->elem), fd->cname);
            break;
        case TY_MAP: case TY_SET: buf_printf(out, "xs_print_map(self->%s);", fd->cname); break;
        case TY_UNION: buf_printf(out, "xs_print_any_quoted(self->%s);", fd->cname); break;
        case TY_UNKNOWN: if (ty_is_undef(fd->type)) { buf_puts(out, "fputs(\"undefined\", XS_OUT);"); break; } /* fallthrough */
        default: break;
        }
        if (fd->optional) buf_puts(out, " }");
    }
    nl();
    if (c->is_tuple) buf_puts(out, "xs_insp_end(&fr_, \"[\", \"]\", true, false, false);");
    else buf_printf(out, "xs_insp_end(&fr_, \"%s\", \"}\", false, false, false);", c->is_interface ? "{" : xs_sprintf("%s {", c->name));
    indent--;
    nl();
    buf_puts(out, "}\n");
    }

    if (in_hierarchy(c)) { emit_hier_ctor(c); goto methods; }
    /* constructor */
    Func *ctor = c->ctor;
    if (c->stack_at) { /* objects in frames too (stackalloc.c): the body takes the memory, C_new allocates it */
        if (ctor) emit_signature(ctor);
        else buf_printf(out, "%s %s *%s_new(void)", class_init_fails(c) ? "XS_FN" : "XS_FI", c->cname, c->cname);
        buf_printf(out, " {\n    return %s_new_at(xs_obj_alloc_t(sizeof(%s), &%s_type)", c->cname, c->cname, c->cname);
        /* (the wrapper is the allocation plus a call: its storage follows the constructor's, for stack traces) */
        for (int i = 0; ctor && i < ctor->nparams; i++) buf_printf(out, ", %s", ctor->params[i].cname);
        buf_puts(out, ");\n}\n");
        emit_new_at_signature(c);
    } else if (ctor) emit_signature(ctor);
    else buf_printf(out, "%s %s *%s_new(void)", class_init_fails(c) ? "XS_FN" : "XS_FI", c->cname, c->cname);
    buf_puts(out, " {");
    /* field initializers run inside the constructor's frame (and its single call site, when inlined there) */
    cur_func = ctor;
    indent++;
    nl();
    if (c->stack_at) buf_puts(out, "/* self: cleared, counted, typed */");
    else { /* nz: the constructor writes every field before anything reads one, so the memory is not cleared first */
        mark_init_stores(c, ctor);
        /* a program that can catch: an exception in the constructor must release self and the fields set so far,
           so self is cleared (no field left to chance) and registered on the cleanup stack */
        bool cln_self = use_cln && !c->nohdr && !c->region && !c->rdual;
        const char *nz = ctor_sets_every_field(c) && !cln_self ? "_nz" : "";
        if (c->nohdr && c->region) buf_printf(out, "%s *self = xs_region_alloc_nh(&xs_rg_%d, sizeof(%s));", c->cname, c->region, c->cname);
        else if (c->nohdr) buf_printf(out, "%s *self = xs_rg_on_%d ? xs_region_alloc_nh(&xs_rg_%d, sizeof(%s)) : xs_nh_heap(sizeof(%s));", c->cname, c->rdual, c->rdual, c->cname, c->cname);
        else if (c->region) buf_printf(out, "%s *self = xs_region_alloc%s(&xs_rg_%d, sizeof(%s), &%s_type);", c->cname, nz, c->region, c->cname, c->cname);
        else if (c->rdual) /* region.c: in its loop's region while an iteration runs, on the heap otherwise */
            buf_printf(out, "%s *self = xs_rg_on_%d ? xs_region_alloc_dual%s(&xs_rg_%d, sizeof(%s), &%s_type) : xs_obj_alloc_t%s(sizeof(%s), &%s_type);",
                       c->cname, c->rdual, nz, c->rdual, c->cname, c->cname, nz, c->cname, c->cname);
        else buf_printf(out, "%s *self = xs_obj_alloc_t%s(sizeof(%s), &%s_type);", c->cname, nz, c->cname, c->cname);
        if (cln_self) { buf_puts(out, " xs_cln_push((void **)&self);"); ctor_cln = true; }
    }
    for (int i = 0; i < c->nfields; i++) /* nullable primitives start as their null sentinel, not 0 */
        if (!c->fields[i].init && null_prim(c->fields[i].type)) { nl(); buf_printf(out, "self->%s = %s;", c->fields[i].cname, null_value(c->fields[i].type)); }
    nl();
    buf_puts(out, "size_t ar_mark = xs_ar_mark();");
    nl();
    buf_puts(out, "(void)ar_mark;");
    for (int i = 0; i < c->nfields; i++) {
        Field *fd = &c->fields[i];
        if (!fd->init) continue;
        used_ar = false;
        nl();
        buf_printf(out, "self->%s = ", fd->cname);
        emit_owned(fd->init);
        buf_puts(out, ";");
        drain_if_needed();
    }
    cur_func = NULL;
    if (ctor) {
        cur_func = ctor;
        vol_locals = stmt_has_try(ctor->body);
        in_ctor = true;
        emit_prof_enter(ctor);
        emit_dbg_enter(ctor);
        dbg_params(ctor, false, NULL);
        nl();
        emit_block(ctor->body, true);
        emit_dbg_finish();
        emit_prof_leave();
        in_ctor = false;
        cur_func = NULL;
    }
    nl();
    buf_puts(out, ctor_cln ? "xs_cln_n--; return self;" : "return self;");
    ctor_cln = false;
    indent--;
    nl();
    buf_puts(out, "}\n");

methods:
    for (int i = 0; i < c->nmethods; i++) {
        if (!c->methods[i]->used || c->methods[i]->cls != c || c->methods[i]->is_abstract) continue; /* inherited ones live in their class */
        buf_putc(out, '\n');
        Func *m = c->methods[i];
        if (m->deco_stub) { /* XB-107: its own body as <cname>__orig; the method itself calls its slot through the stub */
            char *cn = m->cname;
            m->cname = xs_sprintf("%s__orig", cn);
            emit_signature(m);
            emit_func_body(m);
            m->cname = cn;
            buf_putc(out, '\n');
            emit_signature(m);
            buf_printf(out, " {\n    %s%s(self", m->ret->kind == TY_VOID ? "" : "return ", m->deco_stub->cname);
            for (int k = 0; k < m->nparams; k++) buf_printf(out, ", %s", m->params[k].cname);
            buf_puts(out, ");\n}\n");
            continue;
        }
        emit_signature(m);
        emit_func_body(m);
    }
    cur_class = NULL;
}

/* ---- top-level variables that only top-level code uses become locals of main ----
   A global is stored back to memory before every call that may fail (the error path could read it); a local
   of main stays in a register: `let sum = 0; for (...) sum += ...` at the top level ran like code in a function. */
typedef struct { const char **v; int n, cap; } NameSet;
static void ns_add(NameSet *ns, const char *n) {
    if (!n) return;
    if (ns->n == ns->cap) { ns->cap = ns->cap ? ns->cap * 2 : 64; ns->v = realloc(ns->v, sizeof(char *) * (size_t)ns->cap); }
    ns->v[ns->n++] = n;
}
static bool ns_has(NameSet *ns, const char *n) {
    for (int i = 0; i < ns->n; i++) if (strcmp(ns->v[i], n) == 0) return true;
    return false;
}
static void fnrefs_stmt(Stmt *s, NameSet *ns, bool in_fn);
static void fnrefs_expr(Expr *e, NameSet *ns, bool in_fn) {
    if (!e) return;
    if (in_fn && e->kind == E_IDENT) ns_add(ns, e->cname);
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn) fnrefs_stmt(e->arrow->fn->body, ns, true); /* its body is a function */
    fnrefs_expr(e->lhs, ns, in_fn);
    fnrefs_expr(e->rhs, ns, in_fn);
    for (int i = 0; i < e->args.len; i++) fnrefs_expr(e->args.items[i], ns, in_fn);
}
static void fnrefs_stmt(Stmt *s, NameSet *ns, bool in_fn) {
    if (!s) return;
    fnrefs_expr(s->init, ns, in_fn);
    fnrefs_expr(s->expr, ns, in_fn);
    fnrefs_expr(s->cond, ns, in_fn);
    fnrefs_expr(s->for_update, ns, in_fn);
    fnrefs_stmt(s->for_init, ns, in_fn);
    fnrefs_stmt(s->body, ns, in_fn);
    fnrefs_stmt(s->else_body, ns, in_fn);
    for (int i = 0; i < s->stmts.len; i++) fnrefs_stmt(s->stmts.items[i], ns, in_fn);
    for (int i = 0; i < s->ncases; i++) { fnrefs_expr(s->cases[i].value, ns, in_fn); fnrefs_stmt(s->cases[i].body, ns, in_fn); }
}
static NameSet main_locals; /* top-level variables declared in main */
/* a top-level const of a number, int or boolean literal (const IM: int = 139968): a C constant, not a global
   assigned in main, so the C compiler folds it (x % IM becomes a multiplication, overflow checks with it go) */
static bool const_lit_global(Stmt *s) {
    if (s->kind != S_VAR || !s->is_const || xs_debug || !s->init || !s->type) return false;
    if (s->type->kind != TY_INT && s->type->kind != TY_NUMBER && s->type->kind != TY_BOOLEAN) return false;
    Expr *e = s->init;
    if (e->kind == E_UNARY && e->op == TK_MINUS) e = e->lhs ? e->lhs : e->rhs;
    return e && (e->kind == E_NUMBER || e->kind == E_BOOL) && e->type && e->type->kind == s->type->kind;
}
static bool rv_main_only(const char *c) { /* a top-level variable that is not a local of main is a global */
    for (int i = 0; i < prog->top.len; i++) {
        Stmt *t = prog->top.items[i];
        if (t->kind == S_VAR && t->cname && strcmp(t->cname, c) == 0) return ns_has(&main_locals, c);
    }
    return true;
}
static void find_main_locals(Program *p, bool has_try) {
    main_locals.n = 0;
    if (xs_debug || has_try || getenv("XISTO_NO_MAIN_LOCALS")) return;
    NameSet used = {0};
    for (int i = 0; i < p->nfuncs; i++) if (p->funcs[i]->body) fnrefs_stmt(p->funcs[i]->body, &used, true);
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        for (int k = 0; k < c->nfields; k++) fnrefs_expr(c->fields[k].init, &used, true);
        if (c->ctor && c->ctor->body) fnrefs_stmt(c->ctor->body, &used, true);
        for (int k = 0; k < c->nmethods; k++) if (c->methods[k]->body) fnrefs_stmt(c->methods[k]->body, &used, true);
    }
    for (int i = 0; i < p->top.len; i++) fnrefs_stmt(p->top.items[i], &used, false); /* arrows written at the top level */
    for (int i = 0; i < p->top.len; i++) {
        Stmt *s = p->top.items[i];
        if (s->kind == S_VAR && !s->cell && !s->exported && !ns_has(&used, s->cname)) ns_add(&main_locals, s->cname);
    }
    free(used.v);
}

static bool stmt_has_loop(Stmt *s) {
    if (!s) return false;
    if (s->kind == S_FOR || s->kind == S_WHILE || s->kind == S_FOROF) return true;
    if (stmt_has_loop(s->for_init) || stmt_has_loop(s->body) || stmt_has_loop(s->else_body)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (stmt_has_loop(s->stmts.items[i])) return true;
    for (int i = 0; i < s->ncases; i++) if (stmt_has_loop(s->cases[i].body)) return true;
    return false;
}

/* ---- functions that run once ----
   A function with no loop and no recursion, called only from code that itself runs once (main's top level
   outside loops, or another such function) and never used as a value: its body runs a single time, so the
   optimizer's work on it is wasted. Typically the wiring of an application (decorators, routes, modules). */
typedef struct { Func *callee, *caller; bool hot; } RoEdge;
static RoEdge *ro_edges;
static int nro, capro;
static void ro_add(Func *callee, Func *caller, bool hot) {
    if (!callee) return;
    if (nro == capro) { capro = capro ? capro * 2 : 256; ro_edges = realloc(ro_edges, sizeof(RoEdge) * (size_t)capro); }
    ro_edges[nro++] = (RoEdge){callee, caller, hot};
}
static void ro_stmt(Stmt *s, Func *ctx, int loop);
static void ro_expr(Expr *e, Func *ctx, int loop) {
    if (!e) return;
    switch (e->kind) {
    case E_CALL: if (e->func) ro_add(e->func, ctx, loop > 0); break;
    case E_MEMBER: if (e->is_call && e->func) { if (e->virt) e->func->escapes = true; ro_add(e->func, ctx, loop > 0); } break;
    case E_NEW_OBJ: if (e->cls && !e->cls->builtin && e->cls->ctor) ro_add(e->cls->ctor, ctx, loop > 0); break;
    case E_FUNC_REF: if (e->func) e->func->escapes = true; break;
    case E_ARROW: if (e->arrow && e->arrow->fn) ro_stmt(e->arrow->fn->body, ctx, loop + 1000); return; /* a closure may run many times */
    default: break;
    }
    ro_expr(e->lhs, ctx, loop);
    ro_expr(e->rhs, ctx, loop);
    for (int i = 0; i < e->args.len; i++) ro_expr(e->args.items[i], ctx, loop);
}
static void ro_stmt(Stmt *s, Func *ctx, int loop) {
    if (!s) return;
    int l = loop + (s->kind == S_FOR || s->kind == S_WHILE || s->kind == S_FOROF);
    ro_expr(s->init, ctx, loop);
    ro_expr(s->expr, ctx, s->kind == S_FOROF ? loop : l);
    ro_expr(s->cond, ctx, l);
    ro_expr(s->for_update, ctx, l);
    ro_stmt(s->for_init, ctx, loop);
    ro_stmt(s->body, ctx, l);
    ro_stmt(s->else_body, ctx, loop);
    for (int i = 0; i < s->stmts.len; i++) ro_stmt(s->stmts.items[i], ctx, loop);
    for (int i = 0; i < s->ncases; i++) { ro_expr(s->cases[i].value, ctx, loop); ro_stmt(s->cases[i].body, ctx, loop); }
}
/* does main receive, through always_inline (XS_FA) calls made once, a function with a loop? Then main is
   where that loop runs, and an unoptimized main would run it at -O0 (objects.ts: run(n) called once from the
   top level, 2.5x slower) */
static bool fa_into_main(Func *f) {
    for (int guard = 0; f && guard < 64; guard++) {
        if (strcmp(fn_storage(f), "XS_FA") != 0) return false;
        if (!f->call1_in) return true;
        f = f->call1_in;
    }
    return false;
}
static bool main_inlines_loop(Program *p) {
    for (int i = 0; i < p->nfuncs; i++) {
        Func *f = p->funcs[i];
        if (f->body && f->used && !f->mod->builtin && !f->is_native && stmt_has_loop(f->body) && fa_into_main(f)) return true;
    }
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (!c->used || c->builtin) continue;
        for (int k = 0; k < c->nmethods; k++) {
            Func *f = c->methods[k];
            if (f->body && f->used && f->cls == c && stmt_has_loop(f->body) && fa_into_main(f)) return true;
        }
        if (c->ctor && c->ctor->body && stmt_has_loop(c->ctor->body) && fa_into_main(c->ctor)) return true;
    }
    return false;
}
static void find_run_once(Program *p) {
    if (xs_debug || xs_profile || getenv("XISTO_NO_RUN_ONCE")) return;
    nro = 0;
    Func **all = NULL;
    int nall = 0;
    #define RO_PUSH(f) do { all = realloc(all, sizeof(Func *) * (size_t)(nall + 1)); all[nall++] = (f); } while (0)
    for (int i = 0; i < p->nfuncs; i++) if (p->funcs[i]->body && p->funcs[i]->used && !p->funcs[i]->mod->builtin) RO_PUSH(p->funcs[i]);
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (!c->used || c->builtin) continue;
        if (c->ctor && c->ctor->body) RO_PUSH(c->ctor);
        for (int k = 0; k < c->nmethods; k++) if (c->methods[k]->body && c->methods[k]->cls == c && c->methods[k]->used) RO_PUSH(c->methods[k]);
        for (int k = 0; k < c->nfields; k++) ro_expr(c->fields[k].init, c->ctor, 1000); /* run per object: hot */
    }
    for (int i = 0; i < nall; i++) ro_stmt(all[i]->body, all[i], 0);
    for (int i = 0; i < p->top.len; i++) ro_stmt(p->top.items[i], NULL, 0);
    /* candidates: straight-line, not generic templates, not generators, not escaping, not in a hierarchy */
    for (int i = 0; i < nall; i++) {
        Func *f = all[i];
        f->run_once = !f->escapes && !f->virt_target && !f->is_gen && !f->is_async && !stmt_has_loop(f->body) &&
                      !(f->cls && in_hierarchy(f->cls)) && !f->ntparams;
    }
    /* at least one call, every call cold, from cold code; recursion makes a call hot */
    bool changed = true;
    for (int i = 0; i < nall; i++) {
        bool any = false;
        for (int k = 0; k < nro; k++) if (ro_edges[k].callee == all[i]) { any = true; if (ro_edges[k].caller == all[i]) all[i]->run_once = false; }
        if (!any) all[i]->run_once = false;
    }
    while (changed) {
        changed = false;
        for (int k = 0; k < nro; k++) {
            RoEdge *e = &ro_edges[k];
            if (!e->callee->run_once) continue;
            if (e->hot || (e->caller && !e->caller->run_once)) { e->callee->run_once = false; changed = true; }
        }
    }
    free(all);
    #undef RO_PUSH
}

/* ---- objects without a header ----
   The 8-byte header (count + type) of an object is read by retain/release, print, the cycle collector, JSON
   and reflection. A class whose objects never meet any of those does not need it: binary-trees' TreeNode is 16
   bytes instead of 24, and its walk reads two thirds of the lines (Xeon: -14% CPU, 350 -> 300 MB). The class
   qualifies when the program proves, conservatively:
   - its objects live in a region (region.c: region or dual), its fields are numbers or references to itself,
     written only by the constructor, no base, subclasses, methods, decorators, frames (stackalloc.c), cycles;
   - no other type mentions it except directly (C or C | null): not in arrays, maps, sets, other classes'
     fields; and a value of it is never handed to a built-in (console.log, JSON, postMessage, strings), cast or
     concatenated: only stored in variables and its own fields, passed to user functions, compared, tested;
   - outside its loop's region (a dual class), every `new` runs a bounded number of times: inside a builder
     function (its body is one return), called from code that runs once (main's top level outside loops, or a
     function only called from there), or inside the region's loop. Those objects come from an arena that is
     never given back (they would have lived as long as the program's single call that made them).
   Retains and releases of its values are dropped (rcnt), like a region class's. XISTO_NO_NOHDR=1 turns it off;
   XISTO_REGION_CHECK=1 (which counts region objects) does too. */
static Class *nh_c;
static bool nh_ok;
static Func **nh_builders; static int nh_nb;
static bool nh_mentions(Type *t) { /* nested: in an array, a map, a set */
    if (!t) return false;
    switch (t->kind) {
    case TY_OBJECT: return t->cls == nh_c;
    case TY_ARRAY: case TY_NULLABLE: case TY_SET: return nh_mentions(t->elem);
    case TY_MAP: return nh_mentions(t->key) || nh_mentions(t->elem);
    default: return false;
    }
}
static bool nh_direct(Type *t) { return t && ((t->kind == TY_OBJECT && t->cls == nh_c) || (t->kind == TY_NULLABLE && t->elem && t->elem->kind == TY_OBJECT && t->elem->cls == nh_c)); }
static bool nh_is_builder(Func *f) { for (int i = 0; i < nh_nb; i++) if (nh_builders[i] == f) return true; return false; }
static bool nh_expr_news(Expr *e) {
    if (!e) return false;
    if (e->kind == E_NEW_OBJ && e->cls == nh_c) return true;
    if (e->kind == E_ARROW) return false;
    if (nh_expr_news(e->lhs) || nh_expr_news(e->rhs)) return true;
    for (int i = 0; i < e->args.len; i++) if (nh_expr_news(e->args.items[i])) return true;
    return false;
}
static bool nh_stmt_news(Stmt *s) {
    if (!s) return false;
    if (nh_expr_news(s->init) || nh_expr_news(s->expr) || nh_expr_news(s->cond) || nh_expr_news(s->for_update)) return true;
    if (nh_stmt_news(s->for_init) || nh_stmt_news(s->body) || nh_stmt_news(s->else_body)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (nh_stmt_news(s->stmts.items[i])) return true;
    for (int i = 0; i < s->ncases; i++) if (nh_stmt_news(s->cases[i].body)) return true;
    return false;
}
static bool nh_cold(Func *f) { /* runs a bounded number of times: only called once-ish from main's straight line */
    if (!f || f->escapes || f->is_gen || f->is_async || f->cls) return false;
    bool any = false;
    for (int k = 0; k < nro; k++) {
        RoEdge *e = &ro_edges[k];
        if (e->callee != f) continue;
        any = true;
        if (e->hot || e->caller == f) return false;
        if (e->caller && !nh_cold(e->caller)) return false; /* (the call graph of cold code is shallow) */
    }
    return any;
}
static void nh_stmt(Stmt *s, Func *ctx, int loop, int rg);
static void nh_expr(Expr *e, Expr *parent, Func *ctx, int loop, int rg) {
    if (!e || !nh_ok) return;
    if (e->type && nh_mentions(e->type) && !nh_direct(e->type)) { nh_ok = false; return; }
    if (e->type && nh_direct(e->type) && parent) { /* where a value of the class may go */
        bool fine = false;
        switch (parent->kind) {
        case E_CALL: fine = parent->func && parent->func->body && !parent->builtin.cfn && parent->lhs != e; break;
        case E_NEW_OBJ: fine = parent->cls == nh_c; break;
        case E_MEMBER: fine = !parent->is_call && parent->lhs == e && parent->field >= 0; break;
        case E_ASSIGN: fine = parent->op == TK_ASSIGN; break;
        case E_BINARY: fine = parent->op != TK_PLUS && !parent->builtin.cfn; break;
        case E_COND: fine = true; break;
        case E_UNARY: fine = parent->op == TK_NOT; break;
        case E_AS: fine = parent->op == TK_NOT && !parent->elem_type; break;
        default: fine = false;
        }
        if (!fine) { nh_ok = false; return; }
    }
    if (e->kind == E_NEW_OBJ && e->cls == nh_c) { /* bounded outside the region */
        bool ok = rg == nh_c->rdual || rg == nh_c->region || (ctx && nh_is_builder(ctx)) || (loop == 0 && (!ctx || nh_cold(ctx)));
        if (!ok) { nh_ok = false; return; }
    }
    if ((e->kind == E_CALL || (e->kind == E_MEMBER && e->is_call)) && e->func && nh_is_builder(e->func) && ctx != e->func && !(ctx && nh_is_builder(ctx))) {
        bool ok = rg == nh_c->rdual || rg == nh_c->region || (loop == 0 && (!ctx || nh_cold(ctx)));
        if (!ok) { nh_ok = false; return; }
    }
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn) { nh_stmt(e->arrow->fn->body, e->arrow->fn, loop + 1000, rg); return; }
    nh_expr(e->lhs, e, ctx, loop, rg);
    nh_expr(e->rhs, e, ctx, loop, rg);
    for (int i = 0; i < e->args.len; i++) nh_expr(e->args.items[i], e, ctx, loop, rg);
}
static void nh_stmt(Stmt *s, Func *ctx, int loop, int rg) {
    if (!s || !nh_ok) return;
    bool isloop = s->kind == S_FOR || s->kind == S_WHILE || s->kind == S_FOROF;
    int l = loop + isloop, r = isloop && s->region ? s->region : rg;
    if (s->kind == S_VAR && s->type && nh_mentions(s->type) && !nh_direct(s->type)) { nh_ok = false; return; }
    if (s->kind == S_FOROF && s->expr && s->expr->type && nh_mentions(s->expr->type)) { nh_ok = false; return; }
    nh_expr(s->init, NULL, ctx, loop, rg);
    nh_expr(s->expr, NULL, ctx, s->kind == S_FOROF ? loop : l, s->kind == S_FOROF ? rg : r);
    nh_expr(s->cond, NULL, ctx, l, r);
    nh_expr(s->for_update, NULL, ctx, l, r);
    nh_stmt(s->for_init, ctx, loop, rg);
    nh_stmt(s->body, ctx, l, r);
    nh_stmt(s->else_body, ctx, loop, rg);
    for (int i = 0; i < s->stmts.len; i++) nh_stmt(s->stmts.items[i], ctx, loop, rg);
    for (int i = 0; i < s->ncases; i++) { nh_expr(s->cases[i].value, NULL, ctx, loop, rg); nh_stmt(s->cases[i].body, ctx, loop, rg); }
}
static bool nh_type_bad(Type *t) { return t && nh_mentions(t) && !nh_direct(t); }
static void nohdr_compute(Program *p) {
    if (xs_debug || xs_profile || getenv("XISTO_NO_NOHDR")) return;
    if (region_checked < 0) region_checked = getenv("XISTO_REGION_CHECK") && strcmp(getenv("XISTO_REGION_CHECK"), "1") == 0;
    if (region_checked) return;
    for (int ci = 0; ci < p->nclasses; ci++) {
        Class *c = p->classes[ci];
        if (!c->used || c->builtin || !(c->region || c->rdual) || c->base || c->nsubs || c->is_interface || c->is_abstract ||
            c->nmethods || c->stack_at || c->data_only || c->decos.len || class_cyclic(c) || !c->ctor) continue;
        nh_c = c;
        nh_ok = true;
        for (int k = 0; k < c->nfields && nh_ok; k++) {
            Type *ft = c->fields[k].type;
            if (c->fields[k].init && cref(ft)) nh_ok = false;
            if (cref(ft) && !nh_direct(ft)) nh_ok = false;
            if (mut_has(c, c->fields[k].name)) nh_ok = false;
        }
        for (int cj = 0; cj < p->nclasses && nh_ok; cj++) {
            Class *o = p->classes[cj];
            if (o == c || !o->used) continue;
            for (int k = 0; k < o->nfields; k++) if (nh_mentions(o->fields[k].type)) nh_ok = false;
        }
        if (!nh_ok) continue;
        /* the builders: free functions whose body is one return and that make the class */
        nh_nb = 0;
        for (int i = 0; i < p->nfuncs; i++) {
            Func *f = p->funcs[i];
            if (!f->body || !f->used || f->mod->builtin || f->is_native) continue;
            for (int k = 0; k < f->nparams; k++) if (nh_type_bad(f->params[k].type)) nh_ok = false;
            if (nh_type_bad(f->ret)) nh_ok = false;
            if (!nh_stmt_news(f->body)) continue;
            Stmt *b = f->body;
            if (b->kind == S_BLOCK && b->stmts.len == 1 && b->stmts.items[0]->kind == S_RETURN && !f->escapes && !f->is_gen && !f->is_async && !f->cls) {
                nh_builders = realloc(nh_builders, sizeof(Func *) * (size_t)(nh_nb + 1));
                nh_builders[nh_nb++] = f;
            }
        }
        for (int i = 0; i < p->nfuncs && nh_ok; i++) {
            Func *f = p->funcs[i];
            if (f->body && f->used && !f->mod->builtin && !f->is_native) nh_stmt(f->body, f, 0, 0);
        }
        for (int cj = 0; cj < p->nclasses && nh_ok; cj++) {
            Class *o = p->classes[cj];
            if (!o->used || o->builtin) continue;
            if (o->ctor && o->ctor->body && o != c) nh_stmt(o->ctor->body, o->ctor, 1000, 0); /* (a constructor may run many times) */
            for (int k = 0; k < o->nmethods; k++) if (o->methods[k]->body && o->methods[k]->cls == o) nh_stmt(o->methods[k]->body, o->methods[k], 1000, 0);
            for (int k = 0; k < o->nfields; k++) nh_expr(o->fields[k].init, NULL, NULL, 1000, 0);
        }
        for (int i = 0; i < p->top.len && nh_ok; i++) nh_stmt(p->top.items[i], NULL, 0, 0);
        if (nh_ok) c->nohdr = true;
    }
}

/* ---- consumed constructor parameters ----
   `constructor(l: Tree, r: Tree) { this.left = l; this.right = r; }`: the reference each argument brings is the
   one the field keeps. The caller hands over ownership (a new value moves in, a borrowed one is retained once)
   and the store takes it without a retain: no autorelease, retain and release per argument. Only when nothing
   before the stores can fail (or the moved value would leak): the stores open the body, field initializers
   cannot fail, the parameter is used nowhere else, and the class has no base or subclass. */
static int ident_uses_s(Stmt *s, const char *n);
static int ident_uses_e(Expr *e, const char *n) {
    if (!e) return 0;
    int k = e->kind == E_IDENT && e->cname && strcmp(e->cname, n) == 0;
    if (e->kind == E_ARROW) return 1000; /* captured by a closure: not consumed */
    k += ident_uses_e(e->lhs, n) + ident_uses_e(e->rhs, n);
    for (int i = 0; i < e->args.len; i++) k += ident_uses_e(e->args.items[i], n);
    return k;
}
static int ident_uses_s(Stmt *s, const char *n) {
    if (!s) return 0;
    int k = ident_uses_e(s->init, n) + ident_uses_e(s->expr, n) + ident_uses_e(s->cond, n) + ident_uses_e(s->for_update, n);
    k += ident_uses_s(s->for_init, n) + ident_uses_s(s->body, n) + ident_uses_s(s->else_body, n);
    for (int i = 0; i < s->stmts.len; i++) k += ident_uses_s(s->stmts.items[i], n);
    for (int i = 0; i < s->ncases; i++) k += ident_uses_e(s->cases[i].value, n) + ident_uses_s(s->cases[i].body, n);
    return k;
}
static void find_consumed(Program *p) {
    if (getenv("XISTO_NO_CONSUME") || xs_debug) return;
    for (int ci = 0; ci < p->nclasses; ci++) {
        Class *c = p->classes[ci];
        Func *f = c->ctor;
        if (c->builtin || !f || !f->body || in_hierarchy(c) || f->is_native) continue;
        bool inits_ok = true;
        for (int k = 0; k < c->nfields; k++) if (c->fields[k].init && expr_fails(c->fields[k].init)) inits_ok = false;
        if (!inits_ok) continue;
        Stmt *b = f->body;
        for (int i = 0; i < b->stmts.len; i++) { /* the opening run of `this.f = <param>` */
            Stmt *st = b->stmts.items[i];
            Expr *e = st->kind == S_EXPR ? st->expr : NULL;
            if (!e || e->kind != E_ASSIGN || e->op != TK_ASSIGN || !e->lhs || e->lhs->kind != E_MEMBER || e->lhs->field < 0 ||
                !e->lhs->lhs || e->lhs->lhs->kind != E_THIS || !e->rhs || e->rhs->kind != E_IDENT) break;
            if (!cref(e->type)) continue; /* this.x = x of a number: cannot fail, the run goes on */
            for (int k = 0; k < f->nparams; k++) {
                Param *pm = &f->params[k];
                if (!cref(pm->type) || pm->cell || !e->rhs->cname || strcmp(e->rhs->cname, pm->cname) != 0) continue;
                if (ident_uses_s(b, pm->cname) != 1 || !ty_equal(pm->type, e->lhs->type)) continue;
                pm->consumed = true;
                e->rhs->builtin.cfn = "consumed"; /* emit_owned: the parameter as it is */
            }
        }
    }
}
/* the length a top-level const array declaration fixes: a literal's count, or new Array<T>(N).fill(v) with N a
   literal or a const global with one; 0 when not known */
static int64_t garr_len_of(Program *p, Stmt *s) {
    if (s->kind != S_VAR || !s->is_const || !s->cname || s->cell || s->exported || !s->init || !s->type || s->type->kind != TY_ARRAY) return 0;
    Expr *e = s->init;
    if (e->kind == E_ARRAY_LIT) return e->args.len;
    if (e->kind == E_MEMBER && e->is_call && e->str && strcmp(e->str, "fill") == 0 && e->lhs && e->lhs->kind == E_NEW_ARRAY && e->lhs->lhs) {
        Expr *n = e->lhs->lhs;
        if (n->kind == E_NUMBER && n->is_int_lit) return n->ival > 0 ? n->ival : 0;
        if (n->kind == E_IDENT && n->cname)
            for (int i = 0; i < p->top.len; i++) {
                Stmt *d = p->top.items[i];
                if (d->kind == S_VAR && d->cname && strcmp(d->cname, n->cname) == 0)
                    return const_lit_global(d) && d->init->kind == E_NUMBER && d->init->is_int_lit && d->init->ival > 0 ? d->init->ival : 0;
            }
    }
    return 0;
}
static void garr_collect(Program *p) {
    nbce_garr = ncig = 0;
    if (xs_debug || getenv("XISTO_NO_BCE")) return;
    for (int i = 0; i < p->top.len && ncig < 64; i++) { /* `const N: int = 5`: a known value wherever it is read */
        Stmt *s = p->top.items[i];
        if (s->kind == S_VAR && s->cname && !s->exported && const_lit_global(s) && s->type->kind == TY_INT && s->init->kind == E_NUMBER && s->init->is_int_lit)
            cig[ncig++] = (BceFix){s->cname, s->init->ival};
    }
    for (int i = 0; i < p->top.len && nbce_garr < 32; i++) {
        Stmt *s = p->top.items[i];
        int64_t n = garr_len_of(p, s);
        if (!n || ns_has(&main_locals, s->cname) || !garr_program_ok(p, s->cname, true)) continue;
        BceGArr g = {s->cname, n, false, 0, 0, false};
        Expr *e = s->init;
        if (e->kind == E_ARRAY_LIT && s->type->elem && s->type->elem->kind == TY_INT && garr_program_ok(p, s->cname, false)) {
            g.has_rng = true;
            for (int k = 0; k < e->args.len; k++) {
                Expr *v = e->args.items[k];
                bool neg = v->kind == E_UNARY && v->op == TK_MINUS && v->lhs; /* -1 */
                if (neg) v = v->lhs;
                if (v->kind != E_NUMBER || !v->is_int_lit) { g.has_rng = false; break; }
                int64_t val = neg ? -v->ival : v->ival;
                if (k == 0 || val < g.lo) g.lo = val;
                if (k == 0 || val > g.hi) g.hi = val;
                bool p2 = val > 0 && (val & (val - 1)) == 0;
                g.pow2 = k == 0 ? p2 : g.pow2 && p2; /* every entry a power of two: x % T[i] is a mask */
            }
        }
        bce_garr[nbce_garr++] = g;
    }
}

/* one top-level statement, in main or in a module's xs_init_mN */
static void emit_top_stmt(Program *p, Stmt *s) {
    if (s->kind == S_VAR && const_lit_global(s) && !p->uses_workers && !ns_has(&main_locals, s->cname)) {
        dbg_local(s->name, s->type, s->cname, s->is_const); /* (set where it is declared) */
    } else if (s->kind == S_VAR) {
        used_ar = false;
        buf_printf(out, "%s = ", s->cname);
        if (s->init && nzv_has(s->cname)) { buf_printf(out, "(int64_t)(%s_z = ", s->cname); emit_nzd(s->init); buf_puts(out, ")"); } else
        emit_owned(s->init);
        buf_puts(out, ";");
        dbg_local(s->name, s->type, s->cname, s->is_const);
        drain_if_needed();
    } else {
        emit_stmt(s);
    }
}
/* the file name of a module, for a comment (no directories, no comment terminator) */
static const char *path_base_name(const char *path) {
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    return strstr(b, "*/") ? "module" : b;
}
/* XB-107's this register (emitted only for a program that uses it: other programs' C is unchanged) */
static const char xs_this_reg_c[] =
    "/* XB-107: the object a function (this: T, ...) is called on: set by the caller just before the call (a method's slot,\n"
    "   f.apply(obj, args)), taken in the function's prologue (owned from then on) */\n"
    "static XS_TLS void *xs_this_reg;\n"
    "static inline void xs_this_set(void *v) {\n"
    "    void *old = xs_this_reg;\n"
    "    xs_this_reg = v ? xs_retain_(v) : NULL;\n"
    "    if (old) xs_release_(old);\n"
    "}\n"
    "#define xs_void_null(x) ((x), (void *)0)\n"
    "static inline void *xs_this_take(void) {\n"
    "    void *v = xs_this_reg;\n"
    "    xs_this_reg = NULL;\n"
    "    return v;\n"
    "}\n"
;
/* runtime pieces only some programs use, emitted only for them (every other program's C is unchanged) */
static const char xs_v8_c[] =
    "#include <sys/resource.h>\n"
    "#include <unistd.h>\n"
    "/* xisto:v8: V8's default heap limit on 64 bits (a quarter of the memory, at most 4 GiB) and the memory in use */\n"
    "static inline double xs_v8_heap_limit(void) {\n"
    "    double mem = (double)sysconf(_SC_PHYS_PAGES) * (double)sysconf(_SC_PAGESIZE);\n"
    "    double lim = mem / 4;\n"
    "    return lim > 4294967296.0 ? 4294967296.0 : lim;\n"
    "}\n"
    "static inline double xs_v8_heap_used(void) {\n"
    "    struct rusage ru;\n"
    "    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0;\n"
    "#ifdef __APPLE__\n"
    "    return (double)ru.ru_maxrss;\n"
    "#else\n"
    "    return (double)ru.ru_maxrss * 1024.0;\n"
    "#endif\n"
    "}\n"
;
static const char xs_unk_map_c[] =
    "/* x instanceof Map (want_map) / Set with x an unknown: one of the program's maps (sets), not a Record */\n"
    "static inline bool xs_unk_is_map(void *v, int64_t want_map) {\n"
    "    return xs_is_a(v, xs_map_free) && ((xs_map *)v)->is_map == (want_map != 0) && !((xs_map *)v)->record;\n"
    "}\n"
;
static const char xs_symbol_for_c[] =
    "#include <pthread.h>\n"
    "/* Symbol.for(key): the symbol of that key, the same one each time (the registry lives as long as the program) */\n"
    "XS_NOINLINE xs_Symbol *xs_Symbol_for(xs_str *key) {\n"
    "    static struct xs_symreg { xs_str *k; xs_Symbol *y; struct xs_symreg *next; } *reg;\n"
    "    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;\n"
    "    pthread_mutex_lock(&mu);\n"
    "    for (struct xs_symreg *r = reg; r; r = r->next)\n"
    "        if (r->k->len == key->len && memcmp(r->k->data, key->data, (size_t)key->len) == 0) { pthread_mutex_unlock(&mu); return xs_retain(r->y); }\n"
    "    struct xs_symreg *r = malloc(sizeof *r);\n"
    "    if (!r) xs_panic(\"out of memory\");\n"
    "    r->k = xs_str_immortal(key->data, (size_t)key->len); /* (the registry's: process-lifetime, not counted as a leak) */\n"
    "    r->y = xs_Symbol_new(r->k);\n"
    "    r->y->hdr.rc = XS_IMMORTAL;\n"
    "    xs_live--;\n"
    "    xs_allocs--;\n"
    "    r->next = reg;\n"
    "    reg = r;\n"
    "    xs_Symbol *y = xs_retain(r->y);\n"
    "    pthread_mutex_unlock(&mu);\n"
    "    return y;\n"
    "}\n"
;
void emit_program(Program *p, Buf *o) {
    out = o;
    prog = p;
    if (p->uses_this_reg) buf_puts(out, xs_this_reg_c);
    if (p->uses_v8) buf_puts(out, xs_v8_c);
    if (p->uses_unk_map) buf_puts(out, xs_unk_map_c);
    if (p->uses_symbol_for) buf_puts(out, xs_symbol_for_c);
    indent = 0;
    site_tab.len = 0;
    if (site_tab.data) site_tab.data[0] = 0;
    nsites = 0;
    cyc_done = false;
    region_count = 0;
    for (int i = 0; i < p->nclasses; i++) p->classes[i]->region = p->classes[i]->rdual = 0;
    region_allow_dual = !program_has_try(p) && !getenv("XISTO_NO_DUAL"); /* XISTO_NO_DUAL: measuring knob */
    if (!xs_debug) region_compute(p);
    cyc_compute(p);
    for (int i = 0; i < p->nclasses; i++) if (p->classes[i]->rdual && class_cyclic(p->classes[i])) p->classes[i]->rdual = 0;
    free(region_dual);
    region_dual = xs_alloc(sizeof(bool) * (size_t)(region_count + 1));
    for (int i = 0; i < p->nclasses; i++) if (p->classes[i]->rdual) region_dual[p->classes[i]->rdual] = true;
    if (!xs_debug && !xs_profile) stack_compute(p, class_cyclic);
    rg_compute(p);
    prog_try = program_has_try(p);
    use_cln = (p->uses_net || program_has_try(p)) && !getenv("XISTO_NO_CLN"); /* XISTO_NO_CLN: measuring knob */
    find_consumed(p);
    find_run_once(p);
    nohdr_compute(p);
    count_calls(p);
    buf_puts(out, "\n/* ---- program ---- */\nextern const xs_site xs_sitetab[];\n");
    for (int r = 1; r <= region_count; r++) {
        buf_printf(out, "static XS_TLS xs_region xs_rg_%d;\n", r);
        if (region_dual[r]) buf_printf(out, "#define xs_rg_on_%d xs_rg_%d.on /* an iteration of its loop is running */\n", r, r);
    }

    lit_used = xs_alloc(sizeof(bool) * (size_t)(p->nlits ? p->nlits : 1));
    size_t lits_at = out->len; /* declarations are inserted here once the body says which literals it uses */

    /* an inline type written in a generic's own text ({ data: T | null } in a template's constructor): each instance
       has its copy with T bound; the template's copy is never made and has no C type for T */
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (!c->used || c->builtin || !c->name || strncmp(c->name, "__Obj_", 6) != 0) continue;
        for (int k = 0; k < c->nfields; k++) if (type_has_tparam(c->fields[k].type)) { c->used = false; break; }
    }
    /* native (declared) classes: their struct, free and print come from the package's C */
    for (int i = 0; i < p->nclasses; i++) if (p->classes[i]->used && !p->classes[i]->builtin) buf_printf(out, "typedef struct %s %s;\n", p->classes[i]->cname, p->classes[i]->cname);
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (!c->used || c->builtin) continue;
        buf_printf(out, c->nohdr ? "struct %s { /* no header: nohdr_compute */" : "struct %s { xs_hdr hdr;", c->cname);
        for (int k = 0; k < c->nfields; k++) {
            buf_puts(out, " ");
            emit_decl(c->fields[k].type, c->fields[k].cname);
            buf_puts(out, ";");
        }
        buf_puts(out, " };\n");
    }
    {
        bool top_try = false;
        for (int i = 0; i < p->top.len; i++) if (stmt_has_try(p->top.items[i])) top_try = true;
        find_main_locals(p, top_try);
    }
    nbce_fix_g = 0; /* the globals of a known length (bce_fixed_n) */
    for (int i = 0; i < p->top.len; i++) {
        Stmt *s = p->top.items[i];
        int64_t n = bce_fixed_of(s);
        if (n && !ns_has(&main_locals, s->cname) && nbce_fix_g < 32) bce_fix_g[nbce_fix_g++] = (BceFix){s->cname, n};
    }
    garr_collect(p); /* the constant arrays and tables (bce_garr) */
    fixparam_compute(p); /* (after the top-level const ints: new Float64Array(N) reads them) */
    for (int i = 0; i < p->top.len; i++) {
        Stmt *s = p->top.items[i];
        if (s->kind != S_VAR || ns_has(&main_locals, s->cname)) continue;
        BceGArr *g = garr_find(s->cname);
        if (g && g->has_rng) { /* a constant int table (garr_collect): also a C constant, read where the index is within it */
            buf_printf(out, "static const int64_t %s_c[%d] = {", s->cname, s->init->args.len);
            for (int k = 0; k < s->init->args.len; k++) {
                Expr *v = s->init->args.items[k];
                bool neg = v->kind == E_UNARY && v->op == TK_MINUS;
                buf_printf(out, "%sINT64_C(%lld)", k ? ", " : "", (long long)(neg ? -v->lhs->ival : v->ival));
            }
            buf_puts(out, "};\n");
        }
        if (const_lit_global(s) && !p->uses_workers) {
            buf_puts(out, "static const ");
            emit_decl(s->type, s->cname);
            buf_puts(out, " = ");
            emit_expr(s->init);
            buf_puts(out, ";\n");
            continue;
        }
        buf_puts(out, p->uses_workers ? "static XS_TLS " : "static "); /* (workers: each thread runs its own copy of the program) */
        emit_decl(s->type, s->cname);
        buf_puts(out, ";\n");
    }
    buf_putc(out, '\n');
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (!c->used || c->builtin) continue;
        if (in_hierarchy(c)) {
            Func *ec = emit_eff_ctor(c);
            buf_printf(out, "static void %s_init(%s *self", c->cname, c->cname);
            for (int k = 0; ec && k < ec->nparams; k++) { buf_puts(out, ", "); emit_decl(ec->params[k].type, ec->params[k].cname); }
            buf_puts(out, ");\n");
            if (!c->is_abstract) {
                buf_printf(out, "XS_FN %s *%s_new(", c->cname, c->cname);
                for (int k = 0; ec && k < ec->nparams; k++) { if (k) buf_puts(out, ", "); emit_decl(ec->params[k].type, ec->params[k].cname); }
                if (!ec || !ec->nparams) buf_puts(out, "void");
                buf_puts(out, ");\n");
            }
        } else {
            if (c->ctor) emit_signature(c->ctor);
            else buf_printf(out, "static %s *%s_new(void)", c->cname, c->cname);
            buf_puts(out, ";\n");
            if (c->stack_at) { emit_new_at_signature(c); buf_puts(out, ";\n"); }
        }
        for (int k = 0; k < c->nmethods; k++) {
            if (!c->methods[k]->used || c->methods[k]->cls != c || c->methods[k]->is_abstract) continue;
            if (c->methods[k]->deco_stub) { /* (XB-107: its own body too) */
                Func *m = c->methods[k];
                char *cn = m->cname;
                m->cname = xs_sprintf("%s__orig", cn);
                emit_signature(m);
                buf_puts(out, ";\n");
                m->cname = cn;
            }
            emit_signature(c->methods[k]);
            buf_puts(out, ";\n");
        }
    }
    for (int i = 0; i < p->nfuncs; i++) {
        if (p->funcs[i]->mod->builtin || p->funcs[i]->is_native || !p->funcs[i]->used) continue;
        emit_signature(p->funcs[i]);
        buf_puts(out, ";\n");
        if (p->funcs[i]->par_native) { par_signature(p->funcs[i]); buf_puts(out, ";\n"); } /* its pool version (xisto:parallel) */
        if (p->funcs[i]->body && !p->funcs[i]->is_gen) { /* its inner spine version (emit_spine, emit_spine_num) */
            Func *saved = cur_func; Spine sp_;
            cur_func = p->funcs[i];
            bool sp = spine_fn(p->funcs[i], &sp_);
            cur_func = saved;
            if (sp && sp_.num) { /* emit_spine_num: the int64 loop and the plain fallback */
                char *cname = p->funcs[i]->cname;
                buf_printf(out, "XS_FN int64_t %s_sp(", cname);
                emit_decl(p->funcs[i]->params[0].type, p->funcs[i]->params[0].cname);
                buf_puts(out, ");\n");
                p->funcs[i]->cname = xs_sprintf("%s_dbl", cname);
                emit_signature(p->funcs[i]);
                p->funcs[i]->cname = cname;
                buf_puts(out, ";\n");
            } else if (sp && spine_wide_on()) {
                char *cname = p->funcs[i]->cname;
                p->funcs[i]->cname = xs_sprintf("%s_sp", cname);
                emit_signature(p->funcs[i]);
                p->funcs[i]->cname = cname;
                buf_puts(out, ";\n");
            }
        }
        if (rg_hoist_on && p->funcs[i]->rg_hoist > 0 && p->funcs[i]->rg_used) { /* its region version (rg-hoist) */
            char *cname = p->funcs[i]->cname;
            p->funcs[i]->cname = rg_name(p->funcs[i]);
            emit_signature(p->funcs[i]);
            p->funcs[i]->cname = cname;
            buf_puts(out, ";\n");
        }
    }
    /* class bodies, functions and main go to a separate buffer so JSON helpers they need can be placed first */
    Buf body = {0};
    Buf *main_out = out;
    out = &body;
    json_fns.len = 0;
    if (json_fns.data) json_fns.data[0] = 0;
    json_protos.len = 0;
    if (json_protos.data) json_protos.data[0] = 0;
    njson = 0;
    arrow_fns.len = 0;
    if (arrow_fns.data) arrow_fns.data[0] = 0;
    tramp_fns.len = 0;
    if (tramp_fns.data) tramp_fns.data[0] = 0;
    ntramps = 0;
    clone_fns.len = 0;
    if (clone_fns.data) clone_fns.data[0] = 0;
    nclones = 0;
    for (int i = 0; i < p->nclasses; i++) if (p->classes[i]->used && !p->classes[i]->builtin) emit_class(p->classes[i]);
    for (int i = 0; i < p->nfuncs; i++) {
        if (p->funcs[i]->mod->builtin || p->funcs[i]->is_native || !p->funcs[i]->used) continue;
        buf_putc(out, '\n');
        emit_signature(p->funcs[i]);
        emit_func_body(p->funcs[i]);
        if (p->funcs[i]->par_native) emit_par_variant(p->funcs[i]);
    }
    if (rg_hoist_on) for (int i = 0; i < p->nfuncs; i++) if (p->funcs[i]->rg_hoist > 0 && p->funcs[i]->rg_used) emit_rg_clone(p->funcs[i]);

    vol_locals = false;
    for (int i = 0; i < p->top.len; i++) if (stmt_has_try(p->top.items[i])) vol_locals = true;
    bool wbody = p->uses_workers && !xs_debug; /* (the debugger runs one thread) */
    /* XB-176: the top level of each imported module in a function of its own (xs_init_mN), called by main in the
       same order. One main with every module's decorators, reflection and constants was 16.6 MB of C in a NestJS
       app (1 372 modules): clang went past 8 GB on it. The entry module stays in main (a one-file program, as the
       benchmarks are, emits the same C). A module whose top level is not one contiguous run stays in main too. */
    int *top_run = calloc((size_t)(p->top.len ? p->top.len : 1), sizeof(int)); /* run id at its first statement, -1 inside it, 0 in main */
    int nmodfn = 0;
    Module *entry = p->nmodules ? p->modules[p->nmodules - 1] : NULL;
    int imported_top = 0; /* (a small program keeps one main: the split only pays for a big top level) */
    for (int i = 0; i < p->top.len; i++) if (p->top.items[i]->mod != entry) imported_top++;
    const char *mt = getenv("XISTO_MOD_INIT_MIN");
    if (!xs_debug && !wbody && p->nmodules > 1 && imported_top >= (mt ? atoi(mt) : 200) && !getenv("XISTO_NO_MOD_INIT")) {
        for (int i = 0; i < p->top.len;) {
            Module *m = p->top.items[i]->mod;
            int j = i;
            while (j < p->top.len && p->top.items[j]->mod == m) j++;
            bool ok = m && m != entry && !m->builtin && !(m->path && strncmp(m->path, "builtin:", 8) == 0);
            for (int k = 0; k < p->top.len && ok; k++) if ((k < i || k >= j) && p->top.items[k]->mod == m) ok = false;
            for (int k = i; k < j && ok; k++) {
                Stmt *t = p->top.items[k];
                if (t->kind == S_VAR && nzv_has(t->cname)) ok = false; /* (its _z shadow lives in main) */
            }
            if (ok) {
                top_run[i] = ++nmodfn;
                for (int k = i + 1; k < j; k++) top_run[k] = -1;
            }
            i = j;
        }
    }
    Buf *main_buf = out;
    size_t main_at = out->len; /* the module functions go here, before main */
    { /* a top level without loops (a server, an app: it wires things up and hands over) is not optimized */
        bool loops = false;
        for (int i = 0; i < p->top.len && !loops; i++) loops = stmt_has_loop(p->top.items[i]);
        loops = loops || main_inlines_loop(p);
        if (wbody) buf_puts(out, "\nstatic void xs_prog_body(void) { /* the program: main's thread and each worker's run it (xisto:worker_threads) */");
        else buf_puts(out, loops || xs_debug ? "\nint main(int argc, char **argv) {" : "\nXS_RUN_ONCE int main(int argc, char **argv) {");
    }
    indent++;
    if (!wbody) { nl(); buf_puts(out, "xs_init(argc, argv);"); }
    nl();
    buf_puts(out, "size_t ar_mark = xs_ar_mark();");
    nl();
    buf_puts(out, "(void)ar_mark;");
    if (!wbody) { nl(); buf_puts(out, "xs_init_lits();"); }
    if (xs_debug) { nl(); buf_puts(out, "xs_dbg_init();"); }
    for (int i = 0; i < p->top.len; i++) { /* top-level variables only top-level code uses: locals */
        Stmt *s = p->top.items[i];
        if (top_run[i]) continue; /* (a module function's: file statics, declared with it) */
        if (s->kind != S_VAR || !ns_has(&main_locals, s->cname)) continue;
        nl();
        emit_decl(s->type, s->cname);
        buf_puts(out, " = 0;");
    }
    cur_func = NULL;
    rv_compute(NULL, p->top.items, p->top.len);
    nzv_compute(NULL, p->top.items, p->top.len);
    for (int i = 0; i < nnzv; i++) { nl(); buf_printf(out, "double %s_z = 0; (void)%s_z;", nzv[i], nzv[i]); } /* XB-087 (only main reads them) */
    Buf modfns = {0};
    if (nmodfn) { /* the module functions, with the analyses above (ranges, borrowed values) in place */
        Buf *mo = out;
        int mindent = indent;
        bool mvol = vol_locals;
        out = &modfns;
        for (int i = 0; i < p->top.len; i++) {
            if (top_run[i] <= 0) continue;
            int j = i + 1;
            while (j < p->top.len && top_run[j] == -1) j++;
            for (int k = i; k < j; k++) { /* its top-level locals: file statics (main releases them at the end, as before) */
                Stmt *t = p->top.items[k];
                if (t->kind != S_VAR || !ns_has(&main_locals, t->cname)) continue;
                buf_puts(out, "static ");
                emit_decl(t->type, t->cname);
                buf_puts(out, ";\n");
            }
            bool rloops = false, rtry = false;
            for (int k = i; k < j; k++) { rloops = rloops || stmt_has_loop(p->top.items[k]); rtry = rtry || stmt_has_try(p->top.items[k]); }
            buf_printf(out, "%s void xs_init_m%d(void) { /* %s */", rloops ? "XS_NOINLINE" : "XS_RUN_ONCE static", top_run[i], p->top.items[i]->mod->path ? path_base_name(p->top.items[i]->mod->path) : "module");
            indent = 1;
            vol_locals = rtry;
            nl();
            buf_puts(out, "size_t ar_mark = xs_ar_mark();");
            nl();
            buf_puts(out, "(void)ar_mark;");
            epush(true);
            for (int k = i; k < j; k++) {
                Stmt *t = p->top.items[k];
                cur_mod = t->mod;
                nl();
                emit_top_stmt(p, t);
            }
            emit_releases(escope);
            epop();
            buf_puts(out, "\n}\n");
            i = j - 1;
        }
        out = mo;
        indent = mindent;
        vol_locals = mvol;
    }
    emit_dbg_enter(NULL);
    epush(true);
    for (int i = 0; i < p->top.len; i++) {
        Stmt *s = p->top.items[i];
        if (top_run[i] > 0) { nl(); buf_printf(out, "xs_init_m%d();", top_run[i]); continue; }
        if (top_run[i] < 0) continue;
        cur_mod = s->mod;
        nl();
        emit_dbg_hook(s);
        emit_top_stmt(p, s);
    }
    emit_releases(escope);
    epop();
    emit_dbg_finish();
    if (wbody) { /* main: the setup once, then the program, as any thread */
        /* the body's own top-level locals: each thread releases its own here (main cannot see them) */
        for (int i = 0; i < p->top.len; i++) {
            Stmt *s = p->top.items[i];
            if (s->kind == S_VAR && crc(s->type) && ns_has(&main_locals, s->cname)) { nl(); buf_printf(out, "if (xs_rc_counting()) xs_release(%s);", s->cname); }
        }
        indent--;
        nl();
        buf_puts(out, "}\nint main(int argc, char **argv) {");
        indent++;
        nl();
        buf_puts(out, "xs_init(argc, argv);");
        nl();
        buf_puts(out, "xs_init_lits();");
        nl();
        buf_puts(out, "xs_wt_body = xs_prog_body;");
        nl();
        buf_puts(out, "xs_prog_body();");
    }
    /* the globals are released only when someone counts (XISTO_RC_STATS): otherwise the process ends and the
       system takes the memory back at once, as Node and Bun do (freeing millions of objects first costs time) */
    nl();
    buf_puts(out, "xs_before_exit(); /* background tasks finish while the globals they use are alive */");
    nl();
    buf_puts(out, "if (xs_rc_counting()) {");
    for (int i = 0; i < p->top.len; i++) {
        Stmt *s = p->top.items[i];
        if (s->kind == S_VAR && crc(s->type) && !(wbody && ns_has(&main_locals, s->cname))) {
            nl();
            buf_printf(out, "    xs_release(%s);", s->cname);
        }
    }
    nl();
    buf_puts(out, "}");
    nl();
    buf_puts(out, "xs_exit();");
    nl();
    buf_puts(out, "return 0;");
    indent--;
    nl();
    buf_puts(out, "}\n");
    if (modfns.len) buf_insert(main_buf, main_at, modfns.data);
    free(modfns.data);
    out = main_out;
    if (xs_any_used) emit_dyn_tables(p); /* any: every class's fields by name (before the descriptors that point to them) */
    /* class free/print prototypes are needed by JSON parsers that allocate objects */
    for (int i = 0; i < p->nclasses; i++)
        if (p->classes[i]->used && !p->classes[i]->builtin) {
            Class *c = p->classes[i];
            bool cy = class_cyclic(c);
            buf_printf(out, "static void %s_free(void *p);\nstatic void %s_print(void *p);\n", c->cname, c->cname);
            if (cy) buf_printf(out, "static void %s_visit(void *p, xs_visit_cb cb, void *arg);\n", c->cname);
            if (c->region) {
                bool holds = false, rrefs = false;
                for (int k = 0; k < c->nfields; k++) {
                    if (!cref(c->fields[k].type)) continue;
                    if (region_ref(c->fields[k].type, c->region)) rrefs = true; else holds = true;
                }
                if (holds) buf_printf(out, "static void %s_drop(void *p);\n", c->cname);
                if (rrefs) buf_printf(out, "static void %s_rref(void *p);\n", c->cname);
                buf_printf(out, "static const xs_type XS_TYPE_SEC %s_type = {%s_free, %s_print, NULL, %s, %s%s};\n", c->cname, c->cname, c->cname,
                           holds ? xs_sprintf("%s_drop", c->cname) : "NULL", rrefs ? xs_sprintf("%s_rref", c->cname) : "NULL", dyn_ref(c));
            } else if (c->rdual) {
                buf_printf(out, "static void %s_rref(void *p);\n", c->cname);
                buf_printf(out, "static const xs_type XS_TYPE_SEC %s_type = {%s_free, %s_print, NULL, NULL, %s_rref%s};\n", c->cname, c->cname, c->cname, c->cname, dyn_ref(c));
            } else
            buf_printf(out, "static const xs_type XS_TYPE_SEC %s_type = {%s_free, %s_print, %s%s};\n", c->cname, c->cname, c->cname, cy ? xs_sprintf("%s_visit", c->cname) : "NULL", dyn_ref(c));
        }
    {
        /* literal declarations and their initializer, for the literals the emitted code references */
        Buf lits = {0};
        Buf *saved = out;
        out = &lits;
        for (int i = 0; i < p->nlits; i++) if (lit_used[i]) buf_printf(out, "static xs_str *%s;\n", lit_cname(i));
        buf_puts(out, "XS_RUN_ONCE static void xs_init_lits(void) {");
        for (int i = 0; i < p->nlits; i++) {
            if (!lit_used[i]) continue;
            buf_printf(out, "\n    %s = xs_str_immortal(", lit_cname(i));
            emit_c_string(p->lits[i]);
            buf_printf(out, ", %zu);", xs_lit_len(p->lits[i]));
        }
        buf_puts(out, "\n}\n");
        out = saved;
        /* splice at lits_at */
        Buf joined = {0};
        buf_printf(&joined, "%.*s", (int)lits_at, out->data);
        buf_puts(&joined, lits.data);
        buf_puts(&joined, out->data + lits_at);
        *out = joined;
    }
    if (cell_fns.data) { buf_puts(out, cell_fns.data); cell_fns.len = 0; cell_fns.data[0] = 0; ncell = 0; }
    if (json_unknown_used) {
        /* every class JSON can write, for the values an unknown holds */
        int ncls = 0;
        Class **cls = xs_alloc(sizeof(Class *) * (size_t)(p->nclasses + 1));
        for (int i = 0; i < p->nclasses; i++) {
            Class *c = p->classes[i];
            if (!c->used || c->builtin || c->is_tuple) continue;
            Type *t = ty_object_of(c);
            if (!json_capable(t)) continue;
            ensure_json(t);
            cls[ncls++] = c;
        }
        Buf u = {0};
        buf_puts(&u, "static void xs_json_unknown(xs_sb *sb, void *v) {\n"
                     "    if (!v || xs_type_of(v)->print == xs_print_closure) { xs_sb_put(sb, \"null\", 4); return; } /* (a function: null, as in an array) */\n"
                     "    void (*f_)(void *) = xs_type_of(v)->free;\n"
                     "    if (f_ == xs_str_free) { xs_sb_str(sb, (xs_str *)v); return; }\n"
                     "    if (f_ == xs_box_int_free) { xs_sb_int(sb, ((xs_box_i *)v)->v); return; }\n"
                     "    if (f_ == xs_box_num_free) { xs_sb_num(sb, ((xs_box_d *)v)->v); return; }\n"
                     "    if (f_ == xs_box_bool_free) { xs_sb_bool(sb, ((xs_box_i *)v)->v != 0); return; }\n"
                     "#define XS_JU_ARR_(T, put) if (f_ == T##_free) { T *a_ = v; xs_sb_put(sb, \"[\", 1); for (size_t i_ = 0; i_ < a_->len; i_++) { if (i_) xs_sb_put(sb, \",\", 1); put(sb, a_->data[i_]); } xs_sb_put(sb, \"]\", 1); return; }\n"
                     "    XS_JU_ARR_(xs_arr_num, xs_sb_num)\n"
                     "    XS_JU_ARR_(xs_arr_int, xs_sb_int)\n"
                     "    XS_JU_ARR_(xs_arr_bool, xs_sb_bool)\n"
                     "    XS_JU_ARR_(xs_arr_ref, xs_json_unknown)\n"
                     "#undef XS_JU_ARR_\n");
        /* a Record<string, V> (an object made as an any, or a literal where an unknown goes) */
            buf_puts(&u, "    if (f_ == xs_map_free && ((xs_map *)v)->str_keys) {\n"
                         "        xs_map *m_ = v; xs_map_entry *e_; bool first_ = true; xs_sb_put(sb, \"{\", 1);\n"
                         "        XS_MAP_EACH(m_, e_) {\n"
                         "            if (m_->val_kind > 2 && e_->val.p && xs_type_of(e_->val.p)->print == xs_print_closure) continue; /* (a function: left out, as JSON.stringify does) */\n"
                         "            if (!first_) xs_sb_put(sb, \",\", 1); first_ = false; xs_sb_str(sb, e_->skey); xs_sb_put(sb, \":\", 1);\n"
                         "            if (m_->val_kind == 0) xs_sb_num(sb, e_->val.d); else if (m_->val_kind == 1) xs_sb_int(sb, e_->val.i);\n"
                         "            else if (m_->val_kind == 2) xs_sb_bool(sb, e_->val.i != 0); else xs_json_unknown(sb, e_->val.p);\n"
                         "        }\n"
                         "        xs_sb_put(sb, \"}\", 1); return;\n"
                         "    }\n"
                         "    if (f_ == xs_Date_free) { xs_sb_date(sb, v); return; }\n");
        for (int i = 0; i < ncls; i++)
            buf_printf(&u, "    if (f_ == %s_free) { json_put_%s(sb, (%s *)v); return; }\n", cls[i]->cname, type_tag(ty_object_of(cls[i])), cls[i]->cname);
        buf_puts(&u, "    xs_panic(\"TypeError: this value cannot be written as JSON\");\n}\n");
        buf_puts(out, "static void xs_json_unknown(xs_sb *sb, void *v);\n");
        if (json_protos.data) buf_puts(out, json_protos.data);
        if (json_fns.data) buf_puts(out, json_fns.data);
        buf_puts(out, u.data);
        free(u.data);
        json_unknown_used = false;
    } else if (json_fns.data) {
        if (json_protos.data) buf_puts(out, json_protos.data);
        buf_puts(out, json_fns.data);
    }
    if (isa_fns.data) { buf_puts(out, isa_fns.data); isa_fns.len = 0; isa_fns.data[0] = 0; nisa = 0; nasif = 0; }
    if (disp_fns.data) { buf_puts(out, disp_fns.data); disp_fns.len = 0; disp_fns.data[0] = 0; ndisp = 0; }
    if (xs_debug) {
        if (dbg_decls.data) buf_puts(out, dbg_decls.data);
        if (dbg_fns.data) buf_puts(out, dbg_fns.data);
        dbg_decls.len = dbg_fns.len = 0;
        if (dbg_decls.data) dbg_decls.data[0] = 0;
        if (dbg_fns.data) dbg_fns.data[0] = 0;
        ndbg_made = 0;
    }
    if (tramp_fns.data) buf_puts(out, tramp_fns.data);
    if (clone_fns.data) buf_puts(out, clone_fns.data);
    if (arrow_fns.data) buf_puts(out, arrow_fns.data);
    if (gen_fns.data) { buf_puts(out, gen_fns.data); gen_fns.len = 0; gen_fns.data[0] = 0; }
    buf_puts(out, body.data ? body.data : "");
    if (jam_used) { /* the jam versions of the callbacks (XB-083) */
        buf_printf(out, "\nstatic void *xs_jam8_of(void *fn) {\n%s    (void)fn;\n    return NULL;\n}\n", jam_tab.data ? jam_tab.data : "");
    }
    jam_used = false;
    jam_tab.len = 0;
    if (jam_tab.data) jam_tab.data[0] = 0;
    /* call sites for stack traces: read only when an error is reported */
    if (!nsites) buf_puts(&site_tab, "    {0}");
    buf_printf(out, "\n__attribute__((used)) const xs_site xs_sitetab[] = {\n%s\n};\n", site_tab.data);
    if (xs_debug) {
        buf_printf(out, "\nconst xs_dbg_point xs_dbg_points[] = {\n%s\n};\nconst int xs_dbg_npoints = %d;\n", ndbg ? dbg_tab.data : "    {\"\", 0, 0}", ndbg);
        dbg_tab.len = 0;
        ndbg = 0;
    }
}
