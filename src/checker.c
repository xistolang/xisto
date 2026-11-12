#include "xisto.h"
#define XR_CORE_ONLY 1
#include "../runtime/xisto_regex.h" /* patterns of /literals/ are compiled here once, to report their errors */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
/* xisto check: names imported from a module that could not be loaded; a use of one was already reported (the
   import), so it stops checking that statement quietly instead of reporting an unknown name */
static const char **missing_names;
static int nmissing_names;
extern const char **xs_failed_decls;
extern int xs_nfailed_decls;
static bool missing_name(const char *name) {
    if (!xs_check_mode) return false;
    if (strncmp(name, "__", 2) == 0) return true; /* a name the compiler made (a decorator's value...): its cause was reported */
    for (int i = 0; i < nmissing_names; i++) if (strcmp(missing_names[i], name) == 0) return true;
    for (int i = 0; i < xs_nfailed_decls; i++) if (strcmp(xs_failed_decls[i], name) == 0) return true;
    return false;
}

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
    struct Func *arrow_fn; /* const p = (n: number, w = 2) => ...: the arrow, so p(7) fills w with its literal default */
    Stmt *auto_var;  /* let x; (no type, no value): typed by the first assignment checked (type is ty_auto until then) */
    bool lit_number; /* `let s = 0` (or 0.5, or any number) with no type written: s += i takes an int i as Number(i), as
                        JavaScript adds it (XB-094); the type the dev never wrote does not ask for a conversion */
    struct Binding *next;
} Binding;

static Type t_auto_ = {.kind = TY_UNKNOWN};
static Type *ty_auto = &t_auto_; /* the type of a `let x;` until its first assignment (read as an unknown before it) */

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

/* a field's C name: f_<name>, with what C does not allow in a name spelled out ('@timestamp' is f__x40timestamp) */
static char *field_cname(const char *name) {
    bool plain = true;
    for (const char *q = name; *q; q++) if (!isalnum((unsigned char)*q) && *q != '_' && *q != '$') plain = false;
    if (plain) return xs_sprintf("f_%s", name);
    Buf b = {0};
    buf_puts(&b, "f_");
    for (const char *q = name; *q; q++) {
        if (isalnum((unsigned char)*q) || *q == '_') buf_putc(&b, *q);
        else buf_printf(&b, "_x%02x", (unsigned char)*q);
    }
    return b.data;
}
static void enter_module(Module *m) {
    cur_mod = m;
    scope = mod_scopes[m->id];
    xs_file_name = m->file;
    xs_source = m->src;
}

/* True when module `via` gives `name` declared in `from`: it is `from`, or it re-exports the name from a module that
   gives it (export { name } from "m", export * from "m"; a barrel of barrels too) */
static bool gives_name(Module *via, Module *from, const char *name, int depth) {
    if (!via || depth > 32) return false;
    if (via == from) return true;
    for (int i = 0; i < via->nimports; i++) {
        Import *im = &via->imports[i];
        if (!im->reexport || !im->from) continue;
        bool listed = im->star;
        for (int k = 0; k < im->n && !listed; k++) listed = strcmp(im->names[k], name) == 0;
        if (listed && gives_name(im->from, from, name, depth + 1)) return true;
    }
    return false;
}

/* True when `name` is imported by the current module from `from` (directly, or through a module that re-exports it). */
static bool imports_name(Module *m, Module *from, const char *name) {
    if (!m) return false;
    const char *q = strchr(name, '\x01'); /* "A\x01m": the A that the import from "m" gives (an alias, XB-117) */
    size_t nl = q ? (size_t)(q - name) : strlen(name);
    for (int i = 0; i < m->nimports; i++)
        for (int k = 0; k < m->imports[i].n; k++) {
            Import *im = &m->imports[i];
            if (strlen(im->names[k]) != nl || strncmp(im->names[k], name, nl) != 0) continue;
            if (q ? (!im->path || strcmp(im->path, q + 1) != 0) : (im->qualified && im->qualified[k])) continue;
            char *plain = q ? xs_strndup(name, nl) : (char *)name;
            if (gives_name(im->from, from, plain, 0)) return true;
        }
    return false;
}

/* xisto check: the member at pos is written after '?.' (read on as '.', and already counted) */
static bool after_optional_dot(Pos pos) {
    const char *s = xs_source;
    if (!s) return false;
    int line = 1;
    while (*s && line < pos.line) { if (*s == '\n') line++; s++; }
    const char *at = s + (pos.col > 0 ? pos.col - 1 : 0);
    const char *q = at;
    while (q > s && (q[-1] == ' ' || q[-1] == '\t')) q--;
    return q - s >= 2 && q[-1] == '.' && q[-2] == '?';
}

/* ---------- built-in classes ---------- */
static int reexports_name(Program *p, Module *via, const char *name, int depth);
static bool reexports_or_has(Module *m, const char *name); /* (scope search through export * from) */

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
    f.cname = field_cname(name);
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
    bi_field(resp, "headers", ty_map_of(ty_string, ty_string)); /* a Map: get(name) and forEach((v, k) => ...), names in lower case */
    bi_method(resp, "text", "xs_Response_text", ty_string, 0);
    bi_field(resp, "url", ty_string); /* the URL of the last request, after the redirects */
    {
        Class *rs = bi_class("ReadableStream", "xs_ReadableStream");
        bi_method(rs, "cancel", "xs_ReadableStream_cancel", ty_void, 0);
        bi_field(resp, "body", ty_nullable_of(ty_object_of(rs))); /* (always null: fetch reads the body whole) */
    }

    Class *req = bi_class("HttpRequest", "xs_HttpRequest");
    bi_field(req, "method", ty_string);
    bi_field(req, "path", ty_string);
    bi_field(req, "body", ty_string);
    bi_method(req, "query", "xs_HttpRequest_query", ty_string, 1, ty_string);
    bi_method(req, "param", "xs_HttpRequest_param", ty_string, 1, ty_string);
    bi_method(req, "header", "xs_HttpRequest_header", ty_string, 1, ty_string);
    bi_method(req, "search", "xs_HttpRequest_search", ty_string, 0);
    bi_method(req, "headerNames", "xs_HttpRequest_headerNames", ty_array_of(ty_string), 0);
    bi_method(req, "queryNames", "xs_HttpRequest_queryNames", ty_array_of(ty_string), 0);
    bi_method(req, "queryAll", "xs_HttpRequest_queryAll", ty_array_of(ty_string), 1, ty_string);

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
    bi_field(err, "__thrown", ty_unknown); /* an object thrown with throw: the Error carries it (instanceof, casts) */
    bi_field(err, "cause", ty_unknown); /* new Error(message, { cause }) (null when not given) */

    bi_class("Date", "xs_Date"); /* members: check_date_member */
    Class *sym = bi_class("symbol", "xs_Symbol"); /* Symbol("desc"): equal only to itself (by reference) */
    bi_field(sym, "description", ty_nullable_of(ty_string));
    bi_field(sym, "__id", ty_int); /* (its number, for packages that key a map by it: Nest's injection tokens) */
    Class *bigc = bi_class("bigint", "xs_big"); /* BigInt values: the operators become calls (big_binary) */
    bi_method(bigc, "toString", "xs_big_to_string", ty_string, 0);
    /* bytes (runtime/xisto_rt.h): Uint8Array, and Buffer, the same object printed the Node way; members: check_bytes_member */
    Class *u8 = bi_class("Uint8Array", "xs_Uint8Array");
    bi_field(u8, "length", ty_int);
    Class *buf = bi_class("Buffer", "xs_Uint8Array");
    bi_field(buf, "length", ty_int);
    buf->base = u8;
    bi_method(resp, "arrayBuffer", "xs_Response_arrayBuffer", ty_object_of(u8), 0); /* (the bytes: a Uint8Array) */
    buf->nbase_fields = 1;
    /* shared memory (runtime/xisto_rt.h): SharedArrayBuffer, and Float64Array on one or on memory of its own;
       members: check_f64_member */
    Class *sab = bi_class("SharedArrayBuffer", "xs_SharedArrayBuffer");
    bi_field(sab, "byteLength", ty_int);
    Class *f64 = bi_class("Float64Array", "xs_Float64Array");
    bi_field(f64, "length", ty_int);
    /* fs.fstatSync (runtime/xisto_rt.h, xs_fs_fstat): the size and the kind of what a descriptor is open on */
    Class *stc = bi_class("Stats", "xs_Stats");
    bi_field(stc, "size", ty_int);
    bi_method(stc, "isFile", "xs_Stats_isFile", ty_boolean, 0);
    bi_method(stc, "isDirectory", "xs_Stats_isDirectory", ty_boolean, 0);
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
    bi_method(srv, "fallback", "xs_Server_fallback", ty_void, 1, handler);
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
    const char *q = strchr(name, '\x01'); /* "A\x01m": the class A of the import from "m" (an alias next to another A, XB-117) */
    if (q) {
        char *base = xs_strndup(name, (size_t)(q - name));
        SYM_EACH(classes_by_name, base, e) {
            Class *c = e->decl;
            if (c->exported && imports_name(cur_mod, c->mod, name)) return c;
        }
        return NULL;
    }
    SYM_EACH(classes_by_name, name, e) {
        Class *c = e->decl;
        if (c->mod == cur_mod || (c->exported && imports_name(cur_mod, c->mod, name))) return c;
        if (c->mod && c->mod->file && strcmp(c->mod->file, "xisto:errors") == 0) return c; /* (TypeError & co: global, like Error) */
        if (c->mod && c->mod->file && strcmp(c->mod->file, "xisto:url") == 0 && c->exported) return c; /* (URL & co: web globals) */
        if (c->mod && c->mod->file && strcmp(c->mod->file, "xisto:intl") == 0 && strncmp(name, "Intl_", 5) == 0) return c; /* (Intl.X: global) */
        if (c->mod && c->mod->file && strcmp(c->mod->file, "xisto:reflect") == 0 && strlen(name) > 9 && !strcmp(name + strlen(name) - 9, "Decorator"))
            return c; /* (MethodDecorator & co: global, as in TypeScript) */
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
    if (!a->type) check_expr(a, ty_number); /* (new Date(x) checked x already: a lowered node is not checked twice) */
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
    if (a->type->kind != TY_NUMBER) xs_error(XS8020, a->pos, NULL, "%s must be a number, but this is %s", what, ty_name(a->type));
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
    if (!e->is_call) xs_error(XS7011, e->pos, "Date has methods only: d.getTime(), d.getFullYear(), d.toISOString() ...", "Date has no property '%s'", m);
    for (size_t i = 0; i < sizeof gets / sizeof gets[0]; i++) {
        if (strcmp(m, gets[i].name) != 0) continue;
        if (e->args.len != 0) {
            if (strncmp(m, "toLocale", 8) == 0) xs_error(XS7014, e->pos, "call it without arguments (en-US, as Node by default); Intl is not supported yet", "Date.%s with locales and options is not supported", m);
            xs_error(XS7013, e->pos, NULL, "Date.%s takes no arguments", m);
        }
        const char *cf = strcmp(m, "valueOf") == 0 ? "getTime" : strcmp(m, "toJSON") == 0 ? "toISOString" : strcmp(m, "toGMTString") == 0 ? "toUTCString" : m;
        e->builtin.cfn = xs_sprintf("xs_Date_%s", cf);
        e->type = gets[i].ret == 0 ? ty_int : gets[i].ret == 1 ? ty_number : ty_string;
        return;
    }
    if (strcmp(m, "setTime") == 0) {
        if (e->args.len != 1) xs_error(XS7017, e->pos, NULL, "Date.setTime takes one argument");
        date_num_arg(&e->args.items[0], "the time value");
        e->builtin.cfn = "xs_Date_setTime";
        e->type = ty_number;
        return;
    }
    for (size_t i = 0; i < sizeof sets / sizeof sets[0]; i++) {
        if (strcmp(m, sets[i].name) != 0) continue;
        int n = e->args.len;
        if (n < 1 || n > sets[i].max) xs_error(XS7012, e->pos, NULL, "Date.%s takes 1 to %d arguments, got %d", m, sets[i].max, n);
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
    xs_error(XS7010, e->pos, "supported: getTime, getFullYear/Month/Date/Day/Hours/Minutes/Seconds/Milliseconds (and getUTC...), getTimezoneOffset, toISOString, toJSON, toString, toDateString, toTimeString, toUTCString, toLocaleString, set... and setUTC...", "Date has no method '%s'", m);
}
static bool is_bytes(Type *t) {
    return t && t->kind == TY_OBJECT && t->cls->builtin && (strcmp(t->cls->name, "Uint8Array") == 0 || strcmp(t->cls->name, "Buffer") == 0);
}
/* node:fs on file descriptors: openSync(path, flags?), closeSync(fd), fstatSync(fd) (a Stats: size, isFile(),
   isDirectory()) and readSync(fd, buffer, offset?, length?, position?) -> bytes read, the buffer a Uint8Array
   (a view on a SharedArrayBuffer too: the bytes land in it, no copy); position null or left out reads at the
   current position, a number reads there without moving it (pread), as in Node. Missing arguments become -1
   (offset: 0), so the runtime takes one shape. */
static bool check_fs_fd_call(Expr *e, Func *f) {
    const char *n = f->name;
    bool is_open = strcmp(n, "openSync") == 0, is_close = strcmp(n, "closeSync") == 0, is_stat = strcmp(n, "fstatSync") == 0, is_read = strcmp(n, "readSync") == 0;
    if (!is_open && !is_close && !is_stat && !is_read) return false;
    if (is_open) {
        if (e->args.len < 1 || e->args.len > 2) xs_error(XS7078, e->pos, "openSync(path, \"r\" | \"w\" | \"a\" | \"r+\")", "openSync takes a path and optionally the flags");
        check_expr(e->args.items[0], ty_string);
        expect_type(e->args.items[0], ty_string, "path of openSync");
        if (e->args.len == 2) {
            Expr *fl = e->args.items[1];
            check_expr(fl, ty_string);
            if (fl->kind != E_STRING || (strcmp(fl->str, "r") != 0 && strcmp(fl->str, "w") != 0 && strcmp(fl->str, "a") != 0 && strcmp(fl->str, "r+") != 0))
                xs_error(XS7121, fl->pos, "supported: \"r\", \"w\", \"a\", \"r+\"", "unsupported flags of openSync");
        } else {
            Expr *fl = xs_alloc(sizeof *fl);
            fl->kind = E_STRING; fl->pos = e->pos; fl->str = "r"; fl->type = ty_string; fl->field = -1;
            expr_list_push(&e->args, fl);
        }
        e->func = NULL; e->builtin.cfn = "xs_fs_open"; e->type = ty_int;
        return true;
    }
    if (e->args.len < 1) xs_error(XS8031, e->pos, NULL, "%s takes a file descriptor", n);
    check_expr(e->args.items[0], ty_int);
    expect_type(e->args.items[0], ty_int, xs_sprintf("file descriptor of %s", n));
    if (is_close) {
        if (e->args.len != 1) xs_error(XS7051, e->pos, NULL, "closeSync takes only the file descriptor");
        e->func = NULL; e->builtin.cfn = "xs_fs_close"; e->type = ty_void;
        return true;
    }
    if (is_stat) {
        if (e->args.len != 1) xs_error(XS7057, e->pos, NULL, "fstatSync takes only the file descriptor");
        e->func = NULL; e->builtin.cfn = "xs_fs_fstat"; e->type = ty_object_of(find_class("Stats"));
        return true;
    }
    if (e->args.len < 2 || e->args.len > 5 || e->args.len == 3) /* (Node reads a third argument alone as an options object) */
        xs_error(XS7085, e->pos, "readSync(fd, buffer) or readSync(fd, buffer, offset, length, position?)", "readSync takes 2, 4 or 5 arguments");
    check_expr(e->args.items[1], NULL);
    if (!is_bytes(e->args.items[1]->type)) xs_error(XS7049, e->args.items[1]->pos, NULL, "buffer of readSync must be a Uint8Array or Buffer, but this is %s", ty_name(e->args.items[1]->type));
    for (int i = 2; i < 5; i++) {
        if (i < e->args.len) {
            Expr *a = e->args.items[i];
            if (a->kind == E_NULL) { e->args.items[i] = num_lit(a->pos, -1, true); continue; }
            check_expr(a, ty_int);
            expect_type(a, ty_int, i == 2 ? "offset of readSync" : i == 3 ? "length of readSync" : "position of readSync");
        } else expr_list_push(&e->args, num_lit(e->pos, i == 2 ? 0 : -1, true));
    }
    e->func = NULL; e->builtin.cfn = "xs_fs_read_sync"; e->type = ty_int;
    return true;
}
static bool is_f64(Type *t) { return t && t->kind == TY_OBJECT && t->cls->builtin && strcmp(t->cls->name, "Float64Array") == 0; }
static bool is_sab(Type *t) { return t && t->kind == TY_OBJECT && t->cls->builtin && strcmp(t->cls->name, "SharedArrayBuffer") == 0; }
static Expr *str_lit(Pos pos, const char *s);
static void undef_text(Expr *x);
static bool boolish(Expr *r);
static Expr *null_str(Pos pos) { /* a missing optional string argument: NULL in C */
    Expr *n = xs_alloc(sizeof *n);
    n->kind = E_NULL; n->pos = pos; n->field = -1; n->type = ty_nullable_of(ty_string);
    return n;
}
#define XS_NOT_GIVEN (-9223372036854775807LL - 1) /* INT64_MIN: an index argument left out */
/* a number where a position goes (s.slice(0, n / 2), '0'.repeat(w - n)): truncated, as JavaScript reads it */
static void index_of_number(Expr *a) {
    if (a->type->kind != TY_NUMBER) return;
    Expr *v = xs_alloc(sizeof *v);
    *v = *a;
    memset(a, 0, sizeof *a);
    a->kind = E_CALL; a->pos = v->pos; a->field = -1; a->type = ty_int;
    a->builtin.cfn = "xs_num_to_index";
    expr_list_push(&a->args, v);
}
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
    #define ARGS(lo, hi) do { if (n < (lo) || n > (hi)) xs_error(XS8039, e->pos, NULL, "%s.%s takes %d to %d argument(s), got %d", c->name, m, (lo), (hi), n); } while (0)
    #define PAD_INT(k, v) do { for (int k_ = e->args.len; k_ <= (k); k_++) expr_list_push(&e->args, num_lit(e->pos, (double)(v), true)); } while (0)
    if (!e->is_call) {
        if (strcmp(m, "length") == 0 || strcmp(m, "byteLength") == 0) { e->field = 0; e->type = ty_int; return; }
        if (strcmp(m, "byteOffset") == 0) { e->builtin.cfn = "xs_u8_byte_offset"; e->type = ty_int; return; } /* (into the SharedArrayBuffer or the array it is a view on) */
        xs_error(XS8015, e->pos, "supported: length, byteLength, byteOffset", "%s has no property '%s'", c->name, m);
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
        else xs_error(XS8043, e->args.items[0]->pos, NULL, "%s.set copies from a Uint8Array or an int[], not %s", c->name, ty_name(st));
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
    if (strcmp(m, "lastIndexOf") == 0) { /* from the end (or from a given index) back */
        ARGS(1, 2);
        for (int k = 0; k < n; k++) int_arg(&e->args.items[k], "argument of lastIndexOf");
        PAD_INT(1, XS_NOT_GIVEN);
        if (n < 2) e->args.items[1]->ival = XS_NOT_GIVEN;
        e->builtin.cfn = "xs_u8_last_index_of";
        e->type = ty_int;
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
        if (!is_bytes(e->args.items[0]->type)) xs_error(XS7052, e->args.items[0]->pos, NULL, "equals compares with a Buffer or Uint8Array");
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
    xs_error(XS8014, e->pos, "supported: length, slice, subarray, set, fill, indexOf, lastIndexOf, includes, reverse, join, toString, toBase64, toHex (and Buffer: equals, toString(encoding))", "%s has no method '%s'", c->name, m);
    #undef ARGS
    #undef PAD_INT
}
/* f.length, f.fill(0.5), f.set(src, 4), f.subarray(lo, hi) ... on a Float64Array */
static void check_f64_member(Expr *e, Class *c) {
    const char *m = e->str;
    int n = e->args.len;
    e->field = -1;
    #define ARGS(lo, hi) do { if (n < (lo) || n > (hi)) xs_error(XS7021, e->pos, NULL, "Float64Array.%s takes %d to %d argument(s), got %d", m, (lo), (hi), n); } while (0)
    #define PAD_INT(k, v) do { for (int k_ = e->args.len; k_ <= (k); k_++) expr_list_push(&e->args, num_lit(e->pos, (double)(v), true)); } while (0)
    if (!e->is_call) {
        if (strcmp(m, "length") == 0) { e->field = 0; e->type = ty_int; return; }
        if (strcmp(m, "byteLength") == 0 || strcmp(m, "byteOffset") == 0) { /* (in bytes: 8 per element) */
            e->builtin.cfn = m[4] == 'L' ? "xs_f64_byte_length" : "xs_f64_byte_offset";
            e->type = ty_int;
            return;
        }
        xs_error(XS7019, e->pos, "supported: length, byteLength, byteOffset", "Float64Array has no property '%s'", m);
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
        else xs_error(XS7022, e->args.items[0]->pos, NULL, "Float64Array.set copies from a Float64Array or a number[], not %s", ty_name(st));
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
    xs_error(XS7018, e->pos, "supported: length, byteLength, byteOffset, fill, set, subarray, slice, join, toString", "Float64Array has no method '%s'", m);
    #undef ARGS
    #undef PAD_INT
}
/* Uint8Array.from / of / fromBase64 / fromHex, Buffer.from / alloc / allocUnsafe / concat / byteLength */
static void check_bytes_static(Expr *e, const char *ns) {
    const char *m = e->str;
    bool is_buf = strcmp(ns, "Buffer") == 0;
    Class *u8 = find_class("Uint8Array"), *bc = find_class(is_buf ? "Buffer" : "Uint8Array");
    int n = e->args.len;
    if (!e->is_call) xs_error(XS8037, e->pos, NULL, "%s.%s is not a value", ns, m);
    e->type = ty_object_of(bc);
    if (strcmp(m, "from") == 0) {
        if (n < 1 || n > 2) xs_error(XS8041, e->pos, NULL, "%s.from takes a source%s", ns, is_buf ? " and an optional encoding" : "");
        check_expr(e->args.items[0], e->args.items[0]->kind == E_ARRAY_LIT ? ty_array_of(ty_int) : NULL);
        Type *st = e->args.items[0]->type;
        if (is_buf && st->kind == TY_STRING) {
            if (n == 2) { check_expr(e->args.items[1], ty_string); expect_type(e->args.items[1], ty_string, "the encoding"); }
            else expr_list_push(&e->args, null_str(e->pos));
            e->builtin.cfn = "xs_buf_from_str";
            return;
        }
        if (n != 1) xs_error(XS8042, e->pos, NULL, "%s.from(bytes) takes one argument", ns);
        if (is_bytes(st)) { e->builtin.cfn = is_buf ? "xs_buf_from_bytes" : "xs_u8_copy"; return; }
        if (st->kind == TY_ARRAY && (st->elem->kind == TY_INT || st->elem->kind == TY_LITINT)) { e->builtin.cfn = is_buf ? "xs_buf_from_ints" : "xs_u8_from_ints"; return; }
        if (!is_buf && st->kind == TY_ARRAY && st->elem->kind == TY_NUMBER) { e->builtin.cfn = "xs_u8_from_nums"; return; }
        xs_error(XS8040, e->args.items[0]->pos, NULL, "%s.from takes %s, not %s", ns, is_buf ? "a string, bytes or an int[]" : "bytes, an int[] or a number[]", ty_name(st));
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
        if (n != 1) xs_error(XS7042, e->pos, NULL, "Uint8Array.%s takes one string", m);
        check_expr(e->args.items[0], ty_string);
        expect_type(e->args.items[0], ty_string, xs_sprintf("argument of Uint8Array.%s", m));
        e->builtin.cfn = m[4] == 'B' ? "xs_u8_from_base64" : "xs_u8_from_hex";
        return;
    }
    if (is_buf && (strcmp(m, "alloc") == 0 || strcmp(m, "allocUnsafe") == 0)) {
        if (n < 1 || n > (m[5] == 'U' ? 1 : 2)) xs_error(XS7004, e->pos, NULL, "Buffer.%s takes a size%s", m, m[5] == 'U' ? "" : " and an optional fill byte");
        for (int k = 0; k < n; k++) int_arg(&e->args.items[k], xs_sprintf("argument of Buffer.%s", m));
        if (n == 1) expr_list_push(&e->args, num_lit(e->pos, 0, true));
        e->builtin.cfn = "xs_buf_alloc";
        return;
    }
    if (is_buf && strcmp(m, "concat") == 0) {
        if (n != 1) xs_error(XS7007, e->pos, NULL, "Buffer.concat takes a list of buffers");
        check_expr(e->args.items[0], ty_array_of(ty_object_of(bc)));
        Type *lt = e->args.items[0]->type;
        if (lt->kind != TY_ARRAY || !is_bytes(lt->elem)) xs_error(XS7006, e->args.items[0]->pos, NULL, "Buffer.concat takes Buffer[] or Uint8Array[], not %s", ty_name(lt));
        e->builtin.cfn = "xs_buf_concat";
        return;
    }
    if (is_buf && strcmp(m, "byteLength") == 0) {
        if (n == 2 && e->args.items[1]->kind == E_STRING) { /* Buffer.byteLength(s, enc) */
            const char *enc = e->args.items[1]->str;
            if (!strcmp(enc, "latin1") || !strcmp(enc, "binary") || !strcmp(enc, "ascii")) { /* one byte per UTF-16 unit: s.length */
                Expr *str = e->args.items[0];
                memset(e, 0, sizeof *e);
                e->kind = E_MEMBER; e->pos = str->pos; e->field = -1; e->lhs = str; e->str = "length";
                check_expr(e, NULL);
                return;
            }
            if (!strcmp(enc, "utf8") || !strcmp(enc, "utf-8")) { e->args.len = 1; n = 1; }
        }
        if (n != 1) xs_error(XS7005, e->pos, NULL, "Buffer.byteLength takes one string");
        check_expr(e->args.items[0], ty_string);
        expect_type(e->args.items[0], ty_string, "argument of Buffer.byteLength");
        e->builtin.cfn = "xs_buf_byte_length";
        e->type = ty_int;
        return;
    }
    if (is_buf && strcmp(m, "isBuffer") == 0 && n == 1) { /* Buffer.isBuffer(v): v instanceof Buffer (TK_IDENT: the instanceof operator) */
        Expr *v = e->args.items[0];
        Expr *cls = xs_alloc(sizeof *cls);
        cls->kind = E_IDENT; cls->pos = e->pos; cls->str = "Buffer"; cls->field = -1;
        memset(e, 0, sizeof *e);
        e->kind = E_BINARY; e->op = TK_IDENT; e->pos = v->pos; e->field = -1; e->lhs = v; e->rhs = cls;
        check_expr(e, ty_boolean);
        return;
    }
    (void)u8;
    xs_error(XS7105, e->pos, is_buf ? "supported: Buffer.from, alloc, allocUnsafe, concat, byteLength" : "supported: Uint8Array.from, of, fromBase64, fromHex", "unknown %s member '%s'", ns, m);
}
static Type *tuple_type(Type **ps, int n, Pos pos);
static void check_stmt(Stmt *s);
static void wrap_call(Expr *e, const char *cfn, Type *type);
static void settle(Expr *e);
static int pr_kind(Type *t);
static void pr_resolved(Expr *e);
static void pr_await(Expr *e);
static Expr *pr_awaited;
static void to_unknown(Expr *e);
/* () => x: an element of Promise.all([...]) as a task (an arrow, so the checker works out its captures) */
static Expr *thunk_of(Expr *x) {
    Expr *e = xs_alloc(sizeof *e);
    e->kind = E_ARROW; e->pos = x->pos; e->field = -1;
    Arrow *a = xs_alloc(sizeof *a);
    a->pos = x->pos;
    a->expr_body = true;
    a->synthetic = true;
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
/* a thenable: an object of a class with a then() that takes no argument it must be given (mongoose's Query, which
   runs when awaited). `await q` and an element q of Promise.all([...]) are q.then() */
static Func *find_method(Class *c, const char *name);
static bool thenable(Type *t) {
    if (!t || t->kind != TY_OBJECT || !t->cls || t->cls->builtin || t->cls->is_interface) return false;
    for (Class *c = t->cls; c; c = c->base) {
        Func *f = find_method(c, "then");
        if (!f) continue;
        for (int i = 0; i < f->nparams; i++) if (!f->params[i].dflt && !f->params[i].rest) return false;
        return true;
    }
    return false;
}
/* e (checked) becomes e.<m>(), to be checked */
static void method_call0(Expr *e, const char *m) {
    Expr *in = xs_alloc(sizeof *in);
    *in = *e;
    in->prechecked = true;
    memset(e, 0, sizeof *e);
    e->kind = E_MEMBER; e->pos = in->pos; e->field = -1; e->lhs = in; e->str = (char *)m; e->is_call = true;
}
static void then_call(Expr *e) { method_call0(e, "then"); }
/* String(x) and `${x}` of an object whose class has toString(): string (bson's ObjectId): x.toString() */
static bool tostring_call(Expr *e) {
    Type *t = e->type;
    if (!t || t->kind != TY_OBJECT || !t->cls || t->cls->builtin || t->cls->is_interface) return false;
    for (Class *c = t->cls; c; c = c->base) {
        Func *f = find_method(c, "toString");
        if (!f) continue;
        if (f->nparams > 0 && !f->params[0].dflt) return false;
        method_call0(e, "toString");
        check_expr(e, NULL);
        return e->type && e->type->kind == TY_STRING;
    }
    return false;
}
/* Promise.all([a(), b()]) / Promise.all(xs.map(f)) / Promise.race(...) / Promise.resolve(v). async is syntax in
   xisto (Promise<T> is T), so a call has its value when it returns; here the elements become tasks that run
   concurrently on the event loop (runtime/xisto_net.h, xs_par_run): their I/O waits overlap. */
static int mapnull_ok; /* checking the xs.map(f) of xs.map(f).filter(p): f may give number | null */
static void check_promise(Expr *e, Type *expected) {
    const char *m = e->str;
    if (!e->is_call) xs_error(XS7030, e->pos, NULL, "Promise.%s is not a value", m);
    Type *pexp = expected ? ty_strip_null(expected) : NULL;
    if (strcmp(m, "resolve") == 0) { /* awaited (or where a value goes): the value itself; kept, a fulfilled promise */
        if (e->args.len > 1) xs_error(XS7035, e->pos, NULL, "Promise.resolve takes one value");
        if (e->args.len == 0) { /* Promise.resolve(): a Promise<void> */
            wrap_call(e, "xs_promise_resolved_v", ty_promise_of(ty_void));
            e->args = (ExprList){0};
            prog->uses_gen = true;
            return;
        }
        Expr *v = e->args.items[0];
        bool kept = pr_awaited != e && (!expected || (pexp && pexp->kind == TY_PROMISE));
        check_expr(v, kept && pexp && pexp->kind == TY_PROMISE ? pexp->elem : expected);
        *e = *v;
        if (kept && e->type->kind != TY_PROMISE) {
            if (pexp && pexp->kind == TY_PROMISE) expect_type(e, pexp->elem, "the value of Promise.resolve");
            settle(e);
            pr_resolved(e);
        }
        return;
    }
    if (strcmp(m, "reject") == 0) { /* a rejected promise */
        if (e->args.len != 1) xs_error(XS7035, e->pos, NULL, "Promise.reject takes one Error");
        Type *et = ty_object_of(find_class("Error"));
        check_expr(e->args.items[0], et);
        expect_type(e->args.items[0], et, "the argument of Promise.reject");
        Type *T = pexp && pexp->kind == TY_PROMISE ? pexp->elem : ty_void;
        Expr *x = e->args.items[0];
        e->kind = E_CALL; e->lhs = NULL; e->is_call = false; e->str = NULL;
        e->builtin.cfn = "xs_promise_rejected";
        e->args = (ExprList){0};
        expr_list_push(&e->args, num_lit(e->pos, pr_kind(T), true));
        expr_list_push(&e->args, x);
        e->type = ty_promise_of(T);
        prog->uses_gen = true;
        return;
    }
    bool race = strcmp(m, "race") == 0 || strcmp(m, "any") == 0;
    if (!race && strcmp(m, "all") != 0) xs_error(XS7111, e->pos, "supported: Promise.all, Promise.race, Promise.any, Promise.resolve, Promise.reject", "unknown Promise member '%s'", m);
    if (e->args.len != 1) xs_error(XS7032, e->pos, NULL, "Promise.%s takes an array", m);
    Expr *a = e->args.items[0];
    prog->uses_gen = true; /* the coroutines and the event loop */
    if (a->kind == E_ARRAY_LIT) { /* each element a task */
        if (a->args.len == 0) xs_error(XS7031, a->pos, NULL, "Promise.%s of an empty list", m);
        Type *t = NULL;
        bool mixed = false;
        ExprList thunks = {0};
        Type **rts = xs_alloc(sizeof(Type *) * (size_t)a->args.len);
        for (int i = 0; i < a->args.len; i++) {
            Expr *th = thunk_of(a->args.items[i]);
            check_expr(th, NULL);
            if (thenable(th->arrow->fn->ret)) { /* Promise.all([model.find(f).lean(), ...]): each query run */
                Stmt *r = th->arrow->fn->body->stmts.items[th->arrow->fn->body->stmts.len - 1];
                then_call(r->expr);
                Func *sf = cur_func;
                cur_func = th->arrow->fn;
                check_expr(r->expr, NULL);
                cur_func = sf;
                th->arrow->fn->ret = r->expr->type;
                th->type->ret = r->expr->type;
            }
            if (th->arrow->fn->ret->kind == TY_PROMISE) { /* a promise kept (XB-106): the task waits for it */
                Stmt *r = th->arrow->fn->body->stmts.items[th->arrow->fn->body->stmts.len - 1];
                pr_await(r->expr);
                th->arrow->fn->ret = r->expr->type;
                th->type->ret = r->expr->type;
            }
            Type *rt = th->arrow->fn->ret;
            if (rt->kind == TY_VOID) xs_error(XS8081, a->args.items[i]->pos, NULL, "an element of Promise.%s gives no value", m);
            rts[i] = rt;
            if (!t) t = rt;
            else if (!ty_equal(t, rt)) mixed = true;
            expr_list_push(&thunks, th);
        }
        if (race && mixed) { /* Promise.race([work, timeout]): the first to settle, of either type, as an unknown (narrowed by its uses) */
            for (int i = 0; i < thunks.len; i++) {
                Func *tf = thunks.items[i]->arrow->fn;
                Stmt *r = tf->body->stmts.items[tf->body->stmts.len - 1];
                if (tf->ret->kind != TY_UNKNOWN) to_unknown(r->expr);
                tf->ret = ty_unknown;
                thunks.items[i]->type->ret = ty_unknown;
            }
            t = ty_unknown;
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
    if (a->type->kind != TY_ARRAY) xs_error(XS7033, a->pos, NULL, "Promise.%s takes an array, not %s", m, ty_name(a->type));
    if (a->type->elem->kind == TY_PROMISE) { /* promises kept (XB-106): each one's value, in order (or the first to settle) */
        Type *t = a->type->elem->elem;
        if (t->kind == TY_VOID && !race) { e->builtin.cfn = "promise_all_arr"; e->elem_type = t; e->type = ty_void; return; }
        if (t->kind == TY_VOID) xs_error(XS8081, a->pos, NULL, "the promises of Promise.race give no value");
        e->builtin.cfn = race ? "promise_race_arr" : "promise_all_arr";
        e->elem_type = t;
        e->type = race ? t : ty_array_of(t);
        return;
    }
    /* values already computed (async calls have finished): all is the array, race its first element */
    if (race) xs_error(XS7034, a->pos, "write the calls in place: Promise.race([a(), b()])", "Promise.race of values already computed");
    *e = *a;
}
static bool is_regexp(Type *t) { return t && t->kind == TY_OBJECT && t->cls->builtin && strcmp(t->cls->name, "RegExp") == 0; }

static const char **class_names(int *n);

/* Replaces placeholder class names left by the parser with the real classes. */
static bool nullable_ok(Type *inner) {
    return inner->kind == TY_UNION || inner->kind == TY_STRING || inner->kind == TY_ARRAY || inner->kind == TY_OBJECT || inner->kind == TY_MAP || inner->kind == TY_SET ||
           inner->kind == TY_FUNC || /* onDone?: () => void: a closure or NULL */
           inner->kind == TY_PROMISE || /* Promise<T> | null: a promise or NULL */
           inner->kind == TY_JSON || /* object | null: the JSON text or NULL (where?: object in an options interface) */
           inner->kind == TY_INT || inner->kind == TY_NUMBER || inner->kind == TY_BOOLEAN || inner->kind == TY_ENUM; /* primitives: a sentinel */
}

/* ---- generic interfaces: one concrete interface per list of type arguments ---- */
static Type *resolve(Type *t, Pos pos);
static Type *ty_numkey(void);
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
            ph->cname = NULL; /* (a member of a generic alias names its template: resolved by name, then instantiated) */
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
    case TY_UNION: {
        Type *n = xs_alloc(sizeof *n);
        *n = *t;
        n->params = xs_alloc(sizeof(Type *) * (size_t)(t->nparams ? t->nparams : 1));
        for (int i = 0; i < t->nparams; i++) n->params[i] = subst(t->params[i], tmpl, args);
        return n;
    }
    case TY_NULLABLE: return ty_nullable_of(subst(t->elem, tmpl, args));
    case TY_SET: return ty_set_of(subst(t->elem, tmpl, args));
    case TY_PROMISE: return ty_promise_of(subst(t->elem, tmpl, args));
    case TY_MAP: { /* (type F<T> = Record<string, unknown>: still a Record) */
        Type *m = ty_map_of(subst(t->key, tmpl, args), subst(t->elem, tmpl, args));
        if (t->record && !m->record) { Type *r = xs_alloc(sizeof *r); *r = *m; r->record = true; m = r; }
        return m;
    }
    case TY_FUNC: {
        Type **ps = xs_alloc(sizeof(Type *) * (size_t)(t->nparams ? t->nparams : 1));
        for (int i = 0; i < t->nparams; i++) ps[i] = subst(t->params[i], tmpl, args);
        Type *ft = ty_func(ps, t->nparams, subst(t->ret, tmpl, args));
        ft->rest = t->rest;
        ft->async_ret = t->async_ret;
        if (ft->ret && ft->ret->kind == TY_PROMISE) { ft->ret = ft->ret->elem; ft->async_ret = true; }
        return ft;
    }
    case TY_TPARAM:
        if (t->elem) { /* F["key"], F[number], NonNullable<F> in a generic alias: over the type argument */
            Type *n = xs_alloc(sizeof *n);
            *n = *t;
            n->elem = subst(t->elem, tmpl, args);
            return n;
        }
        return t;
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
        c->fields[i].cname = field_cname(tmpl->fields[i].name);
    }
    c->nfields = c->cap_fields = tmpl->nfields;
    cur_mod = saved_mod;
    scope = saved_scope;
    if (cur_mod) { xs_file_name = cur_mod->file; xs_source = cur_mod->src; }
    prog->classes = realloc(prog->classes, sizeof(Class *) * (size_t)(prog->nclasses + 1));
    prog->cap_classes = prog->nclasses + 1; /* keep the parser's add_class in step */
    prog->classes[prog->nclasses++] = c;
    return c;
}

/* ---- generic classes: one class per list of type arguments, its body parsed again with them bound ---- */
static void flatten_fields(Class *c, int depth);
static void flatten_methods(Class *c);
static void resolve_signature(Func *f);
static Type *fret(Func *f, Pos pos);
static Func *lookup_func(const char *name);
static void ret_infer_stmt(Stmt *s);
static void ret_infer_begin(Func *f);
static void ret_infer_end(Func *f, Pos pos);
static void check_type_supported(Pos pos, Type *t);
static Func *eff_ctor(Class *c);
static Field *find_field(Class *c, const char *name, int *index);
static Class **class_work; /* instances whose bodies the checker checks after the program's own */
static int nclass_work;
static Class *instantiate_class(Class *tmpl, Type **args, Pos pos) {
    static Class **made;
    static Type ***made_args;
    static int nmade;
    for (int i = 0; i < nmade; i++) {
        if (made[i]->generic_of != tmpl) continue;
        bool same = true;
        for (int k = 0; k < tmpl->ntparams && same; k++) same = ty_equal(made_args[i][k], args[k]);
        if (same) return made[i];
    }
    for (int k = 0; k < tmpl->ntparams; k++)
        if (args[k]->kind == TY_TPARAM) xs_error(XS9902, pos, NULL, "internal: a generic class instantiated with a type parameter");
    Buf name = {0};
    buf_printf(&name, "%s<", tmpl->name);
    for (int k = 0; k < tmpl->ntparams; k++) buf_printf(&name, "%s%s", k ? ", " : "", ty_name(args[k]));
    buf_puts(&name, ">");
    Class *c = xs_parse_class_instance(tmpl, args, name.data);
    c->cname = xs_sprintf("c%d_%s_g%d", tmpl->mod->id, tmpl->name, nmade);
    made = realloc(made, sizeof(Class *) * (size_t)(nmade + 1));
    made_args = realloc(made_args, sizeof(Type **) * (size_t)(nmade + 1));
    made[nmade] = c;
    made_args[nmade] = args;
    int my = nmade++; /* (resolving its signatures can make more instances) */
    prog->classes = realloc(prog->classes, sizeof(Class *) * (size_t)(prog->nclasses + 1));
    prog->cap_classes = prog->nclasses + 1; /* keep the parser's add_class in step */
    prog->classes[prog->nclasses++] = c;
    /* the class pipeline, in the template's module (the caller's scope is put back) */
    Module *saved_mod = cur_mod;
    Scope *saved_scope = scope;
    Class *saved_class = cur_class;
    const char *sfn = xs_file_name, *ssrc = xs_source;
    enter_module(tmpl->mod);
    cur_class = NULL;
    flatten_fields(c, 0);
    if (!eff_ctor(c))
        for (int k = 0; k < c->nfields; k++) if (!c->fields[k].init && !c->fields[k].optional) c->data_only = true;
    for (int k = 0; k < c->nfields; k++) {
        Field *fd = &c->fields[k];
        for (int j = 0; j < k; j++)
            if (strcmp(fd->name, c->fields[j].name) == 0) xs_error(XS9023, fd->pos, NULL, "field '%s' is declared twice", fd->name);
        fd->type = resolve(fd->type, fd->pos);
        check_type_supported(fd->pos, fd->type);
        fd->cname = field_cname(fd->name);
    }
    for (int k = 0; k < c->nmethods; k++) {
        Func *m = c->methods[k];
        for (int j = 0; j < k; j++)
            if (strcmp(m->name, c->methods[j]->name) == 0) xs_error(XS9027, m->pos, NULL, "method '%s' is declared twice", m->name);
        if (find_field(c, m->name, NULL)) xs_error(XS9007, m->pos, NULL, "'%s' is both a field and a method of %s", m->name, c->name);
        m->cname = xs_sprintf("m%d_%s_g%d_%s", tmpl->mod->id, tmpl->name, my, m->name);
        if (!m->ntparams) resolve_signature(m);
    }
    if (c->ctor) {
        c->ctor->cname = xs_sprintf("%s_new", c->cname);
        resolve_signature(c->ctor);
    }
    flatten_methods(c);
    cur_mod = saved_mod;
    scope = saved_scope;
    cur_class = saved_class;
    xs_file_name = sfn;
    xs_source = ssrc;
    class_work = realloc(class_work, sizeof(Class *) * (size_t)(nclass_work + 1));
    class_work[nclass_work++] = c;
    return c;
}
/* a generic class written without type arguments: from the type the place expects (const s: Stack<int> = new Stack()) */
static Class *generic_new(Expr *e, Class *tmpl, Type *expected) {
    if (e->ntargs) {
        if (e->ntargs != tmpl->ntparams)
            xs_error(XS8063, e->pos, xs_sprintf("write new %s<...>() with %d type argument(s)", tmpl->name, tmpl->ntparams), "'%s' takes %d type argument(s), got %d", tmpl->name, tmpl->ntparams, e->ntargs);
        Type **args = xs_alloc(sizeof(Type *) * (size_t)tmpl->ntparams);
        for (int i = 0; i < e->ntargs; i++) args[i] = resolve(e->targs[i], e->pos);
        return instantiate_class(tmpl, args, e->pos);
    }
    Type *x = expected && expected->kind == TY_NULLABLE ? expected->elem : expected;
    if (x && x->kind == TY_OBJECT && x->cls && x->cls->generic_of == tmpl) return x->cls;
    xs_error(XS3007, e->pos, xs_sprintf("give the type arguments: new %s<Type>()", tmpl->name), "cannot infer the type arguments of %s", tmpl->name);
    return NULL;
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
    if (n < 1) xs_error(XS3005, pos, NULL, "an empty tuple type [] has no use");
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
    prog->cap_classes = prog->nclasses + 1; /* keep the parser's add_class in step */
    prog->classes[prog->nclasses++] = c;
    return c;
}
static Type *tuple_type(Type **ps, int n, Pos pos) {
    Type *t = xs_alloc(sizeof *t);
    t->kind = TY_OBJECT;
    t->cls = tuple_class(ps, n, pos);
    return t;
}

static Binding *lookup(const char *name);
/* the type a literal has, read before checking (typeof X of a module constant): numbers, strings, booleans and arrays of one of them */
static Type *literal_type(Expr *e) {
    if (!e) return NULL;
    if (e->kind == E_AS) return literal_type(e->lhs);
    if (e->kind == E_NUMBER) return ty_number;
    if (e->kind == E_STRING) return ty_string;
    if (e->kind == E_BOOL) return ty_boolean;
    if (e->kind == E_ARRAY_LIT && e->args.len) {
        Type *el = literal_type(e->args.items[0]);
        for (int i = 1; el && i < e->args.len; i++) if (literal_type(e->args.items[i]) != el) el = NULL;
        return el ? ty_array_of(el) : NULL;
    }
    return NULL;
}
/* a return type: Promise<T> is T (the function is async); T | Promise<T> too (XB-106) */
static Type *ret_unpromise(Type *t, bool *async) {
    if (!t) return t;
    if (t->kind == TY_PROMISE) { *async = true; return t->elem; }
    Type *u = t->kind == TY_NULLABLE ? t->elem : t;
    if (u->kind != TY_UNION) return t;
    bool any = false;
    for (int i = 0; i < u->nparams; i++) if (u->params[i]->kind == TY_PROMISE) any = true;
    if (!any) return t;
    *async = true;
    Type **m = xs_alloc(sizeof(Type *) * (size_t)u->nparams);
    int n = 0;
    bool nul = t->kind == TY_NULLABLE;
    for (int i = 0; i < u->nparams; i++) {
        Type *x = u->params[i]->kind == TY_PROMISE ? u->params[i]->elem : u->params[i];
        if (x->kind == TY_NULLABLE) { nul = true; x = x->elem; } /* Promise<T> | T with T = X | null: X | null */
        if (x->kind == TY_NULL) { nul = true; continue; }
        bool dup = false;
        for (int k = 0; k < n && !dup; k++) dup = ty_equal(m[k], x);
        if (!dup) m[n++] = x;
    }
    if (n == 0) return ty_null;
    Type *r = m[0];
    if (n > 1) { r = xs_alloc(sizeof *r); *r = *u; r->params = m; r->nparams = n; }
    return nul ? ty_nullable_of(r) : r;
}
static Type *resolve(Type *t, Pos pos);
/* generic function values (Type.gen_sig): a copy of t with the type parameters `names` bound to `args` */
static Type *subst_ftp(Type *t, char **names, Type **args, int n) {
    if (!t) return t;
    if (t->kind == TY_TPARAM && t->tname)
        for (int i = 0; i < n; i++) if (strcmp(t->tname, names[i]) == 0) return args[i];
    Type *c = xs_alloc(sizeof *c);
    *c = *t;
    c->elem = subst_ftp(t->elem, names, args, n);
    c->key = subst_ftp(t->key, names, args, n);
    c->ret = subst_ftp(t->ret, names, args, n);
    if (t->nparams) {
        c->params = xs_alloc(sizeof(Type *) * (size_t)t->nparams);
        for (int i = 0; i < t->nparams; i++) c->params[i] = subst_ftp(t->params[i], names, args, n);
    }
    if (t->ntargs) {
        c->targs = xs_alloc(sizeof(Type *) * (size_t)t->ntargs);
        for (int i = 0; i < t->ntargs; i++) c->targs[i] = subst_ftp(t->targs[i], names, args, n);
    }
    return c;
}
/* f<Row>(x) where f holds a generic function (ft->gen_sig): the result type the signature gives with these type
   arguments (what the call's unknown is converted to); NULL without type arguments (the unknown stays) */
static Type *gen_call_ret(Type *ft, Expr *call) {
    Type *g = ft ? ft->gen_sig : NULL;
    if (!g || !call->ntargs) return NULL;
    if (call->ntargs != g->nftparams)
        xs_error(XS8062, call->pos, NULL, "the function takes %d type argument(s), got %d", g->nftparams, call->ntargs);
    Type **args = xs_alloc(sizeof(Type *) * (size_t)call->ntargs);
    for (int i = 0; i < call->ntargs; i++) args[i] = resolve(call->targs[i], call->pos);
    Type *r = resolve(subst_ftp(g->ret, g->ftparams, args, g->nftparams), call->pos);
    bool as = false;
    return ret_unpromise(r, &as);
}
static void check_expr(Expr *e, Type *expected);
/* e (checked, of the erased result type) becomes `e as T` with T = e->gen_ret */
static void gen_convert(Expr *e) {
    Type *t = e->gen_ret;
    e->gen_ret = NULL;
    if (!t || ty_equal(t, e->type) || t->kind == TY_UNKNOWN) return;
    Expr *in = xs_alloc(sizeof *in);
    *in = *e;
    in->prechecked = true;
    Type *b = ty_strip_null(t);
    TypeKind sk = ty_strip_null(e->type)->kind;
    if ((sk == TY_UNKNOWN || sk == TY_ARRAY || sk == TY_MAP || sk == TY_OBJECT) && (b->kind == TY_ARRAY || (b->kind == TY_MAP && b->record) ||
                                        (b->kind == TY_OBJECT && !b->cls->builtin && b->cls->nmethods == 0 && !b->cls->iface_methods))) {
        /* data (rows of a query, decoded per T in a generic instance): read as its JSON and decoded into T, as the
           instance for T would have decoded it (an object of T itself comes back equal, a copy) */
        Expr *js = xs_alloc(sizeof *js);
        js->kind = E_AS; js->pos = in->pos; js->field = -1; js->lhs = in; js->elem_type = ty_json;
        /* XS2110: accepted (Isa, 11/11), with its cost said: a copy, not the object the function returned */
        Expr *callee = in->kind == E_AWAIT ? in->lhs : in;
        if (callee && callee->kind == E_SPAWN) callee = callee->lhs;
        const char *fname = "f";
        if (callee && callee->kind == E_CALL && callee->lhs && callee->lhs->kind == E_IDENT && callee->lhs->str) fname = callee->lhs->str;
        else if (callee && callee->kind == E_CALL && callee->lhs && callee->lhs->kind == E_MEMBER && callee->lhs->str) fname = callee->lhs->str;
        xs_warn_rule(XS2110, in->pos,
                     "its type parameter is read as unknown, so the value is written to JSON and decoded as the type given (no identity "
                     "with the object returned; a cost per call that grows with its size): give the function a concrete return type, "
                     "or make it a generic function or method instead of a value",
                     "the result of the generic function value '%s' is a copy of %s made through JSON", fname,
                     strstr(ty_name(t), "__Obj_") ? "the object type given" : ty_name(t));
        in = js;
    }
    memset(e, 0, sizeof *e);
    e->kind = E_AS;
    e->pos = in->pos;
    e->field = -1;
    e->lhs = in;
    e->elem_type = t;
    check_expr(e, NULL);
}
/* { a: T } written in a generic class's or function's text: each instance parses it again with T bound, and that
   makes its interface while the checker runs (after the program's classes were registered): registered here, the
   first time a type names it, with its fields resolved where it was written */
static void flatten_fields(Class *c, int depth);
static void apply_mixin(Class *c);
static Func *find_method(Class *c, const char *name);
static bool late_inline_class(Class *c) {
    bool in_prog = false;
    for (int i = prog->nclasses - 1; i >= 0 && !in_prog; i--) in_prog = prog->classes[i] == c;
    if (!in_prog || c->builtin) return false;
    sym_add(&classes_by_name, c->name, c);
    c->cname = xs_sprintf("c%d_%s", c->mod->id, c->name);
    c->used = true;
    Module *saved_mod = cur_mod;
    Scope *saved_scope = scope;
    const char *sfn = xs_file_name, *ssrc = xs_source;
    enter_module(c->mod);
    if (c->mixin && !c->mixin_done) apply_mixin(c); /* Partial<T> in a generic's instance */
    flatten_fields(c, 0);
    for (int k = 0; k < c->nfields; k++) {
        Field *fd = &c->fields[k];
        fd->type = resolve(fd->type, fd->pos);
        fd->cname = field_cname(fd->name);
    }
    cur_mod = saved_mod;
    scope = saved_scope;
    xs_file_name = sfn;
    xs_source = ssrc;
    return true;
}

static bool fake_target(Class *t, Expr *lit);
static void fake_object(Expr *e, Class *t);
static bool objlit_has_spread(Expr *e);
static Type *resolve(Type *t, Pos pos) {
    if (t->kind == TY_TPARAM && t->tname && strncmp(t->tname, "__ret_of:", 9) == 0) { /* (generated reflection) */
        const char *dot = strchr(t->tname + 9, '.');
        Class *c = find_class(xs_strndup(t->tname + 9, (size_t)(dot - (t->tname + 9))));
        Func *m = NULL;
        for (int i = 0; c && i < c->nmethods; i++) if (strcmp(c->methods[i]->name, dot + 1) == 0) m = c->methods[i];
        if (!m) xs_error(XS9031, pos, NULL, "no method %s", t->tname + 9);
        return fret(m, pos);
    }
    if (t->kind == TY_TPARAM && t->tname && strncmp(t->tname, "__ret_of_fn:", 12) == 0) { /* ReturnType<typeof f> */
        Func *f = lookup_func(t->tname + 12);
        if (!f || f->ntparams) xs_error(XS1019, pos, "write the type itself", "ReturnType<typeof %s>: no function '%s' here", t->tname + 12, t->tname + 12);
        Type *r = fret(f, pos);
        *t = *r; /* (in place: whoever holds it now holds the function's type) */
        return t;
    }
    if (t->kind == TY_TPARAM && t->tname && strncmp(t->tname, "__pick:", 7) == 0) { /* Pick<A | B, 'k'>: the union (see parser) */
        Type *u = resolve(t->elem, pos);
        Type *b = ty_strip_null(u);
        Type **ms = b->kind == TY_UNION ? b->params : &b;
        int n = b->kind == TY_UNION ? b->nparams : 1;
        char *keys = xs_sprintf("%s", t->tname + 7);
        for (char *k = strtok(keys, "|"); k; k = strtok(NULL, "|"))
            for (int i = 0; i < n; i++) {
                Class *c = ms[i]->kind == TY_OBJECT ? ms[i]->cls : NULL;
                if (!c || c->builtin || (!find_field(c, k, NULL) && !find_method(c, k)))
                    xs_error(XS1019, pos, "pick from interfaces or classes that have the key", "Pick: %s has no '%s'", ty_name(ms[i]), k);
            }
        *t = *u;
        return t;
    }
    if (t->kind == TY_TPARAM && t->tname && strncmp(t->tname, "__param_of:", 11) == 0) { /* Parameters<typeof f>[i] */
        const char *spec = t->tname + 11, *colon = strrchr(spec, ':');
        char *fname = xs_strndup(spec, (size_t)(colon - spec));
        int i = atoi(colon + 1);
        Func *f = lookup_func(fname);
        Binding *b = f ? NULL : lookup(fname);
        if (f && !f->ntparams && i < f->nparams) { resolve_signature(f); *t = *f->params[i].type; return t; }
        if (b && b->type->kind == TY_FUNC && i < b->type->nparams) { *t = *b->type->params[i]; return t; }
        xs_error(XS1019, pos, "write the type itself", "Parameters<typeof %s>[%d]: no such parameter of a function '%s' here", fname, i, fname);
    }
    if (t->kind == TY_TPARAM && t->tname && strcmp(t->tname, "__nonnull") == 0) { /* NonNullable<T> */
        Type *r = ty_strip_null(resolve(t->elem, pos));
        *t = *r;
        return t;
    }
    if (t->kind == TY_TPARAM && t->tname && strncmp(t->tname, "__typeofw:", 10) == 0) { /* (n = CONST): typeof CONST, int as number */
        Type *u = xs_alloc(sizeof *u);
        u->kind = TY_TPARAM;
        u->tname = xs_sprintf("__typeof:%s", t->tname + 10);
        u = resolve(u, pos);
        if (u->kind == TY_INT || u->kind == TY_LITINT) u = ty_number;
        if (u->kind == TY_STRING && u->tname) u = ty_string; /* (a const string's literal type: any string) */
        *t = *u;
        return t;
    }
    if (t->kind == TY_TPARAM && t->tname && strncmp(t->tname, "__typeof:", 9) == 0) { /* typeof x */
        Binding *b = lookup(t->tname + 9);
        if (!b || !b->type) { /* a module constant not checked yet: the type its literal has (const X = ['a', 'b'] as const) */
            for (int pass = 0; pass < 2; pass++) /* this module's, then an exported one (imported by name) */
                for (int i = 0; i < prog->top.len; i++) {
                    Stmt *st = prog->top.items[i];
                    if (st->kind != S_VAR || !st->name || strcmp(st->name, t->tname + 9) != 0 || !st->init) continue;
                    if (pass == 0 ? st->mod != cur_mod : !st->exported) continue;
                    Type *lt = st->decl_type ? resolve(st->decl_type, pos) : literal_type(st->init);
                    Expr *in = st->init;
                    if (!lt && in->kind == E_CALL && !in->ntargs && in->lhs && in->lhs->kind == E_IDENT) {
                        /* registerAs('databases', (): DatabaseOption[] => ...): T from the callback's written result type */
                        Module *sm = cur_mod;
                        Scope *ss = scope;
                        const char *sfn = xs_file_name, *ssrc = xs_source;
                        enter_module(st->mod);
                        Func *gf = lookup_func(in->lhs->str);
                        if (gf && gf->ntparams && gf->ret && gf->nparams == in->args.len) {
                            Type **args = xs_alloc(sizeof(Type *) * (size_t)gf->ntparams);
                            int got = 0;
                            for (int k = 0; k < gf->nparams; k++) {
                                Type *pt = gf->params[k].type;
                                Expr *a = in->args.items[k];
                                if (!pt || pt->kind != TY_FUNC || !pt->ret || pt->ret->kind != TY_TPARAM || a->kind != E_ARROW || !a->arrow->fn->ret) continue;
                                for (int q = 0; q < gf->ntparams; q++)
                                    if (!args[q] && pt->ret->tname && strcmp(gf->tparams[q], pt->ret->tname) == 0) { args[q] = resolve(a->arrow->fn->ret, in->pos); got++; }
                            }
                            if (got == gf->ntparams) {
                                Module *fsm = cur_mod;
                                enter_module(gf->mod);
                                lt = resolve(subst_ftp(gf->ret, gf->tparams, args, gf->ntparams), pos);
                                cur_mod = fsm;
                            }
                        }
                        cur_mod = sm;
                        scope = ss;
                        xs_file_name = sfn;
                        xs_source = ssrc;
                    }
                    if (!lt && in->kind == E_CALL && in->ntargs && in->lhs && in->lhs->kind == E_IDENT) {
                        /* export default registerAs<Cfg>(...): a generic function's written result with the call's
                           type arguments (typeof cfg in ConfigType<typeof cfg>, before cfg is checked) */
                        Module *sm = cur_mod;
                        Scope *ss = scope;
                        const char *sfn = xs_file_name, *ssrc = xs_source;
                        enter_module(st->mod);
                        Func *gf = lookup_func(in->lhs->str);
                        if (gf && gf->ntparams == in->ntargs && gf->ret) {
                            Type **args = xs_alloc(sizeof(Type *) * (size_t)in->ntargs);
                            for (int k = 0; k < in->ntargs; k++) args[k] = resolve(in->targs[k], in->pos);
                            Module *fsm = cur_mod;
                            enter_module(gf->mod);
                            lt = resolve(subst_ftp(gf->ret, gf->tparams, args, gf->ntparams), pos);
                            cur_mod = fsm;
                        }
                        cur_mod = sm;
                        scope = ss;
                        xs_file_name = sfn;
                        xs_source = ssrc;
                    }
                    if (lt) { *t = *lt; return t; }
                }
        }
        if (!b || !b->type) xs_error(XS1019, pos, "write the type itself", "typeof %s: no variable '%s' here (or its type is not known yet)", t->tname + 9, t->tname + 9);
        *t = *b->type;
        return t;
    }
    if (t->kind == TY_TPARAM && t->tname && strcmp(t->tname, "__elem_of") == 0) { /* T[number] */
        Type *o = resolve(t->elem, pos);
        if (o->kind != TY_ARRAY) xs_error(XS1019, pos, "write the type itself", "%s[number]: %s is not an array type", ty_name(o), ty_name(o));
        *t = *o->elem;
        return t;
    }
    if (t->kind == TY_TPARAM && t->tname && strncmp(t->tname, "__field_of:", 11) == 0) { /* T['k'] */
        const char *k = t->tname + 11;
        Type *o = resolve(t->elem, pos);
        Type *b = o->kind == TY_NULLABLE ? o->elem : o;
        Field *fd = b->kind == TY_OBJECT && !b->cls->builtin ? find_field(b->cls, k, NULL) : NULL;
        if (!fd) xs_error(XS1019, pos, "write the type itself", "%s has no field '%s'", ty_name(o), k);
        if (!fd->type) xs_error(XS1019, pos, "write the field's type", "the type of %s.%s is not known yet here", b->cls->name, k);
        *t = *resolve(fd->type, pos);
        return t;
    }
    switch (t->kind) {
    case TY_TUPLE: { /* in place: whoever holds this type now holds the tuple's object type */
        for (int i = 0; i < t->nparams; i++) t->params[i] = resolve(t->params[i], pos);
        Type *o = tuple_type(t->params, t->nparams, pos);
        *t = *o;
        return t;
    }
    case TY_SET:
        t->elem = resolve(t->elem, pos);
        if (t->elem->kind == TY_NUMBER) t->elem = ty_numkey(); /* Set<number>: whole numbers, as Map<number, V> */
        if (t->elem->kind == TY_STRING && t->elem->tname) t->elem = ty_string;
        if (t->elem->kind == TY_UNION) {
            bool strs = true;
            for (int i = 0; i < t->elem->nparams; i++) strs = strs && t->elem->params[i]->kind == TY_STRING;
            if (strs) t->elem = ty_string; /* Set<'a' | 'b'>: strings */
        }
        return t;
    case TY_ARRAY: case TY_STREAM: t->elem = resolve(t->elem, pos); return t;
    case TY_PROMISE: /* Promise<T> kept as a value */
        t->elem = resolve(t->elem, pos);
        if (t->elem->kind == TY_PROMISE) { Type *u = t->elem; *t = *u; }
        return t;
    case TY_MAP:
        t->key = resolve(t->key, pos);
        t->elem = resolve(t->elem, pos);
        if (t->key->kind == TY_NUMBER) t->key = ty_numkey(); /* Map<number, V>: keys are whole numbers (ids), checked when one goes in */
        if (t->key->kind == TY_ENUM) t->key = t->key->en->is_string ? ty_string : ty_int; /* Record<Enum, V>: by the enum's values */
        if (t->key->kind == TY_STRING && t->key->tname) t->key = ty_string; /* Map<'a' | 'b', V>: string keys */
        if (t->key->kind == TY_UNION) {
            bool strs = true;
            for (int i = 0; i < t->key->nparams; i++) strs = strs && t->key->params[i]->kind == TY_STRING;
            if (strs) t->key = ty_string;
        }
        if (t->key->kind != TY_STRING && t->key->kind != TY_INT)
            xs_error(XS6035, pos, "Map keys are string or int in xisto", "unsupported Map key type '%s'", ty_name(t->key));
        return t;
    case TY_NULLABLE:
        t->elem = resolve(t->elem, pos);
        if (t->elem->kind == TY_UNKNOWN) { Type *u = t->elem; *t = *u; return t; } /* unknown | null, x?: unknown: unknown holds null already */
        if (t->elem->kind == TY_TPARAM) return t; /* T | null in a template's own text: checked per instance */
        if (t->elem->kind == TY_NULLABLE) { bool ab = t->absent; Type *u = t->elem; *t = *u; t->absent = t->absent || ab; return t; } /* (T | null) | null */
        if (!nullable_ok(t->elem))
            xs_error(XS2003, pos, "values that can be null: strings, numbers, booleans, enums, arrays, objects, Map and Set", "'%s | null' is not supported", ty_name(t->elem));
        return t;
    case TY_FUNC:
        for (int i = 0; i < t->nparams; i++) t->params[i] = resolve(t->params[i], pos);
        t->ret = resolve(t->ret, pos);
        { bool as = false; t->ret = ret_unpromise(t->ret, &as); if (as) t->async_ret = true; } /* () => Promise<R>: an async function returning R */
        return t;
    case TY_UNION: { /* members resolved, nested unions flattened, repeats dropped; null makes it nullable */
        Type **m = NULL;
        int n = 0;
        bool nul = false;
        bool has_promise = false; /* Promise<void> | void (a callback that may be async): the promise */
        for (int i = 0; i < t->nparams; i++) if (t->params[i]->kind == TY_PROMISE || (t->params[i]->kind == TY_OBJECT && t->params[i]->cls && t->params[i]->cls->name && !strcmp(t->params[i]->cls->name, "Promise"))) has_promise = true;
        for (int i = 0; i < t->nparams; i++) {
            if (has_promise && t->params[i]->kind == TY_VOID) continue;
            Type *x = resolve(t->params[i], pos);
            if (x->kind == TY_NULLABLE) { nul = true; x = x->elem; }
            Type **xs = x->kind == TY_UNION ? x->params : &x;
            int xn = x->kind == TY_UNION ? x->nparams : 1;
            for (int k = 0; k < xn; k++) {
                bool dup = false;
                for (int j = 0; j < n && !dup; j++) dup = ty_equal(m[j], xs[k]) && (m[j]->tname == xs[k]->tname || (m[j]->tname && xs[k]->tname && strcmp(m[j]->tname, xs[k]->tname) == 0));
                if (dup) continue;
                if (xs[k]->kind == TY_VOID) xs_error(XS2004, pos, NULL, "'%s' cannot be part of a union", ty_name(xs[k]));
                m = realloc(m, sizeof(Type *) * (size_t)(n + 1));
                m[n++] = xs[k];
            }
        }
        Type *u = m[0];
        bool strs = n > 1;
        for (int i = 0; i < n && strs; i++) strs = m[i]->kind == TY_STRING;
        if (strs) u = ty_string; /* 'A' | 'B': a string (TypeScript checks which ones) */
        else if (n > 1) { u = xs_alloc(sizeof *u); u->kind = TY_UNION; u->params = m; u->nparams = n; }
        return nul ? ty_nullable_of(u) : u;
    }
    case TY_OBJECT:
        if (t->cls->mixin && t->cls->mixin->kind && !strcmp(t->cls->mixin->kind, "PickType") && t->cls->mixin->a && !t->cls->mixin->a->kind) {
            /* Pick<Logger, 'log' | 'warn'> of a class with those methods: values of the class itself go there (its methods
               are what is picked; a shape copy would leave them out) */
            Mixin *m = t->cls->mixin;
            Module *was = cur_mod;
            Scope *ss = scope;
            const char *sfn = xs_file_name, *ssrc = xs_source;
            if (t->cls->mod && t->cls->mod != cur_mod) enter_module(t->cls->mod);
            Class *src = m->a->klass ? m->a->klass : find_class(m->a->cls);
            cur_mod = was; scope = ss; xs_file_name = sfn; xs_source = ssrc;
            bool methods = src && !src->is_interface && !src->builtin && m->nkeys > 0;
            for (int i = 0; i < m->nkeys && methods; i++) methods = find_method(src, m->keys[i]) != NULL;
            if (methods) { *t = *ty_object_of(src); return t; }
        }
        if (t->cls->mixin && t->cls->mixin->kind && !strcmp(t->cls->mixin->kind, "IntersectionType") && t->cls->mixin->a && !t->cls->mixin->a->kind) {
            /* Request & { terminal?: T } with an open class (a field __expando: Express's Request): the class itself,
               whose undeclared properties live in its __expando (a shape copy would lose them) */
            Mixin *m = t->cls->mixin;
            Module *was = cur_mod;
            Scope *ss = scope;
            const char *sfn = xs_file_name, *ssrc = xs_source;
            if (t->cls->mod && t->cls->mod != cur_mod) enter_module(t->cls->mod);
            Class *src = m->a->klass ? m->a->klass : find_class(m->a->cls);
            cur_mod = was; scope = ss; xs_file_name = sfn; xs_source = ssrc;
            int xi;
            if (src && !src->is_interface && !src->builtin && find_field(src, "__expando", &xi)) { *t = *ty_object_of(src); return t; }
        }
        if (t->cls->cname) return t;
        if (t->cls->name && strncmp(t->cls->name, "__Obj_", 6) == 0 && t->cls->mod && late_inline_class(t->cls)) return t;
        {
            Enum *en = find_enum(t->cls->name);
            if (en) return ty_enum_of(en);
            Class *c = find_class(t->cls->name);
            if (!c) {
                int n; const char **cands = class_names(&n);
                if (missing_name(t->cls->name)) xs_error_quiet();
                xs_error(XS9033, t->cls->pos.line ? t->cls->pos : pos, xs_help_suggest(t->cls->name, cands, n, "declare the class above, or use number, int, boolean, string or T[]"), "unknown type '%s'", t->cls->name);
            }
            if (c->alias && c->ntparams) { /* type Item<T> = A<T> | B<T>: the alias with these arguments, resolved per use */
                Type **ta = xs_alloc(sizeof(Type *) * (size_t)c->ntparams);
                int n = 0;
                for (; n < t->ntargs && n < c->ntparams; n++) ta[n] = resolve(t->targs[n], pos);
                while (n < c->ntparams && c->ctdefaults && c->ctdefaults[n]) { ta[n] = c->ctdefaults[n]; n++; }
                if (n != c->ntparams || t->ntargs > c->ntparams)
                    xs_error(XS8063, pos, xs_sprintf("write %s<...> with %d type argument(s)", c->name, c->ntparams), "'%s' takes %d type argument(s), got %d", c->name, c->ntparams, t->ntargs);
                Type *a = subst(c->alias, c, ta);
                Module *sm = cur_mod;
                Scope *ss = scope;
                const char *sfn = xs_file_name, *ssrc = xs_source;
                enter_module(c->mod);
                a = resolve(a, c->pos);
                cur_mod = sm;
                scope = ss;
                xs_file_name = sfn;
                xs_source = ssrc;
                return a;
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
            if (c->ntparams > t->ntargs && c->ctdefaults && c->ctdefaults[t->ntargs]) { /* Name<A> of Name<A, B = X>: the defaults */
                Type **ta = xs_alloc(sizeof(Type *) * (size_t)c->ntparams);
                for (int i = 0; i < t->ntargs; i++) ta[i] = t->targs[i];
                int n = t->ntargs;
                while (n < c->ntparams && c->ctdefaults[n]) { ta[n] = c->ctdefaults[n]; n++; }
                t->targs = ta;
                t->ntargs = n;
            }
            if (c->ntparams || t->ntargs) {
                if (c->ntparams != t->ntargs)
                    xs_error(XS8063, pos, xs_sprintf("write %s<...> with %d type argument(s)", c->name, c->ntparams), "'%s' takes %d type argument(s), got %d", c->name, c->ntparams, t->ntargs);
                for (int i = 0; i < t->ntargs; i++) t->targs[i] = resolve(t->targs[i], pos);
                c = c->gen_state ? instantiate_class(c, t->targs, pos) : instantiate(c, t->targs, pos);
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

/* a capture's name as a C identifier: a narrowed path is held under a key like "this.1.Service" (this.client after a
   null test), which an arrow can capture */
static const char *cap_cid(const char *name) {
    bool plain = true;
    for (const char *q = name; *q; q++) if (!(isalnum((unsigned char)*q) || *q == '_')) plain = false;
    if (plain) return name;
    Buf b = {0};
    buf_puts(&b, "np_");
    for (const char *q = name; *q; q++)
        if (isalnum((unsigned char)*q) || *q == '_') buf_putc(&b, *q);
        else if (*q == '.') buf_putc(&b, '_');
    return b.data;
}
static Capture *add_capture(Arrow *a, const char *name, const char *outer_cname, Type *type, bool is_this) {
    name = cap_cid(name);
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
            if (nb > 0 && name[0] == '\x01') {
                /* a narrowed path (this.client after a null test): it is read through the captured `this` or
                   variable, so there is nothing to copy. In a task the checker made (Promise.race([this.c.close()]))
                   the narrowing still holds, as in the source; a callback written in the source does not see it,
                   as in TypeScript (the field may change before it runs) */
                bool all_synthetic = true;
                for (int i = 0; i < nb; i++) if (!boundaries[i]->arrow->synthetic) all_synthetic = false;
                if (!all_synthetic) return NULL;
                mark_used(b);
                return b;
            }
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
                    nbind->cell = xs_sprintf("env->c_%s", cap_cid(name));
                    nbind->cname = xs_sprintf("(env->c_%s->v)", cap_cid(name));
                } else {
                    nbind->is_const = true;
                    nbind->cname = xs_sprintf("env->c_%s", cap_cid(name));
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
    const char *q = strchr(name, '\x01'); /* "f\x01m": the function f of the import from "m" (XB-117) */
    if (q) {
        char *base = xs_strndup(name, (size_t)(q - name));
        SYM_EACH(funcs_by_name, base, e) {
            Func *f = e->decl;
            if (f->exported && imports_name(cur_mod, f->mod, name)) return f;
        }
        return NULL;
    }
    SYM_EACH(funcs_by_name, name, e) {
        Func *f = e->decl;
        if (f->mod == cur_mod || (f->exported && imports_name(cur_mod, f->mod, name))) return f;
        if (strncmp(name, "__xs_", 5) == 0 && f->mod && f->mod->file && strcmp(f->mod->file, "xisto:arrays") == 0) return f; /* (reduce & co) */
        if (f->exported && (strstr(name, "__s_") || !strcmp(name, "__xs_fetch")) && f->mod && f->mod->file && strcmp(f->mod->file, "xisto:url") == 0) return f; /* (URL.canParse, AbortSignal.timeout: statics of globals; fetch with its options in a value) */
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
        if (strcmp(b->name, name) == 0) xs_error(XS9003, pos, NULL, "'%s' is already declared in this scope", name);
    /* a variable may shadow a built-in function or namespace (lines, fetch, console...), as in TypeScript; class
       and enum names stay reserved */
    if (find_class(name) || find_enum(name)) xs_error(XS9009, pos, NULL, "'%s' is built in and cannot be redeclared", name);
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
/* (number | null)[] / (int | null)[]: a number[] / int[] holding the null sentinel (XB-136) */
static bool nullnum_elem(Type *t) { return t->kind == TY_NULLABLE && !ty_is_heap(t->elem) && is_numeric(t->elem); }

static void check_type_supported(Pos pos, Type *t) {
    if (t->kind == TY_ARRAY && (t->elem->kind == TY_VOID || (t->elem->kind == TY_NULLABLE && !ty_is_heap(t->elem) && !nullnum_elem(t->elem))))
        xs_error(XS6039, pos, "arrays hold number, int, boolean, string, enums, objects, functions, arrays, Map and Set, and (T | null) of strings, objects and arrays; not nullable numbers",
                 "unsupported array type '%s'", ty_name(t));
    if (t->kind == TY_SET && t->elem->kind != TY_STRING && t->elem->kind != TY_INT)
        xs_error(XS6037, pos, "Set elements are string or int in xisto", "unsupported Set element type '%s'", ty_name(t));
    if (t->kind == TY_CHAN && (t->elem->kind == TY_VOID || t->elem->kind == TY_FUNC))
        xs_error(XS6040, pos, NULL, "unsupported channel element type '%s'", ty_name(t->elem));
    if (t->kind == TY_MAP && (t->elem->kind == TY_VOID || t->elem->kind == TY_FUNC ||
                              (t->elem->kind == TY_NULLABLE && !ty_is_heap(t->elem) && t->elem->elem->kind != TY_NUMBER)))
        xs_error(XS6036, pos, NULL, "unsupported Map value type '%s'", ty_name(t->elem));
    if (t->kind == TY_STREAM && (t->elem->kind == TY_FUNC || t->elem->kind == TY_VOID || t->elem->kind == TY_NULLABLE || t->elem->kind == TY_STREAM))
        xs_error(XS6042, pos, NULL, "unsupported stream element type '%s'", ty_name(t->elem));
    if (t->kind == TY_VOID) xs_error(XS8147, pos, NULL, "void is not a value type");
}

/* JSON.stringify / JSON.parse cover primitives, strings, classes whose fields are all serializable, and arrays of those. */
static bool elem_widens(Type *x, Type *u);
static void arr_convert(Expr *e, Type *to);
static bool tsc_checked(void);
static bool shape_ok(Type *g, Type *want);
static bool json_ok_unknown; /* JSON.stringify: an unknown is written by its type at run time */
static bool json_ok(Type *t, int depth) {
    if (depth > 24) return false; /* (a type that holds itself: no JSON) */
    switch (t->kind) {
    case TY_UNKNOWN: return json_ok_unknown;
    case TY_UNION: /* number | string (a row's column): held as an unknown is, read and written by what it holds */
        for (int i = 0; i < t->nparams; i++) {
            TypeKind k = t->params[i]->kind;
            if (k == TY_OBJECT && t->params[i]->cls->builtin && !strcmp(t->params[i]->cls->name, "Date")) continue; /* Date | string (a
                row's date column, read from JSON as the text it is: what the value holds says which) */
            if (k != TY_NUMBER && k != TY_INT && k != TY_STRING && k != TY_BOOLEAN) return false;
        }
        return true;
    case TY_NUMBER: case TY_INT: case TY_BOOLEAN: case TY_STRING: case TY_ENUM: return true;
    case TY_JSON: return true; /* embedded as it is */
    case TY_ARRAY: case TY_NULLABLE: return json_ok(t->elem, depth + 1);
    case TY_MAP: return t->record && t->key->kind == TY_STRING && json_ok(t->elem, depth + 1); /* a Record: an object */
    case TY_OBJECT:
        if (t->cls->builtin) /* a Date as its ISO text; a bigint (read from a number or its digits; writing one throws, as in
                                JavaScript); a Buffer as hex text or {"type":"Buffer","data":[...]} */
            return strcmp(t->cls->name, "Date") == 0 || strcmp(t->cls->name, "bigint") == 0 || strcmp(t->cls->name, "Buffer") == 0;
        { /* a type that holds itself (a tree: children?: Item[]) is fine: its helpers call themselves */
            static Class *open[32];
            static int nopen;
            for (int i = 0; i < nopen; i++) if (open[i] == t->cls) return true;
            if (nopen == 32) return false;
            open[nopen++] = t->cls;
            bool ok = true;
            for (int i = 0; i < t->cls->nfields && ok; i++)
                if (!json_ok(t->cls->fields[i].type, depth + 1)) ok = false;
            nopen--;
            return ok;
        }
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
static bool cond_truthy_type(Type *t);
static void make_truthy(Expr *e, const char *what);
static void check_optchain(Expr *e, Type *expected);
static void check_nullish(Expr *e, Type *expected);
/* a primitive, an enum, or a union of them: boxed as an unknown, compared by what it holds */
static bool prim_like(Type *t) {
    if (t->kind == TY_NULLABLE) t = t->elem;
    if (t->kind == TY_NUMBER || t->kind == TY_INT || t->kind == TY_STRING || t->kind == TY_BOOLEAN || t->kind == TY_ENUM) return true;
    if (t->kind != TY_UNION) return false;
    for (int i = 0; i < t->nparams; i++) if (!prim_like(t->params[i])) return false;
    return true;
}
static bool missing_get(Expr *l);
static Expr *copy_target(Expr *x);
static void check_loose_eq(Expr *e);
static bool rereadable(Expr *e);
static bool pure_read(Expr *e);
static bool static_bool(Expr *e, bool *val);
static Expr *expr_copy(Expr *e);
/* rec[k] / rec.k (a Record's get, V | null) read as an object: as in JavaScript, a missing entry is a TypeError */
static Expr *rec_get_nn(Expr *x) {
    if (!(x->type && x->type->kind == TY_NULLABLE && x->kind == E_MEMBER && x->is_call && strcmp(x->str, "get") == 0 &&
          x->lhs->type && x->lhs->type->kind == TY_MAP && x->lhs->type->record)) return x;
    Expr *nn = xs_alloc(sizeof *nn);
    nn->kind = E_AS; nn->op = TK_NOT; nn->pos = x->pos; nn->field = -1;
    nn->lhs = x;
    nn->type = x->type->elem;
    return nn;
}
/* x, this, x.a.b before checking: a path with no call in it, safe to check twice */
static bool plain_path(Expr *e) { return e->kind == E_IDENT || e->kind == E_THIS || (e->kind == E_MEMBER && !e->is_call && e->op != TK_QDOT && plain_path(e->lhs)); }
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

static void check_expr(Expr *e, Type *expected);
static void settle(Expr *e);
static void expect_type(Expr *e, Type *want, const char *what);
static void check_type_supported(Pos pos, Type *t);
static bool is_bytes(Type *t);
static bool is_f64(Type *t);
/* [a, ...xs, b]: each spread source an array (a string's code points, a Set's values, a Map's entries, the bytes
   of a Uint8Array or the numbers of a Float64Array), its elements of the array's element type */
static bool shape_copy_array(Expr *e, Type *want);
static bool plain_data(Class *c);
static void check_spread_lit(Expr *e, Type *elem) {
    Type **ets = xs_alloc(sizeof(Type *) * (size_t)(e->args.len + 1));
    for (int i = 0; i < e->args.len; i++) {
        Expr *a = e->args.items[i];
        if (!(a->kind == E_UNARY && a->op == TK_ELLIPSIS)) {
            check_expr(a, elem);
            ets[i] = a->type;
            continue;
        }
        check_expr(a->lhs, elem && !is_numeric(elem) ? ty_array_of(elem) : NULL);
        Type *st = a->lhs->type;
        const char *m = st->kind == TY_STRING ? "split" : st->kind == TY_SET ? "values" : st->kind == TY_MAP ? "entries" : NULL;
        if (m) { /* an array of its own */
            Expr *call = xs_alloc(sizeof *call);
            call->kind = E_MEMBER;
            call->pos = a->lhs->pos;
            call->lhs = a->lhs;
            call->str = (char *)m;
            call->is_call = true;
            call->field = -1;
            if (st->kind == TY_STRING) {
                Expr *sep = xs_alloc(sizeof *sep);
                sep->kind = E_STRING;
                sep->pos = a->lhs->pos;
                sep->str = "";
                expr_list_push(&call->args, sep);
            }
            a->lhs = call;
            check_expr(a->lhs, NULL);
            st = a->lhs->type;
        }
        if (is_bytes(st)) ets[i] = ty_int;
        else if (is_f64(st)) ets[i] = ty_number;
        else if (st->kind == TY_ARRAY) ets[i] = st->elem;
        else xs_error(XS8101, a->lhs->pos, "spread an array, a string, a Set, a Map, a Uint8Array or a Float64Array", "cannot spread %s", ty_name(st));
        a->type = st;
    }
    if (!elem && e->args.len > 1) { /* [{ term: .. }, ...(c ? [{ range: .. }] : [])]: objects of several shapes, as JSON */
        bool objs = true, differ = false;
        bool ju = json_ok_unknown;
        json_ok_unknown = true;
        for (int i = 0; i < e->args.len && objs; i++) {
            Type *t = ets[i];
            if (t->kind != TY_OBJECT || t->cls->builtin || t->cls->is_tuple || !json_ok(t, 0)) objs = false;
            else if (i && !ty_equal(t, ets[0])) differ = true;
        }
        json_ok_unknown = ju;
        if (objs && differ) { /* [...seed (CreateScreenDto[]), ...page.items (ScreenResponseDto[])]: the one shape every element
                                 copies into (the fields they share), else an object */
            for (int c = 0; c < e->args.len && !elem; c++) {
                bool all = true;
                for (int i = 0; i < e->args.len && all; i++) all = ty_equal(ets[i], ets[c]) || shape_ok(ets[i], ets[c]);
                if (all) elem = ets[c];
            }
            if (!elem) elem = ty_json;
        }
    }
    if (!elem && e->args.len > 1) { /* [{ term: .. }, ...filtros] with filtros: Record<string, unknown>[]: the Record (objects copied in) */
        Type *rec = NULL;
        bool fits = true;
        for (int i = 0; i < e->args.len; i++) {
            Type *t = ets[i];
            if (t->kind == TY_MAP && t->record && t->key->kind == TY_STRING && t->elem->kind == TY_UNKNOWN) { if (!rec) rec = t; }
            else if (!(t->kind == TY_OBJECT && plain_data(t->cls))) fits = false;
        }
        if (rec && fits) elem = rec;
    }
    if (!elem) { /* as TypeScript: numbers win over ints; else the first element's type */
        for (int i = 0; i < e->args.len && !elem; i++) if (ets[i]->kind == TY_NUMBER) elem = ty_number;
        for (int i = 0; i < e->args.len && !elem; i++) if (ets[i]->kind == TY_INT) elem = ty_int;
        if (!elem) {
            Expr *a0 = e->args.items[0];
            if (!(a0->kind == E_UNARY && a0->op == TK_ELLIPSIS)) { settle(a0); ets[0] = a0->type; }
            elem = ets[0];
        }
    }
    if (elem && elem->kind != TY_NULLABLE && ty_is_heap(elem)) { /* [`a=${x}`, c ? `b=${y}` : undefined]: (string | undefined)[], as
                                                                    TypeScript types it (not a null check on each element) */
        bool nul = false;
        for (int i = 0; i < e->args.len; i++) {
            Expr *a = e->args.items[i];
            if (a->kind == E_UNARY && a->op == TK_ELLIPSIS) continue;
            Type *t = ets[i];
            if (t && (t->kind == TY_NULL || ty_is_undef(t) || (t->kind == TY_NULLABLE && ty_equal(t->elem, elem)))) nul = true;
        }
        if (nul && nullable_ok(elem)) elem = ty_nullable_of(elem);
    }
    for (int i = 0; i < e->args.len; i++) {
        Expr *a = e->args.items[i];
        if (a->kind == E_UNARY && a->op == TK_ELLIPSIS) {
            bool ok = ty_equal(ets[i], elem) || (ets[i]->kind == TY_INT && elem->kind == TY_NUMBER && (is_bytes(a->type) || is_f64(a->type)));
            if (!ok && a->lhs->type->kind == TY_ARRAY && (elem_widens(ets[i], elem) || (elem->kind == TY_JSON && ets[i]->kind == TY_OBJECT))) {
                arr_convert(a->lhs, elem); /* ...names where (string | number)[] goes: a copy of the wider type */
                a->type = a->lhs->type;
                ok = true;
            }
            if (!ok && a->lhs->type->kind == TY_ARRAY && ets[i]->kind == TY_UNKNOWN && !ty_is_any(ets[i]) && tsc_checked() &&
                (elem->kind == TY_STRING || elem->kind == TY_NUMBER || elem->kind == TY_INT || elem->kind == TY_BOOLEAN)) {
                arr_convert(a->lhs, elem); /* ...screenScopes (a distinct() result, unknown[]) where string[] goes: each one checked */
                a->type = a->lhs->type;
                ok = true;
            }
            if (!ok && a->lhs->type->kind == TY_ARRAY && shape_copy_array(a->lhs, ty_array_of(elem))) { a->type = a->lhs->type; ok = true; } /* ...rows where shapes with their fields go: copies */
            if (!ok) xs_error(XS8141, a->pos, NULL, "the spread elements are %s, but the array holds %s", ty_name(ets[i]), ty_name(elem));
        } else expect_type(a, elem, "array element");
    }
    e->type = ty_array_of(elem);
    check_type_supported(e->pos, e->type);
}

static void coerce_lit(Expr *e, Type *want) {
    if (e->type->kind != TY_LITINT) return;
    e->type = want;
    if (e->kind == E_BINARY || e->kind == E_UNARY || e->kind == E_AWAIT) {
        coerce_lit(e->lhs, want);
        if (e->rhs) coerce_lit(e->rhs, want);
    }
    if (e->kind == E_COND && e->args.len == 2) { coerce_lit(e->args.items[0], want); coerce_lit(e->args.items[1], want); }
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
/* a target that holds unknown values (Record<string, unknown>[], a shape with unknown fields): decoded by what they hold */
static bool json_target_u(Type *t) {
    bool ju = json_ok_unknown;
    json_ok_unknown = true;
    bool ok = (t->kind == TY_OBJECT || t->kind == TY_ARRAY || (t->kind == TY_MAP && t->record)) && json_ok(t, 0);
    json_ok_unknown = ju;
    return ok;
}

/* T -> unknown: references as they are, primitives boxed */
static Expr *pure_copy(Expr *x);
static void to_unknown(Expr *e) {
    if (e->type->kind == TY_LITINT) coerce_lit(e, ty_int);
    if (e->type->kind == TY_FUNC && e->type->ret && !ty_is_heap(e->type->ret) && e->type->ret->kind != TY_VOID &&
        e->type->ret->kind != TY_UNKNOWN) {
        /* default: Date.now (a () => number) kept as an unknown: held as () => unknown (its result boxed), the one way a
           call through (d as () => unknown)() reads it */
        Expr *callee = pure_copy(e);
        if (!callee && e->kind == E_ARROW && e->arrow->ncaptures == 0) { /* (a thunk with nothing captured: Date.now's) */
            callee = expr_copy(e);
            callee->prechecked = true;
        }
        if (callee && !e->type->async_ret) {
            Type *et = e->type;
            Pos pos = e->pos;
            Func *f = xs_alloc(sizeof *f);
            f->pos = pos; f->name = "arrow";
            f->params = xs_alloc(sizeof(Param) * (size_t)(et->nparams + 1));
            f->nparams = et->nparams;
            f->ret = ty_unknown;
            Expr *call = xs_alloc(sizeof *call);
            call->kind = E_CALL; call->pos = pos; call->field = -1; call->lhs = callee;
            for (int i = 0; i < et->nparams; i++) {
                memset(&f->params[i], 0, sizeof(Param));
                f->params[i].name = xs_sprintf("__fu%d", i);
                f->params[i].type = et->params[i];
                Expr *id = xs_alloc(sizeof *id);
                id->kind = E_IDENT; id->pos = pos; id->field = -1; id->str = f->params[i].name;
                expr_list_push(&call->args, id);
            }
            Stmt *b = xs_alloc(sizeof *b);
            b->kind = S_BLOCK; b->pos = pos;
            Stmt *r = xs_alloc(sizeof *r);
            r->kind = S_RETURN; r->pos = pos; r->expr = call;
            stmt_list_push(&b->stmts, r);
            f->body = b;
            Arrow *a = xs_alloc(sizeof *a);
            a->pos = pos; a->fn = f; a->expr_body = true;
            memset(e, 0, sizeof *e);
            e->kind = E_ARROW; e->pos = pos; e->field = -1; e->arrow = a;
            check_expr(e, NULL);
        }
    }
    if (e->type->kind == TY_NULL) { e->type = ty_unknown; return; }
    Type *t = e->type;
    bool nul = t->kind == TY_NULLABLE && !ty_is_heap(t);
    Type *b = nul ? t->elem : t;
    const char *fn = b->kind == TY_NUMBER ? "xs_box_num" : b->kind == TY_BOOLEAN ? "xs_box_bool"
                   : (b->kind == TY_INT || (b->kind == TY_ENUM && !b->en->is_string)) ? "xs_box_int" : NULL;
    if (!fn) { /* a reference: the same pointer */
        if (e->kind == E_ARRAY_LIT || e->kind == E_OBJLIT || e->kind == E_NEW_MAP || e->kind == E_BINARY || (e->kind == E_MEMBER && e->is_call && e->builtin.cfn)) {
            /* a literal is emitted from its own type, and so is a + b (a string concatenation): keep it under a
               cast ([...] or s + "!" where a union or unknown goes) */
            Expr *v = expr_copy(e);
            memset(e, 0, sizeof *e);
            e->kind = E_UPCAST;
            e->pos = v->pos;
            e->lhs = v;
        }
        e->type = ty_unknown;
        return;
    }
    Expr *v = expr_copy(e);
    memset(e, 0, sizeof *e);
    e->kind = E_CALL;
    e->pos = v->pos;
    e->builtin.cfn = nul ? (b->kind == TY_NUMBER ? "xs_box_num_or_null" : b->kind == TY_BOOLEAN ? "xs_box_bool_or_null" : "xs_box_int_or_null") : fn;
    e->type = ty_unknown;
    expr_list_push(&e->args, v);
}
/* ---------- any: the dynamic path (MANIFESTO, "Ponto importante: any") ----------
   An any is an unknown (a reference; primitives boxed) that is read and written by name: x.f, x[k], x.f = v,
   k in x go to the runtime (xs_dyn_*, through the field table of the object's class), and an any where a T goes
   is checked at run time, as `x as T`. Typed code is not touched: these rules apply only where a value's type
   is any. */
static Expr *dyn_lvalue; /* the target of the assignment being checked: xs[i] = v of an any[] stays an element */
bool xs_any_used; /* the program reads or writes an any by name: the emitter gives every class its field table */
static Field *find_field(Class *c, const char *name, int *index);
static Func *find_method(Class *c, const char *name);
static Expr *dyn_key(Pos pos, const char *s) {
    Expr *k = xs_alloc(sizeof *k);
    k->kind = E_STRING; k->pos = pos; k->field = -1; k->str = (char *)s; k->type = ty_string; k->lit_id = intern(s);
    return k;
}
/* e becomes cfn(a, b, c) (the arguments already checked), a runtime call of type t */
static void dyn_call(Expr *e, const char *cfn, Type *t, Expr *a, Expr *b, Expr *c) {
    Pos pos = e->pos;
    memset(e, 0, sizeof *e);
    e->kind = E_CALL; e->pos = pos; e->field = -1; e->builtin.cfn = cfn; e->type = t;
    if (a) expr_list_push(&e->args, a);
    if (b) expr_list_push(&e->args, b);
    if (c) expr_list_push(&e->args, c);
    xs_any_used = true;
}
/* x becomes cfn(x) */
static void dyn_wrap(Expr *x, const char *cfn, Type *t) { Expr *v = xs_alloc(sizeof *v); *v = *x; dyn_call(x, cfn, t, v, NULL, NULL); }
static void to_unknown(Expr *e);
/* a checked value as an any: references as they are, primitives boxed */
static void to_any(Expr *e) {
    if (ty_is_any(e->type)) return;
    if (e->type->kind != TY_UNKNOWN) to_unknown(e);
    e->type = ty_any;
}
static bool is_big(Type *t);
/* s + x with x not a string: String(x), as JavaScript's + does (null prints "null"); false if x has no such text */
static bool concat_operand(Expr *x) {
    settle(x);
    Type *t = x->type;
    if (t->kind == TY_STRING) return true;
    if (t->kind == TY_LITINT) { coerce_lit(x, ty_int); t = x->type; }
    const char *cfn = NULL;
    if (t->kind == TY_INT) cfn = "xs_str_from_int";
    else if (t->kind == TY_NUMBER) cfn = "xs_str_from_num";
    else if (t->kind == TY_BOOLEAN) cfn = "xs_str_from_bool";
    else if (is_big(t)) cfn = "xs_big_to_string";
    else if (t->kind == TY_ENUM) cfn = t->en->is_string ? NULL : "xs_str_from_int";
    else if (t->kind == TY_UNKNOWN || t->kind == TY_UNION || (t->kind == TY_NULLABLE && t->elem->kind != TY_OBJECT)) {
        if (t->kind == TY_NULLABLE) to_unknown(x);
        cfn = "xs_dyn_str"; xs_any_used = true;
    }
    else if (t->kind == TY_OBJECT && (is_error_class(t->cls) || (t->cls->builtin && strcmp(t->cls->name, "Error") == 0))) cfn = "xs_Error_to_string";
    if (t->kind == TY_ENUM && t->en->is_string) return true;
    if (!cfn) return false;
    if (t->kind != TY_ENUM) /* XB-127: String(x) written for the reader */
        xs_warn_rule(XS2106, x->pos, "write it as a template (`a ${x}`) or convert explicitly: String(x)", "'+' turns %s into text, as JavaScript does", ty_name(t));
    if (strcmp(cfn, "xs_dyn_str") == 0) dyn_wrap(x, cfn, ty_string);
    else wrap_call(x, cfn, ty_string);
    return true;
}
static Type *record_of_any(void) {
    static Type *r;
    if (!r) { r = xs_alloc(sizeof *r); *r = *ty_map_of(ty_string, ty_any); r->record = true; }
    return r;
}
/* an any where a T goes: checked at run time (`x as T`); false when T cannot be checked */
static bool unknown_target(Type *t);
static bool dyn_to(Expr *e, Type *want) {
    Type *w = want->kind == TY_NULLABLE ? want->elem : want;
    if (w->kind == TY_UNION || w->kind == TY_UNKNOWN) { e->type = want; return true; } /* held the same way (a union is not checked) */
    if (!unknown_target(want)) return false;
    Expr *v = xs_alloc(sizeof *v);
    *v = *e;
    Pos pos = e->pos;
    memset(e, 0, sizeof *e);
    e->kind = E_AS; e->pos = pos; e->field = -1; e->lhs = v; e->elem_type = want; e->type = want;
    return true;
}
static bool unknown_target(Type *t) {
    Type *b = t->kind == TY_NULLABLE ? t->elem : t;
    switch (b->kind) {
    case TY_INT: case TY_NUMBER: case TY_BOOLEAN: case TY_STRING: case TY_ARRAY: case TY_MAP: case TY_SET: case TY_FUNC: return true;
    case TY_ENUM: return true;
    case TY_OBJECT: return !b->cls->builtin || strcmp(b->cls->name, "Error") == 0 || strcmp(b->cls->name, "symbol") == 0 || (b->cls->ctor && b->cls->ctor->is_native) ||
                           strcmp(b->cls->name, "Date") == 0 || strcmp(b->cls->name, "Buffer") == 0 || strcmp(b->cls->name, "Uint8Array") == 0;
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
    if (!m && gb->kind == TY_ARRAY && g->kind != TY_NULLABLE) /* string[] where Record<string, unknown> | unknown[] goes */
        for (int i = 0; i < u->nparams && !m; i++)
            if (u->params[i]->kind == TY_ARRAY && elem_widens(gb->elem, u->params[i]->elem)) { arr_convert(e, u->params[i]->elem); m = u->params[i]; }
    if (!m) return false;
    if (g->kind == TY_LITINT) coerce_lit(e, m);
    to_unknown(e);
    e->type = want;
    return true;
}
/* an element of X[] can go where a U goes, U being wider (unknown, a union holding X, number for int, or an array
   of such): TypeScript accepts X[] where U[] goes */
static bool elem_widens(Type *x, Type *u) {
    if (ty_equal(x, u) || x->kind == TY_VOID || x->kind == TY_NULL) return false;
    Type *ub = u->kind == TY_NULLABLE ? u->elem : u;
    Type *xb = x->kind == TY_NULLABLE ? x->elem : x;
    if (x->kind == TY_NULLABLE && u->kind != TY_NULLABLE && ub->kind != TY_UNKNOWN) return false;
    if (ub->kind == TY_UNKNOWN && !ty_is_any(ub)) return true;
    if (ub->kind == TY_NUMBER && xb->kind == TY_INT && !xb->tname) return true;
    if (ub->kind == TY_UNION && xb->kind != TY_UNION && union_member_for(ub, xb, NULL)) return true;
    if (ub->kind == TY_ARRAY && xb->kind == TY_ARRAY) return elem_widens(xb->elem, ub->elem);
    return false;
}
/* X[] where U[] goes (elem_widens): a copy, each element converted (xs.map((v: X): U => v)) */
static void arr_convert(Expr *e, Type *to) {
    Type *from = e->type->elem;
    Expr *orig = xs_alloc(sizeof *orig);
    *orig = *e;
    Pos pos = e->pos;
    Func *f = xs_alloc(sizeof *f);
    f->pos = pos; f->name = "arrow";
    f->params = xs_alloc(sizeof(Param));
    memset(f->params, 0, sizeof(Param));
    f->params[0].name = "__av";
    f->params[0].type = from;
    f->nparams = 1;
    f->ret = to;
    Expr *id = xs_alloc(sizeof *id);
    id->kind = E_IDENT; id->pos = pos; id->field = -1; id->str = "__av";
    Stmt *b = xs_alloc(sizeof *b);
    b->kind = S_BLOCK; b->pos = pos;
    Stmt *r = xs_alloc(sizeof *r);
    r->kind = S_RETURN; r->pos = pos; r->expr = id;
    if (from->kind == TY_UNKNOWN && !ty_is_any(from)) { /* unknown[] into string[] (tsc-checked: a cast, checked per element) */
        Expr *as = xs_alloc(sizeof *as);
        as->kind = E_AS; as->pos = pos; as->field = -1; as->lhs = id; as->elem_type = to;
        r->expr = as;
    }
    stmt_list_push(&b->stmts, r);
    f->body = b;
    Arrow *a = xs_alloc(sizeof *a);
    a->pos = pos; a->fn = f; a->expr_body = true;
    Expr *ar = xs_alloc(sizeof *ar);
    ar->kind = E_ARROW; ar->pos = pos; ar->field = -1; ar->arrow = a;
    memset(e, 0, sizeof *e);
    e->kind = E_MEMBER; e->pos = pos; e->field = -1; e->lhs = orig; e->str = "map"; e->is_call = true;
    expr_list_push(&e->args, ar);
    orig->prechecked = true;
    check_expr(e, ty_array_of(to));
}
/* the key of a Map<number, V> (an element of a Set<number>): an int, made from a number that must be whole */
static Type *ty_numkey(void) {
    static Type *t;
    if (!t) { t = xs_alloc(sizeof *t); *t = *ty_int; t->tname = "numkey"; }
    return t;
}
static bool is_numkey(Type *t) { return t && t->kind == TY_INT && t->tname && strcmp(t->tname, "numkey") == 0; }
static void wrap_call(Expr *e, const char *cfn, Type *type) {
    Expr *a = xs_alloc(sizeof *a);
    *a = *e;
    memset(e, 0, sizeof *e);
    e->kind = E_CALL; e->pos = a->pos; e->field = -1; e->type = type;
    e->builtin.cfn = cfn;
    expr_list_push(&e->args, a);
}
/* ---- Promise values (XB-106) ----
   An async call has its value when it returns (its waits park the caller): await f(), a statement f(), return f().
   Where the program keeps the promise instead (a Promise<T> field, variable or parameter, const p = f(), p.then...)
   the call becomes an E_SPAWN: it runs on a coroutine of its own, and the value is a promise (runtime/xisto_net.h). */
static int pr_kind(Type *t) { /* the runtime's kind of a promise's value: 0 number, 1 int, 2 boolean, 3 reference, 4 none */
    if (!t || t->kind == TY_VOID || t->kind == TY_NULL) return 4;
    Type *b = ty_strip_null(t);
    switch (b->kind) {
    case TY_NUMBER: return 0;
    case TY_INT: case TY_LITINT: return 1;
    case TY_BOOLEAN: return 2;
    case TY_ENUM: return b->en->is_string ? 3 : 1;
    default: return 3;
    }
}
static const char *pr_sfx(int k) { return k == 0 ? "d" : k == 1 ? "i" : k == 2 ? "b" : k == 3 ? "p" : "v"; }
static bool pr_is_call(Expr *e) { return e->kind == E_CALL || (e->kind == E_MEMBER && e->is_call); }
static bool pr_async_call(Expr *e) {
    if (!pr_is_call(e)) return false;
    if (e->func && (e->func->ret_promise || e->func->is_async)) return true;
    if (e->kind == E_CALL && !e->func && !e->builtin.cfn && e->lhs && e->lhs->type && e->lhs->type->kind == TY_FUNC && e->lhs->type->async_ret) return true;
    return false;
}
static void pr_spawn(Expr *e) { /* the call e (checked), kept as a promise */
    Expr *c = xs_alloc(sizeof *c);
    *c = *e;
    memset(e, 0, sizeof *e);
    e->kind = E_SPAWN; e->pos = c->pos; e->field = -1; e->lhs = c;
    e->type = ty_promise_of(c->type);
    prog->uses_gen = true;
    if (getenv("XISTO_SPAWN_LOG")) fprintf(stderr, "spawn: %s:%d:%d\n", xs_file_name ? xs_file_name : "?", c->pos.line, c->pos.col);
}
static void pr_resolved(Expr *e) { /* a value already computed, as a fulfilled promise */
    Type *t = e->type;
    const char *sfx = pr_sfx(pr_kind(t));
    /* (a void or null value: xs_promise_resolved_v takes none; _vx evaluates it and drops it) */
    wrap_call(e, xs_sprintf("xs_promise_resolved_%s", strcmp(sfx, "v") == 0 ? "vx" : sfx), ty_promise_of(t));
    prog->uses_gen = true;
}
static void pr_await(Expr *e) { /* await p, p a promise: its value (or its rejection thrown) */
    Type *t = e->type->elem;
    wrap_call(e, xs_sprintf("xs_promise_await_%s", pr_sfx(pr_kind(t))), t->kind == TY_NULL ? ty_void : t);
}
/* pr_awaited: await x: x, while it is checked (Promise.resolve(v) there is v itself) */

/* the kind a then/catch/finally callback returns, for the runtime (5: a promise, awaited before the next one) */
static int pr_cb_kind(Type **rt) {
    if ((*rt)->kind == TY_PROMISE) { *rt = (*rt)->elem; return 5; }
    return pr_kind(*rt);
}
static bool pr_null_body(Expr *a) { /* () => undefined / () => null */
    if (a->kind != E_ARROW || !a->arrow->expr_body) return false;
    Stmt *b = a->arrow->fn->body;
    Expr *x = b && b->stmts.len ? b->stmts.items[b->stmts.len - 1]->expr : NULL;
    if (x && x->kind == E_IDENT && strcmp(x->str, "undefined") == 0 && !lookup("undefined")) { /* (as null: XB-118 rule 2) */
        xs_warn_rule(XS2101, x->pos, "write null: xisto has one empty value", "'undefined' is deprecated in xisto: it is read as null");
        x->kind = E_NULL;
    }
    return x && x->kind == E_NULL;
}
static Type *pr_err_type(Expr *a) { /* what a rejection handler takes: Error (or the unknown/any it declares) */
    if (a->kind == E_ARROW && a->arrow->fn->nparams >= 1 && a->arrow->fn->params[0].type) {
        Type *t = resolve(a->arrow->fn->params[0].type, a->pos);
        a->arrow->fn->params[0].type = t;
        if (t->kind == TY_UNKNOWN) return t;
    }
    return ty_object_of(find_class("Error"));
}
static Expr *pr_null_fn(Pos pos) {
    Expr *n = xs_alloc(sizeof *n);
    n->kind = E_NULL; n->pos = pos; n->field = -1;
    n->type = ty_nullable_of(ty_func(NULL, 0, ty_void));
    return n;
}
static bool st_has_return(Stmt *s) { /* a return in this body (not in the arrows inside it, which are expressions) */
    if (!s) return false;
    switch (s->kind) {
    case S_RETURN: return true;
    case S_BLOCK: for (int i = 0; i < s->stmts.len; i++) if (st_has_return(s->stmts.items[i])) return true; return false;
    case S_IF: return st_has_return(s->body) || st_has_return(s->else_body);
    case S_WHILE: case S_FOR: case S_FOROF: return st_has_return(s->body);
    case S_TRY: return st_has_return(s->body) || st_has_return(s->else_body) || st_has_return(s->for_init);
    case S_SWITCH: for (int i = 0; i < s->ncases; i++) if (st_has_return(s->cases[i].body)) return true; return false;
    default: return false;
    }
}
static bool st_always_throws(Stmt *s) {
    if (!s) return false;
    switch (s->kind) {
    case S_THROW: return true;
    case S_BLOCK: for (int i = 0; i < s->stmts.len; i++) if (st_always_throws(s->stmts.items[i])) return true; return false;
    case S_IF: return s->else_body && st_always_throws(s->body) && st_always_throws(s->else_body);
    default: return false;
    }
}
/* (err) => { ...; throw err; }: a handler that never returns (TypeScript's never), whatever the promise gives */
static bool pr_never_returns(Expr *a) {
    if (a->kind != E_ARROW || a->arrow->expr_body) return false;
    Stmt *b = a->arrow->fn->body;
    return b && !st_has_return(b) && st_always_throws(b);
}
static bool pr_discard; /* checking `p.then(...)` / `p.catch(...)` as a statement: the promise it makes is not used */
/* p.then(ok, err?) / p.catch(err) / p.finally(fn): a new promise (runtime/xisto_net.h, xs_promise_then) */
static void check_pr_member(Expr *e, Type *expected) {
    (void)expected;
    bool discard = pr_discard;
    pr_discard = false;
    Type *T = e->lhs->type->elem;
    const char *m = e->str;
    int how = strcmp(m, "finally") == 0 ? 1 : 0;
    Expr *ok = NULL, *err = NULL;
    if (strcmp(m, "then") == 0) {
        if (e->args.len < 1 || e->args.len > 2) xs_error(XS8062, e->pos, NULL, "then takes one or two functions");
        ok = e->args.items[0];
        err = e->args.len > 1 ? e->args.items[1] : NULL;
    } else {
        if (e->args.len != 1) xs_error(XS8062, e->pos, NULL, "%s takes one function", m);
        if (how) ok = e->args.items[0]; else err = e->args.items[0];
    }
    Type *R = T, *okr = NULL, *errr = NULL;
    int okk = 4, errk = 4;
    if (ok) {
        Type **ps = xs_alloc(sizeof(Type *));
        ps[0] = T;
        int np = how || T->kind == TY_VOID ? 0 : 1;
        check_expr(ok, ty_func(ps, np, NULL));
        if (ok->type->kind != TY_FUNC) xs_error(XS8018, ok->pos, NULL, "the argument of %s must be a function, but this is %s", m, ty_name(ok->type));
        okr = ok->type->ret;
        okk = pr_cb_kind(&okr);
        if (!how) R = okr;
    }
    if (err) {
        Type **ps = xs_alloc(sizeof(Type *));
        ps[0] = pr_err_type(err);
        Type *want_r = NULL;
        if (pr_null_body(err)) want_r = R->kind == TY_VOID ? ty_void : nullable_ok(ty_strip_null(R)) ? ty_nullable_of(ty_strip_null(R)) : NULL;
        else if (!discard && R->kind != TY_VOID && err->kind == E_ARROW && err->arrow->expr_body && !err->arrow->fn->ret) want_r = R; /* .catch(() => 0): the promise's type */
        bool never = pr_never_returns(err);
        check_expr(err, ty_func(ps, 1, want_r));
        if (err->type->kind != TY_FUNC) xs_error(XS8018, err->pos, NULL, "the argument of %s must be a function, but this is %s", m, ty_name(err->type));
        errr = err->type->ret;
        errk = pr_cb_kind(&errr);
        if (never && errr->kind == TY_VOID) errr = R; /* it returns nothing because it never returns */
        if (!ty_equal(errr, R) && discard) R = ty_void; /* the promise it makes is dropped: no one type needed */
        else if (!ty_equal(errr, R)) {
            if (R->kind == TY_VOID || errr->kind == TY_VOID) {
                if (R->kind != TY_VOID || errr->kind != TY_VOID) xs_error(XS8134, err->pos, "give both one type", "the promise gives %s, and its rejection handler %s", ty_name(R), ty_name(errr));
            } else if (errr->kind == TY_NULLABLE && ty_equal(errr->elem, R)) R = errr;
            else if (R->kind == TY_NULLABLE && ty_equal(R->elem, errr)) {}
            else xs_error(XS8134, err->pos, "give both one type (an interface or a union)", "the promise gives %s, and its rejection handler %s", ty_name(R), ty_name(errr));
        }
    }
    Expr *p = e->lhs;
    e->kind = E_CALL;
    e->lhs = NULL;
    e->is_call = false;
    e->str = NULL;
    e->func = NULL;
    e->builtin.cfn = "xs_promise_then";
    e->args = (ExprList){0};
    expr_list_push(&e->args, p);
    expr_list_push(&e->args, num_lit(e->pos, how, true));
    expr_list_push(&e->args, ok ? ok : pr_null_fn(e->pos));
    expr_list_push(&e->args, num_lit(e->pos, okk, true));
    expr_list_push(&e->args, err ? err : pr_null_fn(e->pos));
    expr_list_push(&e->args, num_lit(e->pos, errk, true));
    expr_list_push(&e->args, num_lit(e->pos, pr_kind(R), true));
    e->type = ty_promise_of(R);
    prog->uses_gen = true;
}
/* Promise<X> where Promise<Y> goes, one of them unknown (a cache of promises of different types, XB-135):
   p.then((v: X): Y => v as Y), a new promise that settles with p (its value boxed into, or checked out of, the
   unknown). It is another promise object: p === converted is false, and it settles one microtask later. */
static void pr_convert(Expr *e, Type *to) {
    Type *from = e->type->elem;
    Expr *orig = xs_alloc(sizeof *orig);
    *orig = *e;
    Pos pos = e->pos;
    Func *f = xs_alloc(sizeof *f);
    f->pos = pos; f->name = "arrow";
    f->params = xs_alloc(sizeof(Param));
    memset(f->params, 0, sizeof(Param));
    f->params[0].name = "__pv";
    f->params[0].type = from;
    f->nparams = 1;
    f->ret = to;
    Expr *id = xs_alloc(sizeof *id);
    id->kind = E_IDENT; id->pos = pos; id->field = -1; id->str = "__pv";
    Expr *as = xs_alloc(sizeof *as);
    as->kind = E_AS; as->pos = pos; as->field = -1; as->lhs = id; as->elem_type = to;
    Stmt *b = xs_alloc(sizeof *b);
    b->kind = S_BLOCK; b->pos = pos;
    Stmt *r = xs_alloc(sizeof *r);
    r->kind = to->kind == TY_VOID ? S_EXPR : S_RETURN; r->pos = pos; r->expr = to->kind == TY_VOID ? id : as;
    stmt_list_push(&b->stmts, r);
    f->body = b;
    Arrow *a = xs_alloc(sizeof *a);
    a->pos = pos; a->fn = f; a->expr_body = true;
    Expr *ar = xs_alloc(sizeof *ar);
    ar->kind = E_ARROW; ar->pos = pos; ar->field = -1; ar->arrow = a;
    memset(e, 0, sizeof *e);
    e->kind = E_MEMBER; e->pos = pos; e->field = -1; e->lhs = orig; e->str = "then"; e->is_call = true;
    expr_list_push(&e->args, ar);
    check_pr_member(e, NULL);
}
static bool pr_convertible(Type *from, Type *to) {
    return !ty_equal(from, to) && (from->kind == TY_UNKNOWN || to->kind == TY_UNKNOWN) && from->kind != TY_VOID && to->kind != TY_VOID;
}
/* new Promise<T>((resolve, reject) => ...) */
static void check_pr_new(Expr *e, Type *expected) {
    Type *T = NULL;
    if (e->ntargs == 1) T = resolve(e->targs[0], e->pos);
    else if (expected && ty_strip_null(expected)->kind == TY_PROMISE) T = ty_strip_null(expected)->elem;
    else if (expected && expected->kind != TY_UNKNOWN) T = expected; /* return new Promise(...) where the function gives T */
    else T = ty_void;
    if (e->args.len != 1) xs_error(XS8062, e->pos, NULL, "new Promise takes one function: (resolve, reject) => { ... }");
    Type **rp = xs_alloc(sizeof(Type *)), **jp = xs_alloc(sizeof(Type *)), **ep = xs_alloc(sizeof(Type *) * 2);
    rp[0] = T;
    jp[0] = ty_object_of(find_class("Error"));
    ep[0] = ty_func(rp, T->kind == TY_VOID ? 0 : 1, ty_void);
    ep[1] = ty_func(jp, 1, ty_void);
    Expr *x = e->args.items[0];
    check_expr(x, ty_func(ep, 2, ty_void));
    if (x->type->kind != TY_FUNC) xs_error(XS8018, x->pos, NULL, "new Promise takes a function, not %s", ty_name(x->type));
    e->kind = E_CALL;
    e->str = NULL;
    e->cls = NULL;
    e->ntargs = 0;
    e->builtin.cfn = "xs_promise_new";
    e->args = (ExprList){0};
    expr_list_push(&e->args, num_lit(e->pos, pr_kind(T), true));
    expr_list_push(&e->args, x);
    e->type = ty_promise_of(T);
    prog->uses_gen = true;
}
/* Number, String, Boolean, Date as values. Where a function goes (xs.map(Number), .filter(Boolean)): the
   conversion, as an arrow (v) => Number(v). Elsewhere (type: Number in a Swagger option, @Type(() => Date)):
   the ClassInfo xisto:reflect gives each, as for a decorated class. */
static bool conv_name(Expr *e) {
    return e && e->kind == E_IDENT && e->str && (strcmp(e->str, "Number") == 0 || strcmp(e->str, "String") == 0 || strcmp(e->str, "Boolean") == 0) && !lookup(e->str);
}
/* Number as a callback, made into (__conv) => Number(__conv) in place (not checked yet) */
static void conv_arrow(Expr *e) {
    const char *n = e->str;
    {
        Expr *v = xs_alloc(sizeof *v);
        v->kind = E_IDENT; v->pos = e->pos; v->field = -1; v->str = "__conv";
        Expr *callee = xs_alloc(sizeof *callee);
        callee->kind = E_IDENT; callee->pos = e->pos; callee->field = -1; callee->str = (char *)n;
        Expr *call = xs_alloc(sizeof *call);
        call->kind = E_CALL; call->pos = e->pos; call->field = -1; call->is_call = true; call->lhs = callee;
        expr_list_push(&call->args, v);
        if (strcmp(n, "Boolean") == 0) { /* Boolean(x): its truthiness, as !!x */
            Expr *n1 = xs_alloc(sizeof *n1), *n2 = xs_alloc(sizeof *n2);
            n1->kind = E_UNARY; n1->op = TK_NOT; n1->pos = e->pos; n1->field = -1; n1->lhs = v;
            n2->kind = E_UNARY; n2->op = TK_NOT; n2->pos = e->pos; n2->field = -1; n2->lhs = n1;
            call = n2;
        }
        Expr *arrow = thunk_of(call);
        Func *f = arrow->arrow->fn;
        f->params = xs_alloc(sizeof(Param));
        memset(f->params, 0, sizeof(Param));
        f->params[0].name = "__conv";
        f->nparams = 1;
        *e = *arrow;
    }
}
static bool builtin_ctor_value(Expr *e, Type *expected) {
    const char *n = e->str;
    bool conv = conv_name(e);
    if (!conv && strcmp(n, "Date") != 0) return false;
    if (conv && expected && expected->kind == TY_FUNC && expected->nparams >= 1) {
        conv_arrow(e);
        check_expr(e, expected);
        return true;
    }
    for (int i = 0; i < prog->nmodules; i++) {
        Module *m = prog->modules[i];
        if (!m->file || strcmp(m->file, "xisto:reflect") != 0) continue;
        char *ci = xs_sprintf("__ci_%s", n);
        for (Binding *mb = mod_scopes[m->id]->head; mb; mb = mb->next)
            if (strcmp(mb->name, ci) == 0) { e->type = mb->type; e->cname = mb->cname; return true; }
    }
    return false;
}
static Binding *lookup(const char *name);
/* x: T | null used where T is needed (x.f, x[i], x < y): read as x! — a TypeError if it is null — with warning
   XS2107 (XB-133): TypeScript proved it non-null where xisto's narrowing cannot follow */
/* --tsc-checked (XISTO_TSC_CHECKED=1, XB-133): the program already passes tsc --strict, so what tsc accepted compiles,
   checked at run time where xisto's types cannot follow it (with a warning each) */
static bool tsc_checked(void) {
    static int on = -1;
    if (on < 0) on = getenv("XISTO_TSC_CHECKED") && strcmp(getenv("XISTO_TSC_CHECKED"), "1") == 0;
    return on;
}
static void nonnull_checked(Expr *e, const char *what) {
    xs_warn_rule(XS2107, e->pos, "check first: if (x !== null) { ... }, or write x! where you know it is set",
                 "%s may be null where %s goes: checked at run time", what, ty_name(e->type->elem));
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
static bool shape_copy(Expr *e, Type *want);
static bool shape_copy_array(Expr *e, Type *want);
static bool shape_ok(Type *g, Type *want);
static void pr_convert(Expr *e, Type *to);
static bool pr_convertible(Type *from, Type *to);
static bool tsc_checked(void);
static bool iface_adapt(Expr *e, Type *want, const char *what);
static bool iface_runtime(Class *ifc, Pos pos);
static Expr *ad_ident(Pos pos, const char *name);
static Binding *lookup(const char *name);
/* the text of a top-level `const NAME = 'text'` (as const) this module sees, when name is one and no local hides it */
static const char *const_string_of(const char *name) {
    Binding *b = lookup(name);
    if (b && !b->is_const) return NULL;
    for (int i = 0; i < prog->top.len; i++) {
        Stmt *st = prog->top.items[i];
        if (st->kind != S_VAR || !st->is_const || !st->name || strcmp(st->name, name) != 0 || !st->init) continue;
        if (st->mod != cur_mod && !st->exported) continue;
        Expr *x = st->init;
        while (x->kind == E_AS && x->lhs) x = x->lhs;
        if (x->kind == E_STRING) return x->str;
    }
    return NULL;
}
static bool objlit_has_spread(Expr *o) { for (int i = 0; i < o->args.len; i++) if (!strcmp(o->keys[i], "...")) return true; return false; }
/* a fresh, unchecked copy of a path of names (f, this.f, a.b.c): read again inside an arrow, where its names are captured */
static Expr *fresh_path(Expr *x) {
    if (!x) return NULL;
    Expr *c = xs_alloc(sizeof *c);
    c->pos = x->pos; c->field = -1;
    if (x->kind == E_THIS) { c->kind = E_THIS; return c; }
    if ((x->kind == E_IDENT || x->kind == E_FUNC_REF) && x->str) { c->kind = E_IDENT; c->str = x->str; return c; }
    if (x->kind == E_MEMBER && !x->is_call && x->str && x->lhs) {
        c->kind = E_MEMBER; c->str = x->str;
        c->lhs = fresh_path(x->lhs);
        return c->lhs ? c : NULL;
    }
    return NULL;
}
static void expect_type(Expr *e, Type *want, const char *what) {
    bool widen_ret = want->kind == TY_FUNC && e->type->kind == TY_FUNC && want->nparams == e->type->nparams && !want->rest && !e->type->rest &&
        want->ret && want->ret->kind == TY_UNKNOWN && !ty_is_any(want->ret) && e->type->ret && e->type->ret->kind != TY_UNKNOWN &&
        e->type->ret->kind != TY_VOID && want->async_ret == e->type->async_ret && e->kind != E_ARROW;
    /* report where (a: unknown) => void goes, report taking fewer parameters (TypeScript passes the ones it takes) */
    bool fewer = want->kind == TY_FUNC && e->type->kind == TY_FUNC && want->nparams > e->type->nparams && !want->rest && !e->type->rest &&
        want->ret && want->ret->kind == TY_VOID && e->kind != E_ARROW && !want->async_ret;
    if (want->kind == TY_FUNC && e->type->kind == TY_FUNC && e->kind != E_ARROW && !ty_equal(e->type, want) && !e->type->rest &&
        (want->rest ? e->type->nparams == 0 : want->nparams >= e->type->nparams) && (want->rest || want->async_ret != e->type->async_ret) &&
        want->ret && want->ret->kind != TY_VOID) {
        /* useFactory: getMongoConfig (a () => Cfg) where (...args: unknown[]) => Cfg | Promise<Cfg> goes: the arrow
           () => getMongoConfig() in its place, checked as any arrow written there (a sync result where a promise may go
           is awaited the same way) */
        Expr *callee = fresh_path(e);
        if (callee) {
            Pos pos = e->pos;
            Expr *call = xs_alloc(sizeof *call);
            call->kind = E_CALL; call->pos = pos; call->field = -1; call->lhs = callee;
            Func *f = xs_alloc(sizeof *f);
            f->pos = pos; f->name = "arrow";
            f->nparams = e->type->nparams;
            f->params = xs_alloc(sizeof(Param) * (size_t)(f->nparams + 1));
            for (int i = 0; i < f->nparams; i++) {
                memset(&f->params[i], 0, sizeof(Param));
                f->params[i].name = xs_sprintf("__fa%d", i);
                Expr *id = xs_alloc(sizeof *id);
                id->kind = E_IDENT; id->pos = pos; id->field = -1; id->str = f->params[i].name;
                expr_list_push(&call->args, id);
            }
            Stmt *b = xs_alloc(sizeof *b);
            b->kind = S_BLOCK; b->pos = pos;
            Stmt *r = xs_alloc(sizeof *r);
            r->kind = S_RETURN; r->pos = pos; r->expr = call;
            stmt_list_push(&b->stmts, r);
            f->body = b;
            Arrow *a = xs_alloc(sizeof *a);
            a->pos = pos; a->fn = f; a->expr_body = true;
            Expr *ar = xs_alloc(sizeof *ar);
            ar->kind = E_ARROW; ar->pos = pos; ar->field = -1; ar->arrow = a;
            check_expr(ar, want);
            if (ar->type && ar->type->kind == TY_FUNC) { *e = *ar; if (ty_equal(e->type, want)) return; }
        }
    }
    if (widen_ret || fewer) {
        /* this.q where (sql: string, binds: unknown[]) => Promise<unknown> goes: TypeScript reads a function whose result
           is wider; here an arrow that calls it, its result held as unknown: (a0, a1) => this.q(a0, a1) */
        bool same = true;
        for (int i = 0; i < e->type->nparams && same; i++) same = ty_equal(want->params[i], e->type->params[i]);
        Expr *callee = same ? fresh_path(e) : NULL;
        if (callee) {
            Expr *call = xs_alloc(sizeof *call);
            call->kind = E_CALL; call->pos = e->pos; call->field = -1; call->lhs = callee;
            Func *f = xs_alloc(sizeof *f);
            f->pos = e->pos; f->name = "arrow";
            f->params = xs_alloc(sizeof(Param) * (size_t)(want->nparams ? want->nparams : 1));
            f->nparams = want->nparams;
            for (int i = 0; i < want->nparams; i++) {
                f->params[i].name = xs_sprintf("__w%d_%d", ++tmp_counter, i);
                f->params[i].type = want->params[i];
                if (i < e->type->nparams) expr_list_push(&call->args, ad_ident(e->pos, f->params[i].name));
            }
            if (callee->kind == E_MEMBER) { /* (this.q(...) is a member call: the field read, then called) */
                Expr *m = callee;
                m->is_call = true;
                m->args = call->args;
                call = m;
            }
            Expr *ar = thunk_of(call);
            ar->arrow->synthetic = false;
            Func *af = ar->arrow->fn;
            af->params = f->params;
            af->nparams = f->nparams;
            af->ret = fewer ? ty_void : ty_unknown;
            if (fewer) { /* (a void call as the body's statement) */
                Stmt *st = af->body->stmts.items[0];
                st->kind = S_EXPR;
                ar->arrow->expr_body = true;
            }
            af->is_async = want->async_ret;
            af->ret_promise = want->async_ret;
            *e = *ar;
            check_expr(e, want);
            return;
        }
    }
    if (want->kind == TY_PROMISE || (want->kind == TY_NULLABLE && want->elem->kind == TY_PROMISE)) { /* a promise kept (XB-106) */
        Type *pw = want->kind == TY_PROMISE ? want : want->elem;
        if (ty_equal(e->type, want)) return;
        if (e->type->kind == TY_NULLABLE && e->type->elem->kind == TY_PROMISE && want->kind == TY_NULLABLE) {
            if (ty_equal(e->type->elem->elem, pw->elem) || pw->elem->kind == TY_VOID) return;
            xs_error(XS8018, e->pos, NULL, "%s must be %s, but this is %s", what, ty_name(want), ty_name(e->type));
        }
        if (e->type->kind == TY_PROMISE) {
            if (ty_equal(e->type->elem, pw->elem) || pw->elem->kind == TY_VOID) return;
            if (pw->elem->kind == TY_UNKNOWN && pr_convertible(e->type->elem, pw->elem)) { pr_convert(e, pw->elem); if (want->kind == TY_NULLABLE) e->type = want; return; }
            if (e->type->elem->kind == TY_UNKNOWN && !ty_is_any(e->type->elem) && tsc_checked() && pr_convertible(e->type->elem, pw->elem) && unknown_target(pw->elem)) {
                xs_warn_rule(XS2108, e->pos, "give the value its type where it is made, or cast it: p as Promise<T>",
                             "%s is unknown where %s goes: checked at run time", what, ty_name(want));
                pr_convert(e, pw->elem);
                if (want->kind == TY_NULLABLE) e->type = want;
                return;
            }
            xs_error(XS8018, e->pos, NULL, "%s must be %s, but this is %s", what, ty_name(want), ty_name(e->type));
        }
        if (e->type->kind == TY_NULL && want->kind == TY_NULLABLE) { e->type = want; return; }
        if (pr_is_call(e) && (ty_equal(e->type, pw->elem) || pw->elem->kind == TY_VOID)) { pr_spawn(e); return; }
        if (pw->elem->kind == TY_VOID && e->type->kind == TY_VOID)
            xs_error(XS8018, e->pos, "make it a call to an async function", "%s must be %s", what, ty_name(want));
        expect_type(e, pw->elem, what);
        if (pr_is_call(e)) pr_spawn(e); else pr_resolved(e);
        return;
    }
    { /* an object of a class that extends Error where an Error goes: an Error with its message and name, the object
         its cause (as throw does; instanceof and casts read the cause) */
        Type *w = want->kind == TY_NULLABLE ? want->elem : want;
        if (w->kind == TY_OBJECT && w->cls->builtin && strcmp(w->cls->name, "Error") == 0 && e->type->kind == TY_OBJECT &&
            is_error_class(e->type->cls) && !e->type->cls->builtin) {
            wrap_call(e, "xs_Error_wrap_err", w);
            if (want->kind == TY_NULLABLE) e->type = want;
            return;
        }
    }
    if (is_numkey(want) && e->type->kind == TY_NUMBER) { wrap_call(e, "xs_num_key", ty_int); return; } /* m.set(id, v), id: number */
    if (want->kind == TY_ARRAY && is_numkey(want->elem) && e->type->kind == TY_ARRAY && e->type->elem->kind == TY_NUMBER) {
        wrap_call(e, "xs_num_keys", want); /* new Set<number>(ids) */
        return;
    }
    if (ty_is_any(want) && e->type->kind == TY_JSON) { wrap_call(e, "xs_json_any", want); return; } /* rows of query<T = any>() */
    if (ty_is_any(e->type) && (want->kind == TY_JSON || (want->kind == TY_NULLABLE && want->elem->kind == TY_JSON))) {
        e->type = ty_unknown; /* an any where an object goes (where: filtro): written as JSON by what it holds */
        to_json(e);
        if (want->kind == TY_NULLABLE) e->type = want;
        return;
    }
    if (ty_is_any(want) && e->type->kind != TY_VOID) { to_any(e); return; } /* (null too: an any may be null) */
    if (ty_is_any(e->type) && want->kind != TY_VOID && want->kind != TY_UNKNOWN && dyn_to(e, want)) return;
    if ((want->kind == TY_NUMBER || (want->kind == TY_NULLABLE && want->elem->kind == TY_NUMBER)) && widen_inferred(e)) return;
    if (e->type->kind == TY_INT && (want->kind == TY_NUMBER || (want->kind == TY_NULLABLE && want->elem->kind == TY_NUMBER))) {
        /* an int where a number goes (total: items.length): exact, since an int is within ±(2^53−1); TypeScript has
           only number, so code written for it does this everywhere */
        Expr *a = xs_alloc(sizeof *a);
        *a = *e;
        memset(e, 0, sizeof *e);
        e->kind = E_CALL; e->pos = a->pos; e->field = -1; e->type = ty_number;
        e->builtin.cfn = "xs_to_number";
        expr_list_push(&e->args, a);
        Expr *callee = xs_alloc(sizeof *callee);
        callee->kind = E_IDENT; callee->pos = a->pos; callee->str = "Number"; callee->field = -1;
        e->lhs = callee;
        if (want->kind == TY_NUMBER) return;
    }
    /* a numeric enum is a number (TypeScript): HttpStatus.UNAUTHORIZED where an int goes */
    if (e->type->kind == TY_ENUM && !e->type->en->is_string && (want->kind == TY_INT || want->kind == TY_NUMBER)) { if (want->kind == TY_INT) e->type = ty_int; else return; }
    if (want->kind == TY_ENUM && !want->en->is_string && e->kind == E_NUMBER && e->type->kind == TY_LITINT) { /* 403 where an HttpStatus goes: a member's value */
        for (int i = 0; i < want->en->nmembers; i++)
            if (!want->en->members[i].is_string && want->en->members[i].ival == (long long)e->num) { coerce_lit(e, ty_int); e->type = want; return; }
    }
    if (want->kind == TY_ENUM && !want->en->is_string && (e->type->kind == TY_INT || e->type->kind == TY_LITINT) && e->kind != E_NUMBER) {
        /* status = exception.getStatus(): a number where a numeric enum goes, as TypeScript allows */
        if (e->type->kind == TY_LITINT) coerce_lit(e, ty_int);
        e->type = want;
        return;
    }
    if (e->type->kind == TY_INT && want->kind == TY_INT) return;
    /* T[] of references is laid out like unknown[]: string[] where unknown[] goes */
    if (want->kind == TY_ARRAY && want->elem->kind == TY_UNKNOWN && e->type->kind == TY_ARRAY && ty_is_heap(e->type->elem)) return;
    /* string[] where (string | null)[] goes: the same references (TypeScript accepts it) */
    if (want->kind == TY_ARRAY && want->elem->kind == TY_NULLABLE && ty_is_heap(want->elem->elem) && e->type->kind == TY_ARRAY &&
        ty_equal(e->type->elem, want->elem->elem)) return;
    { /* int[] where number[] goes, number[] where unknown[] goes, string[] where (string | number)[] goes: a copy */
        Type *w = want->kind == TY_NULLABLE ? want->elem : want;
        if (w->kind == TY_ARRAY && e->type->kind == TY_ARRAY && e->kind == E_ARRAY_LIT && !e->spread && elem_widens(e->type->elem, w->elem)) {
            for (int i = 0; i < e->args.len; i++) expect_type(e->args.items[i], w->elem, what); /* [1, 2] where unknown[] goes: the literal retyped */
            e->type = ty_array_of(w->elem);
            if (want->kind == TY_NULLABLE) {
                Expr *v = expr_copy(e);
                memset(e, 0, sizeof *e);
                e->kind = E_UPCAST; e->pos = v->pos; e->lhs = v; e->field = -1;
                e->type = want;
            }
            return;
        }
        if (w->kind == TY_ARRAY && e->type->kind == TY_ARRAY && e->kind != E_ARRAY_LIT && elem_widens(e->type->elem, w->elem)) {
            arr_convert(e, w->elem);
            if (want->kind == TY_NULLABLE) { /* (the map call is emitted from its own type: under a cast) */
                Expr *v = expr_copy(e);
                memset(e, 0, sizeof *e);
                e->kind = E_UPCAST; e->pos = v->pos; e->lhs = v; e->field = -1;
                e->type = want;
            }
            return;
        }
        /* Map<string, A | B> where Map<string, unknown> goes: a union is held as an unknown is (boxed), the same map */
        if (w->kind == TY_MAP && e->type->kind == TY_MAP && ty_equal(w->key, e->type->key) && w->elem->kind == TY_UNKNOWN && !ty_is_any(w->elem) &&
            (ty_strip_null(e->type->elem)->kind == TY_UNION || e->type->elem->kind == TY_UNKNOWN)) {
            e->type = want;
            return;
        }
        /* Record<string, string> where Record<string, string | undefined> or Record<string, unknown> goes (process.env passed as
           an env): values held by reference either way, the same map (TypeScript lets the wider type read it) */
        if (w->kind == TY_MAP && e->type->kind == TY_MAP && ty_equal(w->key, e->type->key) && ty_is_heap(e->type->elem) &&
            ((w->elem->kind == TY_UNKNOWN && !ty_is_any(w->elem)) || (w->elem->kind == TY_NULLABLE && ty_equal(w->elem->elem, e->type->elem)))) {
            e->type = want;
            return;
        }
        if (w->kind == TY_ARRAY && w->elem->kind == TY_MAP && e->type->kind == TY_ARRAY && e->type->elem->kind == TY_MAP &&
            ty_equal(w->elem->key, e->type->elem->key) && w->elem->elem->kind == TY_UNKNOWN && !ty_is_any(w->elem->elem) &&
            ty_strip_null(e->type->elem->elem)->kind == TY_UNION) {
            e->type = want; /* Map<string, A | B>[] where Map<string, unknown>[] goes: the same */
            return;
        }
    }
    { /* Provider[] (a union's array) where unknown[] goes: its elements are held as unknowns are, the same array */
        Type *w = ty_strip_null(want), *g = ty_strip_null(e->type);
        if (w->kind == TY_ARRAY && g->kind == TY_ARRAY && w->elem->kind == TY_UNKNOWN && !ty_is_any(w->elem) && g->elem->kind == TY_UNION &&
            (e->type->kind != TY_NULLABLE || want->kind == TY_NULLABLE)) {
            e->type = want;
            return;
        }
    }
    if (e->type->kind == TY_ENUM && !e->type->en->is_string && want->kind == TY_INT) { e->type = want; return; } /* (a numeric enum's value where an int goes: a Record<Enum, V>'s key) */
    if (e->type->kind == TY_ENUM && e->type->en->is_string && ty_strip_null(want)->kind == TY_STRING && !ty_strip_null(want)->tname) {
        e->type = want; /* a string enum's value where a string goes: its text (TypeScript widens it) */
        return;
    }
    if (want->kind == TY_UNKNOWN && e->type->kind != TY_UNKNOWN && e->type->kind != TY_VOID) { to_unknown(e); return; }
    if (want->kind == TY_JSON && e->type->kind != TY_JSON && e->type->kind != TY_NULL) { /* unknown members: written by what they hold, as JSON.stringify does */
        json_ok_unknown = true;
        bool ok = e->type->kind == TY_LITINT || json_ok(e->type, 0);
        json_ok_unknown = false;
        if (ok) { to_json(e); return; }
    }
    if (want->kind == TY_NULLABLE && want->elem->kind == TY_JSON && e->type->kind != TY_JSON && e->type->kind != TY_NULL && e->type->kind != TY_NULLABLE) {
        json_ok_unknown = true; /* (as above: order?: object given { [col]: dir ?? 'ASC' }) */
        bool ok = e->type->kind == TY_LITINT || json_ok(e->type, 0);
        json_ok_unknown = false;
        if (ok) { to_json(e); return; } /* into object | null */
    }
    if (e->type->kind == TY_JSON && json_target(want)) { from_json(e, want); return; }
    if (e->type->kind == TY_JSON && json_target_u(want)) { from_json(e, want); return; } /* rows with unknown values: decoded by what they hold */
    if (e->type->kind == TY_JSON && (want->kind == TY_NUMBER || want->kind == TY_INT || want->kind == TY_STRING || want->kind == TY_BOOLEAN) && !want->tname) {
        from_json(e, want); return; /* query<number>(sql): the value decoded as one */
    }
    if (e->type->kind == TY_JSON && want->kind == TY_NULLABLE && (json_target(want->elem) || json_target_u(want->elem))) { from_json(e, want); return; } /* into T | null: JSON null is null */
    if (e->type->kind == TY_LITINT && is_numeric(want)) coerce_lit(e, want);
    if (e->type->kind == TY_LITINT && want->kind == TY_NULLABLE && is_numeric(want->elem)) coerce_lit(e, want->elem);
    if (ty_equal(e->type, want) && !(want->kind == TY_STRING && want->tname)) return;
    if (e->type->kind == TY_FUNC && want->kind == TY_FUNC && e->type->nparams == want->nparams && ty_equal(e->type->ret, want->ret) &&
        !e->type->async_ret == !want->async_ret && !e->type->rest == !want->rest) {
        /* a function value whose parameters differ only where one side takes any and the other a reference
           (applyDecorators: (c: ClassInfo, member: any) => void where a PropertyDecorator goes, and the decorators
           it takes): a reference held as any is the same pointer, so the one closure serves both */
        bool same = true;
        for (int k = 0; k < want->nparams && same; k++) {
            Type *a = e->type->params[k], *b = want->params[k];
            if (ty_equal(a, b)) continue;
            if ((ty_is_any(a) && ty_is_heap(b)) || (ty_is_any(b) && ty_is_heap(a))) continue;
            same = false;
        }
        if (same) { e->type = want; return; }
    }
    if (want->kind == TY_STRING && want->tname && e->type->kind == TY_STRING) { /* a string literal type: that value only */
        if (e->kind != E_STRING || strcmp(e->str, want->tname) != 0)
            xs_error(XS8019, e->pos, NULL, "%s must be \"%s\"", what, want->tname);
        return;
    }
    if (want->kind == TY_UNION || (want->kind == TY_NULLABLE && want->elem->kind == TY_UNION)) { /* an int where number | bigint goes: the number */
        Type *u = want->kind == TY_UNION ? want : want->elem;
        bool has_num = false, has_int = false;
        for (int i = 0; i < u->nparams; i++) { has_num = has_num || u->params[i]->kind == TY_NUMBER; has_int = has_int || u->params[i]->kind == TY_INT; }
        if (e->type->kind == TY_LITINT) coerce_lit(e, has_int || !has_num ? ty_int : ty_number);
        if (e->type->kind == TY_INT && has_num && !has_int) wrap_call(e, "xs_to_number", ty_number);
    }
    if ((want->kind == TY_UNION || (want->kind == TY_NULLABLE && want->elem->kind == TY_UNION)) && pr_is_call(e)) {
        /* work(s) where Promise<T> | ((s: S) => Promise<T>) goes: the call kept as its promise */
        Type *u = want->kind == TY_UNION ? want : want->elem;
        for (int i = 0; i < u->nparams; i++)
            if (u->params[i]->kind == TY_PROMISE && ty_equal(u->params[i]->elem, e->type)) { pr_spawn(e); break; }
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
        xs_error(XS2002, e->pos, xs_sprintf("declare it as %s | null", ty_name(want)), "%s must be %s, but this is null", what, ty_name(want));
    if (shape_copy(e, want)) return;
    if (iface_adapt(e, want, what)) return; /* a class where an interface with methods goes (XB-126) */
    if (want->kind == TY_NULLABLE && shape_copy(e, want->elem)) return;
    if (shape_copy_array(e, want)) { if (want->kind == TY_NULLABLE) e->type = want; return; }
    Type *wu = want->kind == TY_NULLABLE ? want->elem : want;
    if (tsc_checked() && e->type->kind == TY_UNKNOWN && want->kind != TY_VOID && !ty_is_any(want) && (unknown_target(want) || wu->kind == TY_UNION)) {
        /* an unknown (a row's value, a { k: null } field) where T goes, in code tsc checked: as T, checked at run time */
        xs_warn_rule(XS2108, e->pos, "give the value its type where it is made, or cast it: x as T",
                     "%s is unknown where %s goes: checked at run time", what, ty_name(want));
        if (dyn_to(e, want)) return;
    }
    if (tsc_checked() && want->kind != TY_VOID && !ty_is_any(want) &&
        (e->type->kind == TY_UNION || (e->type->kind == TY_NULLABLE && e->type->elem->kind == TY_UNION))) {
        /* string | number where string goes, in code tsc checked: a typeof test xisto's narrowing does not follow (in
           another function, through a helper): the member it holds, checked at run time */
        Type *u = e->type->kind == TY_NULLABLE ? e->type->elem : e->type;
        bool member = false;
        for (int i = 0; i < u->nparams && !member; i++) {
            Type *m = u->params[i];
            if (ty_equal(m, wu) || (is_numeric(m) && is_numeric(wu))) member = true;
        }
        if (member) {
            xs_warn_rule(XS2108, e->pos, "narrow it first: if (typeof x === 'string') { ... }",
                         "%s is %s where %s goes: checked at run time", what, ty_name(e->type), ty_name(want));
            to_unknown(e);
            if (dyn_to(e, want)) return;
        }
    }
    if (tsc_checked() && e->type->kind == TY_NULLABLE && want->kind != TY_NULLABLE && want->kind != TY_VOID) {
        /* T | null where T goes (XB-133): TypeScript proved it non-null where xisto's narrowing cannot (a test in
           another function, an index it trusts); read as x! — a TypeError if it is null — with a warning */
        xs_warn_rule(XS2107, e->pos, "check first: if (x !== null) { ... }, or write x! where you know it is set",
                     "%s may be null where %s goes: checked at run time", what, ty_name(want));
        Expr *inner = xs_alloc(sizeof *inner);
        *inner = *e;
        memset(e, 0, sizeof *e);
        e->kind = E_AS;
        e->op = TK_NOT;
        e->pos = inner->pos;
        e->lhs = inner;
        e->field = -1;
        e->type = inner->type->elem;
        expect_type(e, want, what);
        return;
    }
    if (tsc_checked() && e->type->kind == TY_NUMBER && want->kind == TY_INT) {
        /* xs.slice(0, n) with n a number: as JavaScript reads an index (ToIntegerOrInfinity), the number truncated */
        xs_warn_rule(XS2109, e->pos, "declare it int, or convert: Math.trunc(x)", "%s is a number where an int goes: truncated", what);
        wrap_call(e, "xs_trunc_int", ty_int);
        return;
    }
    if (tsc_checked() && e->type->kind == TY_NULLABLE && e->type->elem->kind == TY_NUMBER && want->kind == TY_NULLABLE && want->elem->kind == TY_INT) {
        xs_warn_rule(XS2109, e->pos, "declare it int, or convert: Math.trunc(x)", "%s is a number where an int goes: truncated", what);
        wrap_call(e, "xs_trunc_int_or_null", want); /* (null stays null) */
        return;
    }
    const char *help = NULL;
    if (e->type->kind == TY_NULLABLE && ty_equal(e->type->elem, want))
        help = "it may be null; check first: if (x !== null) { ... }";
    if (e->type->kind == TY_INT && want->kind == TY_NUMBER) help = "convert explicitly: Number(x)";
    else if (e->type->kind == TY_NUMBER && want->kind == TY_INT) help = "convert explicitly: Math.trunc(x), Math.floor(x), Math.ceil(x) or Math.round(x)";
    else if (is_numeric(e->type) && want->kind == TY_STRING) help = "convert explicitly: String(x)";
    xs_error(XS8018, e->pos, help, "%s must be %s, but this is %s", what, ty_name(want), ty_name(e->type));
}

/* An object of a shape the compiler made (a literal's inferred type, or a type written in place) where an
   interface with those fields goes: TypeScript types are structural, so const RULE = (id: string) => ({ ruleId: id,
   ... }) gives a value that is an OracleRuleRef. Here each is its own struct: the value is copied into the interface,
   field by field, through ((s) => ({ a: s.a, ... }))(value). */
static bool inline_cls(Type *t);
static bool conv_ok(Type *from, Type *to, int depth);
static bool made_shape(Class *c) {
    return c && c->name && (strncmp(c->name, "__Obj_", 6) == 0 || strncmp(c->name, "{ ", 2) == 0) && c->nmethods == 0;
}
/* the fields a structural copy reads from a value of class c (its own and its bases') */
static bool plain_data(Class *c) { return c && !c->builtin && c->nfields > 0; }
/* OperatorIdentity (a Mongo schema class) returned where OperatorIdentityResponseDto goes: TypeScript compares the fields;
   with --tsc-checked a class of fields alone (no methods, no subclasses, no base) is copied as data is */
static bool data_class_src(Class *src) {
    return tsc_checked() && !src->builtin && !src->is_interface && !src->is_abstract && src->nmethods == 0 && src->nsubs == 0 &&
           !src->base_name && !src->tparams && !src->iface_methods;
}
static bool shape_copy(Expr *e, Type *want) {
    Type *g = e->type;
    if (!g || !want) return false;
    bool nul = false;
    if (g->kind == TY_NULLABLE) {
        if (want->kind != TY_NULLABLE) return false; /* (a T | null where a U goes: not this) */
        g = g->elem; want = want->elem; nul = true;
    }
    else if (want->kind == TY_NULLABLE) want = want->elem;
    if (g->kind != TY_OBJECT || !plain_data(g->cls)) return false;
    Class *src = g->cls;
    bool to_rec = want->kind == TY_MAP && want->record && want->key->kind == TY_STRING;
    if (!to_rec) {
        if (want->kind != TY_OBJECT || g->cls == want->cls) return false;
        Class *dst = want->cls;
        /* where an interface of data goes (or a shape the compiler made): TypeScript compares the fields */
        /* (a class of data too: a response DTO filled from a row, return row; TypeScript compares the fields) */
        bool data_cls = !dst->builtin && !dst->is_interface && !dst->is_abstract && dst->nsubs == 0 && !dst->tparams &&
                        (!dst->ctor || dst->ctor->nparams == 0) && dst->nfields > 0 && !dst->base_name &&
                        (src->is_interface || made_shape(src) || data_class_src(src)); /* (from data; from another class only as
                                                     tsc-checked code has it: a class of fields alone, copied, a new object) */
        if (!(dst->is_interface || made_shape(dst) || data_cls) || dst->nmethods != 0 || dst->iface_methods) return false;
        if (!made_shape(src) && !src->is_interface && src->nmethods == 0 && !src->nfields) return false;
        for (int i = 0; i < dst->nfields; i++)
            if (!find_field(src, dst->fields[i].name, NULL) && !dst->fields[i].optional) return false;
    }
    Class *dst = to_rec ? src : want->cls; /* a Record gets every field of the value */
    /* an open class (Express's headers): into a Record what its __expando holds (it keeps every property, the
       declared ones too) */
    bool open_rec = to_rec && find_field(src, "__expando", NULL);
    int n = 0;
    for (int i = 0; i < dst->nfields; i++) if (find_field(src, dst->fields[i].name, NULL)) n++;
    Expr *inner = xs_alloc(sizeof *inner);
    *inner = *e;
    Expr *obj = xs_alloc(sizeof *obj);
    obj->kind = E_OBJLIT; obj->pos = e->pos; obj->field = -1;
    obj->keys = xs_alloc(sizeof(char *) * (size_t)(n ? n : 1));
    int k = 0;
    for (int i = 0; i < dst->nfields; i++) {
        if (!find_field(src, dst->fields[i].name, NULL)) continue;
        if (open_rec) continue;
        Expr *sv = xs_alloc(sizeof *sv);
        sv->kind = E_IDENT; sv->pos = e->pos; sv->field = -1; sv->str = "__shape";
        Expr *m = xs_alloc(sizeof *m);
        m->kind = E_MEMBER; m->pos = e->pos; m->field = -1; m->lhs = sv; m->str = dst->fields[i].name;
        obj->keys[k++] = dst->fields[i].name;
        expr_list_push(&obj->args, m);
    }
    Expr *body = obj;
    if (open_rec) { /* __shape.__expando as the Record wanted (its values checked as they are read) */
        Expr *sv = xs_alloc(sizeof *sv);
        sv->kind = E_IDENT; sv->pos = e->pos; sv->field = -1; sv->str = "__shape";
        Expr *m = xs_alloc(sizeof *m);
        m->kind = E_MEMBER; m->pos = e->pos; m->field = -1; m->lhs = sv; m->str = "__expando";
        Expr *as = xs_alloc(sizeof *as);
        as->kind = E_AS; as->pos = e->pos; as->field = -1; as->lhs = m; as->elem_type = want;
        body = obj = as;
    }
    if (nul) { /* __shape === null ? null : { ... } */
        Expr *sv = xs_alloc(sizeof *sv);
        sv->kind = E_IDENT; sv->pos = e->pos; sv->field = -1; sv->str = "__shape";
        Expr *z = xs_alloc(sizeof *z);
        z->kind = E_NULL; z->pos = e->pos; z->field = -1;
        Expr *t = xs_alloc(sizeof *t);
        t->kind = E_BINARY; t->op = TK_EQ; t->pos = e->pos; t->field = -1; t->lhs = sv; t->rhs = z;
        Expr *z2 = xs_alloc(sizeof *z2);
        z2->kind = E_NULL; z2->pos = e->pos; z2->field = -1;
        Expr *c = xs_alloc(sizeof *c);
        c->kind = E_COND; c->pos = e->pos; c->field = -1; c->lhs = t;
        expr_list_push(&c->args, z2);
        expr_list_push(&c->args, obj);
        body = c;
    }
    Type *ret = nul ? ty_nullable_of(want) : want;
    Expr *arrow = thunk_of(body);
    Func *f = arrow->arrow->fn;
    f->params = xs_alloc(sizeof(Param));
    memset(f->params, 0, sizeof(Param));
    f->params[0].name = "__shape";
    f->params[0].type = nul ? ty_nullable_of(g) : g;
    f->nparams = 1;
    f->ret = ret;
    memset(e, 0, sizeof *e);
    e->kind = E_CALL; e->pos = inner->pos; e->field = -1; e->is_call = true;
    e->lhs = arrow;
    expr_list_push(&e->args, inner);
    check_expr(arrow, NULL);
    e->type = ret;
    return true;
}

/* T[] where U[] goes and a T is copied into a U (shape_copy): xs.map((__el: T): U => __el) */
static bool shape_ok(Type *g, Type *want) {
    Expr probe;
    memset(&probe, 0, sizeof probe);
    probe.kind = E_NULL; probe.field = -1; probe.type = g;
    Type *w = want->kind == TY_NULLABLE ? want->elem : want, *h = g->kind == TY_NULLABLE ? g->elem : g;
    if (h->kind != TY_OBJECT || !plain_data(h->cls)) return false;
    if (w->kind == TY_MAP && w->record && w->key->kind == TY_STRING) return true; /* rows where Record<string, unknown>[] goes: each one's fields */
    if (w->kind != TY_OBJECT || h->cls == w->cls) return false;
    Class *dst = w->cls, *src = h->cls;
    bool data_cls = !dst->builtin && !dst->is_interface && !dst->is_abstract && dst->nsubs == 0 && !dst->tparams &&
                    (!dst->ctor || dst->ctor->nparams == 0) && dst->nfields > 0 && !dst->base_name &&
                    (src->is_interface || made_shape(src) || data_class_src(src));
    if (!(dst->is_interface || made_shape(dst) || data_cls) || dst->nmethods != 0 || dst->iface_methods) return false;
    for (int i = 0; i < dst->nfields; i++)
        if (!find_field(src, dst->fields[i].name, NULL) && !dst->fields[i].optional) return false;
    (void)probe;
    return true;
}
static bool shape_copy_array(Expr *e, Type *want) {
    Type *g = e->type;
    bool nul = false; /* T[] | null where U[] | null goes: __arr === null ? null : __arr.map(...) */
    if (g && want && g->kind == TY_NULLABLE && g->elem->kind == TY_ARRAY && want->kind == TY_NULLABLE) { g = g->elem; nul = true; }
    if (!g || !want || g->kind != TY_ARRAY) return false;
    Type *w = want->kind == TY_NULLABLE ? want->elem : want;
    if (w->kind != TY_ARRAY || ty_equal(g->elem, w->elem)) return false;
    if (g->elem->kind == TY_NULLABLE && w->elem->kind != TY_NULLABLE) return false;
    if (!shape_ok(g->elem, w->elem)) return false;
    Pos pos = e->pos;
    Expr *inner = xs_alloc(sizeof *inner);
    *inner = *e;
    Expr *el = xs_alloc(sizeof *el);
    el->kind = E_IDENT; el->pos = pos; el->field = -1; el->str = "__el";
    Expr *mapfn = thunk_of(el);
    Func *mf = mapfn->arrow->fn;
    mf->params = xs_alloc(sizeof(Param));
    memset(mf->params, 0, sizeof(Param));
    mf->params[0].name = "__el"; mf->params[0].type = g->elem; mf->nparams = 1;
    mf->ret = w->elem;
    Expr *av = xs_alloc(sizeof *av);
    av->kind = E_IDENT; av->pos = pos; av->field = -1; av->str = "__arr";
    Expr *call = xs_alloc(sizeof *call); /* __arr.map(fn): a member call */
    call->kind = E_MEMBER; call->pos = pos; call->field = -1; call->is_call = true; call->lhs = av; call->str = "map";
    expr_list_push(&call->args, mapfn);
    Expr *body = call;
    if (nul) {
        Expr *a1 = xs_alloc(sizeof *a1);
        a1->kind = E_IDENT; a1->pos = pos; a1->field = -1; a1->str = "__arr";
        Expr *z = xs_alloc(sizeof *z);
        z->kind = E_NULL; z->pos = pos; z->field = -1;
        Expr *t = xs_alloc(sizeof *t);
        t->kind = E_BINARY; t->op = TK_EQ; t->pos = pos; t->field = -1; t->lhs = a1; t->rhs = z;
        Expr *z2 = xs_alloc(sizeof *z2);
        z2->kind = E_NULL; z2->pos = pos; z2->field = -1;
        Expr *c = xs_alloc(sizeof *c);
        c->kind = E_COND; c->pos = pos; c->field = -1; c->lhs = t;
        expr_list_push(&c->args, z2);
        expr_list_push(&c->args, call);
        body = c;
    }
    Expr *arrow = thunk_of(body);
    Func *f = arrow->arrow->fn;
    f->params = xs_alloc(sizeof(Param));
    memset(f->params, 0, sizeof(Param));
    f->params[0].name = "__arr"; f->params[0].type = nul ? ty_nullable_of(g) : g; f->nparams = 1;
    f->ret = nul ? ty_nullable_of(w) : w;
    memset(e, 0, sizeof *e);
    e->kind = E_CALL; e->pos = pos; e->field = -1; e->is_call = true;
    e->lhs = arrow;
    expr_list_push(&e->args, inner);
    check_expr(arrow, NULL);
    e->type = f->ret;
    return true;
}

static void tsc_numeric_operand(Expr *x, bool plus) { /* --tsc-checked: a number | null or an unknown operand, as tsc accepted it */
    if (!tsc_checked()) return;
    if (x->type->kind == TY_NULLABLE && is_numeric(x->type->elem)) { nonnull_checked(x, "the operand"); return; }
    if (x->type->kind == TY_UNKNOWN) {
        xs_warn_rule(XS2108, x->pos, "declare its type", "the operand is unknown where a number goes: checked at run time");
        dyn_wrap(x, "xs_dyn_num", ty_number);
        return;
    }
    if (x->type->kind == TY_UNION && !plus) { /* string | number narrowed by a test xisto does not follow: the number, checked
                                                (not for +, where a string concatenates) */
        bool num = false;
        for (int i = 0; i < x->type->nparams; i++) if (is_numeric(x->type->params[i])) num = true;
        if (!num) return;
        xs_warn_rule(XS2108, x->pos, "narrow it first: if (typeof x === 'number') { ... }", "the operand is %s where a number goes: checked at run time", ty_name(x->type));
        to_unknown(x);
        dyn_wrap(x, "xs_dyn_num", ty_number);
    }
}
static bool is_date_type(Type *t) { t = t ? ty_strip_null(t) : NULL; return t && t->kind == TY_OBJECT && t->cls && t->cls->builtin && !strcmp(t->cls->name, "Date"); }
static Type *unify_numeric(Expr *e, const char *what) {
    Expr *a = e->lhs, *b = e->rhs;
    if ((e->op == TK_LT || e->op == TK_LE || e->op == TK_GT || e->op == TK_GE) && (is_date_type(a->type) || is_date_type(b->type))) {
        /* d1 > d2 with Dates: JavaScript compares their valueOf(), the time in ms (a Date | null narrowed by && too) */
        Expr *xs[2] = {a, b};
        for (int i = 0; i < 2; i++) {
            Expr *x = xs[i];
            if (!is_date_type(x->type)) continue;
            if (x->type->kind == TY_NULLABLE) { if (!tsc_checked()) continue; nonnull_checked(x, "the operand"); }
            Expr *in = xs_alloc(sizeof *in);
            *in = *x;
            in->prechecked = true;
            memset(x, 0, sizeof *x);
            x->kind = E_MEMBER; x->pos = in->pos; x->field = -1; x->lhs = in; x->str = "getTime"; x->is_call = true;
            check_expr(x, NULL);
        }
    }
    tsc_numeric_operand(a, e->op == TK_PLUS);
    tsc_numeric_operand(b, e->op == TK_PLUS);
    if (!is_numeric(a->type) && a->type->kind != TY_LITINT)
        xs_error(XS8112, a->pos, NULL, "left operand of %s must be int or number, but this is %s", what, ty_name(a->type));
    if (!is_numeric(b->type) && b->type->kind != TY_LITINT)
        xs_error(XS8121, b->pos, NULL, "right operand of %s must be int or number, but this is %s", what, ty_name(b->type));
    if (a->type->kind == TY_LITINT && b->type->kind == TY_LITINT) return ty_litint;
    if (a->type->kind == TY_LITINT) coerce_lit(a, b->type);
    if (b->type->kind == TY_LITINT) coerce_lit(b, a->type);
    if (a->type->kind == TY_INT && b->type->kind == TY_NUMBER) widen_inferred(a);
    else if (a->type->kind == TY_NUMBER && b->type->kind == TY_INT) widen_inferred(b);
    /* xs.length >= limit, Math.round(x) / 1e8: an int where a number is met is that number (exact within
       ±(2^53−1)), as TypeScript has only number */
    if ((a->type->kind == TY_INT && b->type->kind == TY_NUMBER) || (a->type->kind == TY_NUMBER && b->type->kind == TY_INT)) {
        xs_warn_rule(XS2103, e->pos, "declare the type you mean: Number(i) for a number, Math.trunc(x) for an int", /* XB-118 rule 1 */
                     "int mixed with number in %s: the result is number", what);
        Expr **slot = a->type->kind == TY_INT ? &e->lhs : &e->rhs;
        Expr *x = *slot;
        Expr *c = xs_alloc(sizeof *c);
        c->kind = E_CALL; c->pos = x->pos; c->field = -1; c->type = ty_number;
        c->builtin.cfn = "xs_to_number";
        expr_list_push(&c->args, x);
        Expr *callee = xs_alloc(sizeof *callee);
        callee->kind = E_IDENT; callee->pos = x->pos; callee->str = "Number"; callee->field = -1;
        c->lhs = callee;
        *slot = c;
        a = e->lhs; b = e->rhs;
    }
    if (!ty_equal(a->type, b->type))
        xs_error(XS8084, e->pos, "int and number do not mix; convert with Number(i) or Math.trunc(x)",
                 "cannot apply %s to %s and %s", what, ty_name(a->type), ty_name(b->type));
    return a->type;
}

/* A left-out argument's default as written, for this call. A default can read the parameters before it
   (function f(a: Uint8Array, end: int = a.length)): those names are the call's own arguments, not whatever the
   caller calls a; each becomes a copy of that argument when it can be read twice without a difference (a name, a
   field, a literal), and otherwise the call must give the argument. */
static Expr *dflt_subst(Expr *d, Func *f, Expr **args, int nargs, Pos at);
/* where an expression's names resolve: a default reads the names of the function's module (not the caller's), and
   the arguments it reads are the caller's own expressions */
typedef struct { Scope *scope; Module *mod; Class *cls; } CheckCtx;
static CheckCtx *dflt_caller;
static Expr *dflt_recv; /* obj in obj.m(...): what `this` reads in m's defaults */
static CheckCtx *ctx_here(void) {
    CheckCtx *c = xs_alloc(sizeof *c);
    c->scope = scope; c->mod = cur_mod; c->cls = cur_class;
    return c;
}
static Expr *dflt_arg(Func *f, int i, Expr **args, int nargs, Pos at) {
    CheckCtx *was = dflt_caller;
    dflt_caller = ctx_here();
    Expr *e = dflt_subst(f->params[i].dflt, f, args, nargs, at);
    dflt_caller = was;
    Module *fm = f->mod ? f->mod : f->cls ? f->cls->mod : NULL; /* (a method's default: its class's module) */
    if (e && fm && fm != cur_mod && e->kind != E_NUMBER && e->kind != E_STRING && e->kind != E_BOOL && e->kind != E_NULL) {
        CheckCtx *c = xs_alloc(sizeof *c);
        c->scope = mod_scopes[fm->id]; c->mod = fm; c->cls = cur_class;
        e->ctx = c;
    }
    return e;
}
static Expr *dflt_subst(Expr *d, Func *f, Expr **args, int nargs, Pos at) {
    if (!d) return NULL;
    Expr *c = xs_alloc(sizeof *c);
    *c = *d;
    if (d->kind == E_IDENT && d->str)
        for (int k = 0; k < nargs && k < f->nparams; k++)
            if (f->params[k].name && strcmp(f->params[k].name, d->str) == 0) {
                Expr *a = args[k], *p = a;
                while (p && p->kind == E_MEMBER && !p->is_call) p = p->lhs; /* (x, x.y.z: no call on the way) */
                bool simple = a && (a->kind == E_NUMBER || a->kind == E_STRING || a->kind == E_NULL ||
                                    (p && (p->kind == E_IDENT || p->kind == E_THIS)));
                if (!simple)
                    xs_error(XS8132, at, xs_sprintf("put the argument for '%s' in a const first, or give every argument", f->params[k].name),
                             "the default of a later parameter reads '%s', and the argument given for it cannot be read twice", d->str);
                Expr *cp = dflt_subst(a, f, NULL, 0, at); /* (a copy of the argument as written) */
                if (dflt_caller) cp->ctx = dflt_caller;
                return cp;
            }
    if (d->kind == E_THIS && dflt_recv && dflt_recv->kind != E_THIS) { /* m(k = this.size): the receiver of this call */
        Expr *p = dflt_recv;
        while (p->kind == E_MEMBER && !p->is_call) p = p->lhs;
        if (p->kind != E_IDENT && p->kind != E_THIS)
            xs_error(XS8132, at, "put the object in a const first, or give every argument", "a default reads 'this', and the object the method is called on cannot be read twice");
        Expr *cp = dflt_subst(dflt_recv, f, NULL, 0, at);
        if (dflt_caller) cp->ctx = dflt_caller;
        return cp;
    }
    if (d->kind == E_ARROW) return c; /* (an arrow's own names are its own) */
    c->lhs = dflt_subst(d->lhs, f, args, nargs, at);
    c->rhs = dflt_subst(d->rhs, f, args, nargs, at);
    if (d->args.len) {
        c->args = (ExprList){0};
        for (int k = 0; k < d->args.len; k++) expr_list_push(&c->args, dflt_subst(d->args.items[k], f, args, nargs, at));
    }
    return c;
}

static bool is_spread(Expr *a) { return a && a->kind == E_UNARY && a->op == TK_ELLIPSIS; }
/* a call through a function value whose last parameter is ...xs: T[]: the arguments from there on, one array */
static void pack_rest(Expr *e, Type *ft) {
    if (!ft || !ft->rest || !ft->nparams) return;
    int r = ft->nparams - 1;
    if (e->args.len == ft->nparams && is_spread(e->args.items[r])) { e->args.items[r] = e->args.items[r]->lhs; return; }
    if (e->args.len < r) return; /* (too few: reported by the caller) */
    Expr *arr = xs_alloc(sizeof *arr);
    arr->kind = E_ARRAY_LIT; arr->pos = e->args.len > r ? e->args.items[r]->pos : e->pos; arr->field = -1;
    for (int i = r; i < e->args.len; i++) {
        if (is_spread(e->args.items[i])) xs_error(XS6032, e->args.items[i]->pos, "pass the array itself: f(xs), with a parameter xs: T[]", "spreading arguments into a call is not supported (xisto has no rest parameters)");
        expr_list_push(&arr->args, e->args.items[i]);
    }
    e->args.len = r;
    expr_list_push(&e->args, arr);
}
static void check_args(Expr *e, Func *f, const char *what) {
    if (f->nparams && f->params[f->nparams - 1].rest) { /* ...xs: T[]: the arguments from there on, one array (or f(...ys): ys) */
        int r = f->nparams - 1;
        if (e->args.len == f->nparams && is_spread(e->args.items[r])) e->args.items[r] = e->args.items[r]->lhs;
        else if (e->args.len > r) {
            Expr *arr = xs_alloc(sizeof *arr);
            arr->kind = E_ARRAY_LIT; arr->pos = e->args.items[r]->pos; arr->field = -1;
            for (int i = r; i < e->args.len; i++) {
                if (is_spread(e->args.items[i])) xs_error(XS6032, e->args.items[i]->pos, "pass the array itself: f(xs), with a parameter xs: T[]", "spreading arguments into a call is not supported (xisto has no rest parameters)");
                expr_list_push(&arr->args, e->args.items[i]);
            }
            e->args.len = r;
            expr_list_push(&e->args, arr);
        }
    }
    for (int i = 0; i < e->args.len; i++)
        if (is_spread(e->args.items[i])) xs_error(XS6032, e->args.items[i]->pos, "pass the array itself: f(xs), with a parameter xs: T[]", "spreading arguments into a call is not supported (xisto has no rest parameters)");
    int required = f->nparams;
    while (required > 0 && f->params[required - 1].dflt) required--;
    if (e->args.len < required || e->args.len > f->nparams) {
        if (required == f->nparams) xs_error(XS8026, e->pos, NULL, "%s takes %d argument(s), got %d", what, f->nparams, e->args.len);
        xs_error(XS8027, e->pos, NULL, "%s takes %d to %d argument(s), got %d", what, required, f->nparams, e->args.len);
    }
    /* left-out arguments: the declared defaults, evaluated at the call as in JavaScript */
    for (int i = e->args.len, given = e->args.len; i < f->nparams; i++) expr_list_push(&e->args, dflt_arg(f, i, e->args.items, given, e->pos));
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
        xs_error(XS7108, e->pos, "supported: Math.PI, E, LN2, LN10, LOG2E, LOG10E, SQRT2, SQRT1_2", "unknown Math constant '%s'", e->str);
    }
    const char *fn = e->str;
    if (strcmp(fn, "imul") == 0 || strcmp(fn, "clz32") == 0) { /* 32-bit integer math: ints */
        int na = fn[0] == 'i' ? 2 : 1;
        if (e->args.len != na) xs_error(XS7026, e->pos, NULL, "Math.%s takes %d argument(s), got %d", fn, na, e->args.len);
        for (int k = 0; k < na; k++) {
            check_expr(e->args.items[k], ty_int);
            if (e->args.items[k]->type->kind == TY_LITINT) coerce_lit(e->args.items[k], ty_int);
            if (!is_numeric(e->args.items[k]->type)) xs_error(XS7046, e->args.items[k]->pos, NULL, "argument of Math.%s must be int or number", fn);
        }
        e->type = ty_int;
        return;
    }
    for (size_t i = 0; i < sizeof fns / sizeof fns[0]; i++) {
        if (strcmp(fn, fns[i].name) != 0) continue;
        bool minmax = strcmp(fn, "min") == 0 || strcmp(fn, "max") == 0; /* any number of arguments, as in JS */
        if (minmax && e->args.len == 1 && is_spread(e->args.items[0])) { /* Math.max(...xs): over the array (-Infinity when empty) */
            Expr *a = e->args.items[0]->lhs;
            check_expr(a, NULL);
            Type *at = a->type;
            if (at->kind != TY_ARRAY || !is_numeric(at->elem)) xs_error(XS7026, a->pos, NULL, "Math.%s(...xs) takes an array of numbers", fn);
            Pos pos = e->pos;
            memset(e, 0, sizeof *e);
            e->kind = E_CALL; e->pos = pos; e->field = -1; e->type = ty_number;
            e->builtin.cfn = at->elem->kind == TY_NUMBER ? (fn[1] == 'a' ? "xs_math_max_nums" : "xs_math_min_nums") : (fn[1] == 'a' ? "xs_math_max_ints" : "xs_math_min_ints");
            expr_list_push(&e->args, a);
            return;
        }
        if (!minmax && e->args.len != fns[i].nargs)
            xs_error(XS7026, e->pos, NULL, "Math.%s takes %d argument(s), got %d", fn, fns[i].nargs, e->args.len);
        for (int k = 0; k < e->args.len; k++) check_expr(e->args.items[k], NULL);
        bool generic = strcmp(fn, "abs") == 0 || strcmp(fn, "min") == 0 || strcmp(fn, "max") == 0;
        bool to_int = strcmp(fn, "floor") == 0 || strcmp(fn, "ceil") == 0 || strcmp(fn, "round") == 0 || strcmp(fn, "trunc") == 0;
        if (generic) {
            Type *t = NULL;
            for (int k = 0; k < e->args.len; k++)
                if (is_numeric(e->args.items[k]->type) && (!t || e->args.items[k]->type->kind == TY_NUMBER)) t = e->args.items[k]->type; /* (an int among numbers: a number) */
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
        xs_error(XS7109, e->pos, xs_help_suggest(e->str, names, (int)(sizeof fns / sizeof fns[0]), "supported: sqrt floor ceil abs round trunc min max pow sin cos tan log exp"), "unknown Math function '%s'", e->str);
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
        if (rx_only) xs_error(XS7096, a0->pos, "pass a RegExp: s.match(/a+/), or new RegExp(text)", "string.%s takes a RegExp", m);
        return false; /* the string versions (the argument is checked again there: no harm) */
    }
    prog->uses_regex = true;
    Class *mc = find_class("RegExpExecArray");
    e->builtin.self_first = true;
    if (strcmp(m, "match") == 0 || strcmp(m, "matchAll") == 0 || strcmp(m, "search") == 0) {
        if (e->args.len != 1) xs_error(XS7098, e->pos, NULL, "string.%s takes one argument", m);
        if (m[5] == 'A') { e->builtin.cfn = "xs_str_match_all_re"; e->type = ty_array_of(ty_object_of(mc)); }
        else if (m[0] == 'm') { e->builtin.cfn = "xs_str_match_re"; e->type = ty_nullable_of(ty_object_of(mc)); }
        else { e->builtin.cfn = "xs_str_search_re"; e->type = ty_int; }
        return true;
    }
    if (strcmp(m, "split") == 0) {
        if (e->args.len > 2) xs_error(XS7099, e->pos, NULL, "string.split takes a separator and an optional limit");
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
    if (e->args.len != 2) xs_error(XS7097, e->pos, NULL, "string.%s takes a pattern and a replacement", m);
    bool all = strcmp(m, "replaceAll") == 0;
    Expr *r = e->args.items[1];
    if (r->kind == E_ARROW || (r->kind == E_IDENT && 0)) {
        int np = r->arrow->fn->nparams;
        if (np > 8) xs_error(XS6019, r->pos, NULL, "a replacement callback takes at most 8 parameters (the match and 7 groups)");
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
        if (r->type->kind == TY_FUNC) xs_error(XS6018, r->pos, "write the callback inline: s.replace(re, (m, p1) => ...)", "a replacement callback must be an arrow function written in place");
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
        {"localeCompare", "xs_str_locale_compare", 1, 2, TY_STRING, TY_STRING, 2},
        {"normalize", "xs_str_normalize", 0, 1, TY_STRING, TY_VOID, 1},
    };
    if (!e->is_call) return false;
    for (size_t i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        if (strcmp(e->str, ms[i].name) != 0) continue;
        if (e->args.len < ms[i].min || e->args.len > ms[i].max)
            xs_error(XS7095, e->pos, NULL, "string.%s takes %d to %d argument(s), got %d", e->str, ms[i].min, ms[i].max, e->args.len);
        for (int k = 0; k < e->args.len; k++) {
            Type *want = (k == 0 ? ms[i].a1 : ms[i].a2) == TY_STRING ? ty_string : ty_int;
            check_expr(e->args.items[k], want);
            if (want == ty_int) index_of_number(e->args.items[k]);
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
        xs_error(XS7093, e->pos, "supported: length, startsWith, endsWith, includes, indexOf, slice, trim, toUpperCase, toLowerCase", "string has no property '%s'", e->str);
    }
    for (size_t i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        if (strcmp(e->str, ms[i].name) != 0) continue;
        int n = ms[i].nargs;
        bool slice = strcmp(e->str, "slice") == 0 || strcmp(e->str, "substring") == 0;
        bool index_of = strcmp(e->str, "indexOf") == 0;
        if ((slice || index_of) ? (e->args.len < 1 || e->args.len > 2) : e->args.len != n)
            xs_error(XS7094, e->pos, NULL, "string.%s takes %d argument(s), got %d", e->str, n, e->args.len);
        Type *argt = ms[i].arg == TY_STRING ? ty_string : ty_int;
        for (int k = 0; k < e->args.len; k++) {
            Type *want = (index_of && k == 1) ? ty_int : argt; /* indexOf(search, fromIndex) */
            check_expr(e->args.items[k], want);
            if (want == ty_int) index_of_number(e->args.items[k]);
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
        xs_error(XS7092, e->pos, xs_help_suggest(e->str, names, (int)(sizeof ms / sizeof ms[0]) + 1, "supported: length, startsWith, endsWith, includes, indexOf, lastIndexOf, slice, substring, trim, trimStart, trimEnd, toUpperCase, toLowerCase, split, replace, replaceAll, padStart, padEnd, repeat, charAt"), "string has no method '%s'", e->str);
    }
}

/* Stream<T>: pull-based, lazy, typed. Sources: lines(path), chunks(path, bytes). */
static void check_stream_member(Expr *e) {
    Type *t = e->lhs->type;
    Type *elem = t->elem;
    const char *m = e->str;
    if (!e->is_call) xs_error(XS7038, e->pos, "streams have methods only: map, filter, batch, take, forEach, reduce, toArray, count, writeLines", "Stream has no property '%s'", m);
    #define NARGS(n) do { if (e->args.len != (n)) xs_error(XS7039, e->pos, NULL, "Stream.%s takes %d argument(s), got %d", m, (n), e->args.len); } while (0)
    if (strcmp(m, "map") == 0 || strcmp(m, "filter") == 0 || strcmp(m, "forEach") == 0) {
        NARGS(1);
        Expr *cb = e->args.items[0];
        Type **ps = xs_alloc(sizeof(Type *)); ps[0] = elem;
        bool is_map = strcmp(m, "map") == 0;
        Type *cbt = ty_func(ps, 1, is_map ? NULL : strcmp(m, "filter") == 0 ? ty_boolean : ty_void);
        if (is_map && !(cb->kind == E_ARROW && !cb->arrow->fn->ret)) { check_expr(cb, NULL); }
        else check_expr(cb, cbt);
        if (cb->type->kind != TY_FUNC || cb->type->nparams != 1 || !ty_equal(cb->type->params[0], elem))
            xs_error(XS7100, cb->pos, NULL, "the callback of Stream.%s takes one %s", m, ty_name(elem));
        if (!is_map) expect_type(cb, cbt, xs_sprintf("callback of Stream.%s", m));
        if (is_map) {
            Type *rt = cb->type->ret;
            if (rt->kind == TY_VOID || rt->kind == TY_FUNC || rt->kind == TY_NULLABLE) xs_error(XS7063, cb->pos, NULL, "map cannot produce a stream of %s", ty_name(rt));
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
        if (elem->kind != TY_STRING) xs_error(XS7122, e->pos, "map the elements to strings first", "writeLines needs a Stream<string>, this is %s", ty_name(t));
        check_expr(e->args.items[0], ty_string);
        expect_type(e->args.items[0], ty_string, "path of writeLines");
        e->type = ty_void;
        e->builtin.cfn = m;
        return;
    }
    #undef NARGS
    static const char *names[] = {"map", "filter", "batch", "take", "forEach", "reduce", "toArray", "count", "writeLines"};
    xs_error(XS7037, e->pos, xs_help_suggest(m, names, 9, NULL), "Stream has no method '%s'", m);
}

static void check_map_member(Expr *e) {
    Type *t = e->lhs->type;
    bool is_map = t->kind == TY_MAP;
    Type *k = is_map ? t->key : t->elem;
    Type *v = t->elem;
    const char *m = e->str;
    if (!e->is_call && is_map && t->record) { /* m.k on a Record: m.get("k") */
        Expr *key = xs_alloc(sizeof *key);
        key->kind = E_STRING; key->pos = e->pos; key->field = -1; key->str = (char *)m;
        key->type = ty_string; key->lit_id = intern(m);
        e->str = "get";
        e->is_call = true;
        e->args = (ExprList){0};
        expr_list_push(&e->args, key);
        m = e->str;
    }
    if (!e->is_call) {
        if (strcmp(m, "size") == 0) { e->type = ty_int; e->builtin.cfn = "size"; return; }
        xs_error(XS8015, e->pos, is_map ? "Map has: size, get, set, has, delete, clear, keys, values, entries" : "Set has: size, add, has, delete, clear, values", "%s has no property '%s'", ty_name(t), m);
    }
    #define ARGS(n) do { if (e->args.len != (n)) xs_error(XS8038, e->pos, NULL, "%s.%s takes %d argument(s), got %d", is_map ? "Map" : "Set", m, (n), e->args.len); } while (0)
    bool k_done = false; /* the key argument is already checked (below) */
    #define ARG(i, ty, what) do { if (!((i) == 0 && k_done)) { check_expr(e->args.items[i], ty); expect_type(e->args.items[i], ty, what); } } while (0)
    /* m.get(n) / m.has(n) on int keys with n: number (Record<number, V>, a literal-union key): a number that is not whole is
       a key no entry has, as in JavaScript (only set() requires a whole number) */
    if (is_map && k->kind == TY_INT && e->args.len >= 1 && (strcmp(m, "get") == 0 || strcmp(m, "has") == 0 || strcmp(m, "delete") == 0)) {
        Expr *a0 = e->args.items[0];
        check_expr(a0, k);
        Type *at = a0->type;
        if (at->kind == TY_NUMBER || (at->kind == TY_UNION && at->nparams > 0 && is_numeric(at->params[0]))) {
            if (at->kind == TY_UNION) { to_unknown(a0); dyn_wrap(a0, "xs_dyn_num", ty_number); }
            wrap_call(a0, "xs_num_key_find", ty_int);
        }
        else expect_type(a0, k, "Map key");
        k_done = true;
    }
    if (is_map && t->record && k->kind == TY_STRING && e->args.len >= 1 && strcmp(m, "values") != 0 && strcmp(m, "keys") != 0) {
        /* binds[i] = v / rec[3] on a Record<string, V>: the number's text is the key, as JavaScript makes it */
        Expr *a0 = e->args.items[0];
        check_expr(a0, NULL);
        if (a0->type->kind == TY_LITINT) coerce_lit(a0, ty_int);
        if (a0->type->kind == TY_INT) wrap_call(a0, "xs_str_from_int", ty_string);
        else if (a0->type->kind == TY_NUMBER) wrap_call(a0, "xs_str_from_num", ty_string);
        else expect_type(a0, k, "Map key");
        k_done = true;
    }
    if (is_map && k->kind == TY_INT && strcmp(m, "set") == 0 && e->args.len == 2) {
        Expr *a0 = e->args.items[0];
        check_expr(a0, k);
        if (a0->type->kind == TY_NUMBER) wrap_call(a0, "xs_num_key", ty_int);
        else expect_type(a0, k, "Map key");
        k_done = true;
    }
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
    xs_error(XS8014, e->pos, xs_help_suggest(m, mnames, 9, NULL), "%s has no method '%s'", ty_name(t), m);
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
/* class X extends Error: fields message and name come first, laid out as the runtime's Error (xs_Error_wrap_err
   reads them when the object is thrown); without a constructor, X's is constructor(message = "") { super(message) } */
static void error_class(Class *c) {
    c->extends_error = true;
    const char *names[2] = {"message", "name"}, *inits[2] = {"", "Error"};
    for (int k = 1; k >= 0; k--) { /* name, then message in front of it */
        int at = -1;
        for (int i = 0; i < c->nfields; i++) if (strcmp(c->fields[i].name, names[k]) == 0) at = i;
        Field f;
        if (at >= 0) { /* declared by the class (name = "QueryFailedError"): moved to the front */
            f = c->fields[at];
            memmove(&c->fields[1], &c->fields[0], sizeof(Field) * (size_t)at);
        } else {
            memset(&f, 0, sizeof f);
            f.name = (char *)names[k];
            f.pos = c->pos;
            f.type = ty_string;
            f.type_src = "string";
            f.owner = c;
            f.init = str_lit(c->pos, inits[k]);
            c->fields = realloc(c->fields, sizeof(Field) * (size_t)(c->nfields + 1));
            c->cap_fields = c->nfields + 1;
            memmove(&c->fields[1], &c->fields[0], sizeof(Field) * (size_t)c->nfields);
            c->nfields++;
        }
        c->fields[0] = f;
    }
    if (c->ctor) return;
    Func *f = xs_alloc(sizeof *f);
    f->name = "constructor";
    f->pos = c->pos;
    f->mod = c->mod;
    f->cls = c;
    f->is_ctor = true;
    f->ret = ty_void;
    f->exported = c->exported;
    f->params = xs_alloc(sizeof(Param));
    f->nparams = 1;
    f->params[0].name = "message";
    f->params[0].type = ty_string;
    f->params[0].type_src = "string";
    f->params[0].dflt = str_lit(c->pos, "");
    Expr *sup = xs_alloc(sizeof *sup), *arg = xs_alloc(sizeof *arg), *call = xs_alloc(sizeof *call);
    sup->kind = E_IDENT; sup->pos = c->pos; sup->str = "super"; sup->field = -1;
    arg->kind = E_IDENT; arg->pos = c->pos; arg->str = "message"; arg->field = -1;
    call->kind = E_CALL; call->pos = c->pos; call->lhs = sup; call->field = -1;
    expr_list_push(&call->args, arg);
    Stmt *st = xs_alloc(sizeof *st), *body = xs_alloc(sizeof *body);
    st->kind = S_EXPR; st->pos = c->pos; st->expr = call;
    body->kind = S_BLOCK; body->pos = c->pos;
    stmt_list_push(&body->stmts, st);
    f->body = body;
    c->ctor = f;
}
/* class X extends PartialType(Y): Y's fields (its bases' first) copied into X, optional for PartialType, without
   the keys OmitType leaves out or with only those PickType keeps; X extends nothing (as in Nest, a new class) */
typedef struct { Field *f; int n; } FieldSet;
static void mixin_eval(Mixin *m, FieldSet *out);
static void apply_mixin(Class *c);
static void fs_add(FieldSet *out, Field f) {
    for (int i = 0; i < out->n; i++) if (strcmp(out->f[i].name, f.name) == 0) { out->f[i] = f; return; } /* redeclared below */
    out->f = realloc(out->f, sizeof(Field) * (size_t)(out->n + 1));
    out->f[out->n++] = f;
}
static void flatten_fields(Class *c, int depth);
static void class_fields_all(Class *k, FieldSet *out) {
    if (k->alias && k->alias->kind == TY_OBJECT && k->alias->cls && k->alias->cls != k && !k->alias->ntargs) {
        class_fields_all(k->alias->cls, out); /* type Ped = A & B, then Ped & C: the fields of what Ped names */
        return;
    }
    if (k->mixin && !k->mixin_done) { Module *was = cur_mod; apply_mixin(k); enter_module(was); }
    if (k->niface_bases && !k->flattened) { Module *was = cur_mod; flatten_fields(k, 0); enter_module(was); }
    if (k->base_name) {
        Module *was = cur_mod;
        enter_module(k->mod);
        Class *b = k->base ? k->base : find_class(k->base_name);
        enter_module(was);
        if (b && !b->builtin) class_fields_all(b, out);
    }
    Module *was = cur_mod;
    enter_module(k->mod);
    for (int i = 0; i < k->nfields; i++) {
        Field f = k->fields[i];
        if (f.type && !k->ntparams) f.type = resolve(f.type, f.pos); /* in the module that wrote it: its names are there */
        fs_add(out, f);
    }
    enter_module(was);
}
static bool key_in(Mixin *m, const char *name) {
    for (int i = 0; i < m->nkeys; i++) if (strcmp(m->keys[i], name) == 0) return true;
    return false;
}
static void mixin_eval(Mixin *m, FieldSet *out) {
    if (!m->kind) {
        Class *k = m->klass ? m->klass : find_class(m->cls);
        if (!k || k->builtin) xs_error(XS9031, m->pos, NULL, "unknown class '%s'", m->cls);
        Module *was = cur_mod;
        enter_module(k->mod);
        class_fields_all(k, out);
        enter_module(was);
        return;
    }
    FieldSet a = {0};
    mixin_eval(m->a, &a);
    bool partial = strcmp(m->kind, "PartialType") == 0, omit = strcmp(m->kind, "OmitType") == 0, pick = strcmp(m->kind, "PickType") == 0;
    for (int i = 0; i < a.n; i++) {
        Field f = a.f[i];
        if ((omit && key_in(m, f.name)) || (pick && !key_in(m, f.name))) continue;
        if (partial && !f.optional) {
            f.optional = true; /* (the initializer stays: Nest's mapped types inherit them) */
            f.type = ty_optional_of(f.type);
        }
        if (strcmp(m->kind, "RequiredType") == 0 && f.optional) { /* Required<A>: every field present */
            f.optional = false;
            f.type = ty_strip_null(f.type);
        }
        fs_add(out, f);
    }
    if (m->b) mixin_eval(m->b, out);
}
static void apply_mixin(Class *c) {
    if (c->mixin_done) return;
    c->mixin_done = true;
    FieldSet fs = {0};
    mixin_eval(c->mixin, &fs);
    for (int i = 0; i < c->nfields; i++) fs_add(&fs, c->fields[i]);
    for (int i = 0; i < fs.n; i++) fs.f[i].owner = c;
    c->fields = fs.f;
    c->nfields = c->cap_fields = fs.n;
}
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
    case TY_ARRAY: case TY_NULLABLE: case TY_SET: case TY_STREAM: case TY_CHAN: case TY_PROMISE: return has_tparam(t->elem);
    case TY_MAP: return has_tparam(t->key) || has_tparam(t->elem);
    case TY_FUNC:
        for (int i = 0; i < t->nparams; i++) if (has_tparam(t->params[i])) return true;
        return has_tparam(t->ret);
    case TY_OBJECT:
        for (int i = 0; i < t->ntargs; i++) if (has_tparam(t->targs[i])) return true;
        return false;
    case TY_UNION:
        for (int i = 0; i < t->nparams; i++) if (has_tparam(t->params[i])) return true;
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
    if (pat->kind == TY_UNION && got->kind != TY_UNION) { /* Promise<T> | T against a value or a promise of one: T */
        Type *g = got->kind == TY_PROMISE ? got->elem : got;
        for (int m = 0; m < pat->nparams; m++) {
            Type *pm = pat->params[m];
            if (pm->kind == TY_PROMISE) unify(pm->elem, g, tmpl, bind);
            else if (pm->kind == TY_TPARAM) unify(pm, g, tmpl, bind);
        }
        return;
    }
    if (pat->kind == TY_PROMISE && got->kind != TY_PROMISE) { unify(pat->elem, got, tmpl, bind); return; } /* () => Promise<T> against an async function's R */
    if (pat->kind == TY_OBJECT && pat->ntargs && got->kind == TY_UNION) { /* Item<T> (a generic alias) against Item<number>: its
                                                                            members carry the alias's arguments */
        for (int m = 0; m < got->nparams; m++) {
            Type *mt = got->params[m];
            if (mt->kind != TY_OBJECT || mt->ntargs != pat->ntargs) continue;
            for (int i = 0; i < pat->ntargs; i++) unify(pat->targs[i], mt->targs[i], tmpl, bind);
        }
        return;
    }
    if (pat->kind != got->kind) return;
    switch (pat->kind) {
    case TY_ARRAY: case TY_SET: case TY_STREAM: case TY_CHAN: case TY_PROMISE: unify(pat->elem, got->elem, tmpl, bind); return;
    case TY_MAP: unify(pat->key, got->key, tmpl, bind); unify(pat->elem, got->elem, tmpl, bind); return;
    case TY_FUNC:
        if (pat->nparams != got->nparams) return;
        for (int i = 0; i < pat->nparams; i++) unify(pat->params[i], got->params[i], tmpl, bind);
        if (pat->ret && pat->ret->kind == TY_TPARAM && got->ret && got->ret->kind == TY_VOID) {
            /* run<R>(fn: () => R) with a void callback: R is void (only here: a void value is never an argument) */
            for (int i = 0; i < tmpl->ntparams; i++)
                if (strcmp(tmpl->tparams[i], pat->ret->tname) == 0 && !bind[i]) bind[i] = got->ret;
            return;
        }
        if (pat->ret && pat->ret->kind == TY_PROMISE && pat->ret->elem->kind == TY_TPARAM && got->ret &&
            (got->ret->kind == TY_VOID || (got->ret->kind == TY_PROMISE && got->ret->elem->kind == TY_VOID))) {
            /* protege<T>(fn: () => Promise<T>) with an async callback that gives nothing: T is void */
            for (int i = 0; i < tmpl->ntparams; i++)
                if (strcmp(tmpl->tparams[i], pat->ret->elem->tname) == 0 && !bind[i]) bind[i] = ty_void;
            return;
        }
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
/* a type's identity for the instance cache: two interfaces both named Row in two modules are two types (XB-117) */
static void ty_key(Buf *b, Type *t, int depth) {
    if (!t) { buf_puts(b, "_"); return; }
    if (depth > 8) { buf_puts(b, ty_name(t)); return; }
    buf_printf(b, "%d", (int)t->kind);
    if (t->kind == TY_OBJECT && t->cls) buf_printf(b, "@%p", (void *)t->cls);
    if (t->tname) buf_printf(b, "'%s'", t->tname);
    if (t->record) buf_puts(b, "r");
    if (t->absent) buf_puts(b, "a");
    if (t->elem) { buf_puts(b, "<"); ty_key(b, t->elem, depth + 1); buf_puts(b, ">"); }
    if (t->key) { buf_puts(b, "{"); ty_key(b, t->key, depth + 1); buf_puts(b, "}"); }
    for (int i = 0; i < t->nparams; i++) { buf_puts(b, i ? "," : "("); ty_key(b, t->params[i], depth + 1); }
    if (t->nparams) buf_puts(b, ")");
    if (t->kind == TY_FUNC) { buf_puts(b, "=>"); ty_key(b, t->ret, depth + 1); }
    for (int i = 0; i < t->ntargs; i++) { buf_puts(b, i ? "," : "["); ty_key(b, t->targs[i], depth + 1); }
    if (t->ntargs) buf_puts(b, "]");
}
static Func *instantiate_func(Func *tmpl, Type **args) {
    Buf key = {0};
    for (int i = 0; i < tmpl->ntparams; i++) { buf_puts(&key, i ? ";" : ""); ty_key(&key, args[i], 0); }
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
/* t with the type parameters bound so far replaced (the others kept, so has_tparam still sees them) */
static Type *subst_bound(Type *t, Func *tmpl, Type **bind) {
    if (!t || !has_tparam(t)) return t;
    switch (t->kind) {
    case TY_TPARAM:
        for (int i = 0; i < tmpl->ntparams; i++) if (bind[i] && strcmp(tmpl->tparams[i], t->tname) == 0) return bind[i];
        return t;
    case TY_ARRAY: { Type *el = subst_bound(t->elem, tmpl, bind); return has_tparam(el) ? t : ty_array_of(el); }
    case TY_NULLABLE: { Type *el = subst_bound(t->elem, tmpl, bind); return has_tparam(el) ? t : ty_nullable_of(el); }
    default: return t; /* (maps, sets, functions of T: left to the second check, as before) */
    }
}
static bool names_tparam(Type *t, const char *n) {
    if (!t) return false;
    if (t->kind == TY_TPARAM) return t->tname && strcmp(t->tname, n) == 0;
    if (t->elem && names_tparam(t->elem, n)) return true;
    if (t->key && names_tparam(t->key, n)) return true;
    if (t->kind == TY_FUNC || t->kind == TY_UNION) { for (int i = 0; i < t->nparams; i++) if (names_tparam(t->params[i], n)) return true; }
    if (t->kind == TY_FUNC && names_tparam(t->ret, n)) return true;
    if (t->kind == TY_OBJECT) for (int i = 0; i < t->ntargs; i++) if (names_tparam(t->targs[i], n)) return true;
    return false;
}
static bool has_tparam_in_params(Func *tmpl, const char *n) {
    for (int i = 0; i < tmpl->nparams; i++) if (names_tparam(tmpl->params[i].type, n)) return true;
    return false;
}
/* a call of a generic function or method: T from the explicit types, the arguments, or the expected result */
static Func *check_generic_call(Expr *e, Func *tmpl, Type *expected, const char *what) {
    Type **bind = xs_alloc(sizeof(Type *) * (size_t)tmpl->ntparams);
    bool partial = false;
    if (e->ntargs) {
        partial = e->ntargs < tmpl->ntparams && strncmp(tmpl->name, "__xs_", 5) == 0; /* xs.reduce<A>(...): A given, T inferred */
        if (e->ntargs != tmpl->ntparams && !partial) xs_error(XS8028, e->pos, NULL, "%s takes %d type argument(s), got %d", what, tmpl->ntparams, e->ntargs);
        for (int i = 0; i < e->ntargs; i++) bind[i] = resolve(e->targs[i], e->pos);
    }
    int required = tmpl->nparams;
    while (required > 0 && tmpl->params[required - 1].dflt) required--;
    if (e->args.len < required || e->args.len > tmpl->nparams)
        xs_error(XS8029, e->pos, NULL, "%s takes %d%s argument(s), got %d", what, required, required == tmpl->nparams ? "" : xs_sprintf(" to %d", tmpl->nparams), e->args.len);
    for (int i = 0; i < e->args.len && i < tmpl->nparams; i++) { /* manager.findOne(User, ...) with entity: EntityTarget<T>: T is User */
        Type *pt = tmpl->params[i].type;
        Expr *a = e->args.items[i];
        if (!pt || pt->kind != TY_OBJECT || !pt->cls || !pt->cls->name || pt->ntargs != 1 || pt->targs[0]->kind != TY_TPARAM ||
            (strcmp(pt->cls->name, "EntityTarget") != 0 && strcmp(pt->cls->name, "TypeOrToken") != 0)) continue;
        /* (TypeOrToken<T>: app.get(ConfigService) gives a ConfigService, as Nest's get<T>(type: Type<T>): T) */
        if (a->kind == E_AS) { /* manager.findOne("TB_X" as never, ...): an entity by name; T is any (TypeORM's ObjectLiteral) */
            for (int k = 0; k < tmpl->ntparams; k++)
                if (!bind[k] && strcmp(tmpl->tparams[k], pt->targs[0]->tname) == 0) bind[k] = ty_any;
            continue;
        }
        if (a->kind != E_IDENT || lookup(a->str)) continue;
        Class *ac = find_class(a->str);
        if (!ac || ac->builtin) continue;
        for (int k = 0; k < tmpl->ntparams; k++)
            if (!bind[k] && strcmp(tmpl->tparams[k], pt->targs[0]->tname) == 0) bind[k] = ty_object_of(ac);
    }
    int ndone = e->args.len;
    bool *done = xs_alloc(sizeof(bool) * (size_t)(e->args.len + 1));
    /* two passes: the arguments that are not arrows first (they bind T from their own types), then the arrows,
       whose untyped parameters take the types bound so far: parallelMap(items: T[], fn: (item: T) => R) with
       (s) => s.length gets s: string from items */
    for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < e->args.len; i++) {
        if (!has_tparam(tmpl->params[i].type) || (e->ntargs && !partial)) continue;
        bool arrow = e->args.items[i]->kind == E_ARROW;
        if (arrow != (pass == 1)) continue;
        Type *pt = tmpl->params[i].type, *ctx = NULL;
        if (arrow && pt->kind == TY_UNION) { /* trabalho: Promise<T> | ((sinal: S) => Promise<T>) given an arrow: its function type */
            Type *fm = NULL;
            int nf = 0;
            for (int k = 0; k < pt->nparams; k++) if (pt->params[k]->kind == TY_FUNC) { fm = pt->params[k]; nf++; }
            if (nf == 1) pt = fm;
        }
        if (arrow && pt->kind == TY_FUNC) { /* map<U>(f: (x: T) => U) with (x) => ...: x from the parameter (or from T
                                               bound by an earlier argument), U from what the arrow returns */
            bool params_known = true;
            Type **ps = xs_alloc(sizeof(Type *) * (size_t)(pt->nparams + 1));
            for (int k = 0; k < pt->nparams; k++) {
                ps[k] = subst_bound(pt->params[k], tmpl, bind);
                if (has_tparam(ps[k])) params_known = false;
            }
            if (params_known) {
                bool pt_async = pt->async_ret;
                Type *rt = subst_bound(ret_unpromise(pt->ret, &pt_async), tmpl, bind); /* (fn: () => Promise<T>): async, R = T */
                /* the template's types are as written (names not yet looked up): resolved where it is written, so
                   the arrow's (c) is the same Conn its body passes on */
                Module *dm = tmpl->cls ? tmpl->cls->mod : tmpl->mod;
                Module *was = cur_mod;
                Scope *ss = scope;
                const char *sfn = xs_file_name, *ssrc = xs_source;
                if (dm && dm != cur_mod) enter_module(dm);
                for (int k = 0; k < pt->nparams; k++) ps[k] = resolve(ps[k], e->pos);
                if (rt && !has_tparam(rt)) rt = resolve(rt, e->pos);
                cur_mod = was; scope = ss; xs_file_name = sfn; xs_source = ssrc;
                ctx = ty_func(ps, pt->nparams, has_tparam(rt) ? NULL : rt);
                ctx->async_ret = pt_async; /* (fn: () => Promise<T>) with () => c ? f() : Promise.resolve(x): async */
            }
        }
        if (!arrow && partial) { /* xs.reduce<Record<string, V>>(f, {}): the type given for A is the {}'s */
            Type *sb = subst_bound(pt, tmpl, bind);
            if (!has_tparam(sb)) {
                Module *dm = tmpl->cls ? tmpl->cls->mod : tmpl->mod;
                Module *was = cur_mod;
                Scope *ss = scope;
                const char *sfn = xs_file_name, *ssrc = xs_source;
                if (dm && dm != cur_mod) enter_module(dm);
                ctx = resolve(sb, e->pos);
                cur_mod = was; scope = ss; xs_file_name = sfn; xs_source = ssrc;
            }
        }
        check_expr(e->args.items[i], ctx);
        done[i] = true;
        unify(arrow ? pt : tmpl->params[i].type, e->args.items[i]->type, tmpl, bind);
    }
    if (expected && tmpl->ret && has_tparam(tmpl->ret) && !ty_is_any(expected) && expected->kind != TY_UNKNOWN && expected->kind != TY_VOID)
        unify(tmpl->ret, expected, tmpl, bind);
    for (int i = 0; i < tmpl->ntparams; i++) /* <T = string>: what nothing bound takes its default, read where it is written */
        if (!bind[i] && tmpl->tdefaults && tmpl->tdefaults[i]) {
            Module *dm = tmpl->cls ? tmpl->cls->mod : tmpl->mod;
            Module *was = cur_mod;
            Scope *ss = scope;
            const char *sfn = xs_file_name, *ssrc = xs_source;
            if (dm && dm != cur_mod) enter_module(dm);
            bind[i] = resolve(tmpl->tdefaults[i], e->pos);
            cur_mod = was; scope = ss; xs_file_name = sfn; xs_source = ssrc;
        }
    for (int i = 0; i < tmpl->ntparams; i++) /* T that nothing gives (only in the result, as executeQuery<T>(sql)): unknown, as TypeScript infers it */
        if (!bind[i] && !has_tparam_in_params(tmpl, tmpl->tparams[i])) { /* XB-118 rule 3 */
            xs_warn_rule(XS2104, e->pos, xs_sprintf("pass it explicitly: %s<Type>(...)", tmpl->name),
                         "type parameter '%s' of %s could not be inferred: it is unknown", tmpl->tparams[i], what);
            bind[i] = ty_unknown;
        }
    for (int i = 0; i < tmpl->ntparams; i++)
        if (!bind[i]) xs_error(XS3010, e->pos, xs_sprintf("give it: %s<Type>(...)", tmpl->name), "cannot infer type parameter %s of %s", tmpl->tparams[i], what);
    Func *inst = instantiate_func(tmpl, bind);
    for (int i = e->args.len, given = e->args.len; i < inst->nparams; i++) /* defaults, as written for this instance */
        expr_list_push(&e->args, dflt_arg(inst, i, e->args.items, given, e->pos));
    for (int i = 0; i < e->args.len; i++) {
        if (i >= ndone || !done[i]) check_expr(e->args.items[i], inst->params[i].type); /* (defaults come after the given ones) */
        expect_type(e->args.items[i], inst->params[i].type, xs_sprintf("argument '%s' of %s", inst->params[i].name, what));
    }
    return inst;
}

/* private: the declaring class only; protected: it and its subclasses (checked at compile time, as TypeScript) */
static bool checking_generated; /* the compiler's own code (reflection adapters) reaches private members */
static void check_access(int access, Class *owner, const char *name, Pos pos) {
    if (!access || !owner || checking_generated) return;
    Class *in = cur_class ? cur_class : cur_func ? (cur_func->cls ? cur_func->cls : cur_func->static_of) : NULL;
    if (in && in != owner && (in->generic_of ? in->generic_of : in) == (owner->generic_of ? owner->generic_of : owner))
        in = owner; /* Query<L, T> written in Query<R, T>'s code (lean()): one class for TypeScript, two instances here */
    if (access == 2 && in != owner)
        xs_error(XS8055, pos, xs_sprintf("use it inside %s, or make it public", owner->name), "'%s' is private to %s", name, owner->name);
    if (access == 1 && !(in && is_subclass(in, owner)))
        xs_error(XS8056, pos, xs_sprintf("use it inside %s or a class that extends it", owner->name), "'%s' is protected in %s", name, owner->name);
}
static const char *prop_name(const char *m) { return strncmp(m, "__get_", 6) == 0 || strncmp(m, "__set_", 6) == 0 ? m + 6 : m; }

static Type *member_expected; /* the type the caller expects of o.m(...) (a generic method's T from its result, as TypeScript) */
static void check_object_member(Expr *e, Class *c) {
    Type *mexp = member_expected;
    member_expected = NULL;
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
    bool null_fn = f && e->is_call && e->opt_call && f->type->kind == TY_NULLABLE && f->type->elem->kind == TY_FUNC;
    bool narrowed_fn = f && e->is_call && !e->opt_call && f->type->kind == TY_NULLABLE && f->type->elem->kind == TY_FUNC && tsc_checked();
    if (f && e->is_call && (f->type->kind == TY_FUNC || null_fn || narrowed_fn)) { /* obj.run(x) where run is a function-valued field (obj.run?.(x): may be null) */
        Type *ft = f->type->kind == TY_NULLABLE ? f->type->elem : f->type;
        Expr *fld = expr_copy(e);
        fld->is_call = false;
        fld->args.len = 0;
        fld->args.items = NULL;
        fld->field = idx;
        fld->type = null_fn ? f->type : ft;
        if (!null_fn && f->type->kind == TY_NULLABLE) { /* this.f(x) after if (this.f): tsc narrowed it; read as this.f! */
            fld->type = f->type;
            nonnull_checked(fld, xs_sprintf("'%s'", f->name));
        }
        e->kind = E_CALL;
        e->lhs = fld;
        e->func = NULL;
        e->cls = NULL;
        pack_rest(e, ft);
        if (e->args.len != ft->nparams) xs_error(XS8062, e->pos, NULL, "'%s' takes %d argument(s), got %d", f->name, ft->nparams, e->args.len);
        for (int i = 0; i < e->args.len; i++) {
            check_expr(e->args.items[i], ft->params[i]);
            expect_type(e->args.items[i], ft->params[i], xs_sprintf("argument %d of '%s'", i + 1, f->name));
        }
        e->type = ft->ret;
        if (null_fn && ft->ret->kind != TY_VOID) e->type = ty_nullable_of(ft->ret);
        e->opt_call = null_fn;
        if (!null_fn) { /* deps.queryOne<Row>(sql): see gen_call_ret */
            e->gen_ret = gen_call_ret(ft, e);
            if (e->gen_ret && !ft->async_ret) gen_convert(e);
        }
        return;
    }
    if (c->is_tuple && !e->is_call && strcmp(e->str, "length") == 0) { /* t.length: a constant */
        long long n = c->nfields;
        e->kind = E_NUMBER; e->is_int_lit = true; e->ival = n; e->num = (double)n; e->lhs = NULL; e->field = -1; e->type = ty_int;
        return;
    }
    if (f) {
        if (e->is_call) xs_error(XS8048, e->pos, NULL, "'%s' is a field of %s, not a method", e->str, c->name);
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
        if (!g && find_method(c, xs_sprintf("__set_%s", e->str))) xs_error(XS8060, e->pos, "add get %s(): T { ... }", "'%s' of %s has a setter but no getter", e->str, c->name);
        if (g) { e->str = g->name; e->is_call = true; }
    }
    if (e->is_call && e->args.len == 0 && !strcmp(e->str, "digest") && c->mod && c->mod->file && !strcmp(c->mod->file, "xisto:crypto") &&
        (!strcmp(c->name, "Hash") || !strcmp(c->name, "Hmac")))
        e->str = "__digestBytes"; /* hash.digest(): a Buffer (with an encoding, text) */
    Func *m = find_method(c, e->str);
    if (!m && !e->is_call && !c->builtin) {
        /* an open class (a field `__expando: Record<string, any>`, as Express's Request): a property the class does
           not declare lives there, as JavaScript adds it to the object (request.terminal = t; req.headers['x-y']) */
        int xi;
        Field *xf = find_field(c, "__expando", &xi);
        if (xf && xf->type->kind == TY_MAP && xf->type->record) {
            Expr *holder = xs_alloc(sizeof *holder);
            holder->kind = E_MEMBER; holder->pos = e->pos; holder->field = -1; holder->lhs = e->lhs; holder->str = "__expando";
            e->lhs->prechecked = true; /* (checked: its type is the open class) */
            Expr *key = xs_alloc(sizeof *key);
            key->kind = E_STRING; key->pos = e->pos; key->field = -1; key->str = e->str; key->type = ty_string; key->lit_id = intern(e->str);
            e->kind = E_INDEX;
            e->lhs = holder;
            e->rhs = key;
            e->str = NULL;
            e->field = -1;
            e->cls = NULL;
            check_expr(e, NULL);
            return;
        }
    }
    if (!m) {
        int n; const char **cands = member_names(c, &n);
        if (c->builtin && strcmp(c->name, "Error") == 0) { cands = (const char *[]){"message", "name", "stack"}; n = 3; }
        if (c->builtin && strcmp(c->name, "Error") == 0 && !e->is_call && tsc_checked()) {
            /* error.code, error.errorNum (a driver's error, caught as Error): read by name, undefined when it has no such field */
            xs_note_dynamic(e->pos);
            to_any(e->lhs);
            dyn_call(e, e->op == TK_QDOT ? "xs_dyn_get_opt" : "xs_dyn_get", ty_any, e->lhs, dyn_key(e->pos, e->str), NULL);
            return;
        }
        xs_error(XS8013, e->pos, xs_help_suggest(e->str, cands, n, NULL), "%s has no member '%s'", c->name, e->str);
    }
    if (!e->is_call) xs_error(XS8049, e->pos, "call it: obj.method(...)", "'%s' is a method of %s, and methods are not values", e->str, c->name);
    if (!c->builtin) check_access(m->access, m->cls, prop_name(m->name), e->pos);
    if (m->is_setter && e->args.len == 1 && strncmp(e->str, "__set_", 6) != 0) xs_error(XS8050, e->pos, NULL, "'%s' is a setter: assign it", prop_name(m->name));
    if (m->ntparams) { /* a generic method: its instance for these types */
        Func *inst = check_generic_call(e, m, mexp, xs_sprintf("%s.%s", c->name, m->name));
        e->func = inst;
        e->type = fret(inst, e->pos);
        return;
    }
    Type *cast_to = NULL; /* s.query<Row[]>(...) on an interface's query<T = any>: the any it gives, read as Row[] */
    if (e->ntargs == 1 && m->ret && (ty_is_any(m->ret) || (m->ret->kind == TY_PROMISE && ty_is_any(m->ret->elem)))) {
        cast_to = resolve(e->targs[0], e->pos);
        e->ntargs = 0;
    }
    Type *mgen = NULL; /* ds.query<Row>(sql) on an interface's query<T = D>(): the result as the signature gives it for Row */
    if (e->ntargs && m->gen_sig) {
        Type tmpf = *m->gen_sig;
        mgen = gen_call_ret(&(Type){.kind = TY_FUNC, .gen_sig = &tmpf}, e);
        e->ntargs = 0;
    }
    if (e->ntargs) xs_error(XS6003, e->pos, NULL, "%s.%s is not generic", c->name, m->name);
    Expr *recv0 = dflt_recv;
    dflt_recv = e->lhs;
    check_args(e, m, xs_sprintf("%s.%s", c->name, m->name));
    dflt_recv = recv0;
    e->func = m;
    e->type = fret(m, e->pos);
    if (mgen) {
        e->gen_ret = mgen;
        if (!(m->is_async || m->ret_promise)) gen_convert(e);
    }
    if (!c->builtin && !e->super_call && (m->is_abstract || overridden_below(c, m))) { /* dispatched by the object's class */
        e->virt = true;
        m->virt_target = true;
        for (int i = 0; i < m->noverrides; i++) m->overrides[i]->virt_target = true;
    }
    if (c->builtin) {
        e->builtin.cfn = m->cname;
        e->builtin.self_first = true;
    }
    if (cast_to && ty_is_any(e->type) && !ty_is_any(cast_to) && !dyn_to(e, cast_to))
        xs_error(XS2010, e->pos, NULL, "cannot check %s at run time", ty_name(cast_to));
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
/* { a: 1, b: 'x' } where no type is expected (const p = { ... }, return { ... } with the return type left out): an
   interface of the values' types, the same one for every literal of the same shape (so two such literals have one type) */
static void infer_objlit(Expr *e) {
    static Class **made;
    static int nmade;
    Type **ts = xs_alloc(sizeof(Type *) * (size_t)(e->args.len ? e->args.len : 1));
    for (int i = 0; i < e->args.len; i++) {
        Expr *v = e->args.items[i];
        for (int k = 0; k < i; k++) if (strcmp(e->keys[k], e->keys[i]) == 0) xs_error(XS9024, v->pos, NULL, "field '%s' is given twice", e->keys[i]);
        check_expr(v, NULL);
        settle(v);
        if (v->type->kind == TY_NULL) v->type = ty_unknown; /* { a: null } with no declared type: an unknown field that holds null */
        if (v->type->kind == TY_VOID) xs_error(XS8102, v->pos, NULL, "cannot store a void value");
        ts[i] = v->type->kind == TY_INT && v->kind != E_NUMBER ? ty_number : v->type; /* (TypeScript: a number) */
        if (ts[i] != v->type) expect_type(v, ts[i], xs_sprintf("field '%s'", e->keys[i]));
    }
    Class *c = NULL;
    for (int m = 0; m < nmade && !c; m++) {
        Class *o = made[m];
        if (o->nfields != e->args.len) continue;
        bool same = true;
        for (int i = 0; i < e->args.len && same; i++)
            same = strcmp(o->fields[i].name, e->keys[i]) == 0 && ty_equal(o->fields[i].type, ts[i]) &&
                   o->fields[i].optional == (ts[i]->kind == TY_NULLABLE && ts[i]->absent);
        if (same) c = o;
    }
    if (!c) {
        c = xs_alloc(sizeof *c);
        Buf name = {0};
        buf_puts(&name, "{ ");
        for (int i = 0; i < e->args.len; i++) buf_printf(&name, "%s%s: %s", i ? "; " : "", e->keys[i], ty_name(ts[i]));
        buf_puts(&name, " }");
        c->name = name.data;
        c->pos = e->pos;
        c->is_interface = true;
        c->mod = cur_mod;
        c->cname = xs_sprintf("lit%d", nmade);
        c->used = true;
        c->fields = xs_alloc(sizeof(Field) * (size_t)(e->args.len ? e->args.len : 1));
        for (int i = 0; i < e->args.len; i++)
            c->fields[i] = (Field){.name = e->keys[i], .pos = e->args.items[i]->pos, .type = ts[i], .cname = field_cname(e->keys[i]), .owner = c,
                                   .optional = ts[i]->kind == TY_NULLABLE && ts[i]->absent}; /* (k: c ? v : undefined: absent when undefined) */
        c->nfields = c->cap_fields = e->args.len;
        c->flattened = true;
        prog->classes = realloc(prog->classes, sizeof(Class *) * (size_t)(prog->nclasses + 1));
        prog->cap_classes = prog->nclasses + 1; /* keep the parser's add_class in step */
        prog->classes[prog->nclasses++] = c;
        made = realloc(made, sizeof(Class *) * (size_t)(nmade + 1));
        made[nmade++] = c;
    }
    e->cls = c;
    e->type = ty_object_of(c);
}

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
        for (int k = 0; k < i; k++) if (strcmp(e->keys[k], e->keys[i]) == 0) xs_error(XS9024, v->pos, NULL, "field '%s' is given twice", e->keys[i]);
        check_expr(v, v->kind == E_OBJLIT ? ty_json : NULL);
        settle(v);
        if (v->type->kind == TY_NULL) v->type = ty_unknown; /* (an unknown that holds null) */
        json_ok_unknown = true; /* (an unknown member is written by what it holds) */
        bool jok = json_ok(v->type, 0);
        json_ok_unknown = false;
        if (!jok) xs_error(XS8004, v->pos, NULL, "%s cannot be part of a JSON value", ty_name(v->type));
        c->fields[i] = (Field){.name = e->keys[i], .pos = v->pos, .type = v->type, .cname = field_cname(e->keys[i])};
    }
    c->nfields = c->cap_fields = e->args.len;
    prog->classes = realloc(prog->classes, sizeof(Class *) * (size_t)(prog->nclasses + 1));
    prog->cap_classes = prog->nclasses + 1; /* keep the parser's add_class in step */
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
    e->is_int_lit = r >= -9007199254740991LL && r <= 9007199254740991LL; /* 2 ** 53: exact, but a number, not an int (XB-075) */
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
/* xs_big_add(a, xs_big_mul(b, c)) -> xs_big_addmul(a, b, c), and the same for sub: one pass and no product
   object when a factor is small (acc += num * 2n in pidigits). The operands keep their order (a, b, c). */
static void big_fuse(Expr *e) {
    if (e->kind != E_CALL || !e->builtin.cfn || e->args.len != 2) return;
    bool add = strcmp(e->builtin.cfn, "xs_big_add") == 0, sub = strcmp(e->builtin.cfn, "xs_big_sub") == 0;
    Expr *m = e->args.items[1];
    if (!(add || sub) || m->kind != E_CALL || !m->builtin.cfn || strcmp(m->builtin.cfn, "xs_big_mul") != 0 || m->args.len != 2) return;
    e->args.items[1] = m->args.items[0];
    expr_list_push(&e->args, m->args.items[1]);
    e->builtin.cfn = add ? "xs_big_addmul" : "xs_big_submul";
}
/* a + b, a < b, a === b... with a bigint on either side: both must be bigints (as in JS, which throws a
   TypeError when they are mixed) */
static bool big_binary(Expr *e) {
    if (!is_big(e->lhs->type) && !is_big(e->rhs->type)) return false;
    if (!is_big(e->lhs->type) || !is_big(e->rhs->type))
        xs_error(XS8094, e->pos, "convert one side: BigInt(i) or Number(b)", "cannot mix bigint and %s", ty_name(is_big(e->lhs->type) ? e->rhs->type : e->lhs->type));
    const char *cmp = e->op == TK_LT ? "xs_big_lt" : e->op == TK_LE ? "xs_big_le" : e->op == TK_GT ? "xs_big_gt" : e->op == TK_GE ? "xs_big_ge"
                    : e->op == TK_EQ ? "xs_big_eq" : e->op == TK_NEQ ? "xs_big_ne" : NULL;
    if (cmp) { big_call(e, cmp, e->lhs, e->rhs, ty_boolean); return true; }
    const char *f = big_op(e->op);
    if (!f) xs_error(XS6031, e->pos, "supported on bigint: + - * / % and comparisons", "operator %s is not supported on bigint", tok_kind_name(e->op));
    big_call(e, f, e->lhs, e->rhs, ty_big());
    big_fuse(e);
    return true;
}

/* a literal or a name: checking a copy of it has no effect elsewhere */
static bool pure_leaf(Expr *x) {
    return x->kind == E_NUMBER || x->kind == E_STRING || x->kind == E_BOOL || x->kind == E_IDENT
        || (x->kind == E_MEMBER && !x->is_call && pure_leaf(x->lhs)) || x->kind == E_THIS;
}

/* new Set(values) / new Map(entries) / new Map() with no type arguments: the type the context declares
   (a field, variable, parameter or return type), else the one the values have */
static void infer_new_collection(Expr *e, Type *expected) {
    bool is_map = e->str && e->str[0] == 'M';
    Type *want = expected && expected->kind == TY_NULLABLE ? expected->elem : expected;
    if (want && want->kind == (is_map ? TY_MAP : TY_SET) && !want->record) { e->elem_type = want; return; }
    const char *hint = is_map ? "write the types: new Map<string, number>()" : "write the type: new Set<string>()";
    if (e->args.len != 1) xs_error(XS3006, e->pos, hint, "cannot infer the type of an empty %s here", is_map ? "Map" : "Set");
    Expr *a = e->args.items[0];
    if (is_map && a->kind == E_ARRAY_LIT && a->args.len && a->args.items[0]->kind == E_ARRAY_LIT && a->args.items[0]->args.len == 2) {
        /* new Map([["a", 1], ["b", 2]]): the first entry gives the key and value types */
        Expr *k = expr_copy(a->args.items[0]->args.items[0]), *v = expr_copy(a->args.items[0]->args.items[1]);
        if (!pure_leaf(k) || !pure_leaf(v)) xs_error(XS3006, a->pos, hint, "cannot infer the types of this Map's entries");
        check_expr(k, NULL); /* copies: the entries are checked again against the type found */
        check_expr(v, NULL);
        Type *kt = k->type->kind == TY_LITINT ? ty_int : k->type, *vt = v->type->kind == TY_LITINT ? ty_number : v->type;
        if (kt->kind == TY_NULL || vt->kind == TY_NULL) xs_error(XS3006, a->pos, hint, "cannot infer the type of null here");
        e->elem_type = ty_map_of(kt, vt);
        return;
    }
    if (is_map && a->kind == E_MEMBER && a->is_call && a->str && strcmp(a->str, "map") == 0 && a->args.len == 1 &&
        a->args.items[0]->kind == E_ARROW) { /* new Map(xs.map((x) => [k, v])): the pair a tuple */
        Stmt *b = a->args.items[0]->arrow->fn->body;
        if (b && b->kind == S_BLOCK && b->stmts.len == 1 && b->stmts.items[0]->kind == S_RETURN && b->stmts.items[0]->expr &&
            b->stmts.items[0]->expr->kind == E_ARRAY_LIT && b->stmts.items[0]->expr->args.len == 2)
            b->stmts.items[0]->expr->want_tuple = true;
    }
    bool ints = !is_map && a->kind == E_ARRAY_LIT && a->args.len; /* new Set([1, 2]): a Set of int, as xisto keeps numbers in one */
    for (int i = 0; ints && i < a->args.len; i++) ints = a->args.items[i]->kind == E_NUMBER && a->args.items[i]->is_int_lit;
    check_expr(a, ints ? ty_array_of(ty_int) : NULL);
    settle(a);
    Type *t = a->type;
    if (!is_map && t->kind == TY_ARRAY && t->elem->kind == TY_NUMBER) { /* new Set(ids) with ids: number[]: whole numbers as a Set<number>'s
                                                                             keys (checked: a fraction is a TypeError), as new Set<number>(ids) */
        wrap_call(a, "xs_num_keys", ty_array_of(ty_numkey()));
        t = a->type;
    }
    if (is_map && t->kind == TY_MAP && !t->record) { e->elem_type = ty_map_of(t->key, t->elem); return; } /* new Map(other): its types */
    if (t->kind != TY_ARRAY && t->kind != TY_SET) xs_error(XS3006, a->pos, hint, "cannot infer the type of this %s", is_map ? "Map" : "Set");
    if (!is_map) { e->elem_type = ty_set_of(t->elem); return; }
    Type *el = t->elem;
    if (el->kind != TY_OBJECT || !el->cls->is_tuple || el->cls->nfields != 2) xs_error(XS3006, a->pos, hint, "the entries of a Map are [key, value] pairs");
    e->elem_type = ty_map_of(el->cls->fields[0].type, el->cls->fields[1].type);
}

/* a copy of x that can be checked on its own (to learn its type) without touching x: literals, names, fields, and
   object and array literals of them; NULL for anything else (a call runs, so it is checked once, where it is) */
/* a copy of an unchecked expression for another branch that runs instead of it (x.m(args) on a union: one class's call
   or the other's): any form but a function written in place (its body is its own); NULL when it has one */
static Expr *branch_copy(Expr *x) {
    if (!x) return NULL;
    switch (x->kind) {
    case E_ARROW: case E_SPAWN: case E_FUNC_REF: return NULL;
    default: break;
    }
    if (x->prechecked || x->type) return NULL; /* (checked already: not copied) */
    Expr *c = xs_alloc(sizeof *c);
    *c = *x;
    if (x->lhs && !(c->lhs = branch_copy(x->lhs))) return NULL;
    if (x->rhs && !(c->rhs = branch_copy(x->rhs))) return NULL;
    c->args = (ExprList){0};
    for (int i = 0; i < x->args.len; i++) {
        Expr *a = branch_copy(x->args.items[i]);
        if (!a) return NULL;
        expr_list_push(&c->args, a);
    }
    if (x->keys) {
        c->keys = xs_alloc(sizeof(char *) * (size_t)(x->args.len + 1));
        memcpy(c->keys, x->keys, sizeof(char *) * (size_t)x->args.len);
    }
    return c;
}
static Expr *pure_copy(Expr *x) {
    if (!x) return NULL;
    Expr *c = xs_alloc(sizeof *c);
    *c = *x;
    switch (x->kind) {
    case E_NUMBER: case E_STRING: case E_BOOL: case E_NULL: case E_IDENT: case E_THIS: return c;
    case E_MEMBER:
        if (x->is_call) return NULL;
        return (c->lhs = pure_copy(x->lhs)) ? c : NULL;
    case E_UNARY:
        if (x->lhs && !(c->lhs = pure_copy(x->lhs))) return NULL;
        if (x->rhs && !(c->rhs = pure_copy(x->rhs))) return NULL;
        return c;
    case E_OBJLIT: case E_ARRAY_LIT: case E_TEMPLATE:
        c->args = (ExprList){0};
        for (int i = 0; i < x->args.len; i++) {
            Expr *a = pure_copy(x->args.items[i]);
            if (!a) return NULL;
            expr_list_push(&c->args, a);
        }
        return c;
    default: return NULL;
    }
}
static Type *pure_type(Expr *x) {
    Expr *c = pure_copy(x);
    if (!c) return NULL;
    check_expr(c, NULL);
    settle(c);
    return c->type;
}
static Expr *member_of(Expr *obj, const char *name, Pos pos) {
    Expr *m = xs_alloc(sizeof *m);
    m->kind = E_MEMBER; m->pos = pos; m->lhs = obj; m->str = (char *)name; m->field = -1;
    return m;
}

/* { ...base, x: 1 } and { [key]: value } (parsed as entries "..." and "[]"): spreads become the fields they copy,
   base.f each (where a type is expected, the ones it has), a later key replacing an earlier one in its place, as
   JavaScript does; computed keys make the object a Record<string, V> (built like one written { a: 1 }).
   True when e is checked already (a Record). */
static bool has_await(Expr *x) {
    if (!x) return false;
    if (x->kind == E_AWAIT || x->kind == E_ARROW) return x->kind == E_AWAIT;
    if (has_await(x->lhs) || has_await(x->rhs)) return true;
    for (int i = 0; i < x->args.len; i++) if (has_await(x->args.items[i])) return true;
    return false;
}
static int objlit_as_cast; /* > 0 while the object literal of an `as` cast is checked (--tsc-checked) */
static bool objlit_expand(Expr *e, Type *expected);
/* { ...f(), x: 1 }: the computed value once, as the parameter of an arrow called in place:
   ((s) => ({ ...s, x: 1 }))(f()), so the spread reads a name */
static bool objlit_hoist_spreads(Expr *e, Type *expected) {
    int n = 0;
    for (int i = 0; i < e->args.len; i++)
        if (strcmp(e->keys[i], "...") == 0 && !pure_copy(e->args.items[i]->lhs) &&
            !(e->args.items[i]->lhs->kind == E_COND && !e->args.items[i]->lhs->builtin.cfn)) n++;
    if (!n) return false;
    for (int i = 0; i < e->args.len; i++) { /* (an await cannot move into an arrow; a hoisted spread's value is the call's argument, outside) */
        Expr *a = e->args.items[i];
        bool hoisted = strcmp(e->keys[i], "...") == 0 && !pure_copy(a->lhs) && !(a->lhs->kind == E_COND && !a->lhs->builtin.cfn);
        if (!hoisted && has_await(a)) return false;
    }
    static int nsp;
    Expr *inner = xs_alloc(sizeof *inner);
    *inner = *e;
    inner->args = (ExprList){0};
    char **keys = xs_alloc(sizeof(char *) * (size_t)(e->args.len + 1));
    Func *f = NULL;
    Expr *arrow = NULL;
    ExprList callargs = {0};
    Param *ps = xs_alloc(sizeof(Param) * (size_t)n);
    int np = 0;
    for (int i = 0; i < e->args.len; i++) {
        Expr *a = e->args.items[i];
        keys[i] = e->keys[i];
        if (strcmp(e->keys[i], "...") == 0 && !pure_copy(a->lhs) && !(a->lhs->kind == E_COND && !a->lhs->builtin.cfn)) {
            Expr *src = a->lhs;
            check_expr(src, NULL);
            settle(src);
            if (src->type->kind == TY_LITINT) coerce_lit(src, ty_int);
            char *pn = xs_sprintf("__sp%d", ++nsp);
            memset(&ps[np], 0, sizeof ps[np]);
            ps[np].name = pn;
            ps[np].type = src->type;
            np++;
            expr_list_push(&callargs, src);
            Expr *id = xs_alloc(sizeof *id);
            id->kind = E_IDENT; id->pos = src->pos; id->str = pn; id->field = -1;
            Expr *sp = expr_copy(a);
            sp->lhs = id;
            expr_list_push(&inner->args, sp);
        } else expr_list_push(&inner->args, a);
    }
    inner->keys = keys;
    arrow = thunk_of(inner);
    f = arrow->arrow->fn;
    f->params = ps;
    f->nparams = np;
    Type *t = expected && expected->kind == TY_NULLABLE ? expected->elem : expected;
    if (t && t->kind == TY_OBJECT && !t->cls->builtin) f->ret = t;
    Pos at = e->pos;
    memset(e, 0, sizeof *e);
    e->kind = E_CALL; e->pos = at; e->field = -1; e->is_call = true;
    e->lhs = arrow;
    e->args = callargs;
    check_expr(arrow, NULL);
    e->type = arrow->type->ret;
    return true;
}
static bool objlit_expand(Expr *e, Type *expected) {
    for (int i = 0; e->keys && i < e->args.len; i++) { /* ...(c && { k: v }): JavaScript spreads nothing of false: ...(c ? { k: v } : {}) */
        Expr *a = e->args.items[i];
        if (strcmp(e->keys[i], "...") != 0 || !a) continue;
        Expr *x = a->kind == E_UNARY && a->op == TK_ELLIPSIS && a->lhs ? a->lhs : a;
        if (x->kind != E_BINARY || x->op != TK_AND || !x->rhs || x->rhs->kind != E_OBJLIT) continue;
        Expr *empty = xs_alloc(sizeof *empty);
        empty->kind = E_OBJLIT; empty->pos = x->rhs->pos; empty->field = -1;
        Expr *obj = x->rhs, *cond = x->lhs;
        memset(x, 0, sizeof *x);
        x->kind = E_COND; x->pos = cond->pos; x->field = -1; x->lhs = cond;
        expr_list_push(&x->args, obj);
        expr_list_push(&x->args, empty);
    }
    if (objlit_hoist_spreads(e, expected)) return true;
    Type *t = expected && expected->kind == TY_NULLABLE ? expected->elem : expected;
    bool computed = false;
    for (int i = 0; i < e->args.len; i++) if (strcmp(e->keys[i], "[]") == 0) computed = true;
    if (computed) {
        Type *rec = t && t->kind == TY_MAP && t->record ? t : NULL;
        const char *hint = "declare it as a Record: const m: Record<string, number> = { [key]: 1 };";
        if (!rec) {
            if (t && t->kind != TY_UNKNOWN && t->kind != TY_JSON) xs_error(XS3006, e->pos, hint, "an object with computed keys is a Record<string, V>, not %s", ty_name(t));
            Type *v = NULL;
            for (int i = 0; i < e->args.len; i++) {
                Expr *a = e->args.items[i];
                if (strcmp(e->keys[i], "...") == 0) xs_error(XS1018, a->pos, hint, "a spread and computed keys in one object literal are not supported");
                Expr *val = strcmp(e->keys[i], "[]") == 0 ? a->rhs : a;
                Type *vt = pure_type(val);
                if (t && (vt == NULL || (v && !ty_equal(v, vt->kind == TY_INT ? ty_number : vt)))) {
                    /* { [col]: dir ?? 'ASC' } or { 1: s, 2: n } where an object or unknown goes (FindOptions.order, binds):
                       a Record<string, unknown> */
                    v = ty_unknown;
                    break;
                }
                if (!vt) xs_error(XS3006, val->pos, hint, "cannot infer the type of this value without a declared type");
                if (vt->kind == TY_INT) vt = ty_number;
                if (vt->kind == TY_NULL) xs_error(XS3009, val->pos, hint, "cannot infer the type of null here");
                if (!v) v = vt;
                else if (!ty_equal(v, vt)) xs_error(XS3006, val->pos, hint, "the values are %s and %s: declare the type", ty_name(v), ty_name(vt));
            }
            rec = xs_alloc(sizeof *rec);
            *rec = *ty_map_of(ty_string, v);
            rec->record = true;
        }
        Expr *arr = xs_alloc(sizeof *arr);
        arr->kind = E_ARRAY_LIT; arr->pos = e->pos; arr->field = -1;
        for (int i = 0; i < e->args.len; i++) {
            Expr *a = e->args.items[i];
            if (strcmp(e->keys[i], "...") == 0) xs_error(XS1018, a->pos, hint, "a spread and computed keys in one object literal are not supported");
            Expr *pair = xs_alloc(sizeof *pair);
            pair->kind = E_ARRAY_LIT; pair->pos = a->pos; pair->field = -1;
            Expr *k, *val;
            if (strcmp(e->keys[i], "[]") == 0) {
                k = a->lhs; val = a->rhs;
                if (k->kind == E_NUMBER && k->is_int_lit && rec->key->kind == TY_STRING) { /* { 1: x } where Record<string, V> goes: the key "1" */
                    Expr *ks = xs_alloc(sizeof *ks);
                    ks->kind = E_STRING; ks->pos = k->pos; ks->field = -1; ks->str = xs_sprintf("%lld", k->ival);
                    k = ks;
                }
            }
            else {
                k = xs_alloc(sizeof *k);
                k->kind = E_STRING; k->pos = a->pos; k->field = -1; k->str = e->keys[i];
                val = a;
            }
            expr_list_push(&pair->args, k);
            expr_list_push(&pair->args, val);
            expr_list_push(&arr->args, pair);
        }
        e->kind = E_NEW_MAP;
        e->elem_type = rec;
        e->args = (ExprList){0};
        e->keys = NULL;
        e->spread = false;
        if (arr->args.len) expr_list_push(&e->args, arr);
        check_expr(e, rec);
        e->type = rec;
        return true;
    }
    { /* { ...rec, k: v } with rec a Record: a Record of rec's entries and then k (later keys win, as in JavaScript):
         new Map([...rec.entries(), ['k', v]]) read as an object */
        Type *v = t && t->kind == TY_MAP && t->record && t->key->kind == TY_STRING ? t->elem : NULL;
        bool any_rec = false, ok = true;
        for (int i = 0; i < e->args.len && ok; i++) {
            if (strcmp(e->keys[i], "...") != 0) continue;
            Type *st = pure_type(e->args.items[i]->lhs);
            if (st && st->kind == TY_NULLABLE) st = st->elem; /* (...opts.headers with headers?: a Record: nothing when absent) */
            if (st && st->kind == TY_MAP && st->record && st->key->kind == TY_STRING) {
                any_rec = true;
                if (!v) v = st->elem;
                else if (!ty_equal(v, st->elem)) ok = false;
            } else ok = false; /* (a Record and an object spread together: not this) */
        }
        if (any_rec && ok) {
            for (int i = 0; i < e->args.len; i++) if (strcmp(e->keys[i], "[]") == 0) ok = false;
        }
        if (any_rec && ok) {
            Type *rec = xs_alloc(sizeof *rec);
            *rec = *ty_map_of(ty_string, v);
            rec->record = true;
            Expr *arr = xs_alloc(sizeof *arr);
            arr->kind = E_ARRAY_LIT; arr->pos = e->pos; arr->field = -1; arr->spread = true;
            for (int i = 0; i < e->args.len; i++) {
                Expr *a = e->args.items[i];
                if (strcmp(e->keys[i], "...") == 0) {
                    Expr *srcx = a->lhs;
                    Type *pt = pure_type(srcx);
                    if (pt && pt->kind == TY_NULLABLE) { /* (x ?? {}): a null spreads nothing, as in JavaScript */
                        Expr *empty = xs_alloc(sizeof *empty);
                        empty->kind = E_OBJLIT; empty->pos = a->pos; empty->field = -1;
                        Expr *qq = xs_alloc(sizeof *qq);
                        qq->kind = E_BINARY; qq->op = TK_QQ; qq->pos = a->pos; qq->field = -1; qq->lhs = srcx; qq->rhs = empty;
                        srcx = qq;
                    }
                    Expr *ent = xs_alloc(sizeof *ent);
                    ent->kind = E_MEMBER; ent->pos = a->pos; ent->field = -1; ent->lhs = srcx; ent->str = "entries"; ent->is_call = true;
                    Expr *sp = xs_alloc(sizeof *sp);
                    sp->kind = E_UNARY; sp->op = TK_ELLIPSIS; sp->pos = a->pos; sp->field = -1; sp->lhs = ent;
                    expr_list_push(&arr->args, sp);
                    continue;
                }
                Expr *pair = xs_alloc(sizeof *pair);
                pair->kind = E_ARRAY_LIT; pair->pos = a->pos; pair->field = -1;
                Expr *k = xs_alloc(sizeof *k);
                k->kind = E_STRING; k->pos = a->pos; k->field = -1; k->str = e->keys[i];
                expr_list_push(&pair->args, k);
                expr_list_push(&pair->args, a);
                expr_list_push(&arr->args, pair);
            }
            e->kind = E_NEW_MAP;
            e->elem_type = rec;
            e->args = (ExprList){0};
            e->keys = NULL;
            e->spread = false;
            expr_list_push(&e->args, arr);
            check_expr(e, rec);
            e->type = rec;
            return true;
        }
    }
    Class *target = t && t->kind == TY_OBJECT && !t->cls->builtin ? t->cls : NULL;
    char **keys = xs_alloc(sizeof(char *) * 8);
    int cap = 8;
    ExprList vals = {0};
    #define SET_KEY(name_, val_) do { \
        int at_ = -1; \
        for (int q_ = 0; q_ < vals.len; q_++) if (strcmp(keys[q_], name_) == 0) at_ = q_; \
        if (at_ >= 0) vals.items[at_] = (val_); \
        else { if (vals.len == cap) { cap *= 2; keys = realloc(keys, sizeof(char *) * (size_t)cap); } keys[vals.len] = (char *)(name_); expr_list_push(&vals, (val_)); } \
    } while (0)
    for (int i = 0; i < e->args.len; i++) {
        Expr *a = e->args.items[i];
        if (strcmp(e->keys[i], "...") != 0) { SET_KEY(e->keys[i], a); continue; }
        Expr *src = a->lhs;
        if (src->kind == E_COND && !src->builtin.cfn && src->args.len == 2) {
            /* ...(c ? { k: v } : {}): the field k when c holds, null (absent) when not */
            Expr *ya = src->args.items[0], *na = src->args.items[1];
            bool flip = ya->kind == E_OBJLIT && ya->args.len == 0;
            Expr *full = flip ? na : ya, *empty = flip ? ya : na;
            if (full->kind == E_OBJLIT && empty->kind == E_OBJLIT && empty->args.len == 0 && !full->spread) {
                bool ok = true;
                for (int k = 0; k < full->args.len; k++) if (strcmp(full->keys[k], "...") == 0) ok = false;
                if (ok) {
                    for (int k = 0; k < full->args.len; k++) {
                        Expr *c = xs_alloc(sizeof *c);
                        c->kind = E_COND; c->pos = full->args.items[k]->pos; c->field = -1;
                        c->lhs = k == 0 ? src->lhs : expr_copy(src->lhs);
                        Expr *nul = xs_alloc(sizeof *nul); /* (undefined: the key is absent, as JSON.stringify leaves it out) */
                        nul->kind = E_IDENT; nul->pos = c->pos; nul->field = -1; nul->str = "undefined";
                        expr_list_push(&c->args, flip ? nul : full->args.items[k]);
                        expr_list_push(&c->args, flip ? full->args.items[k] : nul);
                        SET_KEY(full->keys[k], c);
                    }
                    continue;
                }
            }
        }
        Type *st = pure_type(src);
        if (!st) xs_error(XS1018, src->pos, "spread a variable: const base = load(); const o = { ...base, x: 1 };", "spread a name or a field here, not a computed value");
        bool nsrc = st->kind == TY_NULLABLE && st->elem->kind == TY_OBJECT && !st->elem->cls->builtin; /* ...x with x: T | null */
        if (nsrc) st = st->elem;
        if (st->kind != TY_OBJECT || st->cls->builtin)
            xs_error(XS8002, src->pos, NULL, "%s cannot be spread into an object literal", ty_name(st));
        Class *sc = st->cls;
        for (int k = 0; k < sc->nfields; k++) {
            const char *fname = sc->fields[k].name;
            if (target && !find_field(target, fname, NULL)) continue; /* (TypeScript allows extra fields from a spread) */
            Expr *m = member_of(pure_copy(src), fname, a->pos);
            if (nsrc) { /* (spreading null adds nothing: x === null ? null : x.f) */
                Expr *z = xs_alloc(sizeof *z);
                z->kind = E_NULL; z->pos = a->pos; z->field = -1;
                Expr *t = xs_alloc(sizeof *t);
                t->kind = E_BINARY; t->op = TK_EQ; t->pos = a->pos; t->field = -1; t->lhs = pure_copy(src); t->rhs = z;
                Expr *z2 = xs_alloc(sizeof *z2);
                z2->kind = E_NULL; z2->pos = a->pos; z2->field = -1;
                Expr *c = xs_alloc(sizeof *c);
                c->kind = E_COND; c->pos = a->pos; c->field = -1; c->lhs = t;
                expr_list_push(&c->args, z2);
                expr_list_push(&c->args, m);
                m = c;
            }
            if (nsrc || sc->fields[k].optional) { /* null, or an optional field left out: the earlier value of the key stays */
                Expr *prev = NULL;
                for (int q = 0; q < vals.len; q++) if (strcmp(keys[q], fname) == 0) prev = vals.items[q];
                if (prev) {
                    Expr *qq = xs_alloc(sizeof *qq);
                    qq->kind = E_BINARY; qq->op = TK_QQ; qq->pos = a->pos; qq->field = -1; qq->lhs = m; qq->rhs = prev;
                    m = qq;
                }
            }
            SET_KEY(fname, m);
        }
    }
    #undef SET_KEY
    e->keys = keys;
    e->args = vals;
    e->spread = false;
    return false;
}

static void check_expr_in(Expr *e, Type *expected);
static void check_expr(Expr *e, Type *expected) {
    if (!e->ctx) { check_expr_in(e, expected); return; }
    CheckCtx *c = e->ctx;
    e->ctx = NULL;
    Scope *s0 = scope; Module *m0 = cur_mod; Class *k0 = cur_class;
    const char *fn0 = xs_file_name, *src0 = xs_source;
    if (c->mod) enter_module(c->mod);
    scope = c->scope;
    cur_class = c->cls;
    check_expr_in(e, expected);
    scope = s0; cur_mod = m0; cur_class = k0;
    xs_file_name = fn0; xs_source = src0;
}
/* "x.a.b" of a name or a chain of member reads (?. or .), before checking; NULL for anything else */
static const char *member_path_text(Expr *e) {
    if (!e) return NULL;
    if (e->kind == E_IDENT) return e->str;
    if (e->kind == E_COND && e->builtin.cfn && !strcmp(e->builtin.cfn, "optchain") && e->str && e->args.len >= 1) {
        /* a?.b: the rest is written on the temporary that holds a */
        const char *base = member_path_text(e->lhs), *rest = member_path_text(e->args.items[0]);
        size_t tl = strlen(e->str);
        if (!base || !rest || strncmp(rest, e->str, tl) != 0) return NULL;
        return xs_sprintf("%s%s", base, rest + tl);
    }
    if (e->kind == E_MEMBER && !e->is_call && e->str) {
        const char *l = member_path_text(e->lhs);
        return l ? xs_sprintf("%s.%s", l, e->str) : NULL;
    }
    return NULL;
}
/* juntas(() => a(), () => b()) with juntas<A, B>(a: () => A | Promise<A>, b: () => B | Promise<B>): [Promise<A>, Promise<B>]
   declared above its implementation juntas(...fns: Array<() => unknown>): Array<Promise<unknown>>: the call runs the
   implementation (its arguments checked against it) and is typed by the overload with as many parameters, its type
   parameters bound from the callbacks' results; the result converted, ((__j) => [__j[0], __j[1]])(call) */
static bool overload_typed(Expr *e, Func *f, ExprList *written) {
    Func *o = NULL;
    for (int i = 0; i < f->noverloads; i++) if (f->overloads[i]->nparams == written->len) o = f->overloads[i];
    if (!o || !o->ret) return false;
    Type **bind = o->ntparams ? xs_alloc(sizeof(Type *) * (size_t)o->ntparams) : NULL;
    for (int k = 0; k < o->nparams && k < written->len; k++) {
        Type *pt = o->params[k].type;
        Expr *a = written->items[k];
        if (!pt || pt->kind != TY_FUNC || !pt->ret || a->kind != E_ARROW) continue;
        Type *got = a->arrow->fn->natural_ret;
        if (!got) continue;
        Type *pr = pt->ret; /* A, Promise<A>, or A | Promise<A> */
        const char *tn = NULL;
        if (pr->kind == TY_TPARAM || (pr->kind == TY_OBJECT && pr->cls && !pr->ntargs)) tn = pr->kind == TY_TPARAM ? pr->tname : pr->cls->name;
        else if (pr->kind == TY_PROMISE && pr->elem) tn = pr->elem->kind == TY_TPARAM ? pr->elem->tname : pr->elem->kind == TY_OBJECT ? pr->elem->cls->name : NULL;
        else if (pr->kind == TY_UNION)
            for (int q = 0; q < pr->nparams && !tn; q++) {
                Type *m = pr->params[q];
                if (m->kind == TY_TPARAM) tn = m->tname;
                else if (m->kind == TY_OBJECT && m->cls && !m->ntargs) tn = m->cls->name;
            }
        if (!tn) continue;
        if (got->kind == TY_PROMISE) got = got->elem;
        for (int q = 0; q < o->ntparams; q++) if (!bind[q] && strcmp(o->tparams[q], tn) == 0) bind[q] = got;
    }
    for (int q = 0; q < o->ntparams; q++) if (!bind[q]) return false;
    Module *was = cur_mod;
    Scope *ss = scope;
    const char *sfn = xs_file_name, *ssrc = xs_source;
    enter_module(o->mod ? o->mod : f->mod);
    Type *rt = resolve(o->ntparams ? subst_ftp(o->ret, o->tparams, bind, o->ntparams) : o->ret, e->pos);
    cur_mod = was; scope = ss; xs_file_name = sfn; xs_source = ssrc;
    if (ty_equal(rt, e->type)) return true;
    Class *tc = rt->kind == TY_OBJECT && rt->cls ? rt->cls : NULL;
    if (!tc || !tc->is_tuple || e->type->kind != TY_ARRAY) return false; /* (a tuple from an array: the one form here) */
    Pos pos = e->pos;
    Func *fn = xs_alloc(sizeof *fn);
    fn->pos = pos; fn->name = "arrow";
    fn->params = xs_alloc(sizeof(Param));
    memset(fn->params, 0, sizeof(Param));
    fn->params[0].name = "__j";
    fn->params[0].type = e->type;
    fn->nparams = 1;
    fn->ret = rt;
    Expr *lit = xs_alloc(sizeof *lit);
    lit->kind = E_ARRAY_LIT; lit->pos = pos; lit->field = -1;
    for (int i = 0; i < tc->nfields; i++) {
        Expr *id = xs_alloc(sizeof *id);
        id->kind = E_IDENT; id->pos = pos; id->field = -1; id->str = "__j";
        Expr *ix = xs_alloc(sizeof *ix);
        ix->kind = E_INDEX; ix->pos = pos; ix->field = -1; ix->lhs = id; ix->rhs = num_lit(pos, i, true);
        expr_list_push(&lit->args, ix);
    }
    Stmt *b = xs_alloc(sizeof *b);
    b->kind = S_BLOCK; b->pos = pos;
    Stmt *r = xs_alloc(sizeof *r);
    r->kind = S_RETURN; r->pos = pos; r->expr = lit;
    stmt_list_push(&b->stmts, r);
    fn->body = b;
    Arrow *ar = xs_alloc(sizeof *ar);
    ar->pos = pos; ar->fn = fn; ar->expr_body = true;
    Expr *are = xs_alloc(sizeof *are);
    are->kind = E_ARROW; are->pos = pos; are->field = -1; are->arrow = ar;
    Expr *inner = xs_alloc(sizeof *inner);
    *inner = *e;
    inner->prechecked = true;
    memset(e, 0, sizeof *e);
    e->kind = E_CALL; e->pos = pos; e->field = -1; e->lhs = are;
    expr_list_push(&e->args, inner);
    check_expr(e, NULL);
    return true;
}
static bool cond_prim(Type *t) { return t->kind == TY_STRING || t->kind == TY_NUMBER || t->kind == TY_INT || t->kind == TY_BOOLEAN; }
static void check_expr_in(Expr *e, Type *expected) {
    if (e->prechecked) return;
    if (e->kind == E_CALL && !e->lhs && e->builtin.cfn && e->type) return; /* a runtime call the checker made (wrap_call): checked */
    switch (e->kind) {
    case E_SPAWN: return; /* (made by the checker, already checked) */
    case E_UPCAST: case E_SUPER_CALL: return; /* (made by the checker with their types: a walk that checks again leaves them) */
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
        if (!cur_class) xs_error(XS8069, e->pos, NULL, "'this' outside of a class");
        e->type = ty_object_of(cur_class);
        e->cname = (char *)this_cname();
        return;
    case E_AWAIT: {
        Expr *was = pr_awaited;
        pr_awaited = e->lhs;
        check_expr(e->lhs, expected);
        if (thenable(e->lhs->type)) { /* await q of a thenable (mongoose's Query): await q.then() */
            then_call(e->lhs);
            pr_awaited = e->lhs;
            check_expr(e->lhs, expected);
        }
        pr_awaited = was;
        if (e->lhs->type->kind == TY_PROMISE) pr_await(e->lhs); /* a promise kept: its value */
        else if (e->lhs->type->kind == TY_NULLABLE && e->lhs->type->elem->kind == TY_PROMISE) { /* await of a Promise<T> | null: null gives null */
            Type *t = e->lhs->type->elem->elem;
            int k = pr_kind(t);
            if (k != 3 && k != 4) xs_error(XS2006, e->pos, "check it first: if (p !== null) await p;", "awaiting a %s that may be null", ty_name(e->lhs->type));
            wrap_call(e->lhs, k == 4 ? "xs_promise_await_v" : "xs_promise_await_p", k == 4 ? ty_void : ty_nullable_of(ty_strip_null(t)));
        }
        e->type = e->lhs->type;
        { /* await queryOne<Row>(sql): what the await gives, as a Row */
            Expr *c = e->lhs->kind == E_SPAWN ? e->lhs->lhs : e->lhs;
            if (c && c->gen_ret) { e->gen_ret = c->gen_ret; c->gen_ret = NULL; gen_convert(e); }
        }
        return;
    }
    case E_IDENT: {
        Binding *b = lookup(e->str);
        if (!b) {
            Func *f = lookup_func(e->str);
            if (f && f->ntparams) xs_error(XS8136, e->pos, xs_sprintf("wrap it: (x: Type) => %s(x)", f->name), "the generic function '%s' is not a value", f->name);
            if (f) { /* a function used as a value */
                Type **ps = xs_alloc(sizeof(Type *) * (size_t)(f->nparams ? f->nparams : 1));
                for (int i = 0; i < f->nparams; i++) ps[i] = f->params[i].type;
                e->kind = E_FUNC_REF;
                e->func = f;
                e->cname = f->cname;
                e->type = ty_func(ps, f->nparams, fret(f, e->pos));
                e->type->rest = f->nparams && f->params[f->nparams - 1].rest;
                e->type->async_ret = f->ret_promise || f->is_async;
                return;
            }
            Class *cv = find_class(e->str);
            if (cv && !cv->builtin) { /* a decorated class as a value: its ClassInfo (xisto:reflect) */
                char *ci = xs_sprintf("__ci_%s", cv->name);
                for (Binding *mb = mod_scopes[cv->mod->id]->head; mb; mb = mb->next)
                    if (strcmp(mb->name, ci) == 0) { e->type = mb->type; e->cname = mb->cname; return; }
                /* declared further down (() => Child in a decorator above Child, as a TypeORM relation): the global
                   that will hold it, read when the arrow runs */
                for (int i = 0; i < prog->top.len; i++) {
                    Stmt *st = prog->top.items[i];
                    if (st->kind != S_VAR || st->mod != cv->mod || !st->name || strcmp(st->name, ci) != 0) continue;
                    Class *cic = NULL;
                    SYM_EACH(classes_by_name, "ClassInfo", ce) { Class *k = ce->decl; if (k->mod && k->mod->file && !strcmp(k->mod->file, "xisto:reflect")) cic = k; }
                    if (!cic) break;
                    e->type = ty_object_of(cic);
                    e->cname = xs_sprintf("g%d_%s", cv->mod->id, ci);
                    return;
                }
                xs_error(XS9901, e->pos, "decorate the class (@Injectable(), @Controller(), ...) so the compiler describes it", "class '%s' is used as a value, but it has no ClassInfo", e->str);
            }
            if (builtin_ctor_value(e, expected)) return;
            {
                Enum *en = find_enum(e->str);
                if (en) { /* an enum as a value (@IsEnum(Status), enum: Status in a Swagger option): its values, as strings */
                    Pos at = e->pos;
                    memset(e, 0, sizeof *e);
                    e->kind = E_ARRAY_LIT; e->pos = at; e->field = -1;
                    for (int k = 0; k < en->nmembers; k++) {
                        EnumMember *mb = &en->members[k];
                        Expr *v = xs_alloc(sizeof *v);
                        v->kind = E_STRING; v->pos = at; v->field = -1;
                        v->str = mb->is_string ? mb->sval : xs_sprintf("%lld", mb->ival);
                        expr_list_push(&e->args, v);
                    }
                    static Type *arr_str;
                    if (!arr_str) arr_str = ty_array_of(ty_string);
                    check_expr(e, arr_str);
                    if (expected && expected->kind == TY_UNKNOWN) expect_type(e, expected, "the enum's values");
                    return;
                }
            }
            if (is_builtin_name(e->str)) xs_error(XS8053, e->pos, NULL, "'%s' is built in and not a value", e->str);
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
            if (strcmp(e->str, "undefined") == 0) { /* null is xisto's only empty value (MANIFESTO: null, não undefined): undefined reads as null */
                xs_warn_rule(XS2101, e->pos, "write null: xisto has one empty value", "'undefined' is deprecated in xisto: it is read as null"); /* XB-118 rule 2 */
                e->kind = E_NULL;
                e->str = NULL;
                check_expr(e, expected);
                return;
            }
            if (strcmp(e->str, "null") == 0)
                xs_error(XS2005, e->pos, "every value in xisto has a concrete type; there is no 'undefined' or 'null'", "'%s' does not exist", e->str);
            {
                int n; const char **cands = visible_names(&n);
                if (missing_name(e->str)) xs_error_quiet();
                xs_error(XS9034, e->pos, xs_help_suggest(e->str, cands, n, "declare it first: let name: type = value;"), "unknown variable '%s'", e->str);
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
            if (t->kind == TY_UNKNOWN || t->kind == TY_UNION ||
                (t->kind == TY_NULLABLE && (t->elem->kind == TY_UNION || t->elem->kind == TY_UNKNOWN))) { /* (x?: string | number too) */
                e->builtin.cfn = "xs_typeof";
                e->type = ty_string;
                return;
            }
            Type *b = t->kind == TY_NULLABLE ? t->elem : t;
            const char *v = is_big(b) ? "bigint" : b->kind == TY_OBJECT && b->cls->builtin && strcmp(b->cls->name, "symbol") == 0 ? "symbol" : b->kind == TY_STRING || (b->kind == TY_ENUM && b->en->is_string) ? "string"
                          : is_numeric(b) || b->kind == TY_ENUM ? "number" : b->kind == TY_BOOLEAN ? "boolean" : b->kind == TY_FUNC ? "function"
                          : b->kind == TY_OBJECT && b->cls->mod && b->cls->mod->file && !strcmp(b->cls->mod->file, "xisto:reflect") && !strcmp(b->cls->name, "ClassInfo")
                          ? "function" /* (a class as a value: JavaScript's class is a function) */ : "object";
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
            if (!is_numeric(e->lhs->type)) xs_error(XS8118, e->lhs->pos, NULL, "operand of '~' must be int or number, but this is %s", ty_name(e->lhs->type));
            e->type = ty_int;
            return;
        }
        check_expr(e->lhs, expected);
        if (e->op == TK_NOT) {
            if (e->lhs->type->kind != TY_BOOLEAN && cond_truthy_type(e->lhs->type)) make_truthy(e->lhs, "operand of '!'");
            expect_type(e->lhs, ty_boolean, "operand of '!'");
            e->type = ty_boolean;
        } else if (is_big(e->lhs->type)) { /* -b */
            if (e->op != TK_MINUS) xs_error(XS6034, e->pos, NULL, "unary '+' is not allowed on bigint (as in JS)");
            big_call(e, "xs_big_neg", e->lhs, NULL, ty_big());
        } else if (e->op == TK_PLUS && e->lhs->type->kind == TY_STRING) { /* +s: Number(s), as in JS (+x.toFixed(2)) */
            Expr *v = e->lhs;
            e->kind = E_CALL; e->op = 0; e->lhs = NULL; e->args = (ExprList){0}; e->field = -1;
            e->builtin.cfn = "xs_str_to_number";
            expr_list_push(&e->args, v);
            Expr *callee = xs_alloc(sizeof *callee);
            callee->kind = E_IDENT; callee->pos = v->pos; callee->str = "Number"; callee->field = -1;
            e->lhs = callee;
            e->type = ty_number;
        } else {
            if (!is_numeric(e->lhs->type) && e->lhs->type->kind != TY_LITINT)
                xs_error(XS8119, e->lhs->pos, NULL, "operand of unary '%s' must be int or number, but this is %s", e->op == TK_PLUS ? "+" : "-", ty_name(e->lhs->type));
            e->type = e->lhs->type;
        }
        return;
    case E_BINARY:
        if (e->op == TK_QQ) { check_nullish(e, expected); return; }
        if (e->op == TK_IN) { /* "k" in m: m.has(k) on a Record; on an object, whether the field is there */
            check_expr(e->rhs, NULL);
            Type *rt = e->rhs->type;
            if (ty_is_any(rt)) { /* k in x: x's class has the field, filled (or a Record has the key), at run time */
                check_expr(e->lhs, ty_string);
                expect_type(e->lhs, ty_string, "key of 'in'");
                dyn_call(e, "xs_dyn_has", ty_boolean, e->rhs, e->lhs, NULL);
                return;
            }
            if (rt->kind == TY_MAP && rt->record) {
                Expr *key = e->lhs;
                e->kind = E_MEMBER;
                e->lhs = e->rhs;
                e->rhs = NULL;
                e->str = "has";
                e->is_call = true;
                e->args = (ExprList){0};
                expr_list_push(&e->args, key);
                check_expr(e, expected);
                return;
            }
            Type *ot = ty_strip_null(rt);
            if (ot->kind == TY_UNION && e->lhs->kind == E_STRING) { /* 'erro' in r with r: A | B: which member it holds, at run time
                                                                    (the union is held as an unknown is); if/else narrow it */
                bool objs = true;
                for (int i = 0; i < ot->nparams; i++) if (ot->params[i]->kind != TY_OBJECT) objs = false;
                if (objs) {
                    Expr *key = e->lhs, *v = e->rhs;
                    check_expr(key, ty_string);
                    v->type = ty_unknown;
                    dyn_call(e, "xs_union_has", ty_boolean, v, key, NULL);
                    return;
                }
            }
            if (ot->kind == TY_OBJECT && !ot->cls->builtin && e->lhs->kind == E_STRING) {
                Field *fd = find_field(ot->cls, e->lhs->str, NULL);
                if (fd && fd->optional) { /* "f" in o: o.f is there (an optional field left out is not) */
                    Expr *mem = xs_alloc(sizeof *mem);
                    mem->kind = E_MEMBER; mem->pos = e->pos; mem->field = -1; mem->lhs = e->rhs; mem->str = e->lhs->str;
                    Expr *nul = xs_alloc(sizeof *nul);
                    nul->kind = E_NULL; nul->pos = e->pos; nul->field = -1;
                    e->op = TK_NEQ;
                    e->lhs = mem;
                    e->rhs = nul;
                    check_expr(e, expected);
                    return;
                }
                bool has = fd != NULL || find_method(ot->cls, e->lhs->str) != NULL;
                e->kind = E_BOOL;
                e->boolean = has;
                e->lhs = e->rhs = NULL;
                e->type = ty_boolean;
                return;
            }
            if ((ot->kind == TY_OBJECT && (!ot->cls->builtin || is_error_class(ot->cls) || strcmp(ot->cls->name, "Error") == 0)) || ot->kind == TY_UNKNOWN) {
                /* k in o with k a string known at run time, or o an unknown (an object, tested by typeof): its class's field
                   table (or a Record's keys) says, as JavaScript looks the key up */
                Expr *key = e->lhs, *v = e->rhs;
                check_expr(key, ty_string);
                expect_type(key, ty_string, "the key of 'in'");
                if (v->type->kind != TY_UNKNOWN) to_unknown(v);
                xs_any_used = true;
                dyn_call(e, "xs_dyn_has", ty_boolean, v, key, NULL);
                return;
            }
            xs_error(XS8015, e->pos, "use it on a Record<string, V> or an object with a string literal key: \"name\" in obj", "'in' needs a Record or an object and a key written in place");
        }
        if ((e->op == TK_STARSTAR || tk_bitop(e->op)) && fold_lit_int(e)) { /* 2 ** 53, 1 << 4: a literal, typed by its use */
            check_expr(e, expected);
            return;
        }
        if (e->op == TK_STARSTAR) { /* int ** int: int (checked); with a number: number */
            check_expr(e->lhs, NULL);
            check_expr(e->rhs, NULL);
            if (is_big(e->lhs->type) || is_big(e->rhs->type)) {
                if (!is_big(e->lhs->type) || !is_big(e->rhs->type)) xs_error(XS8095, e->pos, "convert one side: BigInt(i) or Number(b)", "cannot mix bigint and another type in '**'");
                big_call(e, "xs_big_pow", e->lhs, e->rhs, ty_big());
                return;
            }
            bool ints = true;
            for (int k = 0; k < 2; k++) {
                Expr *o = k ? e->rhs : e->lhs;
                if (o->type->kind == TY_LITINT) coerce_lit(o, ty_int);
                if (!is_numeric(o->type)) xs_error(XS8116, o->pos, NULL, "operand of '**' must be int or number, but this is %s", ty_name(o->type));
                if (o->type->kind != TY_INT) ints = false;
            }
            if (e->rhs->kind == E_UNARY && e->rhs->op == TK_MINUS) ints = false; /* 10 ** -2: a fraction */
            { long long la, lb; /* 2 ** 60: two literals whose power leaves the safe range (fold_lit_int left it): a number, as in JS */
              if (ints && lit_int_of(e->lhs, &la) && lit_int_of(e->rhs, &lb)) ints = false; }
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
                if (!is_numeric(o->type)) xs_error(XS8115, o->pos, NULL, "operand of %s must be int or number, but this is %s", tok_kind_name(e->op), ty_name(o->type));
            }
            e->type = ty_int;
            return;
        }
        check_expr(e->lhs, NULL);
        if (e->op == TK_AND) {
            bool sv;
            if (static_bool(e->lhs, &sv) && !sv) { /* typeof n === "object" && ...: false, the right side never runs */
                Pos at = e->pos;
                memset(e, 0, sizeof *e);
                e->kind = E_BOOL; e->boolean = false; e->type = ty_boolean; e->field = -1; e->pos = at;
                return;
            }
        }
        if (e->op == TK_OR && e->lhs->type->kind != TY_BOOLEAN && !is_numeric(e->lhs->type) && e->lhs->type->kind != TY_STRING &&
            truthy_type(e->lhs->type) && rereadable(e->lhs) && boolish(e->rhs))
            make_truthy(e->lhs, "left operand of '||'"); /* err || !Array.isArray(v): a test (err !== null), not a default */
        if (e->op == TK_AND) { /* x !== null && x.f : the right side sees x narrowed */
            if (e->lhs->type->kind != TY_BOOLEAN && truthy_type(e->lhs->type) && rereadable(e->lhs))
                make_truthy(e->lhs, "left operand of '&&'"); /* raw && raw.f: becomes raw !== null && ..., which narrows */
            NSet ts = {0};
            narrow_set(e->lhs, true, &ts);
            UNarrow un = union_narrow(e->lhs);
            if (un.b) {
                push_scope();
                shadow_union(un.b, un.then_t);
                shadow_set(&ts);
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
        } else if (e->op == TK_OR && !is_numeric(e->lhs->type) && !(e->lhs->type->kind != TY_BOOLEAN && truthy_type(e->lhs->type))) {
            /* x === null || x.f : the right side runs when the left is false, so x is not null there */
            NSet fs = {0};
            narrow_set(e->lhs, false, &fs);
            UNarrow un = union_narrow(e->lhs); /* !(e instanceof X) || e.code: X on the right */
            push_scope();
            if (un.b && un.else_t) shadow_union(un.b, un.else_t);
            shadow_set(&fs);
            check_expr(e->rhs, NULL);
            pop_scope();
        } else if (e->op == TK_IDENT) { /* instanceof: the right side is a class name, checked below */
        } else if (e->op == TK_OR && is_numeric(e->lhs->type)) { /* parseInt(s, 10) || 3001: the number unless 0 or NaN */
            Type *nt = e->lhs->type;
            check_expr(e->rhs, nt);
            if (e->rhs->type->kind == TY_LITINT) coerce_lit(e->rhs, nt);
            expect_type(e->rhs, nt, "default after '||'");
            if (!(e->rhs->kind == E_NUMBER || rereadable(e->rhs))) { /* a - b || c.localeCompare(d): a once, the default only when it is 0 or NaN */
                Expr *dflt = e->rhs;
                e->kind = E_COND;
                e->op = 0;
                e->rhs = NULL;
                e->args = (ExprList){0};
                expr_list_push(&e->args, dflt);
                e->builtin.cfn = "or_once";
                e->type = nt;
                return;
            }
            Expr *a = e->lhs, *b = e->rhs;
            e->kind = E_CALL; e->op = 0; e->lhs = NULL; e->rhs = NULL; e->args = (ExprList){0};
            e->builtin.cfn = nt->kind == TY_INT ? "xs_int_or" : "xs_num_or";
            expr_list_push(&e->args, a);
            expr_list_push(&e->args, b);
            e->type = nt;
            return;
        } else if (e->op == TK_OR && truthy_type(e->lhs->type)) { /* value || default */
            Type *want = e->lhs->type->kind == TY_NULLABLE ? e->lhs->type->elem : e->lhs->type;
            check_expr(e->rhs, want);
            if ((e->rhs->type->kind == TY_NULL || (e->rhs->type->kind == TY_NULLABLE && ty_equal(e->rhs->type->elem, want))) && nullable_ok(want)) {
                /* a || b || 'x' (b may be null too): a when truthy, else b, which may be null */
                Type *nw = ty_nullable_of(want);
                expect_type(e->rhs, nw, "default after '||'");
                want = nw;
            } else
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
            val->type = want->kind == TY_NULLABLE ? want->elem : want; /* read only when the test passed */
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
            if (!c || c->is_interface) xs_error(XS9010, e->rhs->pos, "instanceof tests classes (interfaces have no run-time identity)", "'%s' is not a class", e->rhs->str);
            if (!ty_is_heap(e->lhs->type)) xs_error(XS8108, e->lhs->pos, NULL, "instanceof needs an object, but this is %s", ty_name(e->lhs->type));
            e->rhs->cls = c;
            e->rhs->type = ty_object_of(c);
            e->type = ty_boolean;
            return;
        }
        if (ty_is_any(e->lhs->type) || ty_is_any(e->rhs->type)) { /* an any on one side: JavaScript's rules, at run time */
            Expr *l = e->lhs, *r = e->rhs;
            switch (e->op) {
            case TK_PLUS:
                if (l->type->kind == TY_STRING || r->type->kind == TY_STRING) { /* s + x: String(x) */
                    if (ty_is_any(l->type)) dyn_wrap(l, "xs_dyn_str", ty_string);
                    if (ty_is_any(r->type)) dyn_wrap(r, "xs_dyn_str", ty_string);
                    break;
                }
                if (l->type->kind == TY_LITINT) coerce_lit(l, ty_number);
                if (r->type->kind == TY_LITINT) coerce_lit(r, ty_number);
                to_any(l);
                to_any(r);
                dyn_call(e, "xs_dyn_add", ty_any, l, r, NULL);
                return;
            case TK_MINUS: case TK_STAR: case TK_SLASH: case TK_PERCENT: case TK_LT: case TK_LE: case TK_GT: case TK_GE:
                if (ty_is_any(l->type)) dyn_wrap(l, "xs_dyn_num", ty_number);
                if (ty_is_any(r->type)) dyn_wrap(r, "xs_dyn_num", ty_number);
                break;
            case TK_EQ: case TK_NEQ:
                if (l->type->kind == TY_NULL || r->type->kind == TY_NULL) break; /* x === null: a null reference */
                if (l->type->kind == TY_LITINT) coerce_lit(l, ty_number);
                if (r->type->kind == TY_LITINT) coerce_lit(r, ty_number);
                to_any(l);
                to_any(r);
                { bool neq = e->op == TK_NEQ;
                  dyn_call(e, "xs_dyn_eq", ty_boolean, l, r, NULL);
                  if (neq) { Expr *v = expr_copy(e); memset(e, 0, sizeof *e); e->kind = E_UNARY; e->op = TK_NOT; e->pos = v->pos; e->field = -1; e->lhs = v; e->type = ty_boolean; } }
                return;
            default: break;
            }
        }
        if (big_binary(e)) return;
        switch (e->op) {
        case TK_PLUS:
            if (e->lhs->type->kind == TY_STRING || e->rhs->type->kind == TY_STRING) {
                if (!concat_operand(e->lhs)) expect_type(e->lhs, ty_string, "left operand of string concatenation");
                if (!concat_operand(e->rhs)) expect_type(e->rhs, ty_string, "right operand of string concatenation");
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
            { /* d1 < d2 with Dates: their times, as JavaScript compares their valueOf() */
                Type *dl = e->lhs->type, *dr = e->rhs->type;
                bool ld = dl->kind == TY_OBJECT && dl->cls->builtin && strcmp(dl->cls->name, "Date") == 0;
                bool rd = dr->kind == TY_OBJECT && dr->cls->builtin && strcmp(dr->cls->name, "Date") == 0;
                if (ld) wrap_call(e->lhs, "xs_Date_getTime", ty_number);
                if (rd) wrap_call(e->rhs, "xs_Date_getTime", ty_number);
            }
            {
                Type *sl = e->lhs->type, *sr = e->rhs->type;
                bool ls = sl->kind == TY_STRING || (sl->kind == TY_ENUM && sl->en->is_string);
                bool rs = sr->kind == TY_STRING || (sr->kind == TY_ENUM && sr->en->is_string);
                if (ls && rs) { /* 'a' < 'b': strings compare by their characters, as in JavaScript */
                    Expr *a = e->lhs, *b = e->rhs;
                    Expr *c = xs_alloc(sizeof *c);
                    c->kind = E_CALL; c->pos = e->pos; c->field = -1; c->type = ty_int;
                    c->builtin.cfn = "xs_str_order";
                    expr_list_push(&c->args, a);
                    expr_list_push(&c->args, b);
                    Expr *z = xs_alloc(sizeof *z);
                    z->kind = E_NUMBER; z->pos = e->pos; z->field = -1; z->type = ty_int; z->is_int_lit = true; z->ival = 0; z->num = 0;
                    e->lhs = c; e->rhs = z;
                    e->type = ty_boolean;
                    return;
                }
            }
            for (int side = 0; side < 2 && tsc_checked(); side++) { /* n < 1 with n: number | null (--tsc-checked): as JavaScript compares,
                                                                   null as 0 and an absent value (xs?.length) as NaN (false) */
                Expr *x = side ? e->rhs : e->lhs;
                if (x->type->kind != TY_NULLABLE || !is_numeric(x->type->elem)) continue;
                xs_warn_rule(XS2107, x->pos, "check first: if (x !== null) { ... }",
                             "%s may be null in a comparison: null compares as 0%s", ty_name(x->type), x->type->absent ? ", an absent value as NaN" : "");
                Expr *inner = expr_copy(x);
                Expr *v = xs_alloc(sizeof *v), *v2 = xs_alloc(sizeof *v2), *z = xs_alloc(sizeof *z), *dv = xs_alloc(sizeof *dv);
                v->kind = E_IDENT; v->pos = x->pos; v->field = -1; v->str = "__cmp";
                *v2 = *v;
                z->kind = E_NULL; z->pos = x->pos; z->field = -1;
                if (x->type->absent) { dv->kind = E_IDENT; dv->pos = x->pos; dv->field = -1; dv->str = "NaN"; }
                else { dv->kind = E_NUMBER; dv->pos = x->pos; dv->field = -1; dv->num = 0; }
                Expr *t0 = xs_alloc(sizeof *t0);
                t0->kind = E_BINARY; t0->op = TK_EQ; t0->pos = x->pos; t0->field = -1; t0->lhs = v; t0->rhs = z;
                Expr *cnum = xs_alloc(sizeof *cnum); /* Number(__cmp) in the branch where it is not null */
                Expr *callee = xs_alloc(sizeof *callee);
                callee->kind = E_IDENT; callee->pos = x->pos; callee->field = -1; callee->str = "Number";
                cnum->kind = E_CALL; cnum->pos = x->pos; cnum->field = -1; cnum->lhs = callee;
                expr_list_push(&cnum->args, v2);
                Expr *c = xs_alloc(sizeof *c);
                c->kind = E_COND; c->pos = x->pos; c->field = -1; c->lhs = t0;
                expr_list_push(&c->args, dv);
                expr_list_push(&c->args, cnum);
                Expr *arrow = thunk_of(c);
                Func *f = arrow->arrow->fn;
                f->params = xs_alloc(sizeof(Param));
                memset(f->params, 0, sizeof(Param));
                f->params[0].name = "__cmp"; f->params[0].type = x->type; f->nparams = 1;
                f->ret = ty_number;
                memset(x, 0, sizeof *x);
                x->kind = E_CALL; x->pos = inner->pos; x->field = -1; x->is_call = true; x->lhs = arrow;
                expr_list_push(&x->args, inner);
                check_expr(arrow, NULL);
                x->type = ty_number;
            }
            Type *t = unify_numeric(e, "comparison");
            if (t->kind == TY_LITINT) { coerce_lit(e->lhs, ty_number); coerce_lit(e->rhs, ty_number); }
            e->type = ty_boolean;
            return;
        }
        case TK_EQ: case TK_NEQ: {
            if (e->loose) check_loose_eq(e); /* == / !=: the same as === / !== when nothing would be converted */
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
                if (!related) xs_error(XS8089, e->pos, NULL, "cannot compare %s with %s", ty_name(e->lhs->type), ty_name(e->rhs->type));
                e->type = ty_boolean;
                e->builtin.cfn = "ref_eq";
                return;
            }
        }
            if (e->lhs->type->kind == TY_NULL || e->rhs->type->kind == TY_NULL) {
                Expr *other = e->lhs->type->kind == TY_NULL ? e->rhs : e->lhs;
                if (other->type->kind != TY_NULLABLE && other->type->kind != TY_NULL && other->type->kind != TY_UNKNOWN) {
                    /* x === null (or undefined) with x never null: always false, as TypeScript lets it be written
                       (a check kept from code where the value could be absent) */
                    if (!pure_read(other))
                        xs_error(XS2001, e->pos, "only values declared as T | null can be null", "%s can never be null", ty_name(other->type));
                    bool neq = e->op == TK_NEQ;
                    Binding *ob = other->kind == E_IDENT ? lookup(other->str) : NULL;
                    bool flow = (ob && ob->orig) || (other->kind == E_AS && other->op == TK_NOT && !other->elem_type); /* narrowed here, declared T | null */
                    if (!flow) xs_warn_rule(XS2102, e->pos, "remove the test, or declare the value as T | null if it can be null", /* XB-118 rule 4 */
                                 "%s is never null: this comparison is always %s (deprecated)", ty_name(other->type), neq ? "true" : "false");
                    Pos at = e->pos;
                    memset(e, 0, sizeof *e);
                    e->kind = E_BOOL; e->boolean = neq; e->type = ty_boolean; e->field = -1; e->pos = at;
                    return;
                }
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
                { /* a === b with both T | null (T a primitive): equal when both are null or both hold the same value */
                    Type *lt = ty_strip_null(e->lhs->type), *rt = ty_strip_null(e->rhs->type);
                    bool prim = (is_numeric(lt) && is_numeric(rt)) || (lt->kind == TY_STRING && rt->kind == TY_STRING) ||
                                (lt->kind == TY_BOOLEAN && rt->kind == TY_BOOLEAN) ||
                                /* a column read as string | number | null against a string: by what each holds, as === does */
                                (prim_like(lt) && prim_like(rt) && (lt->kind == TY_UNION || rt->kind == TY_UNION || lt->kind == TY_ENUM || rt->kind == TY_ENUM));
                    if (prim) {
                        bool neq = e->op == TK_NEQ;
                        Expr *l = e->lhs, *r = e->rhs;
                        if (lt->kind == TY_INT && rt->kind != TY_INT) { /* (int | null) with (number | null): as numbers */ }
                        to_unknown(l);
                        to_unknown(r);
                        dyn_call(e, "xs_dyn_eq", ty_boolean, l, r, NULL);
                        if (neq) { Expr *v = expr_copy(e); memset(e, 0, sizeof *e); e->kind = E_UNARY; e->op = TK_NOT; e->pos = v->pos; e->field = -1; e->lhs = v; e->type = ty_boolean; }
                        return;
                    }
                }
                { /* a === b with both references of one class (T | null): the same object, or both null */
                    Type *lt = ty_strip_null(e->lhs->type), *rt = ty_strip_null(e->rhs->type);
                    if (((lt->kind == TY_OBJECT || lt->kind == TY_ARRAY || lt->kind == TY_MAP || lt->kind == TY_SET) && ty_equal(lt, rt)) ||
                        (lt->kind == TY_PROMISE && rt->kind == TY_PROMISE)) { /* (a promise kept: the same one) */
                        e->type = ty_boolean;
                        e->builtin.cfn = "ref_eq";
                        return;
                    }
                }
                xs_error(XS2011, e->pos, "compare against null first: if (x !== null)", "cannot compare values that may be null");
            }
            for (int side = 0; side < 2; side++) { /* v === 'x' with v: string | number: by what v holds (TypeScript allows it) */
                Expr *u = side ? e->rhs : e->lhs, *o = side ? e->lhs : e->rhs;
                Type *ot = o->type;
                if (u->type->kind == TY_UNION && (ot->kind == TY_STRING || ot->kind == TY_BOOLEAN || is_numeric(ot) || ot->kind == TY_LITINT)) {
                    to_unknown(u);
                    break;
                }
            }
            if ((e->lhs->type->kind == TY_UNKNOWN) != (e->rhs->type->kind == TY_UNKNOWN)) {
                /* v === '' / v === true with v: unknown: the same value, at run time (as with an any) */
                Expr *l = e->lhs, *r = e->rhs;
                Type *ot = l->type->kind == TY_UNKNOWN ? r->type : l->type;
                if (ot->kind == TY_STRING || ot->kind == TY_BOOLEAN || is_numeric(ot) || ot->kind == TY_LITINT || ot->kind == TY_ENUM ||
                    ot->kind == TY_OBJECT || ot->kind == TY_ARRAY || ot->kind == TY_MAP || ot->kind == TY_SET) { /* (an object: the same one) */
                    if (l->type->kind == TY_LITINT) coerce_lit(l, ty_number);
                    if (r->type->kind == TY_LITINT) coerce_lit(r, ty_number);
                    to_any(l);
                    to_any(r);
                    xs_any_used = true;
                    bool neq = e->op == TK_NEQ;
                    dyn_call(e, "xs_dyn_eq", ty_boolean, l, r, NULL);
                    if (neq) { Expr *v = expr_copy(e); memset(e, 0, sizeof *e); e->kind = E_UNARY; e->op = TK_NOT; e->pos = v->pos; e->field = -1; e->lhs = v; e->type = ty_boolean; }
                    return;
                }
            }
            if (is_numeric(e->lhs->type) || is_numeric(e->rhs->type) || e->lhs->type->kind == TY_LITINT) {
                Type *t = unify_numeric(e, "equality");
                if (t->kind == TY_LITINT) { coerce_lit(e->lhs, ty_number); coerce_lit(e->rhs, ty_number); }
            } else if (!ty_equal(e->lhs->type, e->rhs->type)) {
                xs_error(XS8089, e->pos, NULL, "cannot compare %s with %s", ty_name(e->lhs->type), ty_name(e->rhs->type));
            }
            if (e->lhs->type->kind == TY_ARRAY || e->lhs->type->kind == TY_OBJECT || e->lhs->type->kind == TY_FUNC)
                xs_error(XS8036, e->pos, NULL, "%s values cannot be compared with '==='", ty_name(e->lhs->type));
            e->type = ty_boolean;
            return;
        case TK_AND: case TK_OR:
            if (e->lhs->type->kind != TY_BOOLEAN && cond_truthy_type(e->lhs->type)) make_truthy(e->lhs, "left operand of logical operator");
            if (e->rhs->type->kind != TY_BOOLEAN && cond_truthy_type(e->rhs->type)) make_truthy(e->rhs, "right operand of logical operator");
            expect_type(e->lhs, ty_boolean, "left operand of logical operator");
            expect_type(e->rhs, ty_boolean, "right operand of logical operator");
            e->type = ty_boolean;
            return;
        default:
            xs_error(XS9903, e->pos, NULL, "internal: unexpected binary operator");
        }
        return;
    case E_ASSIGN:
    case E_POSTFIX: {
        if (e->kind == E_ASSIGN && e->op == TK_ASSIGN && e->lhs->kind == E_MEMBER && !e->lhs->is_call && e->lhs->lhs &&
            e->lhs->lhs->kind == E_IDENT && !strcmp(e->lhs->lhs->str, "process") && e->lhs->str && !strcmp(e->lhs->str, "exitCode") &&
            !lookup("process")) { /* process.exitCode = n: the status the program ends with */
            check_expr(e->rhs, ty_int);
            expect_type(e->rhs, ty_int, "process.exitCode");
            Expr *callee = xs_alloc(sizeof *callee);
            callee->kind = E_IDENT; callee->pos = e->pos; callee->str = "process"; callee->field = -1;
            Expr *v = e->rhs;
            e->kind = E_CALL; e->lhs = callee; e->args = (ExprList){0};
            expr_list_push(&e->args, v);
            e->builtin.cfn = "xs_process_set_exit_code";
            e->type = ty_int;
            return;
        }
        if (e->kind == E_ASSIGN && e->op == TK_QQ_ASSIGN) { /* a ??= b: a = a ?? b (b runs only when a is null) */
            Expr *again = copy_target(e->lhs);
            if (!again) xs_error(XS8139, e->pos, "use a variable, a field or an element with a simple index: x ?\?= v", "the left side of '?\?=' must be a variable, a field or an array element");
            Expr *qq = xs_alloc(sizeof *qq);
            qq->kind = E_BINARY;
            qq->op = TK_QQ;
            qq->pos = e->pos;
            qq->lhs = again;
            qq->rhs = e->rhs;
            e->op = TK_ASSIGN;
            e->rhs = qq;
        }
        if (e->kind == E_POSTFIX && ((e->lhs->kind == E_INDEX) || (e->lhs->kind == E_MEMBER && !e->lhs->is_call && e->lhs->str)) &&
            e->lhs->lhs && plain_path(e->lhs->lhs) && !(e->lhs->lhs->kind == E_IDENT && !lookup(e->lhs->lhs->str))) {
            check_expr(e->lhs->lhs, NULL); /* (a path: checking it twice is harmless) */
            Type *rt = e->lhs->lhs->type;
            if (rt && rt->kind == TY_MAP && rt->record) { /* counts[k]++ on a Record<string, number>: counts[k] += 1 */
                Expr *one = xs_alloc(sizeof *one);
                one->kind = E_NUMBER; one->pos = e->pos; one->field = -1; one->num = 1; one->ival = 1; one->is_int_lit = true;
                e->kind = E_ASSIGN;
                e->op = e->op == TK_MINUSMINUS ? TK_MINUS_ASSIGN : TK_PLUS_ASSIGN;
                e->rhs = one;
            }
        }
        if (e->kind == E_ASSIGN && e->op == TK_ASSIGN && e->lhs->kind == E_IDENT) { /* the first assignment of a `let x;` gives it its type */
            Binding *b = lookup(e->lhs->str);
            Binding *root = b;
            while (root && root->origin) root = root->origin;
            if (root && root->auto_var && root->type == ty_auto) {
                check_expr(e->rhs, NULL);
                settle(e->rhs);
                Type *t = e->rhs->type;
                if (t->kind == TY_LITINT) { coerce_lit(e->rhs, ty_number); t = ty_number; }
                if (t->kind == TY_NULL) xs_error(XS2013, e->rhs->pos, "write the type: let x: Todo | null;", "cannot infer the type of '%s' from null", root->name);
                if (t->kind == TY_VOID) xs_error(XS8102, e->rhs->pos, NULL, "cannot store a void value");
                e->rhs->prechecked = true;
                for (Binding *q = b; q; q = q->origin) if (q->type == ty_auto) q->type = t;
                Stmt *st = root->auto_var;
                st->type = t;
                check_expr(st->init, t);
                if (st->init->kind == E_NULL) st->init->type = t->kind == TY_NULLABLE ? t : st->init->type;
            }
        }
        Expr *target = e->lhs;
        bool dyn_target = false;
        if (target->kind == E_MEMBER && !target->is_call && target->str && target->lhs && rereadable(target->lhs) &&
            !(target->lhs->kind == E_IDENT && !lookup(target->lhs->str))) {
            /* request.terminal = t on an open class (a field __expando: Express's Request): the property goes there */
            check_expr(target->lhs, NULL);
            Type *rt = target->lhs->type;
            Class *rc = rt && rt->kind == TY_OBJECT && !rt->cls->builtin ? rt->cls : NULL;
            int xi;
            Field *xf = rc ? find_field(rc, "__expando", &xi) : NULL;
            if (xf && xf->type->kind == TY_MAP && xf->type->record && !find_field(rc, target->str, NULL) &&
                !find_method(rc, xs_sprintf("__set_%s", target->str)) && !find_method(rc, target->str)) {
                Expr *holder = xs_alloc(sizeof *holder);
                holder->kind = E_MEMBER; holder->pos = target->pos; holder->field = -1; holder->lhs = target->lhs; holder->str = "__expando";
                target->lhs->prechecked = true;
                Expr *key = xs_alloc(sizeof *key);
                key->kind = E_STRING; key->pos = target->pos; key->field = -1; key->str = target->str; key->type = ty_string; key->lit_id = intern(target->str);
                target->kind = E_INDEX;
                target->lhs = holder;
                target->rhs = key;
                target->str = NULL;
            }
        }
        if (e->kind == E_ASSIGN && ((target->kind == E_INDEX) || (target->kind == E_MEMBER && !target->is_call && target->str)) && target->lhs &&
            target->lhs->kind == E_AS && target->lhs->elem_type && ty_is_any(target->lhs->elem_type) && target->lhs->lhs->kind != E_OBJLIT) {
            /* (o as any).f = v: o's own field when its class has it (the cast only quiets TypeScript), else by name */
            Expr *o = target->lhs, *inner = o->lhs;
            check_expr(inner, NULL);
            Type *it = inner->type;
            if (target->kind == E_MEMBER && it->kind == TY_OBJECT && !it->cls->builtin && find_field(it->cls, target->str, NULL) && plain_path(inner))
                target->lhs = inner;
            else { *o = *inner; to_any(o); dyn_target = true; }
        }
        if (dyn_target) {
            /* x.f = v / x[k] = v on an any: by name, checked at run time */
            {
                if (e->op != TK_ASSIGN) xs_error(XS2018, e->pos, "read it, compute, and assign: x.f = x.f + v (or give x its type)", "compound assignment to a field of an any is not supported");
                Expr *key;
                const char *fn = "xs_dyn_set";
                if (target->kind == E_MEMBER) key = dyn_key(target->pos, target->str);
                else {
                    key = target->rhs;
                    check_expr(key, NULL);
                    if (key->type->kind == TY_LITINT) coerce_lit(key, ty_int); else settle(key);
                    if (key->type->kind != TY_STRING) { to_any(key); fn = "xs_dyn_set_any"; }
                }
                check_expr(e->rhs, ty_any);
                to_any(e->rhs);
                dyn_call(e, fn, ty_void, target->lhs, key, e->rhs);
                return;
            }
        }
        if (e->kind == E_ASSIGN && ((target->kind == E_INDEX) || (target->kind == E_MEMBER && !target->is_call && target->str)) &&
            target->lhs && plain_path(target->lhs) && !(target->lhs->kind == E_IDENT && !lookup(target->lhs->str))) {
            check_expr(target->lhs, NULL); /* a variable or a field path: checking it twice is harmless */
            target->lhs = rec_get_nn(target->lhs);
            if (target->kind == E_INDEX && target->lhs->type && target->lhs->type->kind == TY_OBJECT && !target->lhs->type->cls->builtin &&
                target->rhs->kind == E_IDENT) { /* req[AUDIT_CONTEXT_KEY] = v: the field the constant names */
                const char *val = const_string_of(target->rhs->str);
                if (val && find_field(target->lhs->type->cls, val, NULL)) { target->kind = E_MEMBER; target->str = (char *)val; target->rhs = NULL; }
            }
            if (target->kind == E_INDEX && target->lhs->type && target->lhs->type->kind == TY_OBJECT && !target->lhs->type->cls->builtin &&
                !target->lhs->type->cls->is_tuple && target->rhs->kind != E_STRING && target->rhs->kind != E_NUMBER) {
                check_expr(target->rhs, NULL);
                if (target->rhs->type->kind == TY_STRING) { xs_note_dynamic(target->pos); to_any(target->lhs); } /* o[k] = v, k: keyof O */
            }
            if (target->lhs->type && ty_is_any(target->lhs->type) && e->op >= TK_PLUS_ASSIGN && e->op <= TK_SLASH_ASSIGN &&
                (target->kind == E_MEMBER || pure_read(target->rhs))) {
                /* o[k] += v with o an any: o[k] = o[k] + v (the key read twice: a name or a literal) */
                TokKind op = e->op == TK_PLUS_ASSIGN ? TK_PLUS : e->op == TK_MINUS_ASSIGN ? TK_MINUS : e->op == TK_STAR_ASSIGN ? TK_STAR : TK_SLASH;
                Expr *rd = expr_copy(target);
                rd->lhs = expr_copy(target->lhs);
                rd->lhs->prechecked = true;
                if (target->kind == E_INDEX) rd->rhs = expr_copy(target->rhs);
                Expr *bin = xs_alloc(sizeof *bin);
                bin->kind = E_BINARY; bin->pos = e->pos; bin->field = -1; bin->op = op; bin->lhs = rd; bin->rhs = e->rhs;
                e->rhs = bin;
                e->op = TK_ASSIGN;
            }
            if (target->lhs->type && ty_is_any(target->lhs->type)) { /* o.a.f = v with o.a an any (a field, a Record's value) */
                if (e->op != TK_ASSIGN) xs_error(XS2018, e->pos, "read it, compute, and assign: x.f = x.f + v (or give x its type)", "compound assignment to a field of an any is not supported");
                Expr *key;
                const char *fn = "xs_dyn_set";
                if (target->kind == E_MEMBER) key = dyn_key(target->pos, target->str);
                else {
                    key = target->rhs;
                    check_expr(key, NULL);
                    if (key->type->kind == TY_LITINT) coerce_lit(key, ty_int); else settle(key);
                    if (key->type->kind != TY_STRING) { to_any(key); fn = "xs_dyn_set_any"; }
                }
                check_expr(e->rhs, ty_any);
                to_any(e->rhs);
                dyn_call(e, fn, ty_void, target->lhs, key, e->rhs);
                return;
            }
            if (target->lhs->type && target->lhs->type->kind == TY_MAP && target->lhs->type->record) { /* m[k] = v: m.set(k, v) */
                Expr *key;
                if (target->kind == E_INDEX) key = target->rhs;
                else {
                    key = xs_alloc(sizeof *key);
                    key->kind = E_STRING; key->pos = target->pos; key->field = -1; key->str = target->str;
                }
                Expr *val = e->rhs;
                if (e->op != TK_ASSIGN) { /* m[k] += v: m.set(k, m.get(k) + v) */
                    TokKind op = e->op == TK_PLUS_ASSIGN ? TK_PLUS : e->op == TK_MINUS_ASSIGN ? TK_MINUS : e->op == TK_STAR_ASSIGN ? TK_STAR : TK_SLASH;
                    Expr *get = xs_alloc(sizeof *get);
                    get->kind = E_MEMBER; get->pos = target->pos; get->field = -1; get->lhs = target->lhs; get->str = "get"; get->is_call = true;
                    expr_list_push(&get->args, key);
                    Expr *bin = xs_alloc(sizeof *bin);
                    bin->kind = E_BINARY; bin->pos = e->pos; bin->field = -1; bin->op = op; bin->lhs = get; bin->rhs = val;
                    val = bin;
                }
                Expr *map = target->lhs;
                e->kind = E_MEMBER;
                e->lhs = map;
                e->rhs = NULL;
                e->str = "set";
                e->is_call = true;
                e->args = (ExprList){0};
                expr_list_push(&e->args, key);
                expr_list_push(&e->args, val);
                check_expr(e, NULL);
                return;
            }
        }
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
            dyn_lvalue = target;
            check_expr(target, NULL);
            dyn_lvalue = NULL;
            /* o.f = v with o.f narrowed: the target is the field itself, not its checked read (inner reads, as
               the n.next of n.next.next = v, stay checked reads) */
            if (target->kind == E_AS && target->op == TK_NOT && !target->elem_type && target->lhs->kind == E_MEMBER) *target = *target->lhs;
            path_assigned = path_key(target); /* settled after the value is checked (below) */
        }
        Binding *narrowed = NULL;
        bool widen_rhs = false; /* XB-094 */
        if (target->kind == E_IDENT) {
            Binding *b = lookup(target->str);
            for (Binding *o = b; o; o = o->origin) if (o->lit_number) widen_rhs = true;
            if (strncmp(b->cname, "env->", 5) == 0)
                xs_error(XS8085, e->pos, "arrow functions capture by value; keep the state in an object or array and change that", "cannot assign to '%s', captured from the enclosing function", target->str);
            if (b->is_const && !target->boolean) xs_error(XS8087, e->pos, "declare it with 'let' if it changes", "cannot assign to constant '%s'", target->str);
            if (b->is_param && ty_is_heap(b->type))
                xs_error(XS8099, e->pos, "copy it into a local first: let s: string = param;", "cannot reassign parameter '%s' of reference type", target->str);
            if (b->orig) { narrowed = b; target->type = b->orig; }
        } else if (target->kind == E_INDEX) {
            if (target->builtin.cfn && strcmp(target->builtin.cfn, "xs_u8_get") != 0) xs_error(XS8076, e->pos, NULL, "a match's groups are read-only");
        } else if (target->kind == E_MEMBER && target->field >= 0) {
            if (target->cls->builtin && !(strcmp(target->cls->name, "RegExp") == 0 && strcmp(target->str, "lastIndex") == 0))
                xs_error(XS8135, e->pos, NULL, "the fields of %s are read-only", target->cls->name);
            Field *fd = &target->cls->fields[target->field];
            if (fd->readonly && !(cur_func && cur_func->is_ctor && cur_func->cls == fd->owner))
                xs_error(XS8086, e->pos, xs_sprintf("assign it in the constructor of %s, or drop readonly", fd->owner ? fd->owner->name : target->cls->name), "cannot assign to '%s': it is readonly", fd->name);
        } else if (target->kind == E_MEMBER && !target->is_call && target->str && strcmp(target->str, "length") == 0 && e->op == TK_ASSIGN &&
                   target->lhs->type && target->lhs->type->kind == TY_ARRAY) {
            /* xs.length = n (n <= length): the array cut to its first n elements, as JavaScript does; __xs_truncate(xs, n) */
            Expr *callee = xs_alloc(sizeof *callee);
            callee->kind = E_IDENT; callee->pos = e->pos; callee->str = "__xs_truncate"; callee->field = -1;
            Expr *c = xs_alloc(sizeof *c);
            c->kind = E_CALL; c->pos = e->pos; c->field = -1;
            c->lhs = callee;
            expr_list_push(&c->args, target->lhs);
            expr_list_push(&c->args, e->rhs);
            *e = *c;
            check_expr(e, NULL);
            return;
        } else {
            xs_error(XS8138, e->pos, NULL, "the left side must be a variable, a field or an array element");
        }
        if (e->kind == E_POSTFIX) {
            if (!is_numeric(target->type))
                xs_error(XS8117, e->pos, NULL, "operand of '++'/'--' must be int or number, but this is %s", ty_name(target->type));
            e->type = target->type;
            return;
        }
        check_expr(e->rhs, target->type);
        if (e->op != TK_ASSIGN && is_big(target->type)) { /* acc += x: acc = acc + x (a bigint is a value: a new one) */
            const char *f = big_op(e->op);
            if (!f) xs_error(XS6002, e->pos, "supported on bigint: += -= *= /=", "%s is not supported on bigint", tok_kind_name(e->op));
            if (!is_big(e->rhs->type)) xs_error(XS8094, e->rhs->pos, "convert it: BigInt(i)", "cannot mix bigint and %s", ty_name(e->rhs->type));
            if (!rereadable(target)) xs_error(XS8104, e->pos, "store the target in a const first", "compound assignment of a bigint needs a variable or a field");
            Expr *cur = expr_copy(target), *call = xs_alloc(sizeof *call);
            call->pos = e->pos;
            big_call(call, f, cur, e->rhs, ty_big());
            big_fuse(call);
            e->rhs = call;
            e->op = TK_ASSIGN;
        }
        if (e->op == TK_SLASH_ASSIGN && target->type->kind == TY_INT)
            xs_error(XS8065, e->pos, "division yields number; write x = Math.trunc(x / y)", "'/=' cannot be applied to an int");
        if (e->op != TK_ASSIGN && !is_numeric(target->type) && !(e->op == TK_PLUS_ASSIGN && target->type->kind == TY_STRING))
            xs_error(XS8103, e->pos, NULL, "compound assignment needs int, number or string, but this is %s", ty_name(target->type));
        if (tk_bitop_of_assign(e->op) && target->type->kind == TY_NUMBER && tsc_checked() && rereadable(target)) {
            /* h ^= c with h: number (FNV's let h = 0x811c9dc5): h = h ^ c, the 32-bit result back as a number */
            Expr *bin = xs_alloc(sizeof *bin);
            bin->kind = E_BINARY; bin->pos = e->pos; bin->field = -1; bin->op = tk_bitop_of_assign(e->op);
            bin->lhs = expr_copy(target); bin->rhs = e->rhs;
            e->rhs->prechecked = true;
            check_expr(bin, NULL);
            expect_type(bin, ty_number, "the result");
            e->rhs = bin;
            e->op = TK_ASSIGN;
            e->type = target->type;
            if (narrowed) unnarrow(narrowed);
            return;
        }
        if (tk_bitop_of_assign(e->op)) { /* x &= m: x must hold the int result */
            if (target->type->kind != TY_INT) xs_error(XS8024, e->pos, "declare it as int: bitwise operators give 32-bit integers", "%s needs an int on the left, but this is %s", tok_kind_name(e->op), ty_name(target->type));
            if (e->rhs->type->kind == TY_LITINT) coerce_lit(e->rhs, ty_int);
            if (!is_numeric(e->rhs->type)) xs_error(XS8122, e->rhs->pos, NULL, "right side of %s must be int or number", tok_kind_name(e->op));
            e->type = ty_int;
            return;
        }
        if (widen_rhs && e->op != TK_ASSIGN && target->type->kind == TY_NUMBER && e->rhs->type->kind == TY_INT &&
            (e->op == TK_PLUS_ASSIGN || e->op == TK_MINUS_ASSIGN || e->op == TK_STAR_ASSIGN || e->op == TK_SLASH_ASSIGN) &&
            !getenv("XISTO_NO_LIT_WIDEN")) {
            Expr *a = xs_alloc(sizeof *a);
            *a = *e->rhs;
            Expr *c = e->rhs;
            memset(c, 0, sizeof *c);
            c->kind = E_CALL; c->pos = a->pos; c->field = -1; c->type = ty_number;
            c->builtin.cfn = "xs_to_number";
            expr_list_push(&c->args, a);
            Expr *callee = xs_alloc(sizeof *callee);
            callee->kind = E_IDENT; callee->pos = a->pos; callee->str = "Number"; callee->field = -1;
            c->lhs = callee;
        }
        expect_type(e->rhs, target->type, e->op == TK_ASSIGN ? "assigned value" : "right side of compound assignment");
        e->type = target->type;
        if (narrowed) unnarrow(narrowed);
        if (target->kind == E_IDENT && e->op == TK_ASSIGN && e->rhs->type->kind != TY_NULLABLE && e->rhs->type->kind != TY_NULL &&
            e->rhs->kind != E_NULL && !ty_is_any(e->rhs->type) && e->rhs->type->kind != TY_UNKNOWN) {
            /* as TypeScript: after x = <not null>, x is not null (until it is assigned again) */
            Binding *b = lookup(target->str);
            if (b && b->type->kind == TY_NULLABLE && !b->cell && strncmp(b->cname, "env->", 5) != 0) shadow_narrowed(b);
        }
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
            if (t->kind == TY_PROMISE) t = t->elem; /* a method's Promise<T>: what it gives is T (XB-106) */
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
            else xs_error(XS7116, e->pos, NULL, "unknown reflection intrinsic '%s'", e->builtin.cfn);
            return;
        }
        if (e->lhs->kind != E_IDENT) { /* (() => ...)(), f(a)(b): the function value an expression gives */
            check_expr(e->lhs, NULL);
            Type *ft = e->lhs->type;
            if (ft->kind != TY_FUNC) xs_error(XS6030, e->pos, NULL, "only named functions can be called");
            pack_rest(e, ft);
            if (e->args.len != ft->nparams) xs_error(XS8062, e->pos, NULL, "'%s' takes %d argument(s), got %d", "the function", ft->nparams, e->args.len);
            for (int i = 0; i < e->args.len; i++) {
                check_expr(e->args.items[i], ft->params[i]);
                expect_type(e->args.items[i], ft->params[i], xs_sprintf("argument %d", i + 1));
            }
            e->type = ft->ret;
            return;
        }
        if (strcmp(e->lhs->str, "super") == 0 && !lookup("super")) { /* super(...): the base's constructor on this object */
            Class *cc = cur_func ? cur_func->cls : NULL;
            if (cur_func && cur_func->is_ctor && cc && cc->extends_error) { /* Error's constructor: super(message, { cause }?) */
                if (e->args.len > 2) xs_error(XS8027, e->pos, NULL, "the constructor of Error takes 0 to 2 argument(s), got %d", e->args.len);
                Expr *msg = e->args.len ? e->args.items[0] : str_lit(e->pos, "");
                Expr *self = xs_alloc(sizeof *self), *fm = xs_alloc(sizeof *fm);
                self->kind = E_THIS; self->pos = e->pos; self->field = -1;
                fm->kind = E_MEMBER; fm->pos = e->pos; fm->lhs = self; fm->str = "message"; fm->field = -1;
                memset(&e->args, 0, sizeof e->args);
                e->kind = E_ASSIGN;
                e->op = TK_ASSIGN;
                e->lhs = fm;
                e->rhs = msg;
                check_expr(e, NULL);
                return;
            }
            if (!cur_func || !cur_func->is_ctor || !cc || !cc->base)
                xs_error(XS8125, e->pos, NULL, "super(...) is called in the constructor of a class that extends another");
            Func *bc = eff_ctor(cc->base);
            if (bc) check_args(e, bc, xs_sprintf("the constructor of %s", cc->base->name));
            else if (e->args.len) xs_error(XS8011, e->pos, NULL, "%s has no constructor, so super() takes no arguments", cc->base->name);
            e->kind = E_SUPER_CALL;
            e->cls = cc->base;
            e->func = bc;
            e->type = ty_void;
            super_calls++;
            return;
        }
        const char *name = e->lhs->str;
        if (!lookup_func(name) && !lookup(name)) {
            if (strcmp(name, "Boolean") == 0 && e->args.len == 1) { /* Boolean(x): x's truthiness, as !!x */
                Expr *x = e->args.items[0];
                Expr *n1 = xs_alloc(sizeof *n1);
                n1->kind = E_UNARY; n1->op = TK_NOT; n1->pos = e->pos; n1->field = -1; n1->lhs = x;
                Pos at = e->pos;
                memset(e, 0, sizeof *e);
                e->kind = E_UNARY; e->op = TK_NOT; e->pos = at; e->field = -1; e->lhs = n1;
                check_expr(e, expected);
                return;
            }
            if (!strcmp(name, "encodeURIComponent") || !strcmp(name, "encodeURI") || !strcmp(name, "decodeURIComponent") || !strcmp(name, "decodeURI")) {
                /* the URI globals, on strings (UTF-8 bytes as %XX) */
                if (e->args.len != 1) xs_error(XS8032, e->pos, NULL, "%s takes exactly one argument", name);
                check_expr(e->args.items[0], ty_string);
                expect_type(e->args.items[0], ty_string, xs_sprintf("argument of %s", name));
                e->type = ty_string;
                e->builtin.cfn = !strcmp(name, "encodeURIComponent") ? "xs_encode_uri_component" : !strcmp(name, "encodeURI") ? "xs_encode_uri"
                               : !strcmp(name, "decodeURIComponent") ? "xs_decode_uri_component" : "xs_decode_uri";
                return;
            }
            if (strcmp(name, "isNaN") == 0 || strcmp(name, "isFinite") == 0) { /* the globals, on numbers */
                if (e->args.len != 1) xs_error(XS8032, e->pos, NULL, "%s takes exactly one argument", name);
                check_expr(e->args.items[0], ty_number);
                expect_type(e->args.items[0], ty_number, xs_sprintf("argument of %s", name));
                e->type = ty_boolean;
                e->builtin.cfn = name[2] == 'N' ? "xs_is_nan" : "xs_is_finite";
                return;
            }
            if (strcmp(name, "setTimeout") == 0 || strcmp(name, "setInterval") == 0) { /* Node's timers: a callback later, on the event loop */
                if (e->args.len != 2) xs_error(XS8035, e->pos, xs_sprintf("%s(() => { ... }, ms)", name), "%s takes a callback and a delay in milliseconds", name);
                Type *cbt = ty_func(NULL, 0, ty_void);
                check_expr(e->args.items[0], cbt);
                expect_type(e->args.items[0], cbt, xs_sprintf("callback of %s", name));
                check_expr(e->args.items[1], ty_number);
                if (e->args.items[1]->type->kind == TY_LITINT) coerce_lit(e->args.items[1], ty_number);
                expect_type(e->args.items[1], ty_number, "the delay");
                e->type = ty_int;
                e->builtin.cfn = name[3] == 'T' ? "xs_set_timeout" : "xs_set_interval";
                prog->uses_net = true; /* timers live with the event loop */
                return;
            }
            if (strcmp(name, "clearTimeout") == 0 || strcmp(name, "clearInterval") == 0) {
                if (e->args.len != 1) xs_error(XS8035, e->pos, NULL, "%s takes the timer's id", name);
                check_expr(e->args.items[0], ty_number);
                if (e->args.items[0]->type->kind == TY_LITINT) coerce_lit(e->args.items[0], ty_number);
                expect_type(e->args.items[0], ty_number, "the timer's id");
                e->type = ty_void;
                e->builtin.cfn = "xs_clear_timer";
                prog->uses_net = true;
                return;
            }
            if (strcmp(name, "btoa") == 0 || strcmp(name, "atob") == 0) { /* base64 of a string of bytes (code points 0..255) */
                if (e->args.len != 1) xs_error(XS8035, e->pos, NULL, "%s takes one string", name);
                check_expr(e->args.items[0], ty_string);
                expect_type(e->args.items[0], ty_string, xs_sprintf("argument of %s", name));
                e->type = ty_string;
                e->builtin.cfn = name[0] == 'b' ? "xs_btoa" : "xs_atob";
                return;
            }
            if (strcmp(name, "parseInt") == 0 || strcmp(name, "parseFloat") == 0) { /* the JS globals: number, NaN when no digits */
                bool pi = name[5] == 'I';
                if (e->args.len < 1 || e->args.len > (pi ? 2 : 1)) xs_error(XS8030, e->pos, pi ? "parseInt(text) or parseInt(text, radix)" : "parseFloat(text)", "%s takes %s", name, pi ? "a string and an optional radix" : "one string");
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
                if (e->args.len != 1) xs_error(XS7003, e->pos, NULL, "BigInt() takes exactly one argument");
                Expr *a = e->args.items[0];
                check_expr(a, NULL);
                if (a->type->kind == TY_LITINT) coerce_lit(a, ty_int);
                if (a->type->kind == TY_INT) e->builtin.cfn = "xs_big_from_int";
                else if (a->type->kind == TY_NUMBER) e->builtin.cfn = "xs_big_from_num";
                else if (a->type->kind == TY_STRING) e->builtin.cfn = "xs_big_from_str";
                else if (is_big(a->type)) { *e = *a; return; }
                else xs_error(XS7002, a->pos, NULL, "BigInt() converts int, number or string, but this is %s", ty_name(a->type));
                e->type = ty_big();
                return;
            }
            if (strcmp(name, "Symbol") == 0 && !lookup("Symbol")) { /* Symbol("desc"): a new symbol */
                if (e->args.len > 1) xs_error(XS7123, e->pos, NULL, "Symbol() takes at most one argument (the description)");
                if (e->args.len == 0) { Expr *nul = xs_alloc(sizeof *nul); nul->kind = E_NULL; nul->pos = e->pos; nul->field = -1; expr_list_push(&e->args, nul); }
                check_expr(e->args.items[0], ty_nullable_of(ty_string));
                expect_type(e->args.items[0], ty_nullable_of(ty_string), "the description of a symbol");
                e->builtin.cfn = "xs_Symbol_new";
                e->type = ty_object_of(find_class("symbol"));
                return;
            }
            if (strcmp(name, "Number") == 0) {
                if (e->args.len != 1) xs_error(XS7028, e->pos, NULL, "Number() takes exactly one argument");
                { /* Number(senha || 0): senha ? Number(senha) : 0 (the default a number, the value a string) */
                    Expr *o = e->args.items[0];
                    if (o->kind == E_BINARY && o->op == TK_OR && o->rhs->kind == E_NUMBER && rereadable(o->lhs)) {
                        Expr *cd = xs_alloc(sizeof *cd);
                        cd->kind = E_COND; cd->pos = e->pos; cd->field = -1; cd->lhs = pure_copy(o->lhs);
                        if (cd->lhs) {
                            Expr *conv = xs_alloc(sizeof *conv);
                            *conv = *e;
                            conv->args = (ExprList){0};
                            expr_list_push(&conv->args, o->lhs);
                            expr_list_push(&cd->args, conv);
                            Expr *n = o->rhs;
                            if (n->is_int_lit) { n->is_int_lit = false; n->num = (double)n->ival; }
                            expr_list_push(&cd->args, n);
                            *e = *cd;
                            check_expr(e, expected);
                            return;
                        }
                    }
                }
                check_expr(e->args.items[0], NULL);
                Expr *a = e->args.items[0];
                if (a->type->kind == TY_LITINT) coerce_lit(a, ty_number);
                if (a->type->kind == TY_STRING) e->builtin.cfn = "xs_str_to_number"; /* NaN when not a number, as in JS */
                else if (a->type->kind == TY_INT) e->builtin.cfn = "xs_to_number";
                else if (a->type->kind == TY_NUMBER) e->builtin.cfn = "xs_to_number_id";
                else if (is_big(a->type)) e->builtin.cfn = "xs_big_to_number";
                else if (a->type->kind == TY_BOOLEAN) e->builtin.cfn = "xs_bool_to_number"; /* 1 or 0 */
                else if (a->type->kind == TY_UNKNOWN) { e->builtin.cfn = "xs_dyn_num"; xs_any_used = true; } /* as JavaScript converts, at run time */
                else if (a->type->kind == TY_UNION || a->type->kind == TY_NULLABLE) { /* Number(x) with x: number | string, string | null: by what it holds */
                    if (a->type->kind != TY_UNION) to_unknown(a);
                    e->builtin.cfn = "xs_dyn_num"; xs_any_used = true;
                }
                else xs_error(XS7027, a->pos, NULL, "Number() converts int, bigint or string to number, but this is %s", ty_name(a->type));
                e->type = ty_number;
                return;
            }
            if (strcmp(name, "String") == 0) {
                if (e->args.len != 1) xs_error(XS7041, e->pos, NULL, "String() takes exactly one argument");
                check_expr(e->args.items[0], NULL);
                Expr *a = e->args.items[0];
                settle(a);
                if (ty_is_undef(a->type)) { undef_text(a); *e = *a; return; } /* String(f()) with f(): undefined */
                if (a->type->kind == TY_INT) e->builtin.cfn = "xs_str_from_int";
                else if (a->type->kind == TY_NUMBER) e->builtin.cfn = "xs_str_from_num";
                else if (a->type->kind == TY_BOOLEAN) e->builtin.cfn = "xs_str_from_bool";
                else if (a->type->kind == TY_STRING) e->builtin.cfn = "xs_retain_str";
                else if (is_big(a->type)) e->builtin.cfn = "xs_big_to_string";
                else if (a->type->kind == TY_ENUM) e->builtin.cfn = a->type->en->is_string ? "xs_retain_str" : "xs_str_from_int";
                else if (a->type->kind == TY_OBJECT && (is_error_class(a->type->cls) || (a->type->cls->builtin && strcmp(a->type->cls->name, "Error") == 0)))
                    e->builtin.cfn = "xs_Error_to_string"; /* "Name: message", as Error.prototype.toString */
                else if (a->type->kind == TY_UNKNOWN) { e->builtin.cfn = "xs_dyn_str"; xs_any_used = true; }
                else if (a->type->kind == TY_UNION || (a->type->kind == TY_NULLABLE && a->type->elem->kind != TY_OBJECT)) { /* String(x) with x: string | number, string | null */
                    if (a->type->kind != TY_UNION) to_unknown(a);
                    e->builtin.cfn = "xs_dyn_str"; xs_any_used = true;
                }
                else if (a->type->kind == TY_OBJECT && a->type->cls->builtin && strcmp(a->type->cls->name, "symbol") == 0) e->builtin.cfn = "xs_Symbol_to_string";
                else if (tostring_call(a)) { *e = *a; return; }
                else xs_error(XS7040, a->pos, NULL, "String() converts int, number or boolean, but this is %s", ty_name(a->type));
                e->type = ty_string;
                return;
            }
            if (strcmp(name, "assert") == 0) {
                if (e->args.len < 1 || e->args.len > 2) xs_error(XS7048, e->pos, "assert(condition, \"message\")", "assert takes a condition and an optional message");
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
                if (e->args.len != (is_lines ? 1 : 2)) xs_error(XS8025, e->pos, is_lines ? "lines(path)" : "chunks(path, bytes)", "%s takes %d argument(s)", name, is_lines ? 1 : 2);
                check_expr(e->args.items[0], ty_string);
                expect_type(e->args.items[0], ty_string, xs_sprintf("path of %s", name));
                if (!is_lines) { check_expr(e->args.items[1], ty_int); expect_type(e->args.items[1], ty_int, "chunk size"); }
                e->type = ty_stream_of(ty_string);
                e->builtin.cfn = is_lines ? "xs_stream_lines" : "xs_stream_chunks";
                return;
            }
            if (strcmp(name, "fetch") == 0) {
                if (e->args.len < 1 || e->args.len > 2) xs_error(XS7054, e->pos, "fetch(url) or fetch(url, { method, headers, body, signal })", "fetch() takes a URL and optional options");
                check_expr(e->args.items[0], ty_string);
                expect_type(e->args.items[0], ty_string, "argument of fetch");
                if (e->args.len == 2) {
                    Expr *init0 = e->args.items[1];
                    bool generic = init0->kind != E_OBJLIT;
                    for (int i = 0; !generic && i < init0->args.len; i++)
                        if (!strcmp(init0->keys[i], "...") || !strcmp(init0->keys[i], "redirect")) generic = true;
                    if (generic) { /* options in a value or with spreads: xisto:url's __xs_fetch(url, init: RequestInit) */
                        int gscheme = url_scheme(e->args.items[0]); /* (TLS by this URL: __xs_fetch's own fetch does not decide it) */
                        if (gscheme == 1 && xs_tls_off) xs_error(XS7059, e->args.items[0]->pos, "remove --tls=off, or call the service over http://", "https URL, but the program is built with --tls=off");
                        if (gscheme == 1 || (gscheme == -1 && !xs_tls_off)) prog->uses_tls = true;
                        prog->uses_net = true;
                        Expr *callee = xs_alloc(sizeof *callee);
                        callee->kind = E_IDENT; callee->pos = e->pos; callee->field = -1; callee->str = "__xs_fetch";
                        e->lhs = callee;
                        e->builtin.cfn = NULL;
                        e->args.items[0]->prechecked = true;
                        check_expr(e, expected);
                        return;
                    }
                    /* the options object of the Fetch API, checked by key: method, body, headers, signal */
                    Expr *init = e->args.items[1];
                    if (init->kind != E_OBJLIT) xs_error(XS7053, init->pos, "write the options inline: fetch(url, { method: \"POST\", body })", "fetch options must be an object literal");
                    for (int i = 0; i < init->args.len; i++) {
                        const char *k = init->keys[i];
                        Expr *v = init->args.items[i];
                        if (strcmp(k, "method") == 0 || strcmp(k, "body") == 0) {
                            check_expr(v, ty_string);
                            if (strcmp(k, "body") == 0 && v->type && v->type->kind == TY_OBJECT && v->type->cls &&
                                strcmp(v->type->cls->name, "URLSearchParams") == 0) { /* body: new URLSearchParams({...}): its text */
                                Expr *ts = xs_alloc(sizeof *ts);
                                ts->kind = E_MEMBER; ts->pos = v->pos; ts->field = -1; ts->is_call = true; ts->lhs = v; ts->str = "toString";
                                check_expr(ts, ty_string);
                                init->args.items[i] = v = ts;
                            }
                            if (strcmp(k, "body") == 0 && v->type && v->type->kind == TY_OBJECT && v->type->cls && v->type->cls->builtin &&
                                (strcmp(v->type->cls->name, "Uint8Array") == 0 || strcmp(v->type->cls->name, "Buffer") == 0)) {
                                wrap_call(v, "xs_fetch_body_bytes", ty_string); /* body: bytes, sent as they are */
                            }
                            expect_type(v, ty_string, xs_sprintf("fetch option '%s'", k));
                        } else if (strcmp(k, "headers") == 0 && v->kind != E_OBJLIT) {
                            /* headers built before the call: a Record<string, string> (sent in insertion order) */
                            check_expr(v, NULL);
                            Type *ht = v->type;
                            if (!(ht && ht->kind == TY_MAP && ht->key && ht->key->kind == TY_STRING && ht->elem && ht->elem->kind == TY_STRING))
                                xs_error(XS7058, v->pos, "headers: { \"Content-Type\": \"application/json\" }, or a Record<string, string>", "headers must be an object literal");
                        } else if (strcmp(k, "headers") == 0) {
                            for (int h = 0; h < v->args.len; h++) {
                                check_expr(v->args.items[h], ty_string);
                                expect_type(v->args.items[h], ty_string, xs_sprintf("header '%s'", v->keys[h]));
                                intern(v->keys[h]);
                            }
                            v->type = ty_void;
                        } else if (strcmp(k, "signal") == 0) {
                            if (!(v->kind == E_MEMBER && v->is_call && v->lhs->kind == E_IDENT && strcmp(v->lhs->str, "AbortSignal") == 0 && strcmp(v->str, "timeout") == 0 && v->args.len == 1)) {
                                /* signal: controller.signal (an AbortSignal of xisto:url): fetch reads its __flag byte
                                   while it waits, and rejects with an AbortError once abort() sets it */
                                check_expr(v, NULL);
                                Type *st = v->type && v->type->kind == TY_NULLABLE ? v->type->elem : v->type;
                                if (!(st && st->kind == TY_OBJECT && st->cls && strcmp(st->cls->name, "AbortSignal") == 0 && st->cls->mod &&
                                      st->cls->mod->file && strcmp(st->cls->mod->file, "xisto:url") == 0))
                                    xs_error(XS7102, v->pos, "signal: AbortSignal.timeout(5000), or an AbortController's signal", "the signal must be an AbortSignal");
                                Expr *fl = xs_alloc(sizeof *fl);
                                fl->kind = E_MEMBER; fl->pos = v->pos; fl->field = -1; fl->lhs = v; fl->str = "__flag";
                                check_expr(fl, NULL);
                                init->args.items[i] = fl;
                                continue;
                            }
                            check_expr(v->args.items[0], ty_int);
                            expect_type(v->args.items[0], ty_int, "timeout in milliseconds");
                            v->type = ty_int;
                        } else {
                            static const char *keys[] = {"method", "body", "headers", "signal"};
                            xs_error(XS7120, v->pos, xs_help_suggest(k, keys, 4, "supported: method, body, headers, signal"), "unsupported fetch option '%s'", k);
                        }
                    }
                    init->type = ty_void;
                }
                { /* the built-in Response, also where a module has a class of that name (Express's) */
                    Class *fr = NULL;
                    for (int i = 0; i < nbuiltins; i++) if (strcmp(builtins[i]->name, "Response") == 0) fr = builtins[i];
                    e->type = ty_object_of(fr);
                }
                e->builtin.cfn = "xs_fetch";
                prog->uses_net = true;
                /* TLS is linked only when https can happen: an http:// URL (TLS ends at the load balancer) costs nothing */
                int scheme = url_scheme(e->args.items[0]);
                if (cur_mod && cur_mod->file && !strcmp(cur_mod->file, "xisto:url")) return; /* (__xs_fetch: its caller's URL decides TLS) */
                if (scheme == 1 && xs_tls_off) xs_error(XS7059, e->args.items[0]->pos, "remove --tls=off, or call the service over http://", "https URL, but the program is built with --tls=off");
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
                if (1) /* (any function value: an argument left out where the parameter can be null, as an optional one, e.g. createParamDecorator(...)() with data?) a decorator for members also used on a class ((c, m: MethodInfo | null) => void, @SetMetadata on a controller): null for what a class has not */
                    while (e->args.len < ft->nparams && (ft->params[e->args.len]->kind == TY_NULLABLE || ft->params[e->args.len]->kind == TY_UNKNOWN)) {
                        Expr *nul = xs_alloc(sizeof *nul);
                        nul->kind = E_NULL; nul->pos = e->pos; nul->field = -1;
                        expr_list_push(&e->args, nul);
                    }
                pack_rest(e, ft);
                Func *af = b->origin && b->origin->arrow_fn ? b->origin->arrow_fn : b->arrow_fn;
                if (af && af->nparams == ft->nparams) /* a const arrow's literal defaults fill the arguments left out */
                    while (e->args.len < ft->nparams) {
                        Expr *d = af->params[e->args.len].dflt;
                        bool lit = d && (d->kind == E_NUMBER || d->kind == E_STRING || d->kind == E_BOOL || d->kind == E_NULL);
                        bool made = d && (d->kind == E_NEW_OBJ || ((d->kind == E_OBJLIT || d->kind == E_ARRAY_LIT) && d->args.len == 0)); /* (agora = new Date(), binds = {}): made again at each call, as in JS */
                        for (int k = 0; made && k < d->args.len; k++) {
                            Expr *a = d->args.items[k];
                            made = a->kind == E_NUMBER || a->kind == E_STRING || a->kind == E_BOOL || (a->kind == E_UNARY && a->lhs && a->lhs->kind == E_NUMBER);
                        }
                        /* (ilm = opts.ilm): a name or a field of one, read at the call (the arrow's own scope there) */
                        bool read = d && (d->kind == E_IDENT || (d->kind == E_MEMBER && !d->is_call && d->lhs && d->lhs->kind == E_IDENT));
                        if (!lit && !made && !read) break;
                        Expr *c = dflt_subst(d, af, NULL, 0, e->pos);
                        c->pos = e->pos;
                        expr_list_push(&e->args, c);
                    }
                if (e->args.len != ft->nparams) xs_error(XS8062, e->pos, NULL, "'%s' takes %d argument(s), got %d", name, ft->nparams, e->args.len);
                for (int i = 0; i < e->args.len; i++) {
                    check_expr(e->args.items[i], ft->params[i]);
                    expect_type(e->args.items[i], ft->params[i], xs_sprintf("argument %d of '%s'", i + 1, name));
                }
                e->type = ft->ret;
                e->gen_ret = gen_call_ret(ft, e); /* queryOne<Row>(sql): the unknown as a Row (an await around it converts what it gives) */
                if (e->gen_ret && !ft->async_ret) gen_convert(e);
                return;
            }
            if (b && missing_name(name)) xs_error_quiet();
            if (b) xs_error(XS8052, e->pos, NULL, "'%s' is a variable, not a function", name);
            if (find_class(name)) xs_error(XS8046, e->pos, xs_sprintf("write new %s(...)", name), "'%s' is a class; construct it with 'new'", name);
            {
                int n; const char **cands = visible_names(&n);
                if (missing_name(name)) xs_error_quiet();
                xs_error(XS9032, e->pos, xs_help_suggest(name, cands, n, "declare it at the top level: function name(...): type { ... }"), "unknown function '%s'", name);
            }
        }
        if (f->mod && f->mod->builtin && strcmp(f->name, "execSync") == 0 && e->args.len == 2) {
            /* execSync(cmd, { encoding: "utf8" }): xisto always returns the output as a string */
            Expr *o = e->args.items[1];
            if (o->kind != E_OBJLIT || o->args.len != 1 || strcmp(o->keys[0], "encoding") != 0 || o->args.items[0]->kind != E_STRING)
                xs_error(XS7119, o->pos, "write execSync(command, { encoding: \"utf8\" })", "unsupported execSync options");
            e->args.len = 1;
        }
        if (f->mod && f->mod->builtin && check_fs_fd_call(e, f)) return;
        if (f->mod && f->mod->builtin && strcmp(f->name, "readFileSync") == 0) {
            if (e->args.len < 1 || e->args.len > 2) xs_error(XS7084, e->pos, NULL, "readFileSync takes a path and optionally \"utf8\"");
            Expr *a0 = e->args.items[0];
            if (a0->kind == E_NUMBER && a0->is_int_lit && a0->ival == 0) { /* readFileSync(0): standard input, as in Node */
                if (e->args.len == 2) {
                    Expr *enc = e->args.items[1];
                    if (enc->kind != E_STRING || (strcmp(enc->str, "utf8") != 0 && strcmp(enc->str, "utf-8") != 0))
                        xs_error(XS7118, enc->pos, "text is read as UTF-8: readFileSync(0, \"utf8\")", "unsupported encoding");
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
                    xs_error(XS7118, enc->pos, "text is read as UTF-8: readFileSync(path, \"utf8\"); for other encodings, readFileSync(path).toString(\"base64\")", "unsupported encoding");
                e->args.len = 1;
            }
            e->func = f;
            e->type = ty_string;
            return;
        }
        if (f->ntparams) { /* a generic function: its instance for these types */
            Func *inst = check_generic_call(e, f, expected, xs_sprintf("'%s'", f->name));
            e->func = inst;
            e->type = fret(inst, e->pos);
            return;
        }
        if (e->ntargs) xs_error(XS6005, e->pos, NULL, "'%s' is not generic", f->name);
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
        ExprList ov_args = {0}; /* (the arguments as written: a rest parameter gathers them into one array) */
        if (f->noverloads) for (int i = 0; i < e->args.len; i++) expr_list_push(&ov_args, e->args.items[i]);
        { Expr *r0 = dflt_recv; dflt_recv = NULL; check_args(e, f, xs_sprintf("'%s'", f->name)); dflt_recv = r0; }
        e->func = f;
        e->type = fret(f, e->pos);
        if (f->noverloads && tsc_checked() && overload_typed(e, f, &ov_args)) return;
        if (f->mod && f->mod->builtin && strcmp(f->cname, "xs_sleep_ms") == 0) prog->uses_net = true; /* timers live with the event loop */
        return;
    }
    case E_INDEX:
        if (e->lhs->kind == E_IDENT && e->lhs->str && !lookup(e->lhs->str) && find_enum(e->lhs->str) && !find_enum(e->lhs->str)->is_string) {
            /* TipoProtocolo[s as keyof typeof TipoProtocolo] with s a string: the member of that name's value, undefined
               when there is none (s === 'A' ? 1 : s === 'B' ? 2 : undefined) */
            Expr *k = e->rhs;
            while (k->kind == E_AS && k->lhs) k = k->lhs;
            Expr *probe = pure_copy(k);
            if (probe) {
                check_expr(probe, NULL);
                if (probe->type->kind == TY_STRING) {
                    Enum *en = find_enum(e->lhs->str);
                    Expr *chain = xs_alloc(sizeof *chain);
                    chain->kind = E_NULL; chain->pos = e->pos; chain->field = -1;
                    for (int i = en->nmembers - 1; i >= 0; i--) {
                        Expr *eq = xs_alloc(sizeof *eq);
                        eq->kind = E_BINARY; eq->pos = e->pos; eq->field = -1; eq->op = TK_EQ; eq->lhs = pure_copy(k);
                        eq->rhs = str_lit(e->pos, en->members[i].name);
                        Expr *cd = xs_alloc(sizeof *cd);
                        cd->kind = E_COND; cd->pos = e->pos; cd->field = -1; cd->lhs = eq;
                        expr_list_push(&cd->args, num_lit(e->pos, (double)en->members[i].ival, false));
                        expr_list_push(&cd->args, chain);
                        chain = cd;
                    }
                    *e = *chain;
                    check_expr(e, ty_nullable_of(ty_number));
                    return;
                }
                if (is_numeric(probe->type) || probe->type->kind == TY_LITINT || (probe->type->kind == TY_ENUM && !probe->type->en->is_string)) { /* T[2]: the name of the member with that
                                                                                     value (the reverse mapping), undefined if none */
                    Enum *en = find_enum(e->lhs->str);
                    Expr *chain = xs_alloc(sizeof *chain);
                    chain->kind = E_NULL; chain->pos = e->pos; chain->field = -1;
                    for (int i = en->nmembers - 1; i >= 0; i--) {
                        bool dup = false; /* (the last member of a value names it, as JavaScript's object is written in order) */
                        for (int j = i + 1; j < en->nmembers; j++) if (en->members[j].ival == en->members[i].ival) dup = true;
                        if (dup) continue;
                        Expr *eq = xs_alloc(sizeof *eq);
                        eq->kind = E_BINARY; eq->pos = e->pos; eq->field = -1; eq->op = TK_EQ; eq->lhs = pure_copy(k);
                        eq->rhs = num_lit(e->pos, (double)en->members[i].ival, true);
                        if (probe->type->kind == TY_ENUM) { /* (T[v] with v: T: compared as T's values) */
                            Expr *en_id = xs_alloc(sizeof *en_id);
                            en_id->kind = E_IDENT; en_id->pos = e->pos; en_id->field = -1; en_id->str = en->name;
                            Expr *mv = xs_alloc(sizeof *mv);
                            mv->kind = E_MEMBER; mv->pos = e->pos; mv->field = -1; mv->lhs = en_id; mv->str = en->members[i].name;
                            eq->rhs = mv;
                        }
                        Expr *cd = xs_alloc(sizeof *cd);
                        cd->kind = E_COND; cd->pos = e->pos; cd->field = -1; cd->lhs = eq;
                        expr_list_push(&cd->args, str_lit(e->pos, en->members[i].name));
                        expr_list_push(&cd->args, chain);
                        chain = cd;
                    }
                    *e = *chain;
                    check_expr(e, ty_optional_of(ty_string));
                    return;
                }
            }
        }
        check_expr(e->lhs, NULL);
        if (e->lhs->type->kind == TY_JSON || (e->lhs->type->kind == TY_NULLABLE && e->lhs->type->elem->kind == TY_JSON)) {
            /* rows[0] on an object (TypeORM's query() rows, typed any there): read as an any, by name */
            xs_note_dynamic(e->pos);
            wrap_call(e->lhs, "xs_json_any", ty_any);
        }
        if (ty_is_any(e->lhs->type)) { /* x[k] on an any: a field or key by name, an element by index */
            check_expr(e->rhs, NULL);
            Expr *k = e->rhs;
            if (k->type->kind == TY_LITINT) coerce_lit(k, ty_int); else settle(k);
            if (k->type->kind == TY_STRING) dyn_call(e, "xs_dyn_get", ty_any, e->lhs, k, NULL);
            else if (k->type->kind == TY_INT) dyn_call(e, "xs_dyn_at", ty_any, e->lhs, k, NULL);
            else if (ty_is_any(k->type)) dyn_call(e, "xs_dyn_get_any", ty_any, e->lhs, k, NULL);
            else xs_error(XS8091, k->pos, NULL, "an any is indexed by a string or an int, not %s", ty_name(k->type));
            return;
        }
        if (e->lhs->type->kind == TY_NULLABLE && e->lhs->type->elem->kind == TY_MAP && tsc_checked() &&
            (e->lhs->type->elem->record || e->lhs->type->elem->key->kind == TY_STRING))
            nonnull_checked(e->lhs, "the indexed value"); /* ans?.['X'] ? String(ans['X']) : ...: TypeScript knows ans is set there */
        if (e->lhs->type->kind == TY_MAP && (e->lhs->type->record || e->lhs->type->key->kind == TY_STRING)) { /* m[k] on a Record: m.get(k)
            (a string-keyed map read with [] is a Record TypeScript passed along: row?.['NOME']) */
            Expr *key = e->rhs;
            e->kind = E_MEMBER;
            e->str = "get";
            e->is_call = true;
            e->rhs = NULL;
            e->args = (ExprList){0};
            expr_list_push(&e->args, key);
            check_expr(e, expected);
            return;
        }
        if (ty_strip_null(e->lhs->type)->kind == TY_OBJECT && !ty_strip_null(e->lhs->type)->cls->builtin && e->rhs->kind == E_IDENT) {
            /* req[AUDIT_CONTEXT_KEY] with const AUDIT_CONTEXT_KEY = '__audit': the field of that name */
            const char *val = const_string_of(e->rhs->str);
            Class *oc = ty_strip_null(e->lhs->type)->cls;
            if (val && find_field(oc, val, NULL)) {
                e->kind = E_MEMBER;
                e->str = (char *)val;
                e->rhs = NULL;
                check_expr(e, expected);
                return;
            }
        }
        if (e->lhs->type->kind == TY_OBJECT && !e->lhs->type->cls->is_tuple && !e->lhs->type->cls->builtin && e != dyn_lvalue &&
            (e->rhs->kind == E_IDENT || (e->rhs->kind == E_MEMBER && !e->rhs->is_call) || e->rhs->kind == E_AS)) {
            check_expr(e->rhs, NULL);
            if (e->rhs->kind == E_AS) e->rhs->prechecked = true; /* (o[k as keyof typeof o]; checked once) */
            if (e->rhs->type->kind == TY_STRING) { /* o[k] with k: keyof O (a string): the field named k, by name (the dynamic path) */
                xs_note_dynamic(e->pos);
                to_any(e->lhs);
                dyn_call(e, "xs_dyn_get", ty_any, e->lhs, e->rhs, NULL);
                return;
            }
        }
        if (e->lhs->type->kind == TY_OBJECT && !e->lhs->type->cls->is_tuple && !e->lhs->type->cls->builtin && e->rhs->kind == E_STRING) {
            e->kind = E_MEMBER; /* o['@timestamp']: the field of that name */
            e->str = e->rhs->str;
            e->rhs = NULL;
            e->is_call = false;
            check_expr(e, expected);
            return;
        }
        if (e->lhs->type->kind == TY_NULLABLE) {
            if (!tsc_checked()) xs_error(XS2015, e->pos, "check it first: if (x !== null) { ... }", "indexed value may be null");
            nonnull_checked(e->lhs, "the indexed value");
        }
        check_expr(e->rhs, ty_int);
        if (e->lhs->type->kind == TY_OBJECT && e->lhs->type->cls->is_tuple) { /* t[0]: its field (the index is a constant) */
            Class *tc = e->lhs->type->cls;
            long long k;
            if (!lit_int_of(e->rhs, &k)) xs_error(XS8137, e->rhs->pos, "a tuple's elements have different types: index it with a number written in place, t[0]", "the index of a tuple must be a constant");
            if (k < 0 || k >= tc->nfields) xs_error(XS8107, e->rhs->pos, NULL, "index %lld is out of the tuple %s (length %d)", k, tc->name, tc->nfields);
            e->kind = E_MEMBER;
            e->str = tc->fields[k].name;
            e->rhs = NULL;
            e->is_call = false;
            e->field = (int)k;
            e->cls = tc;
            e->type = tc->fields[k].type;
            return;
        }
        if (e->lhs->type->kind == TY_STRING && tsc_checked()) { /* s[i] with --tsc-checked: s.charAt(i) ('' past the end, where JavaScript gives undefined) */
            Expr *ix = e->rhs;
            if (ix->type) ix->prechecked = true;
            e->kind = E_MEMBER; e->str = "charAt"; e->is_call = true; e->rhs = NULL; e->field = -1;
            memset(&e->args, 0, sizeof e->args);
            expr_list_push(&e->args, ix);
            e->prechecked = false;
            Expr *obj = e->lhs;
            obj->prechecked = true; /* (checked above) */
            check_expr(e, NULL);
            return;
        }
        if (e->lhs->type->kind == TY_STRING)
            xs_error(XS6033, e->pos, "use s.slice(i, i + 1)", "strings cannot be indexed in xisto-2");
        if (is_bytes(e->lhs->type)) { /* b[i]: a byte, as an int (bounds checked) */
            expect_type(e->rhs, ty_int, "byte index");
            e->builtin.cfn = "xs_u8_get";
            e->type = ty_int;
            return;
        }
        if (is_f64(e->lhs->type)) { /* f[i]: a number, read and written in place (bounds checked) */
            if (e->rhs->type->kind == TY_NUMBER)
                xs_error(XS7020, e->rhs->pos, "declare the index as int, or convert it with Math.trunc(x)", "Float64Array index must be int, but this is number");
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
        if (e->lhs->type->kind != TY_ARRAY) xs_error(XS8091, e->pos, NULL, "cannot index a value of type %s", ty_name(e->lhs->type));
        if (e->rhs->type->kind == TY_NUMBER && tsc_checked()) { /* xs[i] with i: number (--tsc-checked): a whole number, checked */
            xs_warn_rule(XS2109, e->rhs->pos, "declare the index as int, or convert it with Math.trunc(x)", "array index is a number where an int goes: checked to be whole at run time");
            wrap_call(e->rhs, "xs_num_key", ty_int);
        }
        if (e->rhs->type->kind == TY_NUMBER)
            xs_error(XS8082, e->rhs->pos, "declare the index as int, or convert it with Math.trunc(x)", "array index must be int, but this is number");
        expect_type(e->rhs, ty_int, "array index");
        if (ty_is_any(e->lhs->type->elem) && e != dyn_lvalue) { dyn_call(e, "xs_dyn_at", ty_any, e->lhs, e->rhs, NULL); return; } /* rows[i] of an any[]: null past the end, as undefined */
        e->type = e->lhs->type->elem;
        return;
    case E_MEMBER:
        if (!e->is_call && e->str && !strcmp(e->str, "name") && e->lhs && e->lhs->kind == E_MEMBER && !e->lhs->is_call &&
            e->lhs->str && !strcmp(e->lhs->str, "constructor") && e->lhs->op != TK_QDOT && pure_copy(e->lhs->lhs)) {
            /* e.constructor.name: the name of the object's class, chosen among the classes it can be by instanceof (the
               deepest first); an Error that is none of the program's subclasses gives its name (TypeError, ...) */
            Expr *obj = e->lhs->lhs, *probe = pure_copy(obj);
            check_expr(probe, NULL);
            Type *ot = probe->type;
            Class *c = ot->kind == TY_OBJECT ? ot->cls : NULL;
            if (c && !c->is_interface && !made_shape(c) && !inline_cls(ot)) {
                bool err = c->builtin && strcmp(c->name, "Error") == 0;
                Expr *chain;
                if (err) {
                    chain = xs_alloc(sizeof *chain);
                    chain->kind = E_MEMBER; chain->pos = e->pos; chain->field = -1; chain->lhs = pure_copy(obj); chain->str = "name";
                } else chain = str_lit(e->pos, c->name);
                for (int depth = 1; depth < 64; depth++) {
                    bool any_at = false;
                    for (int i = 0; i < prog->nclasses; i++) {
                        Class *k = prog->classes[i];
                        int d = 0;
                        for (Class *u = k; u && u != c; u = u->base) d++;
                        if (k == c || k->builtin || k->is_interface || !(err ? is_error_class(k) : is_subclass(k, c)) || find_class(k->name) != k) continue;
                        if (d != depth) { any_at |= d > depth; continue; }
                        any_at = true;
                        Expr *id = xs_alloc(sizeof *id);
                        id->kind = E_IDENT; id->pos = e->pos; id->field = -1; id->str = k->name;
                        Expr *io = xs_alloc(sizeof *io);
                        io->kind = E_BINARY; io->pos = e->pos; io->field = -1; io->op = TK_IDENT; io->lhs = pure_copy(obj); io->rhs = id;
                        Expr *cd = xs_alloc(sizeof *cd);
                        cd->kind = E_COND; cd->pos = e->pos; cd->field = -1; cd->lhs = io;
                        expr_list_push(&cd->args, str_lit(e->pos, k->name));
                        expr_list_push(&cd->args, chain);
                        chain = cd;
                    }
                    if (!any_at) break;
                }
                *e = *chain;
                check_expr(e, expected);
                return;
            }
        }
        if (!e->is_call && e->str && e->lhs && e->lhs->kind == E_AS && e->lhs->elem_type && e->lhs->op != TK_NOT &&
            e->lhs->lhs && e->lhs->lhs->kind == E_IDENT && e->lhs->lhs->str) {
            /* (data as { toJSON?: unknown }).toJSON with data unknown or any: a probe of the shape (TypeScript's as checks
               nothing; the code asks whether data has the field). Read by name, as an any does (undefined when data has
               no such field), then as the type the shape gives it: ((data as any).toJSON as unknown) */
            Binding *ub = lookup(e->lhs->lhs->str);
            Type *want = ub && ub->type && ub->type->kind == TY_UNKNOWN ? resolve(e->lhs->elem_type, e->pos) : NULL;
            Type *w = want && want->kind == TY_NULLABLE ? want->elem : want;
            int fi = -1;
            if (w && w->kind == TY_OBJECT && made_shape(w->cls))
                for (int k = 0; k < w->cls->nfields; k++) if (strcmp(w->cls->fields[k].name, e->str) == 0) fi = k;
            if (fi >= 0) {
                Expr *m = xs_alloc(sizeof *m);
                *m = *e;
                m->lhs->elem_type = ty_any;
                Pos at = e->pos;
                memset(e, 0, sizeof *e);
                e->kind = E_AS; e->pos = at; e->field = -1; e->lhs = m;
                e->elem_type = w->cls->fields[fi].type;
                check_expr(e, expected);
                return;
            }
        }
        if (e->is_call && e->str && !strcmp(e->str, "call") && e->args.len == 2 && e->lhs && e->lhs->kind == E_MEMBER && !e->lhs->is_call &&
            e->lhs->str && !strcmp(e->lhs->str, "hasOwnProperty") && e->lhs->lhs && e->lhs->lhs->kind == E_MEMBER && e->lhs->lhs->str &&
            !strcmp(e->lhs->lhs->str, "prototype") && e->lhs->lhs->lhs && e->lhs->lhs->lhs->kind == E_IDENT &&
            !strcmp(e->lhs->lhs->lhs->str, "Object") && !lookup("Object")) {
            /* Object.prototype.hasOwnProperty.call(o, k): k in o (a Record's key, a field an object has) */
            Expr *o = e->args.items[0], *k = e->args.items[1];
            Pos at = e->pos;
            memset(e, 0, sizeof *e);
            e->kind = E_BINARY; e->op = TK_IN; e->pos = at; e->field = -1; e->lhs = k; e->rhs = o;
            check_expr(e, expected);
            return;
        }
        if (e->is_call && e->str && e->lhs && e->lhs->kind == E_IDENT && strcmp(e->lhs->str, "Object") == 0 && !lookup("Object") &&
            !strcmp(e->str, "assign") && e->args.len == 2 && e->args.items[1]->kind == E_OBJLIT && !objlit_has_spread(e->args.items[1]) &&
            fresh_path(e->args.items[0])) {
            /* Object.assign(patch, { [field]: v, x: 1 }): each entry written into the target (patch[field] = v), in order */
            Expr *tg = e->args.items[0], *src = e->args.items[1];
            Expr *seq = xs_alloc(sizeof *seq);
            seq->kind = E_SEQ; seq->pos = e->pos; seq->field = -1;
            for (int i = 0; i < src->args.len; i++) {
                Expr *a = src->args.items[i], *lv;
                if (strcmp(src->keys[i], "[]") == 0) {
                    lv = xs_alloc(sizeof *lv); lv->kind = E_INDEX; lv->pos = a->pos; lv->field = -1; lv->lhs = fresh_path(tg); lv->rhs = a->lhs;
                    a = a->rhs;
                } else {
                    lv = xs_alloc(sizeof *lv); lv->kind = E_MEMBER; lv->pos = a->pos; lv->field = -1; lv->lhs = fresh_path(tg); lv->str = src->keys[i];
                }
                Expr *as = xs_alloc(sizeof *as); as->kind = E_ASSIGN; as->op = TK_ASSIGN; as->pos = a->pos; as->field = -1; as->lhs = lv; as->rhs = a;
                check_expr(as, NULL);
                expr_list_push(&seq->args, as);
            }
            Expr *back = fresh_path(tg);
            check_expr(back, NULL);
            expr_list_push(&seq->args, back);
            seq->type = back->type;
            *e = *seq;
            return;
        }
        if (e->is_call && e->str && e->lhs && e->lhs->kind == E_IDENT && strcmp(e->lhs->str, "Object") == 0 && !lookup("Object") &&
            !strcmp(e->str, "assign") && e->args.len == 2 && rereadable(e->args.items[0]) && rereadable(e->args.items[1])) {
            /* Object.assign(this, partial): each field of the target's class that the source has, copied (target.f = src.f) */
            Expr *tg = e->args.items[0], *src = e->args.items[1];
            check_expr(tg, NULL);
            check_expr(src, NULL);
            Type *tt = ty_strip_null(tg->type), *st = ty_strip_null(src->type);
            if (tt->kind == TY_OBJECT && !tt->cls->builtin && st->kind == TY_OBJECT && !st->cls->builtin && src->type->kind != TY_NULLABLE) {
                Expr *seq = xs_alloc(sizeof *seq);
                seq->kind = E_SEQ; seq->pos = e->pos; seq->field = -1;
                for (int i = 0; i < tt->cls->nfields; i++) {
                    Field *fd = &tt->cls->fields[i];
                    if (!find_field(st->cls, fd->name, NULL)) continue;
                    Expr *l = fresh_path(tg), *r = fresh_path(src);
                    if (!l || !r) break;
                    Expr *lm = xs_alloc(sizeof *lm); lm->kind = E_MEMBER; lm->pos = e->pos; lm->field = -1; lm->lhs = l; lm->str = fd->name;
                    Expr *rm = xs_alloc(sizeof *rm); rm->kind = E_MEMBER; rm->pos = e->pos; rm->field = -1; rm->lhs = r; rm->str = fd->name;
                    Expr *as = xs_alloc(sizeof *as); as->kind = E_ASSIGN; as->op = TK_ASSIGN; as->pos = e->pos; as->field = -1; as->lhs = lm; as->rhs = rm;
                    check_expr(as, NULL);
                    expr_list_push(&seq->args, as);
                }
                Expr *back = fresh_path(tg);
                check_expr(back, NULL);
                expr_list_push(&seq->args, back);
                seq->type = back->type;
                *e = *seq;
                return;
            }
        }
        if (e->is_call && e->str && e->lhs && e->lhs->kind == E_IDENT && strcmp(e->lhs->str, "Object") == 0 && !lookup("Object") &&
            !strcmp(e->str, "fromEntries") && e->args.len == 1) {
            /* Object.fromEntries(pairs): a Record<string, V> of the [key, value] pairs, as new Map(pairs) read as an object */
            Type *t = expected ? ty_strip_null(expected) : NULL;
            Type *rec = t && t->kind == TY_MAP && t->record && t->key->kind == TY_STRING ? t : NULL;
            Expr *pairs = e->args.items[0];
            if (!rec) {
                check_expr(pairs, NULL);
                Type *pt = pairs->type;
                if (pt->kind == TY_MAP && !pt->record && pt->key->kind == TY_STRING) { /* Object.fromEntries(map): its entries */
                    Type **ps = xs_alloc(sizeof(Type *) * 2);
                    ps[0] = ty_string; ps[1] = pt->elem;
                    pt = ty_array_of(tuple_type(ps, 2, e->pos));
                }
                Type *el = pt->kind == TY_ARRAY ? pt->elem : NULL;
                if (!(el && el->kind == TY_OBJECT && el->cls->is_tuple && el->cls->nfields == 2 && el->cls->fields[0].type->kind == TY_STRING))
                    xs_error(XS8015, e->pos, "Object.fromEntries takes [string, V] pairs: Object.fromEntries(xs.map((x) => [x.k, x.v] as [string, V]))",
                             "Object.fromEntries of %s is not supported", ty_name(pt));
                Type *v = el->cls->fields[1].type;
                if (v->kind == TY_INT || v->kind == TY_LITINT) v = ty_number;
                rec = xs_alloc(sizeof *rec);
                *rec = *ty_map_of(ty_string, v);
                rec->record = true;
            }
            e->kind = E_NEW_MAP;
            e->elem_type = rec;
            e->lhs = NULL;
            e->str = NULL;
            e->is_call = false;
            check_expr(e, rec);
            e->type = rec;
            return;
        }
        if (e->is_call && e->str && e->lhs && e->lhs->kind == E_IDENT && strcmp(e->lhs->str, "Object") == 0 && !lookup("Object") &&
            (!strcmp(e->str, "keys") || !strcmp(e->str, "values") || !strcmp(e->str, "entries")) && e->args.len == 1) {
            /* Object.keys(m) / values / entries: of a Record, its entries' (m.keys()...); of an object, its fields' names */
            Expr *o = e->args.items[0];
            check_expr(o, NULL);
            Type *ot = o->type;
            if (ot->kind == TY_MAP && ot->record) {
                e->lhs = o;
                e->args = (ExprList){0};
                check_expr(e, expected);
                return;
            }
            if (ot->kind == TY_JSON) { /* Object.entries(patch) with patch: object (a JSON value here): read as an any */
                xs_note_dynamic(o->pos);
                wrap_call(o, "xs_json_any", ty_any);
                ot = o->type;
            }
            if (ty_is_any(ot)) { /* Object.entries(x) of an any: x as Record<string, unknown> (checked to be one at run time) */
                Expr *as = xs_alloc(sizeof *as);
                as->kind = E_AS; as->pos = o->pos; as->field = -1; as->lhs = o;
                Type *rec = xs_alloc(sizeof *rec);
                *rec = *ty_map_of(ty_string, ty_unknown);
                rec->record = true;
                as->elem_type = rec;
                o->prechecked = true;
                check_expr(as, NULL);
                e->lhs = as;
                e->args = (ExprList){0};
                check_expr(e, expected);
                return;
            }
            if (ot->kind == TY_UNKNOWN && !ot->tname) { /* Object.keys(v) of an unknown (an object, tested by typeof): v as Record<string,
                                                         unknown>, its fields read by name (as `as` of an unknown makes it) */
                Expr *as = xs_alloc(sizeof *as);
                as->kind = E_AS; as->pos = o->pos; as->field = -1; as->lhs = o;
                Type *rec = xs_alloc(sizeof *rec);
                *rec = *ty_map_of(ty_string, ty_unknown);
                rec->record = true;
                as->elem_type = rec;
                check_expr(as, NULL);
                e->lhs = as;
                e->args = (ExprList){0};
                check_expr(e, expected);
                return;
            }
            Type *st = ty_strip_null(ot);
            if (st->kind == TY_OBJECT && !st->cls->builtin && ot->kind != TY_NULLABLE && !st->cls->is_tuple && pure_read(o)) {
                /* Object.keys/values/entries of an object with optional fields (or its values): of a Record of the fields
                   it has (an optional one left out is not there), read through its class's field table */
                bool opt = false;
                for (int i = 0; i < st->cls->nfields; i++) if (st->cls->fields[i].optional) opt = true;
                if (opt || strcmp(e->str, "keys") != 0) {
                    Expr *u = expr_copy(o);
                    to_unknown(u);
                    Expr *as = xs_alloc(sizeof *as);
                    as->kind = E_AS; as->pos = o->pos; as->field = -1; as->lhs = u;
                    Type *rec = xs_alloc(sizeof *rec);
                    *rec = *ty_map_of(ty_string, ty_unknown);
                    rec->record = true;
                    as->elem_type = rec;
                    u->prechecked = true;
                    check_expr(as, NULL);
                    e->lhs = as;
                    e->args = (ExprList){0};
                    check_expr(e, expected);
                    return;
                }
            }
            if (!strcmp(e->str, "keys") && st->kind == TY_OBJECT && !st->cls->builtin) {
                Class *c = st->cls;
                bool opt = false;
                for (int i = 0; i < c->nfields; i++) if (c->fields[i].optional) opt = true;
                if (opt) xs_error(XS8015, e->pos, "keep the keys in a Record<string, V>, or list the fields you need", "Object.keys of %s, which has optional fields, is not supported yet", c->name);
                e->kind = E_ARRAY_LIT;
                e->lhs = NULL;
                e->str = NULL;
                e->is_call = false;
                e->args = (ExprList){0};
                for (int i = 0; i < c->nfields; i++) {
                    Expr *k = xs_alloc(sizeof *k);
                    k->kind = E_STRING; k->pos = e->pos; k->field = -1; k->str = (char *)c->fields[i].name;
                    expr_list_push(&e->args, k);
                }
                check_expr(e, ty_array_of(ty_string));
                return;
            }
            xs_error(XS8015, e->pos, "Object.keys, values and entries take a Record<string, V> (and keys, an object)", "Object.%s of %s is not supported", e->str, ty_name(ot));
        }
        if (e->is_call && e->str && strcmp(e->str, "write") == 0 && e->lhs->kind == E_MEMBER && !e->lhs->is_call && e->lhs->str &&
            (strcmp(e->lhs->str, "stdout") == 0 || strcmp(e->lhs->str, "stderr") == 0) && e->lhs->lhs->kind == E_IDENT &&
            strcmp(e->lhs->lhs->str, "process") == 0 && !lookup("process")) { /* process.stdout.write(text or bytes) */
            bool err = strcmp(e->lhs->str, "stderr") == 0;
            if (e->args.len != 1) xs_error(XS7080, e->pos, NULL, "process.%s.write takes one argument (a string or a Uint8Array)", e->lhs->str);
            check_expr(e->args.items[0], NULL);
            Type *t = e->args.items[0]->type;
            if (t->kind != TY_STRING && !is_bytes(t)) xs_error(XS7081, e->args.items[0]->pos, NULL, "process.%s.write writes a string or a Uint8Array, not %s", e->lhs->str, ty_name(t));
            e->kind = E_CALL;
            e->lhs = NULL;
            e->is_call = false;
            e->builtin.cfn = t->kind == TY_STRING ? (err ? "xs_stderr_write_str" : "xs_stdout_write_str") : (err ? "xs_stderr_write_bytes" : "xs_stdout_write_bytes");
            if (!err && t->kind != TY_STRING && cur_func && cur_func->mod && cur_func->mod->file && strcmp(cur_func->mod->file, "xisto:io") == 0 && cur_func->name &&
                strcmp(cur_func->name, "writeStdoutLent") == 0)
                e->builtin.cfn = "xs_stdout_write_lent"; /* (xisto:io's writeStdoutLent: the part, with no copy) */
            e->type = ty_boolean;
            return;
        }
        if (e->lhs->kind == E_IDENT && strcmp(e->lhs->str, "super") == 0 && !lookup("super")) { /* super.m(...) */
            Class *cc = cur_func ? cur_func->cls : NULL;
            if (!cc || !cc->base) xs_error(XS8124, e->pos, NULL, "super is used inside a class that extends another");
            if (!e->is_call || !find_method(cc->base, e->str))
                xs_error(XS8014, e->pos, xs_sprintf("read fields with this.%s", e->str), "%s has no method '%s'", cc->base->name, e->str);
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
                    if (!e->is_call) xs_error(XS8123, e->pos, "call it: Class.method(...)", "static methods are not values");
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
                if (e->is_call) xs_error(XS8051, e->pos, NULL, "'%s' is a static field of %s, not a method", e->str, sc->name);
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
                if (!e->is_call || strcmp(e->str, "now") != 0 || e->args.len != 0) xs_error(XS7114, e->pos, "supported: performance.now()", "unknown performance member '%s'", e->str);
                e->type = ty_number;
                e->builtin.cfn = "xs_performance_now";
                return;
            }
            if (strcmp(ns, "Date") == 0) {
                if (e->is_call && strcmp(e->str, "parse") == 0) {
                    if (e->args.len != 1) xs_error(XS7016, e->pos, NULL, "Date.parse takes one string");
                    check_expr(e->args.items[0], ty_string);
                    expect_type(e->args.items[0], ty_string, "argument of Date.parse");
                    e->type = ty_number;
                    e->builtin.cfn = "xs_Date_parse";
                    return;
                }
                if (e->is_call && strcmp(e->str, "UTC") == 0) {
                    int n = e->args.len;
                    if (n < 1 || n > 7) xs_error(XS7015, e->pos, NULL, "Date.UTC takes 1 to 7 arguments");
                    for (int k = 0; k < n; k++) date_num_arg(&e->args.items[k], xs_sprintf("argument %d of Date.UTC", k + 1));
                    for (int k = n; k < 7; k++) expr_list_push(&e->args, num_lit(e->pos, k == 2 ? 1 : 0, false));
                    e->type = ty_number;
                    e->builtin.cfn = "xs_Date_UTC";
                    return;
                }
                if (!e->is_call && strcmp(e->str, "now") == 0) { /* default: Date.now (the function as a value): () => Date.now() */
                    Expr *call = xs_alloc(sizeof *call);
                    *call = *e;
                    call->is_call = true;
                    call->lhs = xs_alloc(sizeof *call->lhs);
                    *call->lhs = *e->lhs;
                    Expr *ar = thunk_of(call);
                    ar->arrow->fn->ret = ty_number;
                    *e = *ar;
                    check_expr(e, expected);
                    return;
                }
                if (!e->is_call || strcmp(e->str, "now") != 0 || e->args.len != 0) xs_error(XS7106, e->pos, "supported: Date.now(), Date.parse(text), Date.UTC(y, m, ...)", "unknown Date member '%s'", e->str);
                e->type = ty_number;
                e->builtin.cfn = "xs_date_now";
                return;
            }
            if (strcmp(ns, "process") == 0) {
                if (!e->is_call && strcmp(e->str, "argv") == 0) { e->type = ty_array_of(ty_string); e->builtin.cfn = "xs_process_argv"; return; }
                if (!e->is_call && strcmp(e->str, "env") == 0) { /* a Record<string, string>: process.env[name] reads as get */
                    static Type *env_t;
                    if (!env_t) { env_t = xs_alloc(sizeof *env_t); *env_t = *ty_map_of(ty_string, ty_string); env_t->record = true; }
                    e->type = env_t; e->builtin.cfn = "xs_process_env"; return;
                }
                if (!e->is_call && strcmp(e->str, "pid") == 0) { e->type = ty_int; e->builtin.cfn = "xs_process_pid"; return; }
                if (e->is_call && strcmp(e->str, "cwd") == 0 && e->args.len == 0) { e->type = ty_string; e->builtin.cfn = "xs_process_cwd"; return; }
                if (e->is_call && strcmp(e->str, "exit") == 0) {
                    if (e->args.len > 1) xs_error(XS7082, e->pos, NULL, "process.exit takes at most one argument");
                    if (e->args.len == 1) { check_expr(e->args.items[0], ty_int); expect_type(e->args.items[0], ty_int, "exit code"); }
                    e->type = ty_void;
                    e->builtin.cfn = "xs_process_exit";
                    return;
                }
                xs_error(XS7115, e->pos, "supported: process.argv, process.env.get(name), process.cwd(), process.exit(code), process.stdout.write(x), process.stderr.write(x)", "unknown process member '%s'", e->str);
            }
            if (strcmp(ns, "Array") == 0 && e->is_call && (strcmp(e->str, "from") == 0 || strcmp(e->str, "isArray") == 0)) {
                if (e->str[0] == 'f' && e->args.len == 2) {
                    Expr *src = e->args.items[0], *fn = e->args.items[1];
                    Expr *c = xs_alloc(sizeof *c);
                    c->kind = E_CALL; c->pos = e->pos; c->field = -1;
                    Expr *callee = xs_alloc(sizeof *callee);
                    callee->kind = E_IDENT; callee->pos = e->pos; callee->field = -1;
                    c->lhs = callee;
                    if (src->kind == E_OBJLIT && src->args.len == 1 && strcmp(src->keys[0], "length") == 0) {
                        /* Array.from({ length: n }, (_, i) => ...): n results of the function, as JavaScript makes them */
                        int np = 2;
                        bool async_fn = false; /* Array.from({ length: n }, worker) with an async worker: n promises (Promise.all of them) */
                        if (fn->kind == E_ARROW) {
                            np = fn->arrow->fn->nparams;
                            Func *af = fn->arrow->fn;
                            if (af->is_async) async_fn = true;
                            else if (fn->arrow->expr_body && af->body && af->body->stmts.len) {
                                Expr *r = af->body->stmts.items[af->body->stmts.len - 1]->expr;
                                if (r && r->kind == E_CALL && r->lhs && r->lhs->kind == E_IDENT) {
                                    Func *cf = lookup_func(r->lhs->str);
                                    Binding *cb = cf ? NULL : lookup(r->lhs->str);
                                    if ((cf && (cf->is_async || cf->ret_promise)) || (cb && cb->type && cb->type->kind == TY_FUNC && cb->type->async_ret)) async_fn = true;
                                }
                            }
                        } else if (fn->kind == E_IDENT) { check_expr(fn, NULL); if (fn->type->kind == TY_FUNC) { np = fn->type->nparams; async_fn = fn->type->async_ret; } } /* (a name: checked twice, harmless) */
                        callee->str = async_fn ? (np == 0 ? "__xs_fromLengthP0" : "__xs_fromLengthP") : np == 0 ? "__xs_fromLength0" : "__xs_fromLength";
                        expr_list_push(&c->args, src->args.items[0]); /* (checked as the call's int argument) */
                        expr_list_push(&c->args, fn);
                        *e = *c;
                        check_expr(e, expected);
                        return;
                    }
                    /* Array.from(xs, f): Array.from(xs).map(f) */
                    Expr *inner = xs_alloc(sizeof *inner);
                    *inner = *e;
                    inner->args = (ExprList){0};
                    expr_list_push(&inner->args, src);
                    memset(&e->args, 0, sizeof e->args);
                    expr_list_push(&e->args, fn);
                    e->lhs = inner;
                    e->str = "map";
                    e->field = -1;
                    check_expr(e, expected);
                    return;
                }
                if (e->args.len != 1) xs_error(XS7001, e->pos, "Array.from(array, Set or Map); Array.isArray(x)", "Array.%s takes one argument here", e->str);
                Expr *src = e->args.items[0];
                check_expr(src, expected && strcmp(e->str, "from") == 0 ? expected : NULL);
                Type *st = src->type;
                if (e->str[0] == 'i' && (st->kind == TY_UNKNOWN || st->kind == TY_UNION || (st->kind == TY_NULLABLE && st->elem->kind == TY_UNION))) {
                    /* Array.isArray(v) with v: unknown (or any, or a union): what v holds, at run time */
                    e->kind = E_CALL; e->lhs = NULL; e->field = -1; e->type = ty_boolean;
                    e->builtin.cfn = "xs_is_array";
                    return;
                }
                if (e->str[0] == 'i' && st->kind == TY_NULLABLE && st->elem->kind == TY_ARRAY) {
                    /* Array.isArray(rows) with rows: T[] | null: rows !== null (and it narrows like that) */
                    Expr *z = xs_alloc(sizeof *z);
                    z->kind = E_NULL; z->pos = e->pos; z->field = -1;
                    memset(e, 0, sizeof *e);
                    z->type = st;
                    e->kind = E_BINARY; e->op = TK_NEQ; e->pos = src->pos; e->field = -1; e->lhs = src; e->rhs = z;
                    e->type = ty_boolean; /* (src is checked: built as checked) */
                    return;
                }
                if (e->str[0] == 'i') { /* the type says it */
                    bool arr = st->kind == TY_ARRAY;
                    e->kind = E_BOOL; e->boolean = arr; e->type = ty_boolean; e->lhs = NULL; e->args.len = 0; e->field = -1;
                    return;
                }
                /* a copy of an array; a Set's values or a Map's entries (already arrays of their own) */
                const char *m = st->kind == TY_ARRAY ? "slice" : st->kind == TY_SET ? "values" : st->kind == TY_MAP ? "entries" : st->kind == TY_STRING ? "split" : NULL;
                if (!m) xs_error(XS8093, src->pos, "Array.from takes an array, a Set, a Map or a string", "cannot make an array from %s", ty_name(st));
                e->lhs = src;
                e->str = (char *)m;
                e->args.len = 0;
                if (st->kind == TY_STRING) { /* Array.from(s): its characters (code points), as s.split("") gives them in xisto */
                    Expr *empty = xs_alloc(sizeof *empty);
                    empty->kind = E_STRING; empty->pos = e->pos; empty->field = -1; empty->str = ""; empty->type = ty_string; empty->lit_id = intern("");
                    expr_list_push(&e->args, empty);
                }
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
                    xs_error(XS7110, e->pos, help, "unknown Number member '%s'", e->str);
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
                if (!fn) xs_error(XS7110, e->pos, help, "unknown Number member '%s'", e->str);
                if (e->args.len != 1) xs_error(XS7029, e->pos, NULL, "Number.%s takes exactly one argument", e->str);
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
                    if (!is_numeric(e->args.items[k]->type)) xs_error(XS7047, e->args.items[k]->pos, NULL, "argument of String.%s must be a number, but this is %s", e->str, ty_name(e->args.items[k]->type));
                }
                e->type = ty_string;
                e->builtin.cfn = e->str[4] == 'C' && e->str[5] == 'h' ? "xs_str_from_char_codes" : "xs_str_from_code_points";
                return;
            }
            if (strcmp(ns, "JSON") == 0) {
                if (!e->is_call || (strcmp(e->str, "stringify") != 0 && strcmp(e->str, "parse") != 0))
                    xs_error(XS7107, e->pos, "supported: JSON.stringify(value), JSON.parse(text) assigned to a typed variable", "unknown JSON member '%s'", e->str);
                if (e->args.len != 1) xs_error(XS7025, e->pos, NULL, "JSON.%s takes exactly one argument", e->str);
                if (strcmp(e->str, "stringify") == 0) {
                    check_expr(e->args.items[0], NULL);
                    settle(e->args.items[0]);
                    Type *t = e->args.items[0]->type;
                    if (t->kind == TY_JSON) { Expr *v = e->args.items[0]; *e = *v; e->type = ty_string; return; } /* already JSON */
                    json_ok_unknown = true;
                    bool ok = json_ok(t, 0);
                    json_ok_unknown = false;
                    if (!ok) xs_error(XS8100, e->args.items[0]->pos, "JSON covers number, int, boolean, string, arrays and classes made of those", "cannot serialize %s to JSON", ty_name(t));
                    e->type = ty_string;
                    e->builtin.cfn = "json_stringify"; /* resolved by the emitter per type */
                    return;
                }
                check_expr(e->args.items[0], ty_string);
                if (e->args.items[0]->type->kind == TY_JSON) e->args.items[0]->type = ty_string; /* JSON text already */
                expect_type(e->args.items[0], ty_string, "argument of JSON.parse");
                if (!expected && tsc_checked()) expected = ty_any; /* TypeScript's JSON.parse gives any: the dynamic path */
                if (!expected) xs_error(XS3003, e->pos, "write the type: const t: Todo = JSON.parse(text);", "JSON.parse needs to know the type it produces");
                if (ty_is_any(expected)) { /* JSON.parse(text) as an any: Records, unknown[] and boxed values */
                    Expr *t = e->args.items[0];
                    wrap_call(t, "xs_json_any", expected);
                    *e = *t;
                    return;
                }
                if (!json_ok(expected, 0)) { /* unknown values (any[], Record<string, unknown>, { x?: unknown }): decoded by what they hold */
                    json_ok_unknown = true;
                    bool ok = json_ok(expected, 0);
                    json_ok_unknown = false;
                    if (!ok) xs_error(XS8096, e->pos, "JSON covers number, int, boolean, string, arrays and classes made of those", "cannot parse JSON into %s", ty_name(expected));
                }
                e->type = expected;
                e->builtin.cfn = "json_parse";
                return;
            }
            if (strcmp(ns, "console") == 0) {
                if (!e->is_call || (strcmp(e->str, "log") != 0 && strcmp(e->str, "info") != 0 && strcmp(e->str, "debug") != 0 &&
                                    strcmp(e->str, "error") != 0 && strcmp(e->str, "warn") != 0))
                    xs_error(XS7112, e->pos, "supported: console.log, console.info, console.debug (stdout), console.error, console.warn (stderr)", "unknown console member '%s'", e->str);
                for (int i = 0; i < e->args.len; i++) {
                    check_expr(e->args.items[i], NULL);
                    settle(e->args.items[i]);
                    Type *t = e->args.items[i]->type;
                    if (ty_is_undef(t)) { undef_text(e->args.items[i]); continue; } /* console.log(f()) with f(): undefined */
                    if (t->kind == TY_VOID || t->kind == TY_FUNC)
                        xs_error(XS8097, e->args.items[i]->pos, NULL, "cannot print a %s value", ty_name(t));
                }
                e->type = ty_void;
                return;
            }
            Enum *en = find_enum(ns);
            if (en) {
                if (e->is_call) xs_error(XS9020, e->pos, NULL, "enum members are values, not functions");
                int idx = -1;
                for (int i = 0; i < en->nmembers; i++) if (strcmp(en->members[i].name, e->str) == 0) idx = i;
                if (idx < 0) {
                    const char **names = xs_alloc(sizeof(char *) * (size_t)en->nmembers);
                    for (int i = 0; i < en->nmembers; i++) names[i] = en->members[i].name;
                    xs_error(XS9016, e->pos, xs_help_suggest(e->str, names, en->nmembers, NULL), "enum %s has no member '%s'", en->name, e->str);
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
                if (!m && !e->is_call && strcmp(e->str, "name") == 0) { /* MyService.name: the class's name, as a string */
                    e->kind = E_STRING;
                    e->str = c->name;
                    e->lhs = NULL;
                    check_expr(e, expected);
                    return;
                }
                if (!m || !e->is_call) xs_error(XS8016, e->pos, NULL, "%s has no static member '%s'", c->name, e->str);
                check_args(e, m, xs_sprintf("%s.%s", c->name, m->name));
                e->func = m;
                e->cls = c;
                e->type = fret(m, e->pos);
                e->builtin.cfn = m->cname;
                prog->uses_net = true;
                return;
            }
        }
        if (e->lhs->kind == E_NEW_ARRAY) {
            Expr *na = e->lhs;
            if (!e->is_call || strcmp(e->str, "fill") != 0)
                xs_error(XS6017, e->pos, "write: new Array<T>(n).fill(x)", "a new array must be filled before use");
            na->elem_type = resolve(na->elem_type, na->pos);
            check_type_supported(na->pos, ty_array_of(na->elem_type));
            check_expr(na->lhs, ty_int);
            if (na->lhs->type->kind == TY_NUMBER)
                xs_error(XS8083, na->lhs->pos, "declare it as int, or convert with Math.trunc(x)", "array length must be int, but this is number");
            expect_type(na->lhs, ty_int, "array length");
            if (e->args.len != 1) xs_error(XS7055, e->pos, NULL, "fill takes exactly one argument");
            check_expr(e->args.items[0], na->elem_type);
            expect_type(e->args.items[0], na->elem_type, "fill value");
            na->type = e->type = ty_array_of(na->elem_type);
            return;
        }
        /* xs.map(f).filter(p) where f gives number | null (an array xisto cannot hold): one loop that keeps the
           values p accepts, __xs_mapFilterNum(xs, f, p) (xisto:arrays) */
        bool mapfilter = e->is_call && e->str && !strcmp(e->str, "filter") && e->args.len == 1 && e->lhs->kind == E_MEMBER &&
                         e->lhs->is_call && e->lhs->str && !strcmp(e->lhs->str, "map") && e->lhs->args.len == 1 && lookup_func("__xs_mapFilterNum");
        if (mapfilter) mapnull_ok++;
        { /* (x as any).f with x of a class that has f: the typed path (the cast only quiets TypeScript) */
            Expr *as = e->lhs;
            bool done = false;
            if (as->kind == E_AS && as->elem_type && ty_is_any(as->elem_type) && e->str && as->lhs->kind != E_OBJLIT && as->lhs->kind != E_ARRAY_LIT) {
                Expr *inner = as->lhs;
                check_expr(inner, NULL);
                Type *it = inner->type;
                Class *ic = it->kind == TY_OBJECT && !it->cls->builtin ? it->cls : NULL;
                if (ic && (e->is_call ? find_method(ic, e->str) != NULL : (find_field(ic, e->str, NULL) || find_method(ic, xs_sprintf("__get_%s", e->str)))))
                    e->lhs = inner;
                else { *as = *inner; to_any(as); }
                done = true;
            }
            if (!done) check_expr(e->lhs, NULL);
        }
        if (mapfilter) {
            mapnull_ok--;
            Type *lt = e->lhs->type;
            if (lt->kind == TY_ARRAY && lt->elem->kind == TY_NULLABLE && !ty_is_heap(lt->elem->elem) && is_numeric(lt->elem->elem)) {
                Expr *mp = e->lhs;
                Expr *callee = xs_alloc(sizeof *callee);
                callee->kind = E_IDENT; callee->pos = e->pos; callee->str = "__xs_mapFilterNum"; callee->field = -1;
                Expr *c = xs_alloc(sizeof *c);
                c->kind = E_CALL; c->pos = e->pos; c->field = -1;
                c->lhs = callee;
                mp->lhs->prechecked = true;
                mp->args.items[0]->prechecked = true;
                expr_list_push(&c->args, mp->lhs);
                expr_list_push(&c->args, mp->args.items[0]);
                expr_list_push(&c->args, e->args.items[0]);
                *e = *c;
                check_expr(e, expected);
                return;
            }
        }
        e->lhs = rec_get_nn(e->lhs);
        if (e->is_call && e->str && (strcmp(e->str, "then") == 0 || strcmp(e->str, "catch") == 0 || strcmp(e->str, "finally") == 0)) {
            Type *lt = e->lhs->type, *ls = ty_strip_null(lt);
            Class *lc = ls->kind == TY_OBJECT ? ls->cls : NULL;
            /* (f().catch(...) with f async and giving T | null: the promise's method, not the value's; x?.then stays the value's) */
            if (lt->kind == TY_PROMISE || (pr_is_call(e->lhs) && (lt->kind != TY_NULLABLE || e->op != TK_QDOT) && !(lc && find_method(lc, e->str)))) { /* XB-106 */
                if (lt->kind != TY_PROMISE) pr_spawn(e->lhs);
                check_pr_member(e, expected);
                return;
            }
        }
        if (ty_is_any(e->lhs->type) && e->is_call && e->str && e->args.len >= 1 && e->args.items[0]->kind == E_ARROW &&
            (!strcmp(e->str, "map") || !strcmp(e->str, "filter") || !strcmp(e->str, "forEach") || !strcmp(e->str, "find") ||
             !strcmp(e->str, "some") || !strcmp(e->str, "every") || !strcmp(e->str, "findIndex") || !strcmp(e->str, "flatMap"))) {
            /* rows.map((r: { DT_EXAME: Date | string }) => ...) with rows an any (a query's result): the callback says what the
               elements are, so it is (rows as T[]).map(...) (decoded and checked as such a cast is) */
            Func *af = e->args.items[0]->arrow->fn;
            Type *el = af->nparams >= 1 && af->params[0].type ? resolve(af->params[0].type, e->args.items[0]->pos) : NULL;
            if (el && !ty_is_any(el) && el->kind != TY_UNKNOWN && el->kind != TY_VOID && json_target(ty_array_of(el))) {
                /* (the rows are data: copied through JSON into T[], as JSON.parse(JSON.stringify(rows)) typed T[] would) */
                Pos at = e->lhs->pos;
                Expr *js = xs_alloc(sizeof *js);
                js->kind = E_IDENT; js->pos = at; js->field = -1; js->str = "JSON";
                Expr *str = xs_alloc(sizeof *str);
                str->kind = E_MEMBER; str->pos = at; str->field = -1; str->lhs = js; str->str = "stringify"; str->is_call = true;
                e->lhs->prechecked = true;
                expr_list_push(&str->args, e->lhs);
                Expr *js2 = xs_alloc(sizeof *js2);
                *js2 = *js;
                Expr *parse = xs_alloc(sizeof *parse);
                parse->kind = E_MEMBER; parse->pos = at; parse->field = -1; parse->lhs = js2; parse->str = "parse"; parse->is_call = true;
                expr_list_push(&parse->args, str);
                Expr *as = xs_alloc(sizeof *as);
                as->kind = E_AS; as->pos = at; as->field = -1; as->lhs = parse; as->elem_type = ty_array_of(el);
                e->lhs = as;
                check_expr(e, expected);
                return;
            }
        }
        if (tsc_checked() && !e->is_call && e->lhs->type && e->lhs->type->kind == TY_UNKNOWN && !ty_is_any(e->lhs->type))
            e->lhs->type = ty_any; /* body.link on an unknown that tsc narrowed ('link' in body): read by name, as an any */
        if (ty_is_any(e->lhs->type)) { /* x.f on an any: by name, through the object's class (or a Record's key) */
            if (e->is_call) xs_error(XS2017, e->pos, "give the value its type (as SomeClass) to call its methods", "cannot call '%s' on an any: its methods are not looked up by name", e->str);
            dyn_call(e, e->op == TK_QDOT ? "xs_dyn_get_opt" : "xs_dyn_get", ty_any, e->lhs, dyn_key(e->pos, e->str), NULL);
            return;
        }
        if (e->lhs->type->kind == TY_NULLABLE) {
            if (xs_check_mode && after_optional_dot(e->pos)) xs_error_quiet(); /* x?.name: the '?.' was counted where it is */
            if (e->is_call && e->args.len == 0 && e->str && (!strcmp(e->str, "unref") || !strcmp(e->str, "ref")) && e->lhs->type->elem->kind == TY_INT) {
                /* timer.unref?.() with timer: NodeJS.Timeout | undefined: changes nothing in xisto (XB-123), null or not */
                Pos at = e->pos;
                Expr *z = xs_alloc(sizeof *z);
                z->kind = E_NUMBER; z->pos = at; z->field = -1; z->type = ty_int; z->is_int_lit = true;
                memset(e, 0, sizeof *e);
                e->kind = E_CALL; e->pos = at; e->field = -1; e->builtin.cfn = "xs_timer_ref_noop"; e->type = ty_void;
                expr_list_push(&e->args, z);
                e->prechecked = true; /* (a ?. around it checks its rest again) */
                return;
            }
            if (tsc_checked() && e->str && strcmp(e->str, "unref") && strcmp(e->str, "ref")) nonnull_checked(e->lhs, xs_sprintf("the value read for '.%s'", e->str));
            else xs_error(XS2006, e->pos, "check it first: if (x !== null) { ... }  (fields need a local copy: const v = this.f;)", "'%s' may be null", e->str);
        }
        if (!e->is_call && e->str && e->lhs->builtin.cfn && strcmp(e->lhs->builtin.cfn, "xs_process_env") == 0 && strcmp(e->str, "size") != 0) {
            /* process.env.NAME: process.env.get("NAME") (a string, or null when it is not set) */
            e->is_call = true;
            e->args = (ExprList){0};
            expr_list_push(&e->args, str_lit(e->pos, e->str));
            e->str = "get";
        }
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
            xs_error(XS7008, e->pos, xs_help_suggest(m, cm, 4, "Channel has send(value), receive(), close() and size"), "Channel has no member '%s'", m);
        }
        if (e->is_call && e->args.len == 0 && e->str && strcmp(e->str, "json") == 0 && e->lhs->type->kind == TY_OBJECT &&
            e->lhs->type->cls->builtin && strcmp(e->lhs->type->cls->name, "Response") == 0) {
            /* response.json(): JSON.parse(response.text()), typed where it goes (any where nothing says) */
            Pos at = e->pos;
            Expr *recv = e->lhs;
            Expr *txt = xs_alloc(sizeof *txt);
            txt->kind = E_MEMBER; txt->pos = at; txt->field = -1; txt->lhs = recv; txt->str = "text"; txt->is_call = true;
            Expr *js = xs_alloc(sizeof *js);
            js->kind = E_IDENT; js->pos = at; js->field = -1; js->str = "JSON";
            memset(e, 0, sizeof *e);
            e->kind = E_MEMBER; e->pos = at; e->field = -1; e->lhs = js; e->str = "parse"; e->is_call = true;
            expr_list_push(&e->args, txt);
            check_expr(e, expected && expected->kind != TY_UNKNOWN ? expected : ty_any);
            return;
        }
        if (e->lhs->type->kind == TY_UNION && e->is_call && e->str && rereadable(e->lhs)) {
            /* exec.query(sql) with exec: DataSource | EntityManager (Pick<A | B, 'query'>), every member a class with the
               method: called on the class the object has, exec instanceof A ? (exec as A).query(sql) : ... (one runs,
               so the arguments, copies of plain expressions, are evaluated once) */
            Type *u = e->lhs->type;
            bool all = true;
            for (int i = 0; i < u->nparams && all; i++) {
                Class *c = u->params[i]->kind == TY_OBJECT ? u->params[i]->cls : NULL;
                all = c && !c->builtin && !c->is_interface && find_method(c, e->str);
            }
            for (int i = 0; i < e->args.len && all; i++) all = branch_copy(e->args.items[i]) != NULL;
            if (all) {
                bool awaited = pr_awaited == e;
                Expr *chain = NULL;
                for (int i = u->nparams - 1; i >= 0; i--) {
                    Class *c = u->params[i]->cls;
                    Expr *as = xs_alloc(sizeof *as);
                    as->kind = E_AS; as->pos = e->pos; as->field = -1; as->lhs = pure_copy(e->lhs); as->elem_type = u->params[i];
                    Expr *call = xs_alloc(sizeof *call);
                    call->kind = E_MEMBER; call->pos = e->pos; call->field = -1; call->is_call = true; call->str = e->str; call->lhs = as;
                    call->targs = e->targs; call->ntargs = e->ntargs;
                    for (int k = 0; k < e->args.len; k++) expr_list_push(&call->args, i == 0 ? e->args.items[k] : branch_copy(e->args.items[k]));
                    Expr *br = call;
                    if (awaited) { br = xs_alloc(sizeof *br); br->kind = E_AWAIT; br->pos = e->pos; br->field = -1; br->lhs = call; }
                    if (!chain) { chain = br; continue; }
                    Expr *cn = xs_alloc(sizeof *cn);
                    cn->kind = E_IDENT; cn->pos = e->pos; cn->field = -1; cn->str = c->name;
                    Expr *io = xs_alloc(sizeof *io);
                    io->kind = E_BINARY; io->op = TK_IDENT; io->pos = e->pos; io->field = -1; io->lhs = pure_copy(e->lhs); io->rhs = cn;
                    Expr *cond = xs_alloc(sizeof *cond);
                    cond->kind = E_COND; cond->pos = e->pos; cond->field = -1; cond->lhs = io;
                    expr_list_push(&cond->args, br);
                    expr_list_push(&cond->args, chain);
                    chain = cond;
                }
                *e = *chain;
                check_expr(e, expected);
                return;
            }
        }
        if (e->lhs->type->kind == TY_UNION) { /* s.kind: a field every member has, read by the object's class */
            Type *u = e->lhs->type;
            if (e->is_call) xs_error(XS8088, e->pos, "narrow it first: if (x instanceof C) / if (typeof x === \"string\") / if (x.kind === \"...\")", "cannot call a method on %s", ty_name(u));
            Type *ft = NULL;
            bool all_lit = true;
            for (int i = 0; i < u->nparams; i++) {
                Type *m = u->params[i];
                Field *fd = m->kind == TY_OBJECT && !m->cls->builtin ? find_field(m->cls, e->str, NULL) : NULL;
                if (!fd) xs_error(XS8054, e->pos, "narrow it first to the member that has it", "'%s' is not a field of every member of %s (%s has none)", e->str, ty_name(u), ty_name(m));
                if (!(fd->type->kind == TY_STRING && fd->type->tname)) all_lit = false;
                if (!ft) ft = fd->type;
                else if (!ty_equal(ft, fd->type)) xs_error(XS8045, e->pos, NULL, "'%s' has different types in the members of %s", e->str, ty_name(u));
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
            if (e->is_call && strcmp(e->str, "push") == 0 && e->args.len > 1 && rereadable(e->lhs)) {
                /* xs.push(a, b): xs.push(a); xs.push(b) (in order; the value of the last is the new length) */
                Expr *seq = xs_alloc(sizeof *seq);
                seq->kind = E_SEQ; seq->pos = e->pos; seq->field = -1;
                for (int i = 0; i < e->args.len; i++) {
                    Expr *p1 = xs_alloc(sizeof *p1);
                    p1->kind = E_MEMBER; p1->pos = e->args.items[i]->pos; p1->field = -1; p1->is_call = true; p1->str = "push";
                    p1->lhs = pure_copy(e->lhs);
                    expr_list_push(&p1->args, e->args.items[i]);
                    check_expr(p1, NULL);
                    expr_list_push(&seq->args, p1);
                }
                seq->type = ty_void;
                *e = *seq;
                return;
            }
            if (e->is_call && strcmp(e->str, "push") == 0) {
                if (e->args.len != 1) xs_error(XS7083, e->pos, NULL, "push takes exactly one argument in xisto");
                check_expr(e->args.items[0], elem);
                expect_type(e->args.items[0], elem, "argument of push");
                e->type = ty_int;
                e->builtin.cfn = "push";
                return;
            }
            if (e->is_call && strcmp(e->str, "pop") == 0) {
                if (e->args.len != 0) xs_error(XS7079, e->pos, NULL, "pop takes no arguments");
                e->type = elem;
                e->builtin.cfn = "pop";
                return;
            }
            if (e->is_call && strcmp(e->str, "unshift") == 0) {
                if (e->args.len != 1) xs_error(XS7117, e->pos, NULL, "unshift takes exactly one argument in xisto");
                check_expr(e->args.items[0], elem);
                expect_type(e->args.items[0], elem, "argument of unshift");
                e->type = ty_int;
                e->builtin.cfn = "unshift";
                return;
            }
            if (e->is_call && strcmp(e->str, "shift") == 0) {
                if (e->args.len != 0) xs_error(XS7087, e->pos, NULL, "shift takes no arguments");
                e->type = elem;
                e->builtin.cfn = "shift";
                return;
            }
            if (e->is_call && strcmp(e->str, "entries") == 0 && e->args.len == 0 && lookup_func("__xs_entries")) {
                /* xs.entries(): the [index, element] pairs (for (const [i, x] of xs.entries())) */
                Expr *callee = xs_alloc(sizeof *callee);
                callee->kind = E_IDENT; callee->pos = e->pos; callee->str = "__xs_entries"; callee->field = -1;
                Expr *c = xs_alloc(sizeof *c);
                c->kind = E_CALL; c->pos = e->pos; c->field = -1;
                c->lhs = callee;
                expr_list_push(&c->args, e->lhs);
                *e = *c;
                check_expr(e, expected);
                return;
            }
            if (e->is_call && strcmp(e->str, "splice") == 0 && e->args.len == 1 && lookup_func("__xs_spliceFrom")) {
                /* xs.splice(i): removes and returns the elements from i on (the loop of xisto:arrays) */
                Expr *callee = xs_alloc(sizeof *callee);
                callee->kind = E_IDENT; callee->pos = e->pos; callee->str = "__xs_spliceFrom"; callee->field = -1;
                Expr *c = xs_alloc(sizeof *c);
                c->kind = E_CALL; c->pos = e->pos; c->field = -1;
                c->lhs = callee;
                expr_list_push(&c->args, e->lhs);
                expr_list_push(&c->args, e->args.items[0]);
                *e = *c;
                check_expr(e, expected);
                return;
            }
            if (e->is_call && strcmp(e->str, "splice") == 0) {
                if (e->args.len != 2) xs_error(XS7091, e->pos, "xisto supports splice(index, 1) to remove one element", "splice takes two arguments: splice(index, 1)");
                check_expr(e->args.items[0], ty_int);
                expect_type(e->args.items[0], ty_int, "splice index");
                check_expr(e->args.items[1], ty_int);
                if (e->args.items[1]->kind != E_NUMBER || e->args.items[1]->ival != 1)
                    xs_error(XS7090, e->args.items[1]->pos, "xisto supports splice(index, 1) only", "splice count must be the literal 1");
                e->type = ty_void;
                e->builtin.cfn = "splice1";
                return;
            }
            if (e->is_call && (!strcmp(e->str, "reduce") || !strcmp(e->str, "flatMap") || !strcmp(e->str, "flat"))) {
                /* xs.reduce(f, a) / xs.flatMap(f) / xs.flat(): the loop of xisto:arrays, __xs_reduce(xs, f, a) */
                const char *m = e->str;
                const char *fn = !strcmp(m, "flat") ? "__xs_flat" : !strcmp(m, "flatMap") ? "__xs_flatMap" : e->args.len >= 2 ? "__xs_reduce" : "__xs_reduce1";
                if (!strcmp(m, "flat") && e->args.len > 0) xs_error(XS7001, e->pos, "flat() flattens one level", "flat takes no argument in xisto");
                Expr *callee = xs_alloc(sizeof *callee);
                callee->kind = E_IDENT; callee->pos = e->pos; callee->str = (char *)fn; callee->field = -1;
                Expr *c = xs_alloc(sizeof *c);
                c->kind = E_CALL; c->pos = e->pos; c->field = -1;
                c->lhs = callee;
                expr_list_push(&c->args, e->lhs);
                for (int i = 0; i < e->args.len; i++) expr_list_push(&c->args, e->args.items[i]);
                c->targs = e->targs; c->ntargs = e->ntargs; /* reduce<A>(...): A, the rest inferred */
                *e = *c;
                check_expr(e, expected);
                return;
            }
            if (e->is_call) {
                const char *m = e->str;
                #define CB(nparams_, ret_) ({ Type **ps_ = xs_alloc(sizeof(Type *) * 2); ps_[0] = elem; ps_[1] = ty_int; ty_func(ps_, nparams_, ret_); })
                if (strcmp(m, "forEach") == 0 || strcmp(m, "filter") == 0 || strcmp(m, "find") == 0 || strcmp(m, "findIndex") == 0 ||
                    strcmp(m, "some") == 0 || strcmp(m, "every") == 0 || strcmp(m, "map") == 0) {
                    if (e->args.len != 1) xs_error(XS8034, e->pos, NULL, "%s takes one function", m);
                    Expr *cb = e->args.items[0];
                    if (conv_name(cb)) conv_arrow(cb); /* xs.map(Number), xs.filter(Boolean) */
                    bool is_map = strcmp(m, "map") == 0, is_each = strcmp(m, "forEach") == 0;
                    /* the callback takes (element) or (element, index); map infers its result type from the callback */
                    int np = 1;
                    if (cb->kind == E_ARROW) np = cb->arrow->fn->nparams;
                    if (np == 0 && cb->kind == E_ARROW) { /* xs.map(() => null): the element, unused */
                        Func *af = cb->arrow->fn;
                        af->params = xs_alloc(sizeof(Param));
                        memset(af->params, 0, sizeof(Param));
                        af->params[0].name = "__el_unused";
                        af->nparams = 1;
                        np = 1;
                    }
                    if (np < 1 || np > 2) xs_error(XS8129, cb->pos, NULL, "the callback of %s takes (element) or (element, index)", m);
                    Type *cbt = CB(np, is_map ? NULL : is_each ? ty_void : ty_boolean);
                    if (is_map) {
                        if (cb->kind == E_ARROW && !cb->arrow->fn->ret) { /* infer the element type of the result */
                            /* (xs.map((c) => ({ ... })) where an Achado[] goes: each result is an Achado) */
                            cbt->ret = expected && expected->kind == TY_ARRAY && (expected->elem->kind == TY_OBJECT ||
                                       (expected->elem->kind == TY_UNION && cb->kind == E_ARROW && cb->arrow->expr_body)) ? expected->elem : NULL;
                            /* (xs.map((d) => ({ provide, useFactory })) where Provider[] goes: each result the member it is) */
                            check_expr(cb, cbt);
                        } else {
                            /* xs.map(([c]): [string, V] => ...): the return type written, the parameters from the elements */
                            bool untyped = false;
                            if (cb->kind == E_ARROW) for (int k = 0; k < cb->arrow->fn->nparams; k++) if (!cb->arrow->fn->params[k].type) untyped = true;
                            check_expr(cb, untyped ? CB(np, NULL) : NULL);
                            /* the parameters must still match the elements: the runtime calls it with them */
                            if (cb->type->kind == TY_FUNC) expect_type(cb, CB(np, cb->type->ret), xs_sprintf("callback of %s", m));
                        }
                    } else {
                        check_expr(cb, cbt);
                        expect_type(cb, cbt, xs_sprintf("callback of %s", m));
                    }
                    if (cb->type->kind != TY_FUNC) xs_error(XS8023, cb->pos, NULL, "%s needs a function", m);
                    if (is_map) {
                        Type *rt = cb->type->ret;
                        bool nullnum = rt->kind == TY_NULLABLE && !ty_is_heap(rt->elem) && is_numeric(rt->elem);
                        if (nullnum && mapnull_ok) e->type = ty_array_of(rt); /* (.filter right after: __xs_mapFilterNum) */
                        else {
                            if (rt->kind == TY_VOID || rt->kind == TY_FUNC || (rt->kind == TY_NULLABLE && !ty_is_heap(rt->elem) && !nullnum_elem(rt)))
                                xs_error(XS7064, cb->pos, NULL, "map cannot produce arrays of %s", ty_name(rt));
                            e->type = ty_array_of(rt);
                            check_type_supported(e->pos, e->type);
                        }
                    } else if (strcmp(m, "filter") == 0) {
                        e->type = e->lhs->type;
                        Type *pr = cb->kind == E_ARROW && cb->arrow->fn->ret && cb->arrow->fn->ret->kind == TY_BOOLEAN ? cb->arrow->fn->ret->elem : NULL;
                        if (pr && elem->kind == TY_NULLABLE && ty_is_heap(elem->elem)) { /* xs.filter((x): x is string => x != null): string[] */
                            Type *pt = resolve(pr, cb->pos);
                            if (ty_equal(pt, elem->elem)) e->type = ty_array_of(pt); /* (laid out alike: the nulls are left out) */
                            else if (conv_ok(elem->elem, pt, 0)) e->type = ty_array_of(elem->elem); /* (v): v is FeedVersion with v a
                                                  literal of FeedVersion's shape: those, not null (converted where FeedVersion[] goes) */
                        }
                        if (pr && elem->kind == TY_UNKNOWN && !ty_is_any(elem)) { /* xs.filter((x): x is string => typeof x === 'string'),
                                                                                    xs unknown[]: string[] (each one checked) */
                            Type *pt = resolve(pr, cb->pos);
                            if (pt->kind == TY_STRING || pt->kind == TY_NUMBER || pt->kind == TY_INT || pt->kind == TY_BOOLEAN) {
                                e->builtin.cfn = m;
                                e->field = np;
                                if (pt->kind == TY_STRING && pt->tname) pt = ty_string;
                                arr_convert(e, pt);
                                return;
                            }
                        }
                    } else if (strcmp(m, "find") == 0) {
                        if (!ty_is_heap(elem)) xs_error(XS7056, e->pos, "for arrays of numbers use findIndex", "find needs an array of objects or strings (it returns T | null)");
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
                if ((strcmp(m, "indexOf") == 0 || strcmp(m, "includes") == 0) && e->args.len == 1 &&
                    (elem->kind == TY_OBJECT || elem->kind == TY_ARRAY || elem->kind == TY_MAP || elem->kind == TY_SET) && rereadable(e->args.items[0])) {
                    /* dates.includes(column) of objects: by identity, as JavaScript's (xs.findIndex((x) => x === v), >= 0) */
                    Pos pos = e->pos;
                    Func *f = xs_alloc(sizeof *f);
                    f->pos = pos; f->name = "arrow";
                    f->params = xs_alloc(sizeof(Param));
                    memset(f->params, 0, sizeof(Param));
                    f->params[0].name = "__ix";
                    f->params[0].type = elem;
                    f->nparams = 1;
                    f->ret = ty_boolean;
                    Expr *id = xs_alloc(sizeof *id);
                    id->kind = E_IDENT; id->pos = pos; id->field = -1; id->str = "__ix";
                    Expr *eq = xs_alloc(sizeof *eq);
                    eq->kind = E_BINARY; eq->pos = pos; eq->field = -1; eq->op = TK_EQ; eq->lhs = id; eq->rhs = e->args.items[0];
                    Stmt *b = xs_alloc(sizeof *b);
                    b->kind = S_BLOCK; b->pos = pos;
                    Stmt *r = xs_alloc(sizeof *r);
                    r->kind = S_RETURN; r->pos = pos; r->expr = eq;
                    stmt_list_push(&b->stmts, r);
                    f->body = b;
                    Arrow *a = xs_alloc(sizeof *a);
                    a->pos = pos; a->fn = f; a->expr_body = true;
                    Expr *ar = xs_alloc(sizeof *ar);
                    ar->kind = E_ARROW; ar->pos = pos; ar->field = -1; ar->arrow = a;
                    bool inc = strcmp(m, "includes") == 0;
                    Expr *fi = xs_alloc(sizeof *fi);
                    fi->kind = E_MEMBER; fi->pos = pos; fi->field = -1; fi->lhs = e->lhs; fi->str = "findIndex"; fi->is_call = true;
                    e->lhs->prechecked = true;
                    expr_list_push(&fi->args, ar);
                    if (inc) {
                        memset(e, 0, sizeof *e);
                        e->kind = E_BINARY; e->pos = pos; e->field = -1; e->op = TK_GE; e->lhs = fi; e->rhs = num_lit(pos, 0, true);
                    } else *e = *fi;
                    check_expr(e, expected);
                    return;
                }
                if (strcmp(m, "indexOf") == 0 || strcmp(m, "includes") == 0) {
                    if (e->args.len != 1) xs_error(XS8033, e->pos, NULL, "%s takes one argument", m);
                    if (elem->kind == TY_OBJECT || elem->kind == TY_ARRAY || elem->kind == TY_MAP || elem->kind == TY_SET)
                        xs_error(XS8006, e->pos, "use findIndex with a comparison", "%s compares by value and works on arrays of numbers, strings, booleans and enums", m);
                    check_expr(e->args.items[0], elem);
                    expect_type(e->args.items[0], elem, xs_sprintf("argument of %s", m));
                    e->type = strcmp(m, "indexOf") == 0 ? ty_int : ty_boolean;
                    e->builtin.cfn = m;
                    return;
                }
                if (strcmp(m, "join") == 0) {
                    if (e->args.len > 1) xs_error(XS7060, e->pos, NULL, "join takes at most one argument");
                    if (e->args.len == 1) { check_expr(e->args.items[0], ty_string); expect_type(e->args.items[0], ty_string, "separator of join"); }
                    if (elem->kind == TY_OBJECT || elem->kind == TY_ARRAY || elem->kind == TY_MAP || elem->kind == TY_SET)
                        xs_error(XS7061, e->pos, "map the elements to strings first", "join works on arrays of numbers, strings, booleans and enums");
                    e->type = ty_string;
                    e->builtin.cfn = m;
                    return;
                }
                if (strcmp(m, "sort") == 0) {
                    if (e->args.len == 0 && (elem->kind == TY_STRING || (elem->kind == TY_ENUM && elem->en->is_string))) {
                        /* names.sort(): strings in their default order (by their characters), as JavaScript sorts */
                        Pos at = e->pos;
                        #define ID_(n_) ({ Expr *x_ = xs_alloc(sizeof *x_); x_->kind = E_IDENT; x_->pos = at; x_->field = -1; x_->str = n_; x_; })
                        #define NUM_(v_) ({ Expr *x_ = xs_alloc(sizeof *x_); x_->kind = E_NUMBER; x_->pos = at; x_->field = -1; x_->is_int_lit = true; x_->ival = v_; x_->num = v_; x_; })
                        #define BIN_(op_, l_, r_) ({ Expr *x_ = xs_alloc(sizeof *x_); x_->kind = E_BINARY; x_->pos = at; x_->field = -1; x_->op = op_; x_->lhs = l_; x_->rhs = r_; x_; })
                        #define COND_(c_, y_, n_) ({ Expr *x_ = xs_alloc(sizeof *x_); x_->kind = E_COND; x_->pos = at; x_->field = -1; x_->lhs = c_; expr_list_push(&x_->args, y_); expr_list_push(&x_->args, n_); x_; })
                        Expr *neg = xs_alloc(sizeof *neg);
                        neg->kind = E_UNARY; neg->op = TK_MINUS; neg->pos = at; neg->field = -1; neg->lhs = NUM_(1);
                        Expr *body = COND_(BIN_(TK_LT, ID_("__sa"), ID_("__sb")), neg, COND_(BIN_(TK_GT, ID_("__sa"), ID_("__sb")), NUM_(1), NUM_(0)));
                        #undef ID_
                        #undef NUM_
                        #undef BIN_
                        #undef COND_
                        Expr *arrow = thunk_of(body);
                        Func *f = arrow->arrow->fn;
                        f->params = xs_alloc(sizeof(Param) * 2);
                        memset(f->params, 0, sizeof(Param) * 2);
                        f->params[0].name = "__sa"; f->params[1].name = "__sb";
                        f->params[0].type = elem; f->params[1].type = elem;
                        f->nparams = 2;
                        f->ret = ty_int;
                        expr_list_push(&e->args, arrow);
                    }
                    if (e->args.len != 1) xs_error(XS7089, e->pos, "JavaScript's default sort compares as strings; xisto requires a comparator: sort((a, b) => a - b)", "sort takes a comparator function");
                    Type **ps = xs_alloc(sizeof(Type *) * 2); ps[0] = elem; ps[1] = elem;
                    Type *cbt = ty_func(ps, 2, NULL);
                    Expr *cb = e->args.items[0];
                    if (cb->kind == E_ARROW && !cb->arrow->fn->ret) check_expr(cb, cbt); else check_expr(cb, NULL);
                    if (cb->type->kind != TY_FUNC || cb->type->nparams != 2 || !ty_equal(cb->type->params[0], elem) || !is_numeric(cb->type->ret))
                        xs_error(XS8130, cb->pos, NULL, "the comparator must be (a: %s, b: %s) => number", ty_name(elem), ty_name(elem));
                    e->type = e->lhs->type;
                    e->builtin.cfn = m;
                    e->field = cb->type->ret->kind == TY_INT ? 1 : 0;
                    return;
                }
                if (strcmp(m, "reverse") == 0) {
                    if (e->args.len != 0) xs_error(XS7086, e->pos, NULL, "reverse takes no arguments");
                    e->type = e->lhs->type;
                    e->builtin.cfn = m;
                    return;
                }
                if (strcmp(m, "slice") == 0) {
                    if (e->args.len > 2) xs_error(XS7088, e->pos, NULL, "slice takes at most two arguments");
                    for (int i = 0; i < e->args.len; i++) { check_expr(e->args.items[i], ty_int); expect_type(e->args.items[i], ty_int, "argument of slice"); }
                    e->type = e->lhs->type;
                    e->builtin.cfn = m;
                    return;
                }
            }
            {
                static const char *names[] = {"length", "push", "pop", "unshift", "shift", "splice", "forEach", "map", "filter", "find", "findIndex", "some", "every", "indexOf", "includes", "join", "sort", "reverse", "slice"};
                xs_error(XS6038, e->pos, xs_help_suggest(e->str, names, 19, "arrays support indexing, length, push, pop, unshift, shift, splice(i, 1), forEach, map, filter, find, findIndex, some, every, indexOf, includes, join, sort(cmp), reverse, slice"), "unsupported array member '%s'", e->str);
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
                    xs_error(XS7036, e->pos, "StatementSync has run(...params), all(...params) and get(...params)", "StatementSync has no member '%s'", m);
                for (int i = 0; i < e->args.len; i++) {
                    Expr *a = e->args.items[i];
                    check_expr(a, NULL);
                    settle(a);
                    TypeKind k = a->type->kind;
                    bool ok = k == TY_INT || k == TY_NUMBER || k == TY_STRING || k == TY_BOOLEAN || k == TY_NULL ||
                              (k == TY_ENUM) || (k == TY_NULLABLE && a->type->elem->kind == TY_STRING);
                    if (!ok) xs_error(XS7050, a->pos, "parameters are int, number, string, boolean or null", "cannot bind %s as a SQL parameter", ty_name(a->type));
                }
                if (strcmp(m, "run") == 0) e->type = ty_object_of(find_class("RunResult"));
                else {
                    bool all = strcmp(m, "all") == 0;
                    Type *want = expected;
                    Type *row = !want ? NULL : all ? (want->kind == TY_ARRAY ? want->elem : NULL) : (want->kind == TY_NULLABLE ? want->elem : NULL);
                    if (!row || row->kind != TY_OBJECT || row->cls->builtin)
                        xs_error(XS3002, e->pos, all ? "write the row type: const rows: Todo[] = stmt.all();" : "write the row type: const row: Todo | null = stmt.get(id);",
                                 "%s() needs to know the row type it produces", m);
                    if (!json_ok(row, 0)) xs_error(XS8001, e->pos, NULL, "%s cannot be built from a row", ty_name(row));
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
                if (o->kind != E_OBJLIT) xs_error(XS7062, o->pos, "app.listen(443, { cert: \"cert.pem\", key: \"key.pem\" })", "listen options must be an object literal");
                Expr *cert = NULL, *key = NULL;
                for (int i = 0; i < o->args.len; i++) {
                    if (strcmp(o->keys[i], "cert") == 0) cert = o->args.items[i];
                    else if (strcmp(o->keys[i], "key") == 0) key = o->args.items[i];
                    else xs_error(XS7113, o->args.items[i]->pos, "supported: cert, key", "unknown listen option '%s'", o->keys[i]);
                    check_expr(o->args.items[i], ty_string);
                    expect_type(o->args.items[i], ty_string, xs_sprintf("listen option '%s'", o->keys[i]));
                }
                if (!cert || !key) xs_error(XS7023, o->pos, "give both: { cert: \"cert.pem\", key: \"key.pem\" }", "HTTPS needs a certificate and a key");
                e->args.len = 1;
                expr_list_push(&e->args, cert);
                expr_list_push(&e->args, key);
                e->cls = oc;
                e->type = ty_void;
                e->builtin.cfn = "xs_Server_listen_tls";
                e->builtin.self_first = true;
                if (xs_tls_off) xs_error(XS7024, o->pos, "remove --tls=off, or listen without cert/key behind the load balancer", "HTTPS server, but the program is built with --tls=off");
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
            member_expected = expected;
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
                if (e->lhs->type->kind == TY_NUMBER && e->args.len == 1 && tsc_checked()) /* c.toString(16) with c a number: whole, checked */
                    wrap_call(e->lhs, "xs_num_key", ty_int);
                bool is_int = e->lhs->type->kind == TY_INT;
                if (!is_int && e->args.len == 1) xs_error(XS7104, e->pos, "convert first: Math.trunc(x).toString(16)", "toString(radix) works on int");
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
            if (e->is_call && strcmp(e->str, "toPrecision") == 0 && e->args.len == 1) { /* n.toPrecision(p) */
                if (e->lhs->type->kind == TY_LITINT) coerce_lit(e->lhs, ty_number);
                if (e->lhs->type->kind == TY_INT) wrap_call(e->lhs, "(double)", ty_number);
                check_expr(e->args.items[0], ty_int);
                expect_type(e->args.items[0], ty_int, "precision");
                e->builtin.cfn = "xs_num_to_precision";
                e->type = ty_string;
                return;
            }
            if (e->is_call && strcmp(e->str, "toLocaleString") == 0 && e->args.len <= 2) {
                /* n.toLocaleString('pt-BR', { style: 'currency', currency: 'BRL', minimumFractionDigits: 2 }): the
                   options written in place (read here); pt-BR and en-US separators (runtime/xisto_rt.h) */
                if (e->lhs->type->kind == TY_LITINT) coerce_lit(e->lhs, ty_number);
                if (e->lhs->type->kind == TY_INT) wrap_call(e->lhs, "(double)", ty_number);
                Expr *loc = e->args.len >= 1 ? e->args.items[0] : NULL;
                Expr *opt = e->args.len == 2 ? e->args.items[1] : NULL;
                if (loc) { check_expr(loc, ty_string); expect_type(loc, ty_string, "locale"); }
                const char *cur = NULL;
                bool currency = false;
                long long minfd = -1, maxfd = -1;
                if (opt) {
                    if (opt->kind != E_OBJLIT) xs_error(XS7053, opt->pos, "write the options in place: { style: 'currency', currency: 'BRL' }", "toLocaleString options must be an object literal");
                    for (int k = 0; k < opt->args.len; k++) {
                        const char *key = opt->keys[k];
                        Expr *v = opt->args.items[k];
                        if (strcmp(key, "style") == 0 && v->kind == E_STRING) currency = strcmp(v->str, "currency") == 0;
                        else if (strcmp(key, "currency") == 0 && v->kind == E_STRING) cur = v->str;
                        else if (strcmp(key, "minimumFractionDigits") == 0 && v->kind == E_NUMBER) minfd = (long long)v->num;
                        else if (strcmp(key, "maximumFractionDigits") == 0 && v->kind == E_NUMBER) maxfd = (long long)v->num;
                        else xs_error(XS7053, v->pos, "supported: style, currency, minimumFractionDigits, maximumFractionDigits, written as literals", "toLocaleString option '%s' is not supported", key);
                    }
                }
                if (currency && !cur) xs_error(XS7053, opt->pos, NULL, "toLocaleString with style 'currency' needs a currency");
                if (minfd < 0) minfd = currency ? 2 : 0;
                if (maxfd < 0) maxfd = currency ? (minfd > 2 ? minfd : 2) : (minfd > 3 ? minfd : 3);
                e->args = (ExprList){0};
                Expr *nl = xs_alloc(sizeof *nl);
                nl->kind = E_NULL; nl->pos = e->pos; nl->field = -1; nl->type = ty_nullable_of(ty_string);
                expr_list_push(&e->args, loc ? loc : nl);
                if (currency) expr_list_push(&e->args, str_lit(e->pos, cur));
                else { Expr *n2 = xs_alloc(sizeof *n2); *n2 = *nl; expr_list_push(&e->args, n2); }
                expr_list_push(&e->args, num_lit(e->pos, (double)minfd, true));
                expr_list_push(&e->args, num_lit(e->pos, (double)maxfd, true));
                e->builtin.cfn = "xs_num_to_locale";
                e->type = ty_string;
                return;
            }
            if (e->is_call && e->args.len == 0 && e->lhs->type->kind == TY_INT && (!strcmp(e->str, "unref") || !strcmp(e->str, "ref"))) {
                /* timer.unref?.() on a setTimeout / setInterval handle (an int here): accepted, it changes nothing
                   (a server keeps running; XB-123) */
                e->builtin.cfn = "xs_timer_ref_noop";
                e->type = ty_void;
                return;
            }
            xs_error(XS8013, e->pos, NULL, "%s has no member '%s'", ty_name(e->lhs->type), e->str);
            return;
        default:
            xs_error(XS8013, e->pos, NULL, "%s has no member '%s'", ty_name(e->lhs->type), e->str);
        }
        return;
    case E_NEW_ARRAY:
        xs_error(XS6017, e->pos, "write: new Array<T>(n).fill(x)", "a new array must be filled before use");
        return;
    case E_SEQ: /* i++, j-- in a for loop's update */
        for (int i = 0; i < e->args.len; i++) check_expr(e->args.items[i], NULL);
        e->type = ty_void;
        return;
    case E_REGEX: { /* /pattern/flags: compiled here once so a bad pattern is a compile error */
        const char *err = NULL;
        int fl = xr_parse_flags(e->rflags, strlen(e->rflags), &err);
        if (fl < 0) xs_error(XS8111, e->pos, "flags: d g i m s u v y, each at most once", "invalid regular expression flags '%s'", e->rflags);
        xr_re *re = xr_compile(e->str, strlen(e->str), fl, &err);
        if (!re) xs_error(XS8110, e->pos, NULL, "invalid regular expression /%s/: %s", e->str, err);
        xr_free(re);
        e->type = ty_object_of(find_class("RegExp"));
        prog->uses_regex = true;
        return;
    }
    case E_NULL:
        if (e->unset && expected && expected->kind != TY_NULLABLE) { /* let x: T; */
            switch (expected->kind) {
            case TY_OBJECT: case TY_UNION: case TY_FUNC: case TY_UNKNOWN: case TY_PROMISE: case TY_ARRAY: case TY_MAP: case TY_SET: case TY_JSON: e->type = expected; return; /* (NULL until assigned) */
            case TY_ENUM: case TY_INT: case TY_NUMBER: e->kind = E_NUMBER; e->num = 0; e->is_int_lit = true; check_expr(e, expected); return;
            case TY_STRING: e->kind = E_STRING; e->str = ""; check_expr(e, expected); return;
            case TY_BOOLEAN: e->kind = E_BOOL; e->boolean = false; check_expr(e, expected); return;
            default: xs_error(XS8146, e->pos, "xisto has no 'undefined', so every variable starts with a value", "variable of type %s must be initialized", ty_name(expected));
            }
        }
        e->type = ty_null;
        if (expected && expected->kind == TY_NULLABLE) e->type = expected;
        return;
    case E_AS: {
        if (e->elem_type && e->lhs) { /* { m: () => ... } as C, or as unknown as C: a fake of C */
            Expr *lit = e->lhs->kind == E_AS && e->lhs->lhs && e->lhs->elem_type && e->lhs->elem_type->kind == TY_UNKNOWN ? e->lhs->lhs : e->lhs;
            if (lit->kind == E_OBJLIT) {
                Type *w = resolve(e->elem_type, e->pos);
                Type *wb = ty_strip_null(w);
                if (wb->kind == TY_OBJECT && fake_target(wb->cls, lit)) {
                    *e = *lit;
                    fake_object(e, wb->cls);
                    return;
                }
            }
        }
        if (!e->elem_type && e->op == TK_NOT) { /* x!: not null, checked at run time */
            check_expr(e->lhs, expected && expected->kind != TY_NULLABLE ? ty_nullable_of(expected) : expected);
            if (e->lhs->type->kind != TY_NULLABLE) { *e = *e->lhs; return; }
            e->type = e->lhs->type->elem;
            return;
        }
        Type *want = resolve(e->elem_type, e->pos);
        if (want->kind == TY_UNKNOWN && want->tname && !strcmp(want->tname, "never") && expected && expected->kind != TY_UNKNOWN &&
            expected->kind != TY_VOID && !has_tparam(expected)) {
            /* { where: { ... } } as never where FindOneOptions goes: never is assignable to anything, so the value is
               checked as the type expected there (an object literal gets its class, not a Record read by name) */
            e->elem_type = want = expected;
            if (e->lhs->kind == E_OBJLIT || e->lhs->kind == E_ARRAY_LIT) {
                Expr *inner = e->lhs;
                *e = *inner;
                check_expr(e, expected);
                return;
            }
        }
        { /* x as Record<string, unknown> (of an unknown, at run time): an object of a class is read through its field
             table, which the program then needs (xs_as_record) */
            Type *w = ty_strip_null(want);
            if (w->kind == TY_MAP && w->record && w->key->kind == TY_STRING && w->elem->kind == TY_UNKNOWN) xs_any_used = true;
        }
        { /* undefined as unknown as T / null as unknown as T: a placeholder assigned before it is read (TypeScript
             trusts it): the reference type's null */
            Expr *n = e->lhs;
            while (n->kind == E_AS && n->elem_type) n = n->lhs;
            if ((n->kind == E_NULL || (n->kind == E_IDENT && n->str && strcmp(n->str, "undefined") == 0 && !lookup("undefined"))) && n != e->lhs &&
                ty_is_heap(want) && want->kind != TY_NULLABLE && !ty_is_any(want)) {
                Pos at = e->pos;
                memset(e, 0, sizeof *e);
                e->kind = E_NULL; e->pos = at; e->field = -1; e->unset = true; e->type = want;
                return;
            }
        }
        bool lit_as = e->lhs->kind == E_OBJLIT && tsc_checked();
        if (lit_as) objlit_as_cast++; /* ({ ... } as Dto): TypeScript lets the cast leave fields out */
        check_expr(e->lhs, want);
        if (lit_as) objlit_as_cast--;
        Expr *v = e->lhs;
        if (v->type->kind == TY_LITINT && is_numeric(want)) coerce_lit(v, want);
        if (want->kind == TY_NULLABLE && v->kind == E_INDEX && !v->builtin.cfn && v->lhs->type && v->lhs->type->kind == TY_ARRAY &&
            ty_is_heap(v->lhs->type->elem) && v->lhs->type->elem->kind != TY_NULLABLE && v->type->kind != TY_NULLABLE) {
            /* rows[0] as Row | undefined: the cast says the element may be missing; null past the end, as JavaScript's undefined */
            v->builtin.cfn = "xs_arr_ref_at_or_null";
            if (v->type->kind != TY_UNKNOWN) v->type = ty_nullable_of(v->type); /* (an unknown holds null already) */
        }
        if (ty_is_any(want)) { *e = *v; to_any(e); return; } /* x as any: the same value, read by name from here on */
        if (ty_is_any(v->type) && (want->kind == TY_UNION || (want->kind == TY_NULLABLE && want->elem->kind == TY_UNION))) { *e = *v; e->type = want; return; }
        if (ty_equal(v->type, want)) { *e = *v; return; }
        if (v->type->kind == TY_PROMISE && want->kind == TY_PROMISE && pr_convertible(v->type->elem, want->elem)) { /* p as Promise<T>, p: Promise<unknown> */
            *e = *v;
            pr_convert(e, want->elem);
            return;
        }
        if (v->type->kind == TY_UNKNOWN && (want->kind == TY_UNION || (want->kind == TY_NULLABLE && want->elem->kind == TY_UNION))) {
            *e = *v; /* unknown as A | B: a union is held as unknown is (boxed); its uses narrow it */
            e->type = want;
            return;
        }
        if (v->type->kind == TY_UNKNOWN && (want->kind == TY_JSON || (want->kind == TY_NULLABLE && want->elem->kind == TY_JSON))) {
            *e = *v; /* unknown as object: TypeScript's object is held as JSON text, written from the value's type at run time */
            to_json(e);
            if (want->kind == TY_NULLABLE) e->type = want;
            return;
        }
        if (v->type->kind == TY_UNKNOWN) { /* checked at run time: the emitter compares the object's type */
            if (!unknown_target(want)) xs_error(XS2010, e->pos, NULL, "cannot check %s at run time", ty_name(want));
            Type *wi = want->kind == TY_NULLABLE ? want->elem : want;
            if (wi->kind == TY_OBJECT && wi->cls->iface_base && iface_runtime(wi->cls, e->pos)) {
                /* unknown as I, I an interface with methods some classes fit without being its subclasses (XB-126): an
                   object of one of them is wrapped in its adapter (the emitter's __iface_as: always an owned reference) */
                *e = *v;
                wrap_call(e, "__iface_as", want);
                e->elem_type = want;
                return;
            }
            e->type = want;
            e->elem_type = want;
            return;
        }
        if (want->kind == TY_UNKNOWN) { *e = *v; to_unknown(e); return; }
        if (v->type->kind == TY_JSON && (json_target(want) || json_target_u(want) ||
                                         ((want->kind == TY_NUMBER || want->kind == TY_INT || want->kind == TY_STRING || want->kind == TY_BOOLEAN) && !want->tname))) {
            *e = *v; from_json(e, want); return;
        }
        if ((v->type->kind == TY_JSON || (v->type->kind == TY_NULLABLE && v->type->elem->kind == TY_JSON)) && want->kind == TY_NULLABLE &&
            ty_is_heap(want->elem) && (json_target(want->elem) || json_target_u(want->elem))) { *e = *v; from_json(e, want); return; } /* (o as T | null) with o: object | null */
        if (want->kind == TY_JSON) { /* x as object: written as JSON text (an unknown inside by its type at run time) */
            bool ju = json_ok_unknown;
            json_ok_unknown = true;
            bool ok = json_ok(v->type, 0);
            json_ok_unknown = ju;
            if (ok) { *e = *v; to_json(e); return; }
        }
        if (want->kind == TY_NULLABLE && want->elem->kind == TY_JSON && v->type->kind != TY_NULL && v->type->kind != TY_NULLABLE && json_ok(v->type, 0)) { *e = *v; to_json(e); return; }
        if (want->kind == TY_NULLABLE && ty_equal(v->type, want->elem)) { *e = *v; e->type = want; return; }
        if (v->type->kind == TY_NULLABLE && ty_equal(v->type->elem, want)) { e->type = want; e->op = TK_NOT; e->elem_type = NULL; return; } /* T | null as T: checked */
        { /* base as Sub: checked at run time (a TypeError when the object is not a Sub); Sub as Base: an upcast */
            Type *g = v->type->kind == TY_NULLABLE ? v->type->elem : v->type, *w = want->kind == TY_NULLABLE ? want->elem : want;
            if (g->kind == TY_OBJECT && w->kind == TY_OBJECT && !g->cls->is_interface && !w->cls->is_interface) {
                if (is_subclass(g->cls, w->cls)) { *e = *v; upcast(e, want); return; }
                if (is_subclass(w->cls, g->cls)) { e->type = want; e->elem_type = want; e->op = TK_CLASS; return; } /* op marks a downcast */
            }
        }
        if (iface_adapt(v, want, "the value of 'as'")) { *e = *v; return; } /* c as I: I's shape (XB-126) */
        { /* (dto as { nuTitular?: number }).nuTitular: TypeScript reads the same object through another shape; here a
             copy of it in that shape (through its JSON: the fields both have), read-only in effect */
            Type *w = want->kind == TY_NULLABLE ? want->elem : want;
            Type *g = v->type->kind == TY_NULLABLE ? v->type->elem : v->type;
            if (w->kind == TY_OBJECT && !w->cls->builtin && g->kind == TY_OBJECT && !g->cls->builtin && json_ok(g, 0) && json_target(w) && v->type->kind != TY_NULLABLE) {
                *e = *v;
                to_json(e);
                from_json(e, want);
                return;
            }
        }
        { /* (ClassInfo | string)[] as Provider[]: unions held as unknown are (boxed), so the same array, as TypeScript's cast
             reads it (an element of another member is checked where it is used) */
            Type *g = ty_strip_null(v->type), *w = ty_strip_null(want);
            if (g->kind == TY_ARRAY && w->kind == TY_ARRAY && (g->elem->kind == TY_UNION || g->elem->kind == TY_UNKNOWN) &&
                (w->elem->kind == TY_UNION || w->elem->kind == TY_UNKNOWN) && !ty_is_any(w->elem) && (v->type->kind != TY_NULLABLE || want->kind == TY_NULLABLE)) {
                *e = *v; e->type = want; return;
            }
        }
        { /* Record<string, string> as Record<string, unknown>: values held by reference either way, the same map */
            Type *g = ty_strip_null(v->type), *w = ty_strip_null(want);
            if (g->kind == TY_MAP && w->kind == TY_MAP && w->record && g->key->kind == w->key->kind && w->elem->kind == TY_UNKNOWN &&
                !ty_is_any(w->elem) && ty_is_heap(g->elem) && (v->type->kind != TY_NULLABLE || want->kind == TY_NULLABLE)) {
                *e = *v; e->type = want; return;
            }
        }
        { /* Record<string, unknown> as Record<string, A | B>: a union is held as unknown is (boxed), so the same map */
            Type *g = v->type;
            if (g->kind == TY_MAP && want->kind == TY_MAP && g->key->kind == want->key->kind && g->elem->kind == TY_UNKNOWN &&
                (want->elem->kind == TY_UNION || (want->elem->kind == TY_NULLABLE && want->elem->elem->kind == TY_UNION))) {
                *e = *v; e->type = want; return;
            }
        }
        /* a value of data where another shape of data goes (TypeScript compares the fields): a copy in that shape */
        if (shape_copy(v, want)) { *e = *v; return; }
        { /* Record<string, unknown> as Record<string, string | number | null>: each value checked as it is decoded */
            bool ju = json_ok_unknown;
            json_ok_unknown = true;
            bool ok = (v->type->kind != TY_NULLABLE || want->kind == TY_NULLABLE) && json_ok(ty_strip_null(v->type), 0);
            json_ok_unknown = ju;
            if (ok && (json_target_u(want) || (want->kind == TY_NULLABLE && json_target_u(want->elem)) || (want->kind == TY_MAP && want->record && json_ok(want, 0)))) {
                *e = *v; to_json(e); from_json(e, want); return; /* (rows[0] as { R?: number } | undefined: a shape, or null) */
            }
        }
        { /* an upcast TypeScript writes with 'as': 0 as number | null, n as number | string | null, Enum.A as string */
            Type *w = want->kind == TY_NULLABLE ? want->elem : want;
            bool lit = v->type->kind == TY_LITINT;
            if (v->type->kind == TY_ENUM && v->type->en->is_string && want->kind == TY_STRING) { *e = *v; e->type = ty_string; return; }
            bool up = (want->kind == TY_NULLABLE && (ty_equal(v->type, w) || (lit && is_numeric(w)))) ||
                      (w->kind == TY_UNION && v->type->kind != TY_UNION && v->type->kind != TY_NULLABLE &&
                       union_member_for(w, lit ? ty_number : v->type, NULL));
            if (up) {
                *e = *v;
                expect_type(e, want, "the value of 'as'");
                if (want->kind == TY_NULLABLE && e->type->kind != TY_NULLABLE && e->type->kind != TY_NULL) upcast(e, want); /* (the field it fills is T | null) */
                return;
            }
        }
        if (want->kind == TY_PROMISE && v->type->kind != TY_PROMISE && pr_is_call(v)) {
            /* f() as Promise<X> with f async (its value here, awaited where it goes): the value as X */
            Expr *in = xs_alloc(sizeof *in);
            *in = *v;
            in->prechecked = true;
            memset(e, 0, sizeof *e);
            e->kind = E_AS; e->pos = in->pos; e->field = -1; e->lhs = in; e->elem_type = want->elem;
            check_expr(e, NULL);
            return;
        }
        { /* PageDto<OperatorIdentity> as PageDto<OperatorIdentityResponseDto>: two instances of one generic class, their
             fields converted as data (a copy, as a structural cast between shapes is) */
            Type *vb = ty_strip_null(v->type), *wb = ty_strip_null(want);
            if (vb->kind == TY_OBJECT && wb->kind == TY_OBJECT && vb->cls != wb->cls && vb->cls->generic_of && vb->cls->generic_of == wb->cls->generic_of &&
                (v->type->kind != TY_NULLABLE || want->kind == TY_NULLABLE)) {
                Expr *x = xs_alloc(sizeof *x);
                *x = *v;
                if (shape_copy(x, want)) { *e = *x; return; }
            }
        }
        xs_error(XS8090, e->pos, "xisto checks types statically: 'as' converts an object (JSON) value or restates the same type", "cannot convert %s to %s", ty_name(v->type), ty_name(want));
        return;
    }
    case E_OBJLIT: {
        if (expected && ty_strip_null(expected)->kind == TY_OBJECT && fake_target(ty_strip_null(expected)->cls, e) &&
            !ty_strip_null(expected)->cls->is_interface) { /* const handler: CallHandler = { handle: () => of(value) } */
            fake_object(e, ty_strip_null(expected)->cls);
            return;
        }
        if (expected && (ty_is_any(expected) || (expected->kind == TY_NULLABLE && ty_is_any(expected->elem)))) {
            /* { a: 1 } where an any goes: a Record<string, any> (rule 2 of any) */
            xs_note_dynamic_record(e->pos);
            check_expr(e, record_of_any());
            to_any(e);
            return;
        }
        if (e->spread && objlit_expand(e, expected)) return;
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
            bool any_lit = false; /* (a union told apart by a kind field never falls back to keys: a wrong kind is an error) */
            for (int i = 0; i < t->nparams; i++) {
                Type *m = t->params[i];
                if (m->kind != TY_OBJECT || m->cls->builtin) continue;
                for (int k = 0; k < m->cls->nfields; k++) if (m->cls->fields[k].type->kind == TY_STRING && m->cls->fields[k].type->tname) any_lit = true;
            }
            if (!pick && !any_lit) { /* by its keys: the one member that has a field for every key given and gets every field it
                            requires ({ provide, useValue } is a ValueProvider, { provide, useFactory } a FactoryProvider) */
                int nfit = 0;
                Type *fit = NULL;
                for (int i = 0; i < t->nparams; i++) {
                    Type *m = t->params[i];
                    if (m->kind != TY_OBJECT || m->cls->builtin || m->cls->ctor) continue;
                    bool ok = true;
                    for (int j = 0; j < e->args.len && ok; j++) {
                        bool has = false;
                        for (int k = 0; k < m->cls->nfields; k++) if (strcmp(m->cls->fields[k].name, e->keys[j]) == 0) has = true;
                        if (!has) ok = false;
                    }
                    for (int k = 0; k < m->cls->nfields && ok; k++) {
                        Field *fd = &m->cls->fields[k];
                        if (fd->optional || fd->init) continue;
                        bool given = false;
                        for (int j = 0; j < e->args.len; j++) if (strcmp(e->keys[j], fd->name) == 0) given = true;
                        if (!given) ok = false;
                    }
                    if (ok) { nfit++; fit = m; }
                }
                if (nfit == 1) pick = fit;
            }
            if (!pick) { /* Record<string, V> | unknown[]: an object literal is the one Record (it is never an array) */
                int nrec = 0;
                Type *rec = NULL;
                for (int i = 0; i < t->nparams; i++)
                    if ((t->params[i]->kind == TY_MAP && t->params[i]->record) || t->params[i]->kind == TY_JSON) { nrec++; rec = t->params[i]; }
                if (nrec == 1) pick = rec;
            }
            if (!pick) xs_error(XS2014, e->pos, "give the member's kind field its literal value", "cannot tell which member of %s this object is", ty_name(t));
            t = pick;
        }
        if (t && t->kind == TY_JSON) { anon_objlit(e); return; }
        if (t && t->kind == TY_UNKNOWN && !ty_is_any(t) && !e->spread) { /* { a: 1 } where an unknown goes (a Record<string, unknown>'s value): a Record of its own */
            static Type *rec_unknown;
            if (!rec_unknown) { rec_unknown = xs_alloc(sizeof *rec_unknown); *rec_unknown = *ty_map_of(ty_string, ty_unknown); rec_unknown->record = true; }
            t = rec_unknown;
        }
        if (t && t->kind == TY_MAP && t->record) { /* a Record<K, V> written as { a: 1, b: 2 }: new Map([["a", 1], ["b", 2]]) */
            Expr *arr = xs_alloc(sizeof *arr);
            arr->kind = E_ARRAY_LIT; arr->pos = e->pos; arr->field = -1;
            for (int i = 0; i < e->args.len; i++) {
                Expr *pair = xs_alloc(sizeof *pair);
                pair->kind = E_ARRAY_LIT; pair->pos = e->args.items[i]->pos; pair->field = -1;
                Expr *k = xs_alloc(sizeof *k);
                k->kind = E_STRING; k->pos = pair->pos; k->field = -1; k->str = e->keys[i];
                expr_list_push(&pair->args, k);
                expr_list_push(&pair->args, e->args.items[i]);
                expr_list_push(&arr->args, pair);
            }
            e->kind = E_NEW_MAP;
            e->elem_type = t;
            e->args = (ExprList){0};
            e->keys = NULL;
            if (arr->args.len) expr_list_push(&e->args, arr);
            check_expr(e, t);
            e->type = t;
            return;
        }
        if (!t) { infer_objlit(e); return; }
        if (t->kind != TY_OBJECT)
            xs_error(XS3006, e->pos, "give it a type: const t: Todo = { ... };  (declare Todo with interface or class)", "an object literal needs a declared type here");
        Class *c = t->cls;
        if (c->builtin) xs_error(XS8002, e->pos, NULL, "%s cannot be built from an object literal", c->name);
        if (c->ctor && c->nmethods > 0) /* (a class of data whose constructor copies a value of its own type in, new PageDto<T>({ ... }):
                                               the literal is that value, as TypeScript reads it) */
            xs_error(XS8008, e->pos, xs_sprintf("write new %s(...)", c->name), "%s has a constructor; build it with new", c->name);
        bool *given = xs_alloc(sizeof(bool) * (size_t)(c->nfields + 1));
        for (int i = 0; i < e->args.len; i++) {
            int idx = -1;
            for (int k = 0; k < c->nfields; k++) if (strcmp(c->fields[k].name, e->keys[i]) == 0) idx = k;
            if (idx < 0) {
                int n; const char **cands = member_names(c, &n);
                xs_error(XS8012, e->args.items[i]->pos, xs_help_suggest(e->keys[i], cands, n, NULL), "%s has no field '%s'", c->name, e->keys[i]);
            }
            if (given[idx]) xs_error(XS9024, e->args.items[i]->pos, NULL, "field '%s' is given twice", e->keys[i]);
            given[idx] = true;
            check_expr(e->args.items[i], c->fields[idx].type);
            expect_type(e->args.items[i], c->fields[idx].type, xs_sprintf("field '%s'", e->keys[i]));
        }
        bool as_cast = objlit_as_cast > 0;
        objlit_as_cast = 0; /* (only the literal the cast holds, not the ones inside it) */
        for (int k = 0; k < c->nfields; k++)
            if (!given[k] && !c->fields[k].init && !c->fields[k].optional && as_cast && ty_is_heap(c->fields[k].type) && c->fields[k].type->kind != TY_NULLABLE)
                xs_warn_rule(XS2108, e->pos, "give it a value, or declare it optional", "field '%s' of %s is left out by 'as': it is null (checked at run time where it is read)", c->fields[k].name, c->name);
            else if (!given[k] && !c->fields[k].init && !c->fields[k].optional)
                xs_error(XS9028, e->pos, xs_sprintf("add %s: ... (or declare it optional: %s?: %s)", c->fields[k].name, c->fields[k].name, ty_name(c->fields[k].type)),
                         "missing field '%s' of %s", c->fields[k].name, c->name);
        e->cls = c;
        e->type = t;
        return;
    }
    case E_ARROW: {
        Arrow *a = e->arrow;
        Func *f = a->fn;
        Type *want = expected && expected->kind == TY_FUNC ? expected :
                     expected && expected->kind == TY_NULLABLE && expected->elem->kind == TY_FUNC ? expected->elem : NULL; /* onX?: (t: T) => void */
        if (!want && expected && (expected->kind == TY_UNION || (expected->kind == TY_NULLABLE && expected->elem->kind == TY_UNION))) {
            /* Promise<T> | ((sinal: Sinal) => Promise<T>): the union's one function type types the arrow */
            Type *u = expected->kind == TY_NULLABLE ? expected->elem : expected;
            int nf = 0;
            for (int i = 0; i < u->nparams; i++) if (u->params[i]->kind == TY_FUNC) { want = u->params[i]; nf++; }
            if (nf != 1) want = NULL;
        }
        if (want && want->rest && want->nparams == 1 && f->nparams >= 1 && !f->params[f->nparams - 1].rest &&
            !f->param_decls.len && f->body && f->body->kind == S_BLOCK) {
            /* (a: A, b: B) => ... where (...args: T[]) => R is expected (Nest's useFactory with inject, a variadic
               callback): TypeScript accepts it. The arrow takes the one array the callers pass, and its named
               parameters become locals read from it on entry (a checked conversion when the element type is
               any or unknown) */
            Type *elem = want->params[0] && want->params[0]->kind == TY_ARRAY ? want->params[0]->elem : ty_unknown;
            StmtList nl = {0};
            char *arr = xs_sprintf("__args%d", ++prog->narrows);
            for (int i = 0; i < f->nparams; i++) {
                Param *pp = &f->params[i];
                Type *pt = pp->type ? resolve(pp->type, e->pos) : elem;
                Expr *ai = xs_alloc(sizeof *ai);
                ai->kind = E_IDENT; ai->pos = e->pos; ai->field = -1; ai->str = arr;
                Expr *ix = xs_alloc(sizeof *ix);
                ix->kind = E_NUMBER; ix->pos = e->pos; ix->field = -1; ix->num = i; ix->ival = i; ix->is_int_lit = true;
                Expr *rd = xs_alloc(sizeof *rd);
                rd->kind = E_INDEX; rd->pos = e->pos; rd->field = -1; rd->lhs = ai; rd->rhs = ix;
                Expr *init = rd;
                if (!ty_equal(pt, elem)) {
                    init = xs_alloc(sizeof *init);
                    init->kind = E_AS; init->pos = e->pos; init->field = -1; init->lhs = rd; init->elem_type = pt;
                }
                Stmt *v = xs_alloc(sizeof *v);
                v->kind = S_VAR; v->pos = e->pos; v->name = pp->name; v->decl_type = pt; v->init = init;
                stmt_list_push(&nl, v);
            }
            for (int k = 0; k < f->body->stmts.len; k++) stmt_list_push(&nl, f->body->stmts.items[k]);
            f->body->stmts = nl;
            Param *ps = xs_alloc(sizeof(Param));
            memset(ps, 0, sizeof(Param));
            ps->name = arr; ps->type = want->params[0]; ps->rest = true;
            f->params = ps;
            f->nparams = 1;
        }
        if (want && want->nparams > f->nparams && !f->param_decls.len) { /* (item) => ... where (item, index) is expected:
                                                                              JavaScript drops the extra arguments; here the
                                                                              arrow takes them, unnamed, so the call matches */
            Param *ps = xs_alloc(sizeof(Param) * (size_t)want->nparams);
            memcpy(ps, f->params, sizeof(Param) * (size_t)f->nparams);
            for (int i = f->nparams; i < want->nparams; i++) {
                memset(&ps[i], 0, sizeof(Param));
                ps[i].name = xs_sprintf("__unused%d", i);
            }
            f->params = ps;
            f->nparams = want->nparams;
        }
        if (want && want->nparams != f->nparams)
            xs_error(XS8145, e->pos, NULL, "this arrow function takes %d parameter(s), but %d are expected here", f->nparams, want->nparams);
        for (int i = 0; i < f->nparams; i++) {
            if (f->params[i].type) f->params[i].type = resolve(f->params[i].type, e->pos);
            else if (want) f->params[i].type = want->params[i];
            else xs_error(XS3014, e->pos, "write the type: (x: number) => ...", "parameter '%s' needs a type here", f->params[i].name);
            if (want && !ty_equal(f->params[i].type, want->params[i])) {
                Type *dt = f->params[i].type, *wt = want->params[i];
                /* a parameter written wider than what callers pass ((data: string | null) or (_: unknown) where a
                   string comes): TypeScript accepts it (parameters are contravariant). The arrow takes what is
                   passed, and the body sees a local of the written type, converted on entry. */
                /* (also a shape with fewer fields: ({ obj, key }: { obj: Record<string, unknown>; key: string }) => ...
                   where class-transformer passes TransformFnParams: the local is a copy of those fields) */
                bool wider = (dt->kind == TY_UNKNOWN || ty_is_any(dt) || (dt->kind == TY_NULLABLE && ty_equal(dt->elem, wt)) ||
                              (dt->kind == TY_OBJECT && wt->kind == TY_OBJECT && shape_ok(wt, dt)) ||
                              /* (o: MyDto) => ... where class-validator's ValidateIf passes any: converted on entry,
                                 checked at run time (a class only: an any holding another class is not copied
                                 into a shape yet) */
                              (dt->kind == TY_OBJECT && ty_is_any(wt) && !dt->cls->is_interface && !made_shape(dt->cls)))
                             && wt->kind != TY_VOID && f->body && f->body->kind == S_BLOCK && !f->params[i].rest && !f->params[i].is_prop;
                if (!wider && dt->kind == TY_FUNC && wt->kind == TY_FUNC && dt->nparams == wt->nparams && ty_equal(dt->ret, wt->ret)) {
                    /* (done: (err: Error | null, key?: any) => void) where (err: any, key?: any) => void is passed
                       (passport's callbacks): the body calls done with a reference where the function takes any;
                       a reference held as unknown is the same pointer, so the one closure serves both */
                    bool same = true;
                    for (int k = 0; k < dt->nparams && same; k++) {
                        Type *a = dt->params[k], *b = wt->params[k];
                        if (ty_equal(a, b)) continue;
                        if ((ty_is_any(b) || b->kind == TY_UNKNOWN) && (ty_is_heap(a) || ty_is_any(a) || a->kind == TY_UNKNOWN)) continue;
                        same = false;
                    }
                    if (same) {
                        f->params[i].type = wt;
                        continue;
                    }
                }
                if (!wider && wt->kind == TY_NULLABLE && ty_equal(wt->elem, dt) && ty_is_heap(dt)) {
                    /* (data: string) where string | null is passed (Nest's param decorators: data is undefined without
                       an argument): TypeScript's parameter bivariance; a reference is laid out the same either way */
                    continue;
                }
                if (!wider)
                    xs_error(XS8120, e->pos, NULL, "parameter '%s' is %s, but %s is expected here", f->params[i].name, ty_name(dt), ty_name(wt));
                Stmt *v = xs_alloc(sizeof *v);
                v->kind = S_VAR; v->pos = e->pos; v->name = f->params[i].name; v->decl_type = dt;
                Expr *src = xs_alloc(sizeof *src);
                src->kind = E_IDENT; src->pos = e->pos; src->field = -1;
                src->str = xs_sprintf("__wide%d_%s", i, f->params[i].name);
                v->init = src;
                f->params[i].name = src->str;
                f->params[i].type = wt;
                StmtList nl = {0};
                stmt_list_push(&nl, v);
                for (int k = 0; k < f->body->stmts.len; k++) stmt_list_push(&nl, f->body->stmts.items[k]);
                f->body->stmts = nl;
            }
        }
        if (f->ret) f->ret = resolve(f->ret, e->pos);
        else if (want) f->ret = want->ret;
        { bool as = false; f->ret = ret_unpromise(f->ret, &as); if (as) f->ret_promise = true; }
        if (f->is_async || (want && want->async_ret)) f->ret_promise = true;
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
            const char *prev = f->params[i].cname; /* checked before (a retry with another expected type): the body
                                                      already reads the parameter by that name */
            Binding *pb = declare(e->pos, f->params[i].name, f->params[i].type, false, false, true);
            if (prev && pb->plain) {
                pb->plain = (char *)prev;
                pb->cell = xs_sprintf("%s_c", prev);
                pb->cname = xs_sprintf("(%s->v)", pb->cell);
            } else if (prev) pb->cname = (char *)prev;
            f->params[i].cname = pb->plain ? pb->plain : pb->cname;
            f->params[i].cell = pb->cell;
        }
        if (!f->ret) {
            /* infer from the body: an expression body takes the expression's type; a block needs an annotation unless it never returns a value */
            if (a->expr_body) { /* ([k, v]) => ...: the reads of a destructured parameter come first */
                for (int i = 0; i + 1 < f->body->stmts.len; i++) check_stmt(f->body->stmts.items[i]);
                Stmt *r = f->body->stmts.items[f->body->stmts.len - 1];
                check_expr(r->expr, NULL);
                if (f->ret_promise && r->expr->type->kind == TY_PROMISE) pr_await(r->expr);
                settle(r->expr);
                f->ret = r->expr->type;
                if (f->ret->kind == TY_VOID) { r->kind = S_EXPR; }
                else if (f->ret->kind == TY_NULL) { /* () => undefined (a .catch that swallows): it gives undefined */
                    r->expr->type = ty_undef;
                    f->ret = ty_undef;
                }
            } else { /* a block: the type its returns agree on (void when none returns a value) */
                ret_infer_begin(f);
                check_block(f->body);
                ret_infer_end(f, e->pos);
                if (f->ret->kind != TY_VOID && !always_returns(f->body))
                    xs_error(XS8144, e->pos, "add a return at the end", "this arrow function may reach its end without returning a %s", ty_name(f->ret));
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
                    xs_error(XS8144, e->pos, "add a return at the end", "this arrow function may reach its end without returning a %s", ty_name(f->ret));
            }
        }
        pop_scope();
        cur_func = saved_func;
        loop_depth = saved_loop;
        switch_depth = saved_switch;
        finally_depth = saved_fin;
        Type **ps = xs_alloc(sizeof(Type *) * (size_t)(f->nparams ? f->nparams : 1));
        for (int i = 0; i < f->nparams; i++) {
            ps[i] = f->params[i].type;
            if (want && i < want->nparams && want->params[i]->kind == TY_NULLABLE && ty_is_heap(ps[i]) && ty_equal(want->params[i]->elem, ps[i]))
                ps[i] = want->params[i]; /* (data: string) taking string | null (see above) */
        }
        e->type = ty_func(ps, f->nparams, f->ret);
        e->type->rest = f->nparams && f->params[f->nparams - 1].rest;
        e->type->async_ret = f->ret_promise;
        e->type->gen_sig = a->gen_sig; /* <T>(..) => ...: its signature as written (calls with type arguments read it) */
        return;
    }
    case E_NEW_MAP:
        if (!e->elem_type) infer_new_collection(e, expected);
        e->type = resolve(e->elem_type, e->pos);
        check_type_supported(e->pos, e->type);
        if (e->type->kind == TY_CHAN) {
            if (e->args.len > 1) xs_error(XS7067, e->pos, NULL, "new Channel<T>(capacity) takes at most one argument");
            if (e->args.len == 1) { check_expr(e->args.items[0], ty_int); expect_type(e->args.items[0], ty_int, "channel capacity"); }
            Type *el = e->type->elem;
            if (el->kind == TY_VOID || el->kind == TY_FUNC || el->kind == TY_NULLABLE || el->kind == TY_NULL)
                xs_error(XS6040, e->pos, NULL, "unsupported channel element type '%s'", ty_name(el));
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
            if (!e->args.items[0]->type) check_expr(e->args.items[0], want); /* (checked already when it gave the type) */
            Expr *src = e->args.items[0];
            if ((is_map && src->type->kind == TY_MAP && !src->type->record) || (!is_map && src->type->kind == TY_SET)) {
                /* new Map(otherMap) / new Set(otherSet) (and Object.fromEntries(map)): a copy, [...other] */
                Expr *sp = xs_alloc(sizeof *sp);
                sp->kind = E_UNARY; sp->op = TK_ELLIPSIS; sp->pos = src->pos; sp->field = -1; sp->lhs = src;
                src->prechecked = true;
                Expr *lit = xs_alloc(sizeof *lit);
                lit->kind = E_ARRAY_LIT; lit->pos = src->pos; lit->field = -1; lit->spread = true;
                expr_list_push(&lit->args, sp);
                e->args.items[0] = lit;
                check_expr(lit, want);
            }
            expect_type(e->args.items[0], want, is_map ? "the entries of new Map" : "the values of new Set");
        }
        return;
    case E_NEW_OBJ: {
        if (e->str && strcmp(e->str, "Promise") == 0 && !find_class("Promise")) { check_pr_new(e, expected); return; }
        Class *c = e->cls && e->cls->adapter_of ? e->cls : find_class(e->str); /* (an adapter has no name to find) */
        if (!c) {
            int n; const char **cands = class_names(&n);
            if (missing_name(e->str)) xs_error_quiet();
            xs_error(XS9031, e->pos, xs_help_suggest(e->str, cands, n, "declare it: class Name { ... }"), "unknown class '%s'", e->str);
        }
        if (c->gen_state) c = generic_new(e, c, expected);
        else if (e->ntargs) xs_error(XS6005, e->pos, NULL, "'%s' is not generic", c->name);
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
                if (e->args.len < 1 || e->args.len > 2) xs_error(XS7043, e->pos, hint, "Worker takes the program's file and its options");
                Expr *f = e->args.items[0];
                if (!(f->kind == E_IDENT && f->str && strcmp(f->str, "__filename") == 0))
                    xs_error(XS7044, f->pos, hint, "a worker runs this program again: its file must be __filename");
                Expr *data = NULL;
                if (e->args.len == 2) {
                    Expr *o = e->args.items[1];
                    if (o->kind != E_OBJLIT) xs_error(XS7103, o->pos, hint, "the options are an object literal");
                    for (int k = 0; k < o->args.len; k++) {
                        if (strcmp(o->keys[k], "workerData") != 0) xs_error(XS7101, o->args.items[k]->pos, hint, "the only option is workerData");
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
                if (e->args.len != 1) xs_error(XS7009, e->pos, "new DatabaseSync(\"app.db\") or new DatabaseSync(\":memory:\")", "DatabaseSync takes a path");
                check_expr(e->args.items[0], ty_string);
                expect_type(e->args.items[0], ty_string, "database path");
                e->builtin.cfn = "xs_DatabaseSync_new";
                prog->uses_sqlite = true;
                return;
            }
            if (strcmp(c->name, "Uint8Array") == 0) { /* new Uint8Array(length | int[] | number[] | bytes | sab, offset?, length?) */
                if (e->args.len < 1 || e->args.len > 3) xs_error(XS7077, e->pos, "new Uint8Array(16), new Uint8Array([1, 2, 3]), new Uint8Array(sharedBuffer)", "new Uint8Array takes one argument (or a SharedArrayBuffer, an offset and a length)");
                check_expr(e->args.items[0], e->args.items[0]->kind == E_ARRAY_LIT ? ty_array_of(ty_int) : NULL);
                Type *st = e->args.items[0]->type;
                if (e->args.len > 1 && !is_sab(st)) xs_error(XS7045, e->pos, "new Uint8Array(sharedBuffer, byteOffset, length)", "an offset and a length go with a SharedArrayBuffer");
                if (is_sab(st)) { /* a view on the shared bytes (runtime: xs_u8_on); a message carries it as the block */
                    for (int k = 1; k < e->args.len; k++) int_arg(&e->args.items[k], k == 1 ? "the byte offset" : "the length");
                    for (int k = e->args.len; k < 3; k++) expr_list_push(&e->args, num_lit(e->pos, (double)XS_NOT_GIVEN, true));
                    e->builtin.cfn = "xs_u8_on";
                    return;
                }
                if (st->kind == TY_NUMBER) { wrap_call(e->args.items[0], "xs_num_key", ty_int); st = ty_int; } /* new Uint8Array(n), n a whole number (checked) */
                if (st->kind == TY_INT || st->kind == TY_LITINT) { if (st->kind == TY_LITINT) coerce_lit(e->args.items[0], ty_int); e->builtin.cfn = "xs_u8_new"; }
                else if (is_bytes(st)) e->builtin.cfn = "xs_u8_copy";
                else if (st->kind == TY_ARRAY && (st->elem->kind == TY_INT || st->elem->kind == TY_LITINT)) e->builtin.cfn = "xs_u8_from_ints";
                else if (st->kind == TY_ARRAY && st->elem->kind == TY_NUMBER) e->builtin.cfn = "xs_u8_from_nums";
                else xs_error(XS7076, e->args.items[0]->pos, NULL, "new Uint8Array takes a length, an int[] or number[], or bytes, not %s", ty_name(st));
                return;
            }
            if (strcmp(c->name, "SharedArrayBuffer") == 0) { /* new SharedArrayBuffer(bytes): zeroed, shared by postMessage */
                if (e->args.len != 1) xs_error(XS7075, e->pos, "new SharedArrayBuffer(8 * n)", "new SharedArrayBuffer takes its length in bytes");
                int_arg(&e->args.items[0], "the length of a SharedArrayBuffer");
                e->builtin.cfn = "xs_sab_new";
                return;
            }
            if (strcmp(c->name, "Float64Array") == 0) { /* new Float64Array(length | number[] | int[] | Float64Array | sab, offset?, length?) */
                const char *hint = "new Float64Array(n), new Float64Array([1.5, 2]), new Float64Array(sharedBuffer)";
                if (e->args.len < 1 || e->args.len > 3) xs_error(XS7071, e->pos, hint, "new Float64Array takes one argument (or a SharedArrayBuffer, an offset and a length)");
                check_expr(e->args.items[0], e->args.items[0]->kind == E_ARRAY_LIT ? ty_array_of(ty_number) : NULL);
                Type *st = e->args.items[0]->type;
                if (e->args.len > 1 && !is_sab(st)) xs_error(XS7045, e->pos, hint, "an offset and a length go with a SharedArrayBuffer");
                if (st->kind == TY_INT || st->kind == TY_LITINT) { if (st->kind == TY_LITINT) coerce_lit(e->args.items[0], ty_int); e->builtin.cfn = "xs_f64_new"; }
                else if (is_f64(st)) e->builtin.cfn = "xs_f64_copy";
                else if (is_sab(st)) {
                    for (int k = 1; k < e->args.len; k++) int_arg(&e->args.items[k], k == 1 ? "the byte offset" : "the length");
                    for (int k = e->args.len; k < 3; k++) expr_list_push(&e->args, num_lit(e->pos, (double)XS_NOT_GIVEN, true));
                    e->builtin.cfn = "xs_f64_on";
                }
                else if (st->kind == TY_ARRAY && st->elem->kind == TY_NUMBER) e->builtin.cfn = "xs_f64_from_nums";
                else if (st->kind == TY_ARRAY && (st->elem->kind == TY_INT || st->elem->kind == TY_LITINT)) e->builtin.cfn = "xs_f64_from_ints";
                else xs_error(XS7070, e->args.items[0]->pos, hint, "new Float64Array takes a length, a number[], a Float64Array or a SharedArrayBuffer, not %s", ty_name(st));
                return;
            }
            if (strcmp(c->name, "TextEncoder") == 0 || strcmp(c->name, "TextDecoder") == 0) {
                if (e->args.len > (c->name[4] == 'D' ? 1 : 0)) xs_error(XS7065, e->pos, NULL, "new %s() takes no arguments (UTF-8 only)", c->name);
                if (e->args.len == 1) { check_expr(e->args.items[0], ty_string); expect_type(e->args.items[0], ty_string, "the encoding"); e->args.len = 0; }
                e->builtin.cfn = c->name[4] == 'E' ? "xs_TextEncoder_new" : "xs_TextDecoder_new";
                return;
            }
            if (strcmp(c->name, "Buffer") == 0) xs_error(XS7066, e->pos, "use Buffer.from(...) or Buffer.alloc(n)", "new Buffer() is deprecated (and not supported)");
            if (strcmp(c->name, "Date") == 0) { /* new Date(), new Date(ms | text | date), new Date(y, m, d?, h?, mi?, s?, ms?) */
                int n = e->args.len;
                if (n == 0) { e->builtin.cfn = "xs_Date_new_now"; return; }
                if (n == 1) {
                    Expr *a = e->args.items[0];
                    check_expr(a, NULL);
                    if (a->type->kind == TY_STRING) { e->builtin.cfn = "xs_Date_new_str"; return; }
                    if (ty_is_any(a->type)) { e->builtin.cfn = "xs_Date_new_dyn"; return; } /* (a Date, a string or a number, at run time) */
                    if (a->type->kind == TY_UNION || a->type->kind == TY_UNKNOWN || (a->type->kind == TY_NULLABLE && a->type->elem->kind != TY_NUMBER)) {
                        /* new Date(v) with v: Date | string (or null: new Date(null) is the epoch, as in JS): by what v holds */
                        to_any(a);
                        e->builtin.cfn = "xs_Date_new_dyn";
                        return;
                    }
                    if (a->type->kind == TY_OBJECT && a->type->cls == c) { e->builtin.cfn = "xs_Date_new_copy"; return; }
                    date_num_arg(&e->args.items[0], "the time value of new Date");
                    e->builtin.cfn = "xs_Date_new_ms";
                    return;
                }
                if (n > 7) xs_error(XS7068, e->pos, NULL, "new Date takes at most 7 arguments");
                for (int k = 0; k < n; k++) date_num_arg(&e->args.items[k], xs_sprintf("argument %d of new Date", k + 1));
                for (int k = n; k < 7; k++) expr_list_push(&e->args, num_lit(e->pos, k == 2 ? 1 : 0, false));
                e->builtin.cfn = "xs_Date_new_parts";
                return;
            }
            if (strcmp(c->name, "RegExp") == 0) { /* new RegExp(pattern, flags = "") */
                if (e->args.len < 1 || e->args.len > 2) xs_error(XS7073, e->pos, "new RegExp(\"a+b\", \"gi\")", "new RegExp takes a pattern and optional flags");
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
                /* new Error(message, { cause }): the cause is kept as err.cause, as in JS (other options are ignored) */
                Expr *cause = NULL;
                if (e->args.len == 2 && e->args.items[1]->kind == E_OBJLIT) {
                    Expr *o = e->args.items[1];
                    for (int k = 0; k < o->args.len; k++)
                        if (o->keys && o->keys[k] && strcmp(o->keys[k], "cause") == 0) cause = o->args.items[k];
                    e->args.len = 1;
                }
                if (e->args.len > 1) xs_error(XS7069, e->pos, NULL, "new Error() takes a message and an options object ({ cause })");
                if (e->args.len == 0) expr_list_push(&e->args, str_lit(e->pos, "")); /* new Error(): message "" */
                if (e->args.len == 1) { check_expr(e->args.items[0], ty_string); expect_type(e->args.items[0], ty_string, "the Error message"); }
                if (cause) {
                    check_expr(cause, ty_unknown);
                    expect_type(cause, ty_unknown, "the Error cause");
                    expr_list_push(&e->args, cause);
                    e->builtin.cfn = "xs_Error_new_cause";
                    return;
                }
                e->builtin.cfn = "xs_Error_new";
                return;
            }
            if (strcmp(c->name, "Server") != 0)
                xs_error(XS8003, e->pos, NULL, "%s cannot be constructed directly", c->name);
            if (e->args.len != 0) xs_error(XS7074, e->pos, NULL, "new Server() takes no arguments");
            e->builtin.cfn = "xs_Server_new";
            prog->uses_net = true;
            return;
        }
        if (c->is_interface) xs_error(XS9005, e->pos, xs_sprintf("write an object literal: const x: %s = { ... };", c->name), "'%s' is an interface; it has no constructor", c->name);
        bool all_definite = c->data_only && c->nmethods > 0; /* client!: Redis; set later (by init(), a framework hook): new is allowed */
        for (int k = 0; k < c->nfields && all_definite; k++)
            if (!c->fields[k].init && !c->fields[k].optional && !c->fields[k].definite) all_definite = false;
        if (c->data_only && !all_definite) xs_error(XS8009, e->pos, "give its fields initializers (or a constructor that sets them) to build it with new", "%s has fields without a value: it is filled from JSON, not constructed", c->name);
        if (c->is_abstract) xs_error(XS8017, e->pos, "make an instance of a class that extends it", "%s is abstract", c->name);
        if (eff_ctor(c)) {
            check_args(e, eff_ctor(c), xs_sprintf("the constructor of %s", c->name));
        } else if (e->args.len != 0) {
            xs_error(XS8010, e->pos, NULL, "%s has no constructor, so 'new %s()' takes no arguments", c->name, c->name);
        }
        return;
    }
    case E_ARRAY_LIT: {
        if (expected && expected->kind == TY_NULLABLE) expected = expected->elem;
        if (expected && expected->kind == TY_UNKNOWN && !ty_is_any(expected) && e->args.len) {
            /* [name, date, 3] where an unknown goes: an unknown[] (each element as what it is) */
            static Type *arr_unknown;
            if (!arr_unknown) arr_unknown = ty_array_of(ty_unknown);
            expected = arr_unknown;
        }
        if (expected && expected->kind == TY_UNION) { /* string | string[]: the literal is the array member */
            Type *arr = NULL;
            for (int i = 0; i < expected->nparams; i++) if (expected->params[i]->kind == TY_ARRAY && !arr) arr = expected->params[i];
            expected = arr;
        }
        if (!expected && e->want_tuple && e->args.len == 2) { /* new Map(xs.map((x) => [k, v])) */
            Type *ps[2];
            for (int k = 0; k < 2; k++) {
                Expr *x = e->args.items[k];
                check_expr(x, NULL);
                settle(x);
                if (x->type->kind == TY_LITINT) coerce_lit(x, k == 0 ? ty_int : ty_number);
                ps[k] = x->type;
                x->prechecked = true; /* (checked once: the tuple below takes it as it is) */
                if (ps[k]->kind == TY_NULL || ps[k]->kind == TY_VOID) xs_error(XS3006, x->pos, "write the types: new Map<K, V>(...)", "cannot infer the type of null here");
            }
            expected = ty_object_of(tuple_class(ps, 2, e->pos));
        }
        if (expected && expected->kind == TY_OBJECT && expected->cls->is_tuple) { /* [x, y] as a tuple: its fields */
            Class *tc = expected->cls;
            if (e->args.len != tc->nfields) xs_error(XS8142, e->pos, NULL, "the tuple %s has %d elements, but this gives %d", tc->name, tc->nfields, e->args.len);
            e->kind = E_OBJLIT;
            e->keys = xs_alloc(sizeof(char *) * (size_t)tc->nfields);
            for (int k = 0; k < tc->nfields; k++) e->keys[k] = tc->fields[k].name;
            check_expr(e, expected);
            return;
        }
        Type *elem = expected && expected->kind == TY_ARRAY ? expected->elem : NULL;
        /* [{ a: 1 }, { a: 2 }] where an object goes: each element an object too (their literals need no declared type) */
        if (expected && expected->kind == TY_JSON && !e->spread && e->args.len > 0) {
            bool lits = false;
            for (int i = 0; i < e->args.len; i++) if (e->args.items[i]->kind == E_OBJLIT || e->args.items[i]->kind == E_ARRAY_LIT) lits = true;
            if (lits) elem = ty_json;
        }
        if (e->spread) { check_spread_lit(e, elem); return; }
        if (e->args.len == 0) {
            if (!elem && expected && (ty_is_any(expected) || expected->kind == TY_UNKNOWN)) { /* x ?? [] with x an any: unknown[] */
                e->type = ty_array_of(ty_unknown);
                return;
            }
            if (!elem) xs_error(XS3008, e->pos, "give it a type: const xs: number[] = [];", "cannot infer the type of an empty array");
            e->type = expected;
            check_type_supported(e->pos, e->type);
            return;
        }
        for (int i = 0; i < e->args.len; i++) check_expr(e->args.items[i], elem);
        if (!elem) { /* [id, 'x', 3]: values of several kinds, an unknown[] (TypeScript: (string | number)[]) */
            int ks = 0, kn = 0, kb = 0, ko = 0, ku = 0;
            for (int i = 0; i < e->args.len; i++) {
                Type *t = e->args.items[i]->type;
                if (t->kind == TY_NULL) continue;
                t = ty_strip_null(t);
                if (t->kind == TY_STRING) ks = 1;
                else if (is_numeric(t) || t->kind == TY_LITINT) kn = 1;
                else if (t->kind == TY_BOOLEAN) kb = 1;
                else if (t->kind == TY_UNKNOWN && !ty_is_any(t)) ku = 1;
                else ko = 1;
            }
            if ((ks + kn + kb > 1 || (ku && ks + kn + kb > 0)) && !ko) {
                static Type *arr_unknown2;
                if (!arr_unknown2) arr_unknown2 = ty_array_of(ty_unknown);
                for (int i = 0; i < e->args.len; i++) expect_type(e->args.items[i], ty_unknown, "array element");
                e->type = arr_unknown2;
                return;
            }
        }
        if (!elem) { /* [{ term: {...} }, { range: {...} }]: objects of different shapes (a search query's clauses): an
                        object[], each element as JSON (TypeScript: a union of the shapes) */
            bool objs = e->args.len > 1, differ = false;
            bool ju = json_ok_unknown;
            json_ok_unknown = true;
            for (int i = 0; i < e->args.len && objs; i++) {
                Type *t = e->args.items[i]->type;
                if (t->kind != TY_OBJECT || t->cls->builtin || t->cls->is_tuple || !json_ok(t, 0)) objs = false;
                else if (i && !ty_equal(t, e->args.items[0]->type)) differ = true;
            }
            json_ok_unknown = ju;
            if (objs && differ) {
                for (int i = 0; i < e->args.len; i++) expect_type(e->args.items[i], ty_json, "array element");
                e->type = ty_array_of(ty_json);
                return;
            }
        }
        if (!elem && e->args.len > 1) { /* [{ term: .. }, exato('runId', id)] with exato giving a Record<string, unknown>: the
                                            Record, the objects copied in (as in a spread literal) */
            Type *rec = NULL;
            bool fits = true;
            for (int i = 0; i < e->args.len; i++) {
                Type *t = e->args.items[i]->type;
                if (t->kind == TY_MAP && t->record && t->key->kind == TY_STRING && t->elem->kind == TY_UNKNOWN) { if (!rec) rec = t; }
                else if (!(t->kind == TY_OBJECT && plain_data(t->cls))) fits = false;
            }
            if (rec && fits) {
                for (int i = 0; i < e->args.len; i++) expect_type(e->args.items[i], rec, "array element");
                e->type = ty_array_of(rec);
                return;
            }
        }
        if (!elem && tsc_checked() && e->args.len > 2) { /* (two elements: a [key, value] pair, a tuple where one goes) */
            /* --tsc-checked: [date, 'x', n], [s, xs]: elements of kinds no one of them takes all the others (TypeScript:
               a union element type), an unknown[] (each element as what it is, checked where it is read) */
            Type *t0 = NULL;
            bool mixed = false;
            for (int i = 0; i < e->args.len && !mixed; i++) {
                Type *t = e->args.items[i]->type;
                if (t->kind == TY_NULL) continue;
                t = ty_strip_null(t);
                if (t->kind == TY_LITINT) t = ty_int;
                if (!t0) t0 = t;
                else if (!ty_equal(t, t0) && !(is_numeric(t) && is_numeric(t0))) mixed = true;
                if (t->kind == TY_UNION) mixed = true;
            }
            if (mixed) {
                static Type *arr_unknown3;
                if (!arr_unknown3) arr_unknown3 = ty_array_of(ty_unknown);
                for (int i = 0; i < e->args.len; i++) expect_type(e->args.items[i], ty_unknown, "array element");
                e->type = arr_unknown3;
                return;
            }
        }
        if (!elem) {
            for (int i = 0; i < e->args.len; i++)
                if (is_numeric(e->args.items[i]->type)) elem = e->args.items[i]->type;
            if (!elem) {
                settle(e->args.items[0]);
                elem = e->args.items[0]->type;
            }
            if (elem->kind != TY_NULLABLE && ty_is_heap(elem) && nullable_ok(elem)) { /* [`a=${x}`, c ? `b=${y}` : undefined]:
                                                (string | undefined)[], as TypeScript types it (no null check on each element) */
                bool nul = false;
                for (int i = 0; i < e->args.len; i++) {
                    Type *t = e->args.items[i]->type;
                    if (t->kind == TY_NULL || (t->kind == TY_NULLABLE && ty_equal(t->elem, elem))) nul = true;
                }
                if (nul) elem = ty_nullable_of(elem);
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
            if (ty_is_undef(part->type)) { undef_text(part); continue; } /* ${x} with x: undefined */
            if (ty_is_any(part->type)) { dyn_wrap(part, "xs_dyn_str", ty_string); continue; } /* ${x}: String(x) */
            if (part->type->kind == TY_UNION || part->type->kind == TY_UNKNOWN ||
                (part->type->kind == TY_NULLABLE && part->type->elem->kind == TY_UNION)) { /* ${x} with x: string | number: by what it holds */
                if (part->type->kind == TY_NULLABLE) to_unknown(part);
                dyn_wrap(part, "xs_dyn_str", ty_string);
                continue;
            }
            TypeKind k = part->type->kind;
            if (k == TY_NULLABLE) { /* ${x} with x: T | null: "null", or "undefined" for x?: T (the text JavaScript writes) */
                TypeKind ek = part->type->elem->kind;
                if (ek == TY_STRING || ek == TY_INT || ek == TY_NUMBER || ek == TY_BOOLEAN || ek == TY_ENUM) {
                    part->lit_id = intern(part->type->absent ? "undefined" : "null");
                    continue;
                }
            }
            if (k == TY_OBJECT && tostring_call(part)) continue;
            if (k != TY_STRING && k != TY_INT && k != TY_NUMBER && k != TY_BOOLEAN && k != TY_ENUM)
                xs_error(XS8098, part->pos, "template literals take strings, numbers, ints, booleans and enums; call a method that returns a string for the rest", "cannot put a %s inside a template literal", ty_name(part->type));
        }
        e->type = ty_string;
        return;
    case E_COND: {
        if (e->builtin.cfn && strcmp(e->builtin.cfn, "optchain") == 0) { check_optchain(e, expected); return; }
        /* typeof x.m === "string" ? x.m : "" with x any: the branch reads x.m as a string (checked), as TypeScript
           narrows it (then string methods can be called on the result) */
        const char *ty_path = NULL;
        Type *ty_to = NULL;
        {
            Expr *c = e->lhs;
            if (c && c->kind == E_BINARY && c->op == TK_EQ && c->lhs && c->lhs->kind == E_UNARY && c->lhs->op == TK_IDENT &&
                c->rhs && c->rhs->kind == E_STRING) {
                ty_to = !strcmp(c->rhs->str, "string") ? ty_string : !strcmp(c->rhs->str, "number") ? ty_number : !strcmp(c->rhs->str, "boolean") ? ty_boolean : NULL;
                if (ty_to) ty_path = member_path_text(c->lhs->lhs);
            }
        }
        const char *then_path = ty_path && e->args.len == 2 ? member_path_text(e->args.items[0]) : NULL;
        check_expr(e->lhs, NULL);
        if (e->lhs->type->kind != TY_BOOLEAN && cond_truthy_type(e->lhs->type)) make_truthy(e->lhs, "condition of '?:'");
        if (e->lhs->type->kind != TY_BOOLEAN)
            xs_error(XS8105, e->lhs->pos, "there is no truthiness in xisto; compare explicitly", "condition of '?:' must be boolean, but this is %s", ty_name(e->lhs->type));
        Expr *a = e->args.items[0], *b = e->args.items[1];
        bool undef_side = (a->kind == E_IDENT && a->str && !strcmp(a->str, "undefined") && !lookup("undefined")) ||
                          (b->kind == E_IDENT && b->str && !strcmp(b->str, "undefined") && !lookup("undefined"));
        {
            bool sv;
            if (static_bool(e->lhs, &sv)) { /* a condition the types decide: only the branch that runs is checked */
                Expr *live = sv ? a : b;
                check_expr(live, expected);
                *e = *live;
                return;
            }
        }
        {
            NSet ts = {0}, fs = {0};
            narrow_set(e->lhs, true, &ts);
            narrow_set(e->lhs, false, &fs);
            UNarrow un = union_narrow(e->lhs);
            /* c ? [x] : []: the empty array takes the type of the other branch */
            bool ea = !expected && a->kind == E_ARRAY_LIT && a->args.len == 0;
            bool eb = !expected && b->kind == E_ARRAY_LIT && b->args.len == 0;
            /* c ? Promise.resolve({ a: false }) : this.flags(): the literal takes the type of the other branch */
            #define PR_OBJLIT(x) ((x)->kind == E_OBJLIT || ((x)->kind == E_MEMBER && (x)->is_call && (x)->str && !strcmp((x)->str, "resolve") && \
                                  (x)->lhs && (x)->lhs->kind == E_IDENT && !strcmp((x)->lhs->str, "Promise") && (x)->args.len == 1 && \
                                  (x)->args.items[0]->kind == E_OBJLIT))
            bool la = !expected && PR_OBJLIT(a) && !PR_OBJLIT(b);
            #undef PR_OBJLIT
            if (ea && !eb) {
                push_scope();
                if (un.b) shadow_union(un.b, un.else_t);
                shadow_set(&fs);
                check_expr(b, NULL);
                pop_scope();
                check_expr(a, b->type->kind == TY_ARRAY ? b->type : NULL);
            } else if (la) {
                push_scope();
                if (un.b) shadow_union(un.b, un.else_t);
                shadow_set(&fs);
                check_expr(b, NULL);
                pop_scope();
                push_scope();
                if (un.b) shadow_union(un.b, un.then_t);
                shadow_set(&ts);
                Type *bt = b->type;
                Type *want_a = NULL;
                if (bt->kind == TY_PROMISE) { if (a->kind != E_OBJLIT && ty_strip_null(bt->elem)->kind == TY_OBJECT) want_a = bt; }
                else if (ty_strip_null(bt)->kind == TY_OBJECT) want_a = a->kind == E_OBJLIT ? bt : ty_promise_of(bt); /* (b an async call, read as its value) */
                check_expr(a, want_a);
                pop_scope();
            } else {
                push_scope();
                if (un.b) shadow_union(un.b, un.then_t);
                shadow_set(&ts);
                check_expr(a, expected);
                pop_scope();
                push_scope();
                if (un.b) shadow_union(un.b, un.else_t);
                shadow_set(&fs);
                check_expr(b, eb && a->type->kind == TY_ARRAY ? a->type : expected);
                pop_scope();
            }
        }
        if (ty_path && then_path && !strcmp(ty_path, then_path) && ty_is_any(a->type)) {
            Expr *in = xs_alloc(sizeof *in);
            *in = *a;
            memset(a, 0, sizeof *a);
            a->kind = E_AS; a->pos = in->pos; a->field = -1; a->lhs = in; a->elem_type = ty_to;
            in->prechecked = true;
            check_expr(a, ty_to);
        }
        if (a->type->kind == TY_LITINT && b->type->kind == TY_LITINT) {
            if (expected && is_numeric(expected)) { coerce_lit(a, expected); coerce_lit(b, expected); }
        } else if (a->type->kind == TY_LITINT) coerce_lit(a, is_numeric(ty_strip_null(b->type)) || b->type->kind == TY_ENUM ? b->type : ty_number);
        else if (b->type->kind == TY_LITINT) coerce_lit(b, is_numeric(ty_strip_null(a->type)) || a->type->kind == TY_ENUM ? a->type : ty_number);
        if (!ty_equal(a->type, b->type)) { /* T and null make T | null; two classes make their nearest common base */
            Type *ta = a->type, *tb = b->type, *u = NULL;
            bool nul = ta->kind == TY_NULL || tb->kind == TY_NULL || ta->kind == TY_NULLABLE || tb->kind == TY_NULLABLE;
            Type *ba = ta->kind == TY_NULLABLE ? ta->elem : ta, *bb = tb->kind == TY_NULLABLE ? tb->elem : tb;
            if (ba->kind == TY_NULL) ba = bb;
            if (bb->kind == TY_NULL) bb = ba;
            if (ba->kind != TY_NULL && ty_equal(ba, bb)) u = ba;
            else if (is_numeric(ba) && is_numeric(bb)) u = ty_number; /* Math.trunc(n) : null-or-number: a number */
            else if (ty_is_any(ta) || ty_is_any(tb)) { u = ty_any; nul = false; } /* data ? user[data] : user: an any (the other side read by name too) */
            else if (ta->kind == TY_UNKNOWN || tb->kind == TY_UNKNOWN) { u = ty_unknown; nul = false; } /* d ? new Date(d) : row.x: unknown, as TypeScript */
            else if (ba->kind == TY_OBJECT && bb->kind == TY_OBJECT && !ba->cls->is_interface && !bb->cls->is_interface)
                for (Class *c = ba->cls; c && !u; c = c->base) if (is_subclass(bb->cls, c)) u = ty_object_of(c);
            if (u && nul && (ty_is_any(u) || u->kind == TY_UNKNOWN)) nul = false; /* (an any or an unknown holds null too) */
            if (u && nul) {
                if (!nullable_ok(u)) u = NULL;
                else if (undef_side || ty_is_undef(ta) || ty_is_undef(tb)) u = ty_optional_of(u); /* c ? v : undefined: absent, not null (JSON leaves it out) */
                else u = ty_nullable_of(u);
            }
            if (!u && expected && (expected->kind == TY_UNION || (expected->kind == TY_NULLABLE && expected->elem->kind == TY_UNION))) u = expected; /* both go into the union */
            if (!u && cond_prim(ba) && cond_prim(bb)) { /* c ? 'a' : 1: string | number, as TypeScript */
                Type *un = xs_alloc(sizeof *un);
                un->kind = TY_UNION; un->nparams = 2;
                un->params = xs_alloc(sizeof(Type *) * 2);
                un->params[0] = ba->kind == TY_INT ? ty_number : ba; un->params[1] = bb->kind == TY_INT ? ty_number : bb;
                u = resolve(un, e->pos);
                if (nul && u->kind != TY_NULLABLE) u = ty_nullable_of(u);
            }
            if (!u) { /* c ? this.load() : Promise.resolve(null): Promise<T | null>, the null a resolved promise of it */
                Expr **pn = ta->kind == TY_PROMISE && ta->elem->kind == TY_NULL && tb->kind != TY_PROMISE ? &e->args.items[0] :
                            tb->kind == TY_PROMISE && tb->elem->kind == TY_NULL && ta->kind != TY_PROMISE ? &e->args.items[1] : NULL;
                Type *other = pn == &e->args.items[0] ? tb : ta;
                if (pn && other->kind != TY_VOID && other->kind != TY_NULL && (other->kind == TY_NULLABLE || nullable_ok(other))) {
                    Expr *z = xs_alloc(sizeof *z);
                    z->kind = E_NULL; z->pos = (*pn)->pos; z->field = -1; z->type = ty_null;
                    *pn = z;
                    if (pn == &e->args.items[0]) a = z; else b = z;
                    u = ty_promise_of(other->kind == TY_NULLABLE ? other : ty_nullable_of(other));
                }
            }
            if (!u) { /* c ? cached : this.load(): T and Promise<T> make Promise<T> (the value a resolved promise) */
                Type *pa = ta->kind == TY_PROMISE ? ta->elem : NULL, *pb = tb->kind == TY_PROMISE ? tb->elem : NULL;
                if (pa && !pb && (ty_equal(pa, tb) || (tb->kind == TY_NULL && pa->kind == TY_NULLABLE))) u = ta;
                else if (pb && !pa && (ty_equal(pb, ta) || (ta->kind == TY_NULL && pb->kind == TY_NULLABLE))) u = tb;
            }
            if (!u && !nul && ba->kind == TY_OBJECT && bb->kind == TY_OBJECT) { /* two shapes of the same fields: the first */
                if (shape_ok(bb, ba)) u = ba;
                else if (shape_ok(ba, bb)) u = bb;
            }
            if (!u && ba->kind == TY_ARRAY && bb->kind == TY_ARRAY) { /* c ? [x, 'y'] : [1, 2]: the wider array (unknown[]) */
                if (elem_widens(bb->elem, ba->elem)) u = ta;
                else if (elem_widens(ba->elem, bb->elem)) u = tb;
                if (u && nul && u->kind != TY_NULLABLE) u = ty_nullable_of(u);
            }
            if (!u && tsc_checked() && ta->kind != TY_VOID && tb->kind != TY_VOID && ta->kind != TY_PROMISE && tb->kind != TY_PROMISE) {
                /* x instanceof Date ? x.getTime() : x (--tsc-checked): TypeScript's union of both, held as an unknown */
                xs_warn_rule(XS2108, e->pos, "give both branches one type", "the branches of '?:' are %s and %s: an unknown, checked where it is used", ty_name(ta), ty_name(tb));
                u = ty_unknown;
            }
            if (!u) xs_error(XS8143, e->pos, NULL, "the two branches of '?:' must have the same type, but they are %s and %s", ty_name(ta), ty_name(tb));
            expect_type(a, u, "first branch of '?:'");
            expect_type(b, u, "second branch of '?:'");
            e->type = u;
            return;
        }
        if (a->type->kind == TY_VOID) xs_error(XS8128, e->pos, NULL, "the branches of '?:' cannot be void");
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
        if (cm && cm != cur_mod) enter_module(cm); /* first: it sets the module's scope, put back below */
        scope = sc;
        scope->head = head;
        cur_func = cf;
        cur_class = cc;
        loop_depth = ld;
        switch_depth = sd;
        finally_depth = fd;
        xs_error_recover = outer;
        mapnull_ok = 0; /* (an error inside a map(f).filter(p) left it up) */
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
    if (!s->decl_type && s->init && s->init->kind == E_NULL && s->init->unset) { /* let x; */
        s->type = ty_auto;
        Binding *b = declare(s->pos, s->name, s->type, false, global, false);
        b->auto_var = s;
        s->cname = b->cname;
        s->cell = b->cell;
        return;
    }
    check_expr(s->init, s->decl_type);
    if (!s->decl_type && pr_async_call(s->init)) pr_spawn(s->init); /* const p = f(): the promise (XB-106) */
    if (s->init->type->kind == TY_VOID && tsc_checked() && (!s->decl_type || s->decl_type->kind == TY_UNKNOWN || s->decl_type->kind == TY_VOID)) {
        /* const r = await fn() with fn's T = void (a generic instance): JavaScript stores undefined; here the call runs
           and r is an unknown that holds null */
        Expr *seq = xs_alloc(sizeof *seq);
        seq->kind = E_SEQ; seq->pos = s->init->pos; seq->field = -1; seq->type = ty_unknown;
        expr_list_push(&seq->args, s->init);
        Expr *nul = xs_alloc(sizeof *nul);
        nul->kind = E_NULL; nul->pos = s->init->pos; nul->field = -1; nul->type = ty_unknown;
        expr_list_push(&seq->args, nul);
        s->init = seq;
        if (s->decl_type && s->decl_type->kind == TY_VOID) s->decl_type = ty_unknown;
    }
    bool keeps_promise = s->decl_type && (s->decl_type->kind == TY_PROMISE || (s->decl_type->kind == TY_NULLABLE && s->decl_type->elem->kind == TY_PROMISE));
    if (s->init->type->kind == TY_VOID && !(keeps_promise && pr_is_call(s->init))) /* (const p: Promise<void> = f(): the promise, made below) */
        xs_error(XS8102, s->init->pos, NULL, "cannot store a void value");
    if (s->init->type->kind == TY_NULL && !s->decl_type) xs_error(XS2013, s->init->pos, "write the type: let x: Todo | null = null;", "cannot infer the type of '%s' from null", s->name);
    if (s->decl_type) expect_type(s->init, s->decl_type, xs_sprintf("initializer of '%s' (declared as %s)", s->name, ty_name(s->decl_type)));
    settle(s->init);
    s->type = s->decl_type ? s->decl_type : s->init->type;
    Binding *b = declare(s->pos, s->name, s->type, s->is_const, global, false);
    if (!s->decl_type && !s->is_const && s->type->kind == TY_NUMBER) b->lit_number = true;
    if (s->is_const && s->init->kind == E_ARROW) b->arrow_fn = s->init->arrow->fn;
    s->cname = b->cname;
    s->cell = b->cell;
}

/* ---- truthiness, narrowly: a string is true when not empty; T | null when not null (and, for strings, not
   empty). Numbers are never coerced. Only names and fields, which can be read twice: the test is rewritten
   into the explicit comparisons, so narrowing keeps working. ---- */
static bool truthy_type(Type *t) {
    return t->kind == TY_STRING || t->kind == TY_NULLABLE || t->kind == TY_UNKNOWN ||
           t->kind == TY_OBJECT || t->kind == TY_ARRAY || t->kind == TY_MAP || t->kind == TY_SET || t->kind == TY_FUNC;
}
/* XB-118 rule 5 (proposed, awaiting Isa's review): an int in a condition (if, !, &&, ||, ?:) is JavaScript's
   truthiness, x !== 0 (`if (!rows.length)`). The only spot: take TY_INT out here to make it an error again. */
static bool tsc_checked(void);
/* with --tsc-checked, a number too (x !== 0 and not NaN) and a union (by what it holds), as tsc accepts them */
static bool cond_truthy_type(Type *t) {
    return truthy_type(t) || t->kind == TY_INT || (tsc_checked() && (t->kind == TY_NUMBER || t->kind == TY_UNION));
}
static bool rereadable(Expr *e) {
    if (e->kind == E_IDENT || e->kind == E_THIS) return true;
    return e->kind == E_MEMBER && !e->is_call && e->field >= 0 && rereadable(e->lhs);
}
/* a checked condition the types decide: typeof n === "object" with n a number, false && x (TypeScript narrows the
   value to never there; the branch it guards never runs, so it is not checked) */
static bool static_bool(Expr *e, bool *val) {
    if (!e) return false;
    if (e->kind == E_BOOL) { *val = e->boolean; return true; }
    if (e->kind == E_BINARY && (e->op == TK_EQ || e->op == TK_NEQ) && e->lhs->kind == E_STRING && e->rhs->kind == E_STRING &&
        e->lhs->str && e->rhs->str && !e->builtin.cfn) {
        bool same = strcmp(e->lhs->str, e->rhs->str) == 0;
        *val = e->op == TK_EQ ? same : !same;
        return true;
    }
    if (e->kind == E_BINARY && e->op == TK_AND) {
        bool a;
        if (static_bool(e->lhs, &a) && !a) { *val = false; return true; }
    }
    return false;
}
/* a read with no call in it (x, o.f, o.f!, xs[i], m[3]): dropping it changes nothing */
static bool pure_read(Expr *e) {
    if (!e) return false;
    switch (e->kind) {
    case E_IDENT: case E_THIS: case E_NUMBER: case E_STRING: case E_BOOL: case E_NULL: return true;
    case E_MEMBER: return !e->is_call && pure_read(e->lhs);
    case E_INDEX: return pure_read(e->lhs) && pure_read(e->rhs);
    case E_AS: return pure_read(e->lhs);
    default: return false;
    }
}
static Expr *expr_copy(Expr *e) { Expr *c = xs_alloc(sizeof *c); *c = *e; return c; }
/* x (checked) of the type undefined, where JavaScript writes its text (console.log, String, a template): "undefined",
   x still evaluated first when it does something */
static void undef_text(Expr *x) {
    Expr *str = xs_alloc(sizeof *str);
    str->kind = E_STRING; str->pos = x->pos; str->field = -1; str->str = "undefined"; str->type = ty_string;
    str->lit_id = intern("undefined");
    if (pure_read(x)) { *x = *str; return; }
    Expr *v = xs_alloc(sizeof *v);
    *v = *x;
    memset(x, 0, sizeof *x);
    x->kind = E_SEQ; x->pos = v->pos; x->field = -1; x->type = ty_string;
    expr_list_push(&x->args, v);
    expr_list_push(&x->args, str);
}

/* an expression whose value is a boolean by its form (!x, a === b, x instanceof C, a && b): not a default value */
static bool boolish(Expr *r) {
    if (!r) return false;
    if (r->kind == E_BOOL) return true;
    if (r->kind == E_UNARY && r->op == TK_NOT) return true;
    if (r->kind == E_BINARY) {
        TokKind o = r->op;
        if (o == TK_EQ || o == TK_NEQ || o == TK_LT || o == TK_LE || o == TK_GT || o == TK_GE || o == TK_IDENT || o == TK_IN) return true;
        if (o == TK_AND || o == TK_OR) return boolish(r->lhs) && boolish(r->rhs);
    }
    return false;
}
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

/* ---------- null: a?.b, a ?? b, a ??= b (MANIFESTO, "null, não undefined") ---------- */
/* v !== null for a checked v of a nullable type (no truthiness: "" and 0 are values) */
static Expr *null_test_of(Expr *v) {
    Expr *nul = xs_alloc(sizeof *nul);
    nul->kind = E_NULL;
    nul->pos = v->pos;
    nul->type = ty_null; /* (the emitter tests a number's sentinel by this) */
    Expr *test = xs_alloc(sizeof *test);
    test->kind = E_BINARY;
    test->op = TK_NEQ;
    test->pos = v->pos;
    test->lhs = v;
    test->rhs = nul;
    test->type = ty_boolean;
    return test;
}
/* a?.rest (parsed as E_COND "optchain": lhs = a, str = the temporary, args[0] = the rest built on it): a is
   evaluated once into a constant of a's non-null type that the rest reads; the whole is the rest's type made
   nullable (null when a is null), or void for a call that returns nothing. Emitted by emit.c (E_COND optchain) */
static void check_optchain(Expr *e, Type *expected) {
    Expr *base = e->lhs, *rest = e->args.items[0];
    if (base->kind == E_AS && base->elem_type && base->op != TK_NOT && base->lhs->kind == E_IDENT) {
        /* (err as Error)?.message with err unknown: TypeScript's cast checks nothing, so a value that is not an Error
           reads undefined there; here: err instanceof Error ? err as Error : null */
        Type *want = resolve(base->elem_type, base->pos);
        Expr *probe = expr_copy(base->lhs);
        check_expr(probe, NULL);
        if (want->kind == TY_OBJECT && want->cls && !want->cls->is_interface && probe->type->kind == TY_UNKNOWN && !ty_is_any(probe->type)) {
            Expr *cn = xs_alloc(sizeof *cn);
            cn->kind = E_IDENT; cn->pos = base->pos; cn->field = -1; cn->str = want->cls->name;
            Expr *io = xs_alloc(sizeof *io);
            io->kind = E_BINARY; io->op = TK_IDENT; io->pos = base->pos; io->field = -1; io->lhs = expr_copy(base->lhs); io->rhs = cn;
            Expr *cast = expr_copy(base);
            cast->lhs = expr_copy(base->lhs);
            Expr *nul = xs_alloc(sizeof *nul);
            nul->kind = E_NULL; nul->pos = base->pos; nul->field = -1;
            Expr *c = xs_alloc(sizeof *c);
            c->kind = E_COND; c->pos = base->pos; c->field = -1; c->lhs = io;
            expr_list_push(&c->args, cast);
            expr_list_push(&c->args, nul);
            e->lhs = base = c;
        }
    }
    check_expr(base, NULL);
    settle(base);
    Type *bt = base->type;
    if (bt->kind == TY_NULL) xs_error(XS2001, e->pos, "only values declared as T | null can be null", "null can never be read with '?.'");
    Type *t = bt->kind == TY_NULLABLE ? bt->elem : bt;
    push_scope();
    Binding *b = declare(e->pos, e->str, t, true, false, false);
    b->no_warn = true;
    b->used = true;
    check_expr(rest, expected && expected->kind == TY_NULLABLE ? expected->elem : expected);
    settle(rest);
    missing_get(rest); /* a?.get(k): absent for a missing k too */
    pop_scope();
    e->cname = b->cname;
    Type *rt = rest->type;
    if (ty_is_any(rt) || rt->kind == TY_UNKNOWN) e->type = rt; /* (an any or an unknown may be null already) */
    else if (rt->kind == TY_VOID) e->type = ty_void;
    else if (rt->kind == TY_NULLABLE || rt->kind == TY_NULL) e->type = rt;
    else { /* a null a gives JavaScript's undefined: absent (a rest that is itself T | null keeps its null) */
        if (!nullable_ok(rt)) xs_error(XS2011, e->pos, "read it after a null test: if (a !== null) ...", "'?.' cannot give a %s or null", ty_name(rt));
        e->type = ty_optional_of(rt);
    }
}
/* m.get(k) with a primitive value (a missing key is an error there: get has no null for it), read through ?? or ?.:
   m.has(k) ? m.get(k) : absent, as JavaScript's undefined for a missing key. m a name or a field, k a name or a literal */
static bool missing_get(Expr *l) {
    if (!(l->kind == E_MEMBER && l->is_call && l->builtin.cfn && strcmp(l->builtin.cfn, "get") == 0 && l->lhs->type &&
          l->lhs->type->kind == TY_MAP && l->type && l->type->kind != TY_NULLABLE && l->type->kind != TY_UNKNOWN && rereadable(l->lhs) && l->args.len == 1))
        return false;
    Expr *k = l->args.items[0];
    if (k->kind != E_IDENT && k->kind != E_STRING && k->kind != E_NUMBER) return false;
    Expr *get = expr_copy(l);
    Expr *has = xs_alloc(sizeof *has);
    has->kind = E_MEMBER;
    has->pos = l->pos;
    has->lhs = expr_copy(l->lhs);
    has->str = "has";
    has->is_call = true;
    has->field = -1;
    expr_list_push(&has->args, expr_copy(k));
    check_expr(has, NULL);
    Type *t = ty_optional_of(get->type);
    Expr *nul = xs_alloc(sizeof *nul);
    nul->kind = E_NULL;
    nul->pos = l->pos;
    nul->type = t;
    memset(l, 0, sizeof *l);
    l->kind = E_COND;
    l->pos = get->pos;
    l->lhs = has;
    expr_list_push(&l->args, get);
    expr_list_push(&l->args, nul);
    l->type = t;
    return true;
}
/* a ?? b: a unless it is null; b runs only then. With a never null, a (b is checked, never run) */
static void check_nullish(Expr *e, Type *expected) {
    check_expr(e->lhs, NULL);
    settle(e->lhs);
    {   /* s.match(/x/g) ?? []: the matches as a string[] (the empty array takes that type) */
        Type *m = e->lhs->type;
        if (m && m->kind == TY_NULLABLE && m->elem->kind == TY_OBJECT && m->elem->cls->builtin && !strcmp(m->elem->cls->name, "RegExpExecArray") &&
            e->rhs && e->rhs->kind == E_ARRAY_LIT && e->rhs->args.len == 0)
            wrap_call(e->lhs, "xs_RegExpExecArray_strings", ty_nullable_of(ty_array_of(ty_string)));
    }
    Type *lt = e->lhs->type;
    if (lt->kind == TY_NULL) {
        Expr *r = e->rhs;
        check_expr(r, expected);
        *e = *r;
        return;
    }
    Expr *l = e->lhs;
    if (ty_is_any(lt)) lt = ty_nullable_of(lt); /* an any may be null */
    if (lt->kind != TY_NULLABLE && missing_get(l)) lt = l->type; /* m.get(k) ?? d: absent when k is missing */
    if (lt->kind != TY_NULLABLE) {
        check_expr(e->rhs, lt);
        *e = *l;
        return;
    }
    Type *want = lt->elem;
    if ((want->kind == TY_NUMBER || want->kind == TY_INT) && e->rhs->kind == E_STRING) {
        /* n ?? '' (n a number | null): JavaScript gives number | string, used as text: String(n) ?? '' */
        Expr *v = expr_copy(l);
        memset(l, 0, sizeof *l);
        l->kind = E_CALL; l->pos = v->pos; l->field = -1;
        l->builtin.cfn = want->kind == TY_NUMBER ? "xs_str_of_num_or_null" : "xs_str_of_int_or_null";
        expr_list_push(&l->args, v);
        l->type = lt = ty_nullable_of(ty_string);
        want = ty_string;
    }
    check_expr(e->rhs, want);
    if (e->rhs->type->kind == TY_LITINT && is_numeric(want)) coerce_lit(e->rhs, want);
    if (!ty_is_any(want) && want->kind != TY_UNKNOWN &&
        (e->rhs->type->kind == TY_UNKNOWN || (want->kind == TY_STRING && (is_numeric(e->rhs->type) || e->rhs->type->kind == TY_LITINT)) ||
         (is_numeric(want) && e->rhs->type->kind == TY_STRING) || /* Enum[s] ?? s: number | string */
         /* (err as Error)?.message ?? err: string | Error, an unknown (String() of it reads what it holds) */
         ((want->kind == TY_STRING || is_numeric(want) || want->kind == TY_BOOLEAN) && (e->rhs->type->kind == TY_OBJECT || e->rhs->type->kind == TY_ARRAY)))) {
        /* s ?? row.X with X unknown, s ?? 0: TypeScript gives string | unknown, string | number: an unknown here */
        to_unknown(l);
        lt = l->type;
        want = ty_unknown;
    }
    Type *res = want;
    if ((e->rhs->type->kind == TY_NULL || e->rhs->type->kind == TY_NULLABLE) && !ty_is_any(want)) res = ty_nullable_of(want);
    expect_type(e->rhs, res, "default after '?\?'");
    Expr *d = e->rhs;
    if (!rereadable(e->lhs)) { /* f() ?? d: f() once (emit.c, or_once with a null test) */
        e->kind = E_COND;
        e->op = 0;
        e->rhs = NULL;
        e->args.len = 0;
        expr_list_push(&e->args, d);
        e->builtin.cfn = "qq_once";
        e->type = res;
        return;
    }
    Expr *val = expr_copy(e->lhs);
    if (res == want) val->type = want; /* read only after the test passed */
    Expr *test = null_test_of(expr_copy(e->lhs));
    e->kind = E_COND;
    e->op = 0;
    e->lhs = test;
    e->rhs = NULL;
    e->args.len = 0;
    expr_list_push(&e->args, val);
    expr_list_push(&e->args, d);
    e->type = res;
}
/* a copy of an unchecked target of ??= (a, a.b, this.b, a[i] with a simple index), to read it on the right */
static Expr *copy_target(Expr *x) {
    if (!x) return NULL;
    switch (x->kind) {
    case E_IDENT: case E_THIS: case E_NUMBER: return expr_copy(x);
    case E_MEMBER:
        if (x->is_call) break;
        { Expr *c = expr_copy(x); c->lhs = copy_target(x->lhs); return c->lhs ? c : NULL; }
    case E_INDEX:
        { Expr *c = expr_copy(x); c->lhs = copy_target(x->lhs); c->rhs = copy_target(x->rhs); return c->lhs && c->rhs ? c : NULL; }
    default: break;
    }
    return NULL;
}
/* == / != between values that JavaScript would convert first (1 == "1", 0 == false): refused */
static int js_kind(Type *t) { /* 1 number, 2 string, 3 boolean, 0 other */
    if (t->kind == TY_NULLABLE) t = t->elem;
    if (t->kind == TY_NUMBER || t->kind == TY_INT || t->kind == TY_LITINT || (t->kind == TY_ENUM && !t->en->is_string)) return 1;
    if (t->kind == TY_STRING || (t->kind == TY_ENUM && t->en->is_string)) return 2;
    if (t->kind == TY_BOOLEAN) return 3;
    return 0;
}
static void check_loose_eq(Expr *e) {
    int a = js_kind(e->lhs->type), b = js_kind(e->rhs->type);
    if (!a || !b || a == b) return;
    const char *op = e->op == TK_EQ ? "==" : "!=";
    const char *help = (a == 1 && b == 2) || (a == 2 && b == 1) ? "JavaScript would turn the string into a number first; say which: Number(s) === n, or String(n) === s"
                     : "JavaScript would turn the boolean into a number first; compare values of one type";
    xs_error(XS8148, e->pos, help, "'%s' between %s and %s would convert one of them", op, ty_name(e->lhs->type), ty_name(e->rhs->type));
}

static void make_truthy(Expr *e, const char *what) {
    if (e->type->kind == TY_INT) { /* XB-118 rule 5: x !== 0 */
        xs_warn_rule(XS2105, e->pos, "compare explicitly: x !== 0 (or x > 0)", "an int as a condition: read as '!== 0'");
        Expr *v = expr_copy(e);
        Expr *z = xs_alloc(sizeof *z);
        z->kind = E_NUMBER; z->pos = e->pos; z->field = -1; z->type = ty_int; z->is_int_lit = true; z->ival = 0; z->num = 0;
        memset(e, 0, sizeof *e);
        e->kind = E_BINARY; e->op = TK_NEQ; e->pos = v->pos; e->field = -1; e->type = ty_boolean;
        e->lhs = v; e->rhs = z;
        return;
    }
    if (e->type->kind == TY_NUMBER && rereadable(e)) { /* --tsc-checked: x !== 0 && x === x (0 and NaN are false) */
        xs_warn_rule(XS2105, e->pos, "compare explicitly: x !== 0 (or x > 0)", "a number as a condition: read as '!== 0 and not NaN'");
        Expr *v1 = expr_copy(e), *v2 = expr_copy(e), *v3 = expr_copy(e);
        Expr *z = xs_alloc(sizeof *z);
        z->kind = E_NUMBER; z->pos = e->pos; z->field = -1; z->type = ty_number; z->num = 0;
        Expr *a = xs_alloc(sizeof *a);
        a->kind = E_BINARY; a->op = TK_NEQ; a->pos = e->pos; a->field = -1; a->type = ty_boolean; a->lhs = v1; a->rhs = z;
        Expr *b = xs_alloc(sizeof *b);
        b->kind = E_BINARY; b->op = TK_EQ; b->pos = e->pos; b->field = -1; b->type = ty_boolean; b->lhs = v2; b->rhs = v3;
        Pos at = e->pos;
        memset(e, 0, sizeof *e);
        e->kind = E_BINARY; e->op = TK_AND; e->pos = at; e->field = -1; e->type = ty_boolean; e->lhs = a; e->rhs = b;
        return;
    }
    if (e->type->kind == TY_UNION) { e->type = ty_unknown; dyn_wrap(e, "xs_dyn_truthy", ty_boolean); return; } /* (held as unknown is) */
    if (e->type->kind == TY_UNKNOWN) { dyn_wrap(e, "xs_dyn_truthy", ty_boolean); return; } /* JavaScript's truthiness, at run time (any and unknown) */
    if (e->kind == E_INDEX && !e->builtin.cfn && e->lhs->type && e->lhs->type->kind == TY_ARRAY && ty_is_heap(e->type) &&
        e->type->kind != TY_NULLABLE && ty_is_heap(e->lhs->type->elem) && nullable_ok(e->type)) {
        /* if (rows[0]) / !usuarios[0]: the element, or null past the end (JavaScript's undefined, false) */
        e->builtin.cfn = "xs_arr_ref_at_or_null";
        e->type = ty_nullable_of(e->type);
    }
    if (e->type->kind == TY_OBJECT || e->type->kind == TY_ARRAY || e->type->kind == TY_MAP || e->type->kind == TY_SET || e->type->kind == TY_FUNC) {
        /* if (this.dataSource): a value that is never null is an object, always truthy */
        if (!rereadable(e)) xs_error(XS8021, e->pos, "store it in a const first, or compare explicitly", "%s must be boolean (truthiness applies to names and fields only)", what);
        Pos at = e->pos;
        memset(e, 0, sizeof *e);
        e->kind = E_BOOL; e->boolean = true; e->type = ty_boolean; e->field = -1; e->pos = at;
        return;
    }
    if (!rereadable(e)) { /* !xs?.length, if (m[k]), f() && ...: the value once, then its truthiness, through ((t) => !!t)(value) */
        if (e->type->kind == TY_VOID || e->type->kind == TY_NULL)
            xs_error(XS8021, e->pos, "store it in a const first, or compare explicitly", "%s must be boolean (truthiness applies to names and fields only)", what);
        Expr *inner = xs_alloc(sizeof *inner);
        *inner = *e;
        Expr *n = xs_alloc(sizeof *n), *n1 = xs_alloc(sizeof *n1), *n2 = xs_alloc(sizeof *n2);
        n->kind = E_IDENT; n->pos = e->pos; n->field = -1; n->str = "__truthy";
        n1->kind = E_UNARY; n1->op = TK_NOT; n1->pos = e->pos; n1->field = -1; n1->lhs = n;
        n2->kind = E_UNARY; n2->op = TK_NOT; n2->pos = e->pos; n2->field = -1; n2->lhs = n1;
        Expr *arrow = thunk_of(n2);
        Func *f = arrow->arrow->fn;
        f->params = xs_alloc(sizeof(Param));
        memset(f->params, 0, sizeof(Param));
        f->params[0].name = "__truthy";
        f->params[0].type = inner->type->kind == TY_LITINT ? ty_int : inner->type;
        if (inner->type->kind == TY_LITINT) coerce_lit(inner, ty_int);
        f->nparams = 1;
        f->ret = ty_boolean;
        Pos at = e->pos;
        memset(e, 0, sizeof *e);
        e->kind = E_CALL; e->pos = at; e->field = -1; e->is_call = true;
        e->lhs = arrow;
        expr_list_push(&e->args, inner);
        check_expr(arrow, NULL);
        e->type = ty_boolean;
        return;
    }
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
    Type *el = v->type->elem;
    if (is_numeric(el) || el->kind == TY_BOOLEAN || (el->kind == TY_ENUM && !el->en->is_string)) {
        /* n: number | null: not null, and then not 0 (nor NaN), as JavaScript's truthiness */
        Expr *v2 = expr_copy(v);
        v2->type = el;
        Expr *t2 = v2;
        if (el->kind != TY_BOOLEAN) {
            Expr *z = xs_alloc(sizeof *z);
            z->kind = E_NUMBER; z->pos = v->pos; z->field = -1; z->type = el->kind == TY_NUMBER ? ty_number : ty_int;
            z->is_int_lit = el->kind != TY_NUMBER; z->ival = 0; z->num = 0;
            t2 = xs_alloc(sizeof *t2);
            t2->kind = E_BINARY; t2->op = TK_NEQ; t2->pos = v->pos; t2->field = -1; t2->type = ty_boolean;
            t2->lhs = v2; t2->rhs = z;
            if (el->kind == TY_ENUM) v2->type = ty_int;
            if (el->kind == TY_NUMBER) { /* and not NaN: x === x */
                Expr *v3 = expr_copy(v), *v4 = expr_copy(v);
                v3->type = v4->type = el;
                Expr *same = xs_alloc(sizeof *same);
                same->kind = E_BINARY; same->op = TK_EQ; same->pos = v->pos; same->field = -1; same->type = ty_boolean;
                same->lhs = v3; same->rhs = v4;
                Expr *both = xs_alloc(sizeof *both);
                both->kind = E_BINARY; both->op = TK_AND; both->pos = v->pos; both->field = -1; both->type = ty_boolean;
                both->lhs = t2; both->rhs = same;
                t2 = both;
            }
        }
        e->kind = E_BINARY;
        e->op = TK_AND;
        e->lhs = test;
        e->rhs = t2;
        return;
    }
    if (el->kind != TY_STRING) { *e = *test; return; }
    Expr *v2 = expr_copy(v);
    v2->type = ty_string; /* read after the null test */
    e->kind = E_BINARY;
    e->op = TK_AND;
    e->lhs = test;
    e->rhs = str_truthy_call(v2);
}
static void check_cond(Expr *c, const char *what) {
    check_expr(c, NULL);
    if (c->type->kind != TY_BOOLEAN && cond_truthy_type(c->type)) make_truthy(c, what);
    if (c->type->kind != TY_BOOLEAN)
        xs_error(XS8022, c->pos, "there is no truthiness for numbers in xisto; compare explicitly, e.g. 'x !== 0'", "%s must be boolean, but this is %s", what, ty_name(c->type));
}

/* A field read through a variable or `this` (o.f, this.f) of a nullable type: the name its narrowing goes by in
   the scopes, from the variable's C name (unique, so an inner `o` that shadows it is another path), or NULL */
/* the stable reference a path starts from: x, this, x.f, this.f.g (fields only, no calls), or NULL */
static const char *path_base(Expr *e) {
    while (e && e->kind == E_AS && e->op == TK_NOT && !e->elem_type) e = e->lhs; /* o.f narrowed already: the same path */
    if (!e) return NULL;
    if (e->kind == E_THIS) return "this";
    if (e->kind == E_IDENT) {
        Binding *b = lookup(e->str);
        return b && b->cname ? b->cname : NULL;
    }
    if (e->kind == E_MEMBER && !e->is_call && e->field >= 0 && e->cls && !e->cls->builtin) {
        const char *in = path_base(e->lhs);
        return in ? xs_sprintf("%s.%d.%s", in, e->field, e->cls->name) : NULL;
    }
    return NULL;
}
static const char *path_key(Expr *m) {
    if (!m || m->kind != E_MEMBER || m->is_call || m->field < 0 || !m->type || m->type->kind != TY_NULLABLE || !m->cls || m->cls->builtin) return NULL;
    const char *in = path_base(m->lhs); /* o.f, this.f and deeper: ctx.aut.f, as TypeScript narrows them */
    if (!in) return NULL;
    return xs_sprintf("\x01%s.%d.%s", in, m->field, m->cls->name);
}
/* `x !== null` / `x === null` on a nullable local: returns the binding and whether the test is "not null". */
static Expr *union_ident(Expr *e);
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
    Expr *id = union_ident(cond->lhs) ? union_ident(cond->lhs) : union_ident(cond->rhs); /* (x seen through a union narrowing too) */
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

static bool is_optchain(Expr *e) { return e && e->kind == E_COND && e->builtin.cfn && strcmp(e->builtin.cfn, "optchain") == 0; }
static void narrow_set(Expr *c, bool when, NSet *out);
/* a?.f compared (a?.f === 'x', a?.f !== null, or the truthy test it was rewritten into): when the comparison can
   only hold with a not null, a is narrowed, as TypeScript reads it; a?.f !== null also narrows the path a.f */
static void optchain_narrow(Expr *c, bool when, NSet *out) {
    Expr *oc = NULL, *other = NULL;
    bool truthy = false;
    if (c->kind == E_CALL && c->args.len == 1 && is_optchain(c->args.items[0]) && c->lhs && c->lhs->kind == E_ARROW && c->lhs->arrow &&
        c->lhs->arrow->fn->nparams == 1 && strcmp(c->lhs->arrow->fn->params[0].name, "__truthy") == 0) { /* if (a?.f): the truthy test of make_truthy */
        oc = c->args.items[0];
        truthy = true;
    } else {
        if (c->kind != E_BINARY || (c->op != TK_EQ && c->op != TK_NEQ)) return;
        oc = is_optchain(c->lhs) ? c->lhs : is_optchain(c->rhs) ? c->rhs : NULL;
        if (!oc) return;
        other = oc == c->lhs ? c->rhs : c->lhs;
    }
    if (truthy) {
        if (!when) return;
        static Expr nul0 = {.kind = E_NULL};
        other = &nul0;
        c = &(Expr){.kind = E_BINARY, .op = TK_NEQ};
    }
    bool other_null = other->kind == E_NULL;
    bool other_lit = other->kind == E_STRING || other->kind == E_NUMBER || other->kind == E_BOOL;
    bool base_nn = other_null ? (c->op == TK_NEQ) == when : other_lit ? (c->op == TK_EQ) == when : false;
    if (!base_nn) return;
    Expr *base = oc->lhs;
    if (base->kind == E_IDENT || base->kind == E_MEMBER) narrow_set(null_test_of(base), true, out);
    Expr *rest = oc->args.items[0];
    if (other_null && rest->kind == E_MEMBER && !rest->is_call && rest->lhs->kind == E_IDENT && oc->str && strcmp(rest->lhs->str, oc->str) == 0 &&
        (base->kind == E_IDENT || base->kind == E_THIS)) {
        Expr *m = expr_copy(rest);
        m->lhs = base;
        narrow_set(null_test_of(m), true, out);
    }
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
    if (!b) optchain_narrow(c, when, out);
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
    if (m->kind == TY_OBJECT && m->cls && m->cls->builtin && strcmp(m->cls->name, "bigint") == 0) return "bigint";
    if (m->kind == TY_OBJECT && m->cls && m->cls->builtin && strcmp(m->cls->name, "symbol") == 0) return "symbol";
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
    if (c->kind == E_UNARY && c->op == TK_NOT) { /* !(x instanceof C): the branches swap */
        r = union_narrow(c->lhs);
        Type *t = r.then_t;
        r.then_t = r.else_t;
        r.else_t = t;
        return r;
    }
    if (c->kind == E_BINARY && (c->op == TK_AND || c->op == TK_OR)) {
        /* a caught Error narrowed on one side: (e instanceof X && ...) true, (!(e instanceof X) || ...) false */
        /* (and any union or unknown narrowed by the left side: !Array.isArray(v) || v.length !== 1 || ...: v an array
           after the whole condition is false) */
        UNarrow a = union_narrow(c->lhs);
        if (!a.b) a = union_narrow(c->rhs); /* err || !Array.isArray(v): v an array once the whole is false, too */
        if (!a.b) return r;
        r.b = a.b;
        if (c->op == TK_AND) r.then_t = a.then_t;
        else r.else_t = a.else_t;
        return r;
    }
    if (c->kind == E_CALL && c->builtin.cfn && strcmp(c->builtin.cfn, "xs_is_array") == 0 && c->args.len == 1) {
        /* Array.isArray(x) with x: Record<string, V> | V[]: the array member, and the rest on the other side */
        Expr *id = union_ident(c->args.items[0]);
        Binding *b = id ? lookup(id->str) : NULL;
        if (!b) return r;
        Type *u = b->type;
        if (u->kind == TY_UNKNOWN) { r.b = b; r.then_t = ty_array_of(ty_is_any(u) ? ty_any : ty_unknown); return r; } /* v: unknown, an array: unknown[] (an any: any[]) */
        bool nul = u->kind == TY_NULLABLE && u->elem->kind == TY_UNION; /* (string | string[] | undefined: the array, or the rest | null) */
        if (nul) u = u->elem;
        if (u->kind != TY_UNION) return r;
        bool *keep = xs_alloc(sizeof(bool) * (size_t)u->nparams), *other = xs_alloc(sizeof(bool) * (size_t)u->nparams);
        for (int i = 0; i < u->nparams; i++) { keep[i] = u->params[i]->kind == TY_ARRAY; other[i] = !keep[i]; }
        r.b = b;
        r.then_t = union_subset(u, keep);
        r.else_t = union_subset(u, other);
        if (nul && r.else_t && r.else_t->kind != TY_NULLABLE && nullable_ok(r.else_t)) r.else_t = ty_nullable_of(r.else_t);
        return r;
    }
    if (c->kind == E_CALL && c->builtin.cfn && strcmp(c->builtin.cfn, "xs_union_has") == 0 && c->args.len == 2 && c->args.items[1]->kind == E_STRING) {
        /* 'k' in x: the members that have k, and on the other side those without it (or with it optional) */
        Expr *id = union_ident(c->args.items[0]);
        Binding *b = id ? lookup(id->str) : NULL;
        if (!b) return r;
        Type *u = b->type;
        if (u->kind == TY_NULLABLE) u = u->elem;
        if (u->kind != TY_UNION) return r;
        const char *k = c->args.items[1]->str;
        bool *keep = xs_alloc(sizeof(bool) * (size_t)u->nparams), *other = xs_alloc(sizeof(bool) * (size_t)u->nparams);
        for (int i = 0; i < u->nparams; i++) {
            Field *fd = u->params[i]->kind == TY_OBJECT ? find_field(u->params[i]->cls, k, NULL) : NULL;
            keep[i] = fd != NULL;
            other[i] = !fd || fd->optional;
        }
        r.b = b;
        r.then_t = union_subset(u, keep);
        r.else_t = union_subset(u, other);
        return r;
    }
    bool is_ufield = c->kind == E_MEMBER && c->builtin.cfn && strcmp(c->builtin.cfn, "union_field") == 0;
    if (c->kind != E_BINARY && !is_ufield) return r;
    bool neg = c->kind == E_BINARY && c->op == TK_NEQ;
    Expr *id = NULL, *lit = NULL, *bfield = NULL;
    bool bwant = true;
    int how = 0; /* 1 typeof, 2 instanceof, 3 discriminant field, 4 boolean discriminant ({ ok: true } | { ok: false }) */
    if (is_ufield) { id = union_ident(c->lhs); how = 4; bfield = c; }
    else if ((c->op == TK_EQ || c->op == TK_NEQ) && (c->rhs->kind == E_BOOL || c->lhs->kind == E_BOOL)) {
        Expr *a = c->rhs->kind == E_BOOL ? c->lhs : c->rhs, *bl = c->rhs->kind == E_BOOL ? c->rhs : c->lhs;
        if (a->kind == E_MEMBER && a->builtin.cfn && strcmp(a->builtin.cfn, "union_field") == 0) { id = union_ident(a->lhs); how = 4; bfield = a; bwant = bl->boolean; }
    }
    else if (c->op == TK_IDENT) { id = union_ident(c->lhs); how = 2; }
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
    if (how == 2 && !neg && b->type->kind == TY_OBJECT && b->type->cls->builtin && strcmp(b->type->cls->name, "Error") == 0 &&
        c->rhs->cls && is_error_class(c->rhs->cls)) { /* catch (e) { if (e instanceof HttpException) ... }: e is the thrown object */
        r.b = b;
        r.then_t = ty_object_of(c->rhs->cls);
        return r;
    }
    if (b->type->kind == TY_UNKNOWN && (how == 1 || how == 2)) { /* x: unknown (or any): x instanceof C / typeof x === "string" */
        Type *t = NULL;
        if (how == 2 && c->rhs->cls && !c->rhs->cls->is_interface && !(c->rhs->cls->ntparams && !c->rhs->cls->generic_of))
            t = ty_object_of(c->rhs->cls); /* (a generic class: which instance is not known, so x stays unknown) */
        else if (how == 1 && strcmp(lit->str, "string") == 0) t = ty_string;
        else if (how == 1 && strcmp(lit->str, "number") == 0) t = ty_number;
        else if (how == 1 && strcmp(lit->str, "boolean") == 0) t = ty_boolean;
        else if (how == 1 && strcmp(lit->str, "symbol") == 0 && find_class("symbol")) t = ty_object_of(find_class("symbol"));
        else if (how == 1 && strcmp(lit->str, "bigint") == 0) t = ty_big();
        if (!t) return r;
        r.b = b;
        if (neg) r.else_t = t; else r.then_t = t;
        return r;
    }
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
        else if (how == 4) {
            Field *fd = m->kind == TY_OBJECT ? find_field(m->cls, bfield->str, NULL) : NULL;
            if (!fd || fd->type->kind != TY_BOOLEAN || !fd->type->tname) return (UNarrow){0}; /* not a literal true / false */
            yes = strcmp(fd->type->tname, bwant ? "true" : "false") == 0;
        }
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
static bool assigns_non_null(Stmt *body, const char *name);

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
    case S_VAR:
        if (s->for_head) { /* for (let i = 0, j = n - 1; ...): the loop's variables, a copy per iteration */
            bool sv = for_init_decl;
            for_init_decl = true;
            check_var(s, false);
            for_init_decl = sv;
            return;
        }
        check_var(s, false);
        return;
    case S_EXPR:
        { /* p.catch(...); and await p.catch((err) => log(err)); (the value dropped: the handler need not give the promise's type) */
            Expr *x = s->expr->kind == E_AWAIT && s->expr->lhs ? s->expr->lhs : s->expr;
            pr_discard = x->kind == E_MEMBER && x->is_call && x->str && (strcmp(x->str, "then") == 0 || strcmp(x->str, "catch") == 0);
        }
        check_expr(s->expr, NULL);
        pr_discard = false;
        settle(s->expr);
        return;
    case S_IF: {
        check_cond(s->cond, "condition");
        UNarrow un = union_narrow(s->cond);
        if (un.b) { /* a union narrowed in each branch; after `if (...) return;` the rest sees the other members */
            NSet uts = {0}, ufs = {0}; /* (and the names the condition shows not null, as below) */
            narrow_set(s->cond, true, &uts);
            narrow_set(s->cond, false, &ufs);
            push_scope();
            shadow_union(un.b, un.then_t);
            shadow_set(&uts);
            check_block_union(s->body, NULL, NULL);
            pop_scope();
            if (s->else_body) {
                push_scope();
                shadow_union(un.b, un.else_t);
                shadow_set(&ufs);
                if (s->else_body->kind == S_BLOCK) check_block_union(s->else_body, NULL, NULL);
                else check_stmt(s->else_body);
                pop_scope();
            } else if (always_exits(s->body)) { shadow_union(un.b, un.else_t); shadow_set(&ufs); }
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
        else if (!s->else_body) /* if (!x) { x = new Map(); }  →  x is not null after it */
            for (int i = 0; i < fs.n; i++)
                if (assigns_non_null(s->body, fs.b[i]->name)) shadow_narrowed(fs.b[i]);
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
        if (t->kind == TY_OBJECT && is_error_class(t->cls)) {
            /* an object of a class that extends Error: an Error with its message and name, the object its cause */
            Expr *v = expr_copy(s->expr);
            Expr *w = xs_alloc(sizeof *w);
            w->kind = E_CALL;
            w->pos = v->pos;
            w->builtin.cfn = "xs_Error_wrap_err";
            w->type = ty_object_of(err);
            expr_list_push(&w->args, v);
            s->expr = w;
            return;
        }
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
        if (t->kind == TY_UNKNOWN) { /* throw item.erro (unknown, any): an Error rethrown as it is; another value, as the cause of one */
            Expr *v = expr_copy(s->expr);
            Expr *w = xs_alloc(sizeof *w);
            w->kind = E_CALL;
            w->pos = v->pos;
            w->builtin.cfn = "xs_Error_of_unknown";
            w->type = ty_object_of(err);
            expr_list_push(&w->args, v);
            s->expr = w;
            return;
        }
        if (t->kind != TY_OBJECT || t->cls != err)
            xs_error(XS8005, s->expr->pos, "throw new Error(\"message\"), or an object of one of your classes", "%s cannot be thrown", ty_name(t));
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
            if (s->decl_type) { s->decl_type = resolve(s->decl_type, s->pos); if (s->decl_type->kind != et->kind) xs_error(XS8133, s->pos, NULL, "the elements are %s, not %s", ty_name(et), ty_name(s->decl_type)); }
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
        if (s->expr->type->kind == TY_STRING) { /* for (const ch of str): its code points, one string each (as JavaScript) */
            Expr *sep = xs_alloc(sizeof *sep);
            sep->kind = E_STRING;
            sep->pos = s->expr->pos;
            sep->str = "";
            Expr *call = xs_alloc(sizeof *call);
            call->kind = E_MEMBER;
            call->pos = s->expr->pos;
            call->lhs = s->expr;
            call->str = "split";
            call->is_call = true;
            call->field = -1;
            expr_list_push(&call->args, sep);
            s->expr = call;
            check_expr(s->expr, NULL);
        }
        if (ty_is_any(s->expr->type)) { /* for (const r of rows) with rows an any (query()'s rows): an any[], checked at run time */
            static Type *arr_any;
            if (!arr_any) arr_any = ty_array_of(ty_any);
            dyn_to(s->expr, arr_any);
        }
        if (s->expr->type->kind == TY_UNKNOWN && !ty_is_any(s->expr->type) && tsc_checked()) { /* for (const x of lista) with lista
                                                     unknown (narrowed by Array.isArray in a way xisto does not follow): unknown[], checked */
            static Type *arr_unk;
            if (!arr_unk) arr_unk = ty_array_of(ty_unknown);
            Expr *v = xs_alloc(sizeof *v);
            *v = *s->expr;
            memset(s->expr, 0, sizeof *s->expr);
            s->expr->kind = E_AS; s->expr->pos = v->pos; s->expr->field = -1; s->expr->lhs = v; s->expr->elem_type = arr_unk;
            v->prechecked = true;
            check_expr(s->expr, NULL);
        }
        if (s->expr->type->kind != TY_ARRAY && s->expr->type->kind != TY_STREAM)
            xs_error(XS8092, s->expr->pos, "for...of iterates arrays, strings, streams, Map and Set", "cannot iterate over %s", ty_name(s->expr->type));
        Type *elem = s->expr->type->elem;
        if (s->decl_type) {
            s->decl_type = resolve(s->decl_type, s->pos);
            if (!ty_equal(s->decl_type, elem)) xs_error(XS8133, s->pos, NULL, "the elements are %s, not %s", ty_name(elem), ty_name(s->decl_type));
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
    case S_CONTINUE:
        if (s->name) xs_error(XS6044, s->pos, "put the label on a loop that holds this statement", "no loop labeled '%s' around this %s", s->name, s->kind == S_BREAK ? "break" : "continue");
        if (s->kind == S_CONTINUE) goto check_continue;
        if (!loop_depth && !switch_depth) {
            xs_error(XS8066, s->pos, NULL, "'break' outside of a loop or switch");
        }
        return;
    check_continue:
        if (!loop_depth) {
            xs_error(XS8067, s->pos, NULL, "'continue' outside of a loop");
        }
        return;
    case S_SWITCH: {
        check_expr(s->expr, NULL);
        settle(s->expr);
        Type *t = s->expr->type;
        if (t->kind == TY_NULLABLE && (is_numeric(t->elem) || t->elem->kind == TY_STRING) && !t->elem->tname) {
            /* switch (pflIn) with pflIn: number | null: null matches none of the cases (JavaScript's ===), so the switch
               is on pflIn ?? a value no case has (NaN, equal to nothing; a string no case is) */
            bool case_null = false;
            for (int i = 0; i < s->ncases; i++) if (s->cases[i].value && s->cases[i].value->kind == E_NULL) case_null = true;
            if (!case_null) {
                Expr *d;
                if (is_numeric(t->elem)) {
                    d = xs_alloc(sizeof *d); d->kind = E_IDENT; d->pos = s->expr->pos; d->field = -1; d->str = "NaN";
                } else d = str_lit(s->expr->pos, "\x01(null)\x01");
                Expr *qq = xs_alloc(sizeof *qq);
                qq->kind = E_BINARY; qq->pos = s->expr->pos; qq->field = -1; qq->op = TK_QQ; qq->lhs = s->expr; qq->rhs = d;
                s->expr->prechecked = true;
                s->expr = qq;
                check_expr(qq, NULL);
                if (is_numeric(t->elem) && qq->type->kind == TY_INT) qq->type = ty_number;
                settle(qq);
                t = qq->type;
            }
        }
        if (!is_numeric(t) && t->kind != TY_STRING && t->kind != TY_BOOLEAN && t->kind != TY_ENUM)
            xs_error(XS8127, s->expr->pos, NULL, "switch works on int, number, string, boolean or enum, but this is %s", ty_name(t));
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
                if (!t) xs_error(XS8114, c->value->pos, NULL, "no member of %s has %s \"%s\"", ty_name(u), sx->str, c->value->str);
                check_block_union(c->body, sb, t);
            } else check_block(c->body);
        }
        switch_depth--;
        return;
    }
    case S_YIELD: {
        if (!cur_func || !cur_func->is_gen)
            xs_error(XS8070, s->pos, "make it a generator: function* name(): Generator<T> { ... }", "'yield' is used inside a generator");
        if (finally_depth) xs_error(XS6011, s->pos, NULL, "'yield' inside a try with finally is not supported yet");
        Type *elem = cur_func->ret->elem;
        check_expr(s->expr, elem);
        expect_type(s->expr, elem, xs_sprintf("value yielded by '%s'", cur_func->name));
        return;
    }
    case S_RETURN:
        if (!cur_func) xs_error(XS8068, s->pos, NULL, "'return' outside of a function");
        if (cur_func->is_gen && s->expr) xs_error(XS8074, s->expr->pos, "yield it, then return;", "a generator returns no value");
        if (!cur_func->ret) { ret_infer_stmt(s); return; } /* the return type is inferred: this return is part of it */
        if (s->expr) {
            if (cur_func->ret->kind == TY_VOID) {
                /* return f(); where f is void too (TypeScript allows it; a generic run<R> instantiated with void):
                   the call, then return; */
                check_expr(s->expr, NULL);
                if (cur_func->ret_promise && s->expr->type && s->expr->type->kind == TY_PROMISE) pr_await(s->expr); /* return p: its settling */
                /* return r where r held a T = void result (const r = await fn() in a generic instance): nothing to return */
                bool void_held = tsc_checked() && s->expr->type && s->expr->type->kind == TY_UNKNOWN && s->expr->kind == E_IDENT;
                if ((!s->expr->type || s->expr->type->kind != TY_VOID) && !void_held)
                    xs_error(XS8057, s->expr->pos, NULL, "'%s' is void and cannot return a value", cur_func->name);
                settle(s->expr);
                Stmt *call = xs_alloc(sizeof *call), *ret = xs_alloc(sizeof *ret);
                call->kind = S_EXPR;
                call->pos = s->pos;
                call->expr = s->expr;
                ret->kind = S_RETURN;
                ret->pos = s->pos;
                s->kind = S_BLOCK;
                s->expr = NULL;
                s->stmts = (StmtList){0};
                stmt_list_push(&s->stmts, call);
                stmt_list_push(&s->stmts, ret);
                return;
            }
            if (cur_func->ret_promise && s->expr->kind == E_MEMBER && s->expr->is_call && s->expr->str && !strcmp(s->expr->str, "reject") &&
                s->expr->lhs && s->expr->lhs->kind == E_IDENT && !strcmp(s->expr->lhs->str, "Promise") && s->expr->args.len == 1 && !lookup("Promise")) {
                /* return Promise.reject(err) in an async function: the promise it returns rejects with err, as throw err does */
                s->kind = S_THROW;
                s->expr = s->expr->args.items[0];
                check_stmt(s);
                return;
            }
            if ((checking_generated || (tsc_checked() && cur_func->name && !strcmp(cur_func->name, "arrow") && !ty_is_any(cur_func->ret))) &&
                cur_func->ret->kind == TY_UNKNOWN) { /* reflection's invoke of a method whose return
                                                                              type was inferred: void gives null */
                check_expr(s->expr, NULL);
                if (!cur_func->natural_ret && s->expr->type) cur_func->natural_ret = s->expr->type; /* (overload typing) */
                if (s->expr->type->kind == TY_VOID) {
                    Stmt *call = xs_alloc(sizeof *call), *ret = xs_alloc(sizeof *ret);
                    call->kind = S_EXPR; call->pos = s->pos; call->expr = s->expr;
                    ret->kind = S_RETURN; ret->pos = s->pos; ret->expr = xs_alloc(sizeof *ret->expr);
                    ret->expr->kind = E_NULL; ret->expr->pos = s->pos; ret->expr->field = -1;
                    s->kind = S_BLOCK; s->expr = NULL; s->stmts = (StmtList){0};
                    stmt_list_push(&s->stmts, call);
                    stmt_list_push(&s->stmts, ret);
                    check_stmt(ret);
                    return;
                }
                expect_type(s->expr, cur_func->ret, xs_sprintf("return value of '%s'", cur_func->name));
                return;
            }
            check_expr(s->expr, cur_func->ret);
            if (cur_func->ret->kind == TY_UNKNOWN && !ty_is_any(cur_func->ret) && !cur_func->natural_ret && s->expr->type)
                cur_func->natural_ret = s->expr->type; /* (() => this.grupo(x) where () => unknown goes: what it gives, for an overload) */
            if (cur_func->ret_promise && s->expr->type->kind == TY_PROMISE && cur_func->ret->kind != TY_PROMISE) pr_await(s->expr);
            if (cur_func->ret->kind == TY_BOOLEAN && s->expr->type->kind != TY_BOOLEAN && cur_func->name && strcmp(cur_func->name, "arrow") == 0 &&
                s->expr->type->kind != TY_VOID && s->expr->type->kind != TY_LITINT) /* xs.filter((x) => x.items.length): a predicate's value, as a condition */
                make_truthy(s->expr, "predicate");
            expect_type(s->expr, cur_func->ret, xs_sprintf("return value of '%s'", cur_func->name));
        } else if (ty_is_undef(cur_func->ret)) { /* return; in f(): undefined: its one value */
            s->expr = xs_alloc(sizeof *s->expr);
            s->expr->kind = E_NULL; s->expr->pos = s->pos; s->expr->field = -1;
            check_expr(s->expr, cur_func->ret);
        } else if (cur_func->ret->kind != TY_VOID && !cur_func->is_gen) {
            xs_error(XS8059, s->pos, NULL, "'%s' must return a value of type %s", cur_func->name, ty_name(cur_func->ret));
        }
        return;
    case S_BLOCK: check_block(s); return;
    }
}

/* the block's last top-level assignment to the local `name` is `name = v` with v never null */
static bool assigns_non_null(Stmt *body, const char *name) {
    Stmt **items = body->kind == S_BLOCK ? body->stmts.items : &body;
    int n = body->kind == S_BLOCK ? body->stmts.len : 1;
    for (int i = n - 1; i >= 0; i--) {
        Stmt *st = items[i];
        if (st->kind != S_EXPR || !st->expr || st->expr->kind != E_ASSIGN) continue;
        Expr *a = st->expr;
        if (!a->lhs || a->lhs->kind != E_IDENT || !a->lhs->str || strcmp(a->lhs->str, name) != 0) continue;
        if (a->op != TK_ASSIGN || !a->rhs || !a->rhs->type) return false;
        Type *t = a->rhs->type;
        return t->kind != TY_NULLABLE && t->kind != TY_NULL && t->kind != TY_UNKNOWN && !ty_is_any(t) && t->kind != TY_VOID;
    }
    return false;
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

/* a break that leaves this loop (one inside a nested loop or switch leaves that one; a labelled one may leave this) */
static bool breaks_out(Stmt *s, int depth) {
    if (!s) return false;
    switch (s->kind) {
    case S_BREAK: return depth == 0 || s->name != NULL;
    case S_BLOCK: for (int i = 0; i < s->stmts.len; i++) if (breaks_out(s->stmts.items[i], depth)) return true; return false;
    case S_IF: return breaks_out(s->body, depth) || breaks_out(s->else_body, depth);
    case S_TRY: return breaks_out(s->body, depth) || breaks_out(s->else_body, depth) || breaks_out(s->for_init, depth);
    case S_WHILE: case S_FOR: case S_FOROF: return breaks_out(s->body, depth + 1);
    case S_SWITCH: for (int i = 0; i < s->ncases; i++) if (breaks_out(s->cases[i].body, depth + 1)) return true; return false;
    default: return false;
    }
}
static bool always_returns(Stmt *s) {
    switch (s->kind) {
    case S_RETURN: return true;
    case S_WHILE: case S_FOR: /* for (;;) / while (true) with no break out: it never ends but by a return or a throw */
        return (!s->cond || (s->cond->kind == E_BOOL && s->cond->boolean)) && !breaks_out(s->body, 0);
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
    if (f->ret) f->ret = resolve(f->ret, f->pos);
    else if (!f->ret_infer) f->ret_infer = 1; /* left out: inferred from the body (fret) */
    { bool as = false; f->ret = ret_unpromise(f->ret, &as); if (as) f->ret_promise = true; } /* (): Promise<T>: T, and async (XB-106) */
    if (f->is_async) f->ret_promise = true;
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
/* let end = a.indexOf(x); if (end < 0) end = a.length: an integer when its start and every assignment are */
static const char **ii_lnames; static Expr **ii_linit; static int ii_ln;
static Stmt *ii_body; /* the function being inferred */
static bool ii_stmt_ok(Stmt *s, const char *self);
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
    if (s->kind == S_VAR && !s->is_const && !s->decl_type && s->init && s->name) {
        ii_lnames = realloc(ii_lnames, sizeof(char *) * (size_t)(ii_ln + 1)); ii_linit = realloc(ii_linit, sizeof(Expr *) * (size_t)(ii_ln + 1));
        ii_lnames[ii_ln] = s->name; ii_linit[ii_ln++] = s->init;
    }
    ii_scan(s->for_init); ii_scan(s->body); ii_scan(s->else_body);
    for (int i = 0; i < s->stmts.len; i++) ii_scan(s->stmts.items[i]);
    for (int i = 0; i < s->ncases; i++) ii_scan(s->cases[i].body);
}
static void ii_enter(Func *f, bool reset) {
    if (reset) ii_tn = ii_an = ii_cn = ii_ln = 0;
    ii_body = f->body;
    for (int i = 0; i < f->nparams; i++) ii_typed(f->params[i].name, f->params[i].type);
    ii_scan(f->body);
}
/* XB-088: modular state (an LCG, a hash): `last = (last * A + C) % M`. A product is no int by its form (it can pass
   2^53, where a number only loses precision), unless its operands are bounded: the integer literals and consts, and
   the variable itself when every assignment to it, anywhere in the program, is an integer literal or a `% K` of a
   literal K (so it stays below the largest K). The bound of the whole expression must stay inside the safe range. */
static bool ii_lit_of(Expr *e, double *v) {
    if (!e) return false;
    if (e->kind == E_NUMBER && e->is_int_lit) { *v = e->num; return true; }
    if (e->kind == E_UNARY && e->op == TK_MINUS && e->lhs && e->lhs->kind == E_NUMBER && e->lhs->is_int_lit) { *v = -e->lhs->num; return true; }
    if (e->kind == E_IDENT && e->str)
        for (int i = ii_cn - 1; i >= 0; i--) if (strcmp(ii_cnames[i], e->str) == 0) return ii_depth < 8 && ii_lit_of(ii_cinit[i], v);
    return false;
}
static double ii_mod_k(Expr *e) { /* `x % K` with a literal 0 < K <= 2^26: K; else 0 */
    double k;
    if (e && e->kind == E_BINARY && e->op == TK_PERCENT && ii_lit_of(e->rhs, &k) && k > 0 && k <= 67108864.0) return k;
    return 0;
}
/* a function with a parameter or a function-scope variable of that name: its uses of the name are its own */
static bool ii_shadows(Func *f, const char *name) {
    if (!f || !name) return false;
    for (int i = 0; i < f->nparams; i++) if (f->params[i].name && strcmp(f->params[i].name, name) == 0) return true;
    Stmt *b = f->body;
    if (b && b->kind == S_BLOCK)
        for (int i = 0; i < b->stmts.len; i++) {
            Stmt *v = b->stmts.items[i];
            if (v && v->kind == S_VAR && v->name && strcmp(v->name, name) == 0) return true;
        }
    return false;
}
bool ii_mod_scan_s_(Stmt *s, const char *self, double *b);
static double ii_self_bound; /* (of ii_self_bound_of: 0 not bounded) */
static const char *ii_self_bound_of;
static bool ii_mod_scan_e(Expr *e, const char *self, double *b) {
    if (!e) return true;
    if (e->kind == E_ASSIGN && e->lhs && e->lhs->kind == E_IDENT && e->lhs->str && strcmp(e->lhs->str, self) == 0) {
        double v, k;
        if (e->op != TK_ASSIGN) return false;
        if (ii_lit_of(e->rhs, &v)) { if (fabs(v) > *b) *b = fabs(v); }
        else if ((k = ii_mod_k(e->rhs)) > 0) { if (k > *b) *b = k; }
        else return false;
    }
    if ((e->kind == E_POSTFIX || (e->kind == E_UNARY && (e->op == TK_PLUSPLUS || e->op == TK_MINUSMINUS))) &&
        e->lhs && e->lhs->kind == E_IDENT && e->lhs->str && strcmp(e->lhs->str, self) == 0) return false;
    if (e->kind == E_ARROW && e->arrow && e->arrow->fn) { /* (closures assign what they capture) */
        Stmt *body = e->arrow->fn->body;
        if (!ii_mod_scan_s_(body, self, b)) return false;
    }
    if (!ii_mod_scan_e(e->lhs, self, b) || !ii_mod_scan_e(e->rhs, self, b)) return false;
    for (int i = 0; i < e->args.len; i++) if (!ii_mod_scan_e(e->args.items[i], self, b)) return false;
    return true;
}
bool ii_mod_scan_s_(Stmt *s, const char *self, double *b) {
    if (!s) return true;
    if (s->kind == S_VAR && s->name && strcmp(s->name, self) == 0 && s->init) {
        double v, k;
        if (ii_lit_of(s->init, &v)) { if (fabs(v) > *b) *b = fabs(v); }
        else if ((k = ii_mod_k(s->init)) > 0) { if (k > *b) *b = k; }
        else return false;
    }
    if (!ii_mod_scan_e(s->init, self, b) || !ii_mod_scan_e(s->expr, self, b) || !ii_mod_scan_e(s->cond, self, b) || !ii_mod_scan_e(s->for_update, self, b)) return false;
    if (!ii_mod_scan_s_(s->for_init, self, b) || !ii_mod_scan_s_(s->body, self, b) || !ii_mod_scan_s_(s->else_body, self, b)) return false;
    for (int i = 0; i < s->stmts.len; i++) if (!ii_mod_scan_s_(s->stmts.items[i], self, b)) return false;
    for (int i = 0; i < s->ncases; i++) if (!ii_mod_scan_e(s->cases[i].value, self, b) || !ii_mod_scan_s_(s->cases[i].body, self, b)) return false;
    return true;
}
static double ii_self_mod_bound(const char *self) { /* every assignment to self in the program: its bound, or 0 */
    double b = 0;
    if (!prog) return 0;
    for (int i = 0; i < prog->top.len; i++) if (!ii_mod_scan_s_(prog->top.items[i], self, &b)) return 0;
    for (int f = 0; f < prog->nfuncs; f++) if (!ii_shadows(prog->funcs[f], self) && !ii_mod_scan_s_(prog->funcs[f]->body, self, &b)) return 0;
    for (int c = 0; c < prog->nclasses; c++) {
        Class *cl = prog->classes[c];
        if (cl->builtin) continue;
        for (int m = 0; m < cl->nmethods; m++) if (!ii_shadows(cl->methods[m], self) && !ii_mod_scan_s_(cl->methods[m]->body, self, &b)) return 0;
        if (cl->ctor && !ii_shadows(cl->ctor, self) && !ii_mod_scan_s_(cl->ctor->body, self, &b)) return 0;
    }
    return b;
}
static double ii_bound(Expr *e, const char *self) { /* |e| at most this, or -1 */
    double v;
    if (!e) return -1;
    if (ii_lit_of(e, &v)) return fabs(v);
    if (e->kind == E_IDENT && e->str && strcmp(e->str, self) == 0) {
        if (!ii_self_bound_of || strcmp(ii_self_bound_of, self) != 0) { ii_self_bound_of = self; ii_self_bound = ii_self_mod_bound(self); }
        return ii_self_bound > 0 ? ii_self_bound : -1;
    }
    if (e->kind == E_BINARY && (e->op == TK_PLUS || e->op == TK_MINUS || e->op == TK_STAR)) {
        double a = ii_bound(e->lhs, self), b = ii_bound(e->rhs, self);
        if (a < 0 || b < 0) return -1;
        return e->op == TK_STAR ? a * b : a + b;
    }
    if (e->kind == E_BINARY && e->op == TK_PERCENT) { double k = ii_mod_k(e); return k > 0 ? k : -1; }
    return -1;
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
        for (int i = ii_ln - 1; i >= 0; i--)
            if (strcmp(ii_lnames[i], e->str) == 0) {
                if (ii_depth > 4 || !ii_body) return false;
                ii_depth++; bool r = int_shaped(ii_linit[i], e->str) && ii_stmt_ok(ii_body, e->str); ii_depth--;
                return r;
            }
        return false;
    case E_INDEX: return e->lhs && e->lhs->kind == E_IDENT && e->lhs->str && ii_in(ii_anames, ii_an, e->lhs->str);
    case E_MEMBER: /* (not x.length: it can be a getter of the program's own that gives a number, and the
                      types are not known yet here; i < a.length is a comparison, not an assignment, and is fine) */
        if (!e->is_call) { /* x.length: an int on a string, an array, a typed array; unless the program has a
                             field, getter or method of that name of its own */
            if (!e->str || strcmp(e->str, "length") != 0) return false;
            for (int c = 0; prog && c < prog->nclasses; c++) {
                Class *k = prog->classes[c];
                if (k->builtin) continue; /* (the built-in ones: their length is an int) */
                for (int f = 0; f < k->nfields; f++) if (k->fields[f].name && !strcmp(k->fields[f].name, "length")) return false;
                for (int f = 0; f < k->nmethods; f++)
                    if (k->methods[f]->name && (!strcmp(k->methods[f]->name, "length") || !strcmp(k->methods[f]->name, "__get_length"))) return false;
            }
            return true;
        }
        if (e->str && (!strcmp(e->str, "indexOf") || !strcmp(e->str, "findIndex"))) { /* a position: an int on a
               string, an array, a typed array; unless the program has a method of that name of its own */
            for (int c = 0; prog && c < prog->nclasses; c++)
                for (int k = 0; !prog->classes[c]->builtin && k < prog->classes[c]->nmethods; k++)
                    if (prog->classes[c]->methods[k]->name && !strcmp(prog->classes[c]->methods[k]->name, e->str)) return false;
            return true;
        }
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
            if (e->op == TK_PERCENT && ii_mod_k(e) > 0 && !getenv("XISTO_NO_MOD_INT")) { /* XB-088: (last * A + C) % M */
                double b = ii_bound(e->lhs, self);
                if (b >= 0 && b <= 9007199254740991.0) return true;
            }
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
static bool ii_int_lit(Expr *v) { /* 3, -3, c ? 1 : 8 (any depth of ?: whose branches are integer literals) */
    if (!v) return false;
    if (v->kind == E_UNARY && v->op == TK_MINUS) v = v->lhs;
    if (v && v->kind == E_COND && v->args.len == 2) return ii_int_lit(v->args.items[0]) && ii_int_lit(v->args.items[1]);
    return v && v->kind == E_NUMBER && v->is_int_lit;
}
static bool ii_candidate(Stmt *s) {
    if (!s || s->kind != S_VAR || s->decl_type || !s->init || !s->name) return false;
    return ii_int_lit(s->init);
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
            for (int f = 0; f < p->nfuncs && ok; f++) ok = ii_shadows(p->funcs[f], v->name) || ii_stmt_ok(p->funcs[f]->body, v->name);
            for (int c = 0; c < p->nclasses && ok; c++) {
                Class *cl = p->classes[c];
                for (int m = 0; m < cl->nmethods && ok; m++) ok = ii_shadows(cl->methods[m], v->name) || ii_stmt_ok(cl->methods[m]->body, v->name);
                if (cl->ctor) ok = ok && (ii_shadows(cl->ctor, v->name) || ii_stmt_ok(cl->ctor->body, v->name));
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

/* ---- a return type left out (function f(a: number) { return a * 2; }): the type its returns agree on ----
   Each `return e;` is checked with no expected type; the types merge (T and null: T | null; an int is a number, as
   TypeScript has it); `return [];` waits for the others. At the end every returned value is converted to the type
   found, as if it had been written. */
static void ret_infer_begin(Func *f) {
    f->ret_infer = 2;
    f->ret_acc = NULL;
    f->ret_null = f->ret_bare = false;
    f->ret_exprs.len = f->ret_late.len = 0;
}
static void ret_infer_stmt(Stmt *s) {
    Func *f = cur_func;
    const char *hint = "write the return type: f(): Todo { ... }";
    if (!s->expr) { f->ret_bare = true; return; }
    Expr *e = s->expr;
    if (e->kind == E_ARRAY_LIT && e->args.len == 0) { expr_list_push(&f->ret_late, e); return; }
    check_expr(e, f->ret_acc && f->ret_acc->kind != TY_VOID ? f->ret_acc : NULL);
    if (f->ret_promise && e->type->kind == TY_PROMISE) pr_await(e); /* an async function returning a promise: its value */
    Type *t = e->type;
    if (t->kind == TY_VOID) { /* return f(); where f is void: the call, then return; */
        if (f->ret_acc && f->ret_acc->kind != TY_VOID)
            xs_error(XS8057, e->pos, hint, "'%s' returns %s elsewhere, and nothing here", f->name, ty_name(f->ret_acc));
        f->ret_acc = ty_void;
        Stmt *call = xs_alloc(sizeof *call), *ret = xs_alloc(sizeof *ret);
        call->kind = S_EXPR; call->pos = s->pos; call->expr = e;
        ret->kind = S_RETURN; ret->pos = s->pos;
        s->kind = S_BLOCK; s->expr = NULL; s->stmts = (StmtList){0};
        stmt_list_push(&s->stmts, call);
        stmt_list_push(&s->stmts, ret);
        return;
    }
    expr_list_push(&f->ret_exprs, e);
    if (t->kind == TY_NULL) { f->ret_null = true; return; }
    if (t->kind == TY_NULLABLE) { f->ret_null = true; t = t->elem; }
    if (t->kind == TY_LITINT || t->kind == TY_INT) t = ty_number;
    if (!f->ret_acc) f->ret_acc = t;
    else if (f->ret_acc->kind == TY_VOID)
        xs_error(XS8057, e->pos, hint, "'%s' returns nothing elsewhere, and %s here", f->name, ty_name(t));
    else if (!ty_equal(f->ret_acc, t) && (t->kind == TY_UNKNOWN || f->ret_acc->kind == TY_UNKNOWN) && !ty_is_any(t) && !ty_is_any(f->ret_acc))
        f->ret_acc = ty_unknown; /* return v (an unknown) and return x elsewhere: an unknown (TypeScript: the union of both) */
    else if (!ty_equal(f->ret_acc, t))
        xs_error(XS3001, e->pos, hint, "'%s' returns %s and %s: write the return type", f->name, ty_name(f->ret_acc), ty_name(t));
}
static void ret_infer_end(Func *f, Pos pos) {
    const char *hint = "write the return type: f(): Todo | null { ... }";
    Type *t = f->ret_acc;
    f->ret_infer = 3;
    f->ret = ty_unknown; /* (until the end: an error below leaves uses of it quiet) */
    f->ret_failed = true;
    if (!t) {
        if (f->ret_null || f->ret_late.len) xs_error(XS2012, pos, hint, "cannot infer the return type of '%s' from %s alone", f->name, f->ret_null ? "null" : "[]");
        t = ty_void;
    }
    if (f->ret_null && t->kind != TY_NULLABLE && t->kind != TY_UNKNOWN) { /* (an unknown holds null) */
        if (!nullable_ok(t)) xs_error(XS2012, pos, hint, "'%s' returns %s or null: write the return type", f->name, ty_name(t));
        t = ty_nullable_of(t);
    }
    if (t->kind != TY_VOID && f->ret_bare) xs_error(XS8059, pos, hint, "'%s' returns %s, and nothing on some paths", f->name, ty_name(t));
    f->ret = t;
    f->ret_failed = false; /* (an error in a returned value below is its own) */
    for (int i = 0; i < f->ret_exprs.len; i++)
        expect_type(f->ret_exprs.items[i], t, xs_sprintf("return value of '%s'", f->name));
    for (int i = 0; i < f->ret_late.len; i++) {
        check_expr(f->ret_late.items[i], t);
        expect_type(f->ret_late.items[i], t, xs_sprintf("return value of '%s'", f->name));
    }
    f->ret_failed = false;
}

static void check_func(Func *f) {
    if (f->is_native) return; /* `declare`: the package's C */
    if (f->ret_infer >= 2) return; /* checked already, when its return type was needed */
    bool inferring = !f->ret;
    int errs = xs_error_count;
    if (inferring) ret_infer_begin(f);
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
                xs_error(XS9030, f->pos, NULL, "parameter '%s' is declared twice", f->params[i].name);
        const char *bname = f->params[i].name;
        if (ty_is_heap(f->params[i].type) && f->body && f->body->kind == S_BLOCK && !f->is_gen) {
            /* s = '...' on a string (or object) parameter: the parameter is read into a local of its name first,
               `let s = s_in`, which the body then reassigns (a parameter itself is borrowed, never reassigned) */
            bool reassigned = false, deep = false;
            for (int k = 0; k < nsev; k++)
                if (sev[k].kind == 'a' && strcmp(sev[k].name, bname) == 0) { reassigned = true; if (sev[k].depth > 0) deep = true; }
            Stmt *first = f->body->stmts.len ? f->body->stmts.items[0] : NULL;
            bool done = false;
            for (int k = 0; k < f->body->stmts.len && !done; k++) {
                Stmt *st = f->body->stmts.items[k];
                if (!st->generated || st->kind != S_VAR || !st->init || st->init->kind != E_IDENT) break;
                if (strcmp(st->name, bname) == 0) done = true;
            }
            (void)first;
            if (reassigned && !deep) {
                char *hidden = xs_sprintf("__in_%s", bname);
                if (!done) {
                    Stmt *v = xs_alloc(sizeof *v);
                    v->kind = S_VAR; v->pos = f->pos; v->name = (char *)bname; v->generated = true;
                    v->decl_type = f->params[i].type;
                    Expr *id = xs_alloc(sizeof *id);
                    id->kind = E_IDENT; id->pos = f->pos; id->str = hidden; id->field = -1;
                    v->init = id;
                    StmtList *L = &f->body->stmts;
                    stmt_list_push(L, v);
                    memmove(L->items + 1, L->items, sizeof(Stmt *) * (size_t)(L->len - 1));
                    L->items[0] = v;
                }
                bname = hidden;
            }
        }
        Binding *pb = declare(f->pos, bname, f->params[i].type, false, false, true);
        f->params[i].cname = pb->plain ? pb->plain : pb->cname;
        f->params[i].cell = pb->cell;
    }
    if (f->is_gen) {
        if (f->ret->kind != TY_STREAM) xs_error(XS8073, f->pos, "write the return type: function* f(): Generator<T>", "a generator returns Generator<T> (or Iterable<T>)");
        prog->uses_gen = true; /* its body runs on a coroutine stack */
    }
    check_block(f->body);
    pop_scope();
    if (inferring) { ret_infer_end(f, f->pos); if (xs_error_count > errs) f->ret_failed = true; }
    if (f->ret->kind != TY_VOID && !f->is_gen && !always_returns(f->body))
        xs_error(XS8058, f->pos, "add a return at the end of the function", "'%s' may reach its end without returning a %s", f->name, ty_name(f->ret));
    tmp_counter = saved_tmp;
    cur_func = NULL;
    cur_class = NULL;
}

/* Every field without an initializer must be assigned (this.f = ...) directly in the constructor body. */
/* the base's fields first (the same layout prefix), then the class's own; interfaces take their bases' fields */
static void flatten_fields(Class *c, int depth) {
    if (c->flattened) return;
    if (depth > 64) xs_error(XS9001, c->pos, NULL, "'%s' extends itself", c->name);
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
            if (!b || !b->is_interface) xs_error(XS9011, c->pos, NULL, "'%s' is not an interface %s can extend", c->iface_bases[i], c->name);
            ib[i] = b;
        }
        bases = ib;
        nb = c->niface_bases;
    }
    if (!nb) return;
    int total = 0;
    for (int i = 0; i < nb; i++) {
        c->flattened = false; /* a cycle comes back here */
        if (bases[i] == c || is_subclass(bases[i], c)) xs_error(XS9001, c->pos, NULL, "'%s' extends itself", c->name);
        flatten_fields(bases[i], depth + 1);
        c->flattened = true;
        total += bases[i]->nfields;
    }
    Field *all = xs_alloc(sizeof(Field) * (size_t)(total + c->nfields + 1));
    int n = 0;
    for (int i = 0; i < nb; i++)
        for (int k = 0; k < bases[i]->nfields; k++) {
            for (int j = 0; j < n; j++)
                if (strcmp(all[j].name, bases[i]->fields[k].name) == 0) xs_error(XS9021, c->pos, NULL, "field '%s' comes from two interfaces", all[j].name);
            all[n++] = bases[i]->fields[k];
        }
    int inherited = n;
    for (int k = 0; k < c->nfields; k++) {
        bool again = false;
        for (int j = 0; j < inherited && c->base && c->base->iface_base; j++)
            if (strcmp(all[j].name, c->fields[k].name) == 0) { /* readonly rotina = "A" of interface Proc { rotina: string; ... } */
                Field f = all[j];
                f.init = c->fields[k].init;
                f.pos = c->fields[k].pos;
                f.readonly = c->fields[k].readonly; /* (the implementation's own: it assigns it) */
                f.owner = c;
                all[j] = f;
                again = true;
            }
        if (again) continue;
        for (int j = 0; j < inherited && c->base && c->fields[k].init; j++)
            if (strcmp(all[j].name, c->fields[k].name) == 0) { /* protected readonly logger = new Logger(X.name) over the base's
                logger: the same field, the subclass's value set after the base's constructor ran, as in JavaScript */
                Field f = all[j];
                f.init = c->fields[k].init;
                f.pos = c->fields[k].pos;
                f.owner = c;
                all[j] = f;
                again = true;
            }
        if (again) continue;
        for (int j = 0; j < inherited; j++)
            if (strcmp(all[j].name, c->fields[k].name) == 0)
                xs_error(XS9022, c->fields[k].pos, "a subclass keeps the base's fields: assign it in the constructor instead", "field '%s' is already declared in %s", c->fields[k].name, nb == 1 ? bases[0]->name : "a base");
        all[n++] = c->fields[k];
    }
    c->fields = all;
    c->nfields = c->cap_fields = n;
    c->nbase_fields = c->base ? inherited : 0; /* an interface owns every field it lists */
}

/* the base's methods the class does not override join its list (still owned by the base); an override keeps the
   signature; a concrete class has no abstract method left */

static bool inline_cls(Type *t) { return t && t->kind == TY_OBJECT && t->cls && t->cls->name && strncmp(t->cls->name, "__Obj_", 6) == 0; }
/* a: the type of an override's parameter or result; b: the overridden one's. When both are object types written in
   place with the same fields (by name and type), the override takes b's */
static Type *inline_same(Type *a, Type *b) {
    if (!a || !b || ty_equal(a, b)) return a;
    if (a->kind == TY_ARRAY && b->kind == TY_ARRAY) return inline_same(a->elem, b->elem) == b->elem ? b : a;
    bool na = a->kind == TY_NULLABLE, nb = b->kind == TY_NULLABLE;
    if (na != nb) return a;
    Type *x = na ? a->elem : a, *y = nb ? b->elem : b;
    if (!inline_cls(x) || !inline_cls(y) || x->cls->nfields != y->cls->nfields) return a;
    for (int i = 0; i < x->cls->nfields; i++) {
        Field *g = find_field(y->cls, x->cls->fields[i].name, NULL);
        if (!g || !x->cls->fields[i].type || !g->type) return a;
        if (!ty_equal(x->cls->fields[i].type, g->type) && inline_same(x->cls->fields[i].type, g->type) != g->type) return a;
    }
    return b;
}
static bool implements_by_shape(Class *c);
static void flatten_methods(Class *c) {
    if (!c->base || c->nbase_fields < 0) return;
    if (c->base->base && !c->base->methods_flat) flatten_methods(c->base);
    if (c->methods_flat) return;
    c->methods_flat = true;
    if (implements_by_shape(c)) return; /* (XB-126: out of the interface's hierarchy, adapted where it goes) */
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
        if (!own->ret && bm->ret && own->ret_infer == 1) { own->ret = bm->ret; own->ret_infer = 0; } /* an override that
                                                                         leaves out its return type returns what it overrides */
        if (own->nparams < bm->nparams && own->body && !own->ntparams) { /* auditAppa(): void overriding auditAppa(a, b): TypeScript
                                                                            lets an override take fewer; it takes the rest, unnamed */
            bool prefix = true;
            for (int k = 0; prefix && k < own->nparams; k++) prefix = ty_equal(own->params[k].type, bm->params[k].type);
            if (prefix) {
                Param *ps = xs_alloc(sizeof(Param) * (size_t)bm->nparams);
                memcpy(ps, own->params, sizeof(Param) * (size_t)own->nparams);
                for (int k = own->nparams; k < bm->nparams; k++) {
                    memset(&ps[k], 0, sizeof(Param));
                    ps[k].name = xs_sprintf("__unused%d", k);
                    ps[k].type = bm->params[k].type;
                    { /* a call on the subclass gives only what it declares (JavaScript: the rest undefined, never read) */
                        Type *pt = ps[k].type;
                        Expr *d = NULL;
                        if (pt->kind == TY_INT || pt->kind == TY_NUMBER) d = num_lit(own->pos, 0, pt->kind == TY_INT);
                        else if (pt->kind == TY_STRING) d = str_lit(own->pos, "");
                        else if (pt->kind == TY_BOOLEAN) { d = xs_alloc(sizeof *d); d->kind = E_BOOL; d->pos = own->pos; d->field = -1; }
                        else if (pt->kind == TY_NULLABLE || bm->params[k].dflt) { d = xs_alloc(sizeof *d); d->kind = E_NULL; d->pos = own->pos; d->field = -1; }
                        if (bm->params[k].dflt && pt->kind != TY_NULLABLE) d = bm->params[k].dflt;
                        ps[k].dflt = d;
                    }
                }
                own->params = ps;
                own->nparams = bm->nparams;
            }
        }
        if (own->nparams == bm->nparams) { /* { existe: boolean; canal: Num } and { ...; canal: NumeroOracle | null }: object types
                                                written in place that have the same fields are one type */
            own->ret = inline_same(own->ret, bm->ret);
            for (int k = 0; k < own->nparams; k++) own->params[k].type = inline_same(own->params[k].type, bm->params[k].type);
        }
        bool same = own->nparams == bm->nparams && (!own->ret || !bm->ret || ty_equal(own->ret, bm->ret));
        for (int k = 0; same && k < own->nparams; k++) same = ty_equal(own->params[k].type, bm->params[k].type);
        if (!same && own->nparams != bm->nparams && tsc_checked() && !bm->is_abstract && !b->is_interface && !b->iface_methods && own->body) {
            /* upsert(input) over upsert(filter, set, setOnInsert?): TypeScript takes it, and JavaScript calls it by name: a call
               through the base (this.upsert(...) in the base's own code too) runs this one, with the arguments it was given
               (the extra ones dropped). Here a bridge with the base's signature does that: this.upsert(a0 as P0, ...),
               each argument converted (checked at run time where the types differ), its result back as the base's */
            own->shadow = true;
            Pos pos = own->pos;
            Func *br = xs_alloc(sizeof *br);
            *br = *bm;
            br->name = xs_sprintf("__ovr_%s", own->name);
            br->cls = c;
            br->mod = c->mod;
            br->pos = pos;
            br->overrides = NULL; br->noverrides = 0;
            br->is_abstract = false;
            br->is_native = false;
            br->used = false;
            br->params = xs_alloc(sizeof(Param) * (size_t)(bm->nparams + 1));
            for (int k = 0; k < bm->nparams; k++) {
                br->params[k] = bm->params[k];
                br->params[k].name = xs_sprintf("__oa%d", k);
                br->params[k].cname = NULL;
                br->params[k].is_prop = false;
                br->params[k].decos = (DecoList){0};
            }
            Expr *th = xs_alloc(sizeof *th);
            th->kind = E_THIS; th->pos = pos; th->field = -1;
            Expr *call = xs_alloc(sizeof *call);
            call->kind = E_MEMBER; call->pos = pos; call->field = -1; call->lhs = th; call->str = own->name; call->is_call = true;
            for (int k = 0; k < own->nparams && k < bm->nparams; k++) {
                Expr *id = xs_alloc(sizeof *id);
                id->kind = E_IDENT; id->pos = pos; id->field = -1; id->str = br->params[k].name;
                Expr *a = id;
                bool jc = false;
                if (!ty_equal(bm->params[k].type, own->params[k].type) && !conv_ok(bm->params[k].type, own->params[k].type, 0)) {
                    Type *ft = ty_strip_null(bm->params[k].type), *tt = ty_strip_null(own->params[k].type);
                    bool ju = json_ok_unknown;
                    json_ok_unknown = true;
                    jc = json_target(tt) && (json_ok(ft, 0) || ft->kind == TY_JSON);
                    json_ok_unknown = ju;
                }
                if (jc) { /* a Record (an object) where an interface of data goes: copied through JSON, as the data it is */
                    Expr *js = xs_alloc(sizeof *js);
                    js->kind = E_IDENT; js->pos = pos; js->field = -1; js->str = "JSON";
                    Expr *str = xs_alloc(sizeof *str);
                    str->kind = E_MEMBER; str->pos = pos; str->field = -1; str->lhs = js; str->str = "stringify"; str->is_call = true;
                    expr_list_push(&str->args, id);
                    Expr *js2 = xs_alloc(sizeof *js2);
                    *js2 = *js;
                    Expr *parse = xs_alloc(sizeof *parse);
                    parse->kind = E_MEMBER; parse->pos = pos; parse->field = -1; parse->lhs = js2; parse->str = "parse"; parse->is_call = true;
                    expr_list_push(&parse->args, str);
                    a = xs_alloc(sizeof *a);
                    a->kind = E_AS; a->pos = pos; a->field = -1; a->lhs = parse; a->elem_type = own->params[k].type;
                } else if (!ty_equal(bm->params[k].type, own->params[k].type) && !conv_ok(bm->params[k].type, own->params[k].type, 0)) {
                    Expr *u = xs_alloc(sizeof *u);
                    u->kind = E_AS; u->pos = pos; u->field = -1; u->lhs = id; u->elem_type = ty_unknown;
                    a = xs_alloc(sizeof *a);
                    a->kind = E_AS; a->pos = pos; a->field = -1; a->lhs = u; a->elem_type = own->params[k].type;
                }
                expr_list_push(&call->args, a);
            }
            Stmt *body = xs_alloc(sizeof *body);
            body->kind = S_BLOCK; body->pos = pos;
            Stmt *r = xs_alloc(sizeof *r);
            r->pos = pos;
            if (!bm->ret || bm->ret->kind == TY_VOID) { r->kind = S_EXPR; r->expr = call; }
            else {
                Expr *v = call;
                if (own->ret_promise) {
                    Expr *aw = xs_alloc(sizeof *aw);
                    aw->kind = E_AWAIT; aw->pos = pos; aw->field = -1; aw->lhs = call;
                    v = aw;
                }
                if (own->ret && !ty_equal(own->ret, bm->ret) && !conv_ok(own->ret, bm->ret, 0)) {
                    Expr *u = xs_alloc(sizeof *u);
                    u->kind = E_AS; u->pos = pos; u->field = -1; u->lhs = v; u->elem_type = ty_unknown;
                    Expr *w = xs_alloc(sizeof *w);
                    w->kind = E_AS; w->pos = pos; w->field = -1; w->lhs = u; w->elem_type = bm->ret;
                    v = w;
                }
                r->kind = S_RETURN; r->expr = v;
            }
            stmt_list_push(&body->stmts, r);
            br->body = body;
            br->cname = xs_sprintf("m%d_%s___ovr_%s", c->mod->id, c->name, own->name);
            own->bridge = br;
            c->methods = realloc(c->methods, sizeof(Func *) * (size_t)(c->nmethods + 1));
            c->methods[c->nmethods++] = br;
            for (Class *up = b; up; up = up->base) { /* every method it overrides, at every level up, learns about the bridge */
                Func *m = NULL;
                for (int k = 0; k < up->nmethods; k++) if (up->methods[k]->cls == up && strcmp(up->methods[k]->name, own->name) == 0) m = up->methods[k];
                if (!m) continue;
                m->overrides = realloc(m->overrides, sizeof(Func *) * (size_t)(m->noverrides + 1));
                m->overrides[m->noverrides++] = br;
                if (m->shadow) break;
            }
            continue;
        }
        if (!same) {
            Buf sig = {0};
            buf_puts(&sig, "(");
            for (int k = 0; k < bm->nparams; k++) buf_printf(&sig, "%s%s: %s", k ? ", " : "", bm->params[k].name, ty_name(bm->params[k].type));
            buf_printf(&sig, "): %s", ty_name(bm->ret));
            xs_error(XS8061, own->pos, xs_sprintf("keep the signature of %s.%s%s", b->name, bm->name, sig.data), "'%s' overrides %s.%s with another signature", own->name, b->name, bm->name);
        }
        if (find_field(b, own->name, NULL)) xs_error(XS8047, own->pos, NULL, "'%s' is a field of %s", own->name, b->name);
        /* every method it overrides, at every level up, learns about it */
        for (Class *up = b; up; up = up->base) {
            Func *m = NULL;
            for (int k = 0; k < up->nmethods; k++) if (up->methods[k]->cls == up && strcmp(up->methods[k]->name, own->name) == 0) m = up->methods[k];
            if (!m) continue;
            m->overrides = realloc(m->overrides, sizeof(Func *) * (size_t)(m->noverrides + 1));
            m->overrides[m->noverrides++] = own;
            if (m->shadow) break; /* (the methods above it take other parameters) */
        }
    }
    if (!c->is_abstract)
        for (int k = 0; k < c->nmethods; k++)
            if (c->methods[k]->is_abstract)
                xs_error(XS8007, c->pos, xs_sprintf("implement %s(...) in %s, or declare %s abstract", c->methods[k]->name, c->name, c->name),
                         "%s does not implement the abstract method %s.%s", c->name, c->methods[k]->cls->name, c->methods[k]->name);
}

/* ---- XB-126: structural interfaces ----
   TypeScript compares an interface with methods by its shape: a class that has each method, with a signature that
   fits, is one, whether or not it says `implements`. Here such an interface is an abstract class (a call through it
   picks the implementation by the object's class), so a class outside its hierarchy gets an ADAPTER: a subclass of
   the interface that holds the object (field __t) and whose methods call the object's, converting the arguments
   (the interface's types into the class's) and the result (back) with the conversions expect_type already makes
   (data shapes copied field by field, T into T | null, int into number, ...). One adapter class per (class,
   interface); the exact `implements` path stays a plain subclass with no adapter (the fast one).
   Identity: each conversion makes a new adapter object, so two conversions of one object give two objects (=== is
   false between them; TypeScript has the one object). Callers such as Nest only call methods through them. */
static bool iface_fits(Class *src, Class *ifc, bool infer, Pos pos, int depth, char **why);
/* a value of type `from` goes where `to` goes through expect_type's conversions (a conservative mirror of them,
   with no error reported: the adapter is only made when every one of its calls will check) */
static bool conv_ok(Type *from, Type *to, int depth);
static bool shape_pair_ok(Class *src, Class *dst, int depth) { /* the conditions of shape_copy, and each field converts */
    if (!plain_data(src)) return false;
    bool data_cls = !dst->builtin && !dst->is_interface && !dst->is_abstract && dst->nsubs == 0 && !dst->tparams &&
                    (!dst->ctor || dst->ctor->nparams == 0) && dst->nfields > 0 && !dst->base_name &&
                    (src->is_interface || made_shape(src));
    if (!(dst->is_interface || made_shape(dst) || data_cls) || dst->nmethods != 0 || dst->iface_methods) return false;
    if (!made_shape(src) && !src->is_interface && src->nmethods == 0 && !src->nfields) return false;
    for (int i = 0; i < dst->nfields; i++) {
        Field *s = find_field(src, dst->fields[i].name, NULL);
        if (!s) { if (!dst->fields[i].optional) return false; continue; }
        if (!s->type || !dst->fields[i].type || !conv_ok(s->type, dst->fields[i].type, depth + 1)) return false;
    }
    return true;
}
static bool conv_obj_ok(Class *f, Class *t, int depth) {
    if (f == t) return true;
    if (!t->is_interface && is_subclass(f, t)) return true;
    if (t->iface_base && !f->builtin) return iface_fits(f, t, false, (Pos){0, 0}, depth + 1, NULL);
    return shape_pair_ok(f, t, depth);
}
static bool conv_ok(Type *from, Type *to, int depth) {
    if (!from || !to || depth > 8) return false;
    if (ty_equal(from, to)) return true;
    if (from->kind == TY_VOID || to->kind == TY_VOID) return false;
    if (ty_is_any(to) || to->kind == TY_UNKNOWN) return true;
    if (from->kind == TY_LITINT && is_numeric(to)) return true;
    if (from->kind == TY_INT && to->kind == TY_NUMBER) return true;
    if (to->kind == TY_NULLABLE) {
        if (from->kind == TY_NULL) return true;
        if (from->kind == TY_NULLABLE) /* (a primitive's null is its own: only objects convert inside T | null) */
            return from->elem->kind == TY_OBJECT && to->elem->kind == TY_OBJECT && conv_obj_ok(from->elem->cls, to->elem->cls, depth);
        return conv_ok(from, to->elem, depth);
    }
    if (from->kind == TY_NULLABLE) return false;
    if (from->kind == TY_OBJECT && to->kind == TY_OBJECT) return conv_obj_ok(from->cls, to->cls, depth);
    if (from->kind == TY_ARRAY && to->kind == TY_ARRAY) {
        if (to->elem->kind == TY_UNKNOWN && ty_is_heap(from->elem)) return true;
        if (to->elem->kind == TY_NULLABLE && ty_is_heap(to->elem->elem) && ty_equal(from->elem, to->elem->elem)) return true;
        if (from->elem->kind == TY_INT && to->elem->kind == TY_NUMBER) return true;
        Type *fe = ty_strip_null(from->elem), *te = ty_strip_null(to->elem);
        if (from->elem->kind == TY_NULLABLE && to->elem->kind != TY_NULLABLE) return false;
        return fe->kind == TY_OBJECT && te->kind == TY_OBJECT && shape_ok(from->elem, to->elem) && shape_pair_ok(fe->cls, te->cls, depth);
    }
    return false;
}
/* a type as the message shows it: an object type written in place by its fields (its class name is a hash) */
static const char *ad_tname(Type *t) {
    if (t && t->kind == TY_NULLABLE && t->elem->kind == TY_OBJECT && made_shape(t->elem->cls)) return xs_sprintf("%s | null", ad_tname(t->elem));
    if (!t || t->kind != TY_OBJECT || !made_shape(t->cls) || t->cls->name[0] == '{') return ty_name(t);
    Buf b = {0};
    buf_puts(&b, "{ ");
    for (int i = 0; i < t->cls->nfields; i++) {
        Field *f = &t->cls->fields[i];
        bool opt = f->optional && f->type && f->type->kind == TY_NULLABLE;
        buf_printf(&b, "%s%s: %s", f->name, opt ? "?" : "", ad_tname(opt ? f->type->elem : f->type));
        buf_puts(&b, i + 1 < t->cls->nfields ? "; " : " ");
    }
    buf_puts(&b, "}");
    return b.data;
}
static int fn_required(Func *f) {
    int n = 0;
    for (int k = 0; k < f->nparams; k++) if (!f->params[k].dflt && !f->params[k].rest) n = k + 1;
    return n;
}
/* the method `cm` of a class serves where the interface's `im` is called: its parameters take what im's callers pass,
   its result is what they get (ret NULL, not inferred yet: `infer` infers it, otherwise it is left to the adapter's
   check) */
static bool method_fits(Func *cm, Func *im, Class *src, Class *ifc, bool infer, Pos pos, int depth, char **why) {
    const char *iname = ifc->name, *cn = src->name, *mn = im->name;
    if (!cm || cm->is_getter || cm->is_setter || cm->is_gen) {
        if (why) *why = xs_sprintf("%s has no method '%s', which %s declares", cn, mn, iname);
        return false;
    }
    if (cm->access) {
        if (why) *why = xs_sprintf("%s.%s is %s; %s.%s is public", cn, mn, cm->access == 2 ? "private" : "protected", iname, mn);
        return false;
    }
    if (cm->ntparams && !im->ntparams && fn_required(cm) <= im->nparams) return true; /* (query<T = any>(sql, ..): Promise<T> where
        the interface's query(sql, binds?): R goes: the adapter calls it, its T taken from R; the call is checked there) */
    if (cm->ntparams || im->ntparams) {
        if (why) *why = xs_sprintf("%s.%s is generic: write the same signature as %s.%s, or implement %s", cn, mn, iname, mn, iname);
        return false;
    }
    int req = fn_required(cm);
    if (req > im->nparams) {
        if (why) *why = xs_sprintf("%s.%s needs %d argument(s), but %s.%s passes %d", cn, mn, req, iname, mn, im->nparams);
        return false;
    }
    int n = cm->nparams < im->nparams ? cm->nparams : im->nparams;
    for (int k = 0; k < n; k++) {
        Param *cp = &cm->params[k], *ip = &im->params[k];
        bool ok = (cp->rest || ip->rest) ? cp->rest == ip->rest && ty_equal(cp->type, ip->type) : conv_ok(ip->type, cp->type, depth);
        if (!ok) {
            if (why) *why = xs_sprintf("parameter '%s' of %s.%s is %s, but %s.%s passes %s", cp->name, cn, mn, ad_tname(cp->type), iname, mn, ad_tname(ip->type));
            return false;
        }
    }
    if (im->ret && im->ret->kind == TY_VOID) return true; /* (the result is dropped: any will do) */
    if (cm->ret_promise && !im->ret_promise) {
        if (why) *why = xs_sprintf("%s.%s is async; %s.%s returns %s", cn, mn, iname, mn, ty_name(im->ret));
        return false;
    }
    Type *cr = cm->ret;
    if (!cr && infer && cm->ret_infer && !cm->ret_failed) cr = fret(cm, pos);
    if (!cr) return true;
    if (!im->ret || !conv_ok(cr, im->ret, depth)) {
        if (why) *why = xs_sprintf("%s.%s returns %s, but %s.%s must return %s", cn, mn, ad_tname(cr), iname, mn, im->ret ? ad_tname(im->ret) : "?");
        return false;
    }
    return true;
}
static bool iface_fits(Class *src, Class *ifc, bool infer, Pos pos, int depth, char **why) {
    if (depth > 4 || src == ifc || src->builtin || src->gen_state) return false;
    for (int k = 0; k < ifc->nmethods; k++) {
        Func *im = ifc->methods[k];
        if (!im->is_abstract) continue;
        if (!method_fits(find_method(src, im->name), im, src, ifc, infer, pos, depth, why)) return false;
    }
    return true;
}
typedef struct { Class *src, *ifc, *ad; } IfaceAd;
static IfaceAd *iface_ads;
static int niface_ads;
static Expr *ad_ident(Pos pos, const char *name) {
    Expr *x = xs_alloc(sizeof *x);
    x->kind = E_IDENT; x->pos = pos; x->field = -1; x->str = (char *)name;
    return x;
}
static Expr *ad_this_t(Pos pos) { /* this.__t */
    Expr *self = xs_alloc(sizeof *self);
    self->kind = E_THIS; self->pos = pos; self->field = -1;
    Expr *m = xs_alloc(sizeof *m);
    m->kind = E_MEMBER; m->pos = pos; m->field = -1; m->lhs = self; m->str = "__t";
    return m;
}
static Stmt *ad_block(Pos pos, Stmt *one) {
    Stmt *b = xs_alloc(sizeof *b);
    b->kind = S_BLOCK; b->pos = pos;
    stmt_list_push(&b->stmts, one);
    return b;
}
/* class <src> as <ifc> extends ifc { constructor(__t: src) { this.__t = __t; } m(__a0: A): R { return this.__t.m(__a0); } ... }
   built as checked-to-be declarations (types already resolved) and checked with the generic instances */
static Class *make_adapter(Class *src, Class *ifc) {
    Pos pos = src->pos;
    Class *a = xs_alloc(sizeof *a);
    a->name = xs_sprintf("%s as %s", src->name, ifc->name);
    a->pos = pos;
    a->mod = src->mod;
    a->cname = xs_sprintf("%s__as_%s", src->cname, ifc->cname);
    a->adapter_of = src;
    a->base_name = ifc->name;
    a->base_pos = pos;
    a->base = ifc;
    ifc->subs = realloc(ifc->subs, sizeof(Class *) * (size_t)(ifc->nsubs + 1));
    ifc->subs[ifc->nsubs++] = a;
    Type *st = ty_object_of(src);
    a->fields = xs_alloc(sizeof(Field));
    a->fields[0].name = "__t";
    a->fields[0].pos = pos;
    a->fields[0].type = st;
    a->fields[0].cname = field_cname("__t");
    a->fields[0].readonly = true;
    a->fields[0].owner = a;
    a->nfields = a->cap_fields = 1;
    a->nbase_fields = 0; /* (an interface with methods has no fields) */
    a->flattened = true;
    Func *ct = xs_alloc(sizeof *ct);
    ct->name = "constructor"; ct->pos = pos; ct->mod = src->mod; ct->cls = a; ct->is_ctor = true; ct->ret = ty_void;
    ct->params = xs_alloc(sizeof(Param));
    ct->params[0].name = "__t";
    ct->params[0].type = st;
    ct->nparams = 1;
    ct->cname = xs_sprintf("%s_new", a->cname);
    {
        Expr *as = xs_alloc(sizeof *as);
        as->kind = E_ASSIGN; as->op = TK_ASSIGN; as->pos = pos; as->field = -1; as->lhs = ad_this_t(pos); as->rhs = ad_ident(pos, "__t");
        Stmt *s = xs_alloc(sizeof *s);
        s->kind = S_EXPR; s->pos = pos; s->expr = as;
        ct->body = ad_block(pos, s);
    }
    a->ctor = ct;
    for (int k = 0; k < ifc->nmethods; k++) {
        Func *im = ifc->methods[k];
        if (!im->is_abstract) continue;
        Func *cm = find_method(src, im->name);
        Func *f = xs_alloc(sizeof *f);
        f->name = im->name; f->pos = cm && cm->pos.line ? cm->pos : pos; f->mod = src->mod; f->cls = a;
        f->params = xs_alloc(sizeof(Param) * (size_t)(im->nparams ? im->nparams : 1));
        for (int i = 0; i < im->nparams; i++) {
            f->params[i].name = xs_sprintf("__a%d", i);
            f->params[i].type = im->params[i].type;
            f->params[i].dflt = im->params[i].dflt;
            f->params[i].rest = im->params[i].rest;
        }
        f->nparams = im->nparams;
        f->ret = im->ret;
        f->ret_promise = im->ret_promise;
        f->is_async = im->ret_promise;
        f->cname = xs_sprintf("%s_m_%s", a->cname, im->name);
        int n = cm && cm->nparams < im->nparams ? cm->nparams : im->nparams;
        /* this.__t.m(__a0, ...): the class's parameters take what it declares; an optional one the interface may leave out
           (binds?: T) where the class has a default (binds: T = []): left out when it is, so the default applies, as in
           JavaScript (`__aK == null ? m(.., __aK-1) : m(.., __aK)`) */
        Expr *call = NULL;
        int cut = n;
        while (cut > 0 && cm && cm->params[cut - 1].dflt && !cm->params[cut - 1].rest && im->params[cut - 1].type->kind == TY_NULLABLE &&
               cm->params[cut - 1].type && cm->params[cut - 1].type->kind != TY_NULLABLE)
            cut--;
        for (int upto = cut; upto <= n; upto++) {
            Expr *c1 = xs_alloc(sizeof *c1);
            c1->kind = E_MEMBER; c1->pos = f->pos; c1->field = -1; c1->is_call = true; c1->lhs = ad_this_t(f->pos); c1->str = im->name;
            for (int i = 0; i < upto; i++) {
                Expr *x = ad_ident(f->pos, f->params[i].name);
                if (im->params[i].rest) { /* ...xs passed on spread */
                    Expr *sp = xs_alloc(sizeof *sp);
                    sp->kind = E_UNARY; sp->op = TK_ELLIPSIS; sp->pos = f->pos; sp->field = -1; sp->lhs = x;
                    x = sp;
                }
                expr_list_push(&c1->args, x);
            }
            if (!call) { call = c1; continue; }
            Expr *nul = xs_alloc(sizeof *nul);
            nul->kind = E_NULL; nul->pos = f->pos; nul->field = -1;
            Expr *test = xs_alloc(sizeof *test);
            test->kind = E_BINARY; test->op = TK_EQ; test->loose = true; test->pos = f->pos; test->field = -1;
            test->lhs = ad_ident(f->pos, f->params[upto - 1].name); test->rhs = nul;
            Expr *cond = xs_alloc(sizeof *cond);
            cond->kind = E_COND; cond->pos = f->pos; cond->field = -1; cond->lhs = test;
            expr_list_push(&cond->args, call);
            expr_list_push(&cond->args, c1);
            call = cond;
        }
        Stmt *s = xs_alloc(sizeof *s);
        s->pos = f->pos;
        if (f->ret && f->ret->kind == TY_VOID) { s->kind = S_EXPR; s->expr = call; }
        else { s->kind = S_RETURN; s->expr = call; }
        f->body = ad_block(f->pos, s);
        a->methods = realloc(a->methods, sizeof(Func *) * (size_t)(a->nmethods + 1));
        a->methods[a->nmethods++] = f;
    }
    a->cap_methods = a->nmethods;
    flatten_methods(a); /* (registers each method as the override of the interface's: the call through it lands here) */
    prog->classes = realloc(prog->classes, sizeof(Class *) * (size_t)(prog->nclasses + 1));
    prog->cap_classes = prog->nclasses + 1; /* keep the parser's add_class in step */
    prog->classes[prog->nclasses++] = a;
    class_work = realloc(class_work, sizeof(Class *) * (size_t)(nclass_work + 1));
    class_work[nclass_work++] = a;
    ifc->adapters = realloc(ifc->adapters, sizeof(Class *) * (size_t)(ifc->nadapters + 1));
    ifc->adapters[ifc->nadapters++] = a;
    return a;
}

/* NOOP_CONTEXT = { getHandler: () => () => undefined, getClass: () => class {} } as unknown as ExecutionContext: an
   object literal standing for a class (or an interface) with methods, as fakes and mocks are written. A class made
   for the literal extends the target: each method the literal gives calls the literal's function (held in a field);
   a method it does not give throws TypeError "<name> is not a function" when called (JavaScript's), not at the cast;
   a field of the target the literal gives is set, the others keep their initial values (null where none). */
static int nfakes;
static bool fake_target(Class *t, Expr *lit) {
    if (!t || t->builtin || t->is_tuple || t->fake || lit->kind != E_OBJLIT || !lit->keys || objlit_has_spread(lit)) return false;
    bool any_m = false;
    for (int i = 0; i < lit->args.len; i++) {
        Func *m = find_method(t, lit->keys[i]);
        if (m && !m->is_getter && !m->is_setter) any_m = true;
        else if (!m && !find_field(t, lit->keys[i], NULL)) { /* (an extra key: TypeScript's cast allows it; left out) */ }
    }
    return any_m;
}
static void fake_object(Expr *e, Class *t) {
    Pos pos = e->pos;
    Class *a = xs_alloc(sizeof *a);
    int id = nfakes++;
    a->name = xs_sprintf("{ fake %d } as %s", id, t->name);
    a->pos = pos;
    a->mod = cur_mod;
    a->cname = xs_sprintf("fake%d_%s", id, t->cname ? t->cname : "c");
    a->fake = true;
    a->used = true;
    a->exported = true;
    a->base_name = t->name;
    a->base_pos = pos;
    a->base = t;
    for (Class *u = t; u; u = u->base) u->used = true;
    t->subs = realloc(t->subs, sizeof(Class *) * (size_t)(t->nsubs + 1));
    t->subs[t->nsubs++] = a;
    Func *ct = xs_alloc(sizeof *ct);
    ct->name = "constructor"; ct->pos = pos; ct->mod = cur_mod; ct->cls = a; ct->is_ctor = true; ct->ret = ty_void;
    ct->params = xs_alloc(sizeof(Param) * (size_t)(e->args.len + 1));
    Stmt *body = xs_alloc(sizeof *body);
    body->kind = S_BLOCK; body->pos = pos;
    Expr *nw = xs_alloc(sizeof *nw);
    nw->kind = E_NEW_OBJ; nw->pos = pos; nw->field = -1; nw->str = a->name; nw->cls = a;
    a->fields = xs_alloc(sizeof(Field) * (size_t)(e->args.len + 1));
    for (int i = 0; i < e->args.len; i++) {
        const char *k = e->keys[i];
        Func *m = find_method(t, k);
        Field *tf = m ? NULL : find_field(t, k, NULL);
        if (!m && !tf) continue;
        if (m && (m->is_getter || m->is_setter)) continue;
        const char *fname = m ? xs_sprintf("__fake_%s", k) : k;
        Type *ft = NULL;
        if (m) {
            Type **ps = xs_alloc(sizeof(Type *) * (size_t)(m->nparams + 1));
            for (int q = 0; q < m->nparams; q++) ps[q] = m->params[q].type;
            /* (its result held as unknown when it is a reference: getHandler: () => () => undefined gives a function where a
               RouteHandler goes, as JavaScript allows; the method checks it where it gives it) */
            Expr *fv = e->args.items[i];
            Expr *body = fv->kind == E_ARROW && fv->arrow->expr_body && fv->arrow->fn->body && fv->arrow->fn->body->stmts.len == 1 ?
                         fv->arrow->fn->body->stmts.items[0]->expr : NULL;
            bool loose = m->ret && ty_is_heap(m->ret) && m->ret->kind != TY_UNKNOWN && !m->ret_promise &&
                         !(body && (body->kind == E_OBJLIT || body->kind == E_ARRAY_LIT)); /* (a literal result takes the type) */
            ft = ty_func(ps, m->nparams, loose ? ty_unknown : m->ret);
            ft->async_ret = m->ret_promise;
            Field *nf = &a->fields[a->nfields++];
            memset(nf, 0, sizeof *nf);
            nf->name = fname; nf->pos = pos; nf->type = ft; nf->cname = field_cname(fname); nf->owner = a;
        } else ft = tf->type;
        Param *pp = &ct->params[ct->nparams++];
        memset(pp, 0, sizeof *pp);
        pp->name = xs_sprintf("__fv%d", i);
        pp->type = ft;
        Expr *self = xs_alloc(sizeof *self);
        self->kind = E_THIS; self->pos = pos; self->field = -1;
        Expr *mem = xs_alloc(sizeof *mem);
        mem->kind = E_MEMBER; mem->pos = pos; mem->field = -1; mem->lhs = self; mem->str = (char *)fname;
        Expr *as = xs_alloc(sizeof *as);
        as->kind = E_ASSIGN; as->op = TK_ASSIGN; as->pos = pos; as->field = -1; as->lhs = mem; as->rhs = ad_ident(pos, pp->name);
        Stmt *st = xs_alloc(sizeof *st);
        st->kind = S_EXPR; st->pos = pos; st->expr = as;
        stmt_list_push(&body->stmts, st);
        expr_list_push(&nw->args, e->args.items[i]);
    }
    a->cap_fields = a->nfields;
    ct->body = body;
    ct->cname = xs_sprintf("%s_new", a->cname);
    a->ctor = ct;
    /* every method of the target: the literal's function, or TypeError when called */
    for (int k = 0; k < t->nmethods; k++) {
        Func *tm = t->methods[k];
        if (tm->is_getter || tm->is_setter || tm->is_ctor || tm->ntparams || tm->is_gen) continue;
        bool dup = false;
        for (int j = 0; j < a->nmethods; j++) if (!strcmp(a->methods[j]->name, tm->name)) dup = true;
        if (dup) continue;
        bool given = false;
        for (int i = 0; i < e->args.len; i++) if (!strcmp(e->keys[i], tm->name)) given = true;
        Func *f = xs_alloc(sizeof *f);
        f->name = tm->name; f->pos = pos; f->mod = cur_mod; f->cls = a;
        f->params = xs_alloc(sizeof(Param) * (size_t)(tm->nparams + 1));
        for (int q = 0; q < tm->nparams; q++) {
            memset(&f->params[q], 0, sizeof(Param));
            f->params[q].name = xs_sprintf("__a%d", q);
            f->params[q].type = tm->params[q].type;
            f->params[q].dflt = tm->params[q].dflt;
            f->params[q].rest = tm->params[q].rest;
        }
        f->nparams = tm->nparams;
        f->ret = tm->ret;
        f->ret_promise = tm->ret_promise;
        f->is_async = tm->ret_promise;
        f->cname = xs_sprintf("%s_m_%s", a->cname, tm->name);
        Stmt *s1 = xs_alloc(sizeof *s1);
        s1->pos = pos;
        if (given) {
            Expr *self = xs_alloc(sizeof *self);
            self->kind = E_THIS; self->pos = pos; self->field = -1;
            Expr *mem = xs_alloc(sizeof *mem);
            mem->kind = E_MEMBER; mem->pos = pos; mem->field = -1; mem->lhs = self; mem->str = xs_sprintf("__fake_%s", tm->name);
            Expr *call = xs_alloc(sizeof *call);
            call->kind = E_CALL; call->pos = pos; call->field = -1; call->lhs = mem;
            for (int q = 0; q < tm->nparams; q++) expr_list_push(&call->args, ad_ident(pos, f->params[q].name));
            if (!tm->ret || tm->ret->kind == TY_VOID) { s1->kind = S_EXPR; s1->expr = call; }
            else {
                Expr *v = call;
                Field *ff = NULL;
                for (int q = 0; q < a->nfields; q++) if (!strcmp(a->fields[q].name, xs_sprintf("__fake_%s", tm->name))) ff = &a->fields[q];
                if (ff && ff->type->kind == TY_FUNC && ff->type->ret && ff->type->ret->kind == TY_UNKNOWN && tm->ret->kind != TY_UNKNOWN) {
                    Expr *as = xs_alloc(sizeof *as);
                    as->kind = E_AS; as->pos = pos; as->field = -1; as->lhs = call; as->elem_type = tm->ret;
                    v = as;
                }
                s1->kind = S_RETURN; s1->expr = v;
            }
        } else {
            Expr *ne = xs_alloc(sizeof *ne);
            ne->kind = E_CALL; ne->pos = pos; ne->field = -1; ne->builtin.cfn = "xs_type_error_not_fn";
            ne->type = ty_object_of(find_class("Error"));
            expr_list_push(&ne->args, str_lit(pos, tm->name));
            s1->kind = S_THROW; s1->expr = ne;
        }
        f->body = ad_block(pos, s1);
        a->methods = realloc(a->methods, sizeof(Func *) * (size_t)(a->nmethods + 1));
        a->methods[a->nmethods++] = f;
    }
    a->cap_methods = a->nmethods;
    prog->classes = realloc(prog->classes, sizeof(Class *) * (size_t)(prog->nclasses + 1));
    prog->cap_classes = prog->nclasses + 1;
    prog->classes[prog->nclasses++] = a;
    sym_add(&classes_by_name, a->name, a);
    Module *was = cur_mod;
    flatten_fields(a, 0);
    for (int k = 0; k < a->nfields; k++) if (!a->fields[k].cname) a->fields[k].cname = field_cname(a->fields[k].name);
    for (int k = 0; k < a->nmethods; k++) if (a->methods[k]->cls == a) resolve_signature(a->methods[k]);
    resolve_signature(ct);
    flatten_methods(a);
    if (cur_mod != was) enter_module(was);
    class_work = realloc(class_work, sizeof(Class *) * (size_t)(nclass_work + 1));
    class_work[nclass_work++] = a;
    *e = *nw;
    check_expr(e, NULL);
    upcast(e, ty_object_of(t));
}
/* the adapter of src for ifc, made the first time (NULL when src does not fit: why says the first thing that does
   not; a failure is remembered too) */
static Class *iface_adapter(Class *src, Class *ifc, Pos pos, char **why) {
    for (int i = 0; i < niface_ads; i++)
        if (iface_ads[i].src == src && iface_ads[i].ifc == ifc) {
            if (!iface_ads[i].ad && why) iface_fits(src, ifc, false, pos, 0, why);
            return iface_ads[i].ad;
        }
    Module *was = cur_mod;
    bool fits = iface_fits(src, ifc, true, pos, 0, why);
    if (was && cur_mod != was) enter_module(was); /* (inferring a result type checks a body in its module) */
    Class *a = NULL;
    if (fits) {
        Scope *sc = scope; Class *cc = cur_class; Func *cf = cur_func;
        a = make_adapter(src, ifc);
        scope = sc; cur_class = cc; cur_func = cf;
        if (was && cur_mod != was) enter_module(was);
    }
    iface_ads = realloc(iface_ads, sizeof(IfaceAd) * (size_t)(niface_ads + 1));
    iface_ads[niface_ads++] = (IfaceAd){src, ifc, a};
    return a;
}
/* expect_type: an object of a class where an interface with methods goes, the class not one of its subclasses:
   new <adapter>(value) (x === null ? null : new <adapter>(x) inside T | null) */
static bool iface_adapt(Expr *e, Type *want, const char *what) {
    Type *wa = want->kind == TY_NULLABLE ? want->elem : want; /* (hosts?: { toString(): string }[] | null too) */
    if (e->type && e->type->kind == TY_ARRAY && wa->kind == TY_ARRAY && e->type->elem->kind == TY_OBJECT && wa->elem->kind == TY_OBJECT &&
        wa->elem->cls->iface_base && !e->type->elem->cls->builtin && !e->type->elem->cls->adapter_of &&
        e->type->elem->cls != wa->elem->cls && !is_subclass(e->type->elem->cls, wa->elem->cls)) {
        /* hosts: HostAddress[] where { toString(): string }[] goes: a new array of each one's adapter (xs.map) */
        char *why = NULL;
        if (!iface_adapter(e->type->elem->cls, wa->elem->cls, e->pos, &why)) return false;
        arr_convert(e, wa->elem);
        if (want != wa) e->type = want;
        return true;
    }
    Type *w = want->kind == TY_NULLABLE ? want->elem : want, *g = e->type && e->type->kind == TY_NULLABLE ? e->type->elem : e->type;
    if (!g || w->kind != TY_OBJECT || !w->cls->iface_base || g->kind != TY_OBJECT || g->cls == w->cls || g->cls->builtin ||
        g->cls->adapter_of || is_subclass(g->cls, w->cls))
        return false;
    if (e->type->kind == TY_NULLABLE && want->kind != TY_NULLABLE) return false;
    char *why = NULL;
    Class *a = iface_adapter(g->cls, w->cls, e->pos, &why);
    if (!a) xs_error(XS8149, e->pos, why, "%s must be %s, but %s does not fit it", what, w->cls->name, g->cls->name);
    Pos pos = e->pos;
    Expr *inner = xs_alloc(sizeof *inner);
    *inner = *e;
    Expr *nw = xs_alloc(sizeof *nw);
    nw->kind = E_NEW_OBJ; nw->pos = pos; nw->field = -1; nw->str = a->name; nw->cls = a;
    if (e->type->kind != TY_NULLABLE) {
        inner->prechecked = true;
        expr_list_push(&nw->args, inner);
        check_expr(nw, NULL);
        *e = *nw;
        upcast(e, want);
        return true;
    }
    /* ((__ad: C | null): I | null => __ad === null ? null : new <adapter>(__ad))(value) */
    expr_list_push(&nw->args, ad_ident(pos, "__ad"));
    Expr *z = xs_alloc(sizeof *z);
    z->kind = E_NULL; z->pos = pos; z->field = -1;
    Expr *t = xs_alloc(sizeof *t);
    t->kind = E_BINARY; t->op = TK_EQ; t->pos = pos; t->field = -1; t->lhs = ad_ident(pos, "__ad"); t->rhs = z;
    Expr *z2 = xs_alloc(sizeof *z2);
    z2->kind = E_NULL; z2->pos = pos; z2->field = -1;
    Expr *c = xs_alloc(sizeof *c);
    c->kind = E_COND; c->pos = pos; c->field = -1; c->lhs = t;
    expr_list_push(&c->args, z2);
    expr_list_push(&c->args, nw);
    Expr *arrow = thunk_of(c);
    Func *f = arrow->arrow->fn;
    f->params = xs_alloc(sizeof(Param));
    memset(f->params, 0, sizeof(Param));
    f->params[0].name = "__ad";
    f->params[0].type = e->type;
    f->nparams = 1;
    f->ret = want;
    memset(e, 0, sizeof *e);
    e->kind = E_CALL; e->pos = pos; e->field = -1; e->is_call = true;
    e->lhs = arrow;
    expr_list_push(&e->args, inner);
    check_expr(arrow, NULL);
    e->type = want;
    return true;
}
/* `x as I` from unknown (an injector's construct(args)): the decorated classes that fit I get their adapters now, so
   that the cast can wrap an object of one of them (the emitter's table: class -> adapter); true when there is any */
static bool iface_runtime(Class *ifc, Pos pos) {
    for (int i = 0; i < prog->nclasses; i++) {
        Class *c = prog->classes[i];
        if (!c->decos.len || c->builtin || c->is_interface || c->iface_base || c->gen_state || c->adapter_of || is_subclass(c, ifc)) continue;
        bool named = true; /* (cheap first: every method name is there) */
        for (int k = 0; k < ifc->nmethods && named; k++)
            if (ifc->methods[k]->is_abstract && !find_method(c, ifc->methods[k]->name)) named = false;
        if (named) iface_adapter(c, ifc, pos, NULL);
    }
    return ifc->nadapters > 0;
}
/* class C implements I with a method whose signature is not I's but fits it (wider parameters, a narrower result,
   one more optional parameter): TypeScript accepts it. C then stays out of I's hierarchy, and an adapter makes it
   an I where one goes (an exact signature keeps the plain subclass: the direct path) */
static bool override_same(Func *own, Func *bm) {
    if (own->ntparams) return true; /* (left to the override rule) */
    if (own->nparams > bm->nparams || (own->nparams < bm->nparams && !own->body)) return false;
    for (int k = 0; k < own->nparams; k++)
        if (!ty_equal(own->params[k].type, bm->params[k].type) && inline_same(own->params[k].type, bm->params[k].type) != bm->params[k].type) return false;
    if (own->ret && bm->ret && !ty_equal(own->ret, bm->ret) && inline_same(own->ret, bm->ret) != bm->ret) return false;
    return true;
}
static bool implements_by_shape(Class *c) {
    Class *b = c->base;
    if (!b || !b->iface_base || c->adapter_of) return false;
    bool differs = false;
    for (int i = 0; i < b->nmethods; i++) {
        Func *bm = b->methods[i], *own = NULL;
        if (!bm->is_abstract) continue;
        for (int k = 0; k < c->nmethods; k++) if (c->methods[k]->cls == c && strcmp(c->methods[k]->name, bm->name) == 0) own = c->methods[k];
        if (!own) return false; /* (not implemented: the abstract-method error says so) */
        if (override_same(own, bm)) continue;
        if (!method_fits(own, bm, c, b, false, c->pos, 0, NULL)) return false; /* (the override error says which) */
        differs = true;
    }
    if (!differs) return false;
    int k = 0;
    for (int i = 0; i < b->nsubs; i++) if (b->subs[i] != c) b->subs[k++] = b->subs[i];
    b->nsubs = k;
    c->base = NULL;
    c->base_name = NULL;
    c->nbase_fields = 0;
    return true;
}

/* `this.f = v` on every path through s (if/else both, a block, a try's body) */
static bool st_sets_field(Stmt *s, const char *f) {
    if (!s) return false;
    switch (s->kind) {
    case S_EXPR: {
        Expr *x = s->expr;
        if (x && x->kind == E_SEQ) { /* Object.assign(this, partial): its assignments */
            for (int i = 0; i < x->args.len; i++) {
                Expr *a = x->args.items[i];
                if (a->kind == E_ASSIGN && a->op == TK_ASSIGN && a->lhs->kind == E_MEMBER && !a->lhs->is_call && a->lhs->lhs->kind == E_THIS &&
                    strcmp(a->lhs->str, f) == 0) return true;
            }
            return false;
        }
        if (!x || x->kind != E_ASSIGN || x->op != TK_ASSIGN) return false;
        Expr *t = x->lhs;
        return t->kind == E_MEMBER && !t->is_call && t->lhs->kind == E_THIS && strcmp(t->str, f) == 0;
    }
    case S_BLOCK: for (int i = 0; i < s->stmts.len; i++) if (st_sets_field(s->stmts.items[i], f)) return true; return false;
    case S_IF: return s->else_body && st_sets_field(s->body, f) && st_sets_field(s->else_body, f);
    case S_TRY: return st_sets_field(s->body, f) && (!s->else_body || st_sets_field(s->else_body, f) || always_exits(s->else_body));
    default: return false;
    }
}
static void check_field_init(Class *c) {
    if (c->is_interface) return; /* object literals fill interfaces */
    for (int i = 0; i < c->nfields; i++) {
        Field *fd = &c->fields[i];
        if (i < c->nbase_fields && fd->owner != c) continue;
        if (fd->init || fd->optional) continue;
        bool assigned = false;
        if (c->ctor) {
            assigned = st_sets_field(c->ctor->body, fd->name);
        }
        if (!assigned && !eff_ctor(c)) { c->data_only = true; continue; } /* a DTO: JSON fills it; `new` is refused */
        if (!assigned && fd->definite && fd->type && ty_is_heap(fd->type)) { /* private done!: () => void, set by a callback: null until then */
            Expr *n = xs_alloc(sizeof *n);
            n->kind = E_NULL; n->pos = fd->pos; n->field = -1; n->unset = true; n->type = fd->type;
            fd->init = n;
            continue;
        }
        if (!assigned)
            xs_error(XS9025, fd->pos, "give it an initializer, or assign this.%s in the constructor", "field '%s' of %s is never initialized", fd->name, c->name);
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
        failed_ = true; scope = sc_; cur_func = cf_; cur_class = cc_; loop_depth = ld_; switch_depth = sd_; finally_depth = fd_; mapnull_ok = 0; \
        if (cm_) enter_module(cm_); \
    } \
    xs_error_recover = NULL; }

static bool failed_;

/* the return type of f, inferred now when it was left out and f's body is not checked yet: f is checked here, in
   its own context, and whatever was being checked carries on as it was */
static void check_func(Func *f);
#define II_SAVE(arr, n, T) T *arr##_s = n ? xs_alloc(sizeof(T) * (size_t)n) : NULL; if (n) memcpy(arr##_s, arr, sizeof(T) * (size_t)n); int n##_s = n;
#define II_BACK(arr, n, T) if (n##_s) { arr = realloc(arr, sizeof(T) * (size_t)n##_s); memcpy(arr, arr##_s, sizeof(T) * (size_t)n##_s); } n = n##_s;
static Type *fret(Func *f, Pos pos) {
    if (f->ret) { if (f->ret_failed) xs_error_quiet(); return f->ret; }
    if (f->ret_infer == 2)
        xs_error(XS3001, pos, "write its return type: f(n: number): number { ... }", "'%s' is used before its return type is known (it calls itself): write the return type", f->name);
    if (f->ret_infer != 1 || !f->body) xs_error(XS3001, pos, "write the return type", "'%s' has no return type", f->name);
    Scope *sc = scope; Func *cf = cur_func; Class *cc = cur_class; Module *cm = cur_mod;
    const char *fname = xs_file_name, *fsrc = xs_source;
    int ld = loop_depth, sd = switch_depth, fd = finally_depth, tc = tmp_counter, scl = super_calls;
    bool fid = for_init_decl, dnc = decl_no_cell, cg = checking_generated, fl = failed_;
    Stmt *iib = ii_body; int iid = ii_depth;
    II_SAVE(sev, nsev, ScanEv)
    II_SAVE(ii_tnames, ii_tn, const char *)
    II_SAVE(ii_anames, ii_an, const char *)
    II_SAVE(ii_lnames, ii_ln, const char *)
    II_SAVE(ii_cnames, ii_cn, const char *)
    Expr **cinit_s = ii_cn_s ? xs_alloc(sizeof(Expr *) * (size_t)ii_cn_s) : NULL; if (ii_cn_s) memcpy(cinit_s, ii_cinit, sizeof(Expr *) * (size_t)ii_cn_s);
    Expr **linit_s = ii_ln_s ? xs_alloc(sizeof(Expr *) * (size_t)ii_ln_s) : NULL; if (ii_ln_s) memcpy(linit_s, ii_linit, sizeof(Expr *) * (size_t)ii_ln_s);
    jmp_buf *outer = xs_error_recover;
    jmp_buf rb;
    bool ok = true;
    checking_generated = false;
    loop_depth = switch_depth = finally_depth = 0;
    xs_error_recover = outer ? &rb : NULL;
    if (!outer || _setjmp(rb) == 0) check_func(f);
    else ok = false;
    xs_error_recover = outer;
    scope = sc; cur_func = cf; cur_class = cc; cur_mod = cm; xs_file_name = fname; xs_source = fsrc;
    loop_depth = ld; switch_depth = sd; finally_depth = fd; tmp_counter = tc; super_calls = scl;
    for_init_decl = fid; decl_no_cell = dnc; checking_generated = cg; failed_ = fl;
    ii_body = iib; ii_depth = iid;
    II_BACK(sev, nsev, ScanEv)
    if (nsev_s) cap_sev = nsev_s; /* (the array was cut to that size: its capacity too, or the next event writes past it) */
    II_BACK(ii_tnames, ii_tn, const char *)
    II_BACK(ii_anames, ii_an, const char *)
    II_BACK(ii_lnames, ii_ln, const char *)
    II_BACK(ii_cnames, ii_cn, const char *)
    if (ii_cn_s) { ii_cinit = realloc(ii_cinit, sizeof(Expr *) * (size_t)ii_cn_s); memcpy(ii_cinit, cinit_s, sizeof(Expr *) * (size_t)ii_cn_s); }
    if (ii_ln_s) { ii_linit = realloc(ii_linit, sizeof(Expr *) * (size_t)ii_ln_s); memcpy(ii_linit, linit_s, sizeof(Expr *) * (size_t)ii_ln_s); }
    if (!ok || !f->ret || f->ret_failed) { /* its body had an error (reported there): what uses it stops quietly */
        f->ret_infer = 3;
        if (!f->ret) f->ret = ty_unknown;
        xs_error_quiet();
    }
    return f->ret;
}

/* a class's field initializers, constructor and methods */
static void check_class_bodies(Class *c) {
    enter_module(c->mod);
    for (int k = 0; k < c->nfields; k++) { /* the inherited ones were checked in their class */
        Field *fd = &c->fields[k];
        if (!fd->init) continue;
        if (k < c->nbase_fields && fd->owner != c) continue; /* (an interface's field this class gives a value) */
        cur_class = c;
        RECOVER_BEGIN
            if (!fd->init->type) check_expr(fd->init, fd->type); /* (an inferred field's value gave its type: checked) */
            expect_type(fd->init, fd->type, xs_sprintf("initializer of field '%s'", fd->name));
        RECOVER_END
    }
    cur_class = NULL;
    if (c->ctor) {
        RECOVER_BEGIN
            super_calls = 0;
            bool needs_super = c->base && !c->base->iface_methods && !c->fake; /* (an implemented interface has no constructor) */
            if (needs_super) {
                StmtList *l = &c->ctor->body->stmts;
                Stmt *s0 = l->len ? l->items[0] : NULL;
                /* statements before super(...) that do not use `this` (TypeScript checks that; tsc-checked code only) */
                if (tsc_checked())
                    for (int k = 0; k < l->len; k++) {
                        Stmt *sk = l->items[k];
                        if (sk->kind == S_EXPR && sk->expr->kind == E_CALL && sk->expr->lhs && sk->expr->lhs->kind == E_IDENT &&
                            strcmp(sk->expr->lhs->str, "super") == 0) { s0 = sk; break; }
                    }
                if (!s0 || s0->kind != S_EXPR || s0->expr->kind != E_CALL || !s0->expr->lhs || s0->expr->lhs->kind != E_IDENT || strcmp(s0->expr->lhs->str, "super") != 0)
                    xs_error(XS8131, s0 ? s0->pos : c->ctor->pos, xs_sprintf("start the constructor with super(...): %s extends %s", c->name, c->base->name),
                             "the constructor of %s must call super(...) first", c->name);
            }
            check_func(c->ctor);
            if (needs_super && super_calls != 1) xs_error(XS8126, c->ctor->pos, NULL, "super(...) is called once, as the first statement of the constructor");
        RECOVER_END
    }
    RECOVER_BEGIN check_field_init(c); RECOVER_END
    for (int k = 0; k < c->nmethods; k++) {
        if (c->methods[k]->cls != c || c->methods[k]->is_abstract || c->methods[k]->ntparams) continue; /* inherited, or a template */
        RECOVER_BEGIN check_func(c->methods[k]); RECOVER_END
    }
}

/* items = ['a', 'b'], seen = new Set([1]), limits = { min: 1 }: a field without a written type has its value's */
static struct { Class *c; const char *name; } *late_fields; /* fields typed by a value that calls code (infer_field_type) */
static int nlate_fields;
static bool field_infer_late;
static void infer_field_type(Class *c, Field *fd) {
    if (!fd->init) xs_error(XS3011, fd->pos, "fields need an explicit type: base: string;", "field '%s' has no type", fd->name);
    if (fd->init->kind == E_IDENT && !fd->init->type) {
        /* rotina = ROTINA_X: a const of this module (the module's code is checked later): its written type, or its literal's */
        for (int pass = 0; pass < 2; pass++) /* (this module's, then an exported one: imported by name) */
        for (int i = 0; i < prog->top.len; i++) {
            Stmt *st = prog->top.items[i];
            if (st->kind != S_VAR || !st->name || strcmp(st->name, fd->init->str) != 0 || !st->is_const) continue;
            if (pass == 0 ? st->mod != c->mod : (st->mod == c->mod || !st->exported)) continue;
            Type *t = st->decl_type ? resolve(st->decl_type, st->pos)
                    : st->init->kind == E_STRING ? ty_string : st->init->kind == E_BOOL ? ty_boolean : st->init->kind == E_NUMBER ? ty_number : NULL;
            if (!t) t = literal_type(st->init); /* (a const object or array: the shape of its literal) */
            if (t) { fd->type = t; return; }
        }
        xs_error(XS3011, fd->pos, xs_sprintf("write the type: %s: T = %s;", fd->name, fd->init->str), "field '%s' has no type", fd->name);
    }
    if (fd->init->kind == E_ARROW && !fd->init->type && fd->init->arrow && fd->init->arrow->fn->ret) {
        /* q = (sql: string, binds: unknown[]): Promise<R> => ...: the function type its signature writes (the body is
           checked with the class, where `this` is known) */
        Func *af = fd->init->arrow->fn;
        bool typed = true;
        for (int i = 0; i < af->nparams; i++) if (!af->params[i].type) typed = false;
        if (typed) {
            Type **ps = xs_alloc(sizeof(Type *) * (size_t)(af->nparams ? af->nparams : 1));
            for (int i = 0; i < af->nparams; i++) ps[i] = af->params[i].type;
            Type *ft = ty_func(ps, af->nparams, af->is_async && af->ret->kind != TY_PROMISE ? ty_promise_of(af->ret) : af->ret);
            ft->rest = af->nparams && af->params[af->nparams - 1].rest;
            ft->gen_sig = fd->init->arrow->gen_sig; /* (q1 = async <T>(sql): Promise<T> => ...: this.q1<Row>(sql) reads it) */
            fd->type = ft;
            return;
        }
    }
    if (fd->init->kind == E_CALL && !fd->init->type && fd->init->lhs && fd->init->lhs->kind == E_IDENT && !lookup_func(fd->init->lhs->str)) {
        /* flag = flagLigada(env) with const flagLigada = (v: string): boolean => ...: the return type it writes (the
           module's consts are checked after the classes' shapes; the call itself is checked with the class) */
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < prog->top.len; i++) {
                Stmt *st = prog->top.items[i];
                if (st->kind != S_VAR || !st->name || !st->is_const || strcmp(st->name, fd->init->lhs->str) != 0) continue;
                if (pass == 0 ? st->mod != c->mod : (st->mod == c->mod || !st->exported)) continue;
                if (!st->init || st->init->kind != E_ARROW || !st->init->arrow->fn->ret || st->init->arrow->fn->is_async) continue;
                Module *was = cur_mod;
                enter_module(st->mod);
                Type *r = resolve(st->init->arrow->fn->ret, st->pos);
                enter_module(was);
                fd->type = r;
                return;
            }
    }
    if (!fd->init->type && !field_infer_late && (fd->init->kind == E_CALL || fd->init->kind == E_MEMBER || fd->init->kind == E_ARROW ||
                                                 fd->init->kind == E_AWAIT || fd->init->kind == E_COND || fd->init->kind == E_BINARY)) {
        /* travaHabilitada = flagLigada(...), SQL = sql('hap'): checked once every class's methods are known (late_fields) */
        late_fields = realloc(late_fields, sizeof(*late_fields) * (size_t)(nlate_fields + 1));
        late_fields[nlate_fields].c = c;
        late_fields[nlate_fields++].name = fd->name;
        return;
    }
    if (!fd->init->type) {
        Class *cc = cur_class;
        cur_class = c;
        check_expr(fd->init, NULL);
        settle(fd->init);
        cur_class = cc;
    }
    Type *t = fd->init->type;
    if (t->kind == TY_NULL || t->kind == TY_VOID)
        xs_error(XS3011, fd->pos, "write the type: x: Todo | null = null;", "cannot infer the type of field '%s' from its value", fd->name);
    fd->type = t;
}

/* xisto check: a signature that names what could not be loaded (reported there): its types read as unknown, its
   result too (uses stop quietly), and its body is not checked */
static void sig_failed(Func *f) {
    for (int k = 0; k < f->nparams; k++) if (f->params[k].type && f->params[k].type->kind == TY_OBJECT && !f->params[k].type->cls->cname) f->params[k].type = ty_unknown;
    f->ret_failed = true;
    if (!f->ret || (f->ret->kind == TY_OBJECT && !f->ret->cls->cname)) f->ret = ty_unknown;
    f->ret_infer = 3;
    f->sig_failed = true;
}
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
            if (p->enums[j]->mod == en->mod && strcmp(en->name, p->enums[j]->name) == 0) xs_error(XS9018, en->pos, NULL, "enum '%s' is declared twice", en->name);
        for (int j = 0; j < p->nclasses; j++)
            if (p->classes[j]->mod == en->mod && strcmp(p->classes[j]->name, en->name) == 0) xs_error(XS9008, en->pos, NULL, "'%s' is both an enum and a class", en->name);
    }
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        enter_module(c->mod);
        for (int j = 0; j < i; j++)
            if (p->classes[j]->mod == c->mod && strcmp(c->name, p->classes[j]->name) == 0) xs_error(XS9015, c->pos, NULL, "class '%s' is declared twice", c->name);
        for (int j = 0; j < nbuiltins; j++)
            if (strcmp(c->name, builtins[j]->name) == 0 && !((strcmp(c->name, "Worker") == 0 || strcmp(c->name, "MessagePort") == 0) && !wt_imports(c->mod, c->name)) &&
                strcmp(c->name, "Response") != 0) /* (Express's Response: a module's own class comes first where it is declared or imported) */
                xs_error(XS9002, c->pos, NULL, "'%s' is a built-in class", c->name);
        SYM_EACH(funcs_by_name, c->name, e)
            if (((Func *)e->decl)->mod == c->mod) xs_error(XS9006, c->pos, NULL, "'%s' is both a class and a function", c->name);
        if (!c->builtin) c->cname = xs_sprintf("c%d_%s", c->mod->id, c->name); /* declared (native) classes keep their C name */
    }
    for (int i = 0; i < p->nfuncs; i++) {
        Func *f = p->funcs[i];
        if (f->mod->builtin) continue;
        enter_module(f->mod);
        SYM_EACH(funcs_by_name, f->name, e)
            if (e->decl != f && ((Func *)e->decl)->mod == f->mod) xs_error(XS9026, f->pos, NULL, "function '%s' is declared twice", f->name);
        if (find_class(f->name) || find_enum(f->name)) xs_error(XS9009, f->pos, NULL, "'%s' is built in and cannot be redeclared", f->name); /* a function may shadow lines, fetch... */
        if (!f->is_native) f->cname = xs_sprintf("f%d_%s", f->mod->id, f->name);
    }
    /* imports must name exported functions, classes or variables of the source module */
    for (int mi = 0; mi < p->nmodules; mi++) {
        Module *m = p->modules[mi];
        for (int i = 0; i < m->nimports; i++) {
            Import *im = &m->imports[i];
            if (im->from->missing) { /* reported where it was imported */
                for (int k = 0; k < im->n; k++) {
                    missing_names = realloc(missing_names, sizeof(char *) * (size_t)(nmissing_names + 1));
                    missing_names[nmissing_names++] = im->names[k];
                }
                continue;
            }
            for (int k = 0; k < im->n; k++) {
                const char *name = im->names[k];
                bool found = false;
                SYM_EACH(funcs_by_name, name, e) if (((Func *)e->decl)->mod == im->from) found = found || ((Func *)e->decl)->exported;
                SYM_EACH(classes_by_name, name, e) if (((Class *)e->decl)->mod == im->from) found = found || ((Class *)e->decl)->exported;
                SYM_EACH(enums_by_name, name, e) if (((Enum *)e->decl)->mod == im->from) found = found || ((Enum *)e->decl)->exported;
                if (!found && strcmp(im->from->file, "xisto:worker_threads") == 0 && (strcmp(name, "Worker") == 0 || strcmp(name, "MessagePort") == 0))
                    found = true; /* built-in classes */
                if (!found && im->from->builtin && strcmp(im->from->file, "perf_hooks") == 0 && strcmp(name, "performance") == 0)
                    found = true; /* the built-in performance */
                if (!found && im->from->builtin && strcmp(im->from->file, "sqlite") == 0)
                    for (int j = 0; j < nbuiltins; j++)
                        if (strcmp(builtins[j]->name, name) == 0 && (strcmp(name, "DatabaseSync") == 0 || strcmp(name, "StatementSync") == 0)) found = true;
                if (found && im->is_default) { /* import x from "m": x must be m's default export */
                    bool ok = im->from->default_name && strcmp(im->from->default_name, name) == 0;
                    SYM_EACH(funcs_by_name, name, e) if (((Func *)e->decl)->mod == im->from && ((Func *)e->decl)->is_default) ok = true;
                    SYM_EACH(classes_by_name, name, e) if (((Class *)e->decl)->mod == im->from && ((Class *)e->decl)->is_default) ok = true;
                    if (!ok) { enter_module(m); xs_error(XS9013, im->positions[k], "import it by name: import { x } from \"m\"", "'%s' is not the default export of '%s'", name, im->path); }
                }
                for (int j = 0; j < p->top.len && !found; j++) {
                    Stmt *st = p->top.items[j];
                    if (st->kind == S_VAR && st->mod == im->from && strcmp(st->name, name) == 0) found = st->exported;
                }
                if (!found) { /* re-exported by im->from (export { name } from, export * from) */
                    int r = reexports_name(p, im->from, name, 0);
                    if (r == 2) { /* from a module that could not be loaded: reported there */
                        missing_names = realloc(missing_names, sizeof(char *) * (size_t)(nmissing_names + 1));
                        missing_names[nmissing_names++] = (char *)name;
                        continue;
                    }
                    found = r == 1;
                }
                if (!found) {
                    enter_module(m);
                    if (xs_check_mode) { /* xisto check: reported, and checking goes on (its uses are quiet, as a missing module's) */
                        xs_error_continue(XS9012, im->positions[k], xs_sprintf("add 'export' in front of its declaration in %s", im->from->file), "'%s' is not exported by '%s'", name, im->path);
                        missing_names = realloc(missing_names, sizeof(char *) * (size_t)(nmissing_names + 1));
                        missing_names[nmissing_names++] = (char *)name;
                        continue;
                    }
                    xs_error(XS9012, im->positions[k], xs_sprintf("add 'export' in front of its declaration in %s", im->from->file), "'%s' is not exported by '%s'", name, im->path);
                }
            }
        }
    }
    for (int i = 0; i < p->nfuncs; i++) {
        if (p->funcs[i]->mod->builtin) continue;
        enter_module(p->funcs[i]->mod);
        if (p->funcs[i]->ntparams) continue; /* a template resolves per instance */
        if (!xs_check_mode) { resolve_signature(p->funcs[i]); continue; }
        RECOVER_BEGIN resolve_signature(p->funcs[i]); RECOVER_END /* xisto check: a type of a module that is not there (reported) costs this function */
        if (failed_) sig_failed(p->funcs[i]);
    }
    /* interface Contract { execute(x: In): Out; }: an abstract class, the base of the class that implements it (when
       that class extends nothing else), so a Contract variable calls the implementation's method */
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (!c->iface_methods || !c->is_interface) continue;
        enter_module(c->mod);
        if (c->niface_bases == 1 && !c->ntparams) { /* interface B extends A { m(): R } with A an interface of methods too: B's
                                                       abstract class extends A's */
            Class *b = find_class(c->iface_bases[0]);
            if (b && b->iface_methods && !b->ntparams) { c->base_name = c->iface_bases[0]; c->base_pos = c->pos; c->niface_bases = 0; }
        }
        if (c->ntparams || c->niface_bases) { /* (fields: the base's, which the implementation declares again) */
            if (xs_check_mode) { /* xisto check: reported, and the rest of the program is still checked */
                xs_error_continue(XS8113, c->pos, "keep the methods in an interface of their own (or use an abstract class)",
                                  "interface '%s' has methods and %s", c->name, c->ntparams ? "type parameters" : c->nfields ? "fields" : "bases");
                continue;
            }
            xs_error(XS8113, c->pos, "keep the methods in an interface of their own (or use an abstract class)",
                     "interface '%s' has methods and %s", c->name, c->ntparams ? "type parameters" : c->nfields ? "fields" : "bases");
        }
        c->is_interface = false;
        c->is_abstract = true;
        c->iface_base = true;
    }
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        if (c->base_name || c->mixin || c->is_interface) continue;
        enter_module(c->mod);
        for (int k = 0; k < c->nimplements; k++) {
            Class *b = find_class(c->implements[k]);
            if (b && b->iface_methods && !b->is_interface) { c->base_name = c->implements[k]; c->base_pos = c->pos; break; }
        }
    }
    /* inheritance: bases first, so a class sees its base's fields (and later its methods) */
    for (int i = 0; i < p->nclasses; i++) {
        Class *c = p->classes[i];
        enter_module(c->mod);
        if (c->mixin) apply_mixin(c);
        if (c->base_name) {
            Class *b = find_class(c->base_name);
            if (!b) {
                int n; const char **cands = class_names(&n);
                if (missing_name(c->base_name)) xs_error_quiet();
                xs_error(XS9031, c->base_pos, xs_help_suggest(c->base_name, cands, n, NULL), "unknown class '%s'", c->base_name);
            }
            if (c->nbase_targs && !c->ntparams) { /* class X extends Base<A>: the instance Base<A> is the base */
                if (b->ntparams != c->nbase_targs)
                    xs_error(XS8063, c->base_pos, NULL, "'%s' takes %d type argument(s), got %d", b->name, b->ntparams, c->nbase_targs);
                Type **args = xs_alloc(sizeof(Type *) * (size_t)c->nbase_targs);
                for (int k = 0; k < c->nbase_targs; k++) args[k] = resolve(c->base_targs[k], c->base_pos);
                if (xs_check_mode) { /* xisto check: an instance naming what could not be loaded costs this base only */
                    Class *tb = b;
                    b = NULL;
                    RECOVER_BEGIN b = instantiate_class(tb, args, c->base_pos); RECOVER_END
                    enter_module(c->mod);
                    if (!b) { c->base_name = NULL; continue; }
                } else b = instantiate_class(b, args, c->base_pos);
                enter_module(c->mod);
            } else if (c->nbase_targs) xs_error(XS6022, c->base_pos, NULL, "a generic class extending a generic class is not supported yet");
            if (b->is_interface) xs_error(XS9004, c->base_pos, xs_sprintf("class %s implements %s", c->name, b->name), "'%s' is an interface; a class extends classes", b->name);
            if (b->builtin && strcmp(b->name, "Error") == 0) { error_class(c); continue; }
            if (b->builtin) xs_error(XS6024, c->base_pos, "wrap it in a field instead", "extending the built-in class %s is not supported", b->name);
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
                if (strcmp(fd->name, c->fields[j].name) == 0) xs_error(XS9023, fd->pos, NULL, "field '%s' is declared twice", fd->name);
            if (!fd->type && xs_check_mode) { /* xisto check: a field whose value cannot type it reads as unknown */
                bool ok = false;
                RECOVER_BEGIN infer_field_type(c, fd); ok = true; RECOVER_END
                enter_module(c->mod);
                cur_class = NULL;
                if (!ok) fd->type = ty_unknown;
            } else if (!fd->type) infer_field_type(c, fd);
            if (!fd->type) { fd->cname = field_cname(fd->name); continue; } /* (typed later, late_fields) */
            Module *fmod = fd->owner && fd->owner->mod ? fd->owner->mod : c->mod; /* an inherited field: its type reads where it is written */
            if (fmod != cur_mod) enter_module(fmod);
            if (xs_check_mode) { /* xisto check: a field naming what could not be loaded reads as unknown */
                Type *ft = fd->type;
                bool ok = false;
                RECOVER_BEGIN ft = resolve(ft, fd->pos); ok = true; RECOVER_END
                cur_class = NULL;
                fd->type = ok ? ft : ty_unknown;
            } else fd->type = resolve(fd->type, fd->pos);
            if (fmod != c->mod || cur_mod != c->mod) enter_module(c->mod);
            check_type_supported(fd->pos, fd->type);
            fd->cname = field_cname(fd->name);
        }
        for (int k = 0; k < c->nmethods; k++) {
            Func *m = c->methods[k];
            for (int j = 0; j < k; j++)
                if (strcmp(m->name, c->methods[j]->name) == 0) xs_error(XS9027, m->pos, NULL, "method '%s' is declared twice", m->name);
            if (find_field(c, m->name, NULL)) xs_error(XS9007, m->pos, NULL, "'%s' is both a field and a method of %s", m->name, c->name);
            if (!m->is_native) m->cname = xs_sprintf("m%d_%s_%s", c->mod->id, c->name, m->name);
            if (!m->ntparams && xs_check_mode) { RECOVER_BEGIN resolve_signature(m); RECOVER_END enter_module(c->mod); cur_class = NULL; if (failed_) sig_failed(m); }
            else if (!m->ntparams) resolve_signature(m);
        }
        if (c->ctor) {
            if (!c->ctor->is_native) c->ctor->cname = xs_sprintf("c%d_%s_new", c->mod->id, c->name);
            if (xs_check_mode) {
                RECOVER_BEGIN resolve_signature(c->ctor); RECOVER_END
                enter_module(c->mod);
                cur_class = NULL;
                if (failed_) sig_failed(c->ctor);
            } else resolve_signature(c->ctor);
        }
    }
    for (int i = 0; i < p->nclasses; i++) {
        enter_module(p->classes[i]->mod);
        if (xs_check_mode) { RECOVER_BEGIN flatten_methods(p->classes[i]); RECOVER_END cur_class = NULL; } /* (xisto check: reported, on to the next) */
        else flatten_methods(p->classes[i]);
    }
    /* fields typed by a value that calls a function or a method: now that every signature is known */
    field_infer_late = true;
    for (int i = 0; i < nlate_fields; i++) {
        Class *c = late_fields[i].c;
        Field *fd = find_field(c, late_fields[i].name, NULL);
        if (!fd || fd->type) continue;
        enter_module(c->mod);
        RECOVER_BEGIN
            infer_field_type(c, fd);
            fd->type = resolve(fd->type, fd->pos);
            check_type_supported(fd->pos, fd->type);
        RECOVER_END
        if (!fd->type) fd->type = ty_unknown;
        for (int j = 0; j < p->nclasses; j++) /* the copies its subclasses hold */
            for (Class *b = p->classes[j]->base; b; b = b->base)
                if (b == c) {
                    Field *sf = find_field(p->classes[j], fd->name, NULL);
                    if (sf && !sf->type) sf->type = fd->type;
                    break;
                }
    }
    cur_class = NULL;

    /* Top-level statements, module by module, dependencies first. Imported variables become
       read-only aliases in the importing module's scope. */
    for (int mi = 0; mi < p->nmodules; mi++) {
        Module *m = p->modules[mi];
        enter_module(m);
        for (int i = 0; i < m->nimports; i++) {
            Import *im = &m->imports[i];
            for (int k = 0; k < im->n; k++) {
                Scope *src = mod_scopes[im->from->id];
                { /* a name the module only re-exports with export * from "m": the scope of the module that has it */
                    Module *via = im->from;
                    for (int depth = 0; depth < 32 && via; depth++) {
                        bool has = false;
                        size_t inl = strlen(im->names[k]);
                        for (Binding *b = mod_scopes[via->id]->head; b && !has; b = b->next) /* the name, or a static of the class it names (Class__s_x) */
                            has = strcmp(b->name, im->names[k]) == 0 || (strncmp(b->name, im->names[k], inl) == 0 && strncmp(b->name + inl, "__s_", 4) == 0);
                        if (has) { src = mod_scopes[via->id]; break; }
                        Module *next_via = NULL;
                        for (int q = 0; q < via->nimports && !next_via; q++)
                            if (via->imports[q].star && via->imports[q].from && !via->imports[q].from->missing && reexports_or_has(via->imports[q].from, im->names[k]))
                                next_via = via->imports[q].from;
                        via = next_via;
                    }
                }
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
        if (c->gen_state || c->generic_of || c->adapter_of) continue; /* a generic class's template; instances (and adapters) are checked below */
        check_class_bodies(c);
    }
    for (int i = 0; i < p->nfuncs; i++)
        if (!p->funcs[i]->mod->builtin && !p->funcs[i]->ntparams && !p->funcs[i]->sig_failed) { RECOVER_BEGIN check_func(p->funcs[i]); RECOVER_END }
    /* generic instances, classes and functions (an instance can make more); then they join the program */
    for (int w = 0, cw = 0; w < ngen_work || cw < nclass_work;) {
        if (cw < nclass_work) { check_class_bodies(class_work[cw++]); continue; }
        RECOVER_BEGIN check_func(gen_work[w]); RECOVER_END
        w++;
    }
    {   /* the templates of generic classes leave the program: only their instances are emitted */
        int k = 0;
        for (int i = 0; i < p->nclasses; i++) if (!p->classes[i]->gen_state) p->classes[k++] = p->classes[i];
        p->nclasses = k;
    }
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
    xs_warn_rule_summary(); /* (one line: the rule warnings, XB-118) */
    if (xs_error_count) {
        if (!xs_json_errors) fprintf(stderr, "xisto: %d error%s\n", xs_error_count, xs_error_count == 1 ? "" : "s");
        exit(1);
    }
}

/* 1: module `via` re-exports `name` (export { name } from "m", export * from "m"), declared and exported where it comes
   from; 2: through a module that could not be loaded (xisto check); 0: not */
static int reexports_name(Program *p, Module *via, const char *name, int depth) {
    if (!via || depth > 32) return 0;
    for (int i = 0; i < via->nimports; i++) {
        Import *im = &via->imports[i];
        if (!im->reexport || !im->from) continue;
        bool listed = im->star;
        for (int k = 0; k < im->n && !listed; k++) listed = strcmp(im->names[k], name) == 0;
        if (!listed) continue;
        if (im->from->missing) return 2;
        Module *m = im->from;
        bool here = false;
        SYM_EACH(funcs_by_name, name, e) if (((Func *)e->decl)->mod == m && ((Func *)e->decl)->exported) here = true;
        SYM_EACH(classes_by_name, name, e) if (((Class *)e->decl)->mod == m && ((Class *)e->decl)->exported) here = true;
        SYM_EACH(enums_by_name, name, e) if (((Enum *)e->decl)->mod == m && ((Enum *)e->decl)->exported) here = true;
        for (int j = 0; j < p->top.len && !here; j++) {
            Stmt *st = p->top.items[j];
            if (st->kind == S_VAR && st->mod == m && st->name && strcmp(st->name, name) == 0 && st->exported) here = true;
        }
        if (here) return 1;
        int r = reexports_name(p, m, name, depth + 1);
        if (r) return r;
    }
    return 0;
}

/* module m's scope has `name`, or a module it re-exports with export * from does (at any depth) */
static bool reexports_or_has(Module *m, const char *name) {
    for (int depth = 0; depth < 32 && m; depth++) {
        size_t nl = strlen(name);
        for (Binding *b = mod_scopes[m->id]->head; b; b = b->next)
            if (strcmp(b->name, name) == 0 || (strncmp(b->name, name, nl) == 0 && strncmp(b->name + nl, "__s_", 4) == 0)) return true; /* or a static of the class it names */
        Module *next = NULL;
        for (int q = 0; q < m->nimports && !next; q++)
            if (m->imports[q].star && m->imports[q].from && !m->imports[q].from->missing && reexports_or_has(m->imports[q].from, name)) next = m->imports[q].from;
        m = next;
    }
    return false;
}
