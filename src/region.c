/* Regions: objects that cannot outlive a loop iteration, freed in one step when it ends.

   A loop (for / while) gets a region for a set R of classes when the compiler can prove that no object of
   those classes made in one iteration is reachable after it. Then they are allocated from an arena, carry no
   reference-count work that matters (their free is a no-op), are never cycle suspects, and the arena is
   rewound at the start of the next iteration: a tree with parent pointers, a doubly linked list, a graph
   built and dropped per iteration cost no collector at all.

   The proof is conservative, by types, over the loop body B and the functions it calls (its extent):
   - every `new C` in the program is inside B, or in a function only ever called from B (or from such
     functions): no C exists outside an iteration;
   - no class outside R has a field whose type mentions C, and B and its extent read no variable declared
     outside B (globals included) whose type mentions C: nothing that outlives the iteration can hold one;
   - the extent has no await, yield, closures, function values, `unknown`, unions, JSON values, casts, and B
     no return: no other way out; R classes have no base, no subclasses, no decorators;
   - built-in calls handed a C are container methods (array, Map, Set: the container obeys the same rules)
   or console.log.
   XISTO_REGION_CHECK=1 checks every iteration at run time (each object's count must come back to zero once
   the region's own references are taken away) and stops with a message if one escaped. */
#include "xisto.h"

#include <string.h>

/* ---- code nodes: the top level, each function and method, and each class's field initializers ---- */
typedef struct {
    Func *fn;          /* NULL: top level (index 0) or a class's field initializers (cls) */
    Class *cls;
    int *calls; int ncalls, ccap;      /* callee nodes */
    Class **allocs; int nallocs, acap; /* classes made here (new C) */
    bool bad;          /* await, yield, closure, function value, unknown... */
    bool has_loop;     /* has a loop of its own (round 1 of region_compute looks at loops whose extent has none) */
} RNode;

static Program *rp;
static RNode *nodes;
static int nnodes, cnodes;

static int node_of_func(Func *f) {
    for (int i = 1; i < nnodes; i++) if (nodes[i].fn == f) return i;
    return -1;
}
static int node_of_init(Class *c) {
    for (int i = 1; i < nnodes; i++) if (!nodes[i].fn && nodes[i].cls == c) return i;
    return -1;
}
static int add_node(Func *fn, Class *cls) {
    if (nnodes == cnodes) { cnodes = cnodes ? cnodes * 2 : 256; nodes = realloc(nodes, sizeof(RNode) * (size_t)cnodes); }
    nodes[nnodes] = (RNode){.fn = fn, .cls = cls};
    return nnodes++;
}
static void push_call(RNode *n, int to) {
    if (to < 0) return;
    for (int i = 0; i < n->ncalls; i++) if (n->calls[i] == to) return;
    if (n->ncalls == n->ccap) { n->ccap = n->ccap ? n->ccap * 2 : 8; n->calls = realloc(n->calls, sizeof(int) * (size_t)n->ccap); }
    n->calls[n->ncalls++] = to;
}
static void push_alloc(RNode *n, Class *c) {
    for (int i = 0; i < n->nallocs; i++) if (n->allocs[i] == c) return;
    if (n->nallocs == n->acap) { n->acap = n->acap ? n->acap * 2 : 8; n->allocs = realloc(n->allocs, sizeof(Class *) * (size_t)n->acap); }
    n->allocs[n->nallocs++] = c;
}

static bool bad_type(Type *t) {
    if (!t) return false;
    switch (t->kind) {
    case TY_UNKNOWN: case TY_UNION: case TY_JSON: case TY_FUNC: case TY_STREAM: case TY_CHAN: case TY_TPARAM: return true;
    case TY_ARRAY: case TY_NULLABLE: case TY_SET: return bad_type(t->elem);
    case TY_MAP: return bad_type(t->key) || bad_type(t->elem);
    default: return false;
    }
}

