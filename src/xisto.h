/* Xisto compiler: shared definitions (xisto-2). */
#ifndef XISTO_H
#define XISTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- util ---------- */

void *xs_alloc(size_t n);
char *xs_strndup(const char *s, size_t n);
char *xs_sprintf(const char *fmt, ...);

typedef struct {
    char *data;
    size_t len, cap;
} Buf;

void buf_putc(Buf *b, char c);
void buf_puts(Buf *b, const char *s);
void buf_printf(Buf *b, const char *fmt, ...);
/* ccunits.c: a program's C cut into codegen units that compile in parallel (--dev); 0 when it stays whole.
   A negative count asks for that many units even for a small program. */
int cc_split_units(const char *src, size_t len, int want, Buf *units);

/* ---------- source positions and errors ---------- */

typedef struct {
    int line, col;
} Pos;

extern const char *xs_file_name;
extern const char *xs_source;

#include <setjmp.h>
/* Diagnostics: every error has a stable code from src/diag.def (XS1001...) and a category; `xisto check` counts
   what a program needs, by category. The category says what kind of change it asks for. */
typedef enum {
    DC_SYNTAX,       /* the text does not parse as the TypeScript xisto reads */
    DC_DYNAMIC,      /* any, undefined, a value that may be null where xisto needs one type */
    DC_MISSING_TYPE, /* a type the code has to write: a parameter, a return, an empty array */
    DC_NODE_IMPORT,  /* node:fs and friends: imported as xisto:fs */
    DC_MODULE,       /* a module or package that cannot be found or installed */
    DC_UNSUPPORTED,  /* a TypeScript feature xisto does not have (yet) */
    DC_RUNTIME_API,  /* a built-in (Date, Buffer, string...) used in a way xisto does not provide */
    DC_TYPE_RULE,    /* a type error under xisto's rules (int and number, one static type per value) */
    DC_DECLARATION,  /* an unknown, duplicate or unexported name */
    DC_INTERNAL,     /* a compiler bug */
    DC_SEMANTIC,     /* compiles, but may give another result than Node: to review (warnings) */
    DC_DYNPATH,      /* compiles: `any`, read and written by name and checked at run time (notes, not changes) */
    DC_DEPRECATED,   /* compiles, with a warning: a form xisto reads another way (undefined as null) */
    DC_IMPLICIT,     /* compiles, with a warning: a conversion xisto makes for the code (int to number) */
    DC_COUNT
} DiagCat;
typedef enum {
    XS0000, /* no code */
#define DIAG(code, cat, msg) code,
#include "diag.def"
#undef DIAG
    XS_DIAG_COUNT
} DiagCode;
const char *xs_diag_name(DiagCode c); /* "XS3014" */
DiagCat xs_diag_cat(DiagCode c);
const char *xs_diag_cat_title(DiagCat c);  /* "missing type annotation" */
const char *xs_diag_cat_fix(DiagCat c);    /* "manual", "automatic", "package", "review", "report" */
const char *xs_diag_cat_id(DiagCat c);     /* "missing-type" */
void xs_error(DiagCode code, Pos pos, const char *help, const char *fmt, ...);
void xs_error_quiet(void); /* back to the recovery point without a message: the error was reported */
bool xs_node_core_name(const char *spec);                  /* "path", "fs/promises": a Node module without "node:" */
char *xs_tsconfig_resolve(const char *from_file, const char *spec); /* a tsconfig "paths" name: its file or folder, or NULL */
extern bool xs_lex_skipping; /* xisto check: the parser is skipping a declaration that did not parse (no lexer errors there) */
extern bool xs_check_mode; /* xisto check: report every error (no limit of 20) and keep going where possible */
extern int xs_quiet_count;   /* follow-on errors kept quiet (a use of a declaration that failed) */
void xs_error_continue(DiagCode code, Pos pos, const char *help, const char *fmt, ...); /* report, count, go on */
void xs_warn(Pos pos, const char *help, const char *fmt, ...);
void xs_note_dynamic(Pos pos); /* a use of any (the dynamic path): warning XS2007 with its cost */
void xs_note_dynamic_record(Pos pos); /* an object made as any (a Record): XS2007 with the memory cost too */
void xs_warn_rule(DiagCode code, Pos pos, const char *help, const char *fmt, ...); /* XB-118: compiles, with a warning */
void xs_warn_rule_summary(void);
extern bool xs_show_warnings, xs_deny_warnings;
extern jmp_buf *xs_error_recover; /* when set, xs_error longjmps here instead of exiting (up to 20 errors) */
extern int xs_error_count;
extern bool xs_warnings;
/* --tls=off: never link TLS; a dynamic fetch URL is then assumed to be http:// (TLS ends at the load balancer) */
extern bool xs_tls_off;
/* --node-compat (or XISTO_NODE_COMPAT=1): accept node:fs and friends as aliases of xisto:fs */
extern bool xs_node_compat;
const char *xs_display_path(const char *path);
extern int xs_warning_count;
extern bool xs_json_errors; /* --json: one JSON object on stderr instead of the human message */
/* Closest name among candidates (edit distance), or NULL. Used for "did you mean" help lines. */
const char *xs_suggest(const char *name, const char **candidates, int n);
char *xs_help_suggest(const char *name, const char **candidates, int n, const char *fallback);

