/* xisto: parts read in place (runs before the checker)

   for (const part of readStdinParts(opts)) BODY
   where `readStdinParts` comes from xisto:io and BODY never lets `part` outlive the iteration becomes
   for (const part of readStdinPartsInPlace(opts)) BODY
   which reads the input into one array used again from its start: memory holds the largest part instead of the
   whole input (reverse-complement: 250 -> 127 MB, as the hand-written #6), and fewer pages are touched.
   `part` does not outlive the iteration when every use of it is one of:
       part[i] (read or written), part.length,
       an argument of a function imported from xisto:bytes (they read and change the bytes; none keeps them),
       an argument of writeStdout from xisto:io (it takes the bytes at the call, as Node's synchronous write).
   Anything else (assigned, returned, kept in an array, given to another function, a closure or a method of
   the array) and the loop keeps readStdinParts. The output is the same: the bytes a part has while it is in use
   are the bytes it would have had. Off with XISTO_NO_INPLACE_PARTS=1. */
#include "xisto.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static bool is_ident(Expr *e, const char *name) { return e && e->kind == E_IDENT && e->str && (!name || strcmp(e->str, name) == 0); }

/* the name is imported into the function's module from that module path */
static Import *imported(Module *m, const char *name, const char *path) {
    if (!m) return NULL;
    for (int i = 0; i < m->nimports; i++) {
        Import *im = &m->imports[i];
        if (im->is_default || !im->path || strcmp(im->path, path) != 0) continue;
        for (int k = 0; k < im->n; k++) if (strcmp(im->names[k], name) == 0) return im;
    }
    return NULL;
}

typedef struct { Module *m; const char *v; } Ctx;

static bool ok_expr(Ctx *c, Expr *e);
static bool ok_args(Ctx *c, ExprList *a) {
    for (int i = 0; i < a->len; i++) if (!ok_expr(c, a->items[i])) return false;
    return true;
}
/* every use of c->v in e is one that keeps it inside the iteration */
static bool ok_expr(Ctx *c, Expr *e) {
    if (!e) return true;
    switch (e->kind) {
    case E_IDENT: return strcmp(e->str, c->v) != 0; /* a bare use: it may go anywhere */
    case E_ARROW: return false;                       /* (a closure: not followed) */
    case E_INDEX:
        if (is_ident(e->lhs, c->v)) return ok_expr(c, e->rhs);
        return ok_expr(c, e->lhs) && ok_expr(c, e->rhs);
    case E_MEMBER:
        if (is_ident(e->lhs, c->v)) return !e->is_call && e->str && strcmp(e->str, "length") == 0;
        return ok_expr(c, e->lhs) && ok_args(c, &e->args);
    case E_ASSIGN:
        if (is_ident(e->lhs, c->v)) return false;
        return ok_expr(c, e->lhs) && ok_expr(c, e->rhs);
    case E_CALL:
        if (is_ident(e->lhs, NULL) && (imported(c->m, e->lhs->str, "xisto:bytes") ||
                                        (strcmp(e->lhs->str, "writeStdout") == 0 && imported(c->m, "writeStdout", "xisto:io")))) {
            for (int i = 0; i < e->args.len; i++)
                if (!is_ident(e->args.items[i], c->v) && !ok_expr(c, e->args.items[i])) return false;
            return true;
        }
        return ok_expr(c, e->lhs) && ok_args(c, &e->args);
    default:
        return ok_expr(c, e->lhs) && ok_expr(c, e->rhs) && ok_args(c, &e->args);
    }
}
static bool ok_stmt(Ctx *c, Stmt *s) {
    if (!s) return true;
    switch (s->kind) {
    case S_VAR: return strcmp(s->name, c->v) != 0 && ok_expr(c, s->init);
    case S_EXPR: case S_THROW: return ok_expr(c, s->expr);
    case S_RETURN: case S_YIELD: return ok_expr(c, s->expr);
    case S_IF: case S_WHILE: return ok_expr(c, s->cond) && ok_stmt(c, s->body) && ok_stmt(c, s->else_body);
    case S_FOR: return ok_stmt(c, s->for_init) && ok_expr(c, s->cond) && ok_expr(c, s->for_update) && ok_stmt(c, s->body);
    case S_FOROF: return (!s->name || strcmp(s->name, c->v) != 0) && ok_expr(c, s->expr) && ok_stmt(c, s->body);
    case S_BLOCK:
        for (int i = 0; i < s->stmts.len; i++) if (!ok_stmt(c, s->stmts.items[i])) return false;
        return true;
    case S_BREAK: case S_CONTINUE: return true;
    default: return false; /* (switch, try: not followed) */
    }
}