/* ---- walking: what a piece of code calls and makes, and whether it has a way out ---- */
static Stmt *skip_stmt; /* the loop being examined: its body is attributed to it, not to the enclosing code */
static void walk_stmt(RNode *n, Stmt *s);
static void walk_func(RNode *n, Func *f);
static void walk_expr(RNode *n, Expr *e) {
    if (!e) return;
    if (bad_type(e->type)) n->bad = true;
    switch (e->kind) {
    case E_ARROW: /* a closure: bad inside a region, and what it calls or makes counts for the code around it */
        n->bad = true;
        if (e->arrow && e->arrow->fn) walk_func(n, e->arrow->fn);
        break;
    case E_AS:
        if (e->op != TK_NOT) n->bad = true; /* x! (not null, checked at run time) changes no type but the null */
        break;
    case E_AWAIT: case E_FUNC_REF: case E_UPCAST: case E_SUPER_CALL: case E_OBJLIT: n->bad = true; break;
    case E_NEW_OBJ:
        if (e->cls) {
            push_alloc(n, e->cls);
            push_call(n, node_of_init(e->cls));
            if (e->cls->ctor) push_call(n, node_of_func(e->cls->ctor));
        }
        break;
    case E_CALL:
        if (e->func) push_call(n, node_of_func(e->func));
        else if (!e->builtin.cfn && e->lhs && e->lhs->type && e->lhs->type->kind == TY_FUNC) n->bad = true;
        break;
    case E_MEMBER:
        if (e->func) {
            push_call(n, node_of_func(e->func));
            if (e->virt) /* dispatched at run time: every method of that name may run */
                for (int i = 0; i < rp->nclasses; i++)
                    for (int k = 0; k < rp->classes[i]->nmethods; k++)
                        if (strcmp(rp->classes[i]->methods[k]->name, e->func->name) == 0) push_call(n, node_of_func(rp->classes[i]->methods[k]));
        }
        break;
    default: break;
    }
    walk_expr(n, e->lhs);
    walk_expr(n, e->rhs);
    for (int i = 0; i < e->args.len; i++) walk_expr(n, e->args.items[i]);
}
static void walk_stmt(RNode *n, Stmt *s) {
    if (!s || s == skip_stmt) return;
    if (s->kind == S_YIELD) n->bad = true;
    if (s->kind == S_FOR || s->kind == S_WHILE || s->kind == S_FOROF) n->has_loop = true;
    if (s->kind == S_VAR && bad_type(s->type)) n->bad = true;
    walk_expr(n, s->init);
    walk_expr(n, s->expr);
    walk_expr(n, s->cond);
    walk_expr(n, s->for_update);
    walk_stmt(n, s->for_init);
    walk_stmt(n, s->body);
    walk_stmt(n, s->else_body);
    for (int i = 0; i < s->stmts.len; i++) walk_stmt(n, s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) { walk_expr(n, s->cases[i].value); walk_stmt(n, s->cases[i].body); }
}
static void walk_func(RNode *n, Func *f) {
    if (f->is_async) n->bad = true;
    for (int i = 0; i < f->nparams; i++) {
        if (bad_type(f->params[i].type)) n->bad = true;
        walk_expr(n, f->params[i].dflt);
    }
    if (bad_type(f->ret)) n->bad = true;
    walk_stmt(n, f->body);
}
static void build_node(int i) {
    RNode *n = &nodes[i];
    n->ncalls = n->nallocs = 0;
    n->bad = false;
    n->has_loop = false;
    if (i == 0) {
        for (int k = 0; k < rp->top.len; k++) walk_stmt(n, rp->top.items[k]);
    } else if (n->fn) {
        walk_func(n, n->fn);
    } else {
        for (int k = 0; k < n->cls->nfields; k++) walk_expr(n, n->cls->fields[k].init);
    }
}

