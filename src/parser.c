#include <errno.h>
#include <ctype.h>
#include "xisto.h"

#include <limits.h>
#include <unistd.h>

typedef struct {
    Lexer lx;
    Token tok;
    Program *prog;
    Module *mod;
    const char *prev_end; /* end of the last token consumed: source spans for reflection */
    bool in_gen;          /* inside a generator body: `yield` is a statement */
    /* import { setTimeout as sleep }: in the rest of the module the alias reads as the exported name */
    char **alias_from, **alias_to;
    int nalias;
    StmtList pending;     /* declarations a destructuring adds after its own (const [a, b] = e) */
} Parser;

/* generics: the type parameters in scope while a template is parsed, or their types while an instance is */
static char **tp_names;
static int ntp;
static char **subst_names;
static Type **subst_types;
static int nsubst;

static void next(Parser *p) {
    p->prev_end = p->tok.start ? p->tok.start + p->tok.len : NULL;
    p->tok = lexer_next(&p->lx);
    if (p->nalias && p->tok.kind == TK_IDENT)
        for (int i = 0; i < p->nalias; i++)
            if (strcmp(p->tok.str, p->alias_from[i]) == 0) { p->tok.str = p->alias_to[i]; break; }
}
/* the source text from `from` to the end of the last consumed token */
static char *span(Parser *p, const char *from) { return from && p->prev_end > from ? xs_strndup(from, (size_t)(p->prev_end - from)) : xs_sprintf("%s", ""); }
static bool at(Parser *p, TokKind k) { return p->tok.kind == k; }
static bool at_ident(Parser *p, const char *name) { return at(p, TK_IDENT) && strcmp(p->tok.str, name) == 0; }

static const char *found(Parser *p) {
    return p->tok.kind == TK_EOF ? "end of file" : xs_sprintf("'%.*s'", p->tok.len, p->tok.start);
}

static Token expect(Parser *p, TokKind k, const char *help) {
    if (!at(p, k)) xs_error(p->tok.pos, help, "expected %s but found %s", tok_kind_name(k), found(p));
    Token t = p->tok;
    next(p);
    return t;
}

static bool accept(Parser *p, TokKind k) {
    if (at(p, k)) {
        next(p);
        return true;
    }
    return false;
}

static Expr *new_expr(ExprKind kind, Pos pos) {
    Expr *e = xs_alloc(sizeof *e);
    e->kind = kind;
    e->pos = pos;
    e->field = -1;
    return e;
}

static Stmt *new_stmt(StmtKind kind, Pos pos) {
    Stmt *s = xs_alloc(sizeof *s);
    s->kind = kind;
    s->pos = pos;
    return s;
}

static Expr *parse_expr(Parser *p);
static Expr *parse_assign(Parser *p);
static Stmt *parse_stmt(Parser *p);
static Type *parse_type(Parser *p);

/* Unresolved class names are stored as objects of a placeholder class; the checker resolves them. */
static Type *named_type(Parser *p, Token name) {
    Class *c = xs_alloc(sizeof *c);
    c->name = name.str;
    c->pos = name.pos;
    return ty_object_of(c);
}

static Program *parsing_prog;
static Module *parsing_mod;
static void add_class(Program *prog, Class *c);
static bool peek_is(Parser *p, TokKind k);
static bool looks_like_type_args(Parser *p);
static int parse_type_args(Parser *p, Type ***out);
static void add_statics(Program *prog, Module *m, Class *c);
/* `(` starts a function type when it is `()` or `(name:` / `(name?:` / `(name,` */
static bool looks_like_fn_type(Parser *p) {
    Parser save = *p;
    next(p);
    bool fn = at(p, TK_RPAREN);
    if (!fn && at(p, TK_IDENT)) {
        next(p);
        fn = at(p, TK_COLON) || at(p, TK_QUESTION) || at(p, TK_COMMA);
    }
    *p = save;
    return fn;
}
static Type *union_of(Type *a, Type *b);
static Type *parse_type(Parser *p) {
    Type *t;
    if (at(p, TK_LBRACKET)) { /* [A, B]: a tuple */
        next(p);
        Type **ps = NULL;
        int n = 0;
        while (!at(p, TK_RBRACKET)) {
            ps = realloc(ps, sizeof(Type *) * (size_t)(n + 1));
            ps[n++] = parse_type(p);
            if (!accept(p, TK_COMMA)) break;
        }
        expect(p, TK_RBRACKET, "a tuple type lists its element types: [string, int]");
        t = xs_alloc(sizeof *t);
        t->kind = TY_TUPLE;
        t->params = ps;
        t->nparams = n;
        while (at(p, TK_LBRACKET)) {
            next(p);
            expect(p, TK_RBRACKET, NULL);
            t = ty_array_of(t);
        }
        if (at(p, TK_PIPE)) {
            next(p);
            if (!at_ident(p, "null")) xs_error(p->tok.pos, "the only union with a tuple xisto supports is [A, B] | null", "expected 'null' after '|'");
            next(p);
            t = ty_nullable_of(t);
        }
        return t;
    }
    if (at(p, TK_STRING)) { /* a string literal type: "circle" (a string that can only be that) */
        t = xs_alloc(sizeof *t);
        t->kind = TY_STRING;
        t->tname = p->tok.str;
        next(p);
        if (accept(p, TK_PIPE)) return union_of(t, parse_type(p));
        return t;
    }
    if (at(p, TK_LPAREN) && !looks_like_fn_type(p)) { /* (T | null)[]: parentheses group a type */
        next(p);
        t = parse_type(p);
        expect(p, TK_RPAREN, NULL);
        while (at(p, TK_LBRACKET)) {
            next(p);
            expect(p, TK_RBRACKET, NULL);
            t = ty_array_of(t);
        }
        if (at(p, TK_PIPE)) {
            next(p);
            if (!at_ident(p, "null")) xs_error(p->tok.pos, "the only union xisto supports is T | null", "expected 'null' after '|'");
            next(p);
            t = ty_nullable_of(t);
        }
        return t;
    }
    if (at(p, TK_LPAREN)) { /* function type: (a: A, b: B) => R */
        next(p);
        Type **params = NULL;
        int n = 0, cap = 0;
        while (!at(p, TK_RPAREN)) {
            expect(p, TK_IDENT, NULL);
            expect(p, TK_COLON, "function types name their parameters: (req: HttpRequest) => HttpResponse");
            if (n == cap) {
                cap = cap ? cap * 2 : 4;
                params = realloc(params, sizeof(Type *) * (size_t)cap);
            }
            params[n++] = parse_type(p);
            if (!accept(p, TK_COMMA)) break;
        }
        expect(p, TK_RPAREN, NULL);
        expect(p, TK_ARROW, NULL);
        return ty_func(params, n, parse_type(p));
    }
    if (!at(p, TK_IDENT))
        xs_error(p->tok.pos, "supported types: number, int, boolean, string, void, T[], class names, (a: A) => R", "expected a type but found %s", found(p));
    Token name = p->tok;
    const char *n = name.str;
    next(p);
    if (strcmp(n, "number") == 0) t = ty_number;
    else if (strcmp(n, "int") == 0) t = ty_int;
    else if (strcmp(n, "boolean") == 0) t = ty_boolean;
    else if (strcmp(n, "string") == 0) t = ty_string;
    else if (strcmp(n, "void") == 0) t = ty_void;
    else if (strcmp(n, "object") == 0) t = ty_json;
    else if (strcmp(n, "unknown") == 0) t = ty_unknown;

    else if (strcmp(n, "any") == 0)
        xs_error(name.pos, "every value in xisto has one static type", "'any' is not supported");
    else if (strcmp(n, "Promise") == 0) { /* Promise<T> is T: async is syntax only */
        expect(p, TK_LT, "write Promise<T>");
        t = parse_type(p);
        expect(p, TK_GT, NULL);
    } else if (strcmp(n, "Generator") == 0 || strcmp(n, "Iterable") == 0 || strcmp(n, "IterableIterator") == 0 || strcmp(n, "Iterator") == 0) {
        /* what a generator returns: a Stream<T> (for...of, map, filter, take...); Generator<T, R, N>'s R, N are ignored */
        expect(p, TK_LT, xs_sprintf("write %s<T>", n));
        t = ty_stream_of(parse_type(p));
        while (accept(p, TK_COMMA)) parse_type(p);
        expect(p, TK_GT, NULL);
    } else if (strcmp(n, "Array") == 0) {
        expect(p, TK_LT, "write Array<T> or T[]");
        t = ty_array_of(parse_type(p));
        expect(p, TK_GT, NULL);
    } else if (strcmp(n, "Map") == 0) {
        expect(p, TK_LT, "write Map<K, V>");
        Type *k = parse_type(p);
        expect(p, TK_COMMA, "write Map<K, V>");
        Type *v = parse_type(p);
        expect(p, TK_GT, NULL);
        t = ty_map_of(k, v);
    } else if (strcmp(n, "Set") == 0) {
        expect(p, TK_LT, "write Set<T>");
        t = ty_set_of(parse_type(p));
        expect(p, TK_GT, NULL);
    } else if (strcmp(n, "Channel") == 0) {
        expect(p, TK_LT, "write Channel<T>");
        t = ty_chan_of(parse_type(p));
        expect(p, TK_GT, NULL);
    } else if (strcmp(n, "Stream") == 0) {
        expect(p, TK_LT, "write Stream<T>");
        t = ty_stream_of(parse_type(p));
        expect(p, TK_GT, NULL);
    } else if (strcmp(n, "null") == 0) {
        xs_error(name.pos, "write the nullable type as T | null", "'null' alone is not a type");
    } else if (strcmp(n, "undefined") == 0) {
        xs_error(name.pos, "xisto has no undefined; use T | null", "'undefined' is not a type");
    } else if (nsubst && ({ int k_ = -1; for (int i_ = 0; i_ < nsubst; i_++) if (strcmp(subst_names[i_], n) == 0) k_ = i_; k_ >= 0 ? (t = subst_types[k_], 1) : 0; })) {
        /* an instance: T is the type it was made for */
    } else if (({ bool tp_ = false; for (int i_ = 0; i_ < ntp; i_++) if (strcmp(tp_names[i_], n) == 0) tp_ = true; tp_; })) {
        t = xs_alloc(sizeof *t);
        t->kind = TY_TPARAM;
        t->tname = n;
    } else {
        t = named_type(p, name);
        if (at(p, TK_LT)) { /* Name<A, B>: a generic interface */
            next(p);
            do {
                t->targs = realloc(t->targs, sizeof(Type *) * (size_t)(t->ntargs + 1));
                t->targs[t->ntargs++] = parse_type(p);
            } while (accept(p, TK_COMMA));
            expect(p, TK_GT, NULL);
        }
    }
    while (at(p, TK_LBRACKET)) {
        next(p);
        expect(p, TK_RBRACKET, NULL);
        t = ty_array_of(t);
    }
    if (at(p, TK_PIPE)) { /* T | null, or a union A | B | ... */
        next(p);
        if (at_ident(p, "null")) {
            next(p);
            t = ty_nullable_of(t);
            if (accept(p, TK_PIPE)) t = union_of(t, parse_type(p));
        } else {
            t = union_of(t, parse_type(p));
        }
    }
    return t;
}
/* A | B: the members of both (unions flattened); null makes it nullable; one member is that member */
static Type *union_of(Type *a, Type *b) {
    Type **m = NULL;
    int n = 0;
    bool nul = false;
    Type *parts[2] = {a, b};
    for (int i = 0; i < 2; i++) {
        Type *x = parts[i];
        if (x->kind == TY_NULLABLE) { nul = true; x = x->elem; }
        if (x->kind == TY_UNION) {
            for (int k = 0; k < x->nparams; k++) { m = realloc(m, sizeof(Type *) * (size_t)(n + 1)); m[n++] = x->params[k]; }
        } else { m = realloc(m, sizeof(Type *) * (size_t)(n + 1)); m[n++] = x; }
    }
    Type *u;
    if (n == 1) u = m[0];
    else {
        u = xs_alloc(sizeof *u);
        u->kind = TY_UNION;
        u->params = m;
        u->nparams = n;
    }
    return nul ? ty_nullable_of(u) : u;
}

static void parse_args(Parser *p, ExprList *out) {
    expect(p, TK_LPAREN, NULL);
    if (!at(p, TK_RPAREN)) {
        do {
            if (at(p, TK_RPAREN)) break; /* trailing comma */
            expr_list_push(out, parse_expr(p));
        } while (accept(p, TK_COMMA));
    }
    expect(p, TK_RPAREN, NULL);
}

/* Arrow function: (a: T, b) => expr | { ... }  or  x => expr. Decided by looking ahead with a copy of the lexer. */
static bool looks_like_arrow(Parser *p) {
    Parser save = *p;
    bool ok = false;
    if (at(p, TK_IDENT)) {
        next(p);
        ok = at(p, TK_ARROW);
    } else if (at(p, TK_LPAREN)) {
        next(p);
        int depth = 1;
        while (!at(p, TK_EOF) && depth > 0) {
            if (at(p, TK_LPAREN)) depth++;
            else if (at(p, TK_RPAREN)) depth--;
            next(p);
        }
        if (at(p, TK_COLON)) { /* return type annotation */
            next(p);
            parse_type(p);
        }
        ok = at(p, TK_ARROW);
    }
    *p = save;
    return ok;
}

static Stmt *parse_block(Parser *p);
static void destructure(Parser *p, Stmt *holder, bool is_const, bool array);

/* the reads of destructured parameters open the body (once) */
static void prepend_param_decls(Func *f) {
    if (!f->param_decls.len || !f->body) return;
    StmtList b = {0};
    for (int i = 0; i < f->param_decls.len; i++) stmt_list_push(&b, f->param_decls.items[i]);
    for (int i = 0; i < f->body->stmts.len; i++) stmt_list_push(&b, f->body->stmts.items[i]);
    f->body->stmts = b;
    f->param_decls.len = 0;
}