static void try_loop(Module *m, const char *where, Stmt *s) {
    Expr *call = s->expr;
    if (!s->name || !call || call->kind != E_CALL || !is_ident(call->lhs, "readStdinParts")) return;
    Import *im = imported(m, "readStdinParts", "xisto:io");
    if (!im) return;
    Ctx c = {m, s->name};
    if (!ok_stmt(&c, s->body)) {
        if (getenv("XISTO_INPLACE_WHY")) fprintf(stderr, "inplace: %s keeps readStdinParts: '%s' may outlive its iteration\n", where, s->name);
        return;
    }
    bool listed = false;
    for (int k = 0; k < im->n; k++) if (strcmp(im->names[k], "readStdinPartsInPlace") == 0) listed = true;
    if (!listed) { /* the module imports it too (from the same xisto:io) */
        im->names = realloc(im->names, (size_t)(im->n + 1) * sizeof *im->names);
        im->positions = realloc(im->positions, (size_t)(im->n + 1) * sizeof *im->positions);
        im->names[im->n] = "readStdinPartsInPlace";
        im->positions[im->n] = im->pos;
        im->n++;
    }
    call->lhs->str = "readStdinPartsInPlace";
    if (getenv("XISTO_INPLACE_WHY")) fprintf(stderr, "inplace: %s reads the parts of '%s' in place\n", where, s->name);
    /* writeStdout(part) as the last statement of the body: the program changes none of the part's bytes after the
       call (it does not outlive the iteration, and the iteration ends there), so the write may go out straight
       from the part (writeStdoutLent: the reader waits before it overwrites them) */
    Stmt *last = s->body;
    while (last && last->kind == S_BLOCK && last->stmts.len > 0) last = last->stmts.items[last->stmts.len - 1];
    Expr *w = last && last->kind == S_EXPR ? last->expr : NULL;
    const char *nolent = getenv("XISTO_NO_LENT_WRITE");
    if ((nolent && *nolent && strcmp(nolent, "0") != 0) || !w || w->kind != E_CALL || !is_ident(w->lhs, "writeStdout") || w->args.len != 1 ||
        !is_ident(w->args.items[0], s->name))
        return;
    Import *io = imported(m, "writeStdout", "xisto:io");
    if (!io) return;
    bool has = false;
    for (int k = 0; k < io->n; k++) if (strcmp(io->names[k], "writeStdoutLent") == 0) has = true;
    if (!has) {
        io->names = realloc(io->names, (size_t)(io->n + 1) * sizeof *io->names);
        io->positions = realloc(io->positions, (size_t)(io->n + 1) * sizeof *io->positions);
        io->names[io->n] = "writeStdoutLent";
        io->positions[io->n] = io->pos;
        io->n++;
    }
    w->lhs->str = "writeStdoutLent";
    if (getenv("XISTO_INPLACE_WHY")) fprintf(stderr, "inplace: %s writes '%s' with no copy\n", where, s->name);
}

static void walk(Module *m, const char *where, Stmt *s) {
    if (!s) return;
    if (s->kind == S_FOROF) try_loop(m, where, s);
    if (s->kind == S_BLOCK) for (int i = 0; i < s->stmts.len; i++) walk(m, where, s->stmts.items[i]);
    else if (s->kind == S_IF || s->kind == S_TRY) { walk(m, where, s->body); walk(m, where, s->else_body); }
    else if (s->kind == S_FOR || s->kind == S_WHILE || s->kind == S_FOROF) walk(m, where, s->body);
}

void inplace_program(Program *p) {
    if (getenv("XISTO_NO_INPLACE_PARTS")) return;
    for (int i = 0; i < p->nfuncs; i++) {
        Func *f = p->funcs[i];
        if (!f || !f->body || !f->mod || f->mod->builtin) continue;
        walk(f->mod, f->name, f->body);
    }
    for (int i = 0; i < p->top.len; i++) {
        Stmt *s = p->top.items[i];
        if (s && s->mod && !s->mod->builtin) walk(s->mod, "the module body", s);
    }
}