/* ---- types that mention a class ---- */
static bool mentions(Type *t, bool *inR) {
    if (!t) return false;
    switch (t->kind) {
    case TY_OBJECT: {
        for (int i = 0; i < rp->nclasses; i++) if (inR[i] && rp->classes[i] == t->cls) return true;
        return false;
    }
    case TY_ARRAY: case TY_NULLABLE: case TY_SET: return mentions(t->elem, inR);
    case TY_MAP: return mentions(t->key, inR) || mentions(t->elem, inR);
    default: return false;
    }
}
static void unmark_mentioned(Type *t, bool *inR) {
    if (!t) return;
    switch (t->kind) {
    case TY_OBJECT: for (int i = 0; i < rp->nclasses; i++) if (rp->classes[i] == t->cls) inR[i] = false; return;
    case TY_ARRAY: case TY_NULLABLE: case TY_SET: unmark_mentioned(t->elem, inR); return;
    case TY_MAP: unmark_mentioned(t->key, inR); unmark_mentioned(t->elem, inR); return;
    default: return;
    }
}

/* ---- names declared in a piece of code (locals and parameters): every other name is from outside ---- */
typedef struct { const char **v; int n, cap; } Names;
static void names_add(Names *ns, const char *c) {
    if (!c) return;
    if (ns->n == ns->cap) { ns->cap = ns->cap ? ns->cap * 2 : 64; ns->v = realloc(ns->v, sizeof(char *) * (size_t)ns->cap); }
    ns->v[ns->n++] = c;
}
static bool names_has(Names *ns, const char *c) {
    for (int i = 0; i < ns->n; i++) if (strcmp(ns->v[i], c) == 0) return true;
    return false;
}
static void decl_stmt(Names *ns, Stmt *s) {
    if (!s) return;
    if (s->kind == S_VAR || s->kind == S_FOROF || s->kind == S_TRY) names_add(ns, s->cname);
    decl_stmt(ns, s->for_init);
    decl_stmt(ns, s->body);
    decl_stmt(ns, s->else_body);
    for (int i = 0; i < s->stmts.len; i++) decl_stmt(ns, s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) decl_stmt(ns, s->cases[i].body);
}
/* names from outside (and built-in calls handed an R value): the classes they mention leave R */
static bool *cur_inR;
static Names *cur_names;
static bool changed;
static void outside_expr(Expr *e) {
    if (!e) return;
    if (e->kind == E_IDENT && e->cname && !names_has(cur_names, e->cname) && mentions(e->type, cur_inR)) {
        unmark_mentioned(e->type, cur_inR);
        changed = true;
    }
    bool builtin_call = (e->kind == E_CALL && !e->func) || (e->kind == E_MEMBER && e->is_call && !e->func);
    if (builtin_call) {
        bool container = e->kind == E_MEMBER && e->lhs && e->lhs->type &&
                         (e->lhs->type->kind == TY_ARRAY || e->lhs->type->kind == TY_MAP || e->lhs->type->kind == TY_SET);
        bool print = e->kind == E_MEMBER && e->lhs && e->lhs->kind == E_IDENT && e->lhs->str && strcmp(e->lhs->str, "console") == 0;
        if (!container && !print) {
            for (int i = 0; i < e->args.len; i++)
                if (mentions(e->args.items[i]->type, cur_inR)) { unmark_mentioned(e->args.items[i]->type, cur_inR); changed = true; }
            if (e->lhs && mentions(e->lhs->type, cur_inR)) { unmark_mentioned(e->lhs->type, cur_inR); changed = true; }
        }
    }
    outside_expr(e->lhs);
    outside_expr(e->rhs);
    for (int i = 0; i < e->args.len; i++) outside_expr(e->args.items[i]);
}
static void outside_stmt(Stmt *s) {
    if (!s) return;
    outside_expr(s->init);
    outside_expr(s->expr);
    outside_expr(s->cond);
    outside_expr(s->for_update);
    outside_stmt(s->for_init);
    outside_stmt(s->body);
    outside_stmt(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) outside_stmt(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) { outside_expr(s->cases[i].value); outside_stmt(s->cases[i].body); }
}
static bool has_return(Stmt *s) {
    if (!s) return false;
    if (s->kind == S_RETURN) return true;
    if (has_return(s->body) || has_return(s->else_body) || has_return(s->for_init)) return true;
    for (int i = 0; i < s->stmts.len; i++) if (has_return(s->stmts.items[i])) return true;
    for (int i = 0; i < s->ncases; i++) if (has_return(s->cases[i].body)) return true;
    return false;
}
static bool class_casted(Class *c); /* an `as` anywhere in the program names it */
static bool fn_valued(Func *f);

