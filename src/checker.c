#include "xisto.h"
#define XR_CORE_ONLY 1
#include "../runtime/xisto_regex.h" /* patterns of /literals/ are compiled here once, to report their errors */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>

typedef struct Binding {
    char *name;
    Type *type;
    Pos pos;
    bool used;
    bool no_warn;    /* parameters, globals, catch variables */
    struct Binding *origin; /* copies (narrowing, captures, imports) point at the declaration */
    Type *orig;      /* when narrowed from T | null: the declared type */
    bool is_const;
    bool is_param;
    char *cname;
    char *cell;      /* lives in a heap cell (captured by a closure and reassigned): cname is (cell->v) */
    char *plain;     /* the C name before it moved into a cell (a parameter keeps it in the signature) */
    bool union_narrow; /* a union variable seen as one of its members (or fewer of them): read through a conversion */
    bool poisoned;   /* its declaration failed (already reported): a use stops checking that statement quietly */
    bool inferred_int; /* `let i = 0` taken as an int (XB-034): becomes a number by itself where a number goes */
    struct Binding *next;
} Binding;

typedef struct Scope {
    Binding *head;
    struct Scope *parent;
    Arrow *arrow;    /* set on the boundary scope of an arrow function body */
} Scope;

static Program *prog;
static Scope *scope;
static Func *cur_func;   /* NULL at top level */
static Class *cur_class; /* class whose method/constructor is being checked */
static Module *cur_mod;  /* module whose code is being checked */
static int loop_depth, switch_depth, finally_depth;
static Scope **mod_scopes; /* one top-level scope per module */
static int tmp_counter;

/* name -> declarations, so lookups do not scan every function of every module */
typedef struct SymEntry { const char *name; void *decl; struct SymEntry *next; } SymEntry;
typedef struct { SymEntry **buckets; size_t nb; } SymTable;
static SymTable funcs_by_name, classes_by_name, enums_by_name;

static uint64_t sym_hash(const char *s) {
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ull; }
    return h;
}

static void sym_add(SymTable *t, const char *name, void *decl) {
    if (!t->buckets) { t->nb = 4096; t->buckets = xs_alloc(sizeof(SymEntry *) * t->nb); }
    size_t i = sym_hash(name) % t->nb;
    SymEntry *e = xs_alloc(sizeof *e);
    e->name = name;
    e->decl = decl;
    e->next = t->buckets[i];
    t->buckets[i] = e;
}

#define SYM_EACH(t, key_, var) for (SymEntry *var = (t).buckets ? (t).buckets[sym_hash(key_) % (t).nb] : NULL; var; var = var->next) if (strcmp(var->name, (key_)) == 0)

static void enter_module(Module *m) {
    cur_mod = m;
    scope = mod_scopes[m->id];
    xs_file_name = m->file;
    xs_source = m->src;
}

/* True when `name` is imported by the current module from `from`. */
static bool imports_name(Module *m, Module *from, const char *name) {
    if (!m) return false;
    for (int i = 0; i < m->nimports; i++)
        if (m->imports[i].from == from)
            for (int k = 0; k < m->imports[i].n; k++)
                if (strcmp(m->imports[i].names[k], name) == 0) return true;
    return false;
}

/* ---------- built-in classes ---------- */

static Class **builtins;
static int nbuiltins;

static Class *bi_class(const char *name, const char *cname) {
    Class *c = xs_alloc(sizeof *c);
    c->name = (char *)name;
    c->cname = (char *)cname;
    c->builtin = true;
    builtins = realloc(builtins, sizeof(Class *) * (size_t)(nbuiltins + 1));
    builtins[nbuiltins++] = c;
    return c;
}

static void bi_field(Class *c, const char *name, Type *type) {
    c->fields = realloc(c->fields, sizeof(Field) * (size_t)(c->nfields + 1));
    Field f = {0};
    f.name = (char *)name;
    f.type = type;
    f.cname = xs_sprintf("f_%s", name);
    c->fields[c->nfields++] = f;
}

static Func *bi_func(const char *name, const char *cfn, Type *ret, int nparams, va_list ap) {
    Func *f = xs_alloc(sizeof *f);
    f->name = (char *)name;
    f->cname = (char *)cfn;
    f->ret = ret;
    f->nparams = nparams;
    f->params = xs_alloc(sizeof(Param) * (size_t)(nparams ? nparams : 1));
    for (int i = 0; i < nparams; i++) {
        f->params[i].name = xs_sprintf("arg%d", i + 1);
        f->params[i].type = va_arg(ap, Type *);
    }
    return f;
}

static void bi_method(Class *c, const char *name, const char *cfn, Type *ret, int nparams, ...) {
    va_list ap;
    va_start(ap, nparams);
    Func *f = bi_func(name, cfn, ret, nparams, ap);
    va_end(ap);
    f->cls = c;
    c->methods = realloc(c->methods, sizeof(Func *) * (size_t)(c->nmethods + 1));
    c->methods[c->nmethods++] = f;
}

static void bi_static(Class *c, const char *name, const char *cfn, Type *ret, int nparams, ...) {
    va_list ap;
    va_start(ap, nparams);
    Func *f = bi_func(name, cfn, ret, nparams, ap);
    va_end(ap);
    f->cls = c;
    c->statics = realloc(c->statics, sizeof(Func *) * (size_t)(c->nstatics + 1));
    c->statics[c->nstatics++] = f;
}

static void init_builtins(void) {
    if (nbuiltins) return;
    /* fetch() result, named after the Fetch API so the same file runs on Node and Bun */
    Class *resp = bi_class("Response", "xs_Response");
    bi_field(resp, "status", ty_int);
    bi_field(resp, "ok", ty_boolean);
    bi_method(resp, "text", "xs_Response_text", ty_string, 0);

    Class *req = bi_class("HttpRequest", "xs_HttpRequest");
    bi_field(req, "method", ty_string);
    bi_field(req, "path", ty_string);
    bi_field(req, "body", ty_string);
    bi_method(req, "query", "xs_HttpRequest_query", ty_string, 1, ty_string);
    bi_method(req, "param", "xs_HttpRequest_param", ty_string, 1, ty_string);
    bi_method(req, "header", "xs_HttpRequest_header", ty_string, 1, ty_string);

    Class *res = bi_class("HttpResponse", "xs_HttpResponse");
    bi_field(res, "status", ty_int);
    bi_field(res, "body", ty_string);
    bi_static(res, "text", "xs_HttpResponse_text", ty_object_of(res), 1, ty_string);
    bi_static(res, "json", "xs_HttpResponse_json", ty_object_of(res), 1, ty_string);
    bi_static(res, "status", "xs_HttpResponse_status", ty_object_of(res), 2, ty_int, ty_string);
    bi_static(res, "of", "xs_HttpResponse_of", ty_object_of(res), 3, ty_int, ty_string, ty_string);
    bi_method(res, "header", "xs_HttpResponse_header", ty_object_of(res), 2, ty_string, ty_string);
    bi_static(res, "stream", "xs_HttpResponse_stream", ty_object_of(res), 1, ty_stream_of(ty_string));

    /* node:sqlite */
    Class *db = bi_class("DatabaseSync", "xs_DatabaseSync");
    Class *stmt = bi_class("StatementSync", "xs_StatementSync");
    Class *rr = bi_class("RunResult", "xs_RunResult");
    bi_field(rr, "changes", ty_int);
    bi_field(rr, "lastInsertRowid", ty_int);
    bi_method(db, "exec", "xs_DatabaseSync_exec", ty_void, 1, ty_string);
    bi_method(db, "prepare", "xs_DatabaseSync_prepare", ty_object_of(stmt), 1, ty_string);
    bi_method(db, "close", "xs_DatabaseSync_close", ty_void, 0);
    (void)stmt;

    Class *err = bi_class("Error", "xs_Error");
    bi_field(err, "message", ty_string);
    bi_field(err, "name", ty_string);
    bi_field(err, "cause", ty_unknown); /* an object thrown with throw: the Error carries it */

    bi_class("Date", "xs_Date"); /* members: check_date_member */
    Class *bigc = bi_class("bigint", "xs_big"); /* BigInt values: the operators become calls (big_binary) */
    bi_method(bigc, "toString", "xs_big_to_string", ty_string, 0);
    /* bytes (runtime/xisto_rt.h): Uint8Array, and Buffer, the same object printed the Node way; members: check_bytes_member */
    Class *u8 = bi_class("Uint8Array", "xs_Uint8Array");
    bi_field(u8, "length", ty_int);
    Class *buf = bi_class("Buffer", "xs_Uint8Array");
    bi_field(buf, "length", ty_int);
    buf->base = u8;
    buf->nbase_fields = 1;
    /* shared memory (runtime/xisto_rt.h): SharedArrayBuffer, and Float64Array on one or on memory of its own;
       members: check_f64_member */
    Class *sab = bi_class("SharedArrayBuffer", "xs_SharedArrayBuffer");
    bi_field(sab, "byteLength", ty_int);
    Class *f64 = bi_class("Float64Array", "xs_Float64Array");
    bi_field(f64, "length", ty_int);
    Class *tenc = bi_class("TextEncoder", "xs_TextEncoder");
    bi_method(tenc, "encode", "xs_TextEncoder_encode", ty_object_of(u8), 1, ty_string);
    Class *tdec = bi_class("TextDecoder", "xs_TextEncoder");
    bi_method(tdec, "decode", "xs_TextDecoder_decode", ty_string, 1, ty_object_of(u8));
    /* RegExp (runtime/xisto_regex.h): linear time, no backtracking */
    Class *rgroups = bi_class("RegExpGroups", "xs_RegExpGroups");
    Class *rmatch = bi_class("RegExpExecArray", "xs_RegExpExecArray");
    bi_field(rmatch, "index", ty_int);
    bi_field(rmatch, "input", ty_string);
    bi_field(rmatch, "length", ty_int);
    bi_field(rmatch, "groups", ty_nullable_of(ty_object_of(rgroups)));
    bi_method(rmatch, "join", "xs_RegExpExecArray_join", ty_string, 1, ty_string);
    Class *regexp = bi_class("RegExp", "xs_RegExp");
    bi_field(regexp, "source", ty_string);
    bi_field(regexp, "flags", ty_string);
    bi_field(regexp, "lastIndex", ty_int);
    bi_field(regexp, "global", ty_boolean);
    bi_field(regexp, "ignoreCase", ty_boolean);
    bi_field(regexp, "multiline", ty_boolean);
    bi_field(regexp, "sticky", ty_boolean);
    bi_field(regexp, "dotAll", ty_boolean);
    bi_field(regexp, "unicode", ty_boolean);
    bi_field(regexp, "hasIndices", ty_boolean);
    bi_method(regexp, "test", "xs_RegExp_test", ty_boolean, 1, ty_string);
    bi_method(regexp, "exec", "xs_RegExp_exec", ty_nullable_of(ty_object_of(rmatch)), 1, ty_string);
    bi_method(regexp, "toString", "xs_RegExp_toString", ty_string, 0);

    /* node:worker_threads (runtime/xisto_net.h): text messages between threads */
    Class *mport = bi_class("MessagePort", "xs_MessagePort");
    Class *wkr = bi_class("Worker", "xs_Worker");
    {
        Type **lp = xs_alloc(sizeof(Type *));
        lp[0] = ty_string;
        Type *listener = ty_func(lp, 1, ty_void);
        bi_method(wkr, "on", "xs_Worker_on", ty_object_of(wkr), 2, ty_string, listener);
        bi_method(wkr, "postMessage", "xs_Worker_postMessage", ty_void, 1, ty_string);
        bi_method(wkr, "terminate", "xs_Worker_terminate", ty_void, 0); /* (Node returns a Promise of the exit code) */
        bi_method(mport, "on", "xs_MessagePort_on", ty_object_of(mport), 2, ty_string, listener);
        bi_method(mport, "postMessage", "xs_MessagePort_postMessage", ty_void, 1, ty_string);
    }

    Class *srv = bi_class("Server", "xs_Server");
    Type **hp = xs_alloc(sizeof(Type *));
    hp[0] = ty_object_of(req);
    Type *handler = ty_func(hp, 1, ty_object_of(res));
    bi_method(srv, "get", "xs_Server_get", ty_void, 2, ty_string, handler);
    bi_method(srv, "post", "xs_Server_post", ty_void, 2, ty_string, handler);
    bi_method(srv, "put", "xs_Server_put", ty_void, 2, ty_string, handler);
    bi_method(srv, "delete", "xs_Server_delete", ty_void, 2, ty_string, handler);
    bi_method(srv, "listen", "xs_Server_listen", ty_void, 1, ty_int);
}

static Enum *find_enum(const char *name) {
    SYM_EACH(enums_by_name, name, e) {
        Enum *en = e->decl;
        if (en->mod == cur_mod || (en->exported && imports_name(cur_mod, en->mod, name))) return en;
    }
    return NULL;
}

/* m imports name from xisto:worker_threads (Worker and MessagePort are built-in classes visible only there) */
static bool wt_imports(Module *m, const char *name) {
    if (!m) return false;
    if (m->file && strcmp(m->file, "xisto:worker_threads") == 0) return true; /* (the module itself) */
    for (int i = 0; i < m->nimports; i++) {
        Import *im = &m->imports[i];
        if (!im->from || !im->from->file || strcmp(im->from->file, "xisto:worker_threads") != 0) continue;
        for (int k = 0; k < im->n; k++) if (strcmp(im->names[k], name) == 0) return true;
    }
    return false;
}
/* Classes visible from the current module: its own, the ones it imports, and the built-ins. */
static Class *find_class(const char *name) {
    SYM_EACH(classes_by_name, name, e) {
        Class *c = e->decl;
        if (c->mod == cur_mod || (c->exported && imports_name(cur_mod, c->mod, name))) return c;
    }
    if (strcmp(name, "RegExpMatchArray") == 0) name = "RegExpExecArray"; /* TypeScript's two names for a match */
    bool wt = strcmp(name, "Worker") == 0 || strcmp(name, "MessagePort") == 0;
    if (wt && !wt_imports(cur_mod, name)) return NULL; /* (worker_threads' classes: only where they are imported) */
    for (int i = 0; i < nbuiltins; i++)
        if (strcmp(builtins[i]->name, name) == 0) return builtins[i];
    return NULL;
}
static void check_expr(Expr *e, Type *expected);
static void coerce_lit(Expr *e, Type *want);
static void expect_type(Expr *e, Type *want, const char *what);
static bool widen_inferred(Expr *e);
static void infer_ints(Stmt *s);
static void ii_enter(Func *f, bool reset);
static void infer_ints_fn(Stmt *body);
/* an int or number argument of the Date API, as a number (ints converted) */
static void date_num_arg(Expr **slot, const char *what) {
    Expr *a = *slot;
    check_expr(a, ty_number);
    if (a->type->kind == TY_LITINT) coerce_lit(a, ty_number);
    if (a->type->kind == TY_INT) {
        Expr *c = xs_alloc(sizeof *c);
        c->kind = E_CALL; c->pos = a->pos; c->field = -1; c->type = ty_number;
        c->builtin.cfn = "xs_to_number";
        expr_list_push(&c->args, a);
        Expr *callee = xs_alloc(sizeof *callee);
        callee->kind = E_IDENT; callee->pos = a->pos; callee->str = "Number"; callee->field = -1;
        c->lhs = callee;
        *slot = c;
        return;
    }
    if (a->type->kind != TY_NUMBER) xs_error(a->pos, NULL, "%s must be a number, but this is %s", what, ty_name(a->type));
}
static Expr *num_lit(Pos pos, double v, bool is_int) {
    Expr *z = xs_alloc(sizeof *z);
    z->kind = E_NUMBER; z->pos = pos; z->num = v; z->ival = (long long)v; z->is_int_lit = is_int; z->type = is_int ? ty_int : ty_number; z->field = -1;
    return z;
}
/* d.getFullYear(), d.toISOString(), d.setHours(h, m?, s?, ms?) ... (runtime/xisto_rt.h, Date) */
static void check_date_member(Expr *e) {
    static const struct { const char *name; int ret; } gets[] = { /* ret: 0 int, 1 number, 2 string */
        {"getTime", 1}, {"valueOf", 1}, {"getFullYear", 0}, {"getMonth", 0}, {"getDate", 0}, {"getDay", 0}, {"getHours", 0}, {"getMinutes", 0},
        {"getSeconds", 0}, {"getMilliseconds", 0}, {"getUTCFullYear", 0}, {"getUTCMonth", 0}, {"getUTCDate", 0}, {"getUTCDay", 0}, {"getUTCHours", 0},
        {"getUTCMinutes", 0}, {"getUTCSeconds", 0}, {"getUTCMilliseconds", 0}, {"getTimezoneOffset", 0}, {"toISOString", 2}, {"toJSON", 2},
        {"toString", 2}, {"toDateString", 2}, {"toTimeString", 2}, {"toUTCString", 2}, {"toLocaleString", 2}, {"toLocaleDateString", 2},
        {"toLocaleTimeString", 2}, {"toGMTString", 2},
    };
    static const struct { const char *name; int which, max; } sets[] = {
        {"setFullYear", 0, 3}, {"setMonth", 1, 2}, {"setDate", 2, 1}, {"setHours", 3, 4}, {"setMinutes", 4, 3}, {"setSeconds", 5, 2}, {"setMilliseconds", 6, 1},
        {"setUTCFullYear", 8, 3}, {"setUTCMonth", 9, 2}, {"setUTCDate", 10, 1}, {"setUTCHours", 11, 4}, {"setUTCMinutes", 12, 3}, {"setUTCSeconds", 13, 2},
        {"setUTCMilliseconds", 14, 1},
    };
    const char *m = e->str;
    e->field = -1;
    if (!e->is_call) xs_error(e->pos, "Date has methods only: d.getTime(), d.getFullYear(), d.toISOString() ...", "Date has no property '%s'", m);
    for (size_t i = 0; i < sizeof gets / sizeof gets[0]; i++) {
        if (strcmp(m, gets[i].name) != 0) continue;
        if (e->args.len != 0) {
            if (strncmp(m, "toLocale", 8) == 0) xs_error(e->pos, "call it without arguments (en-US, as Node by default); Intl is not supported yet", "Date.%s with locales and options is not supported", m);
            xs_error(e->pos, NULL, "Date.%s takes no arguments", m);
        }
        const char *cf = strcmp(m, "valueOf") == 0 ? "getTime" : strcmp(m, "toJSON") == 0 ? "toISOString" : strcmp(m, "toGMTString") == 0 ? "toUTCString" : m;
        e->builtin.cfn = xs_sprintf("xs_Date_%s", cf);
        e->type = gets[i].ret == 0 ? ty_int : gets[i].ret == 1 ? ty_number : ty_string;
        return;
    }
    if (strcmp(m, "setTime") == 0) {
        if (e->args.len != 1) xs_error(e->pos, NULL, "Date.setTime takes one argument");
        date_num_arg(&e->args.items[0], "the time value");
        e->builtin.cfn = "xs_Date_setTime";
        e->type = ty_number;
        return;
    }
    for (size_t i = 0; i < sizeof sets / sizeof sets[0]; i++) {
        if (strcmp(m, sets[i].name) != 0) continue;
        int n = e->args.len;
        if (n < 1 || n > sets[i].max) xs_error(e->pos, NULL, "Date.%s takes 1 to %d arguments, got %d", m, sets[i].max, n);
        for (int k = 0; k < n; k++) date_num_arg(&e->args.items[k], xs_sprintf("argument %d of Date.%s", k + 1, m));
        /* xs_Date_set(d, which, n, a, b, c, e) */
        ExprList args = {0};
        expr_list_push(&args, num_lit(e->pos, sets[i].which, true));
        expr_list_push(&args, num_lit(e->pos, n, true));
        for (int k = 0; k < 4; k++) expr_list_push(&args, k < n ? e->args.items[k] : num_lit(e->pos, 0, false));
        e->args = args;
        e->builtin.cfn = "xs_Date_set";
        e->type = ty_number;
        return;
    }
    xs_error(e->pos, "supported: getTime, getFullYear/Month/Date/Day/Hours/Minutes/Seconds/Milliseconds (and getUTC...), getTimezoneOffset, toISOString, toJSON, toString, toDateString, toTimeString, toUTCString, toLocaleString, set... and setUTC...", "Date has no method '%s'", m);
}
static bool is_bytes(Type *t) {
    return t && t->kind == TY_OBJECT && t->cls->builtin && (strcmp(t->cls->name, "Uint8Array") == 0 || strcmp(t->cls->name, "Buffer") == 0);
}
static bool is_f64(Type *t) { return t && t->kind == TY_OBJECT && t->cls->builtin && strcmp(t->cls->name, "Float64Array") == 0; }
static bool is_sab(Type *t) { return t && t->kind == TY_OBJECT && t->cls->builtin && strcmp(t->cls->name, "SharedArrayBuffer") == 0; }
static Expr *str_lit(Pos pos, const char *s);
static Expr *null_str(Pos pos) { /* a missing optional string argument: NULL in C */
    Expr *n = xs_alloc(sizeof *n);
    n->kind = E_NULL; n->pos = pos; n->field = -1; n->type = ty_nullable_of(ty_string);
    return n;
}
#define XS_NOT_GIVEN (-9223372036854775807LL - 1) /* INT64_MIN: an index argument left out */
static void int_arg(Expr **slot, const char *what) {
    check_expr(*slot, ty_int);
    expect_type(*slot, ty_int, what);
}
/* b.slice(1, -1), b.set(src, 4), buf.toString("base64") ... */
static void check_bytes_member(Expr *e, Class *c) {
    const char *m = e->str;
    bool is_buf = strcmp(c->name, "Buffer") == 0;
    Class *u8 = find_class("Uint8Array");
    int n = e->args.len;
    e->field = -1;
    #define ARGS(lo, hi) do { if (n < (lo) || n > (hi)) xs_error(e->pos, NULL, "%s.%s takes %d to %d argument(s), got %d", c->name, m, (lo), (hi), n); } while (0)
    #define PAD_INT(k, v) do { for (int k_ = e->args.len; k_ <= (k); k_++) expr_list_push(&e->args, num_lit(e->pos, (double)(v), true)); } while (0)
    if (!e->is_call) {
        if (strcmp(m, "length") == 0 || strcmp(m, "byteLength") == 0) { e->field = 0; e->type = ty_int; return; }
        xs_error(e->pos, "supported: length, byteLength", "%s has no property '%s'", c->name, m);
    }
    if (strcmp(m, "slice") == 0 || strcmp(m, "subarray") == 0) {
        ARGS(0, 2);
        for (int k = 0; k < n; k++) int_arg(&e->args.items[k], xs_sprintf("argument of %s.%s", c->name, m));
        PAD_INT(1, XS_NOT_GIVEN);
        e->builtin.cfn = m[1] == 'l' ? "xs_u8_slice" : "xs_u8_subarray";
        e->type = ty_object_of(c);
        return;
    }
    if (strcmp(m, "set") == 0) {
        ARGS(1, 2);
        check_expr(e->args.items[0], e->args.items[0]->kind == E_ARRAY_LIT ? ty_array_of(ty_int) : NULL);
        Type *st = e->args.items[0]->type;
        if (is_bytes(st)) e->builtin.cfn = "xs_u8_set_from";
        else if (st->kind == TY_ARRAY && (st->elem->kind == TY_INT || st->elem->kind == TY_LITINT)) e->builtin.cfn = "xs_u8_set_ints";
        else xs_error(e->args.items[0]->pos, NULL, "%s.set copies from a Uint8Array or an int[], not %s", c->name, ty_name(st));
        if (n == 2) int_arg(&e->args.items[1], "the offset"); else PAD_INT(1, 0);
        e->type = ty_void;
        return;
    }
    if (strcmp(m, "fill") == 0) {
        ARGS(1, 3);
        for (int k = 0; k < n; k++) int_arg(&e->args.items[k], "argument of fill");
        PAD_INT(2, XS_NOT_GIVEN);
        if (n < 2) { e->args.items[1]->ival = XS_NOT_GIVEN; }
        e->builtin.cfn = "xs_u8_fill";
        e->type = ty_object_of(c);
        return;
    }
    if (strcmp(m, "indexOf") == 0 || strcmp(m, "includes") == 0) {
        bool io = m[1] == 'n' && m[2] == 'd';
        ARGS(1, io ? 2 : 1);
        if (is_buf) { /* buf.indexOf("\n"): Buffer searches for the UTF-8 bytes of a string too, as in Node */
            check_expr(e->args.items[0], NULL);
            if (e->args.items[0]->type->kind == TY_STRING) {
                for (int k = 1; k < n; k++) int_arg(&e->args.items[k], xs_sprintf("argument of %s", m));
                PAD_INT(1, 0);
                e->builtin.cfn = io ? "xs_buf_index_of_str" : "xs_buf_includes_str";
                e->type = io ? ty_int : ty_boolean;
                return;
            }
        }
        for (int k = 0; k < n; k++) int_arg(&e->args.items[k], xs_sprintf("argument of %s", m));
        if (io) PAD_INT(1, 0);
        e->builtin.cfn = io ? "xs_u8_index_of" : "xs_u8_includes";
        e->type = io ? ty_int : ty_boolean;
        return;
    }
    if (strcmp(m, "reverse") == 0) { ARGS(0, 0); e->builtin.cfn = "xs_u8_reverse"; e->type = ty_object_of(c); return; }
    if (strcmp(m, "equals") == 0) {
        ARGS(1, 1);
        check_expr(e->args.items[0], NULL);
        if (!is_bytes(e->args.items[0]->type)) xs_error(e->args.items[0]->pos, NULL, "equals compares with a Buffer or Uint8Array");
        e->builtin.cfn = "xs_u8_equals";
        e->type = ty_boolean;
        return;
    }
    if (strcmp(m, "join") == 0) {
        ARGS(0, 1);
        if (n == 1) { check_expr(e->args.items[0], ty_string); expect_type(e->args.items[0], ty_string, "the separator"); }
        else expr_list_push(&e->args, str_lit(e->pos, ","));
        e->builtin.cfn = "xs_u8_join";
        e->type = ty_string;
        return;
    }
    if (strcmp(m, "toBase64") == 0 || strcmp(m, "toHex") == 0) {
        ARGS(0, 0);
        e->builtin.cfn = m[2] == 'B' ? "xs_u8_to_base64" : "xs_u8_to_hex";
        e->type = ty_string;
        return;
    }
    if (strcmp(m, "toString") == 0) {
        if (!is_buf) { ARGS(0, 0); e->builtin.cfn = "xs_u8_to_string"; e->type = ty_string; return; }
        ARGS(0, 3); /* buf.toString(encoding = "utf8", start, end) */
        if (n >= 1) { check_expr(e->args.items[0], ty_string); expect_type(e->args.items[0], ty_string, "the encoding"); }
        else expr_list_push(&e->args, null_str(e->pos));
        for (int k = 1; k < n; k++) int_arg(&e->args.items[k], "argument of toString");
        PAD_INT(1, 0);
        PAD_INT(2, XS_NOT_GIVEN);
        e->builtin.cfn = "xs_buf_to_string";
        e->type = ty_string;
        return;
    }
    (void)u8;
    xs_error(e->pos, "supported: length, slice, subarray, set, fill, indexOf, includes, reverse, join, toString, toBase64, toHex (and Buffer: equals, toString(encoding))", "%s has no method '%s'", c->name, m);
    #undef ARGS
    #undef PAD_INT
}
/* f.length, f.fill(0.5), f.set(src, 4), f.subarray(lo, hi) ... on a Float64Array */
static void check_f64_member(Expr *e, Class *c) {
    const char *m = e->str;
    int n = e->args.len;
    e->field = -1;
    #define ARGS(lo, hi) do { if (n < (lo) || n > (hi)) xs_error(e->pos, NULL, "Float64Array.%s takes %d to %d argument(s), got %d", m, (lo), (hi), n); } while (0)
    #define PAD_INT(k, v) do { for (int k_ = e->args.len; k_ <= (k); k_++) expr_list_push(&e->args, num_lit(e->pos, (double)(v), true)); } while (0)
    if (!e->is_call) {
        if (strcmp(m, "length") == 0) { e->field = 0; e->type = ty_int; return; }
        if (strcmp(m, "byteLength") == 0 || strcmp(m, "byteOffset") == 0) { /* (in bytes: 8 per element) */
            e->builtin.cfn = m[4] == 'L' ? "xs_f64_byte_length" : "xs_f64_byte_offset";
            e->type = ty_int;
            return;
        }
        xs_error(e->pos, "supported: length, byteLength, byteOffset", "Float64Array has no property '%s'", m);
    }
    if (strcmp(m, "slice") == 0 || strcmp(m, "subarray") == 0) {
        ARGS(0, 2);
        for (int k = 0; k < n; k++) int_arg(&e->args.items[k], xs_sprintf("argument of Float64Array.%s", m));
        PAD_INT(1, XS_NOT_GIVEN);
        e->builtin.cfn = m[1] == 'l' ? "xs_f64_slice" : "xs_f64_subarray";
        e->type = ty_object_of(c);
        return;
    }
    if (strcmp(m, "set") == 0) {
        ARGS(1, 2);
        check_expr(e->args.items[0], e->args.items[0]->kind == E_ARRAY_LIT ? ty_array_of(ty_number) : NULL);
        Type *st = e->args.items[0]->type;
        if (is_f64(st)) e->builtin.cfn = "xs_f64_set_from";
        else if (st->kind == TY_ARRAY && st->elem->kind == TY_NUMBER) e->builtin.cfn = "xs_f64_set_nums";
        else xs_error(e->args.items[0]->pos, NULL, "Float64Array.set copies from a Float64Array or a number[], not %s", ty_name(st));
        if (n == 2) int_arg(&e->args.items[1], "the offset"); else PAD_INT(1, 0);
        e->type = ty_void;
        return;
    }
    if (strcmp(m, "fill") == 0) {
        ARGS(1, 3);
        check_expr(e->args.items[0], ty_number);
        if (e->args.items[0]->type->kind == TY_LITINT) coerce_lit(e->args.items[0], ty_number);
        expect_type(e->args.items[0], ty_number, "the value of fill");
        for (int k = 1; k < n; k++) int_arg(&e->args.items[k], "argument of fill");
        PAD_INT(1, XS_NOT_GIVEN);
        PAD_INT(2, XS_NOT_GIVEN);
        e->builtin.cfn = "xs_f64_fill";
        e->type = ty_object_of(c);
        return;
    }
    if (strcmp(m, "join") == 0) {
        ARGS(0, 1);
        if (n == 1) { check_expr(e->args.items[0], ty_string); expect_type(e->args.items[0], ty_string, "the separator"); }
        else expr_list_push(&e->args, str_lit(e->pos, ","));
        e->builtin.cfn = "xs_f64_join";
        e->type = ty_string;
        return;
    }
    if (strcmp(m, "toString") == 0) { ARGS(0, 0); e->builtin.cfn = "xs_f64_to_string"; e->type = ty_string; return; }
    xs_error(e->pos, "supported: length, byteLength, byteOffset, fill, set, subarray, slice, join, toString", "Float64Array has no method '%s'", m);
    #undef ARGS
    #undef PAD_INT
}
/* Uint8Array.from / of / fromBase64 / fromHex, Buffer.from / alloc / allocUnsafe / concat / byteLength */
static void check_bytes_static(Expr *e, const char *ns) {
    const char *m = e->str;
    bool is_buf = strcmp(ns, "Buffer") == 0;
    Class *u8 = find_class("Uint8Array"), *bc = find_class(is_buf ? "Buffer" : "Uint8Array");
    int n = e->args.len;
    if (!e->is_call) xs_error(e->pos, NULL, "%s.%s is not a value", ns, m);
    e->type = ty_object_of(bc);
    if (strcmp(m, "from") == 0) {
        if (n < 1 || n > 2) xs_error(e->pos, NULL, "%s.from takes a source%s", ns, is_buf ? " and an optional encoding" : "");
        check_expr(e->args.items[0], e->args.items[0]->kind == E_ARRAY_LIT ? ty_array_of(ty_int) : NULL);
        Type *st = e->args.items[0]->type;
        if (is_buf && st->kind == TY_STRING) {
            if (n == 2) { check_expr(e->args.items[1], ty_string); expect_type(e->args.items[1], ty_string, "the encoding"); }
            else expr_list_push(&e->args, null_str(e->pos));
            e->builtin.cfn = "xs_buf_from_str";
            return;
        }
        if (n != 1) xs_error(e->pos, NULL, "%s.from(bytes) takes one argument", ns);
        if (is_bytes(st)) { e->builtin.cfn = is_buf ? "xs_buf_from_bytes" : "xs_u8_copy"; return; }
        if (st->kind == TY_ARRAY && (st->elem->kind == TY_INT || st->elem->kind == TY_LITINT)) { e->builtin.cfn = is_buf ? "xs_buf_from_ints" : "xs_u8_from_ints"; return; }
        if (!is_buf && st->kind == TY_ARRAY && st->elem->kind == TY_NUMBER) { e->builtin.cfn = "xs_u8_from_nums"; return; }
        xs_error(e->args.items[0]->pos, NULL, "%s.from takes %s, not %s", ns, is_buf ? "a string, bytes or an int[]" : "bytes, an int[] or a number[]", ty_name(st));
    }
    if (!is_buf && strcmp(m, "of") == 0) { /* Uint8Array.of(1, 2, 3): from an array literal */
        Expr *lit = xs_alloc(sizeof *lit);
        lit->kind = E_ARRAY_LIT; lit->pos = e->pos; lit->field = -1;
        lit->args = e->args;
        check_expr(lit, ty_array_of(ty_int));
        expect_type(lit, ty_array_of(ty_int), "the values of Uint8Array.of");
        memset(&e->args, 0, sizeof e->args);
        expr_list_push(&e->args, lit);
        e->builtin.cfn = "xs_u8_from_ints";
        return;
    }
    if (!is_buf && (strcmp(m, "fromBase64") == 0 || strcmp(m, "fromHex") == 0)) {
        if (n != 1) xs_error(e->pos, NULL, "Uint8Array.%s takes one string", m);
        check_expr(e->args.items[0], ty_string);
        expect_type(e->args.items[0], ty_string, xs_sprintf("argument of Uint8Array.%s", m));
        e->builtin.cfn = m[4] == 'B' ? "xs_u8_from_base64" : "xs_u8_from_hex";
        return;
    }
    if (is_buf && (strcmp(m, "alloc") == 0 || strcmp(m, "allocUnsafe") == 0)) {
        if (n < 1 || n > (m[5] == 'U' ? 1 : 2)) xs_error(e->pos, NULL, "Buffer.%s takes a size%s", m, m[5] == 'U' ? "" : " and an optional fill byte");
        for (int k = 0; k < n; k++) int_arg(&e->args.items[k], xs_sprintf("argument of Buffer.%s", m));
        if (n == 1) expr_list_push(&e->args, num_lit(e->pos, 0, true));
        e->builtin.cfn = "xs_buf_alloc";
        return;
    }
    if (is_buf && strcmp(m, "concat") == 0) {
        if (n != 1) xs_error(e->pos, NULL, "Buffer.concat takes a list of buffers");
        check_expr(e->args.items[0], ty_array_of(ty_object_of(bc)));
        Type *lt = e->args.items[0]->type;
        if (lt->kind != TY_ARRAY || !is_bytes(lt->elem)) xs_error(e->args.items[0]->pos, NULL, "Buffer.concat takes Buffer[] or Uint8Array[], not %s", ty_name(lt));
        e->builtin.cfn = "xs_buf_concat";
        return;
    }
    if (is_buf && strcmp(m, "byteLength") == 0) {
        if (n != 1) xs_error(e->pos, NULL, "Buffer.byteLength takes one string");
        check_expr(e->args.items[0], ty_string);
        expect_type(e->args.items[0], ty_string, "argument of Buffer.byteLength");
        e->builtin.cfn = "xs_buf_byte_length";
        e->type = ty_int;
        return;
    }
    (void)u8;
    xs_error(e->pos, is_buf ? "supported: Buffer.from, alloc, allocUnsafe, concat, byteLength" : "supported: Uint8Array.from, of, fromBase64, fromHex", "unknown %s member '%s'", ns, m);
}
static Type *tuple_type(Type **ps, int n, Pos pos);
static void check_stmt(Stmt *s);
/* () => x: an element of Promise.all([...]) as a task (an arrow, so the checker works out its captures) */
static Expr *thunk_of(Expr *x) {
    Expr *e = xs_alloc(sizeof *e);
    e->kind = E_ARROW; e->pos = x->pos; e->field = -1;
    Arrow *a = xs_alloc(sizeof *a);
    a->pos = x->pos;
    a->expr_body = true;
    Func *f = xs_alloc(sizeof *f);
    f->pos = x->pos;
    f->name = "arrow";
    Stmt *b = xs_alloc(sizeof *b);
    b->kind = S_BLOCK; b->pos = x->pos;
    Stmt *r = xs_alloc(sizeof *r);
    r->kind = S_RETURN; r->pos = x->pos; r->expr = x;
    b->stmts.items = xs_alloc(sizeof(Stmt *));
    b->stmts.items[0] = r;
    b->stmts.len = b->stmts.cap = 1;
    f->body = b;
    a->fn = f;
    e->arrow = a;
    return e;
}
/* Promise.all([a(), b()]) / Promise.all(xs.map(f)) / Promise.race(...) / Promise.resolve(v). async is syntax in
   xisto (Promise<T> is T), so a call has its value when it returns; here the elements become tasks that run
   concurrently on the event loop (runtime/xisto_net.h, xs_par_run): their I/O waits overlap. */