static Expr *parse_arrow(Parser *p) {
    Pos pos = p->tok.pos;
    Expr *e = new_expr(E_ARROW, pos);
    Arrow *a = xs_alloc(sizeof *a);
    a->pos = pos;
    Func *f = xs_alloc(sizeof *f);
    f->pos = pos;
    f->name = "arrow";
    int cap = 0;
    if (at(p, TK_IDENT)) { /* x => ... */
        Token name = p->tok;
        next(p);
        f->params = xs_alloc(sizeof(Param));
        f->params[0].name = name.str;
        f->nparams = 1;
    } else {
        expect(p, TK_LPAREN, NULL);
        while (!at(p, TK_RPAREN)) {
            if (f->nparams == cap) {
                cap = cap ? cap * 2 : 4;
                f->params = realloc(f->params, sizeof(Param) * (size_t)cap);
            }
            Token name;
            if (at(p, TK_LBRACKET) || at(p, TK_LBRACE)) { /* ([k, v]) => ... / ({ id }) => ...: a hidden parameter */
                StmtList saved = p->pending;
                p->pending = (StmtList){0};
                Stmt holder = {0};
                destructure(p, &holder, true, at(p, TK_LBRACKET));
                name.str = holder.name;
                for (int i = 0; i < p->pending.len; i++) stmt_list_push(&f->param_decls, p->pending.items[i]);
                p->pending = saved;
            } else name = expect(p, TK_IDENT, NULL);
            Param prm = {.name = name.str};
            if (accept(p, TK_COLON)) prm.type = parse_type(p); /* untyped parameters take their type from the context */
            f->params[f->nparams++] = prm;
            if (!accept(p, TK_COMMA)) break;
        }
        expect(p, TK_RPAREN, NULL);
        if (accept(p, TK_COLON)) f->ret = parse_type(p);
    }
    expect(p, TK_ARROW, NULL);
    if (at(p, TK_LBRACE)) {
        f->body = parse_block(p);
    } else {
        a->expr_body = true;
        Stmt *b = new_stmt(S_BLOCK, p->tok.pos);
        Stmt *r = new_stmt(S_RETURN, p->tok.pos);
        r->expr = parse_assign(p);
        stmt_list_push(&b->stmts, r);
        f->body = b;
    }
    prepend_param_decls(f);
    a->fn = f;
    e->arrow = a;
    return e;
}

static Expr *parse_primary(Parser *p) {
    Pos pos = p->tok.pos;
    if (at(p, TK_IDENT) && strncmp(p->tok.str, "__reflect_", 10) == 0) { /* __reflect_kind(T) & co: generated code only */
        Expr *e = new_expr(E_CALL, pos);
        e->builtin.cfn = p->tok.str;
        e->str = p->tok.str;
        next(p);
        expect(p, TK_LPAREN, NULL);
        e->elem_type = parse_type(p);
        expect(p, TK_RPAREN, NULL);
        return e;
    }
    if ((at(p, TK_LPAREN) || at(p, TK_IDENT)) && looks_like_arrow(p)) return parse_arrow(p);
    if (at(p, TK_SLASH) || at(p, TK_SLASH_ASSIGN)) { /* /pattern/flags */
        Expr *e = new_expr(E_REGEX, pos);
        p->lx.pos = (size_t)(p->tok.start - p->lx.src);
        p->lx.col = pos.col;
        if (!lexer_regex(&p->lx, p->tok.start, &e->str, &e->rflags)) xs_error(pos, "close it: /pattern/flags", "unterminated regular expression");
        next(p);
        return e;
    }
    switch (p->tok.kind) {
    case TK_NUMBER: {
        if (p->tok.is_bigint) { /* 123n is BigInt(123), or BigInt("...") when it does not fit an int */
            Expr *c = new_expr(E_CALL, pos);
            Expr *f = new_expr(E_IDENT, pos);
            f->str = "BigInt";
            c->lhs = f;
            const char *d = p->tok.str;
            bool radix = d[0] == '0' && d[1];
            errno = 0;
            char *end;
            long long v = strtoll(radix && (d[1] == 'o' || d[1] == 'O') ? d + 2 : radix && (d[1] == 'b' || d[1] == 'B') ? d + 2 : d, &end,
                                  radix ? (d[1] == 'x' || d[1] == 'X' ? 16 : d[1] == 'o' || d[1] == 'O' ? 8 : 2) : 10);
            Expr *a;
            if (errno == 0 && *end == 0) {
                a = new_expr(E_NUMBER, pos);
                a->num = (double)v;
                a->ival = v;
                a->is_int_lit = true;
            } else {
                a = new_expr(E_STRING, pos);
                a->str = (char *)d;
            }
            a->big_lit = true;
            expr_list_push(&c->args, a);
            next(p);
            return c;
        }
        Expr *e = new_expr(E_NUMBER, pos);
        e->num = p->tok.num;
        e->ival = p->tok.ival;
        e->is_int_lit = p->tok.is_int_lit;
        next(p);
        return e;
    }
    case TK_STRING: {
        Expr *e = new_expr(E_STRING, pos);
        e->str = p->tok.str;
        next(p);
        return e;
    }
    case TK_TRUE:
    case TK_FALSE: {
        Expr *e = new_expr(E_BOOL, pos);
        e->boolean = p->tok.kind == TK_TRUE;
        next(p);
        return e;
    }
    case TK_THIS: {
        next(p);
        return new_expr(E_THIS, pos);
    }
    case TK_TEMPLATE: {
        Expr *e = new_expr(E_TEMPLATE, pos);
        const char *raw = p->tok.str;
        /* absolute position of the raw text start, for error messages inside ${...} */
        int line = pos.line, col = pos.col + 1;
        Buf lit = {0};
        const char *q = raw;
        while (*q) {
            if (q[0] == '$' && q[1] == '{') {
                if (lit.len || !e->args.len) {
                    Expr *se = new_expr(E_STRING, pos);
                    se->str = lit.data ? xs_strndup(lit.data, lit.len) : xs_strndup("", 0);
                    expr_list_push(&e->args, se);
                    lit.len = 0;
                    if (lit.data) lit.data[0] = 0;
                }
                q += 2; col += 2;
                const char *start_expr = q;
                int depth = 1;
                while (*q && depth) {
                    if (*q == '{') depth++;
                    else if (*q == '}') { depth--; if (!depth) break; }
                    else if (*q == '"' || *q == '\'') { char qq = *q++; while (*q && *q != qq) { if (*q == '\\') q++; q++; } }
                    q++;
                }
                size_t sn = (size_t)(q - start_expr);
                char *src = xs_alloc(sn + 4); /* (zeros after the end: the lexer looks a character or two ahead) */
                memcpy(src, start_expr, sn);
                Parser sub = {0};
                lexer_init(&sub.lx, src);
                sub.lx.line = line;
                sub.lx.col = col;
                sub.prog = p->prog;
                sub.mod = p->mod;
                next(&sub);
                if (at(&sub, TK_EOF)) xs_error((Pos){line, col}, NULL, "empty ${} in template literal");
                Expr *inner = parse_expr(&sub);
                if (!at(&sub, TK_EOF)) xs_error(sub.tok.pos, NULL, "unexpected %s inside ${}", tok_kind_name(sub.tok.kind));
                expr_list_push(&e->args, inner);
                for (const char *c = start_expr; c < q; c++) { if (*c == '\n') { line++; col = 1; } else col++; }
                q++; col++; /* closing brace */
                continue;
            }
            if (*q == '\\') {
                const char *bad = NULL;
                int n = xs_unescape(q + 1, &lit, &bad);
                if (!n) xs_error((Pos){line, col}, bad, "malformed escape sequence in template literal");
                for (int k = 0; k <= n; k++) { if (q[k] == '\n') { line++; col = 1; } else col++; }
                q += n + 1;
                continue;
            }
            if (*q == '\n') { line++; col = 1; } else col++;
            buf_putc(&lit, *q++);
        }
        if (lit.len || !e->args.len) {
            Expr *se = new_expr(E_STRING, pos);
            se->str = lit.data ? xs_strndup(lit.data, lit.len) : xs_strndup("", 0);
            expr_list_push(&e->args, se);
        }
        next(p);
        return e;
    }
    case TK_AWAIT: {
        next(p);
        Expr *e = new_expr(E_AWAIT, pos);
        e->lhs = parse_expr(p);
        return e;
    }
    case TK_ASYNC: {
        next(p);
        if (!looks_like_arrow(p)) xs_error(pos, NULL, "expected an arrow function after 'async'");
        Expr *e = parse_arrow(p);
        e->arrow->fn->is_async = true;
        return e;
    }
    case TK_IDENT: {
        if (strcmp(p->tok.str, "null") == 0) {
            next(p);
            return new_expr(E_NULL, pos);
        }
        Expr *e = new_expr(E_IDENT, pos);
        e->str = p->tok.str;
        next(p);
        Type **targs = NULL;
        int ntargs = 0;
        if (at(p, TK_LT) && looks_like_type_args(p)) ntargs = parse_type_args(p, &targs); /* f<int>(x) */
        if (at(p, TK_LPAREN)) {
            Expr *c = new_expr(E_CALL, pos);
            c->lhs = e;
            c->targs = targs;
            c->ntargs = ntargs;
            parse_args(p, &c->args);
            return c;
        }
        return e;
    }
    case TK_LPAREN: {
        next(p);
        Expr *e = parse_expr(p);
        expect(p, TK_RPAREN, NULL);
        return e;
    }
    case TK_LBRACE: { /* object literal: its type comes from the context */
        next(p);
        Expr *e = new_expr(E_OBJLIT, pos);
        int cap = 0;
        while (!at(p, TK_RBRACE)) {
            Token key;
            if (at(p, TK_IDENT) || at(p, TK_STRING)) { key = p->tok; next(p); }
            else { xs_error(p->tok.pos, "keys are names or strings: { id: 1, \"Content-Type\": \"x\" }", "expected a key in the object literal"); return NULL; }
            Expr *val;
            if (accept(p, TK_COLON)) val = parse_assign(p);
            else { /* shorthand { id } */
                if (key.kind != TK_IDENT) xs_error(key.pos, NULL, "a string key needs a value");
                val = new_expr(E_IDENT, key.pos);
                val->str = key.str;
            }
            if (e->args.len == cap) { cap = cap ? cap * 2 : 4; e->keys = realloc(e->keys, sizeof(char *) * (size_t)cap); }
            e->keys[e->args.len] = key.str;
            expr_list_push(&e->args, val);
            if (!accept(p, TK_COMMA)) break;
        }
        expect(p, TK_RBRACE, NULL);
        return e;
    }
    case TK_LBRACKET: {
        next(p);
        Expr *e = new_expr(E_ARRAY_LIT, pos);
        if (!at(p, TK_RBRACKET)) {
            do {
                if (at(p, TK_RBRACKET)) break; /* trailing comma */
                expr_list_push(&e->args, parse_expr(p));
            } while (accept(p, TK_COMMA));
        }
        expect(p, TK_RBRACKET, NULL);
        return e;
    }
    case TK_NEW: {
        next(p);
        Token name = expect(p, TK_IDENT, NULL);
        if (strcmp(name.str, "Array") == 0) {
            Expr *e = new_expr(E_NEW_ARRAY, pos);
            expect(p, TK_LT, "write the element type: new Array<number>(n).fill(0)");
            e->elem_type = parse_type(p);
            expect(p, TK_GT, NULL);
            expect(p, TK_LPAREN, NULL);
            e->lhs = parse_expr(p);
            expect(p, TK_RPAREN, NULL);
            return e;
        }
        if (strcmp(name.str, "Channel") == 0) { /* new Channel<T>(capacity?) */
            Expr *e = new_expr(E_NEW_MAP, pos);
            expect(p, TK_LT, "write new Channel<T>(capacity)");
            e->elem_type = ty_chan_of(parse_type(p));
            expect(p, TK_GT, NULL);
            parse_args(p, &e->args);
            return e;
        }
        if (strcmp(name.str, "Map") == 0 || strcmp(name.str, "Set") == 0) {
            Expr *e = new_expr(E_NEW_MAP, pos);
            bool is_map = name.str[0] == 'M';
            expect(p, TK_LT, is_map ? "write new Map<K, V>()" : "write new Set<T>()");
            Type *k = parse_type(p);
            if (is_map) {
                expect(p, TK_COMMA, "write new Map<K, V>()");
                Type *v = parse_type(p);
                e->elem_type = ty_map_of(k, v);
            } else {
                e->elem_type = ty_set_of(k);
            }
            expect(p, TK_GT, NULL);
            expect(p, TK_LPAREN, NULL);
            if (!at(p, TK_RPAREN)) { /* new Map<K, V>(entries) / new Set<T>(values): an array to start from */
                expr_list_push(&e->args, parse_expr(p));
                if (at(p, TK_COMMA)) xs_error(p->tok.pos, NULL, "new Map/Set takes at most one argument");
            }
            expect(p, TK_RPAREN, NULL);
            return e;
        }
        Expr *e = new_expr(E_NEW_OBJ, pos);
        e->str = name.str;
        parse_args(p, &e->args);
        return e;
    }
    case TK_FUNCTION:
        xs_error(pos, "declare the function at the top level and pass its name", "function expressions are not supported");
        return NULL;
    default:
        xs_error(pos, NULL, "expected an expression but found %s", found(p));
        return NULL;
    }
}

/* `<` that opens type arguments of a call (f<int, string[]>(...)), not a comparison: only type tokens up to the
   matching `>`, and `(` right after it */
static bool looks_like_type_args(Parser *p) {
    Parser save = *p;
    next(p);
    int depth = 1;
    bool ok = false;
    for (int guard = 0; guard < 200; guard++) {
        if (at(p, TK_LT)) depth++;
        else if (at(p, TK_GT)) {
            if (--depth == 0) { next(p); ok = at(p, TK_LPAREN); break; }
        } else if (!at(p, TK_IDENT) && !at(p, TK_LBRACKET) && !at(p, TK_RBRACKET) && !at(p, TK_COMMA) && !at(p, TK_PIPE)) break;
        next(p);
    }
    *p = save;
    return ok;
}
static int parse_type_args(Parser *p, Type ***out) {
    expect(p, TK_LT, NULL);
    int n = 0;
    do {
        *out = realloc(*out, sizeof(Type *) * (size_t)(n + 1));
        (*out)[n++] = parse_type(p);
    } while (accept(p, TK_COMMA));
    expect(p, TK_GT, NULL);
    return n;
}