int region_count;
bool region_allow_dual;

/* the regions given, to undo the inner one of two that would nest (a region loop reached from another's
   iterations: rewinding the inner one would take what the outer one still holds) */
typedef struct { Stmt *L; int home, id; bool *ext; Expr **arrs; int narrs, carrs; } Acc;
static Acc *accs;
static int naccs, caccs;
static int nfixed; /* the code nodes (the loops' own nodes come after) */

/* the array literals of region objects made during an iteration: they live in the region too */
static void mark_arrays_stmt(Stmt *s, Acc *a, bool *inR);
static void mark_arrays_expr(Expr *e, Acc *a, bool *inR) {
    if (!e) return;
    if (e->kind == E_ARRAY_LIT && e->type && e->type->kind == TY_ARRAY) {
        Type *et = e->type->elem && e->type->elem->kind == TY_NULLABLE ? e->type->elem->elem : e->type->elem;
        bool r = false;
        for (int i = 0; et && et->kind == TY_OBJECT && i < rp->nclasses; i++) if (inR[i] && rp->classes[i] == et->cls) r = true;
        if (r) {
            e->region = a->id;
            if (a->narrs == a->carrs) { a->carrs = a->carrs ? a->carrs * 2 : 16; a->arrs = realloc(a->arrs, sizeof(Expr *) * (size_t)a->carrs); }
            a->arrs[a->narrs++] = e;
        }
    }
    mark_arrays_expr(e->lhs, a, inR);
    mark_arrays_expr(e->rhs, a, inR);
    for (int i = 0; i < e->args.len; i++) mark_arrays_expr(e->args.items[i], a, inR);
}
static void mark_arrays_stmt(Stmt *s, Acc *a, bool *inR) {
    if (!s) return;
    mark_arrays_expr(s->init, a, inR);
    mark_arrays_expr(s->expr, a, inR);
    mark_arrays_expr(s->cond, a, inR);
    mark_arrays_expr(s->for_update, a, inR);
    mark_arrays_stmt(s->for_init, a, inR);
    mark_arrays_stmt(s->body, a, inR);
    mark_arrays_stmt(s->else_body, a, inR);
    for (int i = 0; i < s->stmts.len; i++) mark_arrays_stmt(s->stmts.items[i], a, inR);
    for (int i = 0; i < s->ncases; i++) { mark_arrays_expr(s->cases[i].value, a, inR); mark_arrays_stmt(s->cases[i].body, a, inR); }
}

/* 1: only loops whose extent runs no other loop; 2: the rest. A class goes to the innermost loop that can free it
   per iteration: an outer loop would keep every iteration of the inner one until its own iteration ends (the
   binary trees of a whole depth instead of one tree) */
static int loop_round;