/* ---------- tokens ---------- */

typedef enum {
    TK_EOF, TK_IDENT, TK_NUMBER, TK_STRING,
    /* keywords */
    TK_FUNCTION, TK_LET, TK_CONST, TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_RETURN,
    TK_TRUE, TK_FALSE, TK_NEW, TK_CLASS, TK_THIS, TK_ASYNC, TK_AWAIT, TK_IMPORT, TK_EXPORT,
    TK_SWITCH, TK_CASE, TK_DEFAULT, TK_BREAK, TK_CONTINUE, TK_ENUM, TK_TEMPLATE, TK_TRY, TK_CATCH, TK_THROW,
    /* punctuation */
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE, TK_LBRACKET, TK_RBRACKET,
    TK_COMMA, TK_SEMI, TK_COLON, TK_DOT, TK_ELLIPSIS, TK_ARROW, TK_QUESTION, TK_AT,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_ASSIGN, TK_PLUS_ASSIGN, TK_MINUS_ASSIGN, TK_STAR_ASSIGN, TK_SLASH_ASSIGN,
    TK_PLUSPLUS, TK_MINUSMINUS,
    TK_EQ, TK_NEQ, TK_LT, TK_LE, TK_GT, TK_GE,
    TK_AND, TK_OR, TK_NOT, TK_PIPE,
    /* bitwise (JS semantics: 32-bit integers); >> and >>> are two or three adjacent '>' put together by the
       parser, so `Array<Array<int>>` still closes two type argument lists */
    TK_AMP, TK_CARET, TK_TILDE, TK_SHL, TK_SHR, TK_USHR, TK_STARSTAR,
    TK_AMP_ASSIGN, TK_PIPE_ASSIGN, TK_CARET_ASSIGN, TK_SHL_ASSIGN, TK_SHR_ASSIGN, TK_USHR_ASSIGN,
    /* null: a ?? b, a ??= b, a?.b (xisto's only empty value is null; see MANIFESTO, "null, não undefined") */
    TK_QQ, TK_QQ_ASSIGN, TK_QDOT,
    TK_IN, /* (an operator only: "key" in obj; the lexer reads in as a name) */
} TokKind;

typedef struct {
    TokKind kind;
    Pos pos;
    const char *start;
    int len;
    double num;      /* TK_NUMBER */
    long long ival;  /* TK_NUMBER: exact value when the literal is an integer that fits */
    bool is_int_lit;
    bool is_bigint;  /* TK_NUMBER written 123n: str holds the digits (0x.., 0o.., 0b.. kept) */
    char *str;       /* TK_STRING (unescaped), TK_IDENT (name) */
} Token;

typedef struct {
    const char *src;
    size_t pos;
    int line, col;
    Pos fixed;         /* set: every token takes this position (code the compiler writes: its errors point at the source it came from) */
} Lexer;

/* os.c: the operating system under the compiler (macOS, Linux, Windows) */
const char *xs_q(const char *s);          /* one quoted argument of a command line */
int xs_sh(const char *cmd);               /* run a command line: its exit code, or -1 */
const char *xs_devnull(void);
bool xs_mkdirs(const char *path);
bool xs_rename(const char *from, const char *to);
bool xs_copy_file(const char *from, const char *to);
bool xs_exists(const char *path);
bool xs_is_dir(const char *path);
bool xs_on_path(const char *prog);
const char *xs_tmp_base(void);
char *xs_make_temp_dir(const char *prefix);
const char *xs_home(void);
char *xs_real_path(const char *path, char *out);
bool xs_use_pkgs_dir(const char *dir);
int xs_ncpus(void);
/* runs C compiler command lines, at most `jobs` at a time, watching the memory they use together (the driver and
   its cc1 child): over the limit (half the RAM, or XISTO_CC_MEM_MB) they are all stopped and the result is false,
   with the message written. rcs[i] gets each command's exit code. */
bool xs_cc_run(char **cmds, int n, int jobs, int *rcs);
int xs_pid(void);
void xs_list_files(const char *dir, const char *suf1, const char *suf2, char ***out, int *n);
void xs_rmtree(const char *path);
void xs_setenv(const char *name, const char *value);
char *xs_slurp(const char *path, size_t *len);
long long xs_tree_size(const char *dir, int *count);
#if defined(_WIN32) /* setjmp as on POSIX: the C library's own, no SEH unwinding (the compiler's error recovery) */
#include <setjmp.h>
#if defined(__aarch64__)
#define _setjmp(b) __mingw_setjmp((b))
#define _longjmp(b, v) __mingw_longjmp((b), (v))
#else
#define _setjmp(b) _setjmp((b), NULL)
#define _longjmp(b, v) longjmp((b), (v))
#endif
#endif
int xs_unescape(const char *q, Buf *out, const char **bad); /* the escape after a backslash: chars taken, 0 if malformed */
/* compiler strings keep NUL as C0 80: the real length and bytes of a literal */
size_t xs_lit_len(const char *s);
void lexer_init(Lexer *lx, const char *src);
Token lexer_next(Lexer *lx);
bool lexer_regex(Lexer *lx, const char *slash, char **pattern, char **flags); /* re-reads a '/' token as /pattern/flags */
const char *tok_kind_name(TokKind k);
const char *tok_keyword_text(TokKind k);