static void check_promise(Expr *e, Type *expected) {
    const char *m = e->str;
    if (!e->is_call) xs_error(e->pos, NULL, "Promise.%s is not a value", m);
    if (strcmp(m, "resolve") == 0) { /* the value itself */
        if (e->args.len != 1) xs_error(e->pos, NULL, "Promise.resolve takes one value");
        Expr *v = e->args.items[0];
        check_expr(v, expected);
        *e = *v;
        return;
    }
    bool race = strcmp(m, "race") == 0 || strcmp(m, "any") == 0;
    if (!race && strcmp(m, "all") != 0) xs_error(e->pos, "supported: Promise.all, Promise.race, Promise.any, Promise.resolve", "unknown Promise member '%s'", m);
    if (e->args.len != 1) xs_error(e->pos, NULL, "Promise.%s takes an array", m);
    Expr *a = e->args.items[0];
    prog->uses_gen = true; /* the coroutines and the event loop */
    if (a->kind == E_ARRAY_LIT) { /* each element a task */
        if (a->args.len == 0) xs_error(a->pos, NULL, "Promise.%s of an empty list", m);
        Type *t = NULL;
        bool mixed = false;
        ExprList thunks = {0};
        Type **rts = xs_alloc(sizeof(Type *) * (size_t)a->args.len);
        for (int i = 0; i < a->args.len; i++) {
            Expr *th = thunk_of(a->args.items[i]);
            check_expr(th, NULL);
            Type *rt = th->arrow->fn->ret;
            if (rt->kind == TY_VOID) xs_error(a->args.items[i]->pos, NULL, "an element of Promise.%s gives no value", m);
            rts[i] = rt;
            if (!t) t = rt;
            else if (!ty_equal(t, rt)) {
                if (race) xs_error(a->args.items[i]->pos, "give the elements one type (an interface or a union)", "the elements of Promise.%s are %s and %s", m, ty_name(t), ty_name(rt));
                mixed = true;
            }
            expr_list_push(&thunks, th);
        }
        e->args = thunks;
        Type *want = expected && expected->kind == TY_NULLABLE ? expected->elem : expected;
        if (!race && (mixed || (want && want->kind == TY_OBJECT && want->cls->is_tuple))) {
            /* const [user, posts] = await Promise.all([getUser(), getPosts()]): a tuple of the results */
            e->builtin.cfn = "promise_all_tuple";
            e->elem_type = NULL;
            e->type = tuple_type(rts, a->args.len, e->pos);
            return;
        }
        e->builtin.cfn = race ? "promise_race" : "promise_all";
        e->elem_type = t;
        e->type = race ? t : ty_array_of(t);
        return;
    }
    check_expr(a, expected && !race ? expected : NULL);
    if (a->kind == E_MEMBER && a->is_call && a->builtin.cfn && strcmp(a->builtin.cfn, "map") == 0 && a->lhs->type->kind == TY_ARRAY && a->field == 1) {
        /* xs.map(f): f(x) for each element as a task */
        e->builtin.cfn = race ? "promise_race_map" : "promise_all_map";
        e->elem_type = a->type->elem;
        e->type = race ? a->type->elem : a->type;
        return;
    }
    if (a->type->kind != TY_ARRAY) xs_error(a->pos, NULL, "Promise.%s takes an array, not %s", m, ty_name(a->type));
    /* values already computed (async calls have finished): all is the array, race its first element */
    if (race) xs_error(a->pos, "write the calls in place: Promise.race([a(), b()])", "Promise.race of values already computed");
    *e = *a;
}
static bool is_regexp(Type *t) { return t && t->kind == TY_OBJECT && t->cls->builtin && strcmp(t->cls->name, "RegExp") == 0; }

static const char **class_names(int *n);

/* Replaces placeholder class names left by the parser with the real classes. */
static bool nullable_ok(Type *inner) {
    return inner->kind == TY_UNION || inner->kind == TY_STRING || inner->kind == TY_ARRAY || inner->kind == TY_OBJECT || inner->kind == TY_MAP || inner->kind == TY_SET ||
           inner->kind == TY_INT || inner->kind == TY_NUMBER || inner->kind == TY_BOOLEAN || inner->kind == TY_ENUM; /* primitives: a sentinel */
}

/* ---- generic interfaces: one concrete interface per list of type arguments ---- */
static Type *resolve(Type *t, Pos pos);
static Type *subst(Type *t, Class *tmpl, Type **args) {
    switch (t->kind) {
    case TY_OBJECT:
        if (!t->cls->cname && !t->ntargs)
            for (int i = 0; i < tmpl->ntparams; i++) if (strcmp(t->cls->name, tmpl->tparams[i]) == 0) return args[i];
        if (t->ntargs) {
            Type *n = xs_alloc(sizeof *n);
            *n = *t;
            Class *ph = xs_alloc(sizeof *ph);
            *ph = *t->cls;
            n->cls = ph;
            n->targs = xs_alloc(sizeof(Type *) * (size_t)t->ntargs);
            for (int i = 0; i < t->ntargs; i++) n->targs[i] = subst(t->targs[i], tmpl, args);
            return n;
        }
        if (!t->cls->cname) { /* a placeholder: copy, so resolving one instance does not touch the template */
            Type *n = xs_alloc(sizeof *n);
            *n = *t;
            Class *ph = xs_alloc(sizeof *ph);
            *ph = *t->cls;
            n->cls = ph;
            return n;
        }
        return t;
    case TY_ARRAY: return ty_array_of(subst(t->elem, tmpl, args));
    case TY_NULLABLE: return ty_nullable_of(subst(t->elem, tmpl, args));
    case TY_SET: return ty_set_of(subst(t->elem, tmpl, args));
    case TY_MAP: return ty_map_of(subst(t->key, tmpl, args), subst(t->elem, tmpl, args));
    case TY_FUNC: {
        Type **ps = xs_alloc(sizeof(Type *) * (size_t)(t->nparams ? t->nparams : 1));
        for (int i = 0; i < t->nparams; i++) ps[i] = subst(t->params[i], tmpl, args);
        return ty_func(ps, t->nparams, subst(t->ret, tmpl, args));
    }
    default: return t;
    }
}
static Class *instantiate(Class *tmpl, Type **args, Pos pos) {
    static Class **made;
    static Type ***made_args;
    static int nmade;
    for (int i = 0; i < nmade; i++) {
        if (made[i]->generic_of != tmpl) continue;
        bool same = true;
        for (int k = 0; k < tmpl->ntparams && same; k++) same = ty_equal(made_args[i][k], args[k]);
        if (same) return made[i];
    }
    Class *c = xs_alloc(sizeof *c);
    Buf name = {0};
    buf_printf(&name, "%s<", tmpl->name);
    for (int k = 0; k < tmpl->ntparams; k++) buf_printf(&name, "%s%s", k ? ", " : "", ty_name(args[k]));
    buf_puts(&name, ">");
    c->name = name.data;
    c->pos = tmpl->pos;
    c->mod = tmpl->mod;
    c->is_interface = true;
    c->exported = tmpl->exported;
    c->used = true;
    c->cname = xs_sprintf("g%d_%s_%d", tmpl->mod->id, tmpl->name, nmade);
    c->generic_of = tmpl;
    made = realloc(made, sizeof(Class *) * (size_t)(nmade + 1));
    made_args = realloc(made_args, sizeof(Type **) * (size_t)(nmade + 1));
    made[nmade] = c;
    made_args[nmade] = args;
    nmade++;
    /* field types are resolved where the template was written */
    Module *saved_mod = cur_mod;
    Scope *saved_scope = scope;
    enter_module(tmpl->mod);
    c->fields = xs_alloc(sizeof(Field) * (size_t)(tmpl->nfields ? tmpl->nfields : 1));
    for (int i = 0; i < tmpl->nfields; i++) {
        c->fields[i] = tmpl->fields[i];
        c->fields[i].type = resolve(subst(tmpl->fields[i].type, tmpl, args), pos);
        c->fields[i].cname = xs_sprintf("f_%s", tmpl->fields[i].name);
    }
    c->nfields = c->cap_fields = tmpl->nfields;
    cur_mod = saved_mod;
    scope = saved_scope;
    if (cur_mod) { xs_file_name = cur_mod->file; xs_source = cur_mod->src; }
    prog->classes = realloc(prog->classes, sizeof(Class *) * (size_t)(prog->nclasses + 1));
    prog->classes[prog->nclasses++] = c;
    return c;
}

/* [A, B]: one class per list of element types, an interface with fields _0, _1, ... (Class.is_tuple), so a
   tuple is an object like any other for reference counting, arrays, maps and generics; t[0] reads _0 */
static Class *tuple_class(Type **ps, int n, Pos pos) {
    static Class **made;
    static int nmade;
    for (int i = 0; i < nmade; i++) {
        Class *c = made[i];
        if (c->nfields != n) continue;
        bool same = true;
        for (int k = 0; k < n && same; k++) same = ty_equal(c->fields[k].type, ps[k]);
        if (same) return c;
    }
    if (n < 1) xs_error(pos, NULL, "an empty tuple type [] has no use");
    Class *c = xs_alloc(sizeof *c);
    Buf name = {0};
    buf_puts(&name, "[");
    for (int k = 0; k < n; k++) buf_printf(&name, "%s%s", k ? ", " : "", ty_name(ps[k]));
    buf_puts(&name, "]");
    c->name = name.data;
    c->pos = pos;
    c->mod = cur_mod ? cur_mod : prog->modules[0];
    c->is_interface = true;
    c->is_tuple = true;
    c->used = true;
    c->cname = xs_sprintf("tup%d", nmade);
    c->fields = xs_alloc(sizeof(Field) * (size_t)n);
    for (int k = 0; k < n; k++) {
        c->fields[k].name = xs_sprintf("_%d", k);
        c->fields[k].type = ps[k];
        c->fields[k].cname = xs_sprintf("f__%d", k);
        c->fields[k].pos = pos;
        c->fields[k].owner = c;
    }
    c->nfields = c->cap_fields = n;
    made = realloc(made, sizeof(Class *) * (size_t)(nmade + 1));
    made[nmade++] = c;
    prog->classes = realloc(prog->classes, sizeof(Class *) * (size_t)(prog->nclasses + 1));
    prog->classes[prog->nclasses++] = c;
    return c;
}
static Type *tuple_type(Type **ps, int n, Pos pos) {
    Type *t = xs_alloc(sizeof *t);
    t->kind = TY_OBJECT;
    t->cls = tuple_class(ps, n, pos);
    return t;
}

static Type *resolve(Type *t, Pos pos) {
    switch (t->kind) {
    case TY_TUPLE: { /* in place: whoever holds this type now holds the tuple's object type */
        for (int i = 0; i < t->nparams; i++) t->params[i] = resolve(t->params[i], pos);
        Type *o = tuple_type(t->params, t->nparams, pos);
        *t = *o;
        return t;
    }
    case TY_ARRAY: case TY_SET: case TY_STREAM: t->elem = resolve(t->elem, pos); return t;
    case TY_MAP:
        t->key = resolve(t->key, pos);
        t->elem = resolve(t->elem, pos);
        if (t->key->kind != TY_STRING && t->key->kind != TY_INT)
            xs_error(pos, "Map keys are string or int in xisto", "unsupported Map key type '%s'", ty_name(t->key));
        return t;
    case TY_NULLABLE:
        t->elem = resolve(t->elem, pos);
        if (!nullable_ok(t->elem))
            xs_error(pos, "values that can be null: strings, numbers, booleans, enums, arrays, objects, Map and Set", "'%s | null' is not supported", ty_name(t->elem));
        return t;
    case TY_FUNC:
        for (int i = 0; i < t->nparams; i++) t->params[i] = resolve(t->params[i], pos);
        t->ret = resolve(t->ret, pos);
        return t;
    case TY_UNION: { /* members resolved, nested unions flattened, repeats dropped; null makes it nullable */
        Type **m = NULL;
        int n = 0;
        bool nul = false;
        for (int i = 0; i < t->nparams; i++) {
            Type *x = resolve(t->params[i], pos);
            if (x->kind == TY_NULLABLE) { nul = true; x = x->elem; }
            Type **xs = x->kind == TY_UNION ? x->params : &x;
            int xn = x->kind == TY_UNION ? x->nparams : 1;
            for (int k = 0; k < xn; k++) {
                bool dup = false;
                for (int j = 0; j < n && !dup; j++) dup = ty_equal(m[j], xs[k]) && (m[j]->tname == xs[k]->tname || (m[j]->tname && xs[k]->tname && strcmp(m[j]->tname, xs[k]->tname) == 0));
                if (dup) continue;
                if (xs[k]->kind == TY_VOID || xs[k]->kind == TY_FUNC) xs_error(pos, NULL, "'%s' cannot be part of a union", ty_name(xs[k]));
                m = realloc(m, sizeof(Type *) * (size_t)(n + 1));
                m[n++] = xs[k];
            }
        }
        Type *u = m[0];
        if (n > 1) { u = xs_alloc(sizeof *u); u->kind = TY_UNION; u->params = m; u->nparams = n; }
        return nul ? ty_nullable_of(u) : u;
    }
    case TY_OBJECT:
        if (t->cls->cname) return t;
        {
            Enum *en = find_enum(t->cls->name);
            if (en) return ty_enum_of(en);
            Class *c = find_class(t->cls->name);
            if (!c) {
                int n; const char **cands = class_names(&n);
                xs_error(t->cls->pos.line ? t->cls->pos : pos, xs_help_suggest(t->cls->name, cands, n, "declare the class above, or use number, int, boolean, string or T[]"), "unknown type '%s'", t->cls->name);
            }
            if (c->alias) { /* type Name = A | B: the aliased type, resolved once in its own module */
                Module *sm = cur_mod;
                Scope *ss = scope;
                const char *sfn = xs_file_name, *ssrc = xs_source;
                enter_module(c->mod);
                c->alias = resolve(c->alias, c->pos);
                cur_mod = sm;
                scope = ss;
                xs_file_name = sfn;
                xs_source = ssrc;
                return c->alias;
            }
            if (c->ntparams || t->ntargs) {
                if (c->ntparams != t->ntargs)
                    xs_error(pos, xs_sprintf("write %s<...> with %d type argument(s)", c->name, c->ntparams), "'%s' takes %d type argument(s), got %d", c->name, c->ntparams, t->ntargs);
                for (int i = 0; i < t->ntargs; i++) t->targs[i] = resolve(t->targs[i], pos);
                c = instantiate(c, t->targs, pos);
            }
            t->cls = c;
        }
        return t;
    default: return t;
    }
}

/* ---------- scopes ---------- */

static void push_scope(void) {
    Scope *s = xs_alloc(sizeof *s);
    s->parent = scope;
    scope = s;
}

/* unused locals are reported when their scope closes */
static void pop_scope(void) {
    for (Binding *b = scope->head; b; b = b->next) {
        if (b->origin || b->used || b->no_warn) continue;
        xs_warn(b->pos, xs_sprintf("remove it, or name it _%s if it is unused on purpose", b->name), "'%s' is declared but never used", b->name);
        b->used = true; /* once */
    }
    scope = scope->parent;
}

static void mark_used(Binding *b) {
    for (; b; b = b->origin) b->used = true;
}

static Capture *add_capture(Arrow *a, const char *name, const char *outer_cname, Type *type, bool is_this) {
    for (int i = 0; i < a->ncaptures; i++)
        if (strcmp(a->captures[i].name, name) == 0) return &a->captures[i];
    if (a->ncaptures == a->cap_captures) {
        a->cap_captures = a->cap_captures ? a->cap_captures * 2 : 4;
        a->captures = realloc(a->captures, sizeof(Capture) * (size_t)a->cap_captures);
    }
    Capture *c = &a->captures[a->ncaptures++];
    memset(c, 0, sizeof *c); /* (boxed was left as realloc gave it: on gcc 11 builds an arrow lost its `this`) */
    c->name = (char *)name;
    c->outer_cname = (char *)outer_cname;
    c->type = type;
    c->is_this = is_this;
    return c;
}

/* Looks a name up through enclosing scopes. Crossing an arrow boundary into a local of the enclosing
   function captures it by value: the arrow gets its own read-only binding that reads env->c_<name>. */
static Binding *lookup(const char *name) {
    Scope *boundaries[16];
    int nb = 0;
    for (Scope *s = scope; s; s = s->parent) {
        for (Binding *b = s->head; b; b = b->next) {
            if (strcmp(b->name, name) != 0) continue;
            bool global = strncmp(b->cname, "g", 1) == 0 && b->cname[1] >= '0' && b->cname[1] <= '9';
            mark_used(b);
            if (nb == 0 || global) return b;
            /* capture into each arrow from the outermost boundary inward */
            Binding *cur = b;
            for (int i = nb - 1; i >= 0; i--) {
                Arrow *a = boundaries[i]->arrow;
                Capture *c = add_capture(a, name, cur->cell ? cur->cell : cur->cname, cur->type, false);
                Binding *nbind = xs_alloc(sizeof *nbind);
                *nbind = *cur;
                nbind->origin = cur;
                nbind->is_param = false;
                nbind->orig = NULL;
                if (cur->cell) { /* shared with the enclosing function through the cell: reads and writes see each other */
                    c->boxed = true;
                    nbind->cell = xs_sprintf("env->c_%s", name);
                    nbind->cname = xs_sprintf("(env->c_%s->v)", name);
                } else {
                    nbind->is_const = true;
                    nbind->cname = xs_sprintf("env->c_%s", name);
                }
                nbind->next = boundaries[i]->head;
                boundaries[i]->head = nbind;
                cur = nbind;
            }
            return cur;
        }
        if (s->arrow && nb < 16) boundaries[nb++] = s;
    }
    return NULL;
}

/* `this` inside an arrow: captured from the method */
static const char *this_cname(void) {
    const char *name = "self";
    int nb = 0;
    Scope *boundaries[16];
    for (Scope *s = scope; s; s = s->parent) if (s->arrow && nb < 16) boundaries[nb++] = s;
    for (int i = nb - 1; i >= 0; i--) {
        add_capture(boundaries[i]->arrow, "this", name, ty_object_of(cur_class), true);
        name = "env->self";
    }
    return name;
}

static Func *lookup_func(const char *name) {
    SYM_EACH(funcs_by_name, name, e) {
        Func *f = e->decl;
        if (f->mod == cur_mod || (f->exported && imports_name(cur_mod, f->mod, name))) return f;
        const char *st = strstr(name, "__s_"); /* a static method: visible where its class is imported */
        if (st && f->exported) {
            char *cls = xs_strndup(name, (size_t)(st - name));
            if (imports_name(cur_mod, f->mod, cls)) return f;
        }
    }
    return NULL;
}

static bool is_builtin_name(const char *n) {
    return strcmp(n, "Math") == 0 || strcmp(n, "console") == 0 || strcmp(n, "Number") == 0 || strcmp(n, "process") == 0 || strcmp(n, "Date") == 0 || strcmp(n, "performance") == 0 || strcmp(n, "lines") == 0 || strcmp(n, "chunks") == 0 || strcmp(n, "assert") == 0 ||
           strcmp(n, "String") == 0 || strcmp(n, "Array") == 0 || strcmp(n, "fetch") == 0 || strcmp(n, "Promise") == 0 || find_class(n) != NULL || find_enum(n) != NULL;
}

/* Names visible from the current scope plus every top-level function: candidates for "did you mean". */
static const char **visible_names(int *n) {
    int cap = 32, k = 0;
    const char **v = xs_alloc(sizeof(char *) * (size_t)cap);
    for (Scope *s = scope; s; s = s->parent)
        for (Binding *b = s->head; b; b = b->next) {
            if (b->name[0] == '\x01') continue; /* a narrowed field path (path_key) */
            if (k == cap) { cap *= 2; v = realloc(v, sizeof(char *) * (size_t)cap); }
            v[k++] = b->name;
        }
    for (int i = 0; i < prog->nfuncs; i++) {
        if (!lookup_func(prog->funcs[i]->name)) continue;
        if (k == cap) { cap *= 2; v = realloc(v, sizeof(char *) * (size_t)cap); }
        v[k++] = prog->funcs[i]->name;
    }
    *n = k;
    return v;
}

static const char **class_names(int *n) {
    static const char *prims[] = {"number", "int", "boolean", "string", "void"};
    int k = 0;
    const char **v = xs_alloc(sizeof(char *) * (size_t)(prog->nclasses + nbuiltins + 5));
    for (int i = 0; i < 5; i++) v[k++] = prims[i];
    for (int i = 0; i < prog->nclasses; i++) if (find_class(prog->classes[i]->name) == prog->classes[i]) v[k++] = prog->classes[i]->name;
    for (int i = 0; i < nbuiltins; i++) v[k++] = builtins[i]->name;
    *n = k;
    return v;
}

static const char **member_names(Class *c, int *n) {
    int k = 0;
    const char **v = xs_alloc(sizeof(char *) * (size_t)(c->nfields + c->nmethods + 1));
    for (int i = 0; i < c->nfields; i++) v[k++] = c->fields[i].name;
    for (int i = 0; i < c->nmethods; i++) v[k++] = c->methods[i]->name;
    *n = k;
    return v;
}

/* ---- closures that capture by reference: a variable a closure captures and someone reassigns lives in a heap
   cell shared by the function and its closures; every other capture stays a copy (no cost). Found by name per
   function (or module): a use at a deeper arrow than the declaration, and an assignment anywhere. A `let` in a
   for header keeps JavaScript's copy per iteration unless a closure itself assigns it. ---- */
typedef struct { const char *name; int depth; char kind; } ScanEv; /* d declared, u used, a assigned */
static ScanEv *sev;
static int nsev, cap_sev;
static bool for_init_decl;
static bool decl_no_cell;
static void scan_ev(const char *name, int depth, char kind) {
    if (nsev == cap_sev) { cap_sev = cap_sev ? cap_sev * 2 : 64; sev = realloc(sev, sizeof(ScanEv) * (size_t)cap_sev); }
    sev[nsev++] = (ScanEv){name, depth, kind};
}
static void scan_stmt(Stmt *s, int depth);
static void scan_expr(Expr *e, int depth) {
    if (!e) return;
    if (e->kind == E_IDENT && e->str) scan_ev(e->str, depth, 'u');
    if ((e->kind == E_ASSIGN || e->kind == E_POSTFIX) && e->lhs && e->lhs->kind == E_IDENT) scan_ev(e->lhs->str, depth, 'a');
    if (e->kind == E_ARROW) {
        for (int i = 0; i < e->arrow->fn->nparams; i++) scan_ev(e->arrow->fn->params[i].name, depth + 1, 'd');
        scan_stmt(e->arrow->fn->body, depth + 1);
        return;
    }
    scan_expr(e->lhs, depth);
    scan_expr(e->rhs, depth);
    for (int i = 0; i < e->args.len; i++) scan_expr(e->args.items[i], depth);
}
static void scan_stmt(Stmt *s, int depth) {
    if (!s) return;
    if ((s->kind == S_VAR || s->kind == S_FOROF) && s->name) scan_ev(s->name, depth, 'd');
    scan_expr(s->init, depth);
    scan_expr(s->expr, depth);
    scan_expr(s->cond, depth);
    scan_stmt(s->for_init, depth);
    scan_expr(s->for_update, depth);
    scan_stmt(s->body, depth);
    scan_stmt(s->else_body, depth);
    for (int i = 0; i < s->stmts.len; i++) scan_stmt(s->stmts.items[i], depth);
    for (int i = 0; i < s->ncases; i++) { scan_expr(s->cases[i].value, depth); scan_stmt(s->cases[i].body, depth); }
}
static bool boxed_name(const char *name, int decl_depth, bool for_init) {
    bool captured = false, assigned = false, assigned_inside = false;
    for (int i = 0; i < nsev; i++) {
        if (strcmp(sev[i].name, name) != 0) continue;
        if (sev[i].kind == 'u' && sev[i].depth > decl_depth) captured = true;
        if (sev[i].kind == 'a') { assigned = true; if (sev[i].depth > decl_depth) assigned_inside = true; }
    }
    return captured && (for_init ? assigned_inside : assigned);
}
static int arrow_depth(void) {
    int n = 0;
    for (Scope *s = scope; s; s = s->parent) if (s->arrow) n++;
    return n;
}

static Binding *declare(Pos pos, const char *name, Type *type, bool is_const, bool global, bool is_param) {
    for (Binding *b = scope->head; b; b = b->next)
        if (strcmp(b->name, name) == 0) xs_error(pos, NULL, "'%s' is already declared in this scope", name);
    /* a variable may shadow a built-in function or namespace (lines, fetch, console...), as in TypeScript; class
       and enum names stay reserved */
    if (find_class(name) || find_enum(name)) xs_error(pos, NULL, "'%s' is built in and cannot be redeclared", name);
    Binding *b = xs_alloc(sizeof *b);
    b->name = (char *)name;
    b->type = type;
    b->is_const = is_const;
    b->is_param = is_param;
    b->cname = global ? xs_sprintf("g%d_%s", cur_mod ? cur_mod->id : 0, name) : xs_sprintf("v_%s_%d", name, ++tmp_counter);
    if (!global && !decl_no_cell && boxed_name(name, arrow_depth(), for_init_decl)) { /* a closure captures it and someone reassigns it */
        b->plain = b->cname;
        b->cell = xs_sprintf("%s_c", b->cname);
        b->cname = xs_sprintf("(%s->v)", b->cell);
    }
    b->pos = pos;
    b->no_warn = global || is_param || name[0] == '_';
    b->next = scope->head;
    scope->head = b;
    return b;
}

/* ---------- helpers ---------- */

static bool is_numeric(Type *t) { return t->kind == TY_NUMBER || t->kind == TY_INT; }

static void check_type_supported(Pos pos, Type *t) {
    if (t->kind == TY_ARRAY && (t->elem->kind == TY_VOID || (t->elem->kind == TY_NULLABLE && !ty_is_heap(t->elem))))
        xs_error(pos, "arrays hold number, int, boolean, string, enums, objects, functions, arrays, Map and Set, and (T | null) of strings, objects and arrays; not nullable numbers",
                 "unsupported array type '%s'", ty_name(t));
    if (t->kind == TY_SET && t->elem->kind != TY_STRING && t->elem->kind != TY_INT)
        xs_error(pos, "Set elements are string or int in xisto", "unsupported Set element type '%s'", ty_name(t));
    if (t->kind == TY_CHAN && (t->elem->kind == TY_VOID || t->elem->kind == TY_FUNC))
        xs_error(pos, NULL, "unsupported channel element type '%s'", ty_name(t->elem));
    if (t->kind == TY_MAP && (t->elem->kind == TY_VOID || t->elem->kind == TY_FUNC || t->elem->kind == TY_NULLABLE))
        xs_error(pos, NULL, "unsupported Map value type '%s'", ty_name(t->elem));
    if (t->kind == TY_STREAM && (t->elem->kind == TY_FUNC || t->elem->kind == TY_VOID || t->elem->kind == TY_NULLABLE || t->elem->kind == TY_STREAM))
        xs_error(pos, NULL, "unsupported stream element type '%s'", ty_name(t->elem));
    if (t->kind == TY_VOID) xs_error(pos, NULL, "void is not a value type");
}

/* JSON.stringify / JSON.parse cover primitives, strings, classes whose fields are all serializable, and arrays of those. */
static bool json_ok(Type *t, int depth) {
    if (depth > 8) return false;
    switch (t->kind) {
    case TY_NUMBER: case TY_INT: case TY_BOOLEAN: case TY_STRING: case TY_ENUM: return true;
    case TY_JSON: return true; /* embedded as it is */
    case TY_ARRAY: case TY_NULLABLE: return json_ok(t->elem, depth + 1);
    case TY_OBJECT:
        if (t->cls->builtin) return strcmp(t->cls->name, "Date") == 0; /* as its ISO text */
        for (int i = 0; i < t->cls->nfields; i++)
            if (!json_ok(t->cls->fields[i].type, depth + 1)) return false;
        return true;
    default: return false;
    }
}

static int intern(const char *s) {
    for (int i = 0; i < prog->nlits; i++)
        if (strcmp(prog->lits[i], s) == 0) return i;
    if (prog->nlits == prog->cap_lits) {
        prog->cap_lits = prog->cap_lits ? prog->cap_lits * 2 : 16;
        prog->lits = realloc(prog->lits, sizeof(char *) * (size_t)prog->cap_lits);
    }
    prog->lits[prog->nlits] = (char *)s;
    return prog->nlits++;
}
static Expr *str_lit(Pos pos, const char *str) {
    Expr *e = xs_alloc(sizeof *e);
    e->kind = E_STRING; e->pos = pos; e->str = (char *)str; e->type = ty_string; e->field = -1;
    e->lit_id = intern(str);
    return e;
}

static void check_expr(Expr *e, Type *expected);
static bool truthy_type(Type *t);
static void make_truthy(Expr *e, const char *what);
static bool rereadable(Expr *e);
static Expr *expr_copy(Expr *e);
static const char **member_names(Class *c, int *n);
static bool always_returns(Stmt *s);
static void check_block(Stmt *b);
static Binding *narrow_target(Expr *cond, bool *not_null);
static const char *path_key(Expr *m);
typedef struct { Binding *b[8]; int n; } NSet;
static void narrow_set(Expr *c, bool when, NSet *out);
static void shadow_set(NSet *s);
static void shadow_narrowed(Binding *b);
static void unnarrow(Binding *b);
typedef struct { Binding *b; Type *then_t, *else_t; } UNarrow;
static UNarrow union_narrow(Expr *c);
static void shadow_union(Binding *b, Type *t);
static Expr *union_ident(Expr *e);
static Type *union_subset(Type *u, bool *keep);

static void coerce_lit(Expr *e, Type *want) {
    if (e->type->kind != TY_LITINT) return;
    e->type = want;
    if (e->kind == E_BINARY || e->kind == E_UNARY || e->kind == E_AWAIT) {
        coerce_lit(e->lhs, want);
        if (e->rhs) coerce_lit(e->rhs, want);
    }
}

static void settle(Expr *e) {
    if (e->type->kind == TY_LITINT) coerce_lit(e, ty_number);
}

/* ---- `object` (TY_JSON): any serializable value, held as its JSON text ----
   T -> object: serialized where it is converted. object -> T (a class, interface or array, as TypeScript's
   `any` would be): decoded and checked against T, a failure is an Error like JSON.parse's. */
static bool json_ok(Type *t, int depth);
static Expr *json_call(Expr *arg, const char *member, const char *cfn, Type *type) {
    Expr *ns = xs_alloc(sizeof *ns);
    ns->kind = E_IDENT;
    ns->pos = arg->pos;
    ns->str = "JSON";
    Expr *c = xs_alloc(sizeof *c);
    c->kind = E_MEMBER;
    c->pos = arg->pos;
    c->lhs = ns;
    c->str = (char *)member;
    c->is_call = true;
    c->field = -1;
    c->builtin.cfn = cfn;
    c->type = type;
    expr_list_push(&c->args, arg);
    return c;
}
static void to_json(Expr *e) {
    if (e->type->kind == TY_LITINT) coerce_lit(e, ty_int);
    Expr *v = expr_copy(e);
    *e = *json_call(v, "stringify", "json_stringify", ty_json);
}
static void from_json(Expr *e, Type *want) {
    Expr *v = expr_copy(e);
    *e = *json_call(v, "parse", "json_parse", want);
}
static bool json_target(Type *t) { return (t->kind == TY_OBJECT || t->kind == TY_ARRAY) && json_ok(t, 0); }

/* T -> unknown: references as they are, primitives boxed */
static void to_unknown(Expr *e) {
    if (e->type->kind == TY_LITINT) coerce_lit(e, ty_int);
    if (e->type->kind == TY_NULL) { e->type = ty_unknown; return; }
    Type *t = e->type;
    bool nul = t->kind == TY_NULLABLE && !ty_is_heap(t);
    Type *b = nul ? t->elem : t;
    const char *fn = b->kind == TY_NUMBER ? "xs_box_num" : b->kind == TY_BOOLEAN ? "xs_box_bool"
                   : (b->kind == TY_INT || (b->kind == TY_ENUM && !b->en->is_string)) ? "xs_box_int" : NULL;
    if (!fn) { e->type = ty_unknown; return; } /* a reference: the same pointer */
    Expr *v = expr_copy(e);
    memset(e, 0, sizeof *e);
    e->kind = E_CALL;
    e->pos = v->pos;
    e->builtin.cfn = nul ? (b->kind == TY_NUMBER ? "xs_box_num_or_null" : b->kind == TY_BOOLEAN ? "xs_box_bool_or_null" : "xs_box_int_or_null") : fn;
    e->type = ty_unknown;
    expr_list_push(&e->args, v);
}
static bool unknown_target(Type *t) {
    Type *b = t->kind == TY_NULLABLE ? t->elem : t;
    switch (b->kind) {
    case TY_INT: case TY_NUMBER: case TY_BOOLEAN: case TY_STRING: case TY_ARRAY: case TY_MAP: case TY_SET: case TY_FUNC: return true;
    case TY_ENUM: return true;
    case TY_OBJECT: return !b->cls->builtin || strcmp(b->cls->name, "Error") == 0 || (b->cls->ctor && b->cls->ctor->is_native);
    default: return false;
    }
}

static bool is_subclass(Class *sub, Class *base);
static void upcast(Expr *e, Type *to);
static Func *eff_ctor(Class *c);
static void to_unknown(Expr *e);
/* the member of union u a value of type t is (NULL: none) */
static Type *union_member_for(Type *u, Type *t, Expr *e) {
    for (int i = 0; i < u->nparams; i++) {
        Type *m = u->params[i];
        if (m->kind == TY_STRING && m->tname) { if (t->kind == TY_STRING && e && e->kind == E_STRING && strcmp(e->str, m->tname) == 0) return m; continue; }
        if (ty_equal(m, t)) return m;
        if (t->kind == TY_LITINT && (m->kind == TY_NUMBER || m->kind == TY_INT)) return m;
        if (m->kind == TY_OBJECT && t->kind == TY_OBJECT && !m->cls->is_interface && is_subclass(t->cls, m->cls)) return m;
    }
    return NULL;
}
/* a value goes into a union: boxed like an unknown (a union of fewer members is already one) */
static bool enter_union(Expr *e, Type *want) {
    Type *u = want->kind == TY_NULLABLE ? want->elem : want;
    Type *g = e->type;
    if (g->kind == TY_NULL) { if (want->kind != TY_NULLABLE) return false; e->type = want; return true; }
    if (g->kind == TY_NULLABLE && want->kind != TY_NULLABLE) return false;
    Type *gb = g->kind == TY_NULLABLE ? g->elem : g;
    if (gb->kind == TY_UNION) {
        for (int i = 0; i < gb->nparams; i++) if (!union_member_for(u, gb->params[i], NULL)) return false;
        e->type = want;
        return true;
    }
    Type *m = union_member_for(u, gb, e);
    if (!m) return false;
    if (g->kind == TY_LITINT) coerce_lit(e, m);
    to_unknown(e);
    e->type = want;
    return true;
}
static void expect_type(Expr *e, Type *want, const char *what) {
    if ((want->kind == TY_NUMBER || (want->kind == TY_NULLABLE && want->elem->kind == TY_NUMBER)) && widen_inferred(e)) return;
    /* a numeric enum is a number (TypeScript): HttpStatus.UNAUTHORIZED where an int goes */
    if (e->type->kind == TY_ENUM && !e->type->en->is_string && (want->kind == TY_INT || want->kind == TY_NUMBER)) { if (want->kind == TY_INT) e->type = ty_int; else return; }
    if (e->type->kind == TY_INT && want->kind == TY_INT) return;
    /* T[] of references is laid out like unknown[]: string[] where unknown[] goes */
    if (want->kind == TY_ARRAY && want->elem->kind == TY_UNKNOWN && e->type->kind == TY_ARRAY && ty_is_heap(e->type->elem)) return;
    if (want->kind == TY_UNKNOWN && e->type->kind != TY_UNKNOWN && e->type->kind != TY_VOID) { to_unknown(e); return; }
    if (want->kind == TY_JSON && e->type->kind != TY_JSON && e->type->kind != TY_NULL && (e->type->kind == TY_LITINT || json_ok(e->type, 0))) { to_json(e); return; }
    if (e->type->kind == TY_JSON && json_target(want)) { from_json(e, want); return; }
    if (e->type->kind == TY_LITINT && is_numeric(want)) coerce_lit(e, want);
    if (e->type->kind == TY_LITINT && want->kind == TY_NULLABLE && is_numeric(want->elem)) coerce_lit(e, want->elem);
    if (ty_equal(e->type, want) && !(want->kind == TY_STRING && want->tname)) return;
    if (want->kind == TY_STRING && want->tname && e->type->kind == TY_STRING) { /* a string literal type: that value only */
        if (e->kind != E_STRING || strcmp(e->str, want->tname) != 0)
            xs_error(e->pos, NULL, "%s must be \"%s\"", what, want->tname);
        return;
    }
    if ((want->kind == TY_UNION || (want->kind == TY_NULLABLE && want->elem->kind == TY_UNION)) && enter_union(e, want)) return;
    { /* a subclass where its base goes (also inside T | null, and arrays of them, laid out alike) */
        Type *g = e->type->kind == TY_NULLABLE ? e->type->elem : e->type, *w = want->kind == TY_NULLABLE ? want->elem : want;
        if (g->kind == TY_OBJECT && w->kind == TY_OBJECT && g->cls != w->cls && !w->cls->is_interface && is_subclass(g->cls, w->cls) &&
            (e->type->kind != TY_NULLABLE || want->kind == TY_NULLABLE)) { upcast(e, want); return; }
        if (g->kind == TY_ARRAY && w->kind == TY_ARRAY && g->elem->kind == TY_OBJECT && w->elem->kind == TY_OBJECT &&
            !w->elem->cls->is_interface && is_subclass(g->elem->cls, w->elem->cls) && (e->type->kind != TY_NULLABLE || want->kind == TY_NULLABLE)) {
            upcast(e, want);
            return;
        }
    }
    if (want->kind == TY_NULLABLE && (e->type->kind == TY_NULL || ty_equal(e->type, want->elem))) {
        if (e->type->kind == TY_NULL) e->type = want;
        return;
    }
    if (e->type->kind == TY_NULL)
        xs_error(e->pos, xs_sprintf("declare it as %s | null", ty_name(want)), "%s must be %s, but this is null", what, ty_name(want));
    const char *help = NULL;
    if (e->type->kind == TY_NULLABLE && ty_equal(e->type->elem, want))
        help = "it may be null; check first: if (x !== null) { ... }";
    if (e->type->kind == TY_INT && want->kind == TY_NUMBER) help = "convert explicitly: Number(x)";
    else if (e->type->kind == TY_NUMBER && want->kind == TY_INT) help = "convert explicitly: Math.trunc(x), Math.floor(x), Math.ceil(x) or Math.round(x)";
    else if (is_numeric(e->type) && want->kind == TY_STRING) help = "convert explicitly: String(x)";
    xs_error(e->pos, help, "%s must be %s, but this is %s", what, ty_name(want), ty_name(e->type));
}