/* the loop L inside the code of node `home` (0: the top level; its function otherwise) */
static void try_loop(Stmt *L, int home) {
    /* B: the body, as a node of its own; the home node without it */
    int b = add_node(NULL, NULL);
    nodes[b].cls = NULL;
    Stmt *saved = skip_stmt;
    skip_stmt = NULL;
    walk_stmt(&nodes[b], L->body);
    walk_expr(&nodes[b], L->cond);
    walk_expr(&nodes[b], L->for_update);
    skip_stmt = L;
    build_node(home);
    skip_stmt = saved;
    walk_stmt(&nodes[home], L->for_init); /* runs once, before the region: what it makes is outside */
    if (L->kind == S_FOROF) walk_expr(&nodes[home], L->expr); /* the array iterated: made once, outside */
    bool ok = !nodes[b].bad && !(home != 0 && has_return(L->body));
    char why[256] = "";
    if (nodes[b].bad) snprintf(why, sizeof why, "the body has await, yield, a closure, a function value, a cast or a dynamic type");
    else if (!ok) snprintf(why, sizeof why, "the body returns");
    /* the extent: B and everything it calls */
    bool *ext = calloc((size_t)nnodes, sizeof(bool));
    int *stack = malloc(sizeof(int) * (size_t)nnodes), sp = 0;
    ext[b] = true;
    stack[sp++] = b;
    while (sp) {
        int k = stack[--sp];
        for (int i = 0; i < nodes[k].ncalls; i++) {
            int c = nodes[k].calls[i];
            if (!ext[c]) { ext[c] = true; stack[sp++] = c; }
        }
    }
    if (ext[home] && ok) { ok = false; snprintf(why, sizeof why, "the body reaches the code the loop is in"); }
    bool later = false; /* round 1: a loop that runs other loops waits, so the innermost ones choose first */
    for (int k = 0; k < nnodes && ok && loop_round == 1; k++) if (ext[k] && nodes[k].has_loop) { ok = false; later = true; }
    for (int k = 0; k < nnodes && ok; k++)
        if (ext[k] && nodes[k].bad) {
            ok = false;
            snprintf(why, sizeof why, "%s has await, yield, a closure, a function value, a cast or a dynamic type",
                     nodes[k].fn ? nodes[k].fn->name : nodes[k].cls ? nodes[k].cls->name : "the top level");
        }
    /* exclusive: the extent's nodes called only from B or from other exclusive nodes */
    bool *excl = calloc((size_t)nnodes, sizeof(bool));
    for (int k = 0; k < nnodes; k++) excl[k] = ext[k] && k != b;
    for (bool again = true; again && ok;) {
        again = false;
        for (int k = 0; k < nnodes; k++) {
            if (!excl[k]) continue;
            if (nodes[k].fn && fn_valued(nodes[k].fn)) { excl[k] = false; again = true; continue; } /* used as a value */
            for (int j = 0; j < nnodes; j++) {
                if (j == b || excl[j]) continue;
                bool calls = false;
                for (int i = 0; i < nodes[j].ncalls; i++) if (nodes[j].calls[i] == k) calls = true;
                if (calls) { excl[k] = false; again = true; break; }
            }
        }
    }
    /* R: classes made in the extent whose every `new` is in B or an exclusive node; or, dual, also made outside the
       loop or by code that runs outside it too (their objects are then on the heap: nothing outside reads one
       made inside, and nothing inside one made outside, by the same rules below) */
    int nc = rp->nclasses;
    bool *inR = calloc((size_t)(nc ? nc : 1), sizeof(bool));
    bool *dual = calloc((size_t)(nc ? nc : 1), sizeof(bool));
    if (ok) {
        for (int k = 0; k < nnodes; k++) {
            if (!ext[k]) continue;
            for (int a = 0; a < nodes[k].nallocs; a++) {
                Class *c = nodes[k].allocs[a];
                for (int i = 0; i < nc; i++) if (rp->classes[i] == c) inR[i] = true;
            }
        }
        for (int i = 0; i < nc; i++) {
            if (!inR[i]) continue;
            Class *c = rp->classes[i];
            if (c->builtin || c->is_interface || c->data_only || c->base || c->nsubs || c->decos.len || c->generic_of || c->ntparams ||
                c->region || c->rdual || class_casted(c)) {
                inR[i] = false;
                snprintf(why, sizeof why, "%s is a data, generic, derived, decorated or cast class, or already in a region", c->name);
                continue;
            }
            for (int k = 0; k < nnodes && inR[i]; k++) {
                if (k == b || excl[k]) continue;
                for (int a = 0; a < nodes[k].nallocs; a++)
                    if (nodes[k].allocs[a] == c) {
                        if (region_allow_dual) { dual[i] = true; break; }
                        inR[i] = false;
                        snprintf(why, sizeof why, "new %s also runs outside the loop (in %s)", c->name,
                                 nodes[k].fn ? nodes[k].fn->name : nodes[k].cls ? nodes[k].cls->name : "the top level");
                        break;
                    }
            }
        }
        /* fixpoint: holders outside R, and names from outside the extent */
        for (changed = true; changed;) {
            changed = false;
            for (int i = 0; i < nc; i++) {
                if (inR[i]) continue;
                Class *k = rp->classes[i];
                for (int f = 0; f < k->nfields; f++)
                    if (mentions(k->fields[f].type, inR)) { unmark_mentioned(k->fields[f].type, inR); changed = true; }
            }
            cur_inR = inR;
            for (int k = 0; k < nnodes; k++) {
                if (!ext[k]) continue;
                Names ns = {0};
                if (k == b) decl_stmt(&ns, L->body);
                else if (nodes[k].fn) {
                    for (int i = 0; i < nodes[k].fn->nparams; i++) names_add(&ns, nodes[k].fn->params[i].cname);
                    decl_stmt(&ns, nodes[k].fn->body);
                }
                cur_names = &ns;
                if (k == b) { outside_stmt(L->body); outside_expr(L->cond); outside_expr(L->for_update); }
                else if (nodes[k].fn) outside_stmt(nodes[k].fn->body);
                else for (int f = 0; f < nodes[k].cls->nfields; f++) outside_expr(nodes[k].cls->fields[f].init);
                free(ns.v);
            }
        }
    }
    /* a dual region (any of its classes dual: then all are, counted as on the heap) keeps its classes simple:
       fields of numbers, booleans, enums and objects of the region's classes only, so a rewind has nothing to
       release (no drop) and a count reaching zero has nothing to do */
    bool any_dual = false;
    for (int i = 0; i < nc; i++) if (inR[i] && dual[i]) any_dual = true;
    for (int i = 0; i < nc && any_dual; i++) {
        if (!inR[i]) continue;
        Class *c = rp->classes[i];
        for (int f = 0; f < c->nfields && any_dual; f++) {
            Type *t = c->fields[f].type;
            if (t && t->kind == TY_NULLABLE) t = t->elem;
            bool fine = t && (t->kind == TY_INT || t->kind == TY_NUMBER || t->kind == TY_BOOLEAN || t->kind == TY_ENUM);
            for (int j = 0; !fine && t && t->kind == TY_OBJECT && j < nc; j++) if (inR[j] && rp->classes[j] == t->cls) fine = true;
            if (!fine) {
                any_dual = false;
                snprintf(why, sizeof why, "%s is made outside the loop too, and its field %s is not a number, boolean or an object of the loop's classes",
                         c->name, c->fields[f].name);
            }
        }
    }
    if (!any_dual) for (int i = 0; i < nc; i++) if (dual[i]) inR[i] = false;
    int count = 0;
    for (int i = 0; i < nc; i++) count += inR[i];
    if ((!ok || !count) && loop_round == 2 && getenv("XISTO_REGION_WHY")) {
        Module *m = L->mod ? L->mod : home && nodes[home].fn ? nodes[home].fn->mod : NULL;
        fprintf(stderr, "xisto: no region at %s:%d: %s\n", m ? m->file : "?", L->pos.line,
                why[0] ? why : "no class made in the loop stays inside it");
    }
    if (ok && count) {
        int id = ++region_count;
        L->region = id;
        for (int i = 0; i < nc; i++) if (inR[i]) { if (any_dual) rp->classes[i]->rdual = id; else rp->classes[i]->region = id; }
        if (naccs == caccs) { caccs = caccs ? caccs * 2 : 8; accs = realloc(accs, sizeof(Acc) * (size_t)caccs); }
        Acc *ac = &accs[naccs++];
        *ac = (Acc){.L = L, .home = home, .id = id, .ext = calloc((size_t)nfixed, sizeof(bool))};
        for (int k = 0; k < nfixed; k++) ac->ext[k] = ext[k];
        /* the arrays made in the iteration: in the body, and in the code only it runs */
        mark_arrays_stmt(L->body, ac, inR);
        mark_arrays_expr(L->cond, ac, inR);
        mark_arrays_expr(L->for_update, ac, inR);
        for (int k = 0; k < nfixed; k++) {
            if (!excl[k]) continue;
            if (nodes[k].fn) {
                for (int i = 0; i < nodes[k].fn->nparams; i++) mark_arrays_expr(nodes[k].fn->params[i].dflt, ac, inR);
                mark_arrays_stmt(nodes[k].fn->body, ac, inR);
            } else if (nodes[k].cls) for (int f = 0; f < nodes[k].cls->nfields; f++) mark_arrays_expr(nodes[k].cls->fields[f].init, ac, inR);
        }
    }
    free(inR);
    free(dual);
    free(ext);
    free(excl);
    free(stack);
    nnodes = b; /* B's node is no longer needed */
    build_node(home); /* back to the whole home code */
}