/* ---------- types ---------- */

typedef enum { TY_NUMBER, TY_INT, TY_LITINT, TY_BOOLEAN, TY_STRING, TY_VOID, TY_ARRAY, TY_OBJECT, TY_FUNC, TY_ENUM, TY_NULLABLE, TY_NULL, TY_MAP, TY_SET, TY_STREAM, TY_CHAN,
               TY_JSON,   /* TypeScript's `object`: any value, held as its JSON text (jwt payloads, exception bodies) */
               TY_UNKNOWN, /* any value by reference (primitives boxed); `as T` checks the type at run time */
               TY_TPARAM,  /* T inside a generic function's template (tname): replaced in each instance */
               TY_UNION,   /* A | B (members in params): held like unknown (boxed); narrowed by typeof, instanceof, a kind */
               TY_TUPLE,   /* [A, B] as written (params): resolved to the object type of a tuple class (Class.is_tuple) */
               TY_PROMISE  /* Promise<T> kept as a value (elem: T); as a return type it is T and the function async (XB-106) */ } TypeKind;

typedef struct Class Class;
typedef struct Module Module;
typedef struct Enum Enum;

typedef struct Type {
    TypeKind kind;
    struct Type **targs;  /* Name<A, B> before resolution: the type arguments of a generic interface */
    int ntargs;
    struct Type *elem;    /* TY_ARRAY, TY_SET, TY_STREAM: element; TY_NULLABLE: the non-null type; TY_MAP: value */
    bool absent;          /* TY_NULLABLE of an optional field or parameter (x?: T): its null is JavaScript's undefined, so it
                             prints as undefined (JSON leaves the field out); the same type as T | null otherwise */
    struct Type *key;     /* TY_MAP */
    bool record;          /* TY_MAP written Record<K, V> (or an object used as a dictionary): read and written as an object
                             (m[k], m.k, Object.keys, k in m; JSON and console.log print it as an object) */
    Class *cls;           /* TY_OBJECT */
    Enum *en;             /* TY_ENUM */
    struct Type **params; /* TY_FUNC */
    int nparams;
    struct Type *ret;     /* TY_FUNC */
    bool rest;            /* TY_FUNC: the last parameter is ...xs: T[] (the arguments from there on, one array) */
    bool async_ret;       /* TY_FUNC: written (..) => Promise<R> (ret is R): a call not awaited can be kept as a promise */
    const char *tname;    /* TY_TPARAM; TY_STRING: a string literal type ("circle"), the only value allowed */
    const char **slits;   /* TY_STRING of a union of string literals ('error' | 'warn'): the values it was written with (any
                             string is allowed; they let obj[nivel](m) call one of those methods) */
    int nslits;
    struct Type *gen_sig; /* TY_FUNC of a generic function value (<T>(sql: string) => Promise<T | null>): held and called as this
                             type with every T as unknown; gen_sig is the signature as written (T a TY_TPARAM, names in
                             ftparams), from which a call f<Row>(sql) takes its result type (the unknown converted to it) */
    char **ftparams;
    int nftparams;
} Type;

extern Type *ty_number, *ty_int, *ty_litint, *ty_boolean, *ty_string, *ty_void, *ty_null, *ty_json, *ty_unknown, *ty_any, *ty_undef;
static inline bool ty_is_undef(const Type *t) { return t && t->kind == TY_UNKNOWN && !t->tname && t->absent; } /* the type undefined */
extern bool xs_any_used; /* checker.c: the program reads or writes an any by name */
static inline bool ty_is_any(const Type *t) { return t && t->kind == TY_UNKNOWN && t->tname; } /* any: the dynamic path */
Type *ty_nullable_of(Type *inner);
Type *ty_optional_of(Type *inner); /* x?: T */
static inline bool tk_bitop(int k) { return k == TK_AMP || k == TK_PIPE || k == TK_CARET || k == TK_SHL || k == TK_SHR || k == TK_USHR; }
static inline int tk_bitop_of_assign(int k) { /* &= -> & ...; 0 when not a bitwise compound assignment */
    return k == TK_AMP_ASSIGN ? TK_AMP : k == TK_PIPE_ASSIGN ? TK_PIPE : k == TK_CARET_ASSIGN ? TK_CARET : k == TK_SHL_ASSIGN ? TK_SHL : k == TK_SHR_ASSIGN ? TK_SHR : k == TK_USHR_ASSIGN ? TK_USHR : 0;
}
Type *ty_map_of(Type *key, Type *value);
Type *ty_set_of(Type *elem);
Type *ty_stream_of(Type *elem);
Type *ty_chan_of(Type *elem);
Type *ty_promise_of(Type *elem); /* Promise<T> (Promise<Promise<T>> is Promise<T>) */
Type *ty_strip_null(Type *t);
Type *ty_array_of(Type *elem);
Type *ty_object_of(Class *cls);
Type *ty_enum_of(Enum *en);
Type *ty_func(Type **params, int nparams, Type *ret);
bool ty_equal(Type *a, Type *b);
bool ty_is_heap(Type *t); /* string, array, object: reference counted */
const char *ty_name(Type *t);