static Type *unify_numeric(Expr *e, const char *what) {
    Expr *a = e->lhs, *b = e->rhs;
    if (!is_numeric(a->type) && a->type->kind != TY_LITINT)
        xs_error(a->pos, NULL, "left operand of %s must be int or number, but this is %s", what, ty_name(a->type));
    if (!is_numeric(b->type) && b->type->kind != TY_LITINT)
        xs_error(b->pos, NULL, "right operand of %s must be int or number, but this is %s", what, ty_name(b->type));
    if (a->type->kind == TY_LITINT && b->type->kind == TY_LITINT) return ty_litint;
    if (a->type->kind == TY_LITINT) coerce_lit(a, b->type);
    if (b->type->kind == TY_LITINT) coerce_lit(b, a->type);
    if (a->type->kind == TY_INT && b->type->kind == TY_NUMBER) widen_inferred(a);
    else if (a->type->kind == TY_NUMBER && b->type->kind == TY_INT) widen_inferred(b);
    if (!ty_equal(a->type, b->type))
        xs_error(e->pos, "int and number do not mix; convert with Number(i) or Math.trunc(x)",
                 "cannot apply %s to %s and %s", what, ty_name(a->type), ty_name(b->type));
    return a->type;
}

static void check_args(Expr *e, Func *f, const char *what) {
    int required = f->nparams;
    while (required > 0 && f->params[required - 1].dflt) required--;
    if (e->args.len < required || e->args.len > f->nparams) {
        if (required == f->nparams) xs_error(e->pos, NULL, "%s takes %d argument(s), got %d", what, f->nparams, e->args.len);
        xs_error(e->pos, NULL, "%s takes %d to %d argument(s), got %d", what, required, f->nparams, e->args.len);
    }
    /* left-out arguments: the declared defaults, evaluated at the call as in JavaScript */
    for (int i = e->args.len; i < f->nparams; i++) {
        Expr *d = xs_alloc(sizeof *d);
        *d = *f->params[i].dflt;
        expr_list_push(&e->args, d);
    }
    for (int i = 0; i < e->args.len; i++) {
        check_expr(e->args.items[i], f->params[i].type);
        expect_type(e->args.items[i], f->params[i].type, xs_sprintf("argument '%s' of %s", f->params[i].name, what));
    }
}

static void check_math(Expr *e) {
    static const struct { const char *name; int nargs; } fns[] = {
        {"sqrt", 1}, {"floor", 1}, {"ceil", 1}, {"abs", 1}, {"round", 1}, {"trunc", 1},
        {"min", 2}, {"max", 2}, {"pow", 2}, {"sin", 1}, {"cos", 1}, {"tan", 1}, {"log", 1}, {"exp", 1}, {"random", 0},
        {"log2", 1}, {"log10", 1}, {"log1p", 1}, {"expm1", 1}, {"cbrt", 1}, {"sign", 1}, {"asin", 1}, {"acos", 1}, {"atan", 1},
        {"sinh", 1}, {"cosh", 1}, {"tanh", 1}, {"asinh", 1}, {"acosh", 1}, {"atanh", 1}, {"atan2", 2}, {"hypot", 2}, {"fround", 1},
    };
    static const char *consts[] = {"PI", "E", "LN2", "LN10", "LOG2E", "LOG10E", "SQRT2", "SQRT1_2"};
    if (!e->is_call) {
        for (size_t i = 0; i < sizeof consts / sizeof consts[0]; i++)
            if (strcmp(e->str, consts[i]) == 0) {
                e->type = ty_number;
                return;
            }
        xs_error(e->pos, "supported: Math.PI, E, LN2, LN10, LOG2E, LOG10E, SQRT2, SQRT1_2", "unknown Math constant '%s'", e->str);
    }
    const char *fn = e->str;
    if (strcmp(fn, "imul") == 0 || strcmp(fn, "clz32") == 0) { /* 32-bit integer math: ints */
        int na = fn[0] == 'i' ? 2 : 1;
        if (e->args.len != na) xs_error(e->pos, NULL, "Math.%s takes %d argument(s), got %d", fn, na, e->args.len);
        for (int k = 0; k < na; k++) {
            check_expr(e->args.items[k], ty_int);
            if (e->args.items[k]->type->kind == TY_LITINT) coerce_lit(e->args.items[k], ty_int);
            if (!is_numeric(e->args.items[k]->type)) xs_error(e->args.items[k]->pos, NULL, "argument of Math.%s must be int or number", fn);
        }
        e->type = ty_int;
        return;
    }
    for (size_t i = 0; i < sizeof fns / sizeof fns[0]; i++) {
        if (strcmp(fn, fns[i].name) != 0) continue;
        bool minmax = strcmp(fn, "min") == 0 || strcmp(fn, "max") == 0; /* any number of arguments, as in JS */
        if (!minmax && e->args.len != fns[i].nargs)
            xs_error(e->pos, NULL, "Math.%s takes %d argument(s), got %d", fn, fns[i].nargs, e->args.len);
        for (int k = 0; k < e->args.len; k++) check_expr(e->args.items[k], NULL);
        bool generic = strcmp(fn, "abs") == 0 || strcmp(fn, "min") == 0 || strcmp(fn, "max") == 0;
        bool to_int = strcmp(fn, "floor") == 0 || strcmp(fn, "ceil") == 0 || strcmp(fn, "round") == 0 || strcmp(fn, "trunc") == 0;
        if (generic) {
            Type *t = NULL;
            for (int k = 0; k < e->args.len; k++)
                if (is_numeric(e->args.items[k]->type)) t = e->args.items[k]->type;
            if (!t || e->args.len == 0) t = ty_number; /* Math.max(): -Infinity */
            for (int k = 0; k < e->args.len; k++) expect_type(e->args.items[k], t, xs_sprintf("argument of Math.%s", fn));
            e->type = t;
            return;
        }
        for (int k = 0; k < e->args.len; k++) expect_type(e->args.items[k], ty_number, xs_sprintf("argument of Math.%s", fn));
        e->type = to_int ? ty_int : ty_number;
        return;
    }
    {
        const char *names[sizeof fns / sizeof fns[0]];
        for (size_t i = 0; i < sizeof fns / sizeof fns[0]; i++) names[i] = fns[i].name;
        xs_error(e->pos, xs_help_suggest(e->str, names, (int)(sizeof fns / sizeof fns[0]), "supported: sqrt floor ceil abs round trunc min max pow sin cos tan log exp"), "unknown Math function '%s'", e->str);
    }
}

/* split, replace, replaceAll, padStart, padEnd, repeat, lastIndexOf, trimStart, trimEnd, charAt */
/* s.match / matchAll / search, and split / replace / replaceAll with a RegExp */
static bool check_string_regex(Expr *e) {
    if (!e->is_call || e->args.len < 1) return false;
    const char *m = e->str;
    bool rx_only = strcmp(m, "match") == 0 || strcmp(m, "matchAll") == 0 || strcmp(m, "search") == 0;
    bool rx_maybe = strcmp(m, "split") == 0 || strcmp(m, "replace") == 0 || strcmp(m, "replaceAll") == 0;
    if (!rx_only && !rx_maybe) return false;
    Expr *a0 = e->args.items[0];
    check_expr(a0, NULL);
    if (!is_regexp(a0->type)) {
        if (rx_only) xs_error(a0->pos, "pass a RegExp: s.match(/a+/), or new RegExp(text)", "string.%s takes a RegExp", m);
        return false; /* the string versions (the argument is checked again there: no harm) */
    }
    prog->uses_regex = true;
    Class *mc = find_class("RegExpExecArray");
    e->builtin.self_first = true;
    if (strcmp(m, "match") == 0 || strcmp(m, "matchAll") == 0 || strcmp(m, "search") == 0) {
        if (e->args.len != 1) xs_error(e->pos, NULL, "string.%s takes one argument", m);
        if (m[5] == 'A') { e->builtin.cfn = "xs_str_match_all_re"; e->type = ty_array_of(ty_object_of(mc)); }
        else if (m[0] == 'm') { e->builtin.cfn = "xs_str_match_re"; e->type = ty_nullable_of(ty_object_of(mc)); }
        else { e->builtin.cfn = "xs_str_search_re"; e->type = ty_int; }
        return true;
    }
    if (strcmp(m, "split") == 0) {
        if (e->args.len > 2) xs_error(e->pos, NULL, "string.split takes a separator and an optional limit");
        if (e->args.len == 2) { check_expr(e->args.items[1], ty_int); expect_type(e->args.items[1], ty_int, "the split limit"); }
        else {
            Expr *lim = xs_alloc(sizeof *lim);
            lim->kind = E_NUMBER; lim->pos = e->pos; lim->is_int_lit = true; lim->ival = -1; lim->num = -1; lim->type = ty_int; lim->field = -1;
            expr_list_push(&e->args, lim);
        }
        e->builtin.cfn = "xs_str_split_re";
        e->type = ty_array_of(ty_string);
        return true;
    }
    /* replace / replaceAll: a string with $1, $<name>, $&..., or (match, p1, ...) => string */
    if (e->args.len != 2) xs_error(e->pos, NULL, "string.%s takes a pattern and a replacement", m);
    bool all = strcmp(m, "replaceAll") == 0;
    Expr *r = e->args.items[1];
    if (r->kind == E_ARROW || (r->kind == E_IDENT && 0)) {
        int np = r->arrow->fn->nparams;
        if (np > 8) xs_error(r->pos, NULL, "a replacement callback takes at most 8 parameters (the match and 7 groups)");
        Type **ps = xs_alloc(sizeof(Type *) * (size_t)(np ? np : 1));
        for (int k = 0; k < np; k++) ps[k] = ty_string;
        Type *ft = ty_func(ps, np, ty_string);
        check_expr(r, ft);
        expect_type(r, ft, "the replacement callback (match: string, p1: string, ...) => string");
        Expr *ar = xs_alloc(sizeof *ar);
        ar->kind = E_NUMBER; ar->pos = e->pos; ar->is_int_lit = true; ar->ival = np; ar->num = np; ar->type = ty_int; ar->field = -1;
        expr_list_push(&e->args, ar);
        e->builtin.cfn = all ? "xs_str_replace_all_re_fn" : "xs_str_replace_re_fn";
    } else {
        check_expr(r, ty_string);
        if (r->type->kind == TY_FUNC) xs_error(r->pos, "write the callback inline: s.replace(re, (m, p1) => ...)", "a replacement callback must be an arrow function written in place");
        expect_type(r, ty_string, xs_sprintf("the replacement of string.%s", m));
        e->builtin.cfn = all ? "xs_str_replace_all_re" : "xs_str_replace_re";
    }
    e->type = ty_string;
    return true;
}

static bool check_string_member_more(Expr *e) {
    static const struct { const char *name; const char *cfn; int min, max; TypeKind a1, a2; int ret; } ms[] = {
        {"split", "xs_str_split", 1, 1, TY_STRING, TY_VOID, 0},
        {"replace", "xs_str_replace", 2, 2, TY_STRING, TY_STRING, 1},
        {"replaceAll", "xs_str_replace_all", 2, 2, TY_STRING, TY_STRING, 1},
        {"padStart", "xs_str_pad_start", 1, 2, TY_INT, TY_STRING, 1},
        {"padEnd", "xs_str_pad_end", 1, 2, TY_INT, TY_STRING, 1},
        {"repeat", "xs_str_repeat", 1, 1, TY_INT, TY_VOID, 1},
        {"lastIndexOf", "xs_str_last_index_of", 1, 1, TY_STRING, TY_VOID, 2},
        {"trimStart", "xs_str_trim_start", 0, 0, TY_VOID, TY_VOID, 1},
        {"trimEnd", "xs_str_trim_end", 0, 0, TY_VOID, TY_VOID, 1},
        {"charAt", "xs_str_char_at", 1, 1, TY_INT, TY_VOID, 1},
        {"charCodeAt", "xs_str_code_at", 0, 1, TY_INT, TY_VOID, 3},
        {"codePointAt", "xs_str_code_at", 0, 1, TY_INT, TY_VOID, 3},
    };
    if (!e->is_call) return false;
    for (size_t i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        if (strcmp(e->str, ms[i].name) != 0) continue;
        if (e->args.len < ms[i].min || e->args.len > ms[i].max)
            xs_error(e->pos, NULL, "string.%s takes %d to %d argument(s), got %d", e->str, ms[i].min, ms[i].max, e->args.len);
        for (int k = 0; k < e->args.len; k++) {
            Type *want = (k == 0 ? ms[i].a1 : ms[i].a2) == TY_STRING ? ty_string : ty_int;
            check_expr(e->args.items[k], want);
            expect_type(e->args.items[k], want, xs_sprintf("argument of string.%s", e->str));
        }
        if ((strcmp(e->str, "padStart") == 0 || strcmp(e->str, "padEnd") == 0) && e->args.len == 1) {
            Expr *sp = xs_alloc(sizeof *sp);
            sp->kind = E_STRING; sp->pos = e->pos; sp->str = " "; sp->type = ty_string; sp->field = -1;
            sp->lit_id = intern(" ");
            expr_list_push(&e->args, sp);
        }
        if (ms[i].ret == 3 && e->args.len == 0) { /* s.charCodeAt(): index 0 */
            Expr *z = xs_alloc(sizeof *z);
            z->kind = E_NUMBER; z->pos = e->pos; z->is_int_lit = true; z->type = ty_int; z->field = -1;
            expr_list_push(&e->args, z);
        }
        /* charCodeAt / codePointAt: the code point at that index (xisto indexes code points, see linguagem.md),
           NaN past the end */
        e->type = ms[i].ret == 0 ? ty_array_of(ty_string) : ms[i].ret == 1 ? ty_string : ms[i].ret == 3 ? ty_number : ty_int;
        e->builtin.cfn = ms[i].cfn;
        e->builtin.self_first = true;
        return true;
    }
    return false;
}

static void check_string_member(Expr *e) {
    if (check_string_regex(e)) return;
    if (check_string_member_more(e)) return;
    static const struct { const char *name; const char *cfn; int nargs; TypeKind arg; TypeKind ret; } ms[] = {
        {"startsWith", "xs_str_starts_with", 1, TY_STRING, TY_BOOLEAN},
        {"endsWith", "xs_str_ends_with", 1, TY_STRING, TY_BOOLEAN},
        {"includes", "xs_str_includes", 1, TY_STRING, TY_BOOLEAN},
        {"indexOf", "xs_str_index_of", 2, TY_STRING, TY_INT},
        {"slice", "xs_str_slice", 2, TY_INT, TY_STRING},
        {"substring", "xs_str_substring", 2, TY_INT, TY_STRING},
        {"trim", "xs_str_trim", 0, TY_VOID, TY_STRING},
        {"toUpperCase", "xs_str_upper", 0, TY_VOID, TY_STRING},
        {"toLowerCase", "xs_str_lower", 0, TY_VOID, TY_STRING},
    };
    if (!e->is_call) {
        if (strcmp(e->str, "length") == 0) {
            e->type = ty_int;
            e->builtin.cfn = "xs_str_len";
            e->builtin.self_first = true;
            return;
        }
        xs_error(e->pos, "supported: length, startsWith, endsWith, includes, indexOf, slice, trim, toUpperCase, toLowerCase", "string has no property '%s'", e->str);
    }
    for (size_t i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        if (strcmp(e->str, ms[i].name) != 0) continue;
        int n = ms[i].nargs;
        bool slice = strcmp(e->str, "slice") == 0 || strcmp(e->str, "substring") == 0;
        bool index_of = strcmp(e->str, "indexOf") == 0;
        if ((slice || index_of) ? (e->args.len < 1 || e->args.len > 2) : e->args.len != n)
            xs_error(e->pos, NULL, "string.%s takes %d argument(s), got %d", e->str, n, e->args.len);
        Type *argt = ms[i].arg == TY_STRING ? ty_string : ty_int;
        for (int k = 0; k < e->args.len; k++) {
            Type *want = (index_of && k == 1) ? ty_int : argt; /* indexOf(search, fromIndex) */
            check_expr(e->args.items[k], want);
            expect_type(e->args.items[k], want, xs_sprintf("argument of string.%s", e->str));
        }
        if (index_of && e->args.len == 1) { /* default start: 0 */
            Expr *zero = xs_alloc(sizeof *zero);
            zero->kind = E_NUMBER; zero->pos = e->pos; zero->is_int_lit = true; zero->type = ty_int; zero->field = -1;
            expr_list_push(&e->args, zero);
        }
        if (slice && e->args.len == 1) { /* slice(start) means slice(start, length): INT64_MIN tells the runtime "to the end" */
            Expr *end = xs_alloc(sizeof *end);
            end->kind = E_NUMBER;
            end->pos = e->pos;
            end->num = -9223372036854775808.0;
            end->ival = (-9223372036854775807LL - 1);
            end->is_int_lit = true;
            end->type = ty_int;
            end->field = -1;
            expr_list_push(&e->args, end);
        }
        e->type = ms[i].ret == TY_STRING ? ty_string : ms[i].ret == TY_INT ? ty_int : ty_boolean;
        e->builtin.cfn = ms[i].cfn;
        e->builtin.self_first = true;
        return;
    }
    {
        const char *names[sizeof ms / sizeof ms[0] + 1];
        for (size_t i = 0; i < sizeof ms / sizeof ms[0]; i++) names[i] = ms[i].name;
        names[sizeof ms / sizeof ms[0]] = "length";
        xs_error(e->pos, xs_help_suggest(e->str, names, (int)(sizeof ms / sizeof ms[0]) + 1, "supported: length, startsWith, endsWith, includes, indexOf, lastIndexOf, slice, substring, trim, trimStart, trimEnd, toUpperCase, toLowerCase, split, replace, replaceAll, padStart, padEnd, repeat, charAt"), "string has no method '%s'", e->str);
    }
}

/* Stream<T>: pull-based, lazy, typed. Sources: lines(path), chunks(path, bytes). */
static void check_stream_member(Expr *e) {
    Type *t = e->lhs->type;
    Type *elem = t->elem;
    const char *m = e->str;
    if (!e->is_call) xs_error(e->pos, "streams have methods only: map, filter, batch, take, forEach, reduce, toArray, count, writeLines", "Stream has no property '%s'", m);
    #define NARGS(n) do { if (e->args.len != (n)) xs_error(e->pos, NULL, "Stream.%s takes %d argument(s), got %d", m, (n), e->args.len); } while (0)
    if (strcmp(m, "map") == 0 || strcmp(m, "filter") == 0 || strcmp(m, "forEach") == 0) {
        NARGS(1);
        Expr *cb = e->args.items[0];
        Type **ps = xs_alloc(sizeof(Type *)); ps[0] = elem;
        bool is_map = strcmp(m, "map") == 0;
        Type *cbt = ty_func(ps, 1, is_map ? NULL : strcmp(m, "filter") == 0 ? ty_boolean : ty_void);
        if (is_map && !(cb->kind == E_ARROW && !cb->arrow->fn->ret)) { check_expr(cb, NULL); }
        else check_expr(cb, cbt);
        if (cb->type->kind != TY_FUNC || cb->type->nparams != 1 || !ty_equal(cb->type->params[0], elem))
            xs_error(cb->pos, NULL, "the callback of Stream.%s takes one %s", m, ty_name(elem));
        if (!is_map) expect_type(cb, cbt, xs_sprintf("callback of Stream.%s", m));
        if (is_map) {
            Type *rt = cb->type->ret;
            if (rt->kind == TY_VOID || rt->kind == TY_FUNC || rt->kind == TY_NULLABLE) xs_error(cb->pos, NULL, "map cannot produce a stream of %s", ty_name(rt));
            e->type = ty_stream_of(rt);
        } else if (strcmp(m, "filter") == 0) e->type = t;
        else e->type = ty_void;
        e->builtin.cfn = m;
        return;
    }
    if (strcmp(m, "batch") == 0 || strcmp(m, "take") == 0) {
        NARGS(1);
        check_expr(e->args.items[0], ty_int);
        expect_type(e->args.items[0], ty_int, xs_sprintf("argument of Stream.%s", m));
        if (strcmp(m, "batch") == 0) {
            e->type = ty_stream_of(ty_array_of(elem));
            check_type_supported(e->pos, ty_array_of(elem));
        } else e->type = t;
        e->builtin.cfn = m;
        return;
    }
    if (strcmp(m, "reduce") == 0) {
        NARGS(2);
        Expr *init = e->args.items[1];
        check_expr(init, NULL);
        if (init->type->kind == TY_LITINT) coerce_lit(init, is_numeric(elem) ? elem : ty_number); /* reduce((a, n) => a + n, 0) over ints stays int */
        settle(init);
        Type *acc = init->type;
        Type **ps = xs_alloc(sizeof(Type *) * 2); ps[0] = acc; ps[1] = elem;
        Type *cbt = ty_func(ps, 2, acc);
        check_expr(e->args.items[0], cbt);
        expect_type(e->args.items[0], cbt, "callback of Stream.reduce");
        e->type = acc;
        e->builtin.cfn = m;
        return;
    }
    if (strcmp(m, "toArray") == 0) { NARGS(0); e->type = ty_array_of(elem); check_type_supported(e->pos, e->type); e->builtin.cfn = m; return; }
    if (strcmp(m, "count") == 0) { NARGS(0); e->type = ty_int; e->builtin.cfn = m; return; }
    if (strcmp(m, "writeLines") == 0) {
        NARGS(1);
        if (elem->kind != TY_STRING) xs_error(e->pos, "map the elements to strings first", "writeLines needs a Stream<string>, this is %s", ty_name(t));
        check_expr(e->args.items[0], ty_string);
        expect_type(e->args.items[0], ty_string, "path of writeLines");
        e->type = ty_void;
        e->builtin.cfn = m;
        return;
    }
    #undef NARGS
    static const char *names[] = {"map", "filter", "batch", "take", "forEach", "reduce", "toArray", "count", "writeLines"};
    xs_error(e->pos, xs_help_suggest(m, names, 9, NULL), "Stream has no method '%s'", m);
}

static void check_map_member(Expr *e) {
    Type *t = e->lhs->type;
    bool is_map = t->kind == TY_MAP;
    Type *k = is_map ? t->key : t->elem;
    Type *v = t->elem;
    const char *m = e->str;
    if (!e->is_call) {
        if (strcmp(m, "size") == 0) { e->type = ty_int; e->builtin.cfn = "size"; return; }
        xs_error(e->pos, is_map ? "Map has: size, get, set, has, delete, clear, keys, values, entries" : "Set has: size, add, has, delete, clear, values", "%s has no property '%s'", ty_name(t), m);
    }
    #define ARGS(n) do { if (e->args.len != (n)) xs_error(e->pos, NULL, "%s.%s takes %d argument(s), got %d", is_map ? "Map" : "Set", m, (n), e->args.len); } while (0)
    #define ARG(i, ty, what) do { check_expr(e->args.items[i], ty); expect_type(e->args.items[i], ty, what); } while (0)
    if (strcmp(m, "has") == 0 || strcmp(m, "delete") == 0) {
        ARGS(1); ARG(0, k, is_map ? "Map key" : "Set element");
        e->type = ty_boolean; e->builtin.cfn = m; return;
    }
    if (strcmp(m, "clear") == 0) { ARGS(0); e->type = ty_void; e->builtin.cfn = m; return; }
    if (strcmp(m, "values") == 0) { ARGS(0); e->type = ty_array_of(v); check_type_supported(e->pos, e->type); e->builtin.cfn = m; return; }
    if (is_map && strcmp(m, "keys") == 0) { ARGS(0); e->type = ty_array_of(k); e->builtin.cfn = m; return; }
    if (is_map && strcmp(m, "entries") == 0) { /* [key, value] tuples, in insertion order */
        ARGS(0);
        Type **ps = xs_alloc(sizeof(Type *) * 2);
        ps[0] = k;
        ps[1] = v;
        e->type = ty_array_of(tuple_type(ps, 2, e->pos));
        e->builtin.cfn = m;
        return;
    }
    if (is_map && strcmp(m, "get") == 0) {
        ARGS(1); ARG(0, k, "Map key");
        e->type = v->kind == TY_UNKNOWN ? v : ty_is_heap(v) ? ty_nullable_of(v) : v; /* primitives: a missing key is a runtime error; use has() first; unknown: null */
        e->builtin.cfn = m; return;
    }
    if (is_map && strcmp(m, "set") == 0) { ARGS(2); ARG(0, k, "Map key"); ARG(1, v, "Map value"); e->type = t; e->builtin.cfn = m; return; }
    if (!is_map && strcmp(m, "add") == 0) { ARGS(1); ARG(0, k, "Set element"); e->type = t; e->builtin.cfn = m; return; }
    #undef ARGS
    #undef ARG
    static const char *mnames[] = {"size", "get", "set", "has", "delete", "clear", "keys", "values", "add", "entries"};
    xs_error(e->pos, xs_help_suggest(m, mnames, 9, NULL), "%s has no method '%s'", ty_name(t), m);
}

static Field *find_field(Class *c, const char *name, int *index) {
    for (int i = 0; i < c->nfields; i++)
        if (strcmp(c->fields[i].name, name) == 0) {
            if (index) *index = i;
            return &c->fields[i];
        }
    return NULL;
}

static Func *find_method(Class *c, const char *name) {
    for (int i = 0; i < c->nmethods; i++)
        if (strcmp(c->methods[i]->name, name) == 0) return c->methods[i];
    return NULL;
}

/* ---- inheritance ---- */
static bool is_subclass(Class *sub, Class *base) {
    for (Class *c = sub; c; c = c->base) if (c == base) return true;
    return false;
}
/* the constructor `new C(...)` runs: its own, or the nearest base's */
static Func *eff_ctor(Class *c) {
    for (; c; c = c->base) if (c->ctor) return c->ctor;
    return NULL;
}
/* some class below c has its own m: a call through c must look at the object's class */
static bool overridden_below(Class *c, Func *m) {
    for (int i = 0; i < c->nsubs; i++) {
        Func *x = find_method(c->subs[i], m->name);
        if (x != m || overridden_below(c->subs[i], m)) return true;
    }
    return false;
}
/* a subclass value where its base is expected: typed as the base (the emitter casts) */
static void upcast(Expr *e, Type *to) {
    Expr *v = expr_copy(e);
    memset(e, 0, sizeof *e);
    e->kind = E_UPCAST;
    e->pos = v->pos;
    e->lhs = v;
    e->type = to;
    e->field = -1;
}
static int super_calls; /* super(...) calls met while checking a constructor */

/* ---- generics: function f<T>(x: T): T is a template; each list of types used makes an instance (parsed again
   with T bound, checked like any function, compiled on its own: no cost at run time) ---- */
static Func **gen_work;
static int ngen_work;
static bool has_tparam(Type *t) {
    if (!t) return false;
    switch (t->kind) {
    case TY_TPARAM: return true;
    case TY_ARRAY: case TY_NULLABLE: case TY_SET: case TY_STREAM: case TY_CHAN: return has_tparam(t->elem);
    case TY_MAP: return has_tparam(t->key) || has_tparam(t->elem);
    case TY_FUNC:
        for (int i = 0; i < t->nparams; i++) if (has_tparam(t->params[i])) return true;
        return has_tparam(t->ret);
    case TY_OBJECT:
        for (int i = 0; i < t->ntargs; i++) if (has_tparam(t->targs[i])) return true;
        return false;
    default: return false;
    }
}
/* binds the type parameters of pat from got (the type of an argument, or the expected result) */
static void unify(Type *pat, Type *got, Func *tmpl, Type **bind) {
    if (!pat || !got) return;
    if (pat->kind == TY_TPARAM) {
        if (got->kind == TY_NULL || got->kind == TY_VOID) return;
        if (got->kind == TY_LITINT) got = ty_number; /* as TypeScript infers it */
        for (int i = 0; i < tmpl->ntparams; i++)
            if (strcmp(tmpl->tparams[i], pat->tname) == 0 && !bind[i]) bind[i] = got;
        return;
    }
    if (pat->kind == TY_NULLABLE) { unify(pat->elem, got->kind == TY_NULLABLE ? got->elem : got, tmpl, bind); return; }
    if (pat->kind != got->kind) return;
    switch (pat->kind) {
    case TY_ARRAY: case TY_SET: case TY_STREAM: case TY_CHAN: unify(pat->elem, got->elem, tmpl, bind); return;
    case TY_MAP: unify(pat->key, got->key, tmpl, bind); unify(pat->elem, got->elem, tmpl, bind); return;
    case TY_FUNC:
        if (pat->nparams != got->nparams) return;
        for (int i = 0; i < pat->nparams; i++) unify(pat->params[i], got->params[i], tmpl, bind);
        unify(pat->ret, got->ret, tmpl, bind);
        return;
    case TY_OBJECT: /* Box<T> against an instance of Box */
        if (got->cls && got->cls->generic_of && pat->cls && got->cls->generic_of->name && strcmp(got->cls->generic_of->name, pat->cls->name) == 0) {
            for (int i = 0; i < pat->ntargs && i < got->ntargs; i++) unify(pat->targs[i], got->targs[i], tmpl, bind);
        }
        return;
    default: return;
    }
}
static void resolve_signature(Func *f);
static Func *instantiate_func(Func *tmpl, Type **args) {
    Buf key = {0};
    for (int i = 0; i < tmpl->ntparams; i++) buf_printf(&key, "%s%s", i ? "," : "", ty_name(args[i]));
    for (int i = 0; i < tmpl->ninstances; i++) if (strcmp(tmpl->inst_keys[i], key.data) == 0) { free(key.data); return tmpl->instances[i]; }
    Func *inst = xs_parse_instance(tmpl, args);
    inst->cname = xs_sprintf("%s_g%d", tmpl->cname, tmpl->ninstances);
    tmpl->instances = realloc(tmpl->instances, sizeof(Func *) * (size_t)(tmpl->ninstances + 1));
    tmpl->inst_keys = realloc(tmpl->inst_keys, sizeof(char *) * (size_t)(tmpl->ninstances + 1));
    tmpl->instances[tmpl->ninstances] = inst;
    tmpl->inst_keys[tmpl->ninstances++] = key.data;
    /* its signature, in its own module (the caller's scope is put back) */
    Module *sm = cur_mod;
    Scope *ss = scope;
    const char *sfn = xs_file_name, *ssrc = xs_source;
    enter_module(tmpl->cls ? tmpl->cls->mod : tmpl->mod);
    resolve_signature(inst);
    cur_mod = sm;
    scope = ss;
    xs_file_name = sfn;
    xs_source = ssrc;
    gen_work = realloc(gen_work, sizeof(Func *) * (size_t)(ngen_work + 1));
    gen_work[ngen_work++] = inst;
    return inst;
}
/* a call of a generic function or method: T from the explicit types, the arguments, or the expected result */
static Func *check_generic_call(Expr *e, Func *tmpl, Type *expected, const char *what) {
    Type **bind = xs_alloc(sizeof(Type *) * (size_t)tmpl->ntparams);
    if (e->ntargs) {
        if (e->ntargs != tmpl->ntparams) xs_error(e->pos, NULL, "%s takes %d type argument(s), got %d", what, tmpl->ntparams, e->ntargs);
        for (int i = 0; i < e->ntargs; i++) bind[i] = resolve(e->targs[i], e->pos);
    }
    int required = tmpl->nparams;
    while (required > 0 && tmpl->params[required - 1].dflt) required--;
    if (e->args.len < required || e->args.len > tmpl->nparams)
        xs_error(e->pos, NULL, "%s takes %d%s argument(s), got %d", what, required, required == tmpl->nparams ? "" : xs_sprintf(" to %d", tmpl->nparams), e->args.len);
    bool *done = xs_alloc(sizeof(bool) * (size_t)(e->args.len + 1));
    for (int i = 0; i < e->args.len; i++) {
        if (!has_tparam(tmpl->params[i].type) || e->ntargs) continue;
        check_expr(e->args.items[i], NULL);
        done[i] = true;
        unify(tmpl->params[i].type, e->args.items[i]->type, tmpl, bind);
    }
    if (expected && has_tparam(tmpl->ret)) unify(tmpl->ret, expected, tmpl, bind);
    for (int i = 0; i < tmpl->ntparams; i++)
        if (!bind[i]) xs_error(e->pos, xs_sprintf("give it: %s<Type>(...)", tmpl->name), "cannot infer type parameter %s of %s", tmpl->tparams[i], what);
    Func *inst = instantiate_func(tmpl, bind);
    for (int i = e->args.len; i < inst->nparams; i++) { /* defaults, as written for this instance */
        Expr *d = xs_alloc(sizeof *d);
        *d = *inst->params[i].dflt;
        expr_list_push(&e->args, d);
    }
    for (int i = 0; i < e->args.len; i++) {
        if (!done[i]) check_expr(e->args.items[i], inst->params[i].type);
        expect_type(e->args.items[i], inst->params[i].type, xs_sprintf("argument '%s' of %s", inst->params[i].name, what));
    }
    return inst;
}

/* private: the declaring class only; protected: it and its subclasses (checked at compile time, as TypeScript) */
static bool checking_generated; /* the compiler's own code (reflection adapters) reaches private members */
static void check_access(int access, Class *owner, const char *name, Pos pos) {
    if (!access || !owner || checking_generated) return;
    Class *in = cur_class ? cur_class : cur_func ? cur_func->cls : NULL;
    if (access == 2 && in != owner)
        xs_error(pos, xs_sprintf("use it inside %s, or make it public", owner->name), "'%s' is private to %s", name, owner->name);
    if (access == 1 && !(in && is_subclass(in, owner)))
        xs_error(pos, xs_sprintf("use it inside %s or a class that extends it", owner->name), "'%s' is protected in %s", name, owner->name);
}
static const char *prop_name(const char *m) { return strncmp(m, "__get_", 6) == 0 || strncmp(m, "__set_", 6) == 0 ? m + 6 : m; }

static void check_object_member(Expr *e, Class *c) {
    e->cls = c;
    if (c->builtin && strcmp(c->name, "Error") == 0 && !e->is_call && strcmp(e->str, "stack") == 0) {
        /* e.stack: built from the trace captured at creation, the first time it is read */
        e->field = -1;
        e->type = ty_string;
        e->builtin.cfn = "xs_Error_stack";
        return;
    }
    if (c->builtin && strcmp(c->name, "Date") == 0) { check_date_member(e); return; }
    if (c->builtin && (strcmp(c->name, "Uint8Array") == 0 || strcmp(c->name, "Buffer") == 0)) { check_bytes_member(e, c); return; }
    if (c->builtin && strcmp(c->name, "Float64Array") == 0) { check_f64_member(e, c); return; }
    if (c->builtin && strcmp(c->name, "RegExpGroups") == 0 && !e->is_call) { /* m.groups.name */
        Expr *nm = xs_alloc(sizeof *nm);
        nm->kind = E_STRING; nm->pos = e->pos; nm->str = e->str; nm->type = ty_string; nm->field = -1;
        nm->lit_id = intern(e->str);
        e->args.len = 0;
        expr_list_push(&e->args, nm);
        e->field = -1;
        e->type = ty_string;
        e->builtin.cfn = "xs_RegExpGroups_get";
        return;
    }
    int idx;
    Field *f = find_field(c, e->str, &idx);
    if (f && e->is_call && f->type->kind == TY_FUNC) { /* obj.run(x) where run is a function-valued field */
        Type *ft = f->type;
        Expr *fld = expr_copy(e);
        fld->is_call = false;
        fld->args.len = 0;
        fld->args.items = NULL;
        fld->field = idx;
        fld->type = ft;
        e->kind = E_CALL;
        e->lhs = fld;
        e->func = NULL;
        e->cls = NULL;
        if (e->args.len != ft->nparams) xs_error(e->pos, NULL, "'%s' takes %d argument(s), got %d", f->name, ft->nparams, e->args.len);
        for (int i = 0; i < e->args.len; i++) {
            check_expr(e->args.items[i], ft->params[i]);
            expect_type(e->args.items[i], ft->params[i], xs_sprintf("argument %d of '%s'", i + 1, f->name));
        }
        e->type = ft->ret;
        return;
    }
    if (c->is_tuple && !e->is_call && strcmp(e->str, "length") == 0) { /* t.length: a constant */
        long long n = c->nfields;
        e->kind = E_NUMBER; e->is_int_lit = true; e->ival = n; e->num = (double)n; e->lhs = NULL; e->field = -1; e->type = ty_int;
        return;
    }
    if (f) {
        if (e->is_call) xs_error(e->pos, NULL, "'%s' is a field of %s, not a method", e->str, c->name);
        check_access(f->access, f->owner, f->name, e->pos);
        e->field = idx;
        e->type = f->type;
        e->cls = c;
        if (e->type->kind == TY_NULLABLE) { /* narrowed by o.f !== null: read as o.f! (checked) */
            const char *key = path_key(e);
            Binding *pb = key ? lookup(key) : NULL;
            if (pb && pb->type->kind != TY_NULLABLE) {
                Expr *inner = xs_alloc(sizeof *inner);
                *inner = *e;
                memset(e, 0, sizeof *e);
                e->kind = E_AS;
                e->op = TK_NOT;
                e->pos = inner->pos;
                e->lhs = inner;
                e->field = -1;
                e->type = inner->type->elem;
            }
        }
        return;
    }
    if (!e->is_call && !c->builtin) { /* obj.x with get x(): a call */
        Func *g = find_method(c, xs_sprintf("__get_%s", e->str));
        if (!g && find_method(c, xs_sprintf("__set_%s", e->str))) xs_error(e->pos, "add get %s(): T { ... }", "'%s' of %s has a setter but no getter", e->str, c->name);
        if (g) { e->str = g->name; e->is_call = true; }
    }
    Func *m = find_method(c, e->str);
    if (!m) {
        int n; const char **cands = member_names(c, &n);
        if (c->builtin && strcmp(c->name, "Error") == 0) { cands = (const char *[]){"message", "name", "stack"}; n = 3; }
        xs_error(e->pos, xs_help_suggest(e->str, cands, n, NULL), "%s has no member '%s'", c->name, e->str);
    }
    if (!e->is_call) xs_error(e->pos, "call it: obj.method(...)", "'%s' is a method of %s, and methods are not values", e->str, c->name);
    if (!c->builtin) check_access(m->access, m->cls, prop_name(m->name), e->pos);
    if (m->is_setter && e->args.len == 1 && strncmp(e->str, "__set_", 6) != 0) xs_error(e->pos, NULL, "'%s' is a setter: assign it", prop_name(m->name));
    if (m->ntparams) { /* a generic method: its instance for these types */
        Func *inst = check_generic_call(e, m, NULL, xs_sprintf("%s.%s", c->name, m->name));
        e->func = inst;
        e->type = inst->ret;
        return;
    }
    if (e->ntargs) xs_error(e->pos, NULL, "%s.%s is not generic", c->name, m->name);
    check_args(e, m, xs_sprintf("%s.%s", c->name, m->name));
    e->func = m;
    e->type = m->ret;
    if (!c->builtin && !e->super_call && (m->is_abstract || overridden_below(c, m))) { /* dispatched by the object's class */
        e->virt = true;
        m->virt_target = true;
        for (int i = 0; i < m->noverrides; i++) m->overrides[i]->virt_target = true;
    }
    if (c->builtin) {
        e->builtin.cfn = m->cname;
        e->builtin.self_first = true;
    }
}