/* the loops of a piece of code, outermost first; a loop that gets a region is not looked into */
static void find_loops(Stmt *s, int home) {
    if (!s) return;
    if (((s->kind == S_FOR || s->kind == S_WHILE) || (s->kind == S_FOROF && s->expr && s->expr->type && s->expr->type->kind == TY_ARRAY)) && s->body) {
        try_loop(s, home); /* for...of over an array too: the array is made before, each element comes from outside */
        if (s->region) return;
    }
    find_loops(s->for_init, home);
    find_loops(s->body, home);
    find_loops(s->else_body, home);
    for (int i = 0; i < s->stmts.len; i++) find_loops(s->stmts.items[i], home);
    for (int i = 0; i < s->ncases; i++) find_loops(s->cases[i].body, home);
}

static bool *casted;
static Func **valued; /* functions used as values: callable from anywhere */
static int nvalued, cvalued;
static bool fn_valued(Func *f) {
    for (int i = 0; i < nvalued; i++) if (valued[i] == f) return true;
    return false;
}
static void cast_expr(Expr *e) {
    if (!e) return;
    if (e->kind == E_FUNC_REF && e->func && !fn_valued(e->func)) {
        if (nvalued == cvalued) { cvalued = cvalued ? cvalued * 2 : 16; valued = realloc(valued, sizeof(Func *) * (size_t)cvalued); }
        valued[nvalued++] = e->func;
    }
    if (e->kind == E_AS && e->op != TK_NOT) { /* (x! is no cast: the same class, without null) */
        Type *t = e->elem_type ? e->elem_type : e->type;
        for (int i = 0; i < rp->nclasses; i++) {
            bool one[rp->nclasses > 0 ? rp->nclasses : 1];
            memset(one, 0, sizeof one);
            one[i] = true;
            if (mentions(t, one) || mentions(e->type, one)) casted[i] = true;
        }
    }
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn) {
        Stmt *b = e->arrow->fn->body;
        for (int i = 0; b && i < b->stmts.len; i++) { Stmt *st = b->stmts.items[i]; cast_expr(st->expr); cast_expr(st->init); }
    }
    cast_expr(e->lhs);
    cast_expr(e->rhs);
    for (int i = 0; i < e->args.len; i++) cast_expr(e->args.items[i]);
}
static void cast_stmt(Stmt *s) {
    if (!s) return;
    cast_expr(s->init); cast_expr(s->expr); cast_expr(s->cond); cast_expr(s->for_update);
    cast_stmt(s->for_init); cast_stmt(s->body); cast_stmt(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) cast_stmt(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) { cast_expr(s->cases[i].value); cast_stmt(s->cases[i].body); }
}
static bool class_casted(Class *c) {
    for (int i = 0; i < rp->nclasses; i++) if (rp->classes[i] == c) return casted[i];
    return true;
}