/* ---------- AST ---------- */

typedef enum {
    E_NUMBER, E_STRING, E_BOOL, E_IDENT, E_THIS,
    E_UNARY, E_BINARY, E_ASSIGN, E_POSTFIX,
    E_CALL,      /* f(args) */
    E_INDEX,     /* a[i] */
    E_MEMBER,    /* obj.name or obj.name(args) */
    E_NEW_ARRAY, /* new Array<T>(n) */
    E_NEW_OBJ,   /* new C(args) */
    E_ARRAY_LIT, /* [a, b, c] */
    E_AWAIT,     /* await e: identity */
    E_FUNC_REF,  /* a top-level function used as a value */
    E_COND,      /* c ? a : b  (cond=lhs... stored as: lhs=cond, args[0]=then, args[1]=else) */
    E_TEMPLATE,  /* `a ${b} c`: args are the parts, E_STRING for the literal chunks */
    E_ENUM_VAL,  /* Color.Red, resolved: en + member index in field */
    E_NULL,      /* null */
    E_NEW_MAP,   /* new Map<K, V>() / new Set<T>(): type carries the kind */
    E_ARROW,     /* (a: T) => expr | { body }: a closure capturing by value */
    E_OBJLIT,    /* { a: 1, b }: keys in `keys`, values in args; type comes from context */
    E_AS,        /* lhs as T (T in elem_type): a no-op, or decoding an `object` (JSON) value into T */
    E_UPCAST,    /* a subclass where its base goes: lhs, typed as the base (a C cast) */
    E_SUPER_CALL,/* super(args) in a constructor: the base's initializer on this object (cls = the base) */
    E_REGEX,     /* /pattern/flags: str = pattern, rflags = flags */
    E_SEQ,       /* a, b in a for loop's update: args evaluated in order, no value */
    E_SPAWN,     /* an async call kept as a promise (lhs: the call, checked): it runs on a coroutine of its own */
} ExprKind;

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Func Func;
typedef struct Arrow Arrow;

typedef struct {
    Expr **items;
    int len, cap;
} ExprList;

/* How a built-in member call is implemented. */
typedef struct {
    const char *cfn;   /* C function name */
    bool self_first;   /* pass the receiver as first argument */
} Builtin;

struct Expr {
    ExprKind kind;
    Pos pos;
    Type *type;        /* filled by the checker */
    double num;        /* E_NUMBER */
    long long ival;
    bool is_int_lit;
    bool big_lit;      /* the argument of BigInt(...) made from a 123n literal */
    void *ctx;         /* checker: where its names resolve when that is not where it is checked (a parameter's default) */
    bool unset;        /* E_NULL: the start of `let x: Cls;` (assigned before any read, TypeScript checks it) */
    bool boolean;      /* E_BOOL */
    bool prechecked;   /* checked already (its type is final): check_expr leaves it as it is */
    bool want_tuple;   /* E_ARRAY_LIT: [k, v] returned to new Map(xs.map(...)): a tuple, as TypeScript reads it there */
    char *str;         /* E_STRING literal, E_IDENT name, E_MEMBER member name, E_NEW_OBJ class name */
    TokKind op;
    Expr *lhs, *rhs;
    ExprList args;
    bool is_call;
    bool opt_call;     /* x.f?.(args): f may be a function that is null (then the call is skipped) */      /* E_MEMBER */
    bool spread;       /* E_ARRAY_LIT with ...x elements (each an E_UNARY TK_ELLIPSIS): built by emit_spread_lit */
    Type *elem_type;   /* E_NEW_ARRAY */
    char *cname;       /* E_IDENT: resolved C name; E_FUNC_REF: C function name */
    /* resolved by the checker */
    Func *func;        /* E_CALL (user), E_MEMBER method call (user) */
    Class *cls;        /* E_NEW_OBJ, E_MEMBER on object */
    int field;         /* E_MEMBER: index of the field, or -1 */
    Builtin builtin;   /* E_MEMBER / E_CALL implemented by the runtime */
    int lit_id;        /* E_STRING: id of the interned literal */
    Enum *en;          /* E_ENUM_VAL */
    Arrow *arrow;      /* E_ARROW */
    char **keys;       /* E_OBJLIT */
    bool virt;         /* E_MEMBER method call: overridden below the receiver's class, dispatched at run time */
    bool super_call;   /* E_MEMBER: super.m(...), the base's method, never dispatched */
    Type **targs;      /* E_CALL / E_MEMBER call: explicit type arguments f<int>(x) */
    char *rflags;      /* E_REGEX */
    int ntargs;
    int region;        /* E_ARRAY_LIT of region objects made inside that region's loop (region.c): lives in the region */
    bool par_native;   /* E_CALL of xisto:parallel whose fn was proved independent (parallel.c): the pool version */
    bool rng_set;      /* E_IDENT: a range the pass that built it proved (distribute.c's pair slot counter) */
    bool loose;        /* E_BINARY: written == or != (the same as === / !== when no coercion could happen) */
    long long rng_lo, rng_hi;
    Type *gen_ret;     /* E_CALL of a generic function value with type arguments (f<Row>(x)): the result type its
                          signature gives; the unknown it returns is converted to it (here, or by the await around it) */
};