/* The scheme of a fetch URL when it is known at compile time: 1 https, 0 http, -1 only known at run time.
   Looks at the leading literal of "...", `...${x}` and "..." + x. */
static int url_scheme(Expr *u) {
    while (u->kind == E_BINARY && u->op == TK_PLUS) u = u->lhs;
    if (u->kind == E_TEMPLATE && u->args.len > 0) u = u->args.items[0];
    if (u->kind != E_STRING || !u->str) return -1;
    if (strncmp(u->str, "https://", 8) == 0) return 1;
    if (strncmp(u->str, "http://", 7) == 0) return 0;
    return -1;
}

/* { a: 1, b: { c: "x" } } where an object is expected: an interface made from the literal's shape */
static void anon_objlit(Expr *e) {
    Class *c = xs_alloc(sizeof *c);
    static int nanon;
    c->name = xs_sprintf("{anonymous %d}", ++nanon);
    c->pos = e->pos;
    c->is_interface = true;
    c->mod = cur_mod;
    c->cname = xs_sprintf("anon%d", nanon);
    c->used = true;
    c->fields = xs_alloc(sizeof(Field) * (size_t)(e->args.len ? e->args.len : 1));
    for (int i = 0; i < e->args.len; i++) {
        Expr *v = e->args.items[i];
        for (int k = 0; k < i; k++) if (strcmp(e->keys[k], e->keys[i]) == 0) xs_error(v->pos, NULL, "field '%s' is given twice", e->keys[i]);
        check_expr(v, v->kind == E_OBJLIT ? ty_json : NULL);
        settle(v);
        if (v->type->kind == TY_NULL) xs_error(v->pos, "give the object a declared type", "cannot infer the type of null here");
        if (!json_ok(v->type, 0)) xs_error(v->pos, NULL, "%s cannot be part of a JSON value", ty_name(v->type));
        c->fields[i] = (Field){.name = e->keys[i], .pos = v->pos, .type = v->type, .cname = xs_sprintf("f_%s", e->keys[i])};
    }
    c->nfields = c->cap_fields = e->args.len;
    prog->classes = realloc(prog->classes, sizeof(Class *) * (size_t)(prog->nclasses + 1));
    prog->classes[prog->nclasses++] = c;
    e->cls = c;
    e->type = ty_object_of(c);
}

/* __reflect_schema(T): T's shape as JSON Schema (the OpenAPI 3.0 dialect: `nullable`), computed here from the
   static type. Named object types go to "$defs" and are referenced, so recursive types terminate; the
   schema is {"$ref": ..., "$defs": {...}} for them, the plain schema otherwise. Each definition starts a line
   (",\"$defs\":{" then "\n\"Name\":{...}," ...), so a package can lift them out without a JSON parser. Generic: what the Nest CLI
   plugin derives from TypeScript types for @nestjs/swagger, usable by any package. */
typedef struct { Class **cls; int n, cap; } SchemaDefs;
static void schema_name(Buf *b, const char *name) {
    for (const char *s = name; *s; s++) buf_putc(b, (isalnum((unsigned char)*s) || *s == '_') ? *s : '_');
}
static void schema_jstr(Buf *b, const char *s) {
    buf_putc(b, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { buf_putc(b, '\\'); buf_putc(b, (char)c); }
        else if (c < 0x20) buf_printf(b, "\\u%04x", c);
        else buf_putc(b, (char)c);
    }
    buf_putc(b, '"');
}
static void schema_of(Buf *b, Type *t, SchemaDefs *defs) {
    if (!t) { buf_puts(b, "{}"); return; }
    if (t->kind == TY_NULLABLE) {
        Type *in = t->elem;
        if (in && in->kind == TY_OBJECT) { buf_puts(b, "{\"allOf\":["); schema_of(b, in, defs); buf_puts(b, "],\"nullable\":true}"); return; }
        Buf inner = {0};
        schema_of(&inner, in, defs);
        if (inner.len >= 2 && strcmp(inner.data, "{}") != 0) { inner.data[inner.len - 1] = 0; buf_puts(b, inner.data); buf_puts(b, ",\"nullable\":true}"); }
        else buf_puts(b, "{\"nullable\":true}");
        free(inner.data);
        return;
    }
    switch (t->kind) {
    case TY_STRING: buf_puts(b, "{\"type\":\"string\"}"); return;
    case TY_INT: case TY_LITINT: buf_puts(b, "{\"type\":\"integer\"}"); return;
    case TY_NUMBER: buf_puts(b, "{\"type\":\"number\"}"); return;
    case TY_BOOLEAN: buf_puts(b, "{\"type\":\"boolean\"}"); return;
    case TY_ARRAY: case TY_SET: buf_puts(b, "{\"type\":\"array\",\"items\":"); schema_of(b, t->elem, defs); buf_putc(b, '}'); return;
    case TY_MAP: buf_puts(b, "{\"type\":\"object\",\"additionalProperties\":"); schema_of(b, t->elem, defs); buf_putc(b, '}'); return;
    case TY_ENUM: {
        Enum *en = t->en;
        buf_printf(b, "{\"type\":\"%s\",\"enum\":[", en->is_string ? "string" : "integer");
        for (int i = 0; i < en->nmembers; i++) {
            if (i) buf_putc(b, ',');
            if (en->members[i].is_string) schema_jstr(b, en->members[i].sval);
            else buf_printf(b, "%lld", en->members[i].ival);
        }
        buf_puts(b, "]}");
        return;
    }
    case TY_OBJECT: {
        Class *c = t->cls;
        int i = 0;
        while (i < defs->n && defs->cls[i] != c) i++;
        if (i == defs->n) {
            if (defs->n == defs->cap) { defs->cap = defs->cap ? defs->cap * 2 : 8; defs->cls = realloc(defs->cls, (size_t)defs->cap * sizeof(Class *)); }
            defs->cls[defs->n++] = c;
        }
        buf_puts(b, "{\"$ref\":\"#/$defs/");
        schema_name(b, c->name);
        buf_puts(b, "\"}");
        return;
    }
    default: buf_puts(b, "{}"); return; /* object, unknown, void, functions: any */
    }
}
static const char *schema_text(Type *t) {
    SchemaDefs defs = {0};
    Buf root = {0}, out = {0};
    schema_of(&root, t, &defs);
    if (!defs.n) return root.data ? root.data : "{}";
    Buf body = {0};
    for (int i = 0; i < defs.n; i++) { /* defs grows while fields are described */
        Class *c = defs.cls[i];
        if (i) buf_putc(&body, ',');
        buf_puts(&body, "\n\""); /* one line per definition: a raw newline is never inside a JSON string */
        schema_name(&body, c->name);
        buf_puts(&body, "\":{\"type\":\"object\",\"properties\":{");
        int req = 0;
        for (int k = 0; k < c->nfields; k++) {
            Field *f = &c->fields[k];
            if (k) buf_putc(&body, ',');
            schema_jstr(&body, f->name);
            buf_putc(&body, ':');
            schema_of(&body, f->optional ? ty_strip_null(f->type) : f->type, &defs);
            if (!f->optional) req++;
        }
        buf_putc(&body, '}');
        if (req) {
            buf_puts(&body, ",\"required\":[");
            int w = 0;
            for (int k = 0; k < c->nfields; k++) if (!c->fields[k].optional) { if (w++) buf_putc(&body, ','); schema_jstr(&body, c->fields[k].name); }
            buf_putc(&body, ']');
        }
        buf_putc(&body, '}');
    }
    root.data[root.len - 1] = 0; /* {"$ref":"..."} -> add $defs */
    buf_printf(&out, "%s,\"$defs\":{%s}}", root.data, body.data);
    free(root.data);
    free(body.data);
    free(defs.cls);
    return out.data;
}

/* an integer literal, possibly negated */
static bool lit_int_of(Expr *e, long long *v) {
    if (!e) return false;
    if (e->kind == E_NUMBER && e->is_int_lit) { *v = e->ival; return true; }
    if (e->kind == E_UNARY && e->op == TK_MINUS && e->lhs && e->lhs->kind == E_NUMBER && e->lhs->is_int_lit && e->lhs->ival > 0) { *v = -e->lhs->ival; return true; }
    return false;
}
/* lit ** lit and lit <op> lit for the bitwise operators, computed now (JavaScript's results) when the value is an
   exact integer: the expression becomes an integer literal, an int or a number as its use asks, like `16` */
static bool fold_lit_int(Expr *e) {
    long long a, b, r;
    if (!lit_int_of(e->lhs, &a) || !lit_int_of(e->rhs, &b)) return false;
    if (e->op == TK_STARSTAR) {
        if (b < 0 || b > 64) return false;
        double d = 1; /* (by multiplication: the compiler links without libm on Linux) */
        for (long long k = 0; k < b && fabs(d) <= 9007199254740992.0; k++) d *= (double)a;
        if (!(fabs(d) <= 9007199254740992.0)) return false; /* beyond 2^53 it stops being an exact integer */
        r = (long long)d;
    } else {
        int32_t x = (int32_t)(uint32_t)a, y = (int32_t)(uint32_t)b;
        switch (e->op) {
        case TK_AMP: r = x & y; break;
        case TK_PIPE: r = x | y; break;
        case TK_CARET: r = x ^ y; break;
        case TK_SHL: r = (int32_t)((uint32_t)x << (y & 31)); break;
        case TK_SHR: r = x >> (y & 31); break;
        case TK_USHR: r = (long long)((uint32_t)x >> (y & 31)); break;
        default: return false;
        }
    }
    e->kind = E_NUMBER;
    e->is_int_lit = true;
    e->ival = r;
    e->num = (double)r;
    e->lhs = e->rhs = NULL;
    return true;
}
/* ---- BigInt: a builtin class whose operators are runtime calls (runtime/xisto_rt.h, xs_big_*) ---- */
static bool is_big(Type *t) { return t && t->kind == TY_OBJECT && t->cls && t->cls->builtin && strcmp(t->cls->name, "bigint") == 0; }
static Type *ty_big(void) { return ty_object_of(find_class("bigint")); }
static void big_call(Expr *e, const char *cfn, Expr *a, Expr *b, Type *t) { /* e becomes cfn(a[, b]) */
    Pos pos = e->pos;
    memset(e, 0, sizeof *e);
    e->kind = E_CALL;
    e->pos = pos;
    e->field = -1;
    expr_list_push(&e->args, a);
    if (b) expr_list_push(&e->args, b);
    e->builtin.cfn = cfn;
    e->type = t;
}
static const char *big_op(int op) {
    switch (op) {
    case TK_PLUS: case TK_PLUS_ASSIGN: return "xs_big_add";
    case TK_MINUS: case TK_MINUS_ASSIGN: return "xs_big_sub";
    case TK_STAR: case TK_STAR_ASSIGN: return "xs_big_mul";
    case TK_SLASH: case TK_SLASH_ASSIGN: return "xs_big_div";
    case TK_PERCENT: return "xs_big_mod";
    default: return NULL;
    }
}
/* a + b, a < b, a === b... with a bigint on either side: both must be bigints (as in JS, which throws a
   TypeError when they are mixed) */
static bool big_binary(Expr *e) {
    if (!is_big(e->lhs->type) && !is_big(e->rhs->type)) return false;
    if (!is_big(e->lhs->type) || !is_big(e->rhs->type))
        xs_error(e->pos, "convert one side: BigInt(i) or Number(b)", "cannot mix bigint and %s", ty_name(is_big(e->lhs->type) ? e->rhs->type : e->lhs->type));
    const char *cmp = e->op == TK_LT ? "xs_big_lt" : e->op == TK_LE ? "xs_big_le" : e->op == TK_GT ? "xs_big_gt" : e->op == TK_GE ? "xs_big_ge"
                    : e->op == TK_EQ ? "xs_big_eq" : e->op == TK_NEQ ? "xs_big_ne" : NULL;
    if (cmp) { big_call(e, cmp, e->lhs, e->rhs, ty_boolean); return true; }
    const char *f = big_op(e->op);
    if (!f) xs_error(e->pos, "supported on bigint: + - * / % and comparisons", "operator %s is not supported on bigint", tok_kind_name(e->op));
    big_call(e, f, e->lhs, e->rhs, ty_big());
    return true;
}