static Expr *parse_postfix(Parser *p) {
    Expr *e = parse_primary(p);
    for (;;) {
        Pos pos = p->tok.pos;
        if (at(p, TK_LBRACKET)) {
            next(p);
            Expr *ix = new_expr(E_INDEX, pos);
            ix->lhs = e;
            ix->rhs = parse_expr(p);
            expect(p, TK_RBRACKET, NULL);
            e = ix;
        } else if (at(p, TK_DOT)) {
            next(p);
            Token name = expect(p, TK_IDENT, NULL);
            Expr *m = new_expr(E_MEMBER, name.pos);
            m->lhs = e;
            m->str = name.str;
            if (at(p, TK_LT) && looks_like_type_args(p)) m->ntargs = parse_type_args(p, &m->targs); /* obj.m<T>(x) */
            if (at(p, TK_LPAREN)) {
                m->is_call = true;
                parse_args(p, &m->args);
            }
            e = m;
        } else if (at(p, TK_NOT)) { /* x!: asserts not null (checked at run time in xisto) */
            next(p);
            Expr *nn = new_expr(E_AS, pos);
            nn->op = TK_NOT;
            nn->lhs = e;
            e = nn;
        } else if (at(p, TK_PLUSPLUS) || at(p, TK_MINUSMINUS)) {
            Expr *pf = new_expr(E_POSTFIX, pos);
            pf->op = p->tok.kind;
            pf->lhs = e;
            next(p);
            e = pf;
        } else {
            return e;
        }
    }
}

static Expr *parse_unary(Parser *p) {
    if (at_ident(p, "typeof")) { /* typeof x: "string", "number", "boolean", "object", "function" */
        Expr *e = new_expr(E_UNARY, p->tok.pos);
        e->op = TK_IDENT;
        next(p);
        e->lhs = parse_unary(p);
        return e;
    }
    if (at(p, TK_MINUS) || at(p, TK_NOT) || at(p, TK_PLUS) || at(p, TK_TILDE)) {
        Expr *e = new_expr(E_UNARY, p->tok.pos);
        e->op = p->tok.kind;
        next(p);
        e->lhs = parse_unary(p);
        return e;
    }
    if (at(p, TK_PLUSPLUS) || at(p, TK_MINUSMINUS)) { /* ++x / --x: x += 1 / x -= 1, whose value is the new one, as in JS */
        Expr *e = new_expr(E_ASSIGN, p->tok.pos);
        e->op = at(p, TK_PLUSPLUS) ? TK_PLUS_ASSIGN : TK_MINUS_ASSIGN;
        next(p);
        e->lhs = parse_unary(p);
        Expr *one = new_expr(E_NUMBER, e->pos);
        one->is_int_lit = true;
        one->ival = 1;
        one->num = 1;
        e->rhs = one;
        return e;
    }
    return parse_postfix(p);
}

static Expr *binary(TokKind op, Pos pos, Expr *l, Expr *r) {
    Expr *e = new_expr(E_BINARY, pos);
    e->op = op;
    e->lhs = l;
    e->rhs = r;
    return e;
}

#define BINARY_LEVEL(name, below, cond)                      \
    static Expr *name(Parser *p) {                           \
        Expr *e = below(p);                                  \
        while (cond) {                                       \
            TokKind op = p->tok.kind;                        \
            Pos pos = p->tok.pos;                            \
            next(p);                                         \
            e = binary(op, pos, e, below(p));                \
        }                                                    \
        return e;                                            \
    }

/* a ** b, right associative, above * / % */
static Expr *parse_pow(Parser *p) {
    Expr *e = parse_unary(p);
    if (at(p, TK_STARSTAR)) {
        if (e->kind == E_UNARY && !(p->prev_end && p->prev_end[-1] == ')')) xs_error(p->tok.pos, "write (-a) ** b or -(a ** b)", "a unary operator before ** needs parentheses");
        Pos pos = p->tok.pos;
        next(p);
        return binary(TK_STARSTAR, pos, e, parse_pow(p));
    }
    return e;
}
BINARY_LEVEL(parse_mul, parse_pow, at(p, TK_STAR) || at(p, TK_SLASH) || at(p, TK_PERCENT))
BINARY_LEVEL(parse_add, parse_mul, at(p, TK_PLUS) || at(p, TK_MINUS))
/* '>' '>' ['>'] with nothing in between, where an operator is expected: >> or >>> (and >>= / >>>= when the
   last one is '>='). Returns the operator and consumes the tokens, or 0 (nothing consumed). */
static TokKind take_shift_right(Parser *p, bool want_assign) {
    if (!at(p, TK_GT)) return 0;
    Lexer save = p->lx;
    Token t0 = p->tok;
    const char *end = t0.start + t0.len;
    next(p);
    if ((at(p, TK_GT) || at(p, TK_GE)) && p->tok.start == end) {
        bool ge = at(p, TK_GE);
        const char *end2 = p->tok.start + p->tok.len;
        if (ge) {
            if (want_assign) { next(p); return TK_SHR_ASSIGN; }
        } else {
            Lexer save2 = p->lx;
            Token t1 = p->tok;
            next(p);
            bool ge3 = false;
            if ((at(p, TK_GT) || at(p, TK_GE)) && p->tok.start == end2) {
                ge3 = at(p, TK_GE);
                if (ge3 && want_assign) { next(p); return TK_USHR_ASSIGN; }
                if (!ge3 && !want_assign) { next(p); return TK_USHR; }
            }
            p->lx = save2;
            p->tok = t1;
            if (!want_assign && !ge3) { next(p); return TK_SHR; } /* (>>>= is an assignment: left for parse_assign) */
        }
    }
    p->lx = save;
    p->tok = t0;
    return 0;
}
static Expr *parse_shift(Parser *p) {
    Expr *e = parse_add(p);
    for (;;) {
        Pos pos = p->tok.pos;
        TokKind op;
        if (at(p, TK_SHL)) { op = TK_SHL; next(p); }
        else if (!(op = take_shift_right(p, false))) return e;
        e = binary(op, pos, e, parse_add(p));
    }
}
/* `x as T` binds like a relational operator in TypeScript */
static Expr *parse_as(Parser *p) {
    Expr *e = parse_shift(p);
    while (at_ident(p, "as")) {
        Expr *a = new_expr(E_AS, p->tok.pos);
        next(p);
        a->lhs = e;
        a->elem_type = parse_type(p);
        e = a;
    }
    return e;
}
static Expr *parse_rel_base(Parser *p);
/* x instanceof C: the class name is kept in the right operand as an identifier */
static Expr *parse_rel(Parser *p) {
    Expr *e = parse_rel_base(p);
    while (at_ident(p, "instanceof")) {
        Pos pos = p->tok.pos;
        next(p);
        Expr *c = new_expr(E_IDENT, p->tok.pos);
        c->str = expect(p, TK_IDENT, "write a class name: x instanceof Foo").str;
        e = binary(TK_IDENT, pos, e, c); /* TK_IDENT as operator: instanceof */
    }
    return e;
}
/* a '>' with another '>' or '>=' right after it is the start of >> / >>= ..., not a comparison */
static bool gt_glued(Parser *p) {
    if (!at(p, TK_GT)) return false;
    Lexer save = p->lx;
    Token t0 = p->tok;
    next(p);
    bool g = (at(p, TK_GT) || at(p, TK_GE)) && p->tok.start == t0.start + t0.len;
    p->lx = save;
    p->tok = t0;
    return g;
}
BINARY_LEVEL(parse_rel_base, parse_as, at(p, TK_LT) || at(p, TK_LE) || (at(p, TK_GT) && !gt_glued(p)) || at(p, TK_GE))
BINARY_LEVEL(parse_eq, parse_rel, at(p, TK_EQ) || at(p, TK_NEQ))
BINARY_LEVEL(parse_band, parse_eq, at(p, TK_AMP))
BINARY_LEVEL(parse_bxor, parse_band, at(p, TK_CARET))
BINARY_LEVEL(parse_bor, parse_bxor, at(p, TK_PIPE))
BINARY_LEVEL(parse_and, parse_bor, at(p, TK_AND))
BINARY_LEVEL(parse_or, parse_and, at(p, TK_OR))

static bool is_assign_op(TokKind k) {
    return k == TK_ASSIGN || k == TK_PLUS_ASSIGN || k == TK_MINUS_ASSIGN || k == TK_STAR_ASSIGN || k == TK_SLASH_ASSIGN ||
           k == TK_AMP_ASSIGN || k == TK_PIPE_ASSIGN || k == TK_CARET_ASSIGN || k == TK_SHL_ASSIGN;
}

static Expr *parse_assign(Parser *p);
static Expr *parse_assign(Parser *p) {
    Expr *lhs = parse_or(p);
    if (at(p, TK_QUESTION)) { /* ternary, right associative */
        Expr *e = new_expr(E_COND, p->tok.pos);
        next(p);
        e->lhs = lhs;
        expr_list_push(&e->args, parse_assign(p));
        expect(p, TK_COLON, NULL);
        expr_list_push(&e->args, parse_assign(p));
        return e;
    }
    TokKind shr = 0;
    Pos shr_pos = p->tok.pos;
    if (at(p, TK_GT)) shr = take_shift_right(p, true); /* x >>= n, x >>>= n */
    if (shr || is_assign_op(p->tok.kind)) {
        Pos pos = shr ? shr_pos : p->tok.pos;
        TokKind op = shr ? shr : p->tok.kind;
        if (lhs->kind != E_IDENT && lhs->kind != E_INDEX && !(lhs->kind == E_MEMBER && !lhs->is_call))
            xs_error(pos, NULL, "the left side of an assignment must be a variable, a field or an array element");
        if (!shr) next(p);
        Expr *e = new_expr(E_ASSIGN, pos);
        e->op = op;
        e->lhs = lhs;
        e->rhs = parse_assign(p);
        return e;
    }
    return lhs;
}

static Expr *parse_expr(Parser *p) { return parse_assign(p); }

static void expect_semi(Parser *p) {
    if (!at(p, TK_SEMI))
        xs_error(p->tok.pos, "xisto requires explicit semicolons; automatic semicolon insertion is not supported",
                 "expected ';' but found %s", found(p));
    next(p);
}

/* const [a, , c] = e / const { a, b: x } = e: a hidden variable holds e, and one declaration per name reads
   it (an index or a field), queued in p->pending to follow in the same scope. The checker gives the reads
   their meaning: on an array a[i] (checked), on a tuple its field, on an object its field. */
static int ndestr;
static Expr *ident_expr(Pos pos, const char *name) { Expr *e = new_expr(E_IDENT, pos); e->str = (char *)name; return e; }
static void destructure(Parser *p, Stmt *holder, bool is_const, bool array) {
    Pos pos = p->tok.pos;
    char *tmp = xs_sprintf("__d%d", ++ndestr);
    holder->name = tmp;
    holder->is_const = true;
    next(p); /* [ or { */
    int idx = 0;
    while (!at(p, array ? TK_RBRACKET : TK_RBRACE)) {
        if (array && at(p, TK_COMMA)) { next(p); idx++; continue; } /* a hole */
        if (at(p, TK_DOT)) xs_error(p->tok.pos, "take the rest with a slice: const rest = a.slice(n)", "'...rest' in a destructuring is not supported");
        Token nm = expect(p, TK_IDENT, array ? "a name for the element" : "a field name");
        const char *field = nm.str, *var = nm.str;
        if (!array && accept(p, TK_COLON)) var = expect(p, TK_IDENT, "the name to give the field: { field: name }").str;
        Expr *read;
        if (array) {
            read = new_expr(E_INDEX, nm.pos);
            read->lhs = ident_expr(nm.pos, tmp);
            Expr *ix = new_expr(E_NUMBER, nm.pos);
            ix->is_int_lit = true;
            ix->ival = idx;
            ix->num = idx;
            read->rhs = ix;
        } else {
            read = new_expr(E_MEMBER, nm.pos);
            read->lhs = ident_expr(nm.pos, tmp);
            read->str = (char *)field;
        }
        if (accept(p, TK_ASSIGN)) { /* a default: when the value read is null (xisto's absent value) */
            Expr *dflt = parse_expr(p);
            Expr *c = new_expr(E_COND, nm.pos);
            Expr *test = new_expr(E_BINARY, nm.pos);
            test->op = TK_NEQ;
            test->lhs = read;
            test->rhs = new_expr(E_NULL, nm.pos);
            c->lhs = test;
            Expr *again = xs_alloc(sizeof *again);
            *again = *read;
            again->lhs = ident_expr(nm.pos, tmp);
            expr_list_push(&c->args, again);
            expr_list_push(&c->args, dflt);
            read = c;
        }
        Stmt *d = new_stmt(S_VAR, nm.pos);
        d->name = (char *)var;
        d->is_const = is_const;
        d->init = read;
        stmt_list_push(&p->pending, d);
        idx++;
        if (!accept(p, TK_COMMA)) break;
    }
    expect(p, array ? TK_RBRACKET : TK_RBRACE, NULL);
    (void)pos;
}
static Stmt *parse_var(Parser *p) {
    Stmt *s = new_stmt(S_VAR, p->tok.pos);
    s->is_const = at(p, TK_CONST);
    next(p);
    if (at(p, TK_LBRACKET) || at(p, TK_LBRACE)) { /* const [a, b] = e;  const { a, b } = e; */
        destructure(p, s, s->is_const, at(p, TK_LBRACKET));
        if (accept(p, TK_COLON)) s->decl_type = parse_type(p);
        expect(p, TK_ASSIGN, "a destructuring takes its value: const [a, b] = pair;");
        s->init = parse_expr(p);
        return s;
    }
    Token name = expect(p, TK_IDENT, NULL);
    s->name = name.str;
    if (accept(p, TK_COLON)) s->decl_type = parse_type(p);
    if (!at(p, TK_ASSIGN))
        xs_error(p->tok.pos, "xisto has no 'undefined', so every variable starts with a value", "variable '%s' must be initialized", s->name);
    next(p);
    s->init = parse_expr(p);
    while (at(p, TK_COMMA)) { /* let a = 1, b = 2: the next ones follow as their own declarations */
        next(p);
        Stmt *d = new_stmt(S_VAR, p->tok.pos);
        d->is_const = s->is_const;
        Token nm = expect(p, TK_IDENT, NULL);
        d->name = nm.str;
        if (accept(p, TK_COLON)) d->decl_type = parse_type(p);
        if (!at(p, TK_ASSIGN))
            xs_error(p->tok.pos, "xisto has no 'undefined', so every variable starts with a value", "variable '%s' must be initialized", d->name);
        next(p);
        d->init = parse_expr(p);
        stmt_list_push(&p->pending, d);
    }
    return s;
}