typedef struct {
    char *name;        /* variable name in the enclosing function */
    char *outer_cname; /* how to read it where the closure is created */
    Type *type;
    bool is_this;
    bool boxed;        /* the variable lives in a cell: the closure shares it (outer_cname is the cell) */
} Capture;

struct Arrow {
    int id;
    Pos pos;
    Func *fn;          /* synthetic function: params, ret, body (S_BLOCK) */
    Capture *captures;
    int ncaptures, cap_captures;
    bool expr_body;    /* `=> expr`: body is a block with one return (or one expression statement when void) */
    bool emitted;      /* its definition is out: a finally block is emitted twice (normal and throw paths) */
    bool synthetic;    /* made by the checker (a Promise.all/race task, a truthy test): not a callback in the source */
    Type *gen_sig;     /* async <T>(sql: string): Promise<T | null> => ...: its signature as written (Type.gen_sig); the body
                          reads T as unknown */
};

typedef enum { S_VAR, S_EXPR, S_IF, S_WHILE, S_FOR, S_RETURN, S_BLOCK, S_SWITCH, S_BREAK, S_CONTINUE, S_FOROF, S_TRY, S_THROW, S_YIELD } StmtKind;

typedef struct {
    Expr *value;       /* NULL for default */
    Stmt *body;        /* S_BLOCK (the statements until the next case) */
    Pos pos;
} Case;

typedef struct {
    Stmt **items;
    int len, cap;
} StmtList;

struct Stmt {
    StmtKind kind;
    Pos pos;
    Module *mod;       /* top-level statements */
    bool exported;     /* top-level S_VAR */
    char *name;        /* S_VAR */
    bool is_const;
    bool for_head;     /* S_VAR from for (let i = 0, j = ...): each iteration has its own copy, as JavaScript gives it */
    Type *decl_type;   /* may be NULL (inferred) */
    Type *type;
    char *cname;
    char *cell;        /* S_VAR captured by a closure and reassigned: the heap cell holding it (cname reads cell->v) */
    bool generated;    /* made by the compiler (reflection of decorated classes): may reach private members */
    Expr *init;
    Expr *expr;        /* S_EXPR, S_RETURN (may be NULL) */
    Expr *cond;        /* S_IF / S_WHILE / S_FOR */
    Stmt *for_init;
    Expr *for_update;
    Stmt *body;
    Stmt *else_body;
    StmtList stmts;    /* S_BLOCK */
    Case *cases;       /* S_SWITCH */
    int ncases;
    /* S_FOROF: name/decl_type/is_const/cname/type for the loop variable, expr = the array or stream */
    /* S_TRY: body = try block, else_body = catch block (may be NULL), name/cname = the caught Error,
       for_init (reused) = finally block (may be NULL) */
    /* S_THROW: expr */
    int region;        /* S_FOR / S_WHILE: its iterations' objects live in region N (region.c), 0: none */
    bool on_stack;     /* S_VAR = new C(...): the object lives in the frame (stackalloc.c) */
    unsigned stack_str; /* ... and these constructor arguments (bits), fresh strings that die with it, too */
    bool infer_int;    /* S_VAR `let i = 0` whose every assignment is an integer: an int (checker, XB-034) */
    bool par_once;     /* S_WHILE made by parallel.c around the body of a pool callback returning a number or a boolean:
                          one iteration per call, so region.c can give the callback's objects a region (XB-093) */
    bool map_direct;   /* S_FOROF `for (const [k, v] of map)` whose body cannot change the map: over its entries
                          in place, one tuple in the frame per entry, no snapshot array (checker, emit) */
};

/* @Name or @Name(args): kept on classes, methods, fields and parameters; the Nest pass (nest.c) reads them */
typedef struct {
    char *name;
    Pos pos;
    ExprList args;
    bool called;       /* @Name(...) rather than @Name */
    char *src;         /* "Name(args)" as written: the reflection pass applies it */
} Decorator;
typedef struct {
    Decorator *items;
    int len, cap;
} DecoList;

typedef struct {
    char *name;
    Type *type;
    char *cname;
    DecoList decos;
    bool is_prop;      /* constructor(private readonly x: T): also a field, assigned on entry */
    Expr *dflt;        /* x: T = value, or x?: T (null): what a call that leaves it out passes */
    int access;        /* parameter property: 0 public, 1 protected, 2 private */
    bool readonly;
    bool consumed;     /* constructors: the caller hands over a reference the body stores in a field (no retain) */
    char *cell;        /* captured by a closure and reassigned: copied into this heap cell on entry */
    char *type_src;    /* the type as written (reflection) */
    bool rest;         /* ...xs: T[]: the arguments from here on, as one array (checker, check_args) */
} Param;