static void check_expr(Expr *e, Type *expected) {
    switch (e->kind) {
    case E_NUMBER:
        e->type = e->is_int_lit ? ty_litint : ty_number;
        if (expected && is_numeric(expected)) coerce_lit(e, expected);
        return;
    case E_STRING:
        e->type = ty_string;
        e->lit_id = intern(e->str);
        return;
    case E_BOOL: e->type = ty_boolean; return;
    case E_THIS:
        if (!cur_class) xs_error(e->pos, NULL, "'this' outside of a class");
        e->type = ty_object_of(cur_class);
        e->cname = (char *)this_cname();
        return;
    case E_AWAIT:
        check_expr(e->lhs, expected);
        e->type = e->lhs->type;
        return;
    case E_IDENT: {
        Binding *b = lookup(e->str);
        if (!b) {
            Func *f = lookup_func(e->str);
            if (f && f->ntparams) xs_error(e->pos, xs_sprintf("wrap it: (x: Type) => %s(x)", f->name), "the generic function '%s' is not a value", f->name);
            if (f) { /* a function used as a value */
                Type **ps = xs_alloc(sizeof(Type *) * (size_t)(f->nparams ? f->nparams : 1));
                for (int i = 0; i < f->nparams; i++) ps[i] = f->params[i].type;
                e->kind = E_FUNC_REF;
                e->func = f;
                e->cname = f->cname;
                e->type = ty_func(ps, f->nparams, f->ret);
                return;
            }
            Class *cv = find_class(e->str);
            if (cv && !cv->builtin) { /* a decorated class as a value: its ClassInfo (xisto:reflect) */
                char *ci = xs_sprintf("__ci_%s", cv->name);
                for (Binding *mb = mod_scopes[cv->mod->id]->head; mb; mb = mb->next)
                    if (strcmp(mb->name, ci) == 0) { e->type = mb->type; e->cname = mb->cname; return; }
                xs_error(e->pos, "decorate the class (@Injectable(), @Controller(), ...) so the compiler describes it", "class '%s' is used as a value, but it has no ClassInfo", e->str);
            }
            if (is_builtin_name(e->str)) xs_error(e->pos, NULL, "'%s' is built in and not a value", e->str);
            if (strcmp(e->str, "NaN") == 0 || strcmp(e->str, "Infinity") == 0) { /* the global constants */
                e->kind = E_NUMBER;
                e->num = e->str[0] == 'N' ? NAN : INFINITY;
                e->type = ty_number;
                e->field = -1;
                return;
            }
            if (strcmp(e->str, "__filename") == 0) { /* the module's file, absolute (Node's CommonJS __filename) */
                char rp[4096];
                const char *f = cur_mod && cur_mod->file ? cur_mod->file : "";
#if defined(_WIN32)
                const char *abs = _fullpath(rp, f, sizeof rp) ? rp : f;
#else
                const char *abs = realpath(f, rp) ? rp : f;
#endif
                e->kind = E_STRING;
                e->str = xs_strndup(abs, strlen(abs));
                e->type = ty_string;
                check_expr(e, ty_string);
                return;
            }
            if (strcmp(e->str, "undefined") == 0 || strcmp(e->str, "null") == 0)
                xs_error(e->pos, "every value in xisto has a concrete type; there is no 'undefined' or 'null'", "'%s' does not exist", e->str);
            {
                int n; const char **cands = visible_names(&n);
                xs_error(e->pos, xs_help_suggest(e->str, cands, n, "declare it first: let name: type = value;"), "unknown variable '%s'", e->str);
            }
        }
        for (Binding *o = b; o; o = o->origin)
            if (o->poisoned) xs_error_quiet(); /* the error was its declaration's: no cascade */
        e->type = b->type;
        e->cname = b->cname;
        if (b->union_narrow && b->type->kind != TY_UNION) { /* seen as one member: read through an (unchecked) conversion */
            Expr *v = expr_copy(e);
            v->type = b->orig;
            memset(e, 0, sizeof *e);
            e->kind = E_AS;
            e->pos = v->pos;
            e->lhs = v;
            e->op = TK_DOT;
            e->type = b->type;
            e->elem_type = b->type;
            e->field = -1;
        }
        return;
    }
    case E_UNARY:
        if (e->op == TK_IDENT) { /* typeof */
            check_expr(e->lhs, NULL);
            settle(e->lhs);
            Type *t = e->lhs->type;
            if (t->kind == TY_UNKNOWN || t->kind == TY_UNION) { e->builtin.cfn = "xs_typeof"; e->type = ty_string; return; }
            Type *b = t->kind == TY_NULLABLE ? t->elem : t;
            const char *v = is_big(b) ? "bigint" : b->kind == TY_STRING || (b->kind == TY_ENUM && b->en->is_string) ? "string"
                          : is_numeric(b) || b->kind == TY_ENUM ? "number" : b->kind == TY_BOOLEAN ? "boolean" : b->kind == TY_FUNC ? "function" : "object";
            if (t->kind == TY_NULLABLE && b->kind != TY_OBJECT) { e->builtin.cfn = "xs_typeof_nullable"; e->str = (char *)v; e->type = ty_string; return; }
            e->kind = E_STRING;
            e->str = (char *)v;
            e->lit_id = intern(v);
            e->type = ty_string;
            return;
        }
        if (e->op == TK_TILDE) { /* ~x: 32-bit complement, an int */
            check_expr(e->lhs, ty_int);
            if (e->lhs->type->kind == TY_LITINT) coerce_lit(e->lhs, ty_int);
            if (!is_numeric(e->lhs->type)) xs_error(e->lhs->pos, NULL, "operand of '~' must be int or number, but this is %s", ty_name(e->lhs->type));
            e->type = ty_int;
            return;
        }
        check_expr(e->lhs, expected);
        if (e->op == TK_NOT) {
            if (e->lhs->type->kind != TY_BOOLEAN && truthy_type(e->lhs->type)) make_truthy(e->lhs, "operand of '!'");
            expect_type(e->lhs, ty_boolean, "operand of '!'");
            e->type = ty_boolean;
        } else if (is_big(e->lhs->type)) { /* -b */
            if (e->op != TK_MINUS) xs_error(e->pos, NULL, "unary '+' is not allowed on bigint (as in JS)");
            big_call(e, "xs_big_neg", e->lhs, NULL, ty_big());
        } else {
            if (!is_numeric(e->lhs->type) && e->lhs->type->kind != TY_LITINT)
                xs_error(e->lhs->pos, NULL, "operand of unary '-' must be int or number, but this is %s", ty_name(e->lhs->type));
            e->type = e->lhs->type;
        }
        return;
    case E_BINARY:
        if ((e->op == TK_STARSTAR || tk_bitop(e->op)) && fold_lit_int(e)) { /* 2 ** 53, 1 << 4: a literal, typed by its use */
            check_expr(e, expected);
            return;
        }
        if (e->op == TK_STARSTAR) { /* int ** int: int (checked); with a number: number */
            check_expr(e->lhs, NULL);
            check_expr(e->rhs, NULL);
            if (is_big(e->lhs->type) || is_big(e->rhs->type)) {
                if (!is_big(e->lhs->type) || !is_big(e->rhs->type)) xs_error(e->pos, "convert one side: BigInt(i) or Number(b)", "cannot mix bigint and another type in '**'");
                big_call(e, "xs_big_pow", e->lhs, e->rhs, ty_big());
                return;
            }
            bool ints = true;
            for (int k = 0; k < 2; k++) {
                Expr *o = k ? e->rhs : e->lhs;
                if (o->type->kind == TY_LITINT) coerce_lit(o, ty_int);
                if (!is_numeric(o->type)) xs_error(o->pos, NULL, "operand of '**' must be int or number, but this is %s", ty_name(o->type));
                if (o->type->kind != TY_INT) ints = false;
            }
            if (e->rhs->kind == E_UNARY && e->rhs->op == TK_MINUS) ints = false; /* 10 ** -2: a fraction */
            if (!ints) for (int k = 0; k < 2; k++) {
                Expr **o = k ? &e->rhs : &e->lhs;
                if ((*o)->type->kind == TY_INT) date_num_arg(o, "operand of '**'");
            }
            e->type = ints ? ty_int : ty_number;
            return;
        }
        if (tk_bitop(e->op)) { /* & | ^ << >> >>>: on 32-bit integers, as in JS; the result is an int */
            check_expr(e->lhs, ty_int);
            check_expr(e->rhs, ty_int);
            for (int k = 0; k < 2; k++) {
                Expr *o = k ? e->rhs : e->lhs;
                if (o->type->kind == TY_LITINT) coerce_lit(o, ty_int);
                if (!is_numeric(o->type)) xs_error(o->pos, NULL, "operand of %s must be int or number, but this is %s", tok_kind_name(e->op), ty_name(o->type));
            }
            e->type = ty_int;
            return;
        }
        check_expr(e->lhs, NULL);
        if (e->op == TK_AND) { /* x !== null && x.f : the right side sees x narrowed */
            if (e->lhs->type->kind != TY_BOOLEAN && truthy_type(e->lhs->type) && rereadable(e->lhs))
                make_truthy(e->lhs, "left operand of '&&'"); /* raw && raw.f: becomes raw !== null && ..., which narrows */
            NSet ts = {0};
            narrow_set(e->lhs, true, &ts);
            UNarrow un = union_narrow(e->lhs);
            if (un.b) {
                push_scope();
                shadow_union(un.b, un.then_t);
                check_expr(e->rhs, NULL);
                pop_scope();
            } else if (ts.n) {
                push_scope();
                shadow_set(&ts);
                check_expr(e->rhs, NULL);
                pop_scope();
            } else {
                check_expr(e->rhs, NULL);
            }
        } else if (e->op == TK_OR && !(e->lhs->type->kind != TY_BOOLEAN && truthy_type(e->lhs->type))) {
            /* x === null || x.f : the right side runs when the left is false, so x is not null there */
            NSet fs = {0};
            narrow_set(e->lhs, false, &fs);
            push_scope();
            shadow_set(&fs);
            check_expr(e->rhs, NULL);
            pop_scope();
        } else if (e->op == TK_IDENT) { /* instanceof: the right side is a class name, checked below */
        } else if (e->op == TK_OR && truthy_type(e->lhs->type)) { /* value || default */
            Type *want = e->lhs->type->kind == TY_NULLABLE ? e->lhs->type->elem : e->lhs->type;
            check_expr(e->rhs, want);
            expect_type(e->rhs, want, "default after '||'");
            if (!rereadable(e->lhs)) { /* f() || d: the left side is evaluated once (the emitter keeps it in a temporary) */
                Expr *dflt = e->rhs;
                e->kind = E_COND;
                e->op = 0;
                e->rhs = NULL;
                e->args.len = 0;
                expr_list_push(&e->args, dflt);
                e->builtin.cfn = "or_once";
                e->type = want;
                return;
            }
            Expr *val = expr_copy(e->lhs);
            Expr *test = expr_copy(e->lhs);
            make_truthy(test, "left operand of '||'");
            Expr *dflt = e->rhs;
            val->type = want; /* read only when the test passed */
            e->kind = E_COND;
            e->op = 0;
            e->lhs = test;
            e->rhs = NULL;
            e->args.len = 0;
            expr_list_push(&e->args, val);
            expr_list_push(&e->args, dflt);
            e->type = want;
            return;
        } else {
            check_expr(e->rhs, NULL);
        }
        if (e->op == TK_IDENT) { /* x instanceof C */
            Class *c = find_class(e->rhs->str);
            if (!c || c->is_interface) xs_error(e->rhs->pos, "instanceof tests classes (interfaces have no run-time identity)", "'%s' is not a class", e->rhs->str);
            if (!ty_is_heap(e->lhs->type)) xs_error(e->lhs->pos, NULL, "instanceof needs an object, but this is %s", ty_name(e->lhs->type));
            e->rhs->cls = c;
            e->rhs->type = ty_object_of(c);
            e->type = ty_boolean;
            return;
        }
        if (big_binary(e)) return;
        switch (e->op) {
        case TK_PLUS:
            if (e->lhs->type->kind == TY_STRING || e->rhs->type->kind == TY_STRING) {
                expect_type(e->lhs, ty_string, "left operand of string concatenation");
                expect_type(e->rhs, ty_string, "right operand of string concatenation");
                e->type = ty_string;
                return;
            }
            /* fallthrough */
        case TK_MINUS: case TK_STAR: case TK_PERCENT: {
            Type *t = unify_numeric(e, "arithmetic");
            if (t->kind == TY_LITINT && expected && is_numeric(expected)) {
                coerce_lit(e->lhs, expected);
                coerce_lit(e->rhs, expected);
                t = expected;
            }
            e->type = t;
            return;
        }
        case TK_SLASH: {
            Type *t = unify_numeric(e, "division");
            if (t->kind == TY_LITINT) { coerce_lit(e->lhs, ty_number); coerce_lit(e->rhs, ty_number); }
            e->type = ty_number;
            return;
        }
        case TK_LT: case TK_LE: case TK_GT: case TK_GE: {
            Type *t = unify_numeric(e, "comparison");
            if (t->kind == TY_LITINT) { coerce_lit(e->lhs, ty_number); coerce_lit(e->rhs, ty_number); }
            e->type = ty_boolean;
            return;
        }
        case TK_EQ: case TK_NEQ: {
            /* objects, arrays and functions compare by reference (the same object), as in JS; T | null too */
            Type *lt = e->lhs->type->kind == TY_NULLABLE ? e->lhs->type->elem : e->lhs->type;
            Type *rt = e->rhs->type->kind == TY_NULLABLE ? e->rhs->type->elem : e->rhs->type;
            bool lref = lt->kind == TY_OBJECT || lt->kind == TY_ARRAY || lt->kind == TY_FUNC || lt->kind == TY_MAP || lt->kind == TY_SET;
            bool rref = rt->kind == TY_OBJECT || rt->kind == TY_ARRAY || rt->kind == TY_FUNC || rt->kind == TY_MAP || rt->kind == TY_SET;
            if (lref && rref) {
                bool related = ty_equal(lt, rt);
                if (!related && lt->kind == TY_OBJECT && rt->kind == TY_OBJECT)
                    for (Class *k = lt->cls; k && !related; k = k->base) if (k == rt->cls) related = true;
                if (!related && lt->kind == TY_OBJECT && rt->kind == TY_OBJECT)
                    for (Class *k = rt->cls; k && !related; k = k->base) if (k == lt->cls) related = true;
                if (!related) xs_error(e->pos, NULL, "cannot compare %s with %s", ty_name(e->lhs->type), ty_name(e->rhs->type));
                e->type = ty_boolean;
                e->builtin.cfn = "ref_eq";
                return;
            }
        }
            if (e->lhs->type->kind == TY_NULL || e->rhs->type->kind == TY_NULL) {
                Expr *other = e->lhs->type->kind == TY_NULL ? e->rhs : e->lhs;
                if (other->type->kind != TY_NULLABLE && other->type->kind != TY_NULL && other->type->kind != TY_UNKNOWN)
                    xs_error(e->pos, "only values declared as T | null can be null", "%s can never be null", ty_name(other->type));
                e->type = ty_boolean;
                return;
            }
            if (e->lhs->type->kind == TY_NULLABLE || e->rhs->type->kind == TY_NULLABLE) {
                /* x === v with x: T | null and v: T: false when x is null, as in TypeScript */
                Expr *n = e->lhs->type->kind == TY_NULLABLE ? e->lhs : e->rhs, *v = n == e->lhs ? e->rhs : e->lhs;
                if (v->type->kind == TY_LITINT && is_numeric(n->type->elem)) coerce_lit(v, n->type->elem);
                if (v->type->kind != TY_NULLABLE && (ty_equal(n->type->elem, v->type) || (is_numeric(n->type->elem) && is_numeric(v->type)))) {
                    e->type = ty_boolean;
                    e->builtin.cfn = "nullable_eq";
                    return;
                }
                xs_error(e->pos, "compare against null first: if (x !== null)", "cannot compare values that may be null");
            }
            if (is_numeric(e->lhs->type) || is_numeric(e->rhs->type) || e->lhs->type->kind == TY_LITINT) {
                Type *t = unify_numeric(e, "equality");
                if (t->kind == TY_LITINT) { coerce_lit(e->lhs, ty_number); coerce_lit(e->rhs, ty_number); }
            } else if (!ty_equal(e->lhs->type, e->rhs->type)) {
                xs_error(e->pos, NULL, "cannot compare %s with %s", ty_name(e->lhs->type), ty_name(e->rhs->type));
            }
            if (e->lhs->type->kind == TY_ARRAY || e->lhs->type->kind == TY_OBJECT || e->lhs->type->kind == TY_FUNC)
                xs_error(e->pos, NULL, "%s values cannot be compared with '==='", ty_name(e->lhs->type));
            e->type = ty_boolean;
            return;
        case TK_AND: case TK_OR:
            if (e->lhs->type->kind != TY_BOOLEAN && truthy_type(e->lhs->type)) make_truthy(e->lhs, "left operand of logical operator");
            if (e->rhs->type->kind != TY_BOOLEAN && truthy_type(e->rhs->type)) make_truthy(e->rhs, "right operand of logical operator");
            expect_type(e->lhs, ty_boolean, "left operand of logical operator");
            expect_type(e->rhs, ty_boolean, "right operand of logical operator");
            e->type = ty_boolean;
            return;
        default:
            xs_error(e->pos, NULL, "internal: unexpected binary operator");
        }
        return;
    case E_ASSIGN:
    case E_POSTFIX: {
        Expr *target = e->lhs;
        const char *path_assigned = NULL; /* o.f = v: the field path, narrowed or not by what v is */
        if (e->kind == E_ASSIGN && target->kind == E_MEMBER && !target->is_call && rereadable(target->lhs) &&
            !(target->lhs->kind == E_IDENT && !lookup(target->lhs->str))) { /* obj.x = v with set x(v) (not Class.static) */
            check_expr(target->lhs, NULL);
            Type *rt = target->lhs->type;
            Class *rc = rt && rt->kind == TY_OBJECT && !rt->cls->builtin ? rt->cls : NULL;
            Func *setter = rc && !find_field(rc, target->str, NULL) ? find_method(rc, xs_sprintf("__set_%s", target->str)) : NULL;
            if (setter) {
                Expr *val = e->rhs;
                if (e->op != TK_ASSIGN) { /* obj.x += v: obj.x = obj.x + v, through the getter */
                    TokKind op = e->op == TK_PLUS_ASSIGN ? TK_PLUS : e->op == TK_MINUS_ASSIGN ? TK_MINUS : e->op == TK_STAR_ASSIGN ? TK_STAR : TK_SLASH;
                    Expr *get = xs_alloc(sizeof *get);
                    get->kind = E_MEMBER;
                    get->pos = target->pos;
                    get->lhs = target->lhs;
                    get->str = target->str;
                    get->field = -1;
                    Expr *bin = xs_alloc(sizeof *bin);
                    bin->kind = E_BINARY;
                    bin->pos = e->pos;
                    bin->op = op;
                    bin->lhs = get;
                    bin->rhs = val;
                    bin->field = -1;
                    val = bin;
                }
                Expr *recv = target->lhs;
                memset(&e->args, 0, sizeof e->args);
                expr_list_push(&e->args, val);
                e->kind = E_MEMBER;
                e->lhs = recv;
                e->rhs = NULL;
                e->op = 0;
                e->str = setter->name;
                e->is_call = true;
                e->field = -1;
                check_expr(e, NULL);
                return;
            }
        }
        Binding *ub = target->kind == E_IDENT ? lookup(target->str) : NULL;
        if (ub && ub->union_narrow) { target->type = ub->orig; target->cname = ub->cname; } /* x = ...: the union again */
        else {
            check_expr(target, NULL);
            /* o.f = v with o.f narrowed: the target is the field itself, not its checked read (inner reads, as
               the n.next of n.next.next = v, stay checked reads) */
            if (target->kind == E_AS && target->op == TK_NOT && !target->elem_type && target->lhs->kind == E_MEMBER) *target = *target->lhs;
            path_assigned = path_key(target); /* settled after the value is checked (below) */
        }
        Binding *narrowed = NULL;
        if (target->kind == E_IDENT) {
            Binding *b = lookup(target->str);
            if (strncmp(b->cname, "env->", 5) == 0)
                xs_error(e->pos, "arrow functions capture by value; keep the state in an object or array and change that", "cannot assign to '%s', captured from the enclosing function", target->str);
            if (b->is_const) xs_error(e->pos, "declare it with 'let' if it changes", "cannot assign to constant '%s'", target->str);
            if (b->is_param && ty_is_heap(b->type))
                xs_error(e->pos, "copy it into a local first: let s: string = param;", "cannot reassign parameter '%s' of reference type", target->str);
            if (b->orig) { narrowed = b; target->type = b->orig; }
        } else if (target->kind == E_INDEX) {
            if (target->builtin.cfn && strcmp(target->builtin.cfn, "xs_u8_get") != 0) xs_error(e->pos, NULL, "a match's groups are read-only");
        } else if (target->kind == E_MEMBER && target->field >= 0) {
            if (target->cls->builtin && !(strcmp(target->cls->name, "RegExp") == 0 && strcmp(target->str, "lastIndex") == 0))
                xs_error(e->pos, NULL, "the fields of %s are read-only", target->cls->name);
            Field *fd = &target->cls->fields[target->field];
            if (fd->readonly && !(cur_func && cur_func->is_ctor && cur_func->cls == fd->owner))
                xs_error(e->pos, xs_sprintf("assign it in the constructor of %s, or drop readonly", fd->owner ? fd->owner->name : target->cls->name), "cannot assign to '%s': it is readonly", fd->name);
        } else {
            xs_error(e->pos, NULL, "the left side must be a variable, a field or an array element");
        }
        if (e->kind == E_POSTFIX) {
            if (!is_numeric(target->type))
                xs_error(e->pos, NULL, "operand of '++'/'--' must be int or number, but this is %s", ty_name(target->type));
            e->type = target->type;
            return;
        }
        check_expr(e->rhs, target->type);
        if (e->op != TK_ASSIGN && is_big(target->type)) { /* acc += x: acc = acc + x (a bigint is a value: a new one) */
            const char *f = big_op(e->op);
            if (!f) xs_error(e->pos, "supported on bigint: += -= *= /=", "%s is not supported on bigint", tok_kind_name(e->op));
            if (!is_big(e->rhs->type)) xs_error(e->rhs->pos, "convert it: BigInt(i)", "cannot mix bigint and %s", ty_name(e->rhs->type));
            if (!rereadable(target)) xs_error(e->pos, "store the target in a const first", "compound assignment of a bigint needs a variable or a field");
            Expr *cur = expr_copy(target), *call = xs_alloc(sizeof *call);
            call->pos = e->pos;
            big_call(call, f, cur, e->rhs, ty_big());
            e->rhs = call;
            e->op = TK_ASSIGN;
        }
        if (e->op == TK_SLASH_ASSIGN && target->type->kind == TY_INT)
            xs_error(e->pos, "division yields number; write x = Math.trunc(x / y)", "'/=' cannot be applied to an int");
        if (e->op != TK_ASSIGN && !is_numeric(target->type) && !(e->op == TK_PLUS_ASSIGN && target->type->kind == TY_STRING))
            xs_error(e->pos, NULL, "compound assignment needs int, number or string, but this is %s", ty_name(target->type));
        if (tk_bitop_of_assign(e->op)) { /* x &= m: x must hold the int result */
            if (target->type->kind != TY_INT) xs_error(e->pos, "declare it as int: bitwise operators give 32-bit integers", "%s needs an int on the left, but this is %s", tok_kind_name(e->op), ty_name(target->type));
            if (e->rhs->type->kind == TY_LITINT) coerce_lit(e->rhs, ty_int);
            if (!is_numeric(e->rhs->type)) xs_error(e->rhs->pos, NULL, "right side of %s must be int or number", tok_kind_name(e->op));
            e->type = ty_int;
            return;
        }
        expect_type(e->rhs, target->type, e->op == TK_ASSIGN ? "assigned value" : "right side of compound assignment");
        e->type = target->type;
        if (narrowed) unnarrow(narrowed);
        if (path_assigned) { /* as TypeScript: after o.f = <not null>, o.f is not null; after o.f = <maybe null>, it may be */
            Binding *pb = lookup(path_assigned);
            bool nonnull = e->op == TK_ASSIGN && e->rhs->type->kind != TY_NULLABLE && e->rhs->kind != E_NULL;
            if (nonnull) {
                if (!pb) { pb = xs_alloc(sizeof *pb); pb->name = (char *)path_assigned; pb->type = target->type; pb->cname = ""; }
                if (pb->type->kind == TY_NULLABLE) shadow_narrowed(pb);
            } else if (pb && pb->orig) unnarrow(pb);
        }
        return;
    }
    case E_CALL: {
        if (e->builtin.cfn && strncmp(e->builtin.cfn, "__reflect_", 10) == 0) { /* generated reflection code */
            Type *t = resolve(e->elem_type, e->pos);
            e->elem_type = t;
            const char *which = e->builtin.cfn + 10;
            if (strcmp(which, "kind") == 0 || strcmp(which, "typeid") == 0 || strcmp(which, "schema") == 0) {
                const char *v;
                Type *b = t->kind == TY_NULLABLE ? t->elem : t;
                if (strcmp(which, "schema") == 0)
                    v = schema_text(t);
                else if (strcmp(which, "kind") == 0)
                    v = b->kind == TY_STRING ? "string" : b->kind == TY_INT ? "int" : b->kind == TY_NUMBER ? "number" : b->kind == TY_BOOLEAN ? "boolean"
                      : b->kind == TY_OBJECT ? (b->cls->is_interface ? "interface" : "class") : b->kind == TY_ARRAY ? "array"
                      : b->kind == TY_ENUM ? "enum" : b->kind == TY_VOID ? "void" : "other";
                else
                    v = b->kind == TY_OBJECT ? xs_sprintf("%s#%s", b->cls->mod ? xs_display_path(b->cls->mod->file) : "builtin", b->cls->name) : ty_name(b);
                e->kind = E_STRING;
                e->str = (char *)v;
                e->lit_id = intern(v);
                e->type = ty_string;
                e->builtin.cfn = NULL;
                return;
            }
            Type **ps = xs_alloc(sizeof(Type *));
            if (strcmp(which, "decoder") == 0) { ps[0] = ty_string; e->type = ty_func(ps, 1, ty_unknown); }
            else if (strcmp(which, "encoder") == 0) { ps[0] = ty_unknown; e->type = ty_func(ps, 1, ty_string); }
            else xs_error(e->pos, NULL, "unknown reflection intrinsic '%s'", e->builtin.cfn);
            return;
        }
        if (e->lhs->kind != E_IDENT) xs_error(e->pos, NULL, "only named functions can be called");
        if (strcmp(e->lhs->str, "super") == 0 && !lookup("super")) { /* super(...): the base's constructor on this object */
            Class *cc = cur_func ? cur_func->cls : NULL;
            if (!cur_func || !cur_func->is_ctor || !cc || !cc->base)
                xs_error(e->pos, NULL, "super(...) is called in the constructor of a class that extends another");
            Func *bc = eff_ctor(cc->base);
            if (bc) check_args(e, bc, xs_sprintf("the constructor of %s", cc->base->name));
            else if (e->args.len) xs_error(e->pos, NULL, "%s has no constructor, so super() takes no arguments", cc->base->name);
            e->kind = E_SUPER_CALL;
            e->cls = cc->base;
            e->func = bc;
            e->type = ty_void;
            super_calls++;
            return;
        }
        const char *name = e->lhs->str;
        if (!lookup_func(name) && !lookup(name)) {
            if (strcmp(name, "isNaN") == 0 || strcmp(name, "isFinite") == 0) { /* the globals, on numbers */
                if (e->args.len != 1) xs_error(e->pos, NULL, "%s takes exactly one argument", name);
                check_expr(e->args.items[0], ty_number);
                expect_type(e->args.items[0], ty_number, xs_sprintf("argument of %s", name));
                e->type = ty_boolean;
                e->builtin.cfn = name[2] == 'N' ? "xs_is_nan" : "xs_is_finite";
                return;
            }
            if (strcmp(name, "btoa") == 0 || strcmp(name, "atob") == 0) { /* base64 of a string of bytes (code points 0..255) */
                if (e->args.len != 1) xs_error(e->pos, NULL, "%s takes one string", name);
                check_expr(e->args.items[0], ty_string);
                expect_type(e->args.items[0], ty_string, xs_sprintf("argument of %s", name));
                e->type = ty_string;
                e->builtin.cfn = name[0] == 'b' ? "xs_btoa" : "xs_atob";
                return;
            }
            if (strcmp(name, "parseInt") == 0 || strcmp(name, "parseFloat") == 0) { /* the JS globals: number, NaN when no digits */
                bool pi = name[5] == 'I';
                if (e->args.len < 1 || e->args.len > (pi ? 2 : 1)) xs_error(e->pos, pi ? "parseInt(text) or parseInt(text, radix)" : "parseFloat(text)", "%s takes %s", name, pi ? "a string and an optional radix" : "one string");
                check_expr(e->args.items[0], ty_string);
                expect_type(e->args.items[0], ty_string, xs_sprintf("argument of %s", name));
                if (pi) {
                    if (e->args.len == 2) { check_expr(e->args.items[1], ty_int); expect_type(e->args.items[1], ty_int, "the radix"); }
                    else {
                        Expr *z = xs_alloc(sizeof *z);
                        z->kind = E_NUMBER; z->pos = e->pos; z->is_int_lit = true; z->type = ty_int; z->field = -1;
                        expr_list_push(&e->args, z);
                    }
                }
                e->type = ty_number;
                e->builtin.cfn = pi ? "xs_parse_int" : "xs_parse_float";
                return;
            }
            if (strcmp(name, "BigInt") == 0) { /* BigInt(int | number | string); 123n is BigInt(123) */
                if (e->args.len != 1) xs_error(e->pos, NULL, "BigInt() takes exactly one argument");
                Expr *a = e->args.items[0];
                check_expr(a, NULL);
                if (a->type->kind == TY_LITINT) coerce_lit(a, ty_int);
                if (a->type->kind == TY_INT) e->builtin.cfn = "xs_big_from_int";
                else if (a->type->kind == TY_NUMBER) e->builtin.cfn = "xs_big_from_num";
                else if (a->type->kind == TY_STRING) e->builtin.cfn = "xs_big_from_str";
                else if (is_big(a->type)) { *e = *a; return; }
                else xs_error(a->pos, NULL, "BigInt() converts int, number or string, but this is %s", ty_name(a->type));
                e->type = ty_big();
                return;
            }
            if (strcmp(name, "Number") == 0) {
                if (e->args.len != 1) xs_error(e->pos, NULL, "Number() takes exactly one argument");
                check_expr(e->args.items[0], NULL);
                Expr *a = e->args.items[0];
                if (a->type->kind == TY_LITINT) coerce_lit(a, ty_number);
                if (a->type->kind == TY_STRING) e->builtin.cfn = "xs_str_to_number"; /* NaN when not a number, as in JS */
                else if (a->type->kind == TY_INT) e->builtin.cfn = "xs_to_number";
                else if (a->type->kind == TY_NUMBER) e->builtin.cfn = "xs_to_number_id";
                else if (is_big(a->type)) e->builtin.cfn = "xs_big_to_number";
                else xs_error(a->pos, NULL, "Number() converts int, bigint or string to number, but this is %s", ty_name(a->type));
                e->type = ty_number;
                return;
            }
            if (strcmp(name, "String") == 0) {
                if (e->args.len != 1) xs_error(e->pos, NULL, "String() takes exactly one argument");
                check_expr(e->args.items[0], NULL);
                Expr *a = e->args.items[0];
                settle(a);
                if (a->type->kind == TY_INT) e->builtin.cfn = "xs_str_from_int";
                else if (a->type->kind == TY_NUMBER) e->builtin.cfn = "xs_str_from_num";
                else if (a->type->kind == TY_BOOLEAN) e->builtin.cfn = "xs_str_from_bool";
                else if (a->type->kind == TY_STRING) e->builtin.cfn = "xs_retain_str";
                else if (is_big(a->type)) e->builtin.cfn = "xs_big_to_string";
                else if (a->type->kind == TY_ENUM) e->builtin.cfn = a->type->en->is_string ? "xs_retain_str" : "xs_str_from_int";
                else xs_error(a->pos, NULL, "String() converts int, number or boolean, but this is %s", ty_name(a->type));
                e->type = ty_string;
                return;
            }
            if (strcmp(name, "assert") == 0) {
                if (e->args.len < 1 || e->args.len > 2) xs_error(e->pos, "assert(condition, \"message\")", "assert takes a condition and an optional message");
                check_expr(e->args.items[0], ty_boolean);
                expect_type(e->args.items[0], ty_boolean, "assert condition");
                if (e->args.len == 2) { check_expr(e->args.items[1], ty_string); expect_type(e->args.items[1], ty_string, "assert message"); }
                else {
                    Expr *m = xs_alloc(sizeof *m);
                    m->kind = E_STRING; m->pos = e->pos; m->field = -1; m->type = ty_string;
                    m->str = xs_sprintf("%s:%d", xs_file_name, e->pos.line);
                    m->lit_id = intern(m->str);
                    expr_list_push(&e->args, m);
                }
                e->type = ty_void;
                e->builtin.cfn = "xs_assert";
                return;
            }
            if (strcmp(name, "lines") == 0 || strcmp(name, "chunks") == 0) {
                bool is_lines = name[0] == 'l';
                if (e->args.len != (is_lines ? 1 : 2)) xs_error(e->pos, is_lines ? "lines(path)" : "chunks(path, bytes)", "%s takes %d argument(s)", name, is_lines ? 1 : 2);
                check_expr(e->args.items[0], ty_string);
                expect_type(e->args.items[0], ty_string, xs_sprintf("path of %s", name));
                if (!is_lines) { check_expr(e->args.items[1], ty_int); expect_type(e->args.items[1], ty_int, "chunk size"); }
                e->type = ty_stream_of(ty_string);
                e->builtin.cfn = is_lines ? "xs_stream_lines" : "xs_stream_chunks";
                return;
            }
            if (strcmp(name, "fetch") == 0) {
                if (e->args.len < 1 || e->args.len > 2) xs_error(e->pos, "fetch(url) or fetch(url, { method, headers, body, signal })", "fetch() takes a URL and optional options");
                check_expr(e->args.items[0], ty_string);
                expect_type(e->args.items[0], ty_string, "argument of fetch");
                if (e->args.len == 2) {
                    /* the options object of the Fetch API, checked by key: method, body, headers, signal */
                    Expr *init = e->args.items[1];
                    if (init->kind != E_OBJLIT) xs_error(init->pos, "write the options inline: fetch(url, { method: \"POST\", body })", "fetch options must be an object literal");
                    for (int i = 0; i < init->args.len; i++) {
                        const char *k = init->keys[i];
                        Expr *v = init->args.items[i];
                        if (strcmp(k, "method") == 0 || strcmp(k, "body") == 0) {
                            check_expr(v, ty_string);
                            expect_type(v, ty_string, xs_sprintf("fetch option '%s'", k));
                        } else if (strcmp(k, "headers") == 0) {
                            if (v->kind != E_OBJLIT) xs_error(v->pos, "headers: { \"Content-Type\": \"application/json\" }", "headers must be an object literal");
                            for (int h = 0; h < v->args.len; h++) {
                                check_expr(v->args.items[h], ty_string);
                                expect_type(v->args.items[h], ty_string, xs_sprintf("header '%s'", v->keys[h]));
                                intern(v->keys[h]);
                            }
                            v->type = ty_void;
                        } else if (strcmp(k, "signal") == 0) {
                            if (!(v->kind == E_MEMBER && v->is_call && v->lhs->kind == E_IDENT && strcmp(v->lhs->str, "AbortSignal") == 0 && strcmp(v->str, "timeout") == 0 && v->args.len == 1))
                                xs_error(v->pos, "signal: AbortSignal.timeout(5000)", "the only supported signal is AbortSignal.timeout(ms)");
                            check_expr(v->args.items[0], ty_int);
                            expect_type(v->args.items[0], ty_int, "timeout in milliseconds");
                            v->type = ty_int;
                        } else {
                            static const char *keys[] = {"method", "body", "headers", "signal"};
                            xs_error(v->pos, xs_help_suggest(k, keys, 4, "supported: method, body, headers, signal"), "unsupported fetch option '%s'", k);
                        }
                    }
                    init->type = ty_void;
                }
                e->type = ty_object_of(find_class("Response"));
                e->builtin.cfn = "xs_fetch";
                prog->uses_net = true;
                /* TLS is linked only when https can happen: an http:// URL (TLS ends at the load balancer) costs nothing */
                int scheme = url_scheme(e->args.items[0]);
                if (scheme == 1 && xs_tls_off) xs_error(e->args.items[0]->pos, "remove --tls=off, or call the service over http://", "https URL, but the program is built with --tls=off");
                if (scheme == 1 || (scheme == -1 && !xs_tls_off)) prog->uses_tls = true;
                return;
            }
        }
        Func *f = lookup_func(name);
        if (!f) {
            Binding *b = lookup(name);
            if (b && b->type->kind == TY_FUNC) { /* calling a function value */
                Type *ft = b->type;
                e->lhs->type = ft;
                e->lhs->cname = b->cname;
                if (e->args.len != ft->nparams) xs_error(e->pos, NULL, "'%s' takes %d argument(s), got %d", name, ft->nparams, e->args.len);
                for (int i = 0; i < e->args.len; i++) {
                    check_expr(e->args.items[i], ft->params[i]);
                    expect_type(e->args.items[i], ft->params[i], xs_sprintf("argument %d of '%s'", i + 1, name));
                }
                e->type = ft->ret;
                return;
            }
            if (b) xs_error(e->pos, NULL, "'%s' is a variable, not a function", name);
            if (find_class(name)) xs_error(e->pos, xs_sprintf("write new %s(...)", name), "'%s' is a class; construct it with 'new'", name);
            {
                int n; const char **cands = visible_names(&n);
                xs_error(e->pos, xs_help_suggest(name, cands, n, "declare it at the top level: function name(...): type { ... }"), "unknown function '%s'", name);
            }
        }
        if (f->mod && f->mod->builtin && strcmp(f->name, "execSync") == 0 && e->args.len == 2) {
            /* execSync(cmd, { encoding: "utf8" }): xisto always returns the output as a string */
            Expr *o = e->args.items[1];
            if (o->kind != E_OBJLIT || o->args.len != 1 || strcmp(o->keys[0], "encoding") != 0 || o->args.items[0]->kind != E_STRING)
                xs_error(o->pos, "write execSync(command, { encoding: \"utf8\" })", "unsupported execSync options");
            e->args.len = 1;
        }
        if (f->mod && f->mod->builtin && strcmp(f->name, "readFileSync") == 0) {
            if (e->args.len < 1 || e->args.len > 2) xs_error(e->pos, NULL, "readFileSync takes a path and optionally \"utf8\"");
            Expr *a0 = e->args.items[0];
            if (a0->kind == E_NUMBER && a0->is_int_lit && a0->ival == 0) { /* readFileSync(0): standard input, as in Node */
                if (e->args.len == 2) {
                    Expr *enc = e->args.items[1];
                    if (enc->kind != E_STRING || (strcmp(enc->str, "utf8") != 0 && strcmp(enc->str, "utf-8") != 0))
                        xs_error(enc->pos, "text is read as UTF-8: readFileSync(0, \"utf8\")", "unsupported encoding");
                }
                e->func = NULL;
                e->builtin.cfn = e->args.len == 2 ? "xs_fs_read_stdin" : "xs_fs_read_stdin_bytes";
                e->type = e->args.len == 2 ? ty_string : ty_object_of(find_class("Buffer"));
                e->args.len = 0;
                return;
            }
            check_expr(e->args.items[0], ty_string);
            expect_type(e->args.items[0], ty_string, "path of readFileSync");
            if (e->args.len == 1) { /* no encoding: the bytes, a Buffer (as in Node) */
                e->func = NULL;
                e->builtin.cfn = "xs_fs_read_bytes";
                e->type = ty_object_of(find_class("Buffer"));
                return;
            }
            if (e->args.len == 2) {
                Expr *enc = e->args.items[1];
                if (enc->kind != E_STRING || (strcmp(enc->str, "utf8") != 0 && strcmp(enc->str, "utf-8") != 0))
                    xs_error(enc->pos, "text is read as UTF-8: readFileSync(path, \"utf8\"); for other encodings, readFileSync(path).toString(\"base64\")", "unsupported encoding");
                e->args.len = 1;
            }
            e->func = f;
            e->type = ty_string;
            return;
        }
        if (f->ntparams) { /* a generic function: its instance for these types */
            Func *inst = check_generic_call(e, f, expected, xs_sprintf("'%s'", f->name));
            e->func = inst;
            e->type = inst->ret;
            return;
        }
        if (e->ntargs) xs_error(e->pos, NULL, "'%s' is not generic", f->name);
        if (f->mod && f->mod->builtin && f->cname && strcmp(f->cname, "xs_fs_write") == 0 && e->args.len == 2) { /* writeFileSync(path, bytes) */
            check_expr(e->args.items[1], NULL);
            if (is_bytes(e->args.items[1]->type)) {
                check_expr(e->args.items[0], ty_string);
                expect_type(e->args.items[0], ty_string, "the path");
                e->func = NULL;
                e->builtin.cfn = "xs_fs_write_bytes";
                e->type = ty_void;
                return;
            }
        }
        check_args(e, f, xs_sprintf("'%s'", f->name));
        e->func = f;
        e->type = f->ret;
        if (f->mod && f->mod->builtin && strcmp(f->cname, "xs_sleep_ms") == 0) prog->uses_net = true; /* timers live with the event loop */
        return;
    }
    case E_INDEX:
        check_expr(e->lhs, NULL);
        if (e->lhs->type->kind == TY_NULLABLE) xs_error(e->pos, "check it first: if (x !== null) { ... }", "indexed value may be null");
        check_expr(e->rhs, ty_int);
        if (e->lhs->type->kind == TY_OBJECT && e->lhs->type->cls->is_tuple) { /* t[0]: its field (the index is a constant) */
            Class *tc = e->lhs->type->cls;
            long long k;
            if (!lit_int_of(e->rhs, &k)) xs_error(e->rhs->pos, "a tuple's elements have different types: index it with a number written in place, t[0]", "the index of a tuple must be a constant");
            if (k < 0 || k >= tc->nfields) xs_error(e->rhs->pos, NULL, "index %lld is out of the tuple %s (length %d)", k, tc->name, tc->nfields);
            e->kind = E_MEMBER;
            e->str = tc->fields[k].name;
            e->rhs = NULL;
            e->is_call = false;
            e->field = (int)k;
            e->cls = tc;
            e->type = tc->fields[k].type;
            return;
        }
        if (e->lhs->type->kind == TY_STRING)
            xs_error(e->pos, "use s.slice(i, i + 1)", "strings cannot be indexed in xisto-2");
        if (is_bytes(e->lhs->type)) { /* b[i]: a byte, as an int (bounds checked) */
            expect_type(e->rhs, ty_int, "byte index");
            e->builtin.cfn = "xs_u8_get";
            e->type = ty_int;
            return;
        }
        if (is_f64(e->lhs->type)) { /* f[i]: a number, read and written in place (bounds checked) */
            if (e->rhs->type->kind == TY_NUMBER)
                xs_error(e->rhs->pos, "declare the index as int, or convert it with Math.trunc(x)", "Float64Array index must be int, but this is number");
            expect_type(e->rhs, ty_int, "Float64Array index");
            e->type = ty_number;
            return;
        }
        if (e->lhs->type->kind == TY_OBJECT && e->lhs->type->cls->builtin && strcmp(e->lhs->type->cls->name, "RegExpExecArray") == 0) {
            expect_type(e->rhs, ty_int, "match index");
            e->builtin.cfn = "xs_RegExpExecArray_at"; /* m[i]: the text of group i (read only) */
            e->type = ty_string;
            return;
        }
        if (e->lhs->type->kind != TY_ARRAY) xs_error(e->pos, NULL, "cannot index a value of type %s", ty_name(e->lhs->type));
        if (e->rhs->type->kind == TY_NUMBER)
            xs_error(e->rhs->pos, "declare the index as int, or convert it with Math.trunc(x)", "array index must be int, but this is number");
        expect_type(e->rhs, ty_int, "array index");
        e->type = e->lhs->type->elem;
        return;
    case E_MEMBER:
        if (e->is_call && e->str && strcmp(e->str, "write") == 0 && e->lhs->kind == E_MEMBER && !e->lhs->is_call && e->lhs->str &&
            (strcmp(e->lhs->str, "stdout") == 0 || strcmp(e->lhs->str, "stderr") == 0) && e->lhs->lhs->kind == E_IDENT &&
            strcmp(e->lhs->lhs->str, "process") == 0 && !lookup("process")) { /* process.stdout.write(text or bytes) */
            bool err = strcmp(e->lhs->str, "stderr") == 0;
            if (e->args.len != 1) xs_error(e->pos, NULL, "process.%s.write takes one argument (a string or a Uint8Array)", e->lhs->str);
            check_expr(e->args.items[0], NULL);
            Type *t = e->args.items[0]->type;
            if (t->kind != TY_STRING && !is_bytes(t)) xs_error(e->args.items[0]->pos, NULL, "process.%s.write writes a string or a Uint8Array, not %s", e->lhs->str, ty_name(t));
            e->kind = E_CALL;
            e->lhs = NULL;
            e->is_call = false;
            e->builtin.cfn = t->kind == TY_STRING ? (err ? "xs_stderr_write_str" : "xs_stdout_write_str") : (err ? "xs_stderr_write_bytes" : "xs_stdout_write_bytes");
            e->type = ty_boolean;
            return;
        }
        if (e->lhs->kind == E_IDENT && strcmp(e->lhs->str, "super") == 0 && !lookup("super")) { /* super.m(...) */
            Class *cc = cur_func ? cur_func->cls : NULL;
            if (!cc || !cc->base) xs_error(e->pos, NULL, "super is used inside a class that extends another");
            if (!e->is_call || !find_method(cc->base, e->str))
                xs_error(e->pos, xs_sprintf("read fields with this.%s", e->str), "%s has no method '%s'", cc->base->name, e->str);
            Expr *self = xs_alloc(sizeof *self);
            self->kind = E_THIS;
            self->pos = e->lhs->pos;
            self->field = -1;
            check_expr(self, NULL);
            e->lhs = self;
            upcast(e->lhs, ty_object_of(cc->base));
            e->super_call = true;
            check_object_member(e, cc->base);
            return;
        }
        if (e->lhs->kind == E_IDENT && !lookup(e->lhs->str)) {
            const char *ns = e->lhs->str;
            Class *sc = find_class(ns);
            for (int k = 0; sc && !sc->builtin && k < sc->nstatics_user; k++) { /* Class.staticMember */
                if (strcmp(sc->static_names[k], e->str) != 0) continue;
                if (sc->static_is_method[k]) {
                    if (!e->is_call) xs_error(e->pos, "call it: Class.method(...)", "static methods are not values");
                    Expr *callee = xs_alloc(sizeof *callee);
                    callee->kind = E_IDENT;
                    callee->pos = e->pos;
                    callee->str = sc->static_gen[k];
                    callee->field = -1;
                    e->kind = E_CALL;
                    e->lhs = callee;
                    e->is_call = false;
                    check_expr(e, expected);
                    return;
                }
                if (e->is_call) xs_error(e->pos, NULL, "'%s' is a static field of %s, not a method", e->str, sc->name);
                e->kind = E_IDENT;
                e->str = sc->static_gen[k];
                e->lhs = NULL;
                check_expr(e, expected);
                return;
            }
            if (strcmp(ns, "Math") == 0) {
                check_math(e);
                return;
            }
            if (strcmp(ns, "Uint8Array") == 0 || strcmp(ns, "Buffer") == 0) {
                check_bytes_static(e, ns);
                return;
            }
            if (strcmp(ns, "Promise") == 0) {
                check_promise(e, expected);
                return;
            }
            if (strcmp(ns, "performance") == 0) {
                if (!e->is_call || strcmp(e->str, "now") != 0 || e->args.len != 0) xs_error(e->pos, "supported: performance.now()", "unknown performance member '%s'", e->str);
                e->type = ty_number;
                e->builtin.cfn = "xs_performance_now";
                return;
            }
            if (strcmp(ns, "Date") == 0) {
                if (e->is_call && strcmp(e->str, "parse") == 0) {
                    if (e->args.len != 1) xs_error(e->pos, NULL, "Date.parse takes one string");
                    check_expr(e->args.items[0], ty_string);
                    expect_type(e->args.items[0], ty_string, "argument of Date.parse");
                    e->type = ty_number;
                    e->builtin.cfn = "xs_Date_parse";
                    return;
                }
                if (e->is_call && strcmp(e->str, "UTC") == 0) {
                    int n = e->args.len;
                    if (n < 1 || n > 7) xs_error(e->pos, NULL, "Date.UTC takes 1 to 7 arguments");
                    for (int k = 0; k < n; k++) date_num_arg(&e->args.items[k], xs_sprintf("argument %d of Date.UTC", k + 1));
                    for (int k = n; k < 7; k++) expr_list_push(&e->args, num_lit(e->pos, k == 2 ? 1 : 0, false));
                    e->type = ty_number;
                    e->builtin.cfn = "xs_Date_UTC";
                    return;
                }
                if (!e->is_call || strcmp(e->str, "now") != 0 || e->args.len != 0) xs_error(e->pos, "supported: Date.now(), Date.parse(text), Date.UTC(y, m, ...)", "unknown Date member '%s'", e->str);
                e->type = ty_number;
                e->builtin.cfn = "xs_date_now";
                return;
            }
            if (strcmp(ns, "process") == 0) {
                if (!e->is_call && strcmp(e->str, "argv") == 0) { e->type = ty_array_of(ty_string); e->builtin.cfn = "xs_process_argv"; return; }
                if (!e->is_call && strcmp(e->str, "env") == 0) { e->type = ty_map_of(ty_string, ty_string); e->builtin.cfn = "xs_process_env"; return; }
                if (e->is_call && strcmp(e->str, "exit") == 0) {
                    if (e->args.len > 1) xs_error(e->pos, NULL, "process.exit takes at most one argument");
                    if (e->args.len == 1) { check_expr(e->args.items[0], ty_int); expect_type(e->args.items[0], ty_int, "exit code"); }
                    e->type = ty_void;
                    e->builtin.cfn = "xs_process_exit";
                    return;
                }
                xs_error(e->pos, "supported: process.argv, process.env.get(name), process.exit(code), process.stdout.write(x), process.stderr.write(x)", "unknown process member '%s'", e->str);
            }
            if (strcmp(ns, "Array") == 0 && e->is_call && (strcmp(e->str, "from") == 0 || strcmp(e->str, "isArray") == 0)) {
                if (e->args.len != 1) xs_error(e->pos, "Array.from(array, Set or Map); Array.isArray(x)", "Array.%s takes one argument here", e->str);
                Expr *src = e->args.items[0];
                check_expr(src, expected && strcmp(e->str, "from") == 0 ? expected : NULL);
                Type *st = src->type;
                if (e->str[0] == 'i') { /* the type says it */
                    bool arr = st->kind == TY_ARRAY;
                    e->kind = E_BOOL; e->boolean = arr; e->type = ty_boolean; e->lhs = NULL; e->args.len = 0; e->field = -1;
                    return;
                }
                /* a copy of an array; a Set's values or a Map's entries (already arrays of their own) */
                const char *m = st->kind == TY_ARRAY ? "slice" : st->kind == TY_SET ? "values" : st->kind == TY_MAP ? "entries" : NULL;
                if (!m) xs_error(src->pos, "Array.from takes an array, a Set or a Map", "cannot make an array from %s", ty_name(st));
                e->lhs = src;
                e->str = (char *)m;
                e->args.len = 0;
                e->field = -1;
                check_expr(e, expected);
                return;
            }
            if (strcmp(ns, "Number") == 0) {
                static const char *help = "supported: Number.isNaN, isFinite, isInteger, isSafeInteger, parseFloat, parseInt, and the constants "
                                          "MAX_SAFE_INTEGER, MIN_SAFE_INTEGER, EPSILON, MAX_VALUE, MIN_VALUE, POSITIVE_INFINITY, NEGATIVE_INFINITY, NaN";
                if (!e->is_call) { /* a constant: becomes the number itself */
                    static const struct { const char *n; double v; } k[] = {
                        {"MAX_SAFE_INTEGER", 9007199254740991.0}, {"MIN_SAFE_INTEGER", -9007199254740991.0}, {"EPSILON", 2.220446049250313e-16},
                        {"MAX_VALUE", 1.7976931348623157e308}, {"MIN_VALUE", 5e-324}, {"POSITIVE_INFINITY", INFINITY},
                        {"NEGATIVE_INFINITY", -INFINITY}, {"NaN", NAN},
                    };
                    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
                        if (strcmp(e->str, k[i].n) == 0) { e->kind = E_NUMBER; e->num = k[i].v; e->lhs = NULL; e->type = ty_number; e->field = -1; return; }
                    xs_error(e->pos, help, "unknown Number member '%s'", e->str);
                }
                if (strcmp(e->str, "parseFloat") == 0 || strcmp(e->str, "parseInt") == 0) { /* the globals of the same name */
                    Expr *g = xs_alloc(sizeof *g);
                    *g = *e->lhs;
                    g->str = e->str;
                    e->kind = E_CALL;
                    e->lhs = g;
                    e->is_call = false;
                    check_expr(e, expected);
                    return;
                }
                const char *fn = strcmp(e->str, "isNaN") == 0 ? "xs_is_nan" : strcmp(e->str, "isInteger") == 0 ? "xs_is_integer"
                               : strcmp(e->str, "isFinite") == 0 ? "xs_is_finite" : strcmp(e->str, "isSafeInteger") == 0 ? "xs_is_safe_integer" : NULL;
                if (!fn) xs_error(e->pos, help, "unknown Number member '%s'", e->str);
                if (e->args.len != 1) xs_error(e->pos, NULL, "Number.%s takes exactly one argument", e->str);
                check_expr(e->args.items[0], ty_number);
                /* unlike the global isNaN/isFinite, Number.isX(v) of an int is fine (no conversion happens) */
                if (e->args.items[0]->type->kind == TY_LITINT) coerce_lit(e->args.items[0], ty_number);
                expect_type(e->args.items[0], ty_number, xs_sprintf("argument of Number.%s", e->str));
                e->type = ty_boolean;
                e->builtin.cfn = fn;
                return;
            }
            if (strcmp(ns, "String") == 0 && e->is_call && (strcmp(e->str, "fromCharCode") == 0 || strcmp(e->str, "fromCodePoint") == 0)) {
                for (int k = 0; k < e->args.len; k++) { /* ints or numbers: each a code point */
                    check_expr(e->args.items[k], ty_number);
                    if (e->args.items[k]->type->kind == TY_LITINT) coerce_lit(e->args.items[k], ty_number);
                    if (!is_numeric(e->args.items[k]->type)) xs_error(e->args.items[k]->pos, NULL, "argument of String.%s must be a number, but this is %s", e->str, ty_name(e->args.items[k]->type));
                }
                e->type = ty_string;
                e->builtin.cfn = e->str[4] == 'C' && e->str[5] == 'h' ? "xs_str_from_char_codes" : "xs_str_from_code_points";
                return;
            }
            if (strcmp(ns, "JSON") == 0) {
                if (!e->is_call || (strcmp(e->str, "stringify") != 0 && strcmp(e->str, "parse") != 0))
                    xs_error(e->pos, "supported: JSON.stringify(value), JSON.parse(text) assigned to a typed variable", "unknown JSON member '%s'", e->str);
                if (e->args.len != 1) xs_error(e->pos, NULL, "JSON.%s takes exactly one argument", e->str);
                if (strcmp(e->str, "stringify") == 0) {
                    check_expr(e->args.items[0], NULL);
                    settle(e->args.items[0]);
                    Type *t = e->args.items[0]->type;
                    if (t->kind == TY_JSON) { Expr *v = e->args.items[0]; *e = *v; e->type = ty_string; return; } /* already JSON */
                    if (!json_ok(t, 0)) xs_error(e->args.items[0]->pos, "JSON covers number, int, boolean, string, arrays and classes made of those", "cannot serialize %s to JSON", ty_name(t));
                    e->type = ty_string;
                    e->builtin.cfn = "json_stringify"; /* resolved by the emitter per type */
                    return;
                }
                check_expr(e->args.items[0], ty_string);
                if (e->args.items[0]->type->kind == TY_JSON) e->args.items[0]->type = ty_string; /* JSON text already */
                expect_type(e->args.items[0], ty_string, "argument of JSON.parse");
                if (!expected) xs_error(e->pos, "write the type: const t: Todo = JSON.parse(text);", "JSON.parse needs to know the type it produces");
                if (!json_ok(expected, 0)) xs_error(e->pos, "JSON covers number, int, boolean, string, arrays and classes made of those", "cannot parse JSON into %s", ty_name(expected));
                e->type = expected;
                e->builtin.cfn = "json_parse";
                return;
            }
            if (strcmp(ns, "console") == 0) {
                if (!e->is_call || strcmp(e->str, "log") != 0)
                    xs_error(e->pos, "supported: console.log(...)", "unknown console member '%s'", e->str);
                for (int i = 0; i < e->args.len; i++) {
                    check_expr(e->args.items[i], NULL);
                    settle(e->args.items[i]);
                    Type *t = e->args.items[i]->type;
                    if (t->kind == TY_VOID || t->kind == TY_FUNC)
                        xs_error(e->args.items[i]->pos, NULL, "cannot print a %s value", ty_name(t));
                }
                e->type = ty_void;
                return;
            }
            Enum *en = find_enum(ns);
            if (en) {
                if (e->is_call) xs_error(e->pos, NULL, "enum members are values, not functions");
                int idx = -1;
                for (int i = 0; i < en->nmembers; i++) if (strcmp(en->members[i].name, e->str) == 0) idx = i;
                if (idx < 0) {
                    const char **names = xs_alloc(sizeof(char *) * (size_t)en->nmembers);
                    for (int i = 0; i < en->nmembers; i++) names[i] = en->members[i].name;
                    xs_error(e->pos, xs_help_suggest(e->str, names, en->nmembers, NULL), "enum %s has no member '%s'", en->name, e->str);
                }
                e->kind = E_ENUM_VAL;
                e->en = en;
                e->field = idx;
                e->type = ty_enum_of(en);
                if (en->is_string) en->members[idx].lit_id = intern(en->members[idx].sval);
                return;
            }
            Class *c = find_class(ns);
            if (c) { /* static method of a built-in class */
                Func *m = NULL;
                for (int i = 0; i < c->nstatics; i++)
                    if (strcmp(c->statics[i]->name, e->str) == 0) m = c->statics[i];
                if (!m || !e->is_call) xs_error(e->pos, NULL, "%s has no static member '%s'", c->name, e->str);
                check_args(e, m, xs_sprintf("%s.%s", c->name, m->name));
                e->func = m;
                e->cls = c;
                e->type = m->ret;
                e->builtin.cfn = m->cname;
                prog->uses_net = true;
                return;
            }
        }
        if (e->lhs->kind == E_NEW_ARRAY) {
            Expr *na = e->lhs;
            if (!e->is_call || strcmp(e->str, "fill") != 0)
                xs_error(e->pos, "write: new Array<T>(n).fill(x)", "a new array must be filled before use");
            na->elem_type = resolve(na->elem_type, na->pos);
            check_type_supported(na->pos, ty_array_of(na->elem_type));
            check_expr(na->lhs, ty_int);
            if (na->lhs->type->kind == TY_NUMBER)
                xs_error(na->lhs->pos, "declare it as int, or convert with Math.trunc(x)", "array length must be int, but this is number");
            expect_type(na->lhs, ty_int, "array length");
            if (e->args.len != 1) xs_error(e->pos, NULL, "fill takes exactly one argument");
            check_expr(e->args.items[0], na->elem_type);
            expect_type(e->args.items[0], na->elem_type, "fill value");
            na->type = e->type = ty_array_of(na->elem_type);
            return;
        }
        check_expr(e->lhs, NULL);
        if (e->lhs->type->kind == TY_NULLABLE)
            xs_error(e->pos, "check it first: if (x !== null) { ... }  (fields need a local copy: const v = this.f;)", "'%s' may be null", e->str);
        if (e->lhs->type->kind == TY_MAP || e->lhs->type->kind == TY_SET) {
            check_map_member(e);
            return;
        }
        if (e->lhs->type->kind == TY_STREAM) {
            check_stream_member(e);
            return;
        }
        if (e->lhs->type->kind == TY_CHAN) {
            Type *el = e->lhs->type->elem;
            const char *m = e->str;
            if (!e->is_call && strcmp(m, "size") == 0) { e->type = ty_int; e->builtin.cfn = m; return; }
            if (e->is_call && strcmp(m, "send") == 0 && e->args.len == 1) {
                check_expr(e->args.items[0], el);
                expect_type(e->args.items[0], el, "value sent on the channel");
                e->type = ty_void; e->builtin.cfn = m; return;
            }
            if (e->is_call && (strcmp(m, "receive") == 0 || strcmp(m, "close") == 0) && e->args.len == 0) {
                e->type = m[0] == 'r' ? el : ty_void; e->builtin.cfn = m; return;
            }
            static const char *cm[] = {"send", "receive", "close", "size"};
            xs_error(e->pos, xs_help_suggest(m, cm, 4, "Channel has send(value), receive(), close() and size"), "Channel has no member '%s'", m);
        }
        if (e->lhs->type->kind == TY_UNION) { /* s.kind: a field every member has, read by the object's class */
            Type *u = e->lhs->type;
            if (e->is_call) xs_error(e->pos, "narrow it first: if (x instanceof C) / if (typeof x === \"string\") / if (x.kind === \"...\")", "cannot call a method on %s", ty_name(u));
            Type *ft = NULL;
            bool all_lit = true;
            for (int i = 0; i < u->nparams; i++) {
                Type *m = u->params[i];
                Field *fd = m->kind == TY_OBJECT && !m->cls->builtin ? find_field(m->cls, e->str, NULL) : NULL;
                if (!fd) xs_error(e->pos, "narrow it first to the member that has it", "'%s' is not a field of every member of %s (%s has none)", e->str, ty_name(u), ty_name(m));
                if (!(fd->type->kind == TY_STRING && fd->type->tname)) all_lit = false;
                if (!ft) ft = fd->type;
                else if (!ty_equal(ft, fd->type)) xs_error(e->pos, NULL, "'%s' has different types in the members of %s", e->str, ty_name(u));
            }
            e->type = all_lit ? ty_string : ft;
            e->elem_type = u;
            e->builtin.cfn = "union_field";
            e->field = -1;
            return;
        }
        switch (e->lhs->type->kind) {
        case TY_ARRAY: {
            Type *elem = e->lhs->type->elem;
            if (!e->is_call && strcmp(e->str, "length") == 0) {
                e->type = ty_int;
                return;
            }
            if (e->is_call && strcmp(e->str, "push") == 0) {
                if (e->args.len != 1) xs_error(e->pos, NULL, "push takes exactly one argument in xisto");
                check_expr(e->args.items[0], elem);
                expect_type(e->args.items[0], elem, "argument of push");
                e->type = ty_int;
                e->builtin.cfn = "push";
                return;
            }
            if (e->is_call && strcmp(e->str, "pop") == 0) {
                if (e->args.len != 0) xs_error(e->pos, NULL, "pop takes no arguments");
                e->type = elem;
                e->builtin.cfn = "pop";
                return;
            }
            if (e->is_call && strcmp(e->str, "unshift") == 0) {
                if (e->args.len != 1) xs_error(e->pos, NULL, "unshift takes exactly one argument in xisto");
                check_expr(e->args.items[0], elem);
                expect_type(e->args.items[0], elem, "argument of unshift");
                e->type = ty_int;
                e->builtin.cfn = "unshift";
                return;
            }
            if (e->is_call && strcmp(e->str, "shift") == 0) {
                if (e->args.len != 0) xs_error(e->pos, NULL, "shift takes no arguments");
                e->type = elem;
                e->builtin.cfn = "shift";
                return;
            }
            if (e->is_call && strcmp(e->str, "splice") == 0) {
                if (e->args.len != 2) xs_error(e->pos, "xisto supports splice(index, 1) to remove one element", "splice takes two arguments: splice(index, 1)");
                check_expr(e->args.items[0], ty_int);
                expect_type(e->args.items[0], ty_int, "splice index");
                check_expr(e->args.items[1], ty_int);
                if (e->args.items[1]->kind != E_NUMBER || e->args.items[1]->ival != 1)
                    xs_error(e->args.items[1]->pos, "xisto supports splice(index, 1) only", "splice count must be the literal 1");
                e->type = ty_void;
                e->builtin.cfn = "splice1";
                return;
            }
            if (e->is_call) {
                const char *m = e->str;
                #define CB(nparams_, ret_) ({ Type **ps_ = xs_alloc(sizeof(Type *) * 2); ps_[0] = elem; ps_[1] = ty_int; ty_func(ps_, nparams_, ret_); })
                if (strcmp(m, "forEach") == 0 || strcmp(m, "filter") == 0 || strcmp(m, "find") == 0 || strcmp(m, "findIndex") == 0 ||
                    strcmp(m, "some") == 0 || strcmp(m, "every") == 0 || strcmp(m, "map") == 0) {
                    if (e->args.len != 1) xs_error(e->pos, NULL, "%s takes one function", m);
                    Expr *cb = e->args.items[0];
                    bool is_map = strcmp(m, "map") == 0, is_each = strcmp(m, "forEach") == 0;
                    /* the callback takes (element) or (element, index); map infers its result type from the callback */
                    int np = 1;
                    if (cb->kind == E_ARROW) np = cb->arrow->fn->nparams;
                    if (np < 1 || np > 2) xs_error(cb->pos, NULL, "the callback of %s takes (element) or (element, index)", m);
                    Type *cbt = CB(np, is_map ? NULL : is_each ? ty_void : ty_boolean);
                    if (is_map) {
                        if (cb->kind == E_ARROW && !cb->arrow->fn->ret) { /* infer the element type of the result */
                            cbt->ret = NULL;
                            check_expr(cb, cbt);
                        } else {
                            check_expr(cb, NULL);
                            /* the parameters must still match the elements: the runtime calls it with them */
                            if (cb->type->kind == TY_FUNC) expect_type(cb, CB(np, cb->type->ret), xs_sprintf("callback of %s", m));
                        }
                    } else {
                        check_expr(cb, cbt);
                        expect_type(cb, cbt, xs_sprintf("callback of %s", m));
                    }
                    if (cb->type->kind != TY_FUNC) xs_error(cb->pos, NULL, "%s needs a function", m);
                    if (is_map) {
                        Type *rt = cb->type->ret;
                        if (rt->kind == TY_VOID || rt->kind == TY_FUNC || rt->kind == TY_NULLABLE)
                            xs_error(cb->pos, NULL, "map cannot produce arrays of %s", ty_name(rt));
                        e->type = ty_array_of(rt);
                        check_type_supported(e->pos, e->type);
                    } else if (strcmp(m, "filter") == 0) {
                        e->type = e->lhs->type;
                    } else if (strcmp(m, "find") == 0) {
                        if (!ty_is_heap(elem)) xs_error(e->pos, "for arrays of numbers use findIndex", "find needs an array of objects or strings (it returns T | null)");
                        e->type = ty_nullable_of(elem);
                    } else if (strcmp(m, "findIndex") == 0) {
                        e->type = ty_int;
                    } else if (is_each) {
                        e->type = ty_void;
                    } else {
                        e->type = ty_boolean;
                    }
                    e->builtin.cfn = m;
                    e->field = np; /* arity of the callback */
                    return;
                }
                #undef CB
                if (strcmp(m, "indexOf") == 0 || strcmp(m, "includes") == 0) {
                    if (e->args.len != 1) xs_error(e->pos, NULL, "%s takes one argument", m);
                    if (elem->kind == TY_OBJECT || elem->kind == TY_ARRAY || elem->kind == TY_MAP || elem->kind == TY_SET)
                        xs_error(e->pos, "use findIndex with a comparison", "%s compares by value and works on arrays of numbers, strings, booleans and enums", m);
                    check_expr(e->args.items[0], elem);
                    expect_type(e->args.items[0], elem, xs_sprintf("argument of %s", m));
                    e->type = strcmp(m, "indexOf") == 0 ? ty_int : ty_boolean;
                    e->builtin.cfn = m;
                    return;
                }
                if (strcmp(m, "join") == 0) {
                    if (e->args.len > 1) xs_error(e->pos, NULL, "join takes at most one argument");
                    if (e->args.len == 1) { check_expr(e->args.items[0], ty_string); expect_type(e->args.items[0], ty_string, "separator of join"); }
                    if (elem->kind == TY_OBJECT || elem->kind == TY_ARRAY || elem->kind == TY_MAP || elem->kind == TY_SET)
                        xs_error(e->pos, "map the elements to strings first", "join works on arrays of numbers, strings, booleans and enums");
                    e->type = ty_string;
                    e->builtin.cfn = m;
                    return;
                }
                if (strcmp(m, "sort") == 0) {
                    if (e->args.len != 1) xs_error(e->pos, "JavaScript's default sort compares as strings; xisto requires a comparator: sort((a, b) => a - b)", "sort takes a comparator function");
                    Type **ps = xs_alloc(sizeof(Type *) * 2); ps[0] = elem; ps[1] = elem;
                    Type *cbt = ty_func(ps, 2, NULL);
                    Expr *cb = e->args.items[0];
                    if (cb->kind == E_ARROW && !cb->arrow->fn->ret) check_expr(cb, cbt); else check_expr(cb, NULL);
                    if (cb->type->kind != TY_FUNC || cb->type->nparams != 2 || !ty_equal(cb->type->params[0], elem) || !is_numeric(cb->type->ret))
                        xs_error(cb->pos, NULL, "the comparator must be (a: %s, b: %s) => number", ty_name(elem), ty_name(elem));
                    e->type = e->lhs->type;
                    e->builtin.cfn = m;
                    e->field = cb->type->ret->kind == TY_INT ? 1 : 0;
                    return;
                }
                if (strcmp(m, "reverse") == 0) {
                    if (e->args.len != 0) xs_error(e->pos, NULL, "reverse takes no arguments");
                    e->type = e->lhs->type;
                    e->builtin.cfn = m;
                    return;
                }
                if (strcmp(m, "slice") == 0) {
                    if (e->args.len > 2) xs_error(e->pos, NULL, "slice takes at most two arguments");
                    for (int i = 0; i < e->args.len; i++) { check_expr(e->args.items[i], ty_int); expect_type(e->args.items[i], ty_int, "argument of slice"); }
                    e->type = e->lhs->type;
                    e->builtin.cfn = m;
                    return;
                }
            }
            {
                static const char *names[] = {"length", "push", "pop", "unshift", "shift", "splice", "forEach", "map", "filter", "find", "findIndex", "some", "every", "indexOf", "includes", "join", "sort", "reverse", "slice"};
                xs_error(e->pos, xs_help_suggest(e->str, names, 19, "arrays support indexing, length, push, pop, unshift, shift, splice(i, 1), forEach, map, filter, find, findIndex, some, every, indexOf, includes, join, sort(cmp), reverse, slice"), "unsupported array member '%s'", e->str);
            }
            return;
        }
        case TY_STRING:
            check_string_member(e);
            return;
        case TY_OBJECT: {
            Class *oc = e->lhs->type->cls;
            if (oc->builtin && strcmp(oc->name, "StatementSync") == 0) {
                /* stmt.run(...params) / stmt.all(...params) / stmt.get(...params): parameters are bound by their static type */
                const char *m = e->str;
                if (!e->is_call || (strcmp(m, "run") != 0 && strcmp(m, "all") != 0 && strcmp(m, "get") != 0))
                    xs_error(e->pos, "StatementSync has run(...params), all(...params) and get(...params)", "StatementSync has no member '%s'", m);
                for (int i = 0; i < e->args.len; i++) {
                    Expr *a = e->args.items[i];
                    check_expr(a, NULL);
                    settle(a);
                    TypeKind k = a->type->kind;
                    bool ok = k == TY_INT || k == TY_NUMBER || k == TY_STRING || k == TY_BOOLEAN || k == TY_NULL ||
                              (k == TY_ENUM) || (k == TY_NULLABLE && a->type->elem->kind == TY_STRING);
                    if (!ok) xs_error(a->pos, "parameters are int, number, string, boolean or null", "cannot bind %s as a SQL parameter", ty_name(a->type));
                }
                if (strcmp(m, "run") == 0) e->type = ty_object_of(find_class("RunResult"));
                else {
                    bool all = strcmp(m, "all") == 0;
                    Type *want = expected;
                    Type *row = !want ? NULL : all ? (want->kind == TY_ARRAY ? want->elem : NULL) : (want->kind == TY_NULLABLE ? want->elem : NULL);
                    if (!row || row->kind != TY_OBJECT || row->cls->builtin)
                        xs_error(e->pos, all ? "write the row type: const rows: Todo[] = stmt.all();" : "write the row type: const row: Todo | null = stmt.get(id);",
                                 "%s() needs to know the row type it produces", m);
                    if (!json_ok(row, 0)) xs_error(e->pos, NULL, "%s cannot be built from a row", ty_name(row));
                    e->type = want;
                }
                e->builtin.cfn = m;
                e->cls = oc;
                prog->uses_sqlite = true;
                return;
            }
            if (oc->builtin && strcmp(oc->name, "Server") == 0 && e->is_call && strcmp(e->str, "listen") == 0 && e->args.len == 2) {
                /* app.listen(port, { cert: "cert.pem", key: "key.pem" }): HTTPS */
                check_expr(e->args.items[0], ty_int);
                expect_type(e->args.items[0], ty_int, "port");
                Expr *o = e->args.items[1];
                if (o->kind != E_OBJLIT) xs_error(o->pos, "app.listen(443, { cert: \"cert.pem\", key: \"key.pem\" })", "listen options must be an object literal");
                Expr *cert = NULL, *key = NULL;
                for (int i = 0; i < o->args.len; i++) {
                    if (strcmp(o->keys[i], "cert") == 0) cert = o->args.items[i];
                    else if (strcmp(o->keys[i], "key") == 0) key = o->args.items[i];
                    else xs_error(o->args.items[i]->pos, "supported: cert, key", "unknown listen option '%s'", o->keys[i]);
                    check_expr(o->args.items[i], ty_string);
                    expect_type(o->args.items[i], ty_string, xs_sprintf("listen option '%s'", o->keys[i]));
                }
                if (!cert || !key) xs_error(o->pos, "give both: { cert: \"cert.pem\", key: \"key.pem\" }", "HTTPS needs a certificate and a key");
                e->args.len = 1;
                expr_list_push(&e->args, cert);
                expr_list_push(&e->args, key);
                e->cls = oc;
                e->type = ty_void;
                e->builtin.cfn = "xs_Server_listen_tls";
                e->builtin.self_first = true;
                if (xs_tls_off) xs_error(o->pos, "remove --tls=off, or listen without cert/key behind the load balancer", "HTTPS server, but the program is built with --tls=off");
                prog->uses_net = true;
                prog->uses_tls = true;
                return;
            }
            if (oc->builtin && (strcmp(oc->name, "Worker") == 0 || strcmp(oc->name, "MessagePort") == 0) && e->is_call) {
                /* bytes too: postMessage(Uint8Array) and on("message", (m: Uint8Array) => ...), as Node's structured
                   clone carries a typed array; a listener gets the kind it declares */
                bool w = strcmp(oc->name, "Worker") == 0;
                /* shared memory as in Node: a SharedArrayBuffer, or a Float64Array on one, arrives as a view on the
                   same memory (a Float64Array of its own, as a copy) */
                Class *u8 = find_class("Uint8Array");
                if (strcmp(e->str, "postMessage") == 0 && e->args.len == 1) {
                    check_expr(e->args.items[0], NULL);
                    Type *t = e->args.items[0]->type;
                    bool bytes = t && t->kind == TY_OBJECT && t->cls && (t->cls == u8 || (t->cls->base && t->cls->base == u8));
                    if (bytes || is_sab(t) || is_f64(t)) {
                        e->cls = oc;
                        e->type = ty_void;
                        e->builtin.cfn = xs_sprintf("xs_%s_post%s", w ? "Worker" : "MessagePort", bytes ? "Bytes" : is_sab(t) ? "Sab" : "F64");
                        e->builtin.self_first = true;
                        prog->uses_net = true;
                        return;
                    }
                }
                if (strcmp(e->str, "on") == 0 && e->args.len == 2 && e->args.items[1]->kind == E_ARROW && e->args.items[1]->arrow &&
                    e->args.items[1]->arrow->fn && e->args.items[1]->arrow->fn->nparams == 1) {
                    Type *pt = e->args.items[1]->arrow->fn->params[0].type;
                    if (pt) pt = resolve(pt, e->pos);
                    if (pt && pt->kind == TY_OBJECT && (pt->cls == u8 || is_sab(pt) || is_f64(pt))) {
                        check_expr(e->args.items[0], ty_string);
                        expect_type(e->args.items[0], ty_string, "event name");
                        Type **lp = xs_alloc(sizeof(Type *));
                        lp[0] = ty_object_of(pt->cls);
                        check_expr(e->args.items[1], ty_func(lp, 1, ty_void));
                        e->cls = oc;
                        e->type = ty_object_of(oc);
                        e->builtin.cfn = xs_sprintf("xs_%s_on_%s", w ? "Worker" : "MessagePort", pt->cls == u8 ? "bytes" : is_sab(pt) ? "sab" : "f64");
                        e->builtin.self_first = true;
                        prog->uses_net = true;
                        return;
                    }
                }
            }
            check_object_member(e, oc);
            if (oc->builtin && (strcmp(oc->name, "DatabaseSync") == 0 || strcmp(oc->name, "RunResult") == 0)) prog->uses_sqlite = true;
            else if (oc->builtin && strcmp(oc->name, "Error") != 0) prog->uses_net = true;
            return;
        }
        case TY_INT: case TY_NUMBER: case TY_LITINT:
            if (e->is_call && strcmp(e->str, "toString") == 0 && e->args.len <= 1) { /* n.toString(), n.toString(radix) */
                if (e->lhs->type->kind == TY_LITINT) coerce_lit(e->lhs, ty_int);
                if (e->args.len == 1) {
                    check_expr(e->args.items[0], ty_int);
                    expect_type(e->args.items[0], ty_int, "radix");
                }
                bool is_int = e->lhs->type->kind == TY_INT;
                if (!is_int && e->args.len == 1) xs_error(e->pos, "convert first: Math.trunc(x).toString(16)", "toString(radix) works on int");
                e->builtin.cfn = is_int ? (e->args.len ? "xs_int_to_radix" : "xs_str_from_int") : "xs_str_from_num";
                e->type = ty_string;
                return;
            }
            if (e->is_call && strcmp(e->str, "toFixed") == 0 && e->args.len <= 1) { /* n.toFixed(digits): JavaScript's rounding */
                if (e->lhs->type->kind == TY_LITINT) coerce_lit(e->lhs, ty_int);
                if (e->args.len == 1) { check_expr(e->args.items[0], ty_int); expect_type(e->args.items[0], ty_int, "digits"); }
                else { Expr *z = xs_alloc(sizeof *z); z->kind = E_NUMBER; z->pos = e->pos; z->type = ty_int; z->field = -1; expr_list_push(&e->args, z); }
                e->builtin.cfn = e->lhs->type->kind == TY_INT ? "xs_int_to_fixed" : "xs_num_to_fixed";
                e->type = ty_string;
                return;
            }
            xs_error(e->pos, NULL, "%s has no member '%s'", ty_name(e->lhs->type), e->str);
            return;
        default:
            xs_error(e->pos, NULL, "%s has no member '%s'", ty_name(e->lhs->type), e->str);
        }
        return;
    case E_NEW_ARRAY:
        xs_error(e->pos, "write: new Array<T>(n).fill(x)", "a new array must be filled before use");
        return;
    case E_REGEX: { /* /pattern/flags: compiled here once so a bad pattern is a compile error */
        const char *err = NULL;
        int fl = xr_parse_flags(e->rflags, strlen(e->rflags), &err);
        if (fl < 0) xs_error(e->pos, "flags: d g i m s u v y, each at most once", "invalid regular expression flags '%s'", e->rflags);
        xr_re *re = xr_compile(e->str, strlen(e->str), fl, &err);
        if (!re) xs_error(e->pos, NULL, "invalid regular expression /%s/: %s", e->str, err);
        xr_free(re);
        e->type = ty_object_of(find_class("RegExp"));
        prog->uses_regex = true;
        return;
    }
    case E_NULL:
        e->type = ty_null;
        if (expected && expected->kind == TY_NULLABLE) e->type = expected;
        return;
    case E_AS: {
        if (!e->elem_type && e->op == TK_NOT) { /* x!: not null, checked at run time */
            check_expr(e->lhs, expected && expected->kind != TY_NULLABLE ? ty_nullable_of(expected) : expected);
            if (e->lhs->type->kind != TY_NULLABLE) { *e = *e->lhs; return; }
            e->type = e->lhs->type->elem;
            return;
        }
        Type *want = resolve(e->elem_type, e->pos);
        check_expr(e->lhs, want);
        Expr *v = e->lhs;
        if (v->type->kind == TY_LITINT && is_numeric(want)) coerce_lit(v, want);
        if (ty_equal(v->type, want)) { *e = *v; return; }
        if (v->type->kind == TY_UNKNOWN) { /* checked at run time: the emitter compares the object's type */
            if (!unknown_target(want)) xs_error(e->pos, NULL, "cannot check %s at run time", ty_name(want));
            e->type = want;
            e->elem_type = want;
            return;
        }
        if (want->kind == TY_UNKNOWN) { *e = *v; to_unknown(e); return; }
        if (v->type->kind == TY_JSON && json_target(want)) { *e = *v; from_json(e, want); return; }
        if (want->kind == TY_JSON && json_ok(v->type, 0)) { *e = *v; to_json(e); return; }
        if (want->kind == TY_NULLABLE && ty_equal(v->type, want->elem)) { *e = *v; e->type = want; return; }
        if (v->type->kind == TY_NULLABLE && ty_equal(v->type->elem, want)) { e->type = want; e->op = TK_NOT; e->elem_type = NULL; return; } /* T | null as T: checked */
        { /* base as Sub: checked at run time (a TypeError when the object is not a Sub); Sub as Base: an upcast */
            Type *g = v->type->kind == TY_NULLABLE ? v->type->elem : v->type, *w = want->kind == TY_NULLABLE ? want->elem : want;
            if (g->kind == TY_OBJECT && w->kind == TY_OBJECT && !g->cls->is_interface && !w->cls->is_interface) {
                if (is_subclass(g->cls, w->cls)) { *e = *v; upcast(e, want); return; }
                if (is_subclass(w->cls, g->cls)) { e->type = want; e->elem_type = want; e->op = TK_CLASS; return; } /* op marks a downcast */
            }
        }
        xs_error(e->pos, "xisto checks types statically: 'as' converts an object (JSON) value or restates the same type", "cannot convert %s to %s", ty_name(v->type), ty_name(want));
        return;
    }
    case E_OBJLIT: {
        Type *t = expected && expected->kind == TY_NULLABLE ? expected->elem : expected;
        if (t && t->kind == TY_UNION) { /* which member: the one whose literal field (kind: "circle") the object gives */
            Type *pick = NULL;
            int nobj = 0;
            for (int i = 0; i < t->nparams; i++) {
                Type *m = t->params[i];
                if (m->kind != TY_OBJECT || m->cls->builtin) continue;
                nobj++;
                bool match = true, has_lit = false;
                for (int k = 0; k < m->cls->nfields; k++) {
                    Field *fd = &m->cls->fields[k];
                    if (!(fd->type->kind == TY_STRING && fd->type->tname)) continue;
                    has_lit = true;
                    bool given = false;
                    for (int j = 0; j < e->args.len; j++)
                        if (strcmp(e->keys[j], fd->name) == 0 && e->args.items[j]->kind == E_STRING && strcmp(e->args.items[j]->str, fd->type->tname) == 0) given = true;
                    if (!given) match = false;
                }
                if (has_lit && match) pick = m;
            }
            if (!pick && nobj == 1)
                for (int i = 0; i < t->nparams; i++) if (t->params[i]->kind == TY_OBJECT) pick = t->params[i];
            if (!pick) xs_error(e->pos, "give the member's kind field its literal value", "cannot tell which member of %s this object is", ty_name(t));
            t = pick;
        }
        if (t && t->kind == TY_JSON) { anon_objlit(e); return; }
        if (!t || t->kind != TY_OBJECT)
            xs_error(e->pos, "give it a type: const t: Todo = { ... };  (declare Todo with interface or class)", "an object literal needs a declared type here");
        Class *c = t->cls;
        if (c->builtin) xs_error(e->pos, NULL, "%s cannot be built from an object literal", c->name);
        if (c->ctor) xs_error(e->pos, xs_sprintf("write new %s(...)", c->name), "%s has a constructor; build it with new", c->name);
        bool *given = xs_alloc(sizeof(bool) * (size_t)(c->nfields + 1));
        for (int i = 0; i < e->args.len; i++) {
            int idx = -1;
            for (int k = 0; k < c->nfields; k++) if (strcmp(c->fields[k].name, e->keys[i]) == 0) idx = k;
            if (idx < 0) {
                int n; const char **cands = member_names(c, &n);
                xs_error(e->args.items[i]->pos, xs_help_suggest(e->keys[i], cands, n, NULL), "%s has no field '%s'", c->name, e->keys[i]);
            }
            if (given[idx]) xs_error(e->args.items[i]->pos, NULL, "field '%s' is given twice", e->keys[i]);
            given[idx] = true;
            check_expr(e->args.items[i], c->fields[idx].type);
            expect_type(e->args.items[i], c->fields[idx].type, xs_sprintf("field '%s'", e->keys[i]));
        }
        for (int k = 0; k < c->nfields; k++)
            if (!given[k] && !c->fields[k].init && !c->fields[k].optional)
                xs_error(e->pos, xs_sprintf("add %s: ... (or declare it optional: %s?: %s)", c->fields[k].name, c->fields[k].name, ty_name(c->fields[k].type)),
                         "missing field '%s' of %s", c->fields[k].name, c->name);
        e->cls = c;
        e->type = t;
        return;
    }
    case E_ARROW: {
        Arrow *a = e->arrow;
        Func *f = a->fn;
        Type *want = expected && expected->kind == TY_FUNC ? expected : NULL;
        if (want && want->nparams != f->nparams)
            xs_error(e->pos, NULL, "this arrow function takes %d parameter(s), but %d are expected here", f->nparams, want->nparams);
        for (int i = 0; i < f->nparams; i++) {
            if (f->params[i].type) f->params[i].type = resolve(f->params[i].type, e->pos);
            else if (want) f->params[i].type = want->params[i];
            else xs_error(e->pos, "write the type: (x: number) => ...", "parameter '%s' needs a type here", f->params[i].name);
            if (want && !ty_equal(f->params[i].type, want->params[i]))
                xs_error(e->pos, NULL, "parameter '%s' is %s, but %s is expected here", f->params[i].name, ty_name(f->params[i].type), ty_name(want->params[i]));
        }
        if (f->ret) f->ret = resolve(f->ret, e->pos);
        else if (want) f->ret = want->ret;
        /* (x): int => ... where a function returning unknown is expected: TypeScript accepts it (the return type
           is covariant); the arrow returns unknown and its returns are converted (boxed) */
        if (want && want->ret && want->ret->kind == TY_UNKNOWN && f->ret && f->ret->kind != TY_UNKNOWN && f->ret->kind != TY_VOID) f->ret = ty_unknown;
        a->id = ++prog->narrows;
        f->cname = xs_sprintf("arrow%d", a->id);
        /* check the body in its own function context, behind a capture boundary */
        Func *saved_func = cur_func;
        int saved_loop = loop_depth, saved_switch = switch_depth, saved_fin = finally_depth;
        loop_depth = switch_depth = finally_depth = 0;
        cur_func = f;
        push_scope();
        scope->arrow = a;
        if (f->body) { ii_enter(f, false); infer_ints_fn(f->body); } /* XB-034 */
        for (int i = 0; i < f->nparams; i++) {
            Binding *pb = declare(e->pos, f->params[i].name, f->params[i].type, false, false, true);
            f->params[i].cname = pb->plain ? pb->plain : pb->cname;
            f->params[i].cell = pb->cell;
        }
        if (!f->ret) {
            /* infer from the body: an expression body takes the expression's type; a block needs an annotation unless it never returns a value */
            if (a->expr_body) { /* ([k, v]) => ...: the reads of a destructured parameter come first */
                for (int i = 0; i + 1 < f->body->stmts.len; i++) check_stmt(f->body->stmts.items[i]);
                Stmt *r = f->body->stmts.items[f->body->stmts.len - 1];
                check_expr(r->expr, NULL);
                settle(r->expr);
                f->ret = r->expr->type;
                if (f->ret->kind == TY_VOID) { r->kind = S_EXPR; }
                else if (f->ret->kind == TY_NULL) xs_error(r->expr->pos, "write the return type: (x): Todo | null => ...", "cannot infer the return type from null");
            } else {
                f->ret = ty_void;
                check_block(f->body);
                for (int i = 0; i < f->body->stmts.len; i++) if (f->body->stmts.items[i]->kind == S_RETURN && f->body->stmts.items[i]->expr)
                    xs_error(f->body->stmts.items[i]->pos, "write the return type: (x): number => { ... }", "an arrow function with a block body needs a return type to return a value");
            }
        } else {
            if (a->expr_body && f->ret->kind == TY_VOID) {
                for (int i = 0; i + 1 < f->body->stmts.len; i++) check_stmt(f->body->stmts.items[i]);
                Stmt *r = f->body->stmts.items[f->body->stmts.len - 1];
                r->kind = S_EXPR;
                check_expr(r->expr, NULL);
                settle(r->expr);
            } else {
                check_block(f->body);
                if (f->ret->kind != TY_VOID && !always_returns(f->body))
                    xs_error(e->pos, "add a return at the end", "this arrow function may reach its end without returning a %s", ty_name(f->ret));
            }
        }
        pop_scope();
        cur_func = saved_func;
        loop_depth = saved_loop;
        switch_depth = saved_switch;
        finally_depth = saved_fin;
        Type **ps = xs_alloc(sizeof(Type *) * (size_t)(f->nparams ? f->nparams : 1));
        for (int i = 0; i < f->nparams; i++) ps[i] = f->params[i].type;
        e->type = ty_func(ps, f->nparams, f->ret);
        return;
    }
    case E_NEW_MAP:
        e->type = resolve(e->elem_type, e->pos);
        check_type_supported(e->pos, e->type);
        if (e->type->kind == TY_CHAN) {
            if (e->args.len > 1) xs_error(e->pos, NULL, "new Channel<T>(capacity) takes at most one argument");
            if (e->args.len == 1) { check_expr(e->args.items[0], ty_int); expect_type(e->args.items[0], ty_int, "channel capacity"); }
            Type *el = e->type->elem;
            if (el->kind == TY_VOID || el->kind == TY_FUNC || el->kind == TY_NULLABLE || el->kind == TY_NULL)
                xs_error(e->pos, NULL, "unsupported channel element type '%s'", ty_name(el));
            prog->uses_net = true; /* parked receivers and senders are coroutines of the event loop */
        } else if (e->args.len == 1) { /* new Map<K, V>([[k, v], ...]) / new Set<T>([a, b]): an array of entries or values */
            bool is_map = e->type->kind == TY_MAP;
            Type *want;
            if (is_map) {
                Type **ps = xs_alloc(sizeof(Type *) * 2);
                ps[0] = e->type->key;
                ps[1] = e->type->elem;
                want = ty_array_of(tuple_type(ps, 2, e->pos));
            } else want = ty_array_of(e->type->elem);
            check_expr(e->args.items[0], want);
            expect_type(e->args.items[0], want, is_map ? "the entries of new Map" : "the values of new Set");
        }
        return;
    case E_NEW_OBJ: {
        Class *c = find_class(e->str);
        if (!c) {
            int n; const char **cands = class_names(&n);
            xs_error(e->pos, xs_help_suggest(e->str, cands, n, "declare it: class Name { ... }"), "unknown class '%s'", e->str);
        }
        e->cls = c;
        e->type = ty_object_of(c);
        if (c->builtin && c->ctor && c->ctor->is_native) { /* a native package's class: its C constructor */
            check_args(e, c->ctor, xs_sprintf("the constructor of %s", c->name));
            e->builtin.cfn = c->ctor->cname;
            return;
        }
        if (c->builtin) {
            if (strcmp(c->name, "Worker") == 0) { /* new Worker(__filename, { workerData: text }): this program, on a thread */
                const char *hint = "new Worker(__filename, { workerData: JSON.stringify(task) }): the worker runs this same program";
                if (e->args.len < 1 || e->args.len > 2) xs_error(e->pos, hint, "Worker takes the program's file and its options");
                Expr *f = e->args.items[0];
                if (!(f->kind == E_IDENT && f->str && strcmp(f->str, "__filename") == 0))
                    xs_error(f->pos, hint, "a worker runs this program again: its file must be __filename");
                Expr *data = NULL;
                if (e->args.len == 2) {
                    Expr *o = e->args.items[1];
                    if (o->kind != E_OBJLIT) xs_error(o->pos, hint, "the options are an object literal");
                    for (int k = 0; k < o->args.len; k++) {
                        if (strcmp(o->keys[k], "workerData") != 0) xs_error(o->args.items[k]->pos, hint, "the only option is workerData");
                        data = o->args.items[k];
                    }
                }
                if (!data) { data = xs_alloc(sizeof *data); data->kind = E_STRING; data->str = ""; data->pos = e->pos; }
                else {
                    check_expr(data, ty_string);
                    expect_type(data, ty_string, "workerData (text: JSON.stringify(value), and JSON.parse in the worker)");
                }
                e->args.len = 0;
                expr_list_push(&e->args, data);
                if (data->kind == E_STRING && !data->type) check_expr(data, ty_string);
                e->builtin.cfn = "xs_Worker_new";
                prog->uses_net = true;
                prog->uses_workers = true;
                return;
            }
            if (strcmp(c->name, "DatabaseSync") == 0) {
                if (e->args.len != 1) xs_error(e->pos, "new DatabaseSync(\"app.db\") or new DatabaseSync(\":memory:\")", "DatabaseSync takes a path");
                check_expr(e->args.items[0], ty_string);
                expect_type(e->args.items[0], ty_string, "database path");
                e->builtin.cfn = "xs_DatabaseSync_new";
                prog->uses_sqlite = true;
                return;
            }
            if (strcmp(c->name, "Uint8Array") == 0) { /* new Uint8Array(length | int[] | number[] | bytes) */
                if (e->args.len != 1) xs_error(e->pos, "new Uint8Array(16), new Uint8Array([1, 2, 3])", "new Uint8Array takes one argument");
                check_expr(e->args.items[0], e->args.items[0]->kind == E_ARRAY_LIT ? ty_array_of(ty_int) : NULL);
                Type *st = e->args.items[0]->type;
                if (st->kind == TY_INT || st->kind == TY_LITINT) { if (st->kind == TY_LITINT) coerce_lit(e->args.items[0], ty_int); e->builtin.cfn = "xs_u8_new"; }
                else if (is_bytes(st)) e->builtin.cfn = "xs_u8_copy";
                else if (st->kind == TY_ARRAY && (st->elem->kind == TY_INT || st->elem->kind == TY_LITINT)) e->builtin.cfn = "xs_u8_from_ints";
                else if (st->kind == TY_ARRAY && st->elem->kind == TY_NUMBER) e->builtin.cfn = "xs_u8_from_nums";
                else xs_error(e->args.items[0]->pos, NULL, "new Uint8Array takes a length, an int[] or number[], or bytes, not %s", ty_name(st));
                return;
            }
            if (strcmp(c->name, "SharedArrayBuffer") == 0) { /* new SharedArrayBuffer(bytes): zeroed, shared by postMessage */
                if (e->args.len != 1) xs_error(e->pos, "new SharedArrayBuffer(8 * n)", "new SharedArrayBuffer takes its length in bytes");
                int_arg(&e->args.items[0], "the length of a SharedArrayBuffer");
                e->builtin.cfn = "xs_sab_new";
                return;
            }
            if (strcmp(c->name, "Float64Array") == 0) { /* new Float64Array(length | number[] | int[] | Float64Array | sab, offset?, length?) */
                const char *hint = "new Float64Array(n), new Float64Array([1.5, 2]), new Float64Array(sharedBuffer)";
                if (e->args.len < 1 || e->args.len > 3) xs_error(e->pos, hint, "new Float64Array takes one argument (or a SharedArrayBuffer, an offset and a length)");
                check_expr(e->args.items[0], e->args.items[0]->kind == E_ARRAY_LIT ? ty_array_of(ty_number) : NULL);
                Type *st = e->args.items[0]->type;
                if (e->args.len > 1 && !is_sab(st)) xs_error(e->pos, hint, "an offset and a length go with a SharedArrayBuffer");
                if (st->kind == TY_INT || st->kind == TY_LITINT) { if (st->kind == TY_LITINT) coerce_lit(e->args.items[0], ty_int); e->builtin.cfn = "xs_f64_new"; }
                else if (is_f64(st)) e->builtin.cfn = "xs_f64_copy";
                else if (is_sab(st)) {
                    for (int k = 1; k < e->args.len; k++) int_arg(&e->args.items[k], k == 1 ? "the byte offset" : "the length");
                    for (int k = e->args.len; k < 3; k++) expr_list_push(&e->args, num_lit(e->pos, (double)XS_NOT_GIVEN, true));
                    e->builtin.cfn = "xs_f64_on";
                }
                else if (st->kind == TY_ARRAY && st->elem->kind == TY_NUMBER) e->builtin.cfn = "xs_f64_from_nums";
                else if (st->kind == TY_ARRAY && (st->elem->kind == TY_INT || st->elem->kind == TY_LITINT)) e->builtin.cfn = "xs_f64_from_ints";
                else xs_error(e->args.items[0]->pos, hint, "new Float64Array takes a length, a number[], a Float64Array or a SharedArrayBuffer, not %s", ty_name(st));
                return;
            }
            if (strcmp(c->name, "TextEncoder") == 0 || strcmp(c->name, "TextDecoder") == 0) {
                if (e->args.len > (c->name[4] == 'D' ? 1 : 0)) xs_error(e->pos, NULL, "new %s() takes no arguments (UTF-8 only)", c->name);
                if (e->args.len == 1) { check_expr(e->args.items[0], ty_string); expect_type(e->args.items[0], ty_string, "the encoding"); e->args.len = 0; }
                e->builtin.cfn = c->name[4] == 'E' ? "xs_TextEncoder_new" : "xs_TextDecoder_new";
                return;
            }
            if (strcmp(c->name, "Buffer") == 0) xs_error(e->pos, "use Buffer.from(...) or Buffer.alloc(n)", "new Buffer() is deprecated (and not supported)");
            if (strcmp(c->name, "Date") == 0) { /* new Date(), new Date(ms | text | date), new Date(y, m, d?, h?, mi?, s?, ms?) */
                int n = e->args.len;
                if (n == 0) { e->builtin.cfn = "xs_Date_new_now"; return; }
                if (n == 1) {
                    Expr *a = e->args.items[0];
                    check_expr(a, NULL);
                    if (a->type->kind == TY_STRING) { e->builtin.cfn = "xs_Date_new_str"; return; }
                    if (a->type->kind == TY_OBJECT && a->type->cls == c) { e->builtin.cfn = "xs_Date_new_copy"; return; }
                    date_num_arg(&e->args.items[0], "the time value of new Date");
                    e->builtin.cfn = "xs_Date_new_ms";
                    return;
                }
                if (n > 7) xs_error(e->pos, NULL, "new Date takes at most 7 arguments");
                for (int k = 0; k < n; k++) date_num_arg(&e->args.items[k], xs_sprintf("argument %d of new Date", k + 1));
                for (int k = n; k < 7; k++) expr_list_push(&e->args, num_lit(e->pos, k == 2 ? 1 : 0, false));
                e->builtin.cfn = "xs_Date_new_parts";
                return;
            }
            if (strcmp(c->name, "RegExp") == 0) { /* new RegExp(pattern, flags = "") */
                if (e->args.len < 1 || e->args.len > 2) xs_error(e->pos, "new RegExp(\"a+b\", \"gi\")", "new RegExp takes a pattern and optional flags");
                for (int k = 0; k < e->args.len; k++) {
                    check_expr(e->args.items[k], ty_string);
                    expect_type(e->args.items[k], ty_string, k == 0 ? "the RegExp pattern" : "the RegExp flags");
                }
                if (e->args.len == 1) {
                    Expr *nf = xs_alloc(sizeof *nf);
                    nf->kind = E_STRING; nf->pos = e->pos; nf->str = ""; nf->type = ty_string; nf->field = -1;
                    nf->lit_id = intern("");
                    expr_list_push(&e->args, nf);
                }
                e->builtin.cfn = "xs_RegExp_new";
                prog->uses_regex = true;
                return;
            }
            if (strcmp(c->name, "Error") == 0) {
                if (e->args.len > 1) xs_error(e->pos, NULL, "new Error() takes at most one argument");
                if (e->args.len == 1) { check_expr(e->args.items[0], ty_string); expect_type(e->args.items[0], ty_string, "the Error message"); }
                e->builtin.cfn = "xs_Error_new";
                return;
            }
            if (strcmp(c->name, "Server") != 0)
                xs_error(e->pos, NULL, "%s cannot be constructed directly", c->name);
            if (e->args.len != 0) xs_error(e->pos, NULL, "new Server() takes no arguments");
            e->builtin.cfn = "xs_Server_new";
            prog->uses_net = true;
            return;
        }
        if (c->is_interface) xs_error(e->pos, xs_sprintf("write an object literal: const x: %s = { ... };", c->name), "'%s' is an interface; it has no constructor", c->name);
        if (c->data_only) xs_error(e->pos, "give its fields initializers (or a constructor that sets them) to build it with new", "%s has fields without a value: it is filled from JSON, not constructed", c->name);
        if (c->is_abstract) xs_error(e->pos, "make an instance of a class that extends it", "%s is abstract", c->name);
        if (eff_ctor(c)) {
            check_args(e, eff_ctor(c), xs_sprintf("the constructor of %s", c->name));
        } else if (e->args.len != 0) {
            xs_error(e->pos, NULL, "%s has no constructor, so 'new %s()' takes no arguments", c->name, c->name);
        }
        return;
    }
    case E_ARRAY_LIT: {
        if (expected && expected->kind == TY_NULLABLE) expected = expected->elem;
        if (expected && expected->kind == TY_OBJECT && expected->cls->is_tuple) { /* [x, y] as a tuple: its fields */
            Class *tc = expected->cls;
            if (e->args.len != tc->nfields) xs_error(e->pos, NULL, "the tuple %s has %d elements, but this gives %d", tc->name, tc->nfields, e->args.len);
            e->kind = E_OBJLIT;
            e->keys = xs_alloc(sizeof(char *) * (size_t)tc->nfields);
            for (int k = 0; k < tc->nfields; k++) e->keys[k] = tc->fields[k].name;
            check_expr(e, expected);
            return;
        }
        Type *elem = expected && expected->kind == TY_ARRAY ? expected->elem : NULL;
        if (e->args.len == 0) {
            if (!elem) xs_error(e->pos, "give it a type: const xs: number[] = [];", "cannot infer the type of an empty array");
            e->type = expected;
            check_type_supported(e->pos, e->type);
            return;
        }
        for (int i = 0; i < e->args.len; i++) check_expr(e->args.items[i], elem);
        if (!elem) {
            for (int i = 0; i < e->args.len; i++)
                if (is_numeric(e->args.items[i]->type)) elem = e->args.items[i]->type;
            if (!elem) {
                settle(e->args.items[0]);
                elem = e->args.items[0]->type;
            }
        }
        for (int i = 0; i < e->args.len; i++) expect_type(e->args.items[i], elem, "array element");
        e->type = ty_array_of(elem);
        check_type_supported(e->pos, e->type);
        return;
    }
    case E_FUNC_REF:
        return;
    case E_ENUM_VAL:
        return;
    case E_TEMPLATE:
        for (int i = 0; i < e->args.len; i++) {
            Expr *part = e->args.items[i];
            check_expr(part, NULL);
            settle(part);
            if (part->type->kind == TY_STRING && part->kind == E_STRING) part->lit_id = intern(part->str);
            if (is_big(part->type)) { /* `${b}`: its digits, without the n */
                Expr *t = xs_alloc(sizeof *t);
                t->pos = part->pos;
                big_call(t, "xs_big_to_string", part, NULL, ty_string);
                e->args.items[i] = part = t;
            }
            TypeKind k = part->type->kind;
            if (k != TY_STRING && k != TY_INT && k != TY_NUMBER && k != TY_BOOLEAN && k != TY_ENUM)
                xs_error(part->pos, "template literals take strings, numbers, ints, booleans and enums; call a method that returns a string for the rest", "cannot put a %s inside a template literal", ty_name(part->type));
        }
        e->type = ty_string;
        return;
    case E_COND: {
        check_expr(e->lhs, NULL);
        if (e->lhs->type->kind != TY_BOOLEAN && truthy_type(e->lhs->type)) make_truthy(e->lhs, "condition of '?:'");
        if (e->lhs->type->kind != TY_BOOLEAN)
            xs_error(e->lhs->pos, "there is no truthiness in xisto; compare explicitly", "condition of '?:' must be boolean, but this is %s", ty_name(e->lhs->type));
        Expr *a = e->args.items[0], *b = e->args.items[1];
        {
            NSet ts = {0}, fs = {0};
            narrow_set(e->lhs, true, &ts);
            narrow_set(e->lhs, false, &fs);
            UNarrow un = union_narrow(e->lhs);
            push_scope();
            if (un.b) shadow_union(un.b, un.then_t);
            else shadow_set(&ts);
            check_expr(a, expected);
            pop_scope();
            push_scope();
            if (un.b) shadow_union(un.b, un.else_t);
            else shadow_set(&fs);
            check_expr(b, expected);
            pop_scope();
        }
        if (a->type->kind == TY_LITINT && b->type->kind == TY_LITINT) {
            if (expected && is_numeric(expected)) { coerce_lit(a, expected); coerce_lit(b, expected); }
        } else if (a->type->kind == TY_LITINT) coerce_lit(a, b->type);
        else if (b->type->kind == TY_LITINT) coerce_lit(b, a->type);
        if (!ty_equal(a->type, b->type)) { /* T and null make T | null; two classes make their nearest common base */
            Type *ta = a->type, *tb = b->type, *u = NULL;
            bool nul = ta->kind == TY_NULL || tb->kind == TY_NULL || ta->kind == TY_NULLABLE || tb->kind == TY_NULLABLE;
            Type *ba = ta->kind == TY_NULLABLE ? ta->elem : ta, *bb = tb->kind == TY_NULLABLE ? tb->elem : tb;
            if (ba->kind == TY_NULL) ba = bb;
            if (bb->kind == TY_NULL) bb = ba;
            if (ba->kind != TY_NULL && ty_equal(ba, bb)) u = ba;
            else if (ba->kind == TY_OBJECT && bb->kind == TY_OBJECT && !ba->cls->is_interface && !bb->cls->is_interface)
                for (Class *c = ba->cls; c && !u; c = c->base) if (is_subclass(bb->cls, c)) u = ty_object_of(c);
            if (u && nul) {
                if (!nullable_ok(u)) u = NULL;
                else u = ty_nullable_of(u);
            }
            if (!u && expected && (expected->kind == TY_UNION || (expected->kind == TY_NULLABLE && expected->elem->kind == TY_UNION))) u = expected; /* both go into the union */
            if (!u) xs_error(e->pos, NULL, "the two branches of '?:' must have the same type, but they are %s and %s", ty_name(ta), ty_name(tb));
            expect_type(a, u, "first branch of '?:'");
            expect_type(b, u, "second branch of '?:'");
            e->type = u;
            return;
        }
        if (a->type->kind == TY_VOID) xs_error(e->pos, NULL, "the branches of '?:' cannot be void");
        e->type = a->type;
        return;
    }
    }
}