/* the declarations a destructuring queued, after its hidden variable (same module and export at the top level) */
static void drain_pending(Parser *p, StmtList *l, Module *m, bool exported) {
    for (int i = 0; i < p->pending.len; i++) {
        Stmt *d = p->pending.items[i];
        if (m) { d->mod = m; d->exported = exported; }
        stmt_list_push(l, d);
    }
    p->pending.len = 0;
}
static Stmt *parse_block(Parser *p) {
    Stmt *b = new_stmt(S_BLOCK, p->tok.pos);
    expect(p, TK_LBRACE, NULL);
    while (!at(p, TK_RBRACE)) {
        if (at(p, TK_EOF)) xs_error(p->tok.pos, NULL, "unexpected end of file inside a block");
        stmt_list_push(&b->stmts, parse_stmt(p));
        drain_pending(p, &b->stmts, NULL, false);
    }
    next(p);
    return b;
}

static Stmt *parse_body(Parser *p) {
    if (!at(p, TK_LBRACE))
        xs_error(p->tok.pos, "xisto requires braces around the body of if, else, while and for", "expected '{'");
    return parse_block(p);
}
/* the body of if, else, while and for: a block, or (as in TypeScript) one statement, which is then a block of one */
static Stmt *parse_loop_body(Parser *p) {
    if (at(p, TK_LBRACE)) return parse_block(p);
    if (at(p, TK_LET) || at(p, TK_CONST)) xs_error(p->tok.pos, "put it in braces: { let x = ...; }", "a declaration cannot be the body of if, else, while or for");
    Stmt *b = new_stmt(S_BLOCK, p->tok.pos);
    stmt_list_push(&b->stmts, parse_stmt(p));
    return b;
}

static Stmt *parse_stmt(Parser *p) {
    Pos pos = p->tok.pos;
    if (!p->in_gen && at_ident(p, "yield") && !peek_is(p, TK_ASSIGN) && !peek_is(p, TK_LPAREN) && !peek_is(p, TK_DOT) && !peek_is(p, TK_SEMI))
        xs_error(pos, "make it a generator: function* name(): Generator<T> { ... }", "'yield' is used inside a generator");
    if (p->in_gen && at_ident(p, "yield")) { /* yield v;  yield* other;  (statements, in a generator) */
        next(p);
        if (accept(p, TK_STAR)) { /* for (const v of other) yield v; */
            static int ny;
            char *v = xs_sprintf("__yield%d", ++ny);
            Stmt *loop = new_stmt(S_FOROF, pos);
            loop->name = v;
            loop->is_const = true;
            loop->expr = parse_expr(p);
            expect_semi(p);
            Stmt *y = new_stmt(S_YIELD, pos);
            Expr *id = new_expr(E_IDENT, pos);
            id->str = v;
            y->expr = id;
            Stmt *body = new_stmt(S_BLOCK, pos);
            stmt_list_push(&body->stmts, y);
            loop->body = body;
            return loop;
        }
        Stmt *y = new_stmt(S_YIELD, pos);
        if (at(p, TK_SEMI) || at(p, TK_RBRACE)) xs_error(pos, "yield a value: yield x;", "a generator yields values");
        y->expr = parse_expr(p);
        expect_semi(p);
        return y;
    }
    switch (p->tok.kind) {
    case TK_LET:
    case TK_CONST: {
        Stmt *s = parse_var(p);
        expect_semi(p);
        return s;
    }
    case TK_IF: {
        next(p);
        Stmt *s = new_stmt(S_IF, pos);
        expect(p, TK_LPAREN, NULL);
        s->cond = parse_expr(p);
        expect(p, TK_RPAREN, NULL);
        s->body = parse_loop_body(p);
        if (accept(p, TK_ELSE)) s->else_body = at(p, TK_IF) ? parse_stmt(p) : parse_loop_body(p);
        return s;
    }
    case TK_WHILE: {
        next(p);
        Stmt *s = new_stmt(S_WHILE, pos);
        expect(p, TK_LPAREN, NULL);
        s->cond = parse_expr(p);
        expect(p, TK_RPAREN, NULL);
        s->body = parse_loop_body(p);
        return s;
    }
    case TK_FOR: {
        next(p);
        Stmt *s = new_stmt(S_FOR, pos);
        accept(p, TK_AWAIT); /* for await (const x of stream): await is syntax only */
        expect(p, TK_LPAREN, NULL);
        if (at(p, TK_LET) || at(p, TK_CONST)) {
            { /* for (const x of arr) */
                Parser save = *p;
                bool is_const = at(p, TK_CONST);
                next(p);
                if (at(p, TK_LBRACKET) || at(p, TK_LBRACE)) { /* for (const [k, v] of e): the pattern opens the body */
                    Stmt *holder = new_stmt(S_VAR, p->tok.pos);
                    destructure(p, holder, is_const, at(p, TK_LBRACKET));
                    if (!at_ident(p, "of")) xs_error(p->tok.pos, NULL, "expected 'of' after the destructuring of a for loop");
                    next(p);
                    s->kind = S_FOROF;
                    s->name = holder->name;
                    s->is_const = true;
                    s->expr = parse_expr(p);
                    expect(p, TK_RPAREN, NULL);
                    StmtList decls = p->pending;
                    p->pending = (StmtList){0};
                    s->body = parse_loop_body(p);
                    StmtList body = {0};
                    for (int i = 0; i < decls.len; i++) stmt_list_push(&body, decls.items[i]);
                    for (int i = 0; i < s->body->stmts.len; i++) stmt_list_push(&body, s->body->stmts.items[i]);
                    s->body->stmts = body;
                    return s;
                }
                if (at(p, TK_IDENT)) {
                    Token name = p->tok;
                    next(p);
                    Type *dt = NULL;
                    if (accept(p, TK_COLON)) dt = parse_type(p);
                    if (at_ident(p, "of")) {
                        next(p);
                        s->kind = S_FOROF;
                        s->name = name.str;
                        s->is_const = is_const;
                        s->decl_type = dt;
                        s->expr = parse_expr(p);
                        expect(p, TK_RPAREN, NULL);
                        s->body = parse_loop_body(p);
                        return s;
                    }
                    if (at_ident(p, "in")) xs_error(p->tok.pos, "use for (const x of array) or a classic for loop", "'for...in' is not supported");
                }
                *p = save;
            }
            s->for_init = parse_var(p);
            if (p->pending.len) xs_error(s->for_init->pos, "declare the others before the loop", "a for loop declares one variable (or destructures) in xisto");
        } else if (!at(p, TK_SEMI)) {
            Stmt *e = new_stmt(S_EXPR, p->tok.pos);
            e->expr = parse_expr(p);
            s->for_init = e;
        }
        expect(p, TK_SEMI, "xisto supports only the classic for (init; cond; update) loop");
        if (!at(p, TK_SEMI)) s->cond = parse_expr(p);
        expect(p, TK_SEMI, NULL);
        if (!at(p, TK_RPAREN)) s->for_update = parse_expr(p);
        expect(p, TK_RPAREN, NULL);
        s->body = parse_loop_body(p);
        return s;
    }
    case TK_THROW: {
        next(p);
        Stmt *s = new_stmt(S_THROW, pos);
        s->expr = parse_expr(p);
        expect_semi(p);
        return s;
    }
    case TK_TRY: {
        next(p);
        Stmt *s = new_stmt(S_TRY, pos);
        s->body = parse_body(p);
        if (!at(p, TK_CATCH) && !at_ident(p, "finally")) xs_error(p->tok.pos, NULL, "expected 'catch' or 'finally' after the try block");
        if (accept(p, TK_CATCH)) {
            if (accept(p, TK_LPAREN)) {
                s->name = expect(p, TK_IDENT, NULL).str;
                if (accept(p, TK_COLON)) expect(p, TK_IDENT, NULL); /* catch (e: unknown): e is always an Error */
                expect(p, TK_RPAREN, NULL);
            } else {
                s->name = "_err"; /* catch { } without a binding */
            }
            s->else_body = parse_body(p);
        }
        if (at_ident(p, "finally")) {
            next(p);
            s->for_init = parse_body(p);
        }
        return s;
    }
    case TK_BREAK:
    case TK_CONTINUE: {
        Stmt *s = new_stmt(p->tok.kind == TK_BREAK ? S_BREAK : S_CONTINUE, pos);
        next(p);
        if (at(p, TK_IDENT)) xs_error(p->tok.pos, NULL, "labels are not supported");
        expect_semi(p);
        return s;
    }
    case TK_SWITCH: {
        next(p);
        Stmt *s = new_stmt(S_SWITCH, pos);
        expect(p, TK_LPAREN, NULL);
        s->expr = parse_expr(p);
        expect(p, TK_RPAREN, NULL);
        expect(p, TK_LBRACE, NULL);
        int cap = 0;
        bool has_default = false;
        while (!at(p, TK_RBRACE)) {
            Case c = {0};
            c.pos = p->tok.pos;
            if (accept(p, TK_DEFAULT)) {
                if (has_default) xs_error(c.pos, NULL, "two default cases");
                has_default = true;
            } else {
                expect(p, TK_CASE, "expected 'case' or 'default'");
                c.value = parse_expr(p);
            }
            expect(p, TK_COLON, NULL);
            c.body = new_stmt(S_BLOCK, p->tok.pos);
            while (!at(p, TK_CASE) && !at(p, TK_DEFAULT) && !at(p, TK_RBRACE)) {
                if (at(p, TK_EOF)) xs_error(p->tok.pos, NULL, "unexpected end of file inside switch");
                stmt_list_push(&c.body->stmts, parse_stmt(p));
                drain_pending(p, &c.body->stmts, NULL, false);
            }
            if (s->ncases == cap) {
                cap = cap ? cap * 2 : 4;
                s->cases = realloc(s->cases, sizeof(Case) * (size_t)cap);
            }
            s->cases[s->ncases++] = c;
        }
        next(p);
        return s;
    }
    case TK_RETURN: {
        next(p);
        Stmt *s = new_stmt(S_RETURN, pos);
        if (!at(p, TK_SEMI)) s->expr = parse_expr(p);
        expect_semi(p);
        return s;
    }
    case TK_LBRACE:
        return parse_block(p);
    case TK_FUNCTION:
    case TK_ASYNC:
        xs_error(pos, "move it to the top level of the file", "nested functions are not supported");
        return NULL;
    case TK_IMPORT:
    case TK_EXPORT:
        xs_error(pos, "imports and exports go at the top level of the file", "'%s' is not allowed here", p->tok.kind == TK_IMPORT ? "import" : "export");
        return NULL;
    case TK_CLASS:
    case TK_ENUM:
        xs_error(pos, "move it to the top level of the file", "nested classes and enums are not supported");
        return NULL;
    default: {
        Stmt *s = new_stmt(S_EXPR, pos);
        s->expr = parse_expr(p);
        expect_semi(p);
        return s;
    }
    }
}

/* zero or more `@Name` / `@Name(args)` */
static void parse_decorators(Parser *p, DecoList *dl) {
    while (at(p, TK_AT)) {
        Decorator d = {0};
        d.pos = p->tok.pos;
        next(p);
        const char *from = p->tok.start;
        d.name = expect(p, TK_IDENT, "a decorator is a name: @Injectable()").str;
        if (at(p, TK_LPAREN)) { d.called = true; parse_args(p, &d.args); }
        d.src = span(p, from);
        if (dl->len == dl->cap) {
            dl->cap = dl->cap ? dl->cap * 2 : 2;
            dl->items = realloc(dl->items, sizeof(Decorator) * (size_t)dl->cap);
        }
        dl->items[dl->len++] = d;
    }
}

/* is_ctor: parameters may be parameter properties (constructor(private readonly repo: Repo)) */
static void parse_params_ex(Parser *p, Func *f, bool is_ctor) {
    expect(p, TK_LPAREN, NULL);
    int cap = 0;
    while (!at(p, TK_RPAREN)) {
        if (f->nparams == cap) {
            cap = cap ? cap * 2 : 4;
            f->params = realloc(f->params, sizeof(Param) * (size_t)cap);
        }
        Param prm = {0};
        parse_decorators(p, &prm.decos);
        Pos mpos = p->tok.pos;
        while (at_ident(p, "public") || at_ident(p, "private") || at_ident(p, "protected") || at_ident(p, "readonly")) {
            if (!is_ctor) xs_error(p->tok.pos, NULL, "'%s' applies to constructor parameters only", p->tok.str);
            prm.is_prop = true;
            if (at_ident(p, "private")) prm.access = 2;
            else if (at_ident(p, "protected")) prm.access = 1;
            else if (at_ident(p, "readonly")) prm.readonly = true;
            next(p);
        }
        (void)mpos;
        Token name;
        if (at(p, TK_LBRACKET) || at(p, TK_LBRACE)) { /* f([a, b]: [int, int]) / f({ x, y }: Point): a hidden parameter */
            StmtList saved = p->pending;
            p->pending = (StmtList){0};
            Stmt holder = {0};
            name.pos = p->tok.pos;
            destructure(p, &holder, true, at(p, TK_LBRACKET));
            name.str = holder.name;
            for (int i = 0; i < p->pending.len; i++) stmt_list_push(&f->param_decls, p->pending.items[i]);
            p->pending = saved;
        } else name = expect(p, TK_IDENT, NULL);
        bool opt = accept(p, TK_QUESTION);
        if (!at(p, TK_COLON))
            xs_error(p->tok.pos, "parameters need an explicit type: (n: number)", "parameter '%s' has no type", name.str);
        next(p);
        prm.name = name.str;
        const char *tfrom = p->tok.start;
        prm.type = parse_type(p);
        prm.type_src = span(p, tfrom);
        if (opt) { /* x?: T is T | null, null when left out */
            prm.type = ty_nullable_of(prm.type);
            prm.dflt = new_expr(E_NULL, name.pos);
        }
        if (accept(p, TK_ASSIGN)) prm.dflt = parse_expr(p);
        if (!prm.dflt && f->nparams > 0 && f->params[f->nparams - 1].dflt)
            xs_error(name.pos, "give it a default too, or move it before the optional ones", "parameter '%s' follows an optional parameter", name.str);
        f->params[f->nparams++] = prm;
        if (!accept(p, TK_COMMA)) break;
    }
    expect(p, TK_RPAREN, NULL);
}
static void parse_params(Parser *p, Func *f) { parse_params_ex(p, f, false); }