void region_compute(Program *p) {
    rp = p;
    region_count = 0;
    if (getenv("XISTO_NO_REGIONS")) return;
    nnodes = 0;
    add_node(NULL, NULL); /* 0: the top level */
    for (int i = 0; i < p->nfuncs; i++) if (p->funcs[i]->body && !p->funcs[i]->is_native) add_node(p->funcs[i], NULL);
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (c->builtin) continue;
        add_node(NULL, c);
        if (c->ctor && c->ctor->body && node_of_func(c->ctor) < 0) add_node(c->ctor, c);
        for (int k = 0; k < c->nmethods; k++)
            if (c->methods[k]->body && c->methods[k]->cls == c && node_of_func(c->methods[k]) < 0) add_node(c->methods[k], c);
    }
    casted = calloc((size_t)(p->nclasses ? p->nclasses : 1), sizeof(bool));
    nvalued = 0;
    for (int i = 0; i < p->top.len; i++) cast_stmt(p->top.items[i]);
    for (int i = 0; i < nnodes; i++) if (nodes[i].fn) cast_stmt(nodes[i].fn->body);
    for (int i = 0; i < p->nclasses; i++) for (int f = 0; f < p->classes[i]->nfields; f++) cast_expr(p->classes[i]->fields[f].init);
    skip_stmt = NULL;
    for (int i = 0; i < nnodes; i++) build_node(i);
    int fixed = nnodes;
    nfixed = fixed;
    naccs = 0;
    for (loop_round = 1; loop_round <= 2; loop_round++) {
        for (int i = 0; i < p->top.len; i++) find_loops(p->top.items[i], 0);
        for (int i = 1; i < fixed; i++) if (nodes[i].fn && nodes[i].fn->used) find_loops(nodes[i].fn->body, i);
    }
    /* a region whose loop runs inside another region's iterations: undone */
    bool *drop = calloc((size_t)(naccs ? naccs : 1), sizeof(bool));
    for (int a = 0; a < naccs; a++)
        for (int b = 0; b < naccs; b++)
            if (a != b && accs[b].home < nfixed && accs[a].ext[accs[b].home]) drop[b] = true;
    for (int b = 0; b < naccs; b++) {
        Acc *ac = &accs[b];
        if (drop[b]) {
            ac->L->region = 0;
            for (int i = 0; i < p->nclasses; i++) {
                if (p->classes[i]->region == ac->id) p->classes[i]->region = 0;
                if (p->classes[i]->rdual == ac->id) p->classes[i]->rdual = 0;
            }
            for (int i = 0; i < ac->narrs; i++) ac->arrs[i]->region = 0;
        } else if (getenv("XISTO_REGION_WHY")) {
            Module *m = ac->L->mod ? ac->L->mod : ac->home && nodes[ac->home].fn ? nodes[ac->home].fn->mod : NULL;
            fprintf(stderr, "xisto: region %d at %s:%d for", ac->id, m ? m->file : "?", ac->L->pos.line);
            for (int i = 0; i < p->nclasses; i++) {
                if (p->classes[i]->region == ac->id) fprintf(stderr, " %s", p->classes[i]->name);
                if (p->classes[i]->rdual == ac->id) fprintf(stderr, " %s (dual: also on the heap outside the loop)", p->classes[i]->name);
            }
            fprintf(stderr, "\n");
        }
        free(ac->ext);
        free(ac->arrs);
    }
    free(drop);
}