/* ---------- statements ---------- */

static void check_stmt(Stmt *s);

/* a variable whose declaration failed: declared anyway (with its written type, or as unknown) and poisoned,
   so the statements after it are checked and its uses add no errors of their own (XB-011) */
static void declare_poisoned(Stmt *s, bool global) {
    if (s->kind != S_VAR || s->cname) return;
    s->type = s->decl_type ? s->decl_type : ty_unknown;
    Binding *b = declare(s->pos, s->name, s->type, s->is_const, global, false);
    s->cname = b->cname;
    b->poisoned = !s->decl_type; /* with a written type, uses can still be checked against it */
    b->used = true;
}

/* Each statement of a block under its own recovery point: one wrong statement does not hide the errors of
   the ones after it. */
static void check_stmt_recover(Stmt *s) {
    jmp_buf *outer = xs_error_recover;
    if (!outer) { check_stmt(s); return; }
    jmp_buf rb;
    Scope *sc = scope;
    Binding *head = scope->head;
    Func *cf = cur_func;
    Class *cc = cur_class;
    Module *cm = cur_mod;
    int ld = loop_depth, sd = switch_depth, fd = finally_depth;
    xs_error_recover = &rb;
    if (_setjmp(rb) == 0) check_stmt(s);
    else {
        scope = sc;
        scope->head = head;
        cur_func = cf;
        cur_class = cc;
        loop_depth = ld;
        switch_depth = sd;
        finally_depth = fd;
        if (cm && cm != cur_mod) enter_module(cm);
        xs_error_recover = outer;
        declare_poisoned(s, false);
    }
    xs_error_recover = outer;
}