/* 'function' already consumed (and 'async' if present). */
/* <T, U extends X>: the names (a constraint is accepted: each instance is checked with the real type) */
static void parse_tparams(Parser *p, Func *f) {
    expect(p, TK_LT, NULL);
    do {
        f->tparams = realloc(f->tparams, sizeof(char *) * (size_t)(f->ntparams + 1));
        f->tparams[f->ntparams++] = expect(p, TK_IDENT, NULL).str;
        if (at_ident(p, "extends")) { next(p); parse_type(p); }
    } while (accept(p, TK_COMMA));
    expect(p, TK_GT, "close the type parameters: <T, U>");
}
static void parse_params_ex(Parser *p, Func *f, bool ctor);
/* parameters, return type and body; a template keeps where they start, and T is a TY_TPARAM inside */
static void parse_generic_rest(Parser *p, Func *f, bool method) {
    char **saved = tp_names;
    int saved_n = ntp;
    if (f->ntparams) {
        Parser *st = xs_alloc(sizeof *st);
        *st = *p;
        f->gen_state = st;
        f->gen_method = method;
        tp_names = f->tparams;
        ntp = f->ntparams;
    }
    if (method) parse_params_ex(p, f, false);
    else parse_params(p, f);
    if (!at(p, TK_COLON))
        xs_error(p->tok.pos, method ? "write the return type: ping(): string { ... }" : "write the return type: function f(): number { ... }",
                 "%s '%s' has no return type", method ? "method" : "function", f->name);
    next(p);
    const char *rfrom = p->tok.start;
    f->ret = parse_type(p);
    f->ret_src = span(p, rfrom);
    bool saved_gen = p->in_gen;
    p->in_gen = f->is_gen;
    f->body = parse_block(p);
    prepend_param_decls(f);
    p->in_gen = saved_gen;
    tp_names = saved;
    ntp = saved_n;
}
static Func *parse_function_rest(Parser *p, Pos pos, bool is_async) {
    Func *f = xs_alloc(sizeof *f);
    f->pos = pos;
    f->is_async = is_async;
    if (accept(p, TK_STAR)) f->is_gen = true; /* function* */
    f->name = expect(p, TK_IDENT, NULL).str;
    if (at(p, TK_LT)) parse_tparams(p, f);
    parse_generic_rest(p, f, false);
    return f;
}

/* an instance of a generic function or method: its parameters, return type and body parsed again with T bound */
Func *xs_parse_instance(Func *tmpl, Type **args) {
    Parser p = *(Parser *)tmpl->gen_state;
    char **sn = subst_names;
    Type **st = subst_types;
    int sk = nsubst;
    subst_names = tmpl->tparams;
    subst_types = args;
    nsubst = tmpl->ntparams;
    Func *f = xs_alloc(sizeof *f);
    f->pos = tmpl->pos;
    f->name = tmpl->name;
    f->is_async = tmpl->is_async;
    f->cls = tmpl->cls;
    f->mod = tmpl->mod;
    f->exported = tmpl->exported;
    f->template_of = tmpl;
    f->is_gen = tmpl->is_gen;
    parse_generic_rest(&p, f, tmpl->gen_method);
    subst_names = sn;
    subst_types = st;
    nsubst = sk;
    return f;
}

/* member modifiers: public, private, protected, override are accepted; static, abstract and readonly are kept */
enum { MOD_STATIC = 1, MOD_ABSTRACT = 2, MOD_READONLY = 4, MOD_PRIVATE = 8, MOD_PROTECTED = 16 };
static int skip_modifiers(Parser *p) {
    int mods = 0;
    for (;;) {
        if (at_ident(p, "private") && !peek_is(p, TK_LPAREN) && !peek_is(p, TK_COLON)) { mods |= MOD_PRIVATE; next(p); continue; }
        if (at_ident(p, "protected") && !peek_is(p, TK_LPAREN) && !peek_is(p, TK_COLON)) { mods |= MOD_PROTECTED; next(p); continue; }
        if (at_ident(p, "public") || at_ident(p, "override")) { next(p); continue; }
        if (at_ident(p, "readonly")) { mods |= MOD_READONLY; next(p); continue; }
        if (at_ident(p, "static") && !peek_is(p, TK_LPAREN) && !peek_is(p, TK_COLON)) { mods |= MOD_STATIC; next(p); continue; }
        if (at_ident(p, "abstract") && !peek_is(p, TK_LPAREN) && !peek_is(p, TK_COLON)) { mods |= MOD_ABSTRACT; next(p); continue; }
        return mods;
    }
}

static void parse_fields_body(Parser *p, Class *c);

/* interface Name { a: T; b?: U }  or  type Name = { a: T, b: U }; */
static Class *parse_interface(Parser *p) {
    Class *c = xs_alloc(sizeof *c);
    c->pos = p->tok.pos;
    c->is_interface = true;
    bool is_type = at_ident(p, "type");
    next(p);
    c->name = expect(p, TK_IDENT, NULL).str;
    if (at(p, TK_LT)) { /* interface Name<T, U> */
        next(p);
        do {
            c->tparams = realloc(c->tparams, sizeof(char *) * (size_t)(c->ntparams + 1));
            c->tparams[c->ntparams++] = expect(p, TK_IDENT, NULL).str;
        } while (accept(p, TK_COMMA));
        expect(p, TK_GT, NULL);
    }
    if (at_ident(p, "extends")) { /* interface B extends A, C: their fields first */
        if (is_type) xs_error(p->tok.pos, NULL, "a type alias does not extend; use interface B extends A");
        next(p);
        do {
            c->iface_bases = realloc(c->iface_bases, sizeof(char *) * (size_t)(c->niface_bases + 1));
            c->iface_bases[c->niface_bases++] = expect(p, TK_IDENT, NULL).str;
            if (at(p, TK_LT)) xs_error(p->tok.pos, NULL, "extending a generic interface is not supported yet");
        } while (accept(p, TK_COMMA));
    }
    if (is_type) {
        expect(p, TK_ASSIGN, NULL);
        if (!at(p, TK_LBRACE)) { /* type Shape = Circle | Rect; type Id = string | number; */
            c->alias = parse_type(p);
            accept(p, TK_SEMI);
            return c;
        }
    }
    parse_fields_body(p, c);
    if (is_type) accept(p, TK_SEMI);
    return c;
}

static Class *parse_class_ex(Parser *p, bool abstract);
static Class *parse_class(Parser *p) { return parse_class_ex(p, false); }
static Class *parse_class_ex(Parser *p, bool abstract) {
    Class *c = xs_alloc(sizeof *c);
    c->pos = p->tok.pos;
    c->is_abstract = abstract;
    expect(p, TK_CLASS, NULL);
    c->name = expect(p, TK_IDENT, NULL).str;
    if (at_ident(p, "extends")) {
        next(p);
        c->base_pos = p->tok.pos;
        c->base_name = expect(p, TK_IDENT, NULL).str;
        if (at(p, TK_LT)) xs_error(p->tok.pos, NULL, "extending a generic class is not supported yet");
    }
    if (at_ident(p, "implements")) { /* recorded; the methods are what the framework calls */
        next(p);
        do {
            Token t = expect(p, TK_IDENT, NULL);
            if (at(p, TK_LT)) { int depth = 0; do { if (at(p, TK_LT)) depth++; else if (at(p, TK_GT)) depth--; next(p); } while (depth > 0 && !at(p, TK_EOF)); }
            c->implements = realloc(c->implements, sizeof(char *) * (size_t)(c->nimplements + 1));
            c->implements[c->nimplements++] = t.str;
        } while (accept(p, TK_COMMA));
    }
    parse_fields_body(p, c);
    return c;
}

static void parse_fields_body(Parser *p, Class *c) {
    expect(p, TK_LBRACE, NULL);
    while (!at(p, TK_RBRACE)) {
        if (at(p, TK_EOF)) xs_error(p->tok.pos, NULL, "unexpected end of file inside '%s'", c->name);
        DecoList decos = {0};
        parse_decorators(p, &decos);
        Pos pos = p->tok.pos;
        int mods = skip_modifiers(p);
        if ((mods & MOD_ABSTRACT) && !c->is_abstract) xs_error(pos, "write abstract class Name", "an abstract member needs an abstract class");
        if ((mods & MOD_STATIC) && c->is_interface) xs_error(pos, NULL, "interfaces have no static members");
        bool is_async = accept(p, TK_ASYNC);
        bool gen_method = !c->is_interface && accept(p, TK_STAR); /* *values(): a generator method */
        if (gen_method && at(p, TK_LBRACKET)) { /* *[Symbol.iterator](): what for...of uses on the object */
            next(p);
            Token sym = expect(p, TK_IDENT, NULL);
            expect(p, TK_DOT, "write *[Symbol.iterator]()");
            Token it = expect(p, TK_IDENT, NULL);
            if (strcmp(sym.str, "Symbol") != 0 || strcmp(it.str, "iterator") != 0) xs_error(sym.pos, NULL, "only [Symbol.iterator] is supported as a computed method name");
            expect(p, TK_RBRACKET, NULL);
        }
        bool sym_iter = gen_method && at(p, TK_LPAREN) && p->prev_end && p->prev_end[-1] == ']';
        int accessor = 0; /* get x() / set x(v) */
        if (!gen_method && (at_ident(p, "get") || at_ident(p, "set")) && peek_is(p, TK_IDENT) && !c->is_interface) {
            accessor = at_ident(p, "get") ? 1 : 2;
            next(p);
        }
        Token name;
        if (sym_iter) { name = p->tok; name.kind = TK_IDENT; name.str = "__iterator"; name.pos = pos; }
        else name = expect(p, TK_IDENT, NULL);
        if (gen_method && !at(p, TK_LPAREN) && !at(p, TK_LT)) xs_error(name.pos, NULL, "'*' marks a generator method: *%s(): Generator<T> { ... }", name.str);
        if (accessor) {
            if (!at(p, TK_LPAREN)) xs_error(p->tok.pos, NULL, "write %s %s(...) { ... }", accessor == 1 ? "get" : "set", name.str);
            name.str = xs_sprintf("__%s_%s", accessor == 1 ? "get" : "set", name.str);
        }
        if (at(p, TK_LPAREN) && c->is_interface) xs_error(name.pos, "interfaces hold data only; use a class for methods", "method '%s' in interface '%s'", name.str, c->name);
        if (at(p, TK_LPAREN) || (at(p, TK_LT) && !c->is_interface)) { /* method or constructor (or a generic method) */
            Func *f = xs_alloc(sizeof *f);
            f->pos = pos;
            f->name = name.str;
            f->is_async = is_async;
            f->cls = c;
            f->decos = decos;
            f->access = mods & MOD_PRIVATE ? 2 : mods & MOD_PROTECTED ? 1 : 0;
            f->is_getter = accessor == 1;
            f->is_setter = accessor == 2;
            f->is_gen = gen_method;
            if (at(p, TK_LT)) { /* a generic method: m<T>(x: T): T */
                if (strcmp(name.str, "constructor") == 0) xs_error(pos, NULL, "a constructor has no type parameters");
                parse_tparams(p, f);
                parse_generic_rest(p, f, true);
                if (mods & MOD_STATIC) {
                    c->smethods = realloc(c->smethods, sizeof(Func *) * (size_t)(c->nsmethods + 1));
                    c->smethods[c->nsmethods++] = f;
                    f->cls = NULL;
                } else {
                    if (c->nmethods == c->cap_methods) {
                        c->cap_methods = c->cap_methods ? c->cap_methods * 2 : 4;
                        c->methods = realloc(c->methods, sizeof(Func *) * (size_t)c->cap_methods);
                    }
                    c->methods[c->nmethods++] = f;
                }
                continue;
            }
            parse_params_ex(p, f, strcmp(name.str, "constructor") == 0);
            if (strcmp(name.str, "constructor") == 0) {
                f->is_ctor = true;
                f->ret = ty_void;
                if (at(p, TK_COLON)) xs_error(p->tok.pos, NULL, "a constructor has no return type");
                if (c->ctor) xs_error(pos, NULL, "class '%s' has two constructors", c->name);
                c->ctor = f;
            } else {
                if (f->is_setter && !at(p, TK_COLON)) f->ret = ty_void; /* set x(v: T) { ... } */
                else {
                    if (!at(p, TK_COLON))
                        xs_error(p->tok.pos, "write the return type: ping(): string { ... }", "method '%s' has no return type", f->name);
                    next(p);
                    const char *rfrom = p->tok.start;
                    f->ret = parse_type(p);
                    f->ret_src = span(p, rfrom);
                }
                if (c->nmethods == c->cap_methods) {
                    c->cap_methods = c->cap_methods ? c->cap_methods * 2 : 4;
                    c->methods = realloc(c->methods, sizeof(Func *) * (size_t)c->cap_methods);
                }
                if (mods & MOD_STATIC) { /* a static method is a top-level function: moved by the caller */
                    c->smethods = realloc(c->smethods, sizeof(Func *) * (size_t)(c->nsmethods + 1));
                    c->smethods[c->nsmethods++] = f;
                    f->cls = NULL;
                } else {
                    c->methods[c->nmethods++] = f;
                }
            }
            if (mods & MOD_ABSTRACT) { /* abstract m(): T; */
                if (f->is_ctor) xs_error(pos, NULL, "a constructor cannot be abstract");
                f->is_abstract = true;
                f->body = new_stmt(S_BLOCK, pos);
                accept(p, TK_SEMI);
                continue;
            }
            bool saved_gen = p->in_gen;
            p->in_gen = f->is_gen;
            f->body = parse_block(p);
            prepend_param_decls(f);
            p->in_gen = saved_gen;
            if (f->is_ctor) {
                /* parameter properties: a field each, assigned before the body runs */
                int nprops = 0;
                for (int i = 0; i < f->nparams; i++) {
                    Param *prm = &f->params[i];
                    if (!prm->is_prop) continue;
                    Field fd = {0};
                    fd.name = prm->name;
                    fd.pos = f->pos;
                    fd.type = prm->type;
                    fd.access = prm->access;
                    fd.readonly = prm->readonly;
                    fd.owner = c;
                    if (c->nfields == c->cap_fields) {
                        c->cap_fields = c->cap_fields ? c->cap_fields * 2 : 4;
                        c->fields = realloc(c->fields, sizeof(Field) * (size_t)c->cap_fields);
                    }
                    c->fields[c->nfields++] = fd;
                    Expr *self = new_expr(E_THIS, f->pos);
                    Expr *mem = new_expr(E_MEMBER, f->pos);
                    mem->lhs = self;
                    mem->str = prm->name;
                    Expr *val = new_expr(E_IDENT, f->pos);
                    val->str = prm->name;
                    Expr *as = new_expr(E_ASSIGN, f->pos);
                    as->op = TK_ASSIGN;
                    as->lhs = mem;
                    as->rhs = val;
                    Stmt *st = new_stmt(S_EXPR, f->pos);
                    st->expr = as;
                    /* insert at nprops, keeping the order of the parameters */
                    StmtList *l = &f->body->stmts;
                    /* after super(...): a derived class has no `this` before it */
                    int at0 = nprops;
                    if (c->base_name && l->len > nprops && l->items[nprops]->kind == S_EXPR && l->items[nprops]->expr->kind == E_CALL &&
                        l->items[nprops]->expr->lhs && l->items[nprops]->expr->lhs->kind == E_IDENT && strcmp(l->items[nprops]->expr->lhs->str, "super") == 0)
                        at0 = nprops + 1;
                    stmt_list_push(l, st);
                    memmove(&l->items[at0 + 1], &l->items[at0], sizeof(Stmt *) * (size_t)(l->len - 1 - at0));
                    l->items[at0] = st;
                    nprops++;
                }
            }
        } else { /* field */
            if (is_async) xs_error(pos, NULL, "'async' applies to methods only");
            Field fd = {0};
            fd.name = name.str;
            fd.pos = pos;
            fd.decos = decos;
            fd.access = mods & MOD_PRIVATE ? 2 : mods & MOD_PROTECTED ? 1 : 0;
            fd.readonly = (mods & MOD_READONLY) != 0;
            fd.owner = c;
            if (accept(p, TK_QUESTION)) fd.optional = true;
            if (!at(p, TK_COLON))
                xs_error(p->tok.pos, "fields need an explicit type: base: string;", "field '%s' has no type", name.str);
            next(p);
            const char *tfrom = p->tok.start;
            fd.type = parse_type(p);
            fd.type_src = span(p, tfrom);
            if (fd.optional) fd.type = ty_nullable_of(fd.type);
            if (accept(p, TK_ASSIGN)) fd.init = parse_expr(p);
            if (!accept(p, TK_SEMI) && !at(p, TK_RBRACE)) accept(p, TK_COMMA); /* interfaces also separate with ',' or nothing */
            if (mods & MOD_ABSTRACT) xs_error(pos, NULL, "fields cannot be abstract");
            if (mods & MOD_STATIC) { /* a top-level variable, made by the caller where the class is declared */
                if (!fd.init) xs_error(pos, xs_sprintf("static %s: T = value;", name.str), "a static field needs a value");
                c->sfields = realloc(c->sfields, sizeof(Field) * (size_t)(c->nsfields + 1));
                c->sfield_ro = realloc(c->sfield_ro, sizeof(bool) * (size_t)(c->nsfields + 1));
                c->sfield_ro[c->nsfields] = (mods & MOD_READONLY) != 0;
                c->sfields[c->nsfields++] = fd;
                continue;
            }
            if (c->nfields == c->cap_fields) {
                c->cap_fields = c->cap_fields ? c->cap_fields * 2 : 4;
                c->fields = realloc(c->fields, sizeof(Field) * (size_t)c->cap_fields);
            }
            c->fields[c->nfields++] = fd;
        }
    }
    next(p);
}