struct Func {
    char *name;
    Pos pos;
    Module *mod;
    bool exported;
    bool used;         /* reachable from main: only used functions are compiled */
    Param *params;
    int nparams;
    Type *ret;
    Stmt *body;        /* S_BLOCK; NULL for built-ins */
    bool is_async;
    bool ret_promise;  /* declared to return Promise<T> (ret is T): async whether or not it is written async */
    Class *cls;        /* owning class for methods, else NULL */
    Class *static_of;  /* a static method (lowered to Class__s_name): its class, which reaches its private members */
    bool is_ctor;
    char *cname;
    DecoList decos;
    bool is_default;   /* export default function */
    bool is_native;    /* `declare function`: implemented in the package's C (xn_<package>_<name>) */
    char *ret_src;     /* the return type as written (reflection) */
    int fail_state;    /* emitter: 0 unknown, 1 being computed, 2 cannot fail, 3 can fail (stack traces) */
    int ncalls;        /* emitter: direct call sites; -1 when used as a value */
    int64_t *fixparam;  /* emitter: the length every call passes for each typed-array parameter (0: not known) */
    struct Expr *call1;     /* the call site when there is exactly one */
    struct Func *call1_in;  /* the function containing it (NULL: top level) */
    Module *call1_mod;
    int call1_site;    /* its xs_sitetab index, once created */
    int clone_state;   /* emitter: 0 unknown, 1 cloned per call site, 2 not */
    int rg_hoist;      /* emitter: N when every dual class it (transitively) makes belongs to region N; -1 never */
    bool rg_used;      /* emitter: its region version (f_rN) is reached from region N's loop body */
    bool is_abstract;  /* abstract m(): T; no body, every concrete subclass overrides it */
    int access;        /* 0 public, 1 protected, 2 private */
    bool is_getter, is_setter; /* get x(): T / set x(v: T): named __get_x / __set_x, used as obj.x */
    bool is_gen;       /* function* f(): Generator<T>: returns a Stream<T> whose values the body yields */
    struct Func **overrides; /* methods of subclasses that override this one (all levels) */
    int noverrides;
    bool virt_target;  /* called through a dispatcher: never inlined into one call site */
    bool run_once;     /* straight-line code called only from code that runs once: not optimized (build time) */
    bool escapes;      /* used as a value or dispatched: who calls it, and how often, is not known */
    /* generic functions and methods (function f<T>(x: T): T): a template, instantiated per list of types */
    char **tparams;
    int ntparams;
    Type **tdefaults;  /* <T = string>: the type a parameter takes when nothing binds it (NULL: none) */
    void *gen_state;   /* the parser where the parameters start: each instance is parsed again with T bound */
    void *gen_subst;   /* the substitution in force where it was written (a generic method of a generic class's instance) */
    bool gen_method;
    bool gen_arrow;    /* const f = <T>(x: T) => ...: parsed as a generic function (its body after =>) */
    struct Func **instances;
    char **inst_keys;
    int ninstances;
    struct Func *template_of;
    bool inst_checked;
    int ret_infer;     /* the return type left out: 1 to infer, 2 being inferred (its body checked), 3 inferred */
    struct Type *ret_acc; /* while inferred: the type the returns checked so far agree on */
    bool ret_null, ret_bare; /* a `return null;` / a `return;` among them */
    bool ret_failed;      /* its body had errors while inferred: uses of its result stop quietly */
    ExprList ret_exprs;   /* the returned values checked, converted to the type found at the end */
    ExprList ret_late;    /* `return [];`: checked once the type is known (it has no type by itself) */
    StmtList param_decls; /* f([a, b]: T) / ({ x }) => ...: the declarations that open the body (parser) */
    bool *sa_param_esc; /* stackalloc.c: which parameters may outlive the call (NULL: not analyzed, all may) */
    bool sa_this_esc;
    int par_state;     /* parallel.c: 0 not analyzed, 1 being analyzed, 2 safe on a worker thread, 3 not (par_why) */
    const char *par_why;
    bool par_native;   /* an instance of a xisto:parallel function some call runs on the pool: emit its _par version */
    bool sig_failed;   /* xisto check: its signature names what could not be loaded (reported): its body is not checked */
    bool shadow; /* a method with the name of a base method but another arity: what a call through the base runs is bridge */
    struct Func *bridge;
    Type *natural_ret; /* an arrow whose result goes where unknown goes: the type its return had (overload typing) */ /* (shadow) the base's signature, calling this method with the arguments converted (checker) */
    bool overload_sig; /* function f(a: A): R; (no body): an overload signature, kept with the implementation (overloads) */
    struct Func **overloads; /* the overload signatures written before this implementation: what its callers see */
    int noverloads;
    struct Type *gen_sig; /* an interface's query<T = D>(sql): R<T> (held with T as D): the signature as written, from which
                             a call q.query<Row>(sql) takes its result type (the value converted to it, see Type.gen_sig) */
};

typedef struct {
    char *name;
    Pos pos;
    Type *type;
    Expr *init;        /* may be NULL */
    char *cname;
    bool optional;     /* `name?: T`: T | null, starts as null */
    bool definite;     /* `name!: T`: assigned before it is read (TypeScript trusts it), outside the constructor too */
    DecoList decos;
    char *type_src;    /* the type as written, without the ? (reflection) */
    int access;        /* 0 public, 1 protected, 2 private */
    bool readonly;     /* assigned only in the constructor of its class */
    struct Class *owner; /* the class that declares it (inheritance copies fields down) */
} Field;