static void check_block(Stmt *b) {
    push_scope();
    for (int i = 0; i < b->stmts.len; i++) check_stmt_recover(b->stmts.items[i]);
    pop_scope();
}

static void check_var(Stmt *s, bool global) {
    if (s->infer_int && !s->decl_type && !getenv("XISTO_NO_INFER_INT")) { /* XB-034 */
        check_expr(s->init, ty_int);
        if (s->init->type->kind == TY_LITINT) coerce_lit(s->init, ty_int);
        s->type = ty_int;
        Binding *b = declare(s->pos, s->name, s->type, s->is_const, global, false);
        b->inferred_int = true;
        s->cname = b->cname;
        s->cell = b->cell;
        return;
    }
    if (s->decl_type) {
        s->decl_type = resolve(s->decl_type, s->pos);
        check_type_supported(s->pos, s->decl_type);
    }
    check_expr(s->init, s->decl_type);
    if (s->init->type->kind == TY_VOID) xs_error(s->init->pos, NULL, "cannot store a void value");
    if (s->init->type->kind == TY_NULL && !s->decl_type) xs_error(s->init->pos, "write the type: let x: Todo | null = null;", "cannot infer the type of '%s' from null", s->name);
    if (s->decl_type) expect_type(s->init, s->decl_type, xs_sprintf("initializer of '%s' (declared as %s)", s->name, ty_name(s->decl_type)));
    settle(s->init);
    s->type = s->decl_type ? s->decl_type : s->init->type;
    Binding *b = declare(s->pos, s->name, s->type, s->is_const, global, false);
    s->cname = b->cname;
    s->cell = b->cell;
}

/* ---- truthiness, narrowly: a string is true when not empty; T | null when not null (and, for strings, not
   empty). Numbers are never coerced. Only names and fields, which can be read twice: the test is rewritten
   into the explicit comparisons, so narrowing keeps working. ---- */
static bool truthy_type(Type *t) { return t->kind == TY_STRING || t->kind == TY_NULLABLE; }
static bool rereadable(Expr *e) {
    if (e->kind == E_IDENT || e->kind == E_THIS) return true;
    return e->kind == E_MEMBER && !e->is_call && e->field >= 0 && rereadable(e->lhs);
}
static Expr *expr_copy(Expr *e) { Expr *c = xs_alloc(sizeof *c); *c = *e; return c; }
static Expr *str_truthy_call(Expr *v) {
    Expr *c = xs_alloc(sizeof *c);
    c->kind = E_CALL;
    c->pos = v->pos;
    c->type = ty_boolean;
    c->builtin.cfn = "xs_str_truthy";
    expr_list_push(&c->args, v);
    return c;
}
/* rewrites e (already checked, of a truthy type) in place into a boolean test */
static void make_truthy(Expr *e, const char *what) {
    if (!rereadable(e))
        xs_error(e->pos, "store it in a const first, or compare explicitly", "%s must be boolean (truthiness applies to names and fields only)", what);
    Expr *v = expr_copy(e);
    memset(e, 0, sizeof *e);
    e->pos = v->pos;
    e->type = ty_boolean;
    if (v->type->kind == TY_STRING) {
        Expr *call = str_truthy_call(v);
        *e = *call;
        return;
    }
    Expr *nul = xs_alloc(sizeof *nul);
    nul->kind = E_NULL;
    nul->pos = v->pos;
    nul->type = v->type;
    Expr *test = xs_alloc(sizeof *test);
    test->kind = E_BINARY;
    test->op = TK_NEQ;
    test->pos = v->pos;
    test->lhs = v;
    test->rhs = nul;
    test->type = ty_boolean;
    if (v->type->elem->kind != TY_STRING) { *e = *test; return; }
    Expr *v2 = expr_copy(v);
    v2->type = ty_string; /* read after the null test */
    e->kind = E_BINARY;
    e->op = TK_AND;
    e->lhs = test;
    e->rhs = str_truthy_call(v2);
}
static void check_cond(Expr *c, const char *what) {
    check_expr(c, NULL);
    if (c->type->kind != TY_BOOLEAN && truthy_type(c->type)) make_truthy(c, what);
    if (c->type->kind != TY_BOOLEAN)
        xs_error(c->pos, "there is no truthiness for numbers in xisto; compare explicitly, e.g. 'x !== 0'", "%s must be boolean, but this is %s", what, ty_name(c->type));
}

/* A field read through a variable or `this` (o.f, this.f) of a nullable type: the name its narrowing goes by in
   the scopes, from the variable's C name (unique, so an inner `o` that shadows it is another path), or NULL */
static const char *path_key(Expr *m) {
    if (!m || m->kind != E_MEMBER || m->is_call || m->field < 0 || !m->type || m->type->kind != TY_NULLABLE || !m->cls || m->cls->builtin) return NULL;
    if (m->lhs->kind == E_THIS) return xs_sprintf("\x01this.%d.%s", m->field, m->cls->name);
    if (m->lhs->kind != E_IDENT) return NULL;
    Binding *b = lookup(m->lhs->str);
    if (!b || !b->cname) return NULL;
    return xs_sprintf("\x01%s.%d.%s", b->cname, m->field, m->cls->name);
}
/* `x !== null` / `x === null` on a nullable local: returns the binding and whether the test is "not null". */
static Binding *narrow_target(Expr *cond, bool *not_null) {
    if (cond->kind == E_BINARY && cond->op == TK_AND) {
        Binding *b = narrow_target(cond->lhs, not_null);
        return b && *not_null ? b : NULL; /* a && b true: a is; false says nothing */
    }
    if (cond->kind == E_UNARY && cond->op == TK_NOT) { /* !(x !== null && ...): false means x is not null */
        bool nn = false;
        Binding *b = narrow_target(cond->lhs, &nn);
        if (!b || !nn) return NULL;
        *not_null = false;
        return b;
    }
    if (cond->kind != E_BINARY || (cond->op != TK_EQ && cond->op != TK_NEQ)) return NULL;
    Expr *id = cond->lhs->kind == E_IDENT ? cond->lhs : cond->rhs->kind == E_IDENT ? cond->rhs : NULL;
    Expr *nul = cond->lhs->kind == E_NULL ? cond->lhs : cond->rhs->kind == E_NULL ? cond->rhs : NULL;
    if (!id && nul) { /* o.f !== null: the path o.f, as TypeScript narrows it */
        Expr *m = cond->lhs == nul ? cond->rhs : cond->lhs;
        if (m->kind == E_AS && m->op == TK_NOT && !m->elem_type) m = m->lhs; /* already narrowed by an outer test */
        const char *key = path_key(m);
        if (!key) return NULL;
        Binding *pb = lookup(key);
        if (!pb) {
            pb = xs_alloc(sizeof *pb);
            pb->name = (char *)key;
            pb->type = m->type;
            pb->cname = "";
        } else if (pb->type->kind != TY_NULLABLE) return NULL; /* narrowed already */
        *not_null = cond->op == TK_NEQ;
        return pb;
    }
    if (!id || !nul) return NULL;
    Binding *b = lookup(id->str);
    if (!b || b->type->kind != TY_NULLABLE) return NULL;
    *not_null = cond->op == TK_NEQ;
    return b;
}

/* The variables a condition proves non-null when it is `when` (true or false), as TypeScript reads it:
   x !== null (true), x === null (false), A && B (true: both sides), A || B (false: both sides), !A. */
static void narrow_set(Expr *c, bool when, NSet *out) {
    if (!c) return;
    if (c->kind == E_UNARY && c->op == TK_NOT) { narrow_set(c->lhs, !when, out); return; }
    if (c->kind == E_BINARY && c->op == TK_AND) { if (when) { narrow_set(c->lhs, true, out); narrow_set(c->rhs, true, out); } return; }
    if (c->kind == E_BINARY && c->op == TK_OR) { if (!when) { narrow_set(c->lhs, false, out); narrow_set(c->rhs, false, out); } return; }
    bool nn = false;
    Binding *b = narrow_target(c, &nn);
    if (b && nn == when && out->n < 8) out->b[out->n++] = b;
}
static void shadow_narrowed(Binding *b);
static void shadow_set(NSet *s) { for (int i = 0; i < s->n; i++) shadow_narrowed(s->b[i]); }

static void shadow_narrowed(Binding *b) {
    Binding *n = xs_alloc(sizeof *n);
    *n = *b;
    n->origin = b;
    n->orig = b->orig ? b->orig : b->type;
    n->type = b->type->elem;
    n->next = scope->head;
    scope->head = n;
}

/* After assigning to a narrowed variable it may be null again: shadow it back with the declared type. */
static void unnarrow(Binding *b) {
    Binding *n = xs_alloc(sizeof *n);
    *n = *b;
    n->origin = b;
    n->type = b->orig;
    n->orig = NULL;
    n->union_narrow = false;
    n->next = scope->head;
    scope->head = n;
}

/* ---- unions narrowed by a condition: typeof x === "string", x instanceof C, x.kind === "circle" (and !==) ---- */
static const char *typeof_name(Type *m) {
    if (m->kind == TY_STRING || (m->kind == TY_ENUM && m->en->is_string)) return "string";
    if (is_numeric(m) || m->kind == TY_ENUM) return "number";
    if (m->kind == TY_BOOLEAN) return "boolean";
    if (m->kind == TY_FUNC) return "function";
    return "object";
}
static Type *union_subset(Type *u, bool *keep) {
    Type **m = NULL;
    int n = 0;
    for (int i = 0; i < u->nparams; i++) if (keep[i]) { m = realloc(m, sizeof(Type *) * (size_t)(n + 1)); m[n++] = u->params[i]; }
    if (n == 0) return NULL;
    if (n == 1) return m[0];
    Type *r = xs_alloc(sizeof *r);
    r->kind = TY_UNION;
    r->params = m;
    r->nparams = n;
    return r;
}
static Expr *union_ident(Expr *e) { /* x, or x seen through a narrowing */
    if (e->kind == E_AS && e->op == TK_DOT) e = e->lhs;
    return e->kind == E_IDENT ? e : NULL;
}
static UNarrow union_narrow(Expr *c) {
    UNarrow r = {0};
    if (c->kind != E_BINARY) return r;
    bool neg = c->op == TK_NEQ;
    Expr *id = NULL, *lit = NULL;
    int how = 0; /* 1 typeof, 2 instanceof, 3 discriminant field */
    if (c->op == TK_IDENT) { id = union_ident(c->lhs); how = 2; }
    else if (c->op == TK_EQ || c->op == TK_NEQ) {
        Expr *a = c->lhs, *b = c->rhs;
        if (b->kind != E_STRING) { Expr *t = a; a = b; b = t; }
        if (b->kind != E_STRING) return r;
        lit = b;
        if (a->kind == E_UNARY && a->op == TK_IDENT) { id = union_ident(a->lhs); how = 1; }
        else if (a->kind == E_MEMBER && a->builtin.cfn && strcmp(a->builtin.cfn, "union_field") == 0) { id = union_ident(a->lhs); how = 3; }
    }
    if (!id) return r;
    Binding *b = lookup(id->str);
    if (!b) return r;
    Type *u = b->type;
    bool nul = u->kind == TY_NULLABLE && u->elem->kind == TY_UNION; /* number | string | null: null stays on the other side */
    if (nul) u = u->elem;
    if (u->kind != TY_UNION) return r;
    bool *keep = xs_alloc(sizeof(bool) * (size_t)u->nparams), *other = xs_alloc(sizeof(bool) * (size_t)u->nparams);
    for (int i = 0; i < u->nparams; i++) {
        Type *m = u->params[i];
        bool yes = false;
        if (how == 1) yes = strcmp(typeof_name(m), lit->str) == 0;
        else if (how == 2) yes = m->kind == TY_OBJECT && c->rhs->cls && is_subclass(m->cls, c->rhs->cls);
        else {
            Field *fd = m->kind == TY_OBJECT ? find_field(m->cls, c->lhs->kind == E_MEMBER ? c->lhs->str : c->rhs->str, NULL) : NULL;
            yes = fd && fd->type->tname && strcmp(fd->type->tname, lit->str) == 0;
        }
        keep[i] = neg ? !yes : yes;
        other[i] = !keep[i];
    }
    r.b = b;
    r.then_t = union_subset(u, keep);
    r.else_t = union_subset(u, other);
    if (nul) { /* typeof x === "number" is false for null: null joins the other branch (the reverse when negated) */
        if (neg) r.then_t = r.then_t ? ty_nullable_of(r.then_t) : NULL;
        else r.else_t = r.else_t ? ty_nullable_of(r.else_t) : NULL;
    }
    return r;
}
/* the variable seen as t (a member, or fewer members) in the current scope */
static void shadow_union(Binding *b, Type *t) {
    if (!b || !t) return;
    Binding *n = xs_alloc(sizeof *n);
    *n = *b;
    n->origin = b;
    n->orig = b->orig ? b->orig : b->type;
    n->type = t;
    n->union_narrow = true;
    n->next = scope->head;
    scope->head = n;
}
static void check_block_union(Stmt *b, Binding *ub, Type *t) {
    push_scope();
    shadow_union(ub, t);
    for (int i = 0; i < b->stmts.len; i++) check_stmt(b->stmts.items[i]);
    pop_scope();
}

static bool always_exits(Stmt *s);

static void check_block_narrowed_set(Stmt *b, NSet *ns) {
    push_scope();
    shadow_set(ns);
    for (int i = 0; i < b->stmts.len; i++) check_stmt(b->stmts.items[i]);
    pop_scope();
}
static void check_block_narrowed(Stmt *b, Binding *narrow) {
    push_scope();
    if (narrow) shadow_narrowed(narrow);
    for (int i = 0; i < b->stmts.len; i++) check_stmt(b->stmts.items[i]);
    pop_scope();
}

/* the body of a for...of over a Map can run without a snapshot of the entries when nothing in it can add to or
   remove from any map: no call of the program's own code (functions, methods, getters, constructors, closures),
   no set/delete/clear/add, no await or yield (another task could). Conservative: any doubt keeps the snapshot. */
static bool md_expr(Expr *e) {
    if (!e) return true;
    switch (e->kind) {
    case E_ARROW: return true; /* made, not run */
    case E_AWAIT: return false;
    case E_CALL: if (!e->builtin.cfn) return false; break;
    case E_NEW_OBJ: if (!e->cls || !e->cls->builtin) return false; break;
    case E_MEMBER:
        if (e->func && e->func->body) return false; /* a method or getter of the program */
        if (e->is_call && (!e->builtin.cfn && !(e->cls && e->cls->builtin))) return false;
        if (e->is_call && e->str && (!strcmp(e->str, "set") || !strcmp(e->str, "delete") || !strcmp(e->str, "clear") || !strcmp(e->str, "add"))) return false;
        break;
    default: break;
    }
    if (!md_expr(e->lhs) || !md_expr(e->rhs)) return false;
    for (int i = 0; i < e->args.len; i++) if (!md_expr(e->args.items[i])) return false;
    return true;
}
static bool md_stmt(Stmt *s) {
    if (!s) return true;
    if (s->kind == S_YIELD) return false;
    if (!md_expr(s->init) || !md_expr(s->expr) || !md_expr(s->cond) || !md_expr(s->for_update)) return false;
    if (!md_stmt(s->for_init) || !md_stmt(s->body) || !md_stmt(s->else_body)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!md_stmt(s->stmts.items[i])) return false;
    for (int i = 0; i < s->ncases; i++) if (!md_expr(s->cases[i].value) || !md_stmt(s->cases[i].body)) return false;
    return true;
}

static void check_stmt(Stmt *s) {
    switch (s->kind) {
    case S_VAR: check_var(s, false); return;
    case S_EXPR:
        check_expr(s->expr, NULL);
        settle(s->expr);
        return;
    case S_IF: {
        check_cond(s->cond, "condition");
        UNarrow un = union_narrow(s->cond);
        if (un.b) { /* a union narrowed in each branch; after `if (...) return;` the rest sees the other members */
            check_block_union(s->body, un.b, un.then_t);
            if (s->else_body) {
                if (s->else_body->kind == S_BLOCK) check_block_union(s->else_body, un.b, un.else_t);
                else { push_scope(); shadow_union(un.b, un.else_t); check_stmt(s->else_body); pop_scope(); }
            } else if (always_exits(s->body)) shadow_union(un.b, un.else_t);
            return;
        }
        NSet ts = {0}, fs = {0};
        narrow_set(s->cond, true, &ts);
        narrow_set(s->cond, false, &fs);
        check_block_narrowed_set(s->body, &ts);
        if (s->else_body) {
            if (s->else_body->kind == S_BLOCK) check_block_narrowed_set(s->else_body, &fs);
            else { /* else if (...): the chain sees them not null too */
                push_scope();
                shadow_set(&fs);
                check_stmt(s->else_body);
                pop_scope();
            }
        }
        /* if (x === null || y === null) { return; }  →  x and y are not null for the rest of the block */
        if (!s->else_body && always_exits(s->body)) shadow_set(&fs);
        return;
    }
    case S_WHILE: {
        check_cond(s->cond, "condition");
        bool nn = false;
        Binding *nb = narrow_target(s->cond, &nn);
        loop_depth++;
        check_block_narrowed(s->body, nb && nn ? nb : NULL);
        loop_depth--;
        return;
    }
    case S_FOR:
        push_scope();
        if (s->for_init) {
            for_init_decl = true; /* JavaScript gives each iteration its own copy */
            check_stmt(s->for_init);
            for_init_decl = false;
        }
        if (s->cond) check_cond(s->cond, "loop condition");
        if (s->for_update) {
            check_expr(s->for_update, NULL);
            settle(s->for_update);
        }
        loop_depth++;
        {
            bool nn = false;
            Binding *nb = s->cond ? narrow_target(s->cond, &nn) : NULL;
            check_block_narrowed(s->body, nb && nn ? nb : NULL);
        }
        loop_depth--;
        pop_scope();
        return;
    case S_THROW: {
        check_expr(s->expr, NULL);
        Class *err = find_class("Error");
        Type *t = s->expr->type;
        if (t->kind == TY_OBJECT && t->cls != err && !t->cls->builtin) {
            /* any object can be thrown, as in JavaScript: it travels as an Error whose cause is the object */
            Expr *v = expr_copy(s->expr);
            Expr *name = xs_alloc(sizeof *name);
            name->kind = E_STRING;
            name->pos = v->pos;
            name->str = t->cls->name;
            name->lit_id = intern(t->cls->name);
            name->type = ty_string;
            Expr *w = xs_alloc(sizeof *w);
            w->kind = E_CALL;
            w->pos = v->pos;
            w->builtin.cfn = "xs_Error_wrap";
            w->type = ty_object_of(err);
            expr_list_push(&w->args, name);
            expr_list_push(&w->args, v);
            s->expr = w;
            return;
        }
        if (t->kind != TY_OBJECT || t->cls != err)
            xs_error(s->expr->pos, "throw new Error(\"message\"), or an object of one of your classes", "%s cannot be thrown", ty_name(t));
        return;
    }
    case S_TRY: {
        /* with finally, a return/break/continue leaving the try or catch runs the finally on its way out (the
           emitter puts it at the jump); only yield is not supported there */
        bool fin = s->for_init != NULL;
        if (fin) finally_depth++;
        check_block(s->body);
        s->type = ty_object_of(find_class("Error"));
        if (s->else_body) {
            push_scope();
            Binding *cb = declare(s->pos, s->name, s->type, true, false, false);
            cb->no_warn = true;
            s->cname = cb->cname;
            for (int i = 0; i < s->else_body->stmts.len; i++) check_stmt(s->else_body->stmts.items[i]);
            pop_scope();
        }
        if (fin) { finally_depth--; check_block(s->for_init); }
        return;
    }
    case S_FOROF: {
        check_expr(s->expr, NULL);
        if (s->expr->type->kind == TY_OBJECT && !s->expr->type->cls->builtin && find_method(s->expr->type->cls, "__iterator")) {
            Expr *call = xs_alloc(sizeof *call); /* an iterable object: its [Symbol.iterator]() */
            call->kind = E_MEMBER;
            call->pos = s->expr->pos;
            call->lhs = s->expr;
            call->str = "__iterator";
            call->is_call = true;
            call->field = -1;
            s->expr = call;
            check_expr(s->expr, NULL);
        }
        if (is_bytes(s->expr->type) || is_f64(s->expr->type)) { /* for (const b of bytes): ints; of a Float64Array: numbers */
            Type *et = is_f64(s->expr->type) ? ty_number : ty_int;
            if (s->decl_type) { s->decl_type = resolve(s->decl_type, s->pos); if (s->decl_type->kind != et->kind) xs_error(s->pos, NULL, "the elements are %s, not %s", ty_name(et), ty_name(s->decl_type)); }
            s->type = et;
            push_scope();
            decl_no_cell = true;
            s->cname = declare(s->pos, s->name, et, s->is_const, false, false)->cname;
            decl_no_cell = false;
            loop_depth++;
            check_block(s->body);
            loop_depth--;
            pop_scope();
            return;
        }
        if (s->expr->type->kind == TY_MAP || s->expr->type->kind == TY_SET) { /* a Map gives [key, value] entries; a Set its values */
            Expr *call = xs_alloc(sizeof *call);
            call->kind = E_MEMBER;
            call->pos = s->expr->pos;
            call->lhs = s->expr;
            call->str = s->expr->type->kind == TY_MAP ? "entries" : "values";
            call->is_call = true;
            call->field = -1;
            s->expr = call;
            check_expr(s->expr, NULL);
        }
        if (s->expr->type->kind != TY_ARRAY && s->expr->type->kind != TY_STREAM)
            xs_error(s->expr->pos, "for...of iterates arrays, streams, Map and Set", "cannot iterate over %s", ty_name(s->expr->type));
        Type *elem = s->expr->type->elem;
        if (s->decl_type) {
            s->decl_type = resolve(s->decl_type, s->pos);
            if (!ty_equal(s->decl_type, elem)) xs_error(s->pos, NULL, "the elements are %s, not %s", ty_name(elem), ty_name(s->decl_type));
        }
        s->type = elem;
        push_scope();
        decl_no_cell = true; /* each iteration has its own copy, as in JavaScript */
        s->cname = declare(s->pos, s->name, elem, s->is_const, false, false)->cname;
        decl_no_cell = false;
        loop_depth++;
        check_block(s->body);
        loop_depth--;
        pop_scope();
        if (s->expr->kind == E_MEMBER && s->expr->str && strcmp(s->expr->str, "entries") == 0 && s->expr->lhs->type->kind == TY_MAP &&
            strncmp(s->name, "__d", 3) == 0 && !getenv("XISTO_NO_MAP_DIRECT"))
            s->map_direct = md_stmt(s->body); /* (the tuple of a [k, v] destructuring never escapes: only its fields are read) */
        return;
    }
    case S_BREAK:
        if (!loop_depth && !switch_depth) {
            xs_error(s->pos, NULL, "'break' outside of a loop or switch");
        }
        return;
    case S_CONTINUE:
        if (!loop_depth) {
            xs_error(s->pos, NULL, "'continue' outside of a loop");
        }
        return;
    case S_SWITCH: {
        check_expr(s->expr, NULL);
        settle(s->expr);
        Type *t = s->expr->type;
        if (!is_numeric(t) && t->kind != TY_STRING && t->kind != TY_BOOLEAN && t->kind != TY_ENUM)
            xs_error(s->expr->pos, NULL, "switch works on int, number, string, boolean or enum, but this is %s", ty_name(t));
        for (int i = 0; i < s->ncases; i++) {
            Case *c = &s->cases[i];
            if (c->value) {
                check_expr(c->value, t);
                expect_type(c->value, t, "case value");
            }
        }
        switch_depth++;
        Expr *sx = s->expr;
        Expr *sid = sx->kind == E_MEMBER && sx->builtin.cfn && strcmp(sx->builtin.cfn, "union_field") == 0 ? union_ident(sx->lhs) : NULL;
        Binding *sb = sid ? lookup(sid->str) : NULL;
        for (int i = 0; i < s->ncases; i++) {
            Case *c = &s->cases[i];
            if (sb && sb->type->kind == TY_UNION && c->value && c->value->kind == E_STRING) { /* case "circle": that member */
                Type *u = sb->type;
                bool *keep = xs_alloc(sizeof(bool) * (size_t)u->nparams);
                for (int k = 0; k < u->nparams; k++) {
                    Field *fd = u->params[k]->kind == TY_OBJECT ? find_field(u->params[k]->cls, sx->str, NULL) : NULL;
                    keep[k] = fd && fd->type->tname && strcmp(fd->type->tname, c->value->str) == 0;
                }
                Type *t = union_subset(u, keep);
                if (!t) xs_error(c->value->pos, NULL, "no member of %s has %s \"%s\"", ty_name(u), sx->str, c->value->str);
                check_block_union(c->body, sb, t);
            } else check_block(c->body);
        }
        switch_depth--;
        return;
    }
    case S_YIELD: {
        if (!cur_func || !cur_func->is_gen)
            xs_error(s->pos, "make it a generator: function* name(): Generator<T> { ... }", "'yield' is used inside a generator");
        if (finally_depth) xs_error(s->pos, NULL, "'yield' inside a try with finally is not supported yet");
        Type *elem = cur_func->ret->elem;
        check_expr(s->expr, elem);
        expect_type(s->expr, elem, xs_sprintf("value yielded by '%s'", cur_func->name));
        return;
    }
    case S_RETURN:
        if (!cur_func) xs_error(s->pos, NULL, "'return' outside of a function");
        if (cur_func->is_gen && s->expr) xs_error(s->expr->pos, "yield it, then return;", "a generator returns no value");
        if (s->expr) {
            if (cur_func->ret->kind == TY_VOID)
                xs_error(s->expr->pos, NULL, "'%s' is void and cannot return a value", cur_func->name);
            check_expr(s->expr, cur_func->ret);
            expect_type(s->expr, cur_func->ret, xs_sprintf("return value of '%s'", cur_func->name));
        } else if (cur_func->ret->kind != TY_VOID && !cur_func->is_gen) {
            xs_error(s->pos, NULL, "'%s' must return a value of type %s", cur_func->name, ty_name(cur_func->ret));
        }
        return;
    case S_BLOCK: check_block(s); return;
    }
}

