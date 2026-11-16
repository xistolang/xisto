/* Reachability: starting from the top-level statements, mark every function, method and class the
   program can reach. Everything else is dead and is not compiled, in either backend. The manifesto
   calls this "every dependency is weight": importing a module with 200 functions and using one must
   cost the same as writing that one function. */
#include "xisto.h"

static Program *prog;
static Func **work;
static int nwork, cap_work;

static void push_func(Func *f) {
    if (!f || f->used) return;
    f->used = true;
    for (int i = 0; i < f->noverrides; i++) push_func(f->overrides[i]); /* a call may land on any of them */
    if (nwork == cap_work) {
        cap_work = cap_work ? cap_work * 2 : 64;
        work = realloc(work, sizeof(Func *) * (size_t)cap_work);
    }
    work[nwork++] = f;
}

static Class **asif_reached; /* interfaces some `unknown as I` reaches (XB-126) */
static int nasif_n;
static void walk_expr(Expr *e);
static void walk_stmt(Stmt *s);
static void use_type(Type *t);

/* XB-232: which classes can have objects (made), for the TLS decision: a method reached only through its base's
   dispatch (an interface's other implementation) cannot run when no object of its class exists. Made: new C, an
   object literal of C, and what decoding, casts and the runtime's calls give (their result type, its fields too). */
static void made_type(Type *t);
static void made_class(Class *c) {
    if (!c || c->made || c->builtin) return;
    c->made = true;
    for (int i = 0; i < c->nfields; i++) made_type(c->fields[i].type);
}
static void made_type(Type *t) {
    if (!t) return;
    switch (t->kind) {
    case TY_OBJECT: made_class(t->cls); break;
    case TY_ARRAY: case TY_SET: case TY_NULLABLE: made_type(t->elem); break;
    case TY_MAP: made_type(t->key); made_type(t->elem); break;
    case TY_UNION: for (int i = 0; i < t->nparams; i++) made_type(t->params[i]); break;
    default: break;
    }
}
static bool made_or_sub(Class *c, int depth) {
    if (!c || c->made || depth > 64) return c != NULL;
    for (int i = 0; i < c->nsubs; i++) if (made_or_sub(c->subs[i], depth + 1)) return true;
    return false;
}

static void use_class(Class *c) {
    if (!c || c->used || c->builtin) return;
    c->used = true;
    use_class(c->base); /* its fields, initializer and methods are part of this class */
    if (c->ctor) push_func(c->ctor);
    for (int i = 0; i < c->nfields; i++) {
        use_type(c->fields[i].type); /* a field's class is laid out inside this one's struct and JSON code */
        if (c->fields[i].init) walk_expr(c->fields[i].init);
    }
    /* JSON parsing can build any used class without its constructor; printing needs nothing extra */
    for (int i = 0; i < c->nmethods; i++) /* toJSON(): string, which JSON.stringify calls (bson's ObjectId) */
        if (strcmp(c->methods[i]->name, "toJSON") == 0 && c->methods[i]->nparams == 0 && c->methods[i]->ret && c->methods[i]->ret->kind == TY_STRING)
            push_func(c->methods[i]);
}

/* a built-in class whose struct lives in runtime/xisto_net.h (as checker.c's net_class): a field, parameter or
   value of that type needs the struct declared, so the network runtime, even when no member of it is used
   (a class with an `HttpRequest | null` field in a program that runs no server) */
static bool net_struct(const Class *c) {
    static const char *const names[] = {"Response", "ReadableStream", "HttpRequest", "HttpResponse", "Server", "Worker", "MessagePort"};
    if (!c || !c->builtin) return false;
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) if (strcmp(c->name, names[i]) == 0) return true;
    return false;
}
static void use_type(Type *t) {
    if (!t) return;
    switch (t->kind) {
    case TY_OBJECT: if (net_struct(t->cls)) prog->uses_net = true; use_class(t->cls); break;
    case TY_ARRAY: case TY_SET: case TY_NULLABLE: use_type(t->elem); break;
    case TY_MAP: use_type(t->key); use_type(t->elem); break;
    case TY_FUNC: for (int i = 0; i < t->nparams; i++) use_type(t->params[i]); use_type(t->ret); break;
    case TY_UNION: for (int i = 0; i < t->nparams; i++) use_type(t->params[i]); break;
    default: break;
    }
}