struct Class {
    char *name;
    Pos pos;
    Module *mod;
    bool exported;
    bool is_default;   /* export default class X, or export default X; of a class declared here */
    bool used;
    Field *fields;
    int nfields, cap_fields;
    Func **methods;
    int nmethods, cap_methods;
    Func *ctor;        /* may be NULL */
    Func **statics;    /* built-in classes only: Class.method(...) */
    int nstatics;
    bool builtin;
    bool is_interface; /* interface / type alias: fields only, printed without the name, built from object literals */
    char *cname;       /* C struct name */
    DecoList decos;
    char **implements; /* `implements A, B`: names only, the shape is not checked */
    int nimplements;
    char **tparams;    /* interface Name<T, U>: a template, instantiated per use (QueryResult<Row>) */
    struct Type **ctdefaults;     /* interface/class Name<T = unknown>: the type a parameter takes when it is left out (NULL: none) */
    int ntparams;
    struct Class *generic_of; /* an instance: its template */
    void *gen_state;   /* class Name<T> { ... }: the parser where the body starts; each instance parses it again with T bound */
    void *gen_subst;   /* the substitution in force where the template was written (a generic class inside an instance) */
    bool data_only;    /* fields without initializer and no constructor (a DTO): made from JSON, never with new */
    /* inheritance: the base's fields come first (prefix layout), its methods are listed too (their cls is the base) */
    char *base_name;
    struct Type **base_targs; /* class X extends Base<A, B>: the base is that instance of the generic Base */
    int nbase_targs;
    Pos base_pos;
    struct Class *base;
    struct Class **subs; /* direct subclasses */
    int nsubs;
    int nbase_fields;  /* fields[0..nbase_fields) are inherited */
    bool is_abstract;
    bool iface_methods; /* an interface that declares methods: the base of the class that implements it (checker) */
    bool iface_base;    /* that interface, made the abstract base: an implementation declares its fields again */
    struct Class *adapter_of; /* an adapter (XB-126): a subclass of an interface with methods made by the checker, holding
                                 an object of this class (field __t) whose methods its own call */
    struct Class **adapters;  /* an interface with methods: the adapters made for it (the run-time `as` looks them up) */
    int nadapters;
    int region;        /* region.c: its objects live in that loop's region (allocated there, freed per iteration) */
    int rdual;         /* region.c: dual, made inside that loop and outside it too: in the region while an iteration
                          runs (xs_rg_on_N), on the heap otherwise; counted either way, and freed with the region */
    bool stack_at;     /* stackalloc.c: some of its objects live in frames: Class_new_at(mem, ...) is emitted */
    bool nohdr;        /* emitter: objects without the 8-byte header (region objects that nothing counts, see nohdr_compute) */
    bool is_tuple;     /* [A, B]: a class the checker makes per list of element types, fields _0, _1, ... */
    bool flattened;
    bool methods_flat;
    char **iface_bases; /* interface B extends A, C: their fields come first */
    int niface_bases;
    /* static members: a top-level variable or function each, named <Class>__s_<name> */
    char **static_names;
    char **static_gen;
    bool *static_is_method;
    int nstatics_user;
    Field *sfields;    /* parsed static fields and methods, moved to the top level by the parser */
    int nsfields;
    bool *sfield_ro;
    struct Func **smethods;
    int nsmethods;
    Type *alias;       /* type Name = A | B: a name for another type (not an object shape) */
    bool extends_error; /* class X extends Error: fields message and name first, laid out as the runtime's Error */
    struct Mixin *mixin; /* class X extends PartialType(Y) (OmitType, PickType, IntersectionType): Y's fields copied in */
    char *mixin_call;    /* class X extends F(args) (F a class with a static __mixin, e.g. PassportStrategy(Strategy, "jwt")):
                            X extends F, and F.__mixin(args) runs as a class decorator on X */
    bool mixin_done;
    bool fake;           /* { m: () => ... } as C: an object literal standing for a class with methods (no super(...)) */
    bool as_value;       /* class { ... } written as an expression: a value (its ClassInfo), like a decorated class */
};
/* PartialType(Y), OmitType(Y, ["a"] as const), PickType(Y, ["a"]), IntersectionType(A, B): the mapped types of
   @nestjs/swagger (and @nestjs/mapped-types), a class or another mapped type as each argument */
typedef struct Mixin {
    const char *kind;  /* "PartialType", ...; NULL: just the class */
    const char *cls;
    struct Class *klass; /* the class itself, when the parser had it (T bound in a generic's instance): not looked up by name */
    Pos pos;
    struct Mixin *a, *b;
    char **keys;
    int nkeys;
} Mixin;
/* a class that extends Error, directly or through its bases */
static inline bool is_error_class(struct Class *c) {
    for (; c; c = c->base) if (c->extends_error) return true;
    return false;
}

typedef struct {
    char *name;
    bool is_string;
    long long ival;
    char *sval;
    int lit_id;
} EnumMember;