static bool always_exits(Stmt *s) {
    switch (s->kind) {
    case S_RETURN: case S_BREAK: case S_CONTINUE: case S_THROW: return true;
    case S_BLOCK:
        for (int i = 0; i < s->stmts.len; i++)
            if (always_exits(s->stmts.items[i])) return true;
        return false;
    case S_IF: return s->else_body && always_exits(s->body) && always_exits(s->else_body);
    default: return false;
    }
}

static bool always_returns(Stmt *s) {
    switch (s->kind) {
    case S_RETURN: return true;
    case S_BLOCK:
        for (int i = 0; i < s->stmts.len; i++)
            if (always_returns(s->stmts.items[i])) return true;
        return false;
    case S_IF: return s->else_body && always_returns(s->body) && always_returns(s->else_body);
    case S_TRY: /* the finally returns, or the try does and (without a catch, a throw just passes) the catch does too */
        if (s->for_init && always_returns(s->for_init)) return true;
        return always_returns(s->body) && (!s->else_body || always_returns(s->else_body));
    case S_THROW: return true;
    case S_SWITCH: {
        bool has_default = false;
        for (int i = 0; i < s->ncases; i++) {
            if (!s->cases[i].value) has_default = true;
            /* a case that falls through into the next is fine only if the next returns; keep it simple: every non-empty body must return */
            if (s->cases[i].body->stmts.len > 0 && !always_returns(s->cases[i].body)) return false;
        }
        return has_default;
    }
    default: return false;
    }
}

static void resolve_signature(Func *f) {
    for (int i = 0; i < f->nparams; i++) {
        f->params[i].type = resolve(f->params[i].type, f->pos);
        check_type_supported(f->pos, f->params[i].type);
    }
    f->ret = resolve(f->ret, f->pos);
}

/* ---- XB-034: `for (let i = 0; i < a.length; i++)` ----
   A `let` without a type, started with an integer literal, is an int when every assignment to it in its scope
   is an integer by its form: i++, i--, i += 1, i = i + k, i = Math.floor(x), with k itself such
   a variable or a literal. Anything else (i += 0.5, i = f(), i /= 2) leaves it a number, as before. So that no
   program that compiled stops compiling, such an int becomes a number by itself where a number goes (an
   argument, a store, arithmetic with a number): in TypeScript it was a number all along. */
static const char **ii_names;
static int ii_n;
static bool ii_known(const char *n) { for (int i = 0; i < ii_n; i++) if (strcmp(ii_names[i], n) == 0) return true; return false; }
/* names the current function declares with a type as written: `n: int` (ints) and `a: int[]` (arrays of int), so that
   r = n and k = perm[0] are integers by their form too; per function, a name typed in one function says nothing
   about another */
static const char **ii_tnames; static int ii_tn;
static const char **ii_anames; static int ii_an;
static const char **ii_cnames; static Expr **ii_cinit; static int ii_cn; /* const j = i + 1: j is what its value is */
static int ii_depth;
static bool ii_in(const char **l, int n, const char *s) { for (int i = 0; i < n; i++) if (strcmp(l[i], s) == 0) return true; return false; }
static void ii_typed(const char *name, Type *t) {
    if (!name || !t) return;
    if (t->kind == TY_INT) { ii_tnames = realloc(ii_tnames, sizeof(char *) * (size_t)(ii_tn + 1)); ii_tnames[ii_tn++] = name; }
    else if (t->kind == TY_ARRAY && t->elem && t->elem->kind == TY_INT) { ii_anames = realloc(ii_anames, sizeof(char *) * (size_t)(ii_an + 1)); ii_anames[ii_an++] = name; }
}
static void ii_scan(Stmt *s) {
    if (!s) return;
    if ((s->kind == S_VAR || s->kind == S_FOROF) && s->decl_type) ii_typed(s->name, s->decl_type);
    if (s->kind == S_VAR && s->is_const && !s->decl_type && s->init && s->name) {
        ii_cnames = realloc(ii_cnames, sizeof(char *) * (size_t)(ii_cn + 1)); ii_cinit = realloc(ii_cinit, sizeof(Expr *) * (size_t)(ii_cn + 1));
        ii_cnames[ii_cn] = s->name; ii_cinit[ii_cn++] = s->init;
    }
    ii_scan(s->for_init); ii_scan(s->body); ii_scan(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) ii_scan(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) ii_scan(s->cases[i].body);
}
static void ii_enter(Func *f, bool reset) {
    if (reset) ii_tn = ii_an = ii_cn = 0;
    for (int i = 0; i < f->nparams; i++) ii_typed(f->params[i].name, f->params[i].type);
    ii_scan(f->body);
}
static bool int_shaped(Expr *e, const char *self) {
    if (!e) return false;
    switch (e->kind) {
    case E_NUMBER: return e->is_int_lit;
    case E_IDENT:
        if (!e->str) return false;
        if (strcmp(e->str, self) == 0 || ii_known(e->str) || ii_in(ii_tnames, ii_tn, e->str)) return true;
        for (int i = ii_cn - 1; i >= 0; i--)
            if (strcmp(ii_cnames[i], e->str) == 0) {
                if (ii_depth > 8) return false;
                ii_depth++; bool r = int_shaped(ii_cinit[i], self); ii_depth--;
                return r;
            }
        return false;
    case E_INDEX: return e->lhs && e->lhs->kind == E_IDENT && e->lhs->str && ii_in(ii_anames, ii_an, e->lhs->str);
    case E_MEMBER: /* (not x.length: it can be a getter of the program's own that gives a number, and the
                      types are not known yet here; i < a.length is a comparison, not an assignment, and is fine) */
        if (!e->is_call) return false;
        if (!(e->lhs && e->lhs->kind == E_IDENT && e->lhs->str && strcmp(e->lhs->str, "Math") == 0 && e->str)) return false;
        if (!strcmp(e->str, "floor") || !strcmp(e->str, "ceil") || !strcmp(e->str, "round") || !strcmp(e->str, "trunc")) return true;
        if (!strcmp(e->str, "max") || !strcmp(e->str, "min") || !strcmp(e->str, "abs")) { /* of integers: an integer */
            if (e->args.len == 0) return false;
            for (int i = 0; i < e->args.len; i++) if (!int_shaped(e->args.items[i], self)) return false;
            return true;
        }
        return false;
    case E_UNARY: return e->op == TK_MINUS && int_shaped(e->lhs, self);
    case E_BINARY:
        switch (e->op) {
        /* (not * or <<: a product grows past 2^63 quickly, an int would stop with an overflow where JavaScript's
           number only loses precision, as in let f = 1; f *= i) */
        case TK_PLUS: case TK_MINUS: case TK_PERCENT:
            return int_shaped(e->lhs, self) && int_shaped(e->rhs, self);
        case TK_AMP: case TK_PIPE: case TK_CARET: case TK_SHR: case TK_USHR: case TK_SHL:
            return true; /* a 32-bit integer whatever the operands (ToInt32): x & 0xff, (a << k) & m */
        default: return false;
        }
    case E_COND: return e->args.len == 2 && int_shaped(e->args.items[0], self) && int_shaped(e->args.items[1], self);
    case E_CALL: { /* f(...) where every function of that name is declared to return int */
        if (!e->lhs || e->lhs->kind != E_IDENT || !e->lhs->str) return false;
        bool any = false, all = true;
        SYM_EACH(funcs_by_name, e->lhs->str, fe) {
            Func *f = fe->decl;
            any = true;
            if (!f->ret || f->ret->kind != TY_INT) all = false;
        }
        return any && all;
    }
    default: return false;
    }
}
static bool ii_stmt_ok(Stmt *s, const char *self);
static bool ii_expr_ok(Expr *e, const char *self) { /* every assignment to self inside e is an integer */
    if (!e) return true;
    bool tgt = e->lhs && e->lhs->kind == E_IDENT && e->lhs->str && strcmp(e->lhs->str, self) == 0;
    if (e->kind == E_ASSIGN && tgt) {
        if (e->op == TK_SLASH_ASSIGN || e->op == TK_STAR_ASSIGN) return false;
        if (!int_shaped(e->rhs, self)) return false;
    }
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn && e->arrow->fn->body) {
        /* a closure can assign what it captures (the variable then lives in a cell): its assignments count too */
        if (!ii_stmt_ok(e->arrow->fn->body, self)) return false;
    }
    if (!ii_expr_ok(e->lhs, self) || !ii_expr_ok(e->rhs, self)) return false;
    for (int i = 0; i < e->args.len; i++) if (!ii_expr_ok(e->args.items[i], self)) return false;
    return true;
}
static bool ii_stmt_ok(Stmt *s, const char *self) {
    if (!s) return true;
    if (!ii_expr_ok(s->init, self) || !ii_expr_ok(s->expr, self) || !ii_expr_ok(s->cond, self) || !ii_expr_ok(s->for_update, self)) return false;
    if (!ii_stmt_ok(s->for_init, self) || !ii_stmt_ok(s->body, self) || !ii_stmt_ok(s->else_body, self)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!ii_stmt_ok(s->stmts.items[i], self)) return false;
    for (int i = 0; i < s->ncases; i++) if (!ii_expr_ok(s->cases[i].value, self) || !ii_stmt_ok(s->cases[i].body, self)) return false;
    return true;
}
static bool ii_candidate(Stmt *s) {
    if (!s || s->kind != S_VAR || s->is_const || s->decl_type || !s->init || !s->name) return false;
    Expr *v = s->init;
    if (v->kind == E_UNARY && v->op == TK_MINUS) v = v->lhs;
    return v && v->kind == E_NUMBER && v->is_int_lit;
}
static void ii_mark(Stmt *s, bool ok) {
    s->infer_int = ok;
    if (ok) { ii_names = realloc(ii_names, sizeof(char *) * (size_t)(ii_n + 1)); ii_names[ii_n++] = s->name; }
}
static void infer_ints(Stmt *s);
static void infer_ints_list(StmtList *l) {
    for (int i = 0; i < l->len; i++) {
        Stmt *v = l->items[i];
        if (ii_candidate(v)) {
            bool ok = true;
            for (int k = i + 1; k < l->len && ok; k++) ok = ii_stmt_ok(l->items[k], v->name);
            ii_mark(v, ok);
        }
        infer_ints(v);
    }
}
static void infer_ints(Stmt *s) {
    if (!s) return;
    if (s->kind == S_FOR && ii_candidate(s->for_init)) /* for (let i = 0; ...): the loop is its scope */
        ii_mark(s->for_init, ii_stmt_ok(s->body, s->for_init->name) && ii_expr_ok(s->cond, s->for_init->name) && ii_expr_ok(s->for_update, s->for_init->name));
    if (s->kind == S_BLOCK) { infer_ints_list(&s->stmts); return; }
    infer_ints(s->for_init);
    infer_ints(s->body);
    infer_ints(s->else_body);
    for (int i = 0; i < s->ncases; i++) infer_ints(s->cases[i].body);
}
/* A function's candidates, decided together and optimistically (XB-050): each is assumed an int, and the
   ones with an assignment that is not an integer by its form (with the others assumed) are taken out, until
   none is. So `let max = 1; ... let f = 1; f++; if (f > max) max = f` makes both ints, whatever the order in which
   they are declared and however deep. */
typedef struct { Stmt *v; StmtList *list; int idx; Stmt *loop; bool on; } IICand;
static IICand *iic; static int iicn, iicap;
static void ii_collect(Stmt *s) {
    if (!s) return;
    if (s->kind == S_FOR && ii_candidate(s->for_init)) {
        if (iicn == iicap) { iicap = iicap ? iicap * 2 : 32; iic = realloc(iic, sizeof(IICand) * (size_t)iicap); }
        iic[iicn++] = (IICand){s->for_init, NULL, 0, s, true};
    }
    if (s->kind == S_BLOCK)
        for (int i = 0; i < s->stmts.len; i++)
            if (ii_candidate(s->stmts.items[i])) {
                if (iicn == iicap) { iicap = iicap ? iicap * 2 : 32; iic = realloc(iic, sizeof(IICand) * (size_t)iicap); }
                iic[iicn++] = (IICand){s->stmts.items[i], &s->stmts, i, NULL, true};
            }
    if (s->kind != S_FOR) ii_collect(s->for_init); /* (a for's init is its candidate, above) */
    ii_collect(s->body);
    ii_collect(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) ii_collect(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) ii_collect(s->cases[i].body);
}
static void infer_ints_fn(Stmt *body) {
    if (getenv("XISTO_NO_INFER_FIX")) { infer_ints(body); return; } /* (measuring knob: the one-by-one inference) */
    iicn = 0;
    ii_collect(body);
    int base = ii_n;
    for (bool changed = true; changed;) {
        changed = false;
        ii_n = base;
        for (int c = 0; c < iicn; c++) if (iic[c].on) ii_mark(iic[c].v, true); /* the names assumed ints */
        for (int c = 0; c < iicn; c++) {
            if (!iic[c].on) continue;
            IICand *k = &iic[c];
            bool ok = true;
            if (k->loop) ok = ii_stmt_ok(k->loop->body, k->v->name) && ii_expr_ok(k->loop->cond, k->v->name) && ii_expr_ok(k->loop->for_update, k->v->name);
            else for (int j = k->idx + 1; j < k->list->len && ok; j++) ok = ii_stmt_ok(k->list->items[j], k->v->name);
            if (!ok) { k->on = false; changed = true; }
        }
    }
    ii_n = base;
    for (int c = 0; c < iicn; c++) ii_mark(iic[c].v, iic[c].on);
}

/* top-level `let`s are globals: functions may assign them too */
static void infer_ints_top(Program *p) {
    ii_tn = ii_an = ii_cn = 0; /* the top level's typed names and consts (const lim = Math.max(1, n)), as in a function */
    for (int i = 0; i < p->top.len; i++) ii_scan(p->top.items[i]);
    for (int i = 0; i < p->top.len; i++) {
        Stmt *v = p->top.items[i];
        if (ii_candidate(v)) {
            bool ok = true;
            for (int k = i + 1; k < p->top.len && ok; k++) ok = ii_stmt_ok(p->top.items[k], v->name);
            for (int f = 0; f < p->nfuncs && ok; f++) ok = ii_stmt_ok(p->funcs[f]->body, v->name);
            for (int c = 0; c < p->nclasses && ok; c++) {
                Class *cl = p->classes[c];
                for (int m = 0; m < cl->nmethods && ok; m++) ok = ii_stmt_ok(cl->methods[m]->body, v->name);
                if (cl->ctor) ok = ok && ii_stmt_ok(cl->ctor->body, v->name);
            }
            ii_mark(v, ok);
        }
        infer_ints(v);
    }
}
/* made only of inferred ints and integer literals (i, i % 7, -i, i + 1, c ? i : 0): what TypeScript has as a number */
static bool inferred_only(Expr *e) {
    if (!e) return false;
    switch (e->kind) {
    case E_IDENT: {
        if (!e->str) return false;
        for (Binding *o = lookup(e->str); o; o = o->origin) if (o->inferred_int) return true;
        return false;
    }
    case E_NUMBER: return e->is_int_lit;
    case E_UNARY: return e->op == TK_MINUS && inferred_only(e->lhs);
    case E_BINARY:
        if (e->op == TK_EQ || e->op == TK_NEQ || e->op == TK_LT || e->op == TK_LE || e->op == TK_GT || e->op == TK_GE || e->op == TK_AND || e->op == TK_OR) return false;
        return inferred_only(e->lhs) && inferred_only(e->rhs) && (e->lhs->kind != E_NUMBER || e->rhs->kind != E_NUMBER);
    case E_COND: return e->args.len == 2 && inferred_only(e->args.items[0]) && inferred_only(e->args.items[1]);
    default: return false;
    }
}
/* an inferred int (or an expression of them) read where a number goes: Number(e) */
static bool widen_inferred(Expr *e) {
    if (!e || !e->type || e->type->kind != TY_INT || !inferred_only(e)) return false;
    Expr *a = xs_alloc(sizeof *a);
    *a = *e;
    memset(e, 0, sizeof *e);
    e->kind = E_CALL; e->pos = a->pos; e->field = -1; e->type = ty_number;
    e->builtin.cfn = "xs_to_number";
    expr_list_push(&e->args, a);
    Expr *callee = xs_alloc(sizeof *callee);
    callee->kind = E_IDENT; callee->pos = a->pos; callee->str = "Number"; callee->field = -1;
    e->lhs = callee;
    return true;
}

static void check_func(Func *f) {
    if (f->is_native) return; /* `declare`: the package's C */
    if (f->body) { ii_enter(f, true); infer_ints_fn(f->body); } /* XB-034 */
    enter_module(f->cls ? f->cls->mod : f->mod);
    /* C names of locals are numbered per function: an edit in one function leaves every other function's C
       text as it was (the --dev units cached by their text survive it) */
    int saved_tmp = tmp_counter;
    tmp_counter = 0;
    cur_func = f;
    cur_class = f->cls;
    nsev = 0; /* which variables closures share (cells) */
    for (int i = 0; i < f->nparams; i++) scan_ev(f->params[i].name, 0, 'd');
    scan_stmt(f->body, 0);
    push_scope();
    for (int i = 0; i < f->nparams; i++) {
        for (int j = 0; j < i; j++)
            if (strcmp(f->params[i].name, f->params[j].name) == 0)
                xs_error(f->pos, NULL, "parameter '%s' is declared twice", f->params[i].name);
        Binding *pb = declare(f->pos, f->params[i].name, f->params[i].type, false, false, true);
        f->params[i].cname = pb->plain ? pb->plain : pb->cname;
        f->params[i].cell = pb->cell;
    }
    if (f->is_gen) {
        if (f->ret->kind != TY_STREAM) xs_error(f->pos, "write the return type: function* f(): Generator<T>", "a generator returns Generator<T> (or Iterable<T>)");
        prog->uses_gen = true; /* its body runs on a coroutine stack */
    }
    check_block(f->body);
    pop_scope();
    if (f->ret->kind != TY_VOID && !f->is_gen && !always_returns(f->body))
        xs_error(f->pos, "add a return at the end of the function", "'%s' may reach its end without returning a %s", f->name, ty_name(f->ret));
    tmp_counter = saved_tmp;
    cur_func = NULL;
    cur_class = NULL;
}

/* Every field without an initializer must be assigned (this.f = ...) directly in the constructor body. */
/* the base's fields first (the same layout prefix), then the class's own; interfaces take their bases' fields */
static void flatten_fields(Class *c, int depth) {
    if (c->flattened) return;
    if (depth > 64) xs_error(c->pos, NULL, "'%s' extends itself", c->name);
    c->flattened = true;
    Class **bases = NULL;
    int nb = 0;
    if (c->base) { bases = &c->base; nb = 1; }
    Class **ib = NULL;
    if (c->niface_bases) {
        enter_module(c->mod);
        ib = xs_alloc(sizeof(Class *) * (size_t)c->niface_bases);
        for (int i = 0; i < c->niface_bases; i++) {
            Class *b = find_class(c->iface_bases[i]);
            if (!b || !b->is_interface) xs_error(c->pos, NULL, "'%s' is not an interface %s can extend", c->iface_bases[i], c->name);
            ib[i] = b;
        }
        bases = ib;
        nb = c->niface_bases;
    }
    if (!nb) return;
    int total = 0;
    for (int i = 0; i < nb; i++) {
        c->flattened = false; /* a cycle comes back here */
        if (bases[i] == c || is_subclass(bases[i], c)) xs_error(c->pos, NULL, "'%s' extends itself", c->name);
        flatten_fields(bases[i], depth + 1);
        c->flattened = true;
        total += bases[i]->nfields;
    }
    Field *all = xs_alloc(sizeof(Field) * (size_t)(total + c->nfields + 1));
    int n = 0;
    for (int i = 0; i < nb; i++)
        for (int k = 0; k < bases[i]->nfields; k++) {
            for (int j = 0; j < n; j++)
                if (strcmp(all[j].name, bases[i]->fields[k].name) == 0) xs_error(c->pos, NULL, "field '%s' comes from two interfaces", all[j].name);
            all[n++] = bases[i]->fields[k];
        }
    int inherited = n;
    for (int k = 0; k < c->nfields; k++) {
        for (int j = 0; j < inherited; j++)
            if (strcmp(all[j].name, c->fields[k].name) == 0)
                xs_error(c->fields[k].pos, "a subclass keeps the base's fields: assign it in the constructor instead", "field '%s' is already declared in %s", c->fields[k].name, nb == 1 ? bases[0]->name : "a base");
        all[n++] = c->fields[k];
    }
    c->fields = all;
    c->nfields = c->cap_fields = n;
    c->nbase_fields = c->base ? inherited : 0; /* an interface owns every field it lists */
}

/* the base's methods the class does not override join its list (still owned by the base); an override keeps the
   signature; a concrete class has no abstract method left */
static void flatten_methods(Class *c) {
    if (!c->base || c->nbase_fields < 0) return;
    if (c->base->base && !c->base->methods_flat) flatten_methods(c->base);
    if (c->methods_flat) return;
    c->methods_flat = true;
    Class *b = c->base;
    for (int i = 0; i < b->nmethods; i++) {
        Func *bm = b->methods[i];
        Func *own = NULL;
        for (int k = 0; k < c->nmethods; k++) if (c->methods[k]->cls == c && strcmp(c->methods[k]->name, bm->name) == 0) own = c->methods[k];
        if (!own) {
            c->methods = realloc(c->methods, sizeof(Func *) * (size_t)(c->nmethods + 1));
            c->methods[c->nmethods++] = bm;
            continue;
        }
        bool same = own->nparams == bm->nparams && ty_equal(own->ret, bm->ret);
        for (int k = 0; same && k < own->nparams; k++) same = ty_equal(own->params[k].type, bm->params[k].type);
        if (!same) {
            Buf sig = {0};
            buf_puts(&sig, "(");
            for (int k = 0; k < bm->nparams; k++) buf_printf(&sig, "%s%s: %s", k ? ", " : "", bm->params[k].name, ty_name(bm->params[k].type));
            buf_printf(&sig, "): %s", ty_name(bm->ret));
            xs_error(own->pos, xs_sprintf("keep the signature of %s.%s%s", b->name, bm->name, sig.data), "'%s' overrides %s.%s with another signature", own->name, b->name, bm->name);
        }
        if (find_field(b, own->name, NULL)) xs_error(own->pos, NULL, "'%s' is a field of %s", own->name, b->name);
        /* every method it overrides, at every level up, learns about it */
        for (Class *up = b; up; up = up->base) {
            Func *m = NULL;
            for (int k = 0; k < up->nmethods; k++) if (up->methods[k]->cls == up && strcmp(up->methods[k]->name, own->name) == 0) m = up->methods[k];
            if (!m) continue;
            m->overrides = realloc(m->overrides, sizeof(Func *) * (size_t)(m->noverrides + 1));
            m->overrides[m->noverrides++] = own;
        }
    }
    if (!c->is_abstract)
        for (int k = 0; k < c->nmethods; k++)
            if (c->methods[k]->is_abstract)
                xs_error(c->pos, xs_sprintf("implement %s(...) in %s, or declare %s abstract", c->methods[k]->name, c->name, c->name),
                         "%s does not implement the abstract method %s.%s", c->name, c->methods[k]->cls->name, c->methods[k]->name);
}

static void check_field_init(Class *c) {
    if (c->is_interface) return; /* object literals fill interfaces */
    for (int i = c->nbase_fields; i < c->nfields; i++) {
        Field *fd = &c->fields[i];
        if (fd->init || fd->optional) continue;
        bool assigned = false;
        if (c->ctor) {
            for (int k = 0; k < c->ctor->body->stmts.len; k++) {
                Stmt *s = c->ctor->body->stmts.items[k];
                if (s->kind != S_EXPR || s->expr->kind != E_ASSIGN || s->expr->op != TK_ASSIGN) continue;
                Expr *t = s->expr->lhs;
                if (t->kind == E_MEMBER && !t->is_call && t->lhs->kind == E_THIS && strcmp(t->str, fd->name) == 0) assigned = true;
            }
        }
        if (!assigned && !eff_ctor(c)) { c->data_only = true; continue; } /* a DTO: JSON fills it; `new` is refused */
        if (!assigned)
            xs_error(fd->pos, "give it an initializer, or assign this.%s in the constructor", "field '%s' of %s is never initialized", fd->name, c->name);
    }
}

/* Each declaration is checked under a recovery point: an error is reported, the checker state is put
   back, and checking goes on with the next declaration, so one run reports every independent error. */
#define RECOVER_BEGIN { \
    jmp_buf rb_; Scope *sc_ = scope; Func *cf_ = cur_func; Class *cc_ = cur_class; Module *cm_ = cur_mod; \
    int ld_ = loop_depth, sd_ = switch_depth, fd_ = finally_depth; failed_ = false; \
    xs_error_recover = &rb_; \
    if (_setjmp(rb_) == 0) {
#define RECOVER_END } else { \
        failed_ = true; scope = sc_; cur_func = cf_; cur_class = cc_; loop_depth = ld_; switch_depth = sd_; finally_depth = fd_; \
        if (cm_) enter_module(cm_); \
    } \
    xs_error_recover = NULL; }

static bool failed_;

void check_program(Program *p) {
    prog = p;
    init_builtins();
    for (int i = 0; i < p->nfuncs; i++) sym_add(&funcs_by_name, p->funcs[i]->name, p->funcs[i]);
    for (int i = 0; i < p->nclasses; i++) sym_add(&classes_by_name, p->classes[i]->name, p->classes[i]);
    for (int i = 0; i < p->nenums; i++) sym_add(&enums_by_name, p->enums[i]->name, p->enums[i]);
    mod_scopes = xs_alloc(sizeof(Scope *) * (size_t)(p->nmodules ? p->nmodules : 1));
    for (int i = 0; i < p->nmodules; i++) mod_scopes[i] = xs_alloc(sizeof(Scope));

    for (int i = 0; i < p->nenums; i++) {
        Enum *en = p->enums[i];
        enter_module(en->mod);
        for (int j = 0; j < i; j++)
            if (p->enums[j]->mod == en->mod && strcmp(en->name, p->enums[j]->name) == 0) xs_error(en->pos, NULL, "enum '%s' is declared twice", en->name);
        for (int j = 0; j < p->nclasses; j++)
            if (p->classes[j]->mod == en->mod && strcmp(p->classes[j]->name, en->name) == 0) xs_error(en->pos, NULL, "'%s' is both an enum and a class", en->name);
    }
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        enter_module(c->mod);
        for (int j = 0; j < i; j++)
            if (p->classes[j]->mod == c->mod && strcmp(c->name, p->classes[j]->name) == 0) xs_error(c->pos, NULL, "class '%s' is declared twice", c->name);
        for (int j = 0; j < nbuiltins; j++)
            if (strcmp(c->name, builtins[j]->name) == 0 && !((strcmp(c->name, "Worker") == 0 || strcmp(c->name, "MessagePort") == 0) && !wt_imports(c->mod, c->name)))
                xs_error(c->pos, NULL, "'%s' is a built-in class", c->name);
        SYM_EACH(funcs_by_name, c->name, e)
            if (((Func *)e->decl)->mod == c->mod) xs_error(c->pos, NULL, "'%s' is both a class and a function", c->name);
        if (!c->builtin) c->cname = xs_sprintf("c%d_%s", c->mod->id, c->name); /* declared (native) classes keep their C name */
    }
    for (int i = 0; i < p->nfuncs; i++) {
        Func *f = p->funcs[i];
        if (f->mod->builtin) continue;
        enter_module(f->mod);
        SYM_EACH(funcs_by_name, f->name, e)
            if (e->decl != f && ((Func *)e->decl)->mod == f->mod) xs_error(f->pos, NULL, "function '%s' is declared twice", f->name);
        if (find_class(f->name) || find_enum(f->name)) xs_error(f->pos, NULL, "'%s' is built in and cannot be redeclared", f->name); /* a function may shadow lines, fetch... */
        if (!f->is_native) f->cname = xs_sprintf("f%d_%s", f->mod->id, f->name);
    }
    /* imports must name exported functions, classes or variables of the source module */
    for (int mi = 0; mi < p->nmodules; mi++) {
        Module *m = p->modules[mi];
        for (int i = 0; i < m->nimports; i++) {
            Import *im = &m->imports[i];
            for (int k = 0; k < im->n; k++) {
                const char *name = im->names[k];
                bool found = false;
                SYM_EACH(funcs_by_name, name, e) if (((Func *)e->decl)->mod == im->from) found = found || ((Func *)e->decl)->exported;
                SYM_EACH(classes_by_name, name, e) if (((Class *)e->decl)->mod == im->from) found = found || ((Class *)e->decl)->exported;
                SYM_EACH(enums_by_name, name, e) if (((Enum *)e->decl)->mod == im->from) found = found || ((Enum *)e->decl)->exported;
                if (!found && strcmp(im->from->file, "xisto:worker_threads") == 0 && (strcmp(name, "Worker") == 0 || strcmp(name, "MessagePort") == 0))
                    found = true; /* built-in classes */
                if (!found && im->from->builtin && strcmp(im->from->file, "sqlite") == 0)
                    for (int j = 0; j < nbuiltins; j++)
                        if (strcmp(builtins[j]->name, name) == 0 && (strcmp(name, "DatabaseSync") == 0 || strcmp(name, "StatementSync") == 0)) found = true;
                if (found && im->is_default) { /* import x from "m": x must be m's default export */
                    bool ok = false;
                    SYM_EACH(funcs_by_name, name, e) if (((Func *)e->decl)->mod == im->from && ((Func *)e->decl)->is_default) ok = true;
                    if (!ok) { enter_module(m); xs_error(im->positions[k], "import it by name: import { x } from \"m\"", "'%s' is not the default export of '%s'", name, im->path); }
                }
                for (int j = 0; j < p->top.len && !found; j++) {
                    Stmt *st = p->top.items[j];
                    if (st->kind == S_VAR && st->mod == im->from && strcmp(st->name, name) == 0) found = st->exported;
                }
                if (!found) {
                    enter_module(m);
                    xs_error(im->positions[k], xs_sprintf("add 'export' in front of its declaration in %s", im->from->file), "'%s' is not exported by '%s'", name, im->path);
                }
            }
        }
    }
    for (int i = 0; i < p->nfuncs; i++) {
        if (p->funcs[i]->mod->builtin) continue;
        enter_module(p->funcs[i]->mod);
        if (!p->funcs[i]->ntparams) resolve_signature(p->funcs[i]); /* a template resolves per instance */
    }
    /* inheritance: bases first, so a class sees its base's fields (and later its methods) */
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        enter_module(c->mod);
        if (c->base_name) {
            Class *b = find_class(c->base_name);
            if (!b) {
                int n; const char **cands = class_names(&n);
                xs_error(c->base_pos, xs_help_suggest(c->base_name, cands, n, NULL), "unknown class '%s'", c->base_name);
            }
            if (b->is_interface) xs_error(c->base_pos, xs_sprintf("class %s implements %s", c->name, b->name), "'%s' is an interface; a class extends classes", b->name);
            if (b->builtin) xs_error(c->base_pos, "wrap it in a field instead", "extending the built-in class %s is not supported", b->name);
            c->base = b;
            b->subs = realloc(b->subs, sizeof(Class *) * (size_t)(b->nsubs + 1));
            b->subs[b->nsubs++] = c;
        }
    }
    for (int i = 0; i < p->nclasses; i++) flatten_fields(p->classes[i], 0);
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (c->ntparams || c->generic_of) continue; /* templates resolve per instance; instances already did */
        /* a DTO: no constructor and fields without a value, so JSON fills it and `new` is refused */
        if (!eff_ctor(c) && !c->is_interface && !c->builtin)
            for (int k = 0; k < c->nfields; k++) if (!c->fields[k].init && !c->fields[k].optional) c->data_only = true;
        enter_module(c->mod);
        for (int k = 0; k < c->nfields; k++) {
            Field *fd = &c->fields[k];
            for (int j = 0; j < k; j++)
                if (strcmp(fd->name, c->fields[j].name) == 0) xs_error(fd->pos, NULL, "field '%s' is declared twice", fd->name);
            fd->type = resolve(fd->type, fd->pos);
            check_type_supported(fd->pos, fd->type);
            fd->cname = xs_sprintf("f_%s", fd->name);
        }
        for (int k = 0; k < c->nmethods; k++) {
            Func *m = c->methods[k];
            for (int j = 0; j < k; j++)
                if (strcmp(m->name, c->methods[j]->name) == 0) xs_error(m->pos, NULL, "method '%s' is declared twice", m->name);
            if (find_field(c, m->name, NULL)) xs_error(m->pos, NULL, "'%s' is both a field and a method of %s", m->name, c->name);
            if (!m->is_native) m->cname = xs_sprintf("m%d_%s_%s", c->mod->id, c->name, m->name);
            if (!m->ntparams) resolve_signature(m);
        }
        if (c->ctor) {
            if (!c->ctor->is_native) c->ctor->cname = xs_sprintf("c%d_%s_new", c->mod->id, c->name);
            resolve_signature(c->ctor);
        }
    }
    for (int i = 0; i < p->nclasses; i++) { enter_module(p->classes[i]->mod); flatten_methods(p->classes[i]); }

    /* Top-level statements, module by module, dependencies first. Imported variables become
       read-only aliases in the importing module's scope. */
    for (int mi = 0; mi < p->nmodules; mi++) {
        Module *m = p->modules[mi];
        enter_module(m);
        for (int i = 0; i < m->nimports; i++) {
            Import *im = &m->imports[i];
            for (int k = 0; k < im->n; k++) {
                Scope *src = mod_scopes[im->from->id];
                size_t nl = strlen(im->names[k]);
                for (Binding *b = src->head; b; b = b->next) {
                    bool is_static = strncmp(b->name, im->names[k], nl) == 0 && strncmp(b->name + nl, "__s_", 4) == 0; /* Class.static */
                    if (strcmp(b->name, im->names[k]) != 0 && !is_static) continue;
                    Binding *alias = xs_alloc(sizeof *alias);
                    *alias = *b;
                    alias->origin = b;
                    alias->is_const = true;
                    alias->next = scope->head;
                    scope->head = alias;
                }
            }
        }
        nsev = 0; /* the module's top-level code: which of its block variables closures share */
        for (int i = 0; i < p->top.len; i++) if (p->top.items[i]->mod == m) scan_stmt(p->top.items[i], 0);
        static bool ii_top_done;
        if (!ii_top_done) { ii_top_done = true; infer_ints_top(p); } /* XB-034, once: globals may be assigned from any module */
        for (int i = 0; i < p->top.len; i++) {
            Stmt *s = p->top.items[i];
            if (s->mod != m) continue;
            checking_generated = s->generated;
            RECOVER_BEGIN
                if (s->kind == S_VAR) check_var(s, true);
                else check_stmt(s);
            RECOVER_END
            checking_generated = false;
            /* a global whose declaration failed still exists, so later uses do not cascade */
            if (failed_) declare_poisoned(s, true);
        }
    }
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        enter_module(c->mod);
        for (int k = c->nbase_fields; k < c->nfields; k++) { /* the inherited ones were checked in their class */
            Field *fd = &c->fields[k];
            if (!fd->init) continue;
            cur_class = c;
            RECOVER_BEGIN
                check_expr(fd->init, fd->type);
                expect_type(fd->init, fd->type, xs_sprintf("initializer of field '%s'", fd->name));
            RECOVER_END
        }
        cur_class = NULL;
        if (c->ctor) {
            RECOVER_BEGIN
                super_calls = 0;
                if (c->base) {
                    StmtList *l = &c->ctor->body->stmts;
                    Stmt *s0 = l->len ? l->items[0] : NULL;
                    if (!s0 || s0->kind != S_EXPR || s0->expr->kind != E_CALL || !s0->expr->lhs || s0->expr->lhs->kind != E_IDENT || strcmp(s0->expr->lhs->str, "super") != 0)
                        xs_error(s0 ? s0->pos : c->ctor->pos, xs_sprintf("start the constructor with super(...): %s extends %s", c->name, c->base->name),
                                 "the constructor of %s must call super(...) first", c->name);
                }
                check_func(c->ctor);
                if (c->base && super_calls != 1) xs_error(c->ctor->pos, NULL, "super(...) is called once, as the first statement of the constructor");
            RECOVER_END
        }
        RECOVER_BEGIN check_field_init(c); RECOVER_END
        for (int k = 0; k < c->nmethods; k++) {
            if (c->methods[k]->cls != c || c->methods[k]->is_abstract || c->methods[k]->ntparams) continue; /* inherited, or a template */
            RECOVER_BEGIN check_func(c->methods[k]); RECOVER_END
        }
    }
    for (int i = 0; i < p->nfuncs; i++)
        if (!p->funcs[i]->mod->builtin && !p->funcs[i]->ntparams) { RECOVER_BEGIN check_func(p->funcs[i]); RECOVER_END }
    /* generic instances (an instance can make more); then they join the program like any function or method */
    for (int w = 0; w < ngen_work; w++) { RECOVER_BEGIN check_func(gen_work[w]); RECOVER_END }
    for (int w = 0; w < ngen_work; w++) {
        Func *f = gen_work[w];
        if (f->cls) {
            f->cls->methods = realloc(f->cls->methods, sizeof(Func *) * (size_t)(f->cls->nmethods + 1));
            f->cls->methods[f->cls->nmethods++] = f;
        } else {
            p->funcs = realloc(p->funcs, sizeof(Func *) * (size_t)(p->nfuncs + 1));
            p->funcs[p->nfuncs++] = f;
            p->cap_funcs = p->nfuncs;
        }
    }
    xs_error_recover = NULL;
    if (xs_error_count) {
        if (!xs_json_errors) fprintf(stderr, "xisto: %d error%s\n", xs_error_count, xs_error_count == 1 ? "" : "s");
        exit(1);
    }
}