/* declare class Name { constructor(a: A); field: T; method(b: B): R; }: a class implemented in C, like the
   runtime's built-in classes: struct xn_<pkg>_Name, xn_<pkg>_Name_new(...), xn_<pkg>_Name_method(self, ...) */
static Class *parse_declared_class(Parser *p, const char *prefix) {
    Class *c = xs_alloc(sizeof *c);
    c->pos = p->tok.pos;
    expect(p, TK_CLASS, NULL);
    c->name = expect(p, TK_IDENT, NULL).str;
    c->builtin = true;
    c->cname = xs_sprintf("%s%s", prefix, c->name);
    expect(p, TK_LBRACE, NULL);
    while (!at(p, TK_RBRACE)) {
        skip_modifiers(p);
        Token name = expect(p, TK_IDENT, NULL);
        if (at(p, TK_LPAREN)) {
            Func *f = xs_alloc(sizeof *f);
            f->pos = name.pos;
            f->name = name.str;
            f->cls = c;
            f->is_native = true;
            parse_params(p, f);
            if (strcmp(name.str, "constructor") == 0) {
                f->is_ctor = true;
                f->ret = ty_void;
                f->cname = xs_sprintf("%s_new", c->cname);
                c->ctor = f;
            } else {
                expect(p, TK_COLON, "a declared method states its return type");
                f->ret = parse_type(p);
                f->cname = xs_sprintf("%s_%s", c->cname, f->name);
                c->methods = realloc(c->methods, sizeof(Func *) * (size_t)(c->nmethods + 1));
                c->methods[c->nmethods++] = f;
            }
        } else {
            Field fd = {0};
            fd.name = name.str;
            fd.pos = name.pos;
            if (accept(p, TK_QUESTION)) fd.optional = true;
            expect(p, TK_COLON, NULL);
            fd.type = parse_type(p);
            if (fd.optional) fd.type = ty_nullable_of(fd.type);
            c->fields = realloc(c->fields, sizeof(Field) * (size_t)(c->nfields + 1));
            c->fields[c->nfields++] = fd;
        }
        expect_semi(p);
    }
    next(p);
    return c;
}

static void add_func(Program *prog, Func *f) {
    if (prog->nfuncs == prog->cap_funcs) {
        prog->cap_funcs = prog->cap_funcs ? prog->cap_funcs * 2 : 8;
        prog->funcs = realloc(prog->funcs, sizeof(Func *) * (size_t)prog->cap_funcs);
    }
    prog->funcs[prog->nfuncs++] = f;
}

static void add_class(Program *prog, Class *c) {
    if (prog->nclasses == prog->cap_classes) {
        prog->cap_classes = prog->cap_classes ? prog->cap_classes * 2 : 4;
        prog->classes = realloc(prog->classes, sizeof(Class *) * (size_t)prog->cap_classes);
    }
    prog->classes[prog->nclasses++] = c;
}

/* static members become top-level declarations where the class is: a variable per field (initialized in
   program order, as JavaScript runs a class's static initializers) and a function per method */
static void add_statics(Program *prog, Module *m, Class *c) {
    int n = c->nsfields + c->nsmethods;
    if (!n) return;
    c->static_names = xs_alloc(sizeof(char *) * (size_t)n);
    c->static_gen = xs_alloc(sizeof(char *) * (size_t)n);
    c->static_is_method = xs_alloc(sizeof(bool) * (size_t)n);
    for (int i = 0; i < c->nsfields; i++) {
        Field *fd = &c->sfields[i];
        Stmt *st = new_stmt(S_VAR, fd->pos);
        st->name = xs_sprintf("%s__s_%s", c->name, fd->name);
        st->decl_type = fd->type;
        st->init = fd->init;
        st->is_const = c->sfield_ro[i];
        st->mod = m;
        st->exported = c->exported;
        stmt_list_push(&prog->top, st);
        int k = c->nstatics_user++;
        c->static_names[k] = fd->name;
        c->static_gen[k] = st->name;
        c->static_is_method[k] = false;
    }
    for (int i = 0; i < c->nsmethods; i++) {
        Func *f = c->smethods[i];
        int k = c->nstatics_user++;
        c->static_names[k] = f->name;
        f->name = xs_sprintf("%s__s_%s", c->name, f->name);
        c->static_gen[k] = f->name;
        c->static_is_method[k] = true;
        f->mod = m;
        f->exported = c->exported;
        add_func(prog, f);
    }
}

static bool peek_is(Parser *p, TokKind k) {
    Parser save = *p;
    next(p);
    bool r = at(p, k);
    *p = save;
    return r;
}

/* `type` followed by a name: a type alias (otherwise `type` is just an identifier) */
static bool peek_is_ident(Parser *p) {
    Parser save = *p;
    next(p);
    bool r = at(p, TK_IDENT);
    *p = save;
    return r;
}

static void parse_import(Parser *p, Module *m) {
    Pos pos = p->tok.pos;
    expect(p, TK_IMPORT, NULL);
    Import im = {0};
    im.pos = pos;
    int cap = 0;
    if (at(p, TK_IDENT)) { /* import name from "m": m's default export, which must have this name */
        Token name = p->tok;
        next(p);
        im.is_default = true;
        im.names = xs_alloc(sizeof(char *));
        im.positions = xs_alloc(sizeof(Pos));
        im.names[0] = name.str;
        im.positions[0] = name.pos;
        im.n = 1;
        goto from;
    }
    if (!at(p, TK_LBRACE))
        xs_error(p->tok.pos, "xisto supports named imports only: import { a, b } from \"./file\";", "expected '{' after import");
    next(p);
    while (!at(p, TK_RBRACE)) {
        Token name = expect(p, TK_IDENT, NULL);
        if (at_ident(p, "as")) { /* import { a as b } */
            next(p);
            Token alias = expect(p, TK_IDENT, "write the local name: import { a as b }");
            p->alias_from = realloc(p->alias_from, sizeof(char *) * (size_t)(p->nalias + 1));
            p->alias_to = realloc(p->alias_to, sizeof(char *) * (size_t)(p->nalias + 1));
            p->alias_from[p->nalias] = alias.str;
            p->alias_to[p->nalias++] = name.str;
        }
        if (im.n == cap) {
            cap = cap ? cap * 2 : 4;
            im.names = realloc(im.names, sizeof(char *) * (size_t)cap);
            im.positions = realloc(im.positions, sizeof(Pos) * (size_t)cap);
        }
        im.names[im.n] = name.str;
        im.positions[im.n++] = name.pos;
        if (!accept(p, TK_COMMA)) break;
    }
    expect(p, TK_RBRACE, NULL);
from:
    if (!at_ident(p, "from")) xs_error(p->tok.pos, NULL, "expected 'from'");
    next(p);
    Token path = expect(p, TK_STRING, "write the path as a string: from \"./file\"");
    im.path = path.str;
    expect_semi(p);
    m->imports = realloc(m->imports, sizeof(Import) * (size_t)(m->nimports + 1));
    m->imports[m->nimports++] = im;
}

static Module *load_module(Program *prog, const char *file, Pos from_pos, const char *from_file);

/* the strings of a JSON array field of xisto.json ("native": ["a.c"]), NULL-terminated */
static char **json_str_array(const char *meta, const char *key) {
    char *k = strstr(meta, xs_sprintf("\"%s\"", key));
    if (!k) return NULL;
    char *open = strchr(k, '['), *close = open ? strchr(open, ']') : NULL;
    if (!open || !close) return NULL;
    char **out = NULL;
    int n = 0;
    for (char *q = open; q < close; q++) {
        if (*q != '"') continue;
        char *e = strchr(q + 1, '"');
        if (!e || e > close) break;
        out = realloc(out, sizeof(char *) * (size_t)(n + 2));
        out[n++] = xs_strndup(q + 1, (size_t)(e - q - 1));
        q = e;
    }
    if (out) out[n] = NULL;
    return out;
}
/* Native packages: their C sources join the program, and `declare` names get the package's C prefix. */
static const char *pending_prefix; /* set for the module about to be loaded from a package */
static void package_native(Program *prog, const char *dir, const char *name, const char *meta) {
    char **files = json_str_array(meta, "native");
    pending_prefix = NULL;
    if (!files) return;
    Buf pre = {0};
    buf_puts(&pre, "xn_");
    for (const char *c = name; *c; c++) buf_putc(&pre, isalnum((unsigned char)*c) ? *c : '_');
    buf_putc(&pre, '_');
    pending_prefix = pre.data;
    for (char **f = files; *f; f++) {
        char *path = xs_sprintf("%s/%s", dir, *f);
        bool seen = false;
        for (int i = 0; i < prog->nnative; i++) if (strcmp(prog->native_files[i], path) == 0) seen = true;
        if (seen) continue;
        prog->native_files = realloc(prog->native_files, sizeof(char *) * (size_t)(prog->nnative + 1));
        prog->native_files[prog->nnative++] = path;
    }
    char **needs = json_str_array(meta, "needs");
    for (char **nd = needs; nd && *nd; nd++) {
        if (strcmp(*nd, "net") == 0) prog->uses_net = true;
        else if (strcmp(*nd, "crypto") == 0) prog->uses_crypto = true;
    }
}