struct Enum {
    char *name;
    Pos pos;
    Module *mod;
    bool exported;
    bool is_string;    /* every member is a string */
    EnumMember *members;
    int nmembers;
};

typedef struct {
    char **names;      /* imported names */
    Pos *positions;
    int n;
    bool is_default;   /* import x from "m": names[0] must be the name of m's default export */
    char *path;        /* as written */
    Module *from;      /* resolved */
    Pos pos;
    bool reexport;     /* export { a, b } from "m" / export * from "m": the names are this module's exports too */
    bool star;         /* export * from "m": every name m exports (names is empty) */
    char **as_names;   /* export { a as b } from "m": the name each entry is exported under here (NULL: its own name) */
    bool *qualified;   /* import { A as B } next to another import of A: B reads as "A\x01m", A is not visible plainly from here */
} Import;

struct Module {
    int id;
    char *path;        /* real path */
    char *file;        /* as given, for messages */
    char *src;
    Import *imports;
    int nimports;
    char **local_exports; /* export { a, b }; (no "from"): names declared here, exported by a list */
    int nlocal_exports;
    char **exp_from, **exp_to; /* export { a as b };: the local (or imported) a is exported as b */
    int nexp;
    char *default_name;   /* export default f / export default function f / export default <expr> (a const of its own):
                             the declaration `import x from "m"` reads as x */
    bool loading, loaded;
    bool builtin;      /* "node:fs" and friends: functions provided by the runtime */
    bool missing;      /* xisto check: a module that could not be loaded (reported); its imported names are not reported again */
    char *native_prefix; /* package modules: C names of `declare` functions and classes start with this ("xn_pg_") */
};

typedef struct {
    Func **funcs;
    int nfuncs, cap_funcs;
    Class **classes;
    int nclasses, cap_classes;
    StmtList top;      /* top-level statements of every module, dependencies first */
    Module **modules;  /* dependencies first; the entry module is last */
    int nmodules, cap_modules;
    Enum **enums;
    int nenums, cap_enums;
    int narrows;       /* arrow functions numbered by the checker */
    /* filled by the checker */
    char **lits;       /* interned string literals */
    int nlits, cap_lits;
    bool uses_net;
    bool uses_workers; /* imports xisto:worker_threads: per-thread globals, the program body callable (emit.c) */
    bool uses_parallel; /* imports xisto:parallel (parser) */
    bool par_native;   /* some call of it runs on the thread pool (parallel.c): per-thread runtime state (XS_THREADS),
                          the program globals shared */
    bool uses_gen;     /* generators: the coroutine runtime, without worker threads (plain globals, no TLS) */
    bool uses_sqlite;
    bool uses_regex;    /* RegExp: runtime/xisto_regex.h joins the program */
    bool uses_tls;
    bool uses_crypto;  /* node:crypto, or a native package that needs it */
    char **native_files; /* C sources of native packages (xisto.json "native"), compiled with the program */
    int nnative;
} Program;

void expr_list_push(ExprList *l, Expr *e);
void stmt_list_push(StmtList *l, Stmt *s);

Program *parse_program(const char *entry_file);
void par_analyze(Program *p); /* parallel.c: which xisto:parallel calls can run on the thread pool */
char *xs_read_file(const char *path);
char *xs_format(const char *src); /* xisto fmt */
void check_program(Program *p);
void columns_program(Program *p); /* columns.c: a local array of number-only objects as one Float64Array per field */
void distribute_program(Program *p); /* distribute.c: a pair loop over columns, its pure slow consts first for every pair */
void inplace_program(Program *p); /* inplace.c: for-of over readStdinParts whose part stays in its iteration reads in place */
extern bool xs_pad4; /* columns.c: x y z fields as one column of 4 lanes (x86 with AVX2) */
void pad4_program(Program *p); /* pad4.c */
extern bool xs_wide_f64; /* distribute.c: the target has 4-lane double vectors (AVX2): the slow consts that read no column go in a flat loop */
void prune_program(Program *p); /* marks what main reaches; emitters skip the rest */
Func *xs_parse_instance(Func *tmpl, Type **args);
Class *xs_parse_class_instance(Class *tmpl, Type **args, const char *name); /* parser.c: a generic function parsed again with its T bound */
extern bool xs_debug;   /* --debug: statement hooks and the debugger agent (docs/debugger.md) */
extern bool xs_profile; /* --profile: instrument every function with call counts and timing */
/* region.c: loops whose iterations' objects can be freed in one step (Stmt.region, Class.region) */
void region_compute(Program *p);
extern bool region_allow_dual; /* set before region_compute: dual classes allowed (no try: an exception leaving the
                                  loop would leave its region on) */
extern int region_count;
/* stackalloc.c: objects that cannot outlive the call making them (Stmt.on_stack, Class.stack_at) */
void stack_compute(Program *p, bool (*cyclic)(Class *));
void emit_program(Program *p, Buf *out);
void emit_program_qbe(Program *p, Buf *out);

/* QBE backend split into cacheable units: one per module with reachable code, plus "main". */
typedef struct { char *name; Buf text; } Unit;
Unit *emit_program_qbe_units(Program *p, int *nunits);

#endif
