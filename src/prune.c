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

static void use_type(Type *t) {
    if (!t) return;
    switch (t->kind) {
    case TY_OBJECT: use_class(t->cls); break;
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
                if (!a->used && a->adapter_of->used) { use_class(a); more = true; }
            }
    }
}