static Enum *parse_enum(Parser *p) {
    Enum *en = xs_alloc(sizeof *en);
    en->pos = p->tok.pos;
    expect(p, TK_ENUM, NULL);
    en->name = expect(p, TK_IDENT, NULL).str;
    expect(p, TK_LBRACE, NULL);
    int cap = 0;
    long long next_val = 0;
    int nstr = 0;
    while (!at(p, TK_RBRACE)) {
        EnumMember m = {0};
        Pos mpos = p->tok.pos;
        m.name = expect(p, TK_IDENT, NULL).str;
        if (accept(p, TK_ASSIGN)) {
            if (at(p, TK_STRING)) { m.is_string = true; m.sval = p->tok.str; next(p); }
            else if (at(p, TK_NUMBER) && p->tok.is_int_lit) { m.ival = p->tok.ival; next(p); next_val = m.ival + 1; }
            else if (at(p, TK_MINUS)) { next(p); Token n = expect(p, TK_NUMBER, NULL); m.ival = -n.ival; next_val = m.ival + 1; }
            else xs_error(p->tok.pos, "enum members are integer or string constants", "unsupported enum value");
        } else {
            m.ival = next_val++;
        }
        if (m.is_string) nstr++;
        for (int i = 0; i < en->nmembers; i++)
            if (strcmp(en->members[i].name, m.name) == 0) xs_error(mpos, NULL, "enum member '%s' is declared twice", m.name);
        if (en->nmembers == cap) {
            cap = cap ? cap * 2 : 4;
            en->members = realloc(en->members, sizeof(EnumMember) * (size_t)cap);
        }
        en->members[en->nmembers++] = m;
        if (!accept(p, TK_COMMA)) break;
    }
    expect(p, TK_RBRACE, NULL);
    if (nstr && nstr != en->nmembers) xs_error(en->pos, "use all strings or all integers", "enum '%s' mixes string and integer members", en->name);
    en->is_string = nstr > 0;
    if (en->nmembers == 0) xs_error(en->pos, NULL, "enum '%s' has no members", en->name);
    return en;
}

/* ---------- static reflection (xisto:reflect) ----------
   For each class with decorators, a ClassInfo (constructor, methods, fields, with typed adapters) and the
   decorator calls, written as xisto code from the module's own source text and parsed into the module,
   ahead of its top-level statements. TypeScript's legacy decorator order: per member, parameters (last
   first) then the member's decorators (bottom up); constructor parameters; then the class (bottom up). */
static bool class_decorated(Class *c) {
    if (c->decos.len) return true;
    for (int i = 0; i < c->nfields; i++) if (c->fields[i].decos.len) return true;
    for (int i = 0; i < c->nmethods; i++) {
        if (c->methods[i]->decos.len) return true;
        for (int k = 0; k < c->methods[i]->nparams; k++) if (c->methods[i]->params[k].decos.len) return true;
    }
    if (c->ctor) for (int k = 0; k < c->ctor->nparams; k++) if (c->ctor->params[k].decos.len) return true;
    return false;
}
static void tinfo(Buf *b, const char *t) {
    buf_printf(b, "{ kind: __reflect_kind(%s), id: __reflect_typeid(%s), decode: __reflect_decoder(%s), encode: __reflect_encoder(%s), schema: __reflect_schema(%s) }", t, t, t, t, t);
}
static void params_info(Buf *b, Func *f) {
    buf_puts(b, "[");
    for (int i = 0; f && i < f->nparams; i++) {
        buf_printf(b, "%s{ name: \"%s\", index: %d, type: ", i ? ", " : "", f->params[i].name, i);
        tinfo(b, f->params[i].type_src);
        buf_puts(b, " }");
    }
    buf_puts(b, "]");
}
static void call_args(Buf *b, Func *f) {
    for (int i = 0; f && i < f->nparams; i++) buf_printf(b, "%sargs[%d] as %s", i ? ", " : "", i, f->params[i].type_src);
}
static int ndeco;
static void apply(Buf *b, Decorator *d, const char *target) {
    int n = ndeco++;
    buf_printf(b, "const __deco%d = %s;\n__deco%d(%s);\n", n, d->src, n, target);
}
static void reflect_class(Buf *g, Module *m, Class *c) {
    const char *ci_name = xs_sprintf("__ci_%s", c->name);
    buf_printf(g, "const %s: ClassInfo = {\n  id: \"%s#%s\",\n  name: \"%s\",\n  params: ", ci_name, xs_display_path(m->file), c->name, c->name);
    params_info(g, c->ctor);
    bool data_only = false; /* a DTO (fields without values, no constructor): no `new`, JSON makes it */
    for (int k = 0; k < c->nfields && !c->ctor; k++) if (!c->fields[k].init && !c->fields[k].optional) data_only = true;
    if (data_only) {
        buf_printf(g, ",\n  construct: (args: unknown[]): unknown => { throw new Error(\"%s is filled from JSON, not constructed\"); },\n  methods: [", c->name);
    } else {
        buf_printf(g, ",\n  construct: (args: unknown[]): unknown => new %s(", c->name);
        call_args(g, c->ctor);
        buf_puts(g, "),\n  methods: [");
    }
    for (int k = 0; k < c->nmethods; k++) {
        Func *f = c->methods[k];
        bool is_void = strcmp(f->ret_src, "void") == 0 || strcmp(f->ret_src, "Promise<void>") == 0;
        buf_printf(g, "%s\n    { name: \"%s\", params: ", k ? "," : "", f->name);
        params_info(g, f);
        buf_puts(g, ", returns: ");
        if (is_void) tinfo(g, "void"); else tinfo(g, f->ret_src);
        buf_printf(g, ",\n      invoke: (self: unknown, args: unknown[]): unknown => { %s(self as %s).%s(", is_void ? "" : "return ", c->name, f->name);
        call_args(g, f);
        buf_printf(g, ");%s } }", is_void ? " return null;" : "");
    }
    buf_puts(g, "],\n  fields: [");
    for (int k = 0; k < c->nfields; k++) {
        Field *fd = &c->fields[k];
        buf_printf(g, "%s\n    { name: \"%s\", optional: %s, type: ", k ? "," : "", fd->name, fd->optional ? "true" : "false");
        tinfo(g, fd->type_src ? fd->type_src : "unknown");
        buf_printf(g, ", get: (o: unknown): unknown => (o as %s).%s }", c->name, fd->name);
    }
    buf_puts(g, "],\n};\n");
    for (int k = 0; k < c->nmethods; k++) {
        Func *f = c->methods[k];
        for (int i = f->nparams - 1; i >= 0; i--)
            for (int d = f->params[i].decos.len - 1; d >= 0; d--)
                apply(g, &f->params[i].decos.items[d], xs_sprintf("%s, %s.methods[%d], %d", ci_name, ci_name, k, i));
        for (int d = f->decos.len - 1; d >= 0; d--) apply(g, &f->decos.items[d], xs_sprintf("%s, %s.methods[%d]", ci_name, ci_name, k));
    }
    for (int k = 0; k < c->nfields; k++)
        for (int d = c->fields[k].decos.len - 1; d >= 0; d--) apply(g, &c->fields[k].decos.items[d], xs_sprintf("%s, %s.fields[%d]", ci_name, ci_name, k));
    if (c->ctor)
        for (int i = c->ctor->nparams - 1; i >= 0; i--)
            for (int d = c->ctor->params[i].decos.len - 1; d >= 0; d--)
                apply(g, &c->ctor->params[i].decos.items[d], xs_sprintf("%s, null, %d", ci_name, i));
    for (int d = c->decos.len - 1; d >= 0; d--) apply(g, &c->decos.items[d], ci_name);
}
/* Each decorated class's ClassInfo and decorator calls run where the class is declared, as in TypeScript:
   just before the first top-level statement that follows it. */
static void reflect_module(Program *prog, Module *m, int top_start) {
    bool any = false;
    for (int ci = 0; ci < prog->nclasses && !any; ci++) {
        Class *c = prog->classes[ci];
        any = c->mod == m && !c->builtin && !c->is_interface && class_decorated(c);
    }
    if (!any) return;
    /* the generated code names ClassInfo: an import of xisto:reflect into the module */
    Import im = {0};
    im.path = "xisto:reflect";
    static char *names[] = {"ClassInfo"};
    im.names = names;
    im.positions = xs_alloc(sizeof(Pos));
    im.n = 1;
    im.from = load_module(prog, "xisto:reflect", (Pos){1, 1}, m->path);
    m->imports = realloc(m->imports, sizeof(Import) * (size_t)(m->nimports + 1));
    m->imports[m->nimports++] = im;
    xs_file_name = m->file;
    for (int ci = 0; ci < prog->nclasses; ci++) {
        Class *c = prog->classes[ci];
        if (c->mod != m || c->builtin || c->is_interface || !class_decorated(c)) continue;
        Buf g = {0};
        reflect_class(&g, m, c);
        if (getenv("XISTO_SHOW_REFLECT")) fprintf(stderr, "/* reflection of %s in %s */\n%s\n", c->name, m->file, g.data);
        const char *saved_src = xs_source;
        xs_source = g.data;
        Parser gp = {0};
        lexer_init(&gp.lx, g.data);
        gp.prog = prog;
        gp.mod = m;
        next(&gp);
        int before = prog->top.len;
        while (!at(&gp, TK_EOF)) {
            Stmt *st = parse_stmt(&gp);
            st->mod = m;
            st->pos = c->pos; /* errors in generated code point at the class */
            st->generated = true;
            stmt_list_push(&prog->top, st);
        }
        xs_source = saved_src;
        int ng = prog->top.len - before;
        int at_i = before;
        for (int k = top_start; k < before; k++)
            if (prog->top.items[k]->mod == m && prog->top.items[k]->pos.line > c->pos.line) { at_i = k; break; }
        Stmt **gen = xs_alloc(sizeof(Stmt *) * (size_t)(ng ? ng : 1));
        memcpy(gen, prog->top.items + before, sizeof(Stmt *) * (size_t)ng);
        memmove(prog->top.items + at_i + ng, prog->top.items + at_i, sizeof(Stmt *) * (size_t)(before - at_i));
        memcpy(prog->top.items + at_i, gen, sizeof(Stmt *) * (size_t)ng);
    }
}

static void parse_module(Program *prog, Module *m) {
    xs_file_name = m->file;
    xs_source = m->src;
    Parser p = {0};
    lexer_init(&p.lx, m->src);
    p.prog = prog;
    p.mod = m;
    next(&p);
    /* imports first: load dependencies before this module's own declarations are recorded */
    while (at(&p, TK_IMPORT)) parse_import(&p, m);
    for (int i = 0; i < m->nimports; i++) {
        Import *im = &m->imports[i];
        im->from = load_module(prog, im->path, im->pos, m->path);
        xs_file_name = m->file;
        xs_source = m->src;
    }
    parsing_prog = prog;
    parsing_mod = m;
    int top_start = prog->top.len;
    while (!at(&p, TK_EOF)) {
        DecoList decos = {0};
        parse_decorators(&p, &decos);
        bool exported = accept(&p, TK_EXPORT);
        bool is_default = exported && at(&p, TK_DEFAULT) && (next(&p), true);
        bool is_declare = at_ident(&p, "declare") && (next(&p), true);
        if (is_declare) { /* a native package's binding: no body here, the C is in the package */
            if (!m->native_prefix) xs_error(p.tok.pos, "declare functions and classes in a package with C sources (xisto.json \"native\")", "'declare' outside a native package");
            if (at(&p, TK_FUNCTION)) {
                Pos pos = p.tok.pos;
                next(&p);
                Func *f = xs_alloc(sizeof *f);
                f->pos = pos;
                f->name = expect(&p, TK_IDENT, NULL).str;
                parse_params(&p, f);
                expect(&p, TK_COLON, "a declared function states its return type");
                f->ret = parse_type(&p);
                expect_semi(&p);
                f->is_native = true;
                f->cname = xs_sprintf("%s%s", m->native_prefix, f->name);
                f->mod = m;
                f->exported = exported;
                f->is_default = is_default;
                add_func(prog, f);
                continue;
            }
            if (!at(&p, TK_CLASS)) xs_error(p.tok.pos, NULL, "expected 'function' or 'class' after 'declare'");
            Class *c = parse_declared_class(&p, m->native_prefix);
            c->mod = m;
            c->exported = exported;
            add_class(prog, c);
            continue;
        }
        if (decos.len && !at(&p, TK_CLASS)) xs_error(decos.items[0].pos, NULL, "decorators apply to classes, their members and parameters");
        if (is_default && !at(&p, TK_FUNCTION) && !at(&p, TK_ASYNC)) xs_error(p.tok.pos, NULL, "'export default' applies to functions only");
        if (at(&p, TK_IMPORT)) xs_error(p.tok.pos, "put every import at the top of the file", "import after declarations");
        if (at(&p, TK_FUNCTION) || at(&p, TK_ASYNC)) {
            Pos pos = p.tok.pos;
            bool is_async = accept(&p, TK_ASYNC);
            expect(&p, TK_FUNCTION, NULL);
            Func *f = parse_function_rest(&p, pos, is_async);
            f->mod = m;
            f->exported = exported;
            f->is_default = is_default;
            add_func(prog, f);
        } else if (at(&p, TK_CLASS) || (at_ident(&p, "abstract") && peek_is(&p, TK_CLASS))) {
            bool abstract = at_ident(&p, "abstract");
            if (abstract) next(&p);
            Class *c = parse_class_ex(&p, abstract);
            c->decos = decos;
            c->mod = m;
            c->exported = exported;
            add_class(prog, c);
            add_statics(prog, m, c);
        } else if (at_ident(&p, "interface") || (at_ident(&p, "type") && peek_is_ident(&p))) {
            Class *c = parse_interface(&p);
            c->mod = m;
            c->exported = exported;
            add_class(prog, c);
        } else if (at(&p, TK_ENUM)) {
            Enum *en = parse_enum(&p);
            en->mod = m;
            en->exported = exported;
            if (prog->nenums == prog->cap_enums) {
                prog->cap_enums = prog->cap_enums ? prog->cap_enums * 2 : 4;
                prog->enums = realloc(prog->enums, sizeof(Enum *) * (size_t)prog->cap_enums);
            }
            prog->enums[prog->nenums++] = en;
        } else {
            if (exported && !at(&p, TK_LET) && !at(&p, TK_CONST))
                xs_error(p.tok.pos, "export applies to function, class, let and const", "expected a declaration after 'export'");
            Stmt *st = parse_stmt(&p);
            st->mod = m;
            st->exported = exported;
            stmt_list_push(&prog->top, st);
            drain_pending(&p, &prog->top, m, exported);
        }
    }
    reflect_module(prog, m, top_start);
}

static Func *builtin_func(Module *m, const char *name, const char *cfn, Type *ret, int nparams, Type *p1, Type *p2) {
    Func *f = xs_alloc(sizeof *f);
    f->name = (char *)name;
    f->cname = (char *)cfn;
    f->ret = ret;
    f->nparams = nparams;
    f->params = xs_alloc(sizeof(Param) * 2);
    f->params[0].name = "path";
    f->params[0].type = p1;
    f->params[1].name = "data";
    f->params[1].type = p2;
    f->mod = m;
    f->exported = true;
    return f;
}