static void walk_expr(Expr *e) {
    if (!e) return;
    use_type(e->type);
    if (e->kind == E_NEW_OBJ || e->kind == E_OBJLIT) made_class(e->cls);
    if (e->kind == E_AS || ((e->kind == E_CALL || e->kind == E_MEMBER) && (e->builtin.cfn || (e->func && e->func->is_native)))) {
        made_type(e->type);
        made_type(e->elem_type);
    }
    switch (e->kind) {
    case E_CALL:
        if (e->builtin.cfn && strncmp(e->builtin.cfn, "__reflect_", 10) == 0) use_type(e->elem_type); /* decoder/encoder of T */
        if (e->builtin.cfn && strcmp(e->builtin.cfn, "__iface_as") == 0) { /* unknown as I (XB-126): its adapters, below */
            Type *t = e->type->kind == TY_NULLABLE ? e->type->elem : e->type;
            if (!t->cls->used) use_class(t->cls);
            asif_reached = realloc(asif_reached, sizeof(Class *) * (size_t)(nasif_n + 1));
            asif_reached[nasif_n++] = t->cls;
        }
        push_func(e->func);
        walk_expr(e->lhs);
        break;
    case E_MEMBER:
        if (e->func && !e->func->mod) { /* method of a class (built-ins have no body) */ }
        if (e->func && e->func->body) push_func(e->func);
        if (e->cls) use_class(e->cls);
        walk_expr(e->lhs);
        break;
    case E_FUNC_REF:
        push_func(e->func);
        break;
    case E_NEW_OBJ:
        use_class(e->cls);
        break;
    case E_OBJLIT: /* (a literal that became a union member's class: its type may read as the union now) */
        use_class(e->cls);
        break;
    case E_ARROW:
        walk_stmt(e->arrow->fn->body);
        for (int i = 0; i < e->arrow->ncaptures; i++) use_type(e->arrow->captures[i].type);
        break;
    default:
        walk_expr(e->lhs);
        walk_expr(e->rhs);
        break;
    }
    for (int i = 0; i < e->args.len; i++) walk_expr(e->args.items[i]);
}

static void walk_stmt(Stmt *s) {
    if (!s) return;
    use_type(s->type);
    walk_expr(s->init);
    walk_expr(s->expr);
    walk_expr(s->cond);
    walk_stmt(s->for_init);
    walk_expr(s->for_update);
    walk_stmt(s->body);
    walk_stmt(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) walk_stmt(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) {
        walk_expr(s->cases[i].value);
        walk_stmt(s->cases[i].body);
    }
}

static bool listed_func(Program *p, Func *f) {
    for (int i = 0; i < p->nfuncs; i++) if (p->funcs[i] == f) return true;
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (c->ctor == f) return true;
        for (int k = 0; k < c->nmethods; k++) if (c->methods[k] == f) return true;
        for (int k = 0; k < c->nstatics; k++) if (c->statics[k] == f) return true;
    }
    return false;
}

void prune_program(Program *p) {
    prog = p;
    /* the checker marks generic instances and anonymous literals used when it makes them: their fields'
       classes must be reached too (Wrapped<Article> lays out an Article) */
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (c->used && !c->builtin)
            for (int k = 0; k < c->nfields; k++) use_type(c->fields[k].type);
    }
    for (int i = 0; i < p->top.len; i++) walk_stmt(p->top.items[i]);
    for (bool more = true; more;) {
        while (nwork > 0) {
            Func *f = work[--nwork];
            if (f->cls) use_class(f->cls);
            for (int i = 0; i < f->nparams; i++) use_type(f->params[i].type);
            use_type(f->ret);
            if (f->body) walk_stmt(f->body);
        }
        /* an `unknown as I` wraps an object of a class that has an adapter for I: that adapter is needed when the
           class is (its objects exist), not otherwise; a used adapter can make more classes used: until none */
        more = false;
        for (int i = 0; i < nasif_n; i++)
            for (int k = 0; k < asif_reached[i]->nadapters; k++) {
                Class *a = asif_reached[i]->adapters[k];
                if (!a->used && a->adapter_of->used) { use_class(a); made_class(a); more = true; }
            }
    }
    /* the network runtime (XB-250): needed when a function with such code is reachable; a function the program does
       not list (an arrow outside any function, a generic instance) counts as reachable */
    for (int i = 0; i < p->nnet_fns && !p->uses_net; i++) {
        Func *f = p->net_fns[i];
        if (f->used || !listed_func(p, f)) p->uses_net = true;
    }
    for (int i = 0; i < p->ngen_fns && !p->uses_gen; i++) {
        Func *f = p->gen_fns[i];
        if (f->used || !listed_func(p, f)) p->uses_gen = true;
    }
    /* TLS (XB-232): a fetch that may be https counts when its function is reachable, and, for a method or a
       constructor, when an object of its class (or of a subclass) can exist */
    for (int i = 0; i < p->ntls_fns && !p->uses_tls; i++) {
        Func *f = p->tls_fns[i];
        if (!f->used) continue;
        if (f->cls && !f->static_of && !f->cls->builtin && !made_or_sub(f->cls, 0)) continue;
        p->uses_tls = true;
    }
}
