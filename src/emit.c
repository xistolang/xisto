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
}
static bool inlined_once(Func *f) {
    if (getenv("XISTO_NO_TRACE")) return false;
    if (f->ncalls != 1 || !f->call1 || f->is_async || !f->body || stmt_has_try(f->body) || f->virt_target || f->is_gen) return false;
    /* not recursive, directly or through other functions inlined into their only caller */
    for (Func *g = f->call1_in; g; g = g->ncalls == 1 ? g->call1_in : NULL) if (g == f) return false;
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
    bool ok = !getenv("XISTO_NO_TRACE") && f->body && !f->is_ctor && !f->is_async && !f->virt_target && !f->is_gen && f->ncalls >= 2 && func_fails(f)
              && !stmt_has_try(f->body) && body_nodes(f->body) <= 80 && !calls_failing_user(f->body);
    f->clone_state = ok ? 1 : 2;
    return ok;
}
static void emit_func_body(Func *f);
static void emit_signature(Func *f);
static char *emit_clone(Func *f, int csite);
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
static bool nn_expr(Expr *e);
static int byte_hoist_find(Expr *a);
static bool bce_safe(Expr *e); /* an index the loop already checks (see bce_enter) */
/* u8reg: byte arrays a loop keeps in vector registers (see u8reg_loop) */
typedef struct { int kind; Expr *a, *src, *lo, *hi, *ca, *cb; Stmt *loop; } U8Idiom; /* kind 1 copy a[j] = src[j], 2 reverse, 3 shift a[j] = a[j+1], 4 trade (a buf, src the table, ca/cb the cursors) */
static bool match_u8_idiom(Stmt *s, U8Idiom *m);
static bool u8_is_index_on(Expr *e, const char *c);
static bool same_simple(Expr *a, Expr *b);
static bool bce_pure(Expr *e);
static bool stmt_mentions_name(Stmt *s, const char *c);
typedef struct { const char *cname; int64_t n; } U8Reg;
static U8Reg u8regs[8];
static int nu8regs;
static U8Reg *u8reg_find(Expr *e) {
    if (!e || e->kind != E_IDENT || !e->cname) return NULL;
    for (int k = 0; k < nu8regs; k++) if (strcmp(u8regs[k].cname, e->cname) == 0) return &u8regs[k];
    return NULL;
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
    case TY_ARRAY: return t->elem->kind == TY_NUMBER ? "xs_arr_num *" : int_like(t->elem) ? "xs_arr_int *" : t->elem->kind == TY_BOOLEAN ? "xs_arr_bool *" : "xs_arr_ref *";
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
static Buf tramp_fns;     /* trampolines turning top-level functions into closure values */
static int ntramps;
static char **tramp_names;

static const char *arr_suffix(Type *arr) { return arr->elem->kind == TY_NUMBER ? "num" : int_like(arr->elem) ? "int" : arr->elem->kind == TY_BOOLEAN ? "bool" : "ref"; }
static bool arr_is_ref(Type *arr) { return cref(arr->elem); }
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
static const char *arr_struct(Type *arr) { return arr_is_ref(arr) ? "xs_arr_ref" : arr->elem->kind == TY_NUMBER ? "xs_arr_num" : int_like(arr->elem) ? "xs_arr_int" : "xs_arr_bool"; }
static const char *elem_printer(Type *elem) {
    if (elem->kind == TY_NULLABLE) /* (T | null)[]: null prints as null */
        return elem->elem->kind == TY_STRING || (elem->elem->kind == TY_ENUM && elem->elem->en->is_string) ? "xs_print_str_quoted_or_null_" : "xs_print_obj_or_null";
    switch (elem->kind) {
    case TY_STRING: return "xs_print_str_quoted_";
    case TY_ENUM: return elem->en->is_string ? "xs_print_str_quoted_" : "0";
    case TY_OBJECT: return "xs_print_obj";
    case TY_ARRAY: return xs_sprintf("xs_print_%s", arr_struct(elem));
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

static bool is_fresh(Expr *e) {
    if (!rcnt(e->type)) return false;
    switch (e->kind) {
    case E_CALL: case E_NEW_OBJ: case E_ARRAY_LIT: case E_NEW_ARRAY: case E_TEMPLATE: case E_NEW_MAP: case E_ARROW: case E_OBJLIT: case E_REGEX: return true;
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
        if (e->builtin.cfn && strcmp(e->builtin.cfn, "or_once") == 0) return true; /* always owned: see emit_or_once */
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
    for (int i = 0; i < m->nparams; i++) { buf_puts(out, ", "); emit_decl(m->params[i].type, m->params[i].cname); }
    buf_puts(out, ") {\n    void (*f_)(void *) = xs_type_of(self)->free;\n");
    Buf args = {0};
    for (int i = 0; i < m->nparams; i++) buf_printf(&args, ", %s", m->params[i].cname);
    bool is_void = m->ret->kind == TY_VOID;
    for (int i = 0; i < prog->nclasses; i++) {
        Class *k = prog->classes[i];
        if (!k->used || k->builtin || k->is_abstract || !is_sub(k, m->cls)) continue;
        Func *impl = emit_find_method(k, m->name);
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
/* an operand of a bitwise operator as uint32_t (ToInt32, then its bits) */
static void emit_u32(Expr *e) {
    if (e->kind == E_BINARY && (e->op == TK_PLUS || e->op == TK_MINUS) && e->type->kind == TY_INT && !xs_debug &&
        (e->lhs->type->kind == TY_INT || e->lhs->type->kind == TY_LITINT) && (e->rhs->type->kind == TY_INT || e->rhs->type->kind == TY_LITINT)) {
        /* (a + b) >> 1, (h - k) & m: only the low 32 bits are kept, and those of the exact sum are those of the
           sum mod 2^32, so no overflow check (the check stays wherever the whole sum is used) */
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
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == 0xC0 && (unsigned char)s[1] == 0x80) { buf_puts(out, "\\000"); s++; continue; } /* NUL (xs_lit_len) */
        switch (c) {
        case '"': buf_puts(out, "\\\""); break;
        case '\\': buf_puts(out, "\\\\"); break;
        case '\n': buf_puts(out, "\\n"); break;
        case '\t': buf_puts(out, "\\t"); break;
        case '\r': buf_puts(out, "\\r"); break;
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
            buf_printf(out, arr_is_ref(e->lhs->type) ? "((%s*)&(" : "(&(", ctype(e->type));
            emit_borrowed(e->lhs);
            buf_puts(out, ")->data[");
            emit_bce_index(e->rhs);
            buf_puts(out, "])");
            return;
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
    case TY_ARRAY: return xs_sprintf("xs_print_arr_%s", arr_suffix(t));
    case TY_OBJECT: return "xs_print_obj";
    case TY_ENUM: return t->en->is_string ? "xs_print_str" : "xs_print_int";
    case TY_MAP: case TY_SET: return "xs_print_map";
    case TY_STREAM: return "xs_print_stream";
    case TY_CHAN: return "xs_print_obj";
    case TY_NULLABLE: return "xs_print_obj_or_null";
    case TY_UNION: return "xs_print_obj_or_null";
    default: return "xs_print_obj";
    }
}

static void emit_print_arg(Expr *a) {
    if (a->type->kind == TY_NULLABLE || a->type->kind == TY_NULL) {
        /* strings inside nullable print raw at top level, like Node */
        if (null_prim(a->type)) {
            Buf b = {0};
            Buf *saved = out;
            out = &b;
            emit_expr(a);
            out = saved;
            buf_printf(out, "({ %s v_ = %s; if (%s) %s(v_); else fputs(\"null\", XS_OUT); })", ctype(a->type), b.data, not_null(a->type, "v_"), printer_for(a->type->elem));
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
    case TY_ARRAY: buf_printf(out, "xs_print_arr_%s(", arr_suffix(a->type)); break;
    case TY_OBJECT: buf_puts(out, "xs_print_obj("); break;
    case TY_ENUM: buf_puts(out, a->type->en->is_string ? "xs_print_str(" : "xs_print_int("); break;
    case TY_UNION: case TY_UNKNOWN: buf_puts(out, "xs_print_obj_or_null("); break;
    default: break;
    }
    emit_borrowed(a);
    buf_puts(out, ")");
}

/* ---------- JSON: per-type serializer and parser, generated on demand ---------- */

static Buf json_fns;          /* generated helper functions, placed before the program */
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
    default: return "x";
    }
}

static void ensure_json(Type *t);
static const char *where_elem(int i) { return xs_sprintf("[%d]", i); }

static void json_emit_value(Buf *b, Type *t, const char *expr) { /* appends into xs_sb *sb */
    switch (t->kind) {
    case TY_NUMBER: buf_printf(b, "xs_sb_num(sb, %s);", expr); break;
    case TY_INT: buf_printf(b, "xs_sb_int(sb, %s);", expr); break;
    case TY_BOOLEAN: buf_printf(b, "xs_sb_bool(sb, %s);", expr); break;
    case TY_STRING: buf_printf(b, "xs_sb_str(sb, %s);", expr); break;
    case TY_JSON: buf_printf(b, "{ xs_str *j_ = %s; xs_sb_put(sb, j_->data, (size_t)j_->len); }", expr); break; /* embedded as is */
    case TY_ENUM: buf_printf(b, t->en->is_string ? "xs_sb_str(sb, %s);" : "xs_sb_int(sb, %s);", expr); break;
    case TY_OBJECT:
        if (t->cls->builtin && strcmp(t->cls->name, "Date") == 0) { buf_printf(b, "xs_sb_date(sb, %s);", expr); break; }
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
    case TY_OBJECT:
        if (t->cls->builtin && strcmp(t->cls->name, "Date") == 0) { buf_printf(b, "xs_json_date(%s, \"%s\")", jv, where); break; }
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
    if (njson == cap_json) { cap_json = cap_json ? cap_json * 2 : 8; json_types = realloc(json_types, sizeof(Type *) * (size_t)cap_json); }
    json_types[njson++] = t;
    Buf local = {0};
    Buf *b = &local;
    char *tag = type_tag(t);
    if (t->kind == TY_ARRAY) {
        const char *st = arr_struct(t);
        buf_printf(b, "static void json_put_%s(xs_sb *sb, %s *a);\nstatic %s *json_get_%s(xs_jv *v, const char *where);\n", tag, st, st, tag);
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
        buf_printf(b, "static void json_put_%s(xs_sb *sb, %s *o);\nstatic %s *json_get_%s(xs_jv *v, const char *where);\n", tag, c->cname, c->cname, tag);
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
    buf_printf(b, "static void json_put_%s(xs_sb *sb, %s *o);\nstatic %s *json_get_%s(xs_jv *v, const char *where);\n", tag, c->cname, c->cname, tag);
    buf_printf(b, "static void json_put_%s(xs_sb *sb, %s *o) {\n    xs_sb_put(sb, \"{\", 1);\n    bool first_ = true; (void)first_;\n", tag, c->cname);
    for (int i = 0; i < c->nfields; i++) {
        Field *f = &c->fields[i];
        /* optional fields that are null are left out, like undefined in JSON.stringify */
        if (f->optional) buf_printf(b, "    if (%s) {\n", not_null(f->type, xs_sprintf("o->%s", f->cname)));
        buf_printf(b, "    if (!first_) xs_sb_put(sb, \",\", 1);\n    first_ = false;\n    xs_sb_put(sb, \"\\\"%s\\\":\", %zu);\n    ", f->name, strlen(f->name) + 3);
        json_emit_value(b, f->optional ? f->type->elem : f->type, f->optional && f->type->elem->kind == TY_BOOLEAN ? xs_sprintf("(bool)o->%s", f->cname) : xs_sprintf("o->%s", f->cname));
        buf_puts(b, "\n");
        if (f->optional) buf_puts(b, "    }\n");
    }
    buf_puts(b, "    xs_sb_put(sb, \"}\", 1);\n}\n");
    /* parse: allocate the object without running the constructor, then fill every field from the JSON */
    buf_printf(b, "static %s *json_get_%s(xs_jv *v, const char *where) {\n    if (v->kind != XJ_OBJ) xs_panicf(\"JSON: '%%s' must be an object\", where);\n    %s *o = xs_obj_alloc_t(sizeof(%s), &%s_type);\n", c->cname, tag, c->cname, c->cname, c->cname);
    for (int i = 0; i < c->nfields; i++) {
        Field *f = &c->fields[i];
        buf_printf(b, "    o->%s = ", f->cname);
        if (f->optional) { /* absent or null: null */
            buf_printf(b, "({ xs_jv *fv_ = xs_json_field_opt(v, \"%s\"); fv_ && fv_->kind != XJ_NULL ? ", f->name);
            json_emit_read(b, f->type->elem, "fv_", f->name);
            buf_printf(b, " : %s; })", null_value(f->type));
        } else if (c->data_only && (f->type->kind == TY_STRING || f->type->kind == TY_INT || f->type->kind == TY_NUMBER || f->type->kind == TY_BOOLEAN || f->type->kind == TY_ARRAY)) {
            /* a DTO (filled from JSON, checked by its validators): an absent field reads as empty, as class-validator
               would then report it ("x should not be empty"), instead of failing the whole parse */
            const char *zero = f->type->kind == TY_STRING ? "xs_str_new(\"\", 0)" : f->type->kind == TY_BOOLEAN ? "false"
                             : f->type->kind == TY_ARRAY ? (arr_is_ref(f->type) ? xs_sprintf("xs_arr_ref_new(0, %s)", elem_printer(f->type->elem)) : xs_sprintf("%s_new(0, 0)", arr_struct(f->type))) : "0";
            buf_printf(b, "({ xs_jv *fv_ = xs_json_field_opt(v, \"%s\"); fv_ && fv_->kind != XJ_NULL ? ", f->name);
            json_emit_read(b, f->type, "fv_", f->name);
            buf_printf(b, " : %s; })", zero);
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
            buf_puts(out, "({ xs_sb sb_ = {0}; xs_sb *sb = &sb_; ");
            json_emit_value(out, a->type, "(ARG)");
            /* replace the placeholder with the borrowed argument */
            {
                size_t at = out->len;
                char *tail = xs_strndup(out->data, at);
                char *ph = strstr(tail, "(ARG)");
                out->len = (size_t)(ph - tail);
                out->data[out->len] = 0;
                emit_borrowed(a);
                buf_puts(out, ph + 5);
            }
            buf_puts(out, " xs_sb_finish(sb); })");
            return;
        }
        if (strcmp(fn, "json_parse") == 0) {
            /* a generated function, not inlined: whatever fails while parsing returns to one labelled call */
            static int nparse;
            int id = nparse++;
            Buf h = {0};
            buf_printf(&h, "XS_NOINLINE %s json_parse_%d(xs_str *s) { xs_jv *jv_ = xs_json_parse(s); %s r_ = ", ctype(e->type), id, ctype(e->type));
            json_emit_read(&h, e->type, "jv_", "value");
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
            emit_decl(t, xs_sprintf("la%d_", i));
            buf_puts(out, " = ");
            emit_borrowed(a);
            buf_puts(out, "; ");
        }
        for (int i = 0; i < e->args.len; i++) {
            Expr *a = e->args.items[i];
            if (i) buf_puts(out, "xs_print_sep(); ");
            Expr tmpv = *a;
            tmpv.kind = E_IDENT;
            tmpv.cname = xs_sprintf("la%d_", i);
            emit_print_arg(&tmpv);
            buf_puts(out, "; ");
        }
        buf_puts(out, "xs_print_nl(); })");
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
            buf_printf(out, "xs_arr_join_%s(", at->elem->kind == TY_STRING || (at->elem->kind == TY_ENUM && at->elem->en->is_string) || (at->elem->kind == TY_NULLABLE && at->elem->elem->kind == TY_STRING) ? "str" : arr_suffix(at));
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
    if (base->type->kind == TY_STRING && e->builtin.cfn && is_fresh(base) && !getenv("XISTO_NO_CHAIN_FREE") &&
        (!strcmp(e->builtin.cfn, "xs_str_replace_re") || !strcmp(e->builtin.cfn, "xs_str_replace_all_re") ||
         !strcmp(e->builtin.cfn, "xs_str_replace") || !strcmp(e->builtin.cfn, "xs_str_replace_all"))) {
        /* s.replace(...).replace(...): each intermediate string is released as soon as the next one is made,
           not at the end of the statement (regex-redux: a chain of five over 50 MB peaked at 273 MB on the
           Xeon, now ~100). These functions return an owned string (a retained s when nothing matched). */
        static int nchain;
        int id = nchain++;
        buf_printf(out, "({ xs_str *cr%d_ = ", id);
        emit_expr(base);
        buf_printf(out, "; xs_str *cq%d_ = %s(cr%d_", id, e->builtin.cfn, id);
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
        buf_printf(out, "%s((%s *)(", e->virt ? dispatcher(e->func) : e->func->cname, e->func->cls->cname);
        emit_borrowed(base);
        buf_puts(out, ")");
        emit_args_borrowed(&e->args, true);
        buf_puts(out, ")");
        return;
    }
    buf_printf(out, "%s(", e->cls->builtin ? e->builtin.cfn : e->virt ? dispatcher(e->func) : e->func->cname);
    emit_borrowed(base);
    emit_args_borrowed(&e->args, true);
    buf_puts(out, ")");
}

/* Promise.all / race: a task group (runtime/xisto_net.h). The result is built from the tasks' values, which
   are owned (references move into the array). */
static void emit_promise(Expr *e) {
    const char *fn = e->builtin.cfn;
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

/* C expression: `expr` (an unknown) checked and converted to t, as `expr as t` */
static char *from_unknown(Type *t, const char *expr) {
    bool nul = t->kind == TY_NULLABLE;
    Type *b = nul ? t->elem : t;
    const char *what = ty_name(t);
    switch (b->kind) {
    case TY_INT: return xs_sprintf("xs_as_int%s(%s)", nul ? "_or_null" : "", expr);
    case TY_ENUM:
        if (!b->en->is_string) return xs_sprintf("xs_as_int%s(%s)", nul ? "_or_null" : "", expr);
        return xs_sprintf("((xs_str *)xs_as_ref(%s, xs_str_free, %d, \"%s\"))", expr, nul, what);
    case TY_STRING: return xs_sprintf("((xs_str *)xs_as_ref(%s, xs_str_free, %d, \"%s\"))", expr, nul, what);
    case TY_NUMBER: return xs_sprintf("xs_as_num%s(%s)", nul ? "_or_null" : "", expr);
    case TY_BOOLEAN: return xs_sprintf("xs_as_bool%s(%s)", nul ? "_or_null" : "", expr);
    case TY_OBJECT: {
        if (in_hierarchy(b->cls)) return xs_sprintf("((%s)xs_as_isa((void *)(%s), %s, %d, \"%s\"))", ctype(b), expr, isa_fn(b->cls), nul, what);
        const char *fr = strcmp(b->cls->name, "Error") == 0 ? "xs_Error_free" : xs_sprintf("%s_free", b->cls->cname);
        return xs_sprintf("((%s)xs_as_ref(%s, %s, %d, \"%s\"))", ctype(b), expr, fr, nul, what);
    }
    case TY_ARRAY: {
        const char *fr = arr_is_ref(b) ? "xs_arr_ref_free" : xs_sprintf("%s_free", arr_struct(b));
        return xs_sprintf("((%s)xs_as_ref(%s, %s, %d, \"%s\"))", ctype(b), expr, fr, nul, what);
    }
    case TY_MAP: case TY_SET: return xs_sprintf("((xs_map *)xs_as_ref(%s, xs_map_free, %d, \"%s\"))", expr, nul, what);
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
    case TY_NUMBER: case TY_INT: case TY_BOOLEAN: case TY_STRING: case TY_ENUM: case TY_JSON: return true;
    case TY_ARRAY: case TY_NULLABLE: return json_capable(t->elem);
    case TY_OBJECT: {
        if (t->cls->builtin) return false;
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
static void emit_expr(Expr *e) {
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
    char *clone = user_call && cloned_per_site(callee) ? emit_clone(callee, id) : NULL;
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
            buf_printf(out, "({ void *ub%d_ = %s; %s uv%d_ = %s; xs_release(ub%d_); uv%d_; })", id, b.data, ctype(e->type), id, from_unknown(e->elem_type, xs_sprintf("ub%d_", id)), id, id);
            free(b.data);
            return;
        }
        if (e->op == TK_DOT) { /* a union narrowed to one member: the test already proved it */
            Type *t = e->type;
            if (t->kind == TY_NUMBER) buf_printf(out, "xs_as_num(%s)", b.data);
            else if (t->kind == TY_INT || (t->kind == TY_ENUM && !t->en->is_string)) buf_printf(out, "xs_as_int(%s)", b.data);
            else if (t->kind == TY_BOOLEAN) buf_printf(out, "xs_as_bool(%s)", b.data);
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
        buf_puts(out, from_unknown(e->elem_type, b.data));
        free(b.data);
        return;
    }
    case E_OBJLIT: {
        Class *c = e->cls;
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
        buf_puts(&arrow_fns, body.data);
        free(body.data);
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
        if (t->kind == TY_CHAN) {
            buf_printf(out, "xs_chan_new(%d, ", val_kind(t->elem));
            if (e->args.len) emit_expr(e->args.items[0]); else buf_puts(out, "0");
            buf_puts(out, ")");
            return;
        }
        bool is_map = t->kind == TY_MAP;
        Type *k = is_map ? t->key : t->elem;
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
        if (e->builtin.cfn && strcmp(e->builtin.cfn, "or_once") == 0) { /* f() || d: f() once, d only when needed; owned */
            static int nor;
            int id = nor++;
            Type *lt = e->lhs->type;
            bool ref = rcnt(e->type);
            buf_printf(out, "({ %s oo%d_ = ", ctype(lt), id);
            if (ref) emit_owned(e->lhs); else emit_expr(e->lhs);
            const char *v = xs_sprintf("oo%d_", id);
            const char *test = lt->kind == TY_STRING ? xs_sprintf("xs_str_truthy(%s)", v)
                             : lt->elem->kind == TY_STRING ? xs_sprintf("(%s && xs_str_truthy(%s))", v, v) : not_null(lt, v);
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
            if (strcmp(e->builtin.cfn, "xs_typeof") == 0) { buf_puts(out, "xs_typeof("); emit_expr(e->lhs); buf_puts(out, ")"); return; }
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
            if (in_hierarchy(c)) {
                buf_printf(out, "%s((void *)(", isa_fn(c));
                emit_expr(e->lhs);
                buf_puts(out, "))");
                return;
            }
            buf_puts(out, "xs_is_a(");
            emit_expr(e->lhs);
            buf_printf(out, ", %s)", strcmp(c->name, "Error") == 0 ? "xs_Error_free" : c->builtin ? "NULL" : xs_sprintf("%s_free", c->cname));
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
        buf_puts(out, "(");
        emit_expr(e->lhs);
        buf_printf(out, " %s ", binop(e->op));
        emit_expr(e->rhs);
        buf_puts(out, ")");
        return;
    case E_ASSIGN:
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
                if (!nc) { buf_printf(out, ", INT64_C(%lld)", (long long)rg->n); site_arg(e->pos); }
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
        if (e->builtin.cfn && (strcmp(e->builtin.cfn, "__reflect_decoder") == 0 || strcmp(e->builtin.cfn, "__reflect_encoder") == 0)) {
            emit_reflect_codec(e->elem_type, e->builtin.cfn[10] == 'd');
            return;
        }
        if (e->builtin.cfn && strcmp(e->builtin.cfn, "xs_fetch") == 0 && e->args.len == 2) {
            Expr *init = e->args.items[1];
            Expr *method = NULL, *body = NULL, *headers = NULL, *timeout = NULL;
            for (int i = 0; i < init->args.len; i++) {
                const char *k = init->keys[i];
                if (strcmp(k, "method") == 0) method = init->args.items[i];
                else if (strcmp(k, "body") == 0) body = init->args.items[i];
                else if (strcmp(k, "headers") == 0) headers = init->args.items[i];
                else timeout = init->args.items[i]->args.items[0];
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
            buf_puts(out, ")");
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
        buf_printf(out, "%s(", e->builtin.cfn ? e->builtin.cfn : rg_call_name(e->func));
        emit_args_borrowed(&e->args, false);
        buf_puts(out, ")");
        return;
    case E_INDEX:
        if (e->builtin.cfn) { /* m[i] on a RegExp match (the group's text, borrowed), b[i] on bytes */
            U8Reg *rg = strcmp(e->builtin.cfn, "xs_u8_get") == 0 ? u8reg_find(e->lhs) : NULL;
            if (rg) { /* the array is in a register for this loop (u8reg_loop) */
                bool nc = bce_safe(e);
                buf_puts(out, nc ? "xs_v16_get_nc(" : "xs_v16_get(");
                emit_u8reg_name(e->lhs);
                buf_puts(out, ", ");
                if (nc) emit_bce_index(e->rhs); else emit_expr(e->rhs);
                if (!nc) { buf_printf(out, ", INT64_C(%lld)", (long long)rg->n); site_arg(e->pos); }
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
        if (arr_is_ref(e->type)) { /* xs_arr_ref_lit(printer, n, owned...) */
            Type *et = e->type->elem && e->type->elem->kind == TY_NULLABLE ? e->type->elem->elem : e->type->elem;
            (void)et;
            if (e->region && e->args.len == 0) { /* [] of region objects, made during an iteration (region.c) */
                buf_printf(out, "xs_arr_ref_rnew(&xs_rg_%d, %s)", e->region, elem_printer(e->type->elem));
                return;
            }
            if (e->region) /* an array of region objects made during an iteration lives in the region too */
                buf_printf(out, "xs_arr_ref_rlit(&xs_rg_%d, %s, %d", e->region, elem_printer(e->type->elem), e->args.len);
            else
            buf_printf(out, "xs_arr_ref_lit(%s, %d", elem_printer(e->type->elem), e->args.len);
            for (int i = 0; i < e->args.len; i++) {
                buf_puts(out, ", ");
                emit_owned(e->args.items[i]);
            }
            buf_puts(out, ")");
            return;
        }
        buf_printf(out, "%s_lit(%d, (%s[]){", arr_struct(e->type), e->args.len,
                   e->type->elem->kind == TY_NUMBER ? "double" : int_like(e->type->elem) ? "int64_t" : "bool");
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
static int rot_expr(Expr *e, int st); /* the state after evaluating e */
static int rot_stmt(Stmt *s, int st, int *brk, int *cont);
static bool rot_idx_is_other_literal(Expr *i) { return i && i->kind == E_NUMBER && i->is_int_lit && i->ival != rot_lo; }
static int rot_expr(Expr *e, int st) {
    if (!e) return st;
    if (e->kind == E_ARROW) return (e->arrow && e->arrow->fn && stmt_mentions_name(e->arrow->fn->body, rot_x)) ? 0 : st;
    if ((e->kind == E_ASSIGN || e->kind == E_POSTFIX || e->kind == E_UNARY) && e->lhs) {
        Expr *t = e->lhs;
        if (t->kind == E_IDENT && t->cname && strcmp(t->cname, rot_x) == 0) { /* x = ... */
            if (e->kind != E_ASSIGN || e->op != TK_ASSIGN) return 0;
            st = rot_expr(e->rhs, st);
            return u8_is_index_on(e->rhs, rot_a->cname) && e->rhs->rhs->kind == E_NUMBER && e->rhs->rhs->is_int_lit && e->rhs->rhs->ival == rot_lo ? 1 : 0;
        }
        if (u8_is_index_on(t, rot_a->cname)) { /* a[i] = ... */
            st = rot_expr(t->rhs, st); st = rot_expr(e->rhs, st);
            return rot_idx_is_other_literal(t->rhs) ? st : 0;
        }
    }
    st = rot_expr(e->lhs, st); st = rot_expr(e->rhs, st);
    for (int i = 0; i < e->args.len; i++) st = rot_expr(e->args.items[i], st);
    return st;
}
static int rot_min(int a, int b) { return a < b ? a : b; }
static int rot_loop(Stmt *s, int st) {
    int head = st, exit = st;
    for (int pass = 0; pass < 2; pass++) {
        int brk = 1, cont = 1;
        int h = head;
        if (s->kind == S_FOR && pass == 0) h = rot_stmt(s->for_init, h, NULL, NULL);
        h = rot_expr(s->cond, h);
        if (s->kind == S_FOROF) h = rot_expr(s->expr, h);
        int end = rot_stmt(s->body, h, &brk, &cont);
        end = rot_expr(s->for_update, end);
        head = rot_min(rot_min(st, end), cont);
        exit = rot_min(rot_min(head, end), brk);
    }
    return exit;
}
static int rot_stmt(Stmt *s, int st, int *brk, int *cont) {
    if (!s) return st;
    if (s == rot_target) { rot_found = 1; rot_state_at = rot_min(rot_state_at, st); }
    switch (s->kind) {
    case S_VAR:
        st = rot_expr(s->init, st);
        if (s->cname && strcmp(s->cname, rot_x) == 0)
            return s->init && u8_is_index_on(s->init, rot_a->cname) && s->init->rhs->kind == E_NUMBER && s->init->rhs->is_int_lit && s->init->rhs->ival == rot_lo ? 1 : 0;
        return st;
    case S_EXPR: return rot_expr(s->expr, st);
    case S_IF: { int c = rot_expr(s->cond, st); return rot_min(rot_stmt(s->body, c, brk, cont), s->else_body ? rot_stmt(s->else_body, c, brk, cont) : c); }
    case S_BLOCK: for (int i = 0; i < s->stmts.len; i++) st = rot_stmt(s->stmts.items[i], st, brk, cont); return st;
    case S_FOR: case S_WHILE: case S_FOROF: {
        U8Idiom m; /* an idiom's range reaching lo writes it; otherwise the loop is walked */
        if (s->kind == S_FOR && match_u8_idiom(s, &m) && m.a && m.a->cname && strcmp(m.a->cname, rot_a->cname) == 0) {
            if (m.kind == 3 || m.kind == 2) return (m.lo->kind == E_NUMBER && m.lo->ival > rot_lo) ? st : 0; /* [lo'..] */
            return (m.lo->kind == E_NUMBER && m.lo->ival > rot_lo) ? st : 0;
        }
        return rot_loop(s, st);
    }
    case S_SWITCH: { int c = rot_expr(s->expr, st), r = c; for (int i = 0; i < s->ncases; i++) r = rot_min(r, rot_stmt(s->cases[i].body, c, brk, cont)); return r; }
    case S_BREAK: if (brk) *brk = rot_min(*brk, st); return st;
    case S_CONTINUE: if (cont) *cont = rot_min(*cont, st); return st;
    case S_RETURN: case S_THROW: return rot_expr(s->expr, st);
    case S_TRY: return 0;
    default: return 0;
    }
}
static bool u8_rot_ok(Stmt *target, Expr *a, Expr *lo, Expr *x) {
    if (!cur_func || !cur_func->body || !x->cname || lo->kind != E_NUMBER || !lo->is_int_lit) return false;
    rot_a = a; rot_lo = lo->ival; rot_x = x->cname; rot_target = target; rot_found = 0; rot_state_at = 1;
    int brk = 1, cont = 1;
    rot_stmt(cur_func->body, 0, &brk, &cont); /* x starts unknown */
    return rot_found && rot_state_at == 1;
}
/* `for (j = lo; j <= K; j++) a[j] = a[j + 1]; a[K + 1] = v;` with a in a register (fannkuch-redux's next
   permutation): one call, the shuffle and the lane (xs_v16_shl_put). b->stmts[*i] is the loop; on a match the
   pair is emitted and *i skips the store. */
static bool emit_u8_shl_put(Stmt *b, int *i) {
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
    emit_expr(as->rhs); buf_printf(out, ", INT64_C(%lld), XS_S(%d));", (long long)rg->n, site);
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
        emit_stmt(b->stmts.items[i]);
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
        emit_stmt(b->stmts.items[i]);
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
static bool stable_base(Expr *b) {
    while (b && b->kind == E_AS && b->op == TK_NOT && !b->elem_type) b = b->lhs;
    if (!b || !cur_func) return false;
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
    if (e->kind != E_MEMBER || e->is_call || e->field < 0 || !e->cls || e->cls->builtin || e->cls->is_interface) return false;
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
   `const v = new Float64Array(N)`, N a literal <= 16, in a function whose body only indexes v (v[i], v.length) or
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
    if (n <= 0 || n > 16) return 0;
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
   iteration, so i + 1 <= n <= INT64_MAX and the increment needs no overflow check. Without the check (a
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
static Rng rng_mk(__int128 lo, __int128 hi) {
    if (lo < INT64_MIN || hi > INT64_MAX) return rng_none();
    return (Rng){true, (int64_t)lo, (int64_t)hi};
}
static Rng rng_union(Rng a, Rng b) { if (!a.ok || !b.ok) return rng_none(); return (Rng){true, a.lo < b.lo ? a.lo : b.lo, a.hi > b.hi ? a.hi : b.hi}; }
static int rv_find(const char *c) { for (int i = 0; i < rv_n; i++) if (strcmp(rv_name[i], c) == 0) return i; return -1; }
static Rng rng_of(Expr *e) {
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
/* top-level code (main): only its own locals; a top-level variable functions also use is not one of them */
static bool rv_main_only(const char *c);
static void rv_compute(Func *owner, Stmt **stmts, int n) {
    rv_n = rv_ndefs = 0;
    rv_owner = owner;
    rv_valid = false;
    nbce_fix_l = 0;
    bce_fix_owner = owner;
    for (int i = 0; i < n; i++) bce_fix_scan(stmts[i]);
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
    for (int round = 0; round < 6; round++) {
        bool moved = false;
        for (int k = 0; k < rv_n; k++) {
            if (top[k]) { rv_rng[k] = rng_none(); continue; }
            Rng r = {false, 0, 0};
            bool first = true, ok = true;
            for (int d = 0; d < rv_ndefs && ok; d++) {
                if (strcmp(rv_defs[d].name, rv_name[k]) != 0) continue;
                Rng x;
                if (rv_defs[d].loop) { /* i from its start below the bound: [start, bound - 1] */
                    Rng b = rng_of(rv_defs[d].loop->cond->rhs);
                    x = b.ok ? (Rng){true, INT64_MIN, b.hi - 1} : rng_none();
                    /* a counter declared by the loop itself is only read inside it, where it is below the bound: its
                       start does not widen the top (j = i + 1 in `for (let j = i + 1; j < N; j++)` stays under N) */
                    Stmt *fi = rv_defs[d].loop->for_init;
                    bool own = fi && fi->kind == S_VAR && fi->cname && strcmp(fi->cname, rv_name[k]) == 0;
                    if (x.ok) { x.lo = INT64_MAX; for (int e = 0; e < rv_ndefs; e++) if (strcmp(rv_defs[e].name, rv_name[k]) == 0 && rv_defs[e].def) { Rng s0 = rng_of(rv_defs[e].def); if (!s0.ok) { x = rng_none(); break; } if (s0.lo < x.lo) x.lo = s0.lo; if (s0.hi > x.hi && !own) x.hi = s0.hi; } }
                    if (x.ok && own) { Rng s0 = rng_of(fi->init); if (!s0.ok) x = rng_none(); else if (s0.lo < x.lo) x.lo = s0.lo; }
                    if (x.ok && x.lo == INT64_MAX) x = rng_none();
                    if (x.ok && x.lo > x.hi) x.lo = x.hi; /* (a loop that never runs: nothing reads it) */
                } else x = rng_of(rv_defs[d].def);
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
        if (bce_ok(s->body, cand[k], ct[k], plain) && strcmp(cand[k], i) != 0) { bce_arr_t[*na] = ct[k]; arrs[(*na)++] = cand[k]; }
    }
    nbce_aff = 0;
    bce_cur_body = s->body;
    bce_affine(s->body, i);
    int keep = 0;
    for (int k = 0; k < nbce_aff; k++) {
        Expr *E = bce_aff[k].E;
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
static struct { const char *m; int id; } map_hoist[8];
static int nmap_hoist, map_hoist_ids;
static int byte_hoist_find(Expr *a);
static int map_hoist_find(Expr *m) {
    if (!m || m->kind != E_IDENT || !m->cname) return -1;
    for (int i = nmap_hoist - 1; i >= 0; i--) if (strcmp(map_hoist[i].m, m->cname) == 0) return map_hoist[i].id;
    return -1;
}
static bool emit_map_add(Stmt *s) {
    Expr *m, *k, *d, *sum;
    if (!map_add_match(s, &m, &k, &d, &sum)) return false;
    int site = new_site(sum->pos), hc = map_hoist_find(m);
    if (hc >= 0) buf_printf(out, "xs_map_add_hc(");
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
static int loop_hoist_enter(Stmt *s, int *nb) {
    *nb = 0;
    if (xs_debug || sm_mode || ictx || getenv("XISTO_NO_LOOP_HOIST") || !hoist_body_ok(s->body) || !hoist_expr_ok(s->cond) || !hoist_expr_ok(s->for_update)) return 0;
    const char *ms[8]; int nm = 0, pushed = 0;
    collect_idiom_maps(s->body, ms, &nm);
    for (int i = 0; i < nm && nmap_hoist < 8; i++) {
        int idiom = 0;
        int all = name_uses(s->body, ms[i], &idiom) + name_uses_e(s->cond, ms[i]) + name_uses_e(s->for_update, ms[i]);
        if (all != idiom || bce_writes(s, ms[i]) || declares(s, ms[i])) continue;
        int id = map_hoist_ids++;
        buf_printf(out, "xs_map_hc mhc%d_ = xs_map_hc_of(%s); ", id, ren(ms[i]));
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
static bool u8_name(Expr *e) { return e && e->kind == E_IDENT && e->cname && bce_bytes(e->type); }
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
    return e && e->kind == E_INDEX && e->builtin.cfn && strcmp(e->builtin.cfn, "xs_u8_get") == 0 && same_simple(e->lhs, arr) && same_simple(e->rhs, idx);
}
static bool is_sub_of(Expr *e, Expr *S, const char *i) { /* S - i */
    return e && e->kind == E_BINARY && e->op == TK_MINUS && same_simple(e->lhs, S) && e->rhs && e->rhs->kind == E_IDENT && e->rhs->cname && strcmp(e->rhs->cname, i) == 0;
}
/* A local Uint8Array(N <= 16) a loop only indexes (get, set, the idioms below) lives in a vector register for
   the loop (u8reg_setup): xs_v16_* in xisto_rt.h. One list for the nested scopes being emitted. */
static bool match_u8_idiom(Stmt *s, U8Idiom *m) {
    if (s->kind != S_FOR) return false;
    Stmt *fi = s->for_init;
    Expr *c = s->cond, *u = s->for_update;
    if (!fi || fi->kind != S_VAR || !fi->cname || !fi->init || fi->cell || !bce_pure(fi->init)) return false;
    if (!u || u->kind != E_POSTFIX || u->op != TK_PLUSPLUS || !u->lhs || u->lhs->kind != E_IDENT || !u->lhs->cname || strcmp(u->lhs->cname, fi->cname) != 0) return false;
    if (!c || c->kind != E_BINARY || (c->op != TK_LT && c->op != TK_LE) || !c->lhs || c->lhs->kind != E_IDENT || !c->lhs->cname || strcmp(c->lhs->cname, fi->cname) != 0) return false;
    const char *i = fi->cname;
    Stmt *b = s->body;
    if (!b || b->kind != S_BLOCK) return false;
    if (stmt_writes_name(b, i) || writes_name(c->rhs, i)) return false;
    Expr iv = {0}; iv.kind = E_IDENT; iv.cname = (char *)i; /* the counter, as an index */
    m->loop = s; m->lo = fi->init;
    if (b->stmts.len == 1) { /* copy, or shift */
        Expr *as = b->stmts.items[0]->kind == S_EXPR ? b->stmts.items[0]->expr : NULL;
        if (!as || as->kind != E_ASSIGN || as->op != TK_ASSIGN || !bce_pure(c->rhs)) return false;
        Expr *dst = as->lhs, *src = as->rhs;
        if (!dst || dst->kind != E_INDEX || !dst->builtin.cfn || strcmp(dst->builtin.cfn, "xs_u8_get") != 0 || !u8_name(dst->lhs) || !same_simple(dst->rhs, &iv)) return false;
        if (!src || src->kind != E_INDEX || !src->builtin.cfn || strcmp(src->builtin.cfn, "xs_u8_get") != 0 || !u8_name(src->lhs)) return false;
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
    if (match_u8_trade(c, i, b, m)) return true;
    /* reverse: cond is i < (S + 1) >> 1 */
    Expr *h = c->rhs;
    if (!h || h->kind != E_BINARY || h->op != TK_SHR || !h->rhs || h->rhs->kind != E_NUMBER || h->rhs->num != 1) return false;
    Expr *s1 = h->lhs;
    if (!s1 || s1->kind != E_BINARY || s1->op != TK_PLUS || !s1->rhs || s1->rhs->kind != E_NUMBER || s1->rhs->num != 1) return false;
    Expr *S = s1->lhs;
    if (!bce_pure(S) || S->kind != E_IDENT) return false;
    Stmt *t = b->stmts.items[0];
    if (t->kind != S_VAR || !t->is_const || !t->cname || t->cell || !t->init) return false;
    Expr *a1 = b->stmts.items[1]->kind == S_EXPR ? b->stmts.items[1]->expr : NULL, *a2 = b->stmts.items[2]->kind == S_EXPR ? b->stmts.items[2]->expr : NULL;
    if (!a1 || !a2 || a1->kind != E_ASSIGN || a2->kind != E_ASSIGN || a1->op != TK_ASSIGN || a2->op != TK_ASSIGN) return false;
    Expr *ta = t->init;
    if (!ta || ta->kind != E_INDEX || !ta->builtin.cfn || strcmp(ta->builtin.cfn, "xs_u8_get") != 0 || !u8_name(ta->lhs) || !same_simple(ta->rhs, &iv)) return false;
    Expr *a = ta->lhs;
    if (!u8_index_of(a1->lhs, a, &iv) || !a1->rhs || a1->rhs->kind != E_INDEX || !same_simple(a1->rhs->lhs, a) || !is_sub_of(a1->rhs->rhs, S, i)) return false;
    if (!a2->lhs || a2->lhs->kind != E_INDEX || !same_simple(a2->lhs->lhs, a) || !is_sub_of(a2->lhs->rhs, S, i)) return false;
    if (!a2->rhs || a2->rhs->kind != E_IDENT || !a2->rhs->cname || strcmp(a2->rhs->cname, t->cname) != 0) return false;
    if (stmt_writes_name(b, S->cname) || stmt_writes_name(b, a->cname)) return false;
    m->kind = 2; m->a = a; m->src = NULL; m->hi = S;
    return true;
}
/* the loops `for (j = lo; j < hi; j++) dst[j] = src[j]`, the swap loop of a reversal, and `a[j] = a[j + 1]`
   on byte arrays become one runtime call each (xs_u8_copy_range, xs_u8_rev, xs_u8_shl), or one shuffle when
   the array is in a register. XISTO_NO_U8_IDIOM=1 keeps the loops. */
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
        if (ra && rs) { emit_u8reg_name(m.a); buf_puts(out, " = xs_v16_copy_rr("); emit_u8reg_name(m.a); buf_puts(out, ", "); emit_u8reg_name(m.src); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", "); emit_expr(m.hi); buf_printf(out, ", INT64_C(%lld), INT64_C(%lld), XS_S(%d));", (long long)ra->n, (long long)rs->n, site); }
        else if (ra) { emit_u8reg_name(m.a); buf_puts(out, " = xs_v16_copy_in("); emit_u8reg_name(m.a); buf_puts(out, ", "); emit_borrowed(m.src); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", "); emit_expr(m.hi); buf_printf(out, ", INT64_C(%lld), XS_S(%d));", (long long)ra->n, site); }
        else if (rs) { buf_puts(out, "xs_v16_copy_out("); emit_borrowed(m.a); buf_puts(out, ", "); emit_u8reg_name(m.src); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", "); emit_expr(m.hi); buf_printf(out, ", INT64_C(%lld), XS_S(%d));", (long long)rs->n, site); }
        else { buf_puts(out, "xs_u8_copy_range("); emit_borrowed(m.a); buf_puts(out, ", "); emit_borrowed(m.src); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", "); emit_expr(m.hi); buf_printf(out, ", XS_S(%d));", site); }
        return true;
    }
    if (m.kind == 2) {
        if (ra) { emit_u8reg_name(m.a); buf_puts(out, " = xs_v16_rev("); emit_u8reg_name(m.a); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", ("); emit_expr(m.hi); buf_puts(out, ") - ("); emit_expr(m.lo); buf_printf(out, "), INT64_C(%lld), XS_S(%d));", (long long)ra->n, site); }
        else { buf_puts(out, "xs_u8_rev("); emit_borrowed(m.a); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", ("); emit_expr(m.hi); buf_puts(out, ") - ("); emit_expr(m.lo); buf_printf(out, "), XS_S(%d));", site); }
        return true;
    }
    /* shift: the last j is K (<=) or K - 1 (<) */
    const char *last = s->cond->op == TK_LE ? "" : " - INT64_C(1)";
    if (ra) { emit_u8reg_name(m.a); buf_puts(out, " = xs_v16_shl("); emit_u8reg_name(m.a); buf_puts(out, ", "); emit_expr(m.lo); buf_puts(out, ", ("); emit_expr(m.hi); buf_printf(out, ")%s, INT64_C(%lld), XS_S(%d));", last, (long long)ra->n, site); }
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
static bool u8_is_index_on(Expr *e, const char *c) {
    return e && e->kind == E_INDEX && e->builtin.cfn && strcmp(e->builtin.cfn, "xs_u8_get") == 0 && e->lhs && e->lhs->kind == E_IDENT && e->lhs->cname && strcmp(e->lhs->cname, c) == 0;
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
/* The loop s: its registers are chosen, loaded before it, the loop is emitted (by emit_stmt_), the registers
   stored after. Returns false when there is nothing to promote. XISTO_NO_U8_REG=1 turns it off. */
static bool u8reg_loop(Stmt *s) {
    if (xs_debug || sm_mode || ictx || !cur_func || !cur_func->body || stmt_has_try(cur_func->body) || getenv("XISTO_NO_U8_REG") || getenv("XISTO_NO_U8_IDIOM")) return false;
    if (s->kind != S_WHILE && s->kind != S_FOR) return false;
    U8Idiom m;
    if (s->kind == S_FOR && match_u8_idiom(s, &m)) return false; /* one shuffle already */
    U8Reg cand[8]; int nc = 0;
    u8_candidates(cur_func->body, cand, &nc);
    U8Reg chosen[8]; int nch = 0;
    for (int k = 0; k < nc && nu8regs + nch < 8; k++) {
        Expr iv = {0}; iv.kind = E_IDENT; iv.cname = (char *)cand[k].cname;
        if (u8reg_find(&iv)) continue; /* an outer loop holds it already */
        if (!stmt_mentions_name(s, cand[k].cname)) continue; /* not used here */
        if (!u8_stmt_ok(cur_func->body, cand[k].cname, false)) continue; /* escapes somewhere */
        if (!u8_stmt_ok(s, cand[k].cname, true)) continue; /* a use the register cannot serve */
        chosen[nch++] = cand[k];
    }
    if (!nch) return false;
    int base = nu8regs;
    buf_puts(out, "{");
    indent++;
    for (int k = 0; k < nch; k++) {
        u8regs[nu8regs++] = chosen[k];
        nl();
        buf_printf(out, "xs_u8x16 r_%s = xs_v16_load(%s->data, INT64_C(%lld));", chosen[k].cname, ren(chosen[k].cname), (long long)chosen[k].n);
    }
    nl();
    emit_stmt_(s);
    for (int k = 0; k < nch; k++) {
        nl();
        buf_printf(out, "xs_v16_store(%s->data, r_%s, INT64_C(%lld));", ren(chosen[k].cname), chosen[k].cname, (long long)chosen[k].n);
    }
    nu8regs = base;
    indent--;
    nl();
    buf_puts(out, "}");
    return true;
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
static void emit_stmt_(Stmt *s) {
    used_ar = false;
    if ((s->kind == S_WHILE || s->kind == S_FOR) && u8reg_loop(s)) { drain_if_needed(); return; }
    switch (s->kind) {
    case S_VAR:
        emit_var(s);
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
        mark_counted_inc(s);
        {
            size_t mark = out->len;
            buf_puts(out, "{ ");
            int nb = 0, nm = loop_hoist_enter(s, &nb);
            if (nm || nb) {
                emit_stmt_for_inner(s);
                nmap_hoist -= nm;
                nbyte_hoist -= nb;
                buf_puts(out, " }");
                return;
            }
            out->len = mark; /* nothing to hold: as before */
            if (out->data) out->data[mark] = 0;
        }
        emit_stmt_for_inner(s);
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
        if (in_ctor) buf_puts(out, in_init ? "return;" : "return self;");
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
static bool spine_leaves(Expr *e, Func *f, Expr **calls, int *ncalls, int64_t *c) {
    if (!e) return false;
    if (e->kind == E_BINARY && e->op == TK_PLUS && e->type && e->type->kind == TY_INT && !e->builtin.cfn)
        return spine_leaves(e->lhs, f, calls, ncalls, c) && spine_leaves(e->rhs, f, calls, ncalls, c);
    if (e->kind == E_NUMBER && e->is_int_lit && e->num >= 0 && e->num <= 1e9) { *c += (int64_t)e->num; return true; }
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
typedef struct { int ndecls; Stmt *decls[8]; Stmt *base; Stmt *ret; Expr *calls[2]; int64_t c; int64_t k; } Spine;
static bool spine_fn(Func *f, Spine *sp) {
    if (xs_debug || xs_profile || getenv("XISTO_NO_SPINE")) return false;
    if (!f->body || f->is_async || f->is_gen || f->cls || f->nparams != 1 || !f->ret || f->ret->kind != TY_INT) return false;
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
    if (!base || base->kind != S_RETURN || !base->expr || base->expr->kind != E_NUMBER || !base->expr->is_int_lit || base->expr->num < 0) return false;
    if (s_ret->kind != S_RETURN || !s_ret->expr) return false;
    int n = 0;
    sp->c = 0;
    if (!spine_leaves(s_ret->expr, f, sp->calls, &n, &sp->c) || n != 2) return false;
    if (!spine_pure(sp->calls[0]->args.items[0]) || !spine_pure(sp->calls[1]->args.items[0])) return false;
    sp->base = s_if; sp->ret = s_ret; sp->k = (int64_t)base->expr->num;
    return true;
}
static void emit_spine(Func *f, Spine *sp) {
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
    if (sp->k) buf_printf(out, ") return xs_iadd_s(acc_, INT64_C(%lld), XS_S(%d));", (long long)sp->k, new_site(sp->ret->pos));
    else buf_puts(out, ") return acc_;");
    nl();
    buf_puts(out, "acc_ = xs_iadd_s(acc_, ");
    if (sp->c) buf_printf(out, "xs_iadd_s(INT64_C(%lld), ", (long long)sp->c);
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
static void emit_func_body(Func *f) {
    cur_func = f;
    if (f->mod) cur_mod = f->mod;
    if (f->body) rv_compute(f, &f->body, 1);
    if (f->is_gen) { emit_gen(f); cur_func = NULL; return; }
    vol_locals = stmt_has_try(f->body);
    { Spine sp_; if (spine_fn(f, &sp_)) { emit_spine(f, &sp_); cur_func = NULL; return; } }
    buf_puts(out, " {");
    indent++;
    nl();
    buf_puts(out, "size_t ar_mark = xs_ar_mark();");
    nl();
    buf_puts(out, "(void)ar_mark;");
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
    for (int i = c->nbase_fields; i < c->nfields; i++) {
        Field *fd = &c->fields[i];
        if (!fd->init) continue;
        used_ar = false;
        nl();
        buf_printf(out, "self->%s = ", fd->cname);
        emit_owned(fd->init);
        buf_puts(out, ";");
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
    buf_printf(out, ") {\n    %s *self = xs_obj_alloc_t(sizeof(%s), &%s_type);\n    %s_init(self", c->cname, c->cname, c->cname, c->cname);
    for (int i = 0; ec && i < ec->nparams; i++) buf_printf(out, ", %s", ec->params[i].cname);
    buf_puts(out, ");\n    return self;\n}\n");
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
    case TY_FUNC: case TY_UNKNOWN: case TY_STREAM: case TY_CHAN: *any = true; return;
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
    case TY_FUNC: case TY_UNKNOWN: case TY_STREAM: case TY_CHAN: *any = true; return;
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
        case TY_ARRAY: buf_printf(out, "xs_print_arr_%s(self->%s);", arr_suffix(fd->type), fd->cname); break;
        case TY_OBJECT: buf_printf(out, "xs_print_obj(self->%s);", fd->cname); break;
        case TY_ENUM: buf_printf(out, fd->type->en->is_string ? "xs_print_str_quoted(self->%s);" : "xs_print_int(self->%s);", fd->cname); break;
        case TY_NULLABLE:
            buf_printf(out, "if (!%s) fputs(\"null\", XS_OUT); else %s(self->%s);", not_null(fd->type, xs_sprintf("self->%s", fd->cname)),
                       fd->type->elem->kind == TY_STRING ? "xs_print_str_quoted" : printer_for(fd->type->elem), fd->cname);
            break;
        case TY_MAP: case TY_SET: buf_printf(out, "xs_print_map(self->%s);", fd->cname); break;
        case TY_UNION: buf_printf(out, "xs_print_any_quoted(self->%s);", fd->cname); break;
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
        const char *nz = ctor_sets_every_field(c) ? "_nz" : "";
        if (c->nohdr && c->region) buf_printf(out, "%s *self = xs_region_alloc_nh(&xs_rg_%d, sizeof(%s));", c->cname, c->region, c->cname);
        else if (c->nohdr) buf_printf(out, "%s *self = xs_rg_on_%d ? xs_region_alloc_nh(&xs_rg_%d, sizeof(%s)) : xs_nh_heap(sizeof(%s));", c->cname, c->rdual, c->rdual, c->cname, c->cname);
        else if (c->region) buf_printf(out, "%s *self = xs_region_alloc%s(&xs_rg_%d, sizeof(%s), &%s_type);", c->cname, nz, c->region, c->cname, c->cname);
        else if (c->rdual) /* region.c: in its loop's region while an iteration runs, on the heap otherwise */
            buf_printf(out, "%s *self = xs_rg_on_%d ? xs_region_alloc_dual%s(&xs_rg_%d, sizeof(%s), &%s_type) : xs_obj_alloc_t%s(sizeof(%s), &%s_type);",
                       c->cname, c->rdual, nz, c->rdual, c->cname, c->cname, nz, c->cname, c->cname);
        else buf_printf(out, "%s *self = xs_obj_alloc_t%s(sizeof(%s), &%s_type);", c->cname, nz, c->cname, c->cname);
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
    buf_puts(out, "return self;");
    indent--;
    nl();
    buf_puts(out, "}\n");

methods:
    for (int i = 0; i < c->nmethods; i++) {
        if (!c->methods[i]->used || c->methods[i]->cls != c || c->methods[i]->is_abstract) continue; /* inherited ones live in their class */
        buf_putc(out, '\n');
        emit_signature(c->methods[i]);
        emit_func_body(c->methods[i]);
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

void emit_program(Program *p, Buf *o) {
    out = o;
    prog = p;
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
            emit_signature(c->methods[k]);
            buf_puts(out, ";\n");
        }
    }
    for (int i = 0; i < p->nfuncs; i++) {
        if (p->funcs[i]->mod->builtin || p->funcs[i]->is_native || !p->funcs[i]->used) continue;
        emit_signature(p->funcs[i]);
        buf_puts(out, ";\n");
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
    }
    if (rg_hoist_on) for (int i = 0; i < p->nfuncs; i++) if (p->funcs[i]->rg_hoist > 0 && p->funcs[i]->rg_used) emit_rg_clone(p->funcs[i]);

    vol_locals = false;
    for (int i = 0; i < p->top.len; i++) if (stmt_has_try(p->top.items[i])) vol_locals = true;
    bool wbody = p->uses_workers && !xs_debug; /* (the debugger runs one thread) */
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
        if (s->kind != S_VAR || !ns_has(&main_locals, s->cname)) continue;
        nl();
        emit_decl(s->type, s->cname);
        buf_puts(out, " = 0;");
    }
    cur_func = NULL;
    rv_compute(NULL, p->top.items, p->top.len);
    emit_dbg_enter(NULL);
    epush(true);
    for (int i = 0; i < p->top.len; i++) {
        Stmt *s = p->top.items[i];
        cur_mod = s->mod;
        nl();
        emit_dbg_hook(s);
        if (s->kind == S_VAR && const_lit_global(s) && !p->uses_workers && !ns_has(&main_locals, s->cname)) {
            dbg_local(s->name, s->type, s->cname, s->is_const); /* (set where it is declared) */
        } else if (s->kind == S_VAR) {
            used_ar = false;
            buf_printf(out, "%s = ", s->cname);
            emit_owned(s->init);
            buf_puts(out, ";");
            dbg_local(s->name, s->type, s->cname, s->is_const);
            drain_if_needed();
        } else {
            emit_stmt(s);
        }
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
    out = main_out;
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
                buf_printf(out, "static const xs_type XS_TYPE_SEC %s_type = {%s_free, %s_print, NULL, %s, %s};\n", c->cname, c->cname, c->cname,
                           holds ? xs_sprintf("%s_drop", c->cname) : "NULL", rrefs ? xs_sprintf("%s_rref", c->cname) : "NULL");
            } else if (c->rdual) {
                buf_printf(out, "static void %s_rref(void *p);\n", c->cname);
                buf_printf(out, "static const xs_type XS_TYPE_SEC %s_type = {%s_free, %s_print, NULL, NULL, %s_rref};\n", c->cname, c->cname, c->cname, c->cname);
            } else
            buf_printf(out, "static const xs_type XS_TYPE_SEC %s_type = {%s_free, %s_print, %s};\n", c->cname, c->cname, c->cname, cy ? xs_sprintf("%s_visit", c->cname) : "NULL");
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
    if (json_fns.data) buf_puts(out, json_fns.data);
    if (isa_fns.data) { buf_puts(out, isa_fns.data); isa_fns.len = 0; isa_fns.data[0] = 0; nisa = 0; }
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
    /* call sites for stack traces: read only when an error is reported */
    if (!nsites) buf_puts(&site_tab, "    {0}");
    buf_printf(out, "\n__attribute__((used)) const xs_site xs_sitetab[] = {\n%s\n};\n", site_tab.data);
    if (xs_debug) {
        buf_printf(out, "\nconst xs_dbg_point xs_dbg_points[] = {\n%s\n};\nconst int xs_dbg_npoints = %d;\n", ndbg ? dbg_tab.data : "    {\"\", 0, 0}", ndbg);
        dbg_tab.len = 0;
        ndbg = 0;
    }
}