/* Built-in modules implemented by the runtime. Signatures follow Node's, restricted to strings. */
/* xisto:reflect: the descriptors the compiler builds for decorated classes (see "static reflection") */
static const char xs_reflect_src[] =
    "// What a type is, and how to read and write a value of it as JSON.\n"
    "export interface TypeInfo {\n"
    "  kind: string; // \"string\", \"int\", \"number\", \"boolean\", \"class\", \"interface\", \"array\", \"enum\", \"void\", \"other\"\n"
    "  id: string; // classes and interfaces: \"<file>#<Name>\"; otherwise the type as written\n"
    "  decode: (json: string) => unknown;\n"
    "  encode: (value: unknown) => string;\n"
    "  schema: string; // the type's shape as JSON Schema (OpenAPI 3.0 dialect), named object types in $defs, one per line\n"
    "}\n"
    "export interface ParamInfo {\n  name: string;\n  index: int;\n  type: TypeInfo;\n}\n"
    "export interface MethodInfo {\n  name: string;\n  params: ParamInfo[];\n  returns: TypeInfo;\n"
    "  invoke: (self: unknown, args: unknown[]) => unknown;\n}\n"
    "export interface FieldInfo {\n  name: string;\n  optional: boolean;\n  type: TypeInfo;\n  get: (target: unknown) => unknown;\n}\n"
    "export interface ClassInfo {\n  id: string;\n  name: string;\n  params: ParamInfo[];\n"
    "  construct: (args: unknown[]) => unknown;\n  methods: MethodInfo[];\n  fields: FieldInfo[];\n}\n";
/* xisto:worker_threads: Node's contract restricted to strings (workerData and messages are text: JSON.stringify
   on one side, a typed JSON.parse on the other, as Node runs it too). Worker and MessagePort are built-in
   classes; these are the per-thread values, from the runtime (xisto_net.h, "worker threads") */
static const char xs_worker_threads_src[] =
    "declare function wt_is_main(): boolean;\n"
    "declare function wt_data(): string;\n"
    "declare function wt_port(): MessagePort;\n"
    "declare function wt_id(): int;\n"
    "export const isMainThread: boolean = wt_is_main();\n"
    "export const workerData: string = wt_data();\n"
    "export const parentPort: MessagePort = wt_port();\n"
    "export const threadId: int = wt_id();\n";
/* xisto:os: what a program needs to size its workers */
static const char xs_os_src[] =
    "declare function os_parallelism(): int;\n"
    "export function availableParallelism(): int {\n  return os_parallelism();\n}\n"
    "export class CpuInfo {\n  model: string = \"\";\n  speed: int = 0;\n}\n"
    "export function cpus(): CpuInfo[] {\n  const r: CpuInfo[] = [];\n  for (let i = 0; i < os_parallelism(); i++) r.push(new CpuInfo());\n  return r;\n}\n";
static void parse_module(Program *prog, Module *m);

/* xisto:fs is the canonical name of a built-in module; node:fs (and the bare fs, sqlite) only in Node
   compatibility mode: these are xisto's implementations of the Node contracts, not Node itself. */
static Module *load_builtin_module(Program *prog, const char *spec, Pos from_pos) {
    bool node = strncmp(spec, "node:", 5) == 0 || strcmp(spec, "fs") == 0 || strcmp(spec, "sqlite") == 0;
    if (node && !xs_node_compat)
        xs_error(from_pos, xs_sprintf("import it as \"xisto:%s\", or build with --node-compat (XISTO_NODE_COMPAT=1) to keep the Node name",
                                      strncmp(spec, "node:", 5) == 0 ? spec + 5 : spec),
                 "'%s' is xisto's implementation of a Node module, not Node itself", spec);
    const char *name = strncmp(spec, "node:", 5) == 0 ? spec + 5 : strncmp(spec, "xisto:", 6) == 0 ? spec + 6 : spec;
    for (int i = 0; i < prog->nmodules; i++)
        if (strcmp(prog->modules[i]->path, xs_sprintf("builtin:%s", name)) == 0) return prog->modules[i];
    Module *m = xs_alloc(sizeof *m);
    m->builtin = true;
    m->file = (char *)name;
    m->path = xs_sprintf("builtin:%s", name);
    m->src = "";
    m->loaded = true;
    if (prog->nmodules == prog->cap_modules) {
        prog->cap_modules = prog->cap_modules ? prog->cap_modules * 2 : 4;
        prog->modules = realloc(prog->modules, sizeof(Module *) * (size_t)prog->cap_modules);
    }
    m->id = prog->nmodules;
    prog->modules[prog->nmodules++] = m;
    if (strcmp(name, "fs") == 0) {
        add_func(prog, builtin_func(m, "readFileSync", "xs_fs_read", ty_string, 2, ty_string, ty_string));
        add_func(prog, builtin_func(m, "writeFileSync", "xs_fs_write", ty_void, 2, ty_string, ty_string));
        add_func(prog, builtin_func(m, "appendFileSync", "xs_fs_append", ty_void, 2, ty_string, ty_string));
        add_func(prog, builtin_func(m, "existsSync", "xs_fs_exists", ty_boolean, 1, ty_string, NULL));
        add_func(prog, builtin_func(m, "unlinkSync", "xs_fs_unlink", ty_void, 1, ty_string, NULL));
        /* file descriptors (checker: check_fs_fd_call fixes the arities and types) */
        add_func(prog, builtin_func(m, "openSync", "xs_fs_open", ty_int, 2, ty_string, ty_string));
        add_func(prog, builtin_func(m, "closeSync", "xs_fs_close", ty_void, 1, ty_int, NULL));
        add_func(prog, builtin_func(m, "fstatSync", "xs_fs_fstat", ty_int, 1, ty_int, NULL));
        add_func(prog, builtin_func(m, "readSync", "xs_fs_read_sync", ty_int, 2, ty_int, ty_int));
        return m;
    }
    if (strcmp(name, "sqlite") == 0) return m; /* DatabaseSync and StatementSync are built-in classes */
    if (strcmp(name, "timers/promises") == 0) {
        Func *f = builtin_func(m, "setTimeout", "xs_sleep_ms", ty_void, 1, ty_int, NULL);
        f->params[0].name = "ms";
        add_func(prog, f);
        return m;
    }
    if (strcmp(name, "reflect") == 0) { /* xisto:reflect: written in xisto, parsed like any module */
        m->builtin = false;
        m->file = "xisto:reflect";
        m->src = (char *)xs_reflect_src;
        m->loading = true;
        parse_module(prog, m);
        m->loading = false;
        return m;
    }
    if (strcmp(name, "worker_threads") == 0 || strcmp(name, "os") == 0) { /* written in xisto over runtime functions */
        bool wt = strcmp(name, "worker_threads") == 0;
        m->builtin = false;
        m->file = wt ? "xisto:worker_threads" : "xisto:os";
        m->src = (char *)(wt ? xs_worker_threads_src : xs_os_src);
        m->native_prefix = "xs_";
        m->loading = true;
        parse_module(prog, m);
        m->loading = false;
        if (wt) prog->uses_workers = true;
        return m;
    }
    if (strcmp(name, "child_process") == 0) {
        Func *f = builtin_func(m, "execSync", "xs_exec_sync", ty_string, 1, ty_string, NULL);
        f->params[0].name = "command";
        add_func(prog, f);
        return m;
    }
    xs_error(from_pos, "built-in modules: xisto:fs, xisto:sqlite, xisto:timers/promises, xisto:child_process, xisto:crypto, xisto:worker_threads, xisto:os", "unknown built-in module '%s'", spec);
    return NULL;
}

/* Resolves "./x" or "./x.ts" relative to the importing file, loads it once, dependencies first. */
static Module *load_module(Program *prog, const char *file, Pos from_pos, const char *from_file) {
    char path[PATH_MAX];
    pending_prefix = NULL;
    if (from_file && (strncmp(file, "node:", 5) == 0 || strncmp(file, "xisto:", 6) == 0 || strcmp(file, "fs") == 0 || strcmp(file, "sqlite") == 0))
        return load_builtin_module(prog, file, from_pos);
    if (from_file && file[0] == '.') {
        char dir[PATH_MAX];
        strncpy(dir, from_file, sizeof dir - 1);
        char *slash = strrchr(dir, '/');
        if (slash) *slash = 0; else strcpy(dir, ".");
        snprintf(path, sizeof path, "%s/%s", dir, file);
    } else if (from_file) {
        /* a package: <project>/.xisto/pkgs/<name>/, entry from its xisto.json "main", else index.ts or src/index.ts */
        char dir[PATH_MAX];
        strncpy(dir, from_file, sizeof dir - 1);
        dir[sizeof dir - 1] = 0;
        char *found = NULL;
        bool has_project = false;
        for (;;) {
            char *slash = strrchr(dir, '/');
            if (!slash) break;
            *slash = 0;
            char probe[PATH_MAX];
            snprintf(probe, sizeof probe, "%s/.xisto/pkgs/%s", dir, file);
            if (access(probe, R_OK) == 0) { found = xs_strndup(probe, strlen(probe)); break; }
            snprintf(probe, sizeof probe, "%s/xisto.json", dir);
            if (access(probe, R_OK) == 0) has_project = true;
        }
        if (!found && prog->nmodules > 0 && prog->modules[0]->path) {
            /* a package importing another package: the project's .xisto/pkgs, where the application installed both */
            strncpy(dir, prog->modules[0]->path, sizeof dir - 1);
            for (;;) {
                char *slash = strrchr(dir, '/');
                if (!slash) break;
                *slash = 0;
                char probe[PATH_MAX];
                snprintf(probe, sizeof probe, "%s/.xisto/pkgs/%s", dir, file);
                if (access(probe, R_OK) == 0) { found = xs_strndup(probe, strlen(probe)); break; }
            }
        }
        if (!found && has_project) xs_error(from_pos, xs_sprintf("install it: xisto add <git url or path> --name %s", file), "package '%s' is not installed", file);
        if (!found) xs_error(from_pos, xs_sprintf("import files with a relative path (\"./file\"), or add the package: xisto add <git url> --name %s", file), "cannot resolve '%s'", file);
        char *entry = NULL;
        char *meta = xs_read_file(xs_sprintf("%s/xisto.json", found));
        if (meta) {
            char *m = strstr(meta, "\"main\"");
            if (m && (m = strchr(m + 6, '"'))) {
                char *e = strchr(m + 1, '"');
                if (e) entry = xs_sprintf("%s/%.*s", found, (int)(e - m - 1), m + 1);
            }
        }
        if (!entry) entry = access(xs_sprintf("%s/index.ts", found), R_OK) == 0 ? xs_sprintf("%s/index.ts", found) : xs_sprintf("%s/src/index.ts", found);
        if (meta) package_native(prog, found, file, meta);
        strncpy(path, entry, sizeof path - 1);
        path[sizeof path - 1] = 0;
    } else {
        strncpy(path, file, sizeof path - 1);
    }
    size_t n = strlen(path);
    if (access(path, R_OK) != 0 && !(n > 3 && strcmp(path + n - 3, ".ts") == 0)) strncat(path, ".ts", sizeof path - n - 1);
    char real[PATH_MAX];
    if (!xs_real_path(path, real)) {
        if (from_file) xs_error(from_pos, NULL, "cannot find module '%s' (looked for %s)", file, path);
        fprintf(stderr, "xisto: cannot open '%s'\n", file);
        exit(1);
    }
    for (int i = 0; i < prog->nmodules; i++) {
        Module *m = prog->modules[i];
        if (strcmp(m->path, real) == 0) {
            if (m->loading) { /* every module still loading, from the one that closes the cycle to this one, is part of it */
                Buf chain = {0};
                bool in = false;
                for (int k = 0; k < prog->nmodules; k++) {
                    Module *x = prog->modules[k];
                    if (x == m) in = true;
                    if (in && x->loading) { const char *b = strrchr(x->file, '/'); buf_printf(&chain, "%s -> ", b ? b + 1 : x->file); }
                }
                { const char *b = strrchr(m->file, '/'); buf_puts(&chain, b ? b + 1 : m->file); }
                xs_error(from_pos, xs_sprintf("the cycle is %s; move the shared code into a third file that both import", chain.data), "circular import of '%s'", file);
            }
            return m;
        }
    }
    Module *m = xs_alloc(sizeof *m);
    m->native_prefix = (char *)pending_prefix;
    pending_prefix = NULL;
    m->path = xs_strndup(real, strlen(real));
    m->file = from_file ? xs_sprintf("%s", path) : (char *)file;
    m->src = xs_read_file(real);
    if (!m->src) xs_error(from_pos, NULL, "cannot read '%s'", path);
    m->loading = true;
    if (prog->nmodules == prog->cap_modules) {
        prog->cap_modules = prog->cap_modules ? prog->cap_modules * 2 : 4;
        prog->modules = realloc(prog->modules, sizeof(Module *) * (size_t)prog->cap_modules);
    }
    m->id = prog->nmodules;
    prog->modules[prog->nmodules++] = m; /* registered before parsing so cycles are detected */
    parse_module(prog, m);
    m->loading = false;
    m->loaded = true;
    /* move the module to the end so the order is "dependencies first": its imports were appended during parse_module */
    for (int i = 0; i < prog->nmodules; i++)
        if (prog->modules[i] == m) {
            memmove(&prog->modules[i], &prog->modules[i + 1], sizeof(Module *) * (size_t)(prog->nmodules - i - 1));
            prog->modules[prog->nmodules - 1] = m;
            break;
        }
    return m;
}

Program *parse_program(const char *entry_file) {
    Program *prog = xs_alloc(sizeof *prog);
    load_module(prog, entry_file, (Pos){0, 0}, NULL);
    /* top-level statements were appended in parse order (entry first); reorder by module order */
    StmtList ordered = {0};
    for (int mi = 0; mi < prog->nmodules; mi++)
        for (int i = 0; i < prog->top.len; i++)
            if (prog->top.items[i]->mod == prog->modules[mi]) stmt_list_push(&ordered, prog->top.items[i]);
    prog->top = ordered;
    return prog;
}
