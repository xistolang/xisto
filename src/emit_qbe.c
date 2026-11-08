/* QBE IL emitter: the path that does not go through C.
   Covers int, number, boolean, arrays of them, strings (values, concatenation, template literals, methods),
   classes and interfaces (fields, constructors, methods, object literals, nullable references), functions,
   control flow, Math and console.log. Objects carry a field descriptor right after the refcount header,
   so one generic routine frees and prints every class. Closures, Map/Set, streams, exceptions and
   networking still go through the C emitter. */
#include "xisto.h"
#include <math.h>
#include <stdarg.h>

/* reference-counted in this backend: heap types plus string enums (their values are immortal literal strings,
   so retaining them is free, and treating them as references everywhere keeps fields, arrays and printing right) */
static bool qref(Type *t) { return ty_is_heap(t) || (t->kind == TY_ENUM && t->en->is_string); }

static Buf *out;
/* Template literals keep their parts in a stack area reserved once at function entry: an alloc inside a
   loop would grow the stack on every iteration. Each template site gets its own offset in that area. */
static int tpl_off;
#define TPL_MARK "\t# tplbuf\n"
static void tpl_finish(Buf *b, size_t from) {
    char *m = strstr(b->data + from, TPL_MARK);
    if (!m) return;
    char *line = tpl_off ? xs_sprintf("\t%%tplbuf =l alloc8 %d\n", tpl_off) : "";
    size_t pre = (size_t)(m - b->data), ml = strlen(TPL_MARK);
    Buf n = {0};
    buf_printf(&n, "%.*s", (int)pre, b->data);
    buf_puts(&n, line);
    buf_puts(&n, m + ml);
    free(b->data);
    *b = n;
}
static Program *prog;
static int tmp_id, label_id;
static Func *cur_func;
static bool used_ar;

/* ---- debugger (--debug; docs/debugger.md, F6) ----
   A frame on the stack (%dbgf), a hook call per statement, a leave call before every ret, and a shadow cell
   per local (%dbgc + 8k) stored at every assignment: QBE locals are SSA values with no address. The table of
   points is shared by all units and written into the main unit. */
#define DBG_MARK "\t# dbg\n"
static Buf qdbg_points;
static int qdbg_n;
typedef struct { const char *name; Type *t; const char *cname; int from, to; void *scope; bool global; } QVar;
static QVar *qv;
static int nqv;
static int qdbg_fn; /* names this function's data */
static bool cur_func_has_frame;
static void qdbg_local(const char *name, Type *t, const char *cname);
static void qdbg_store(const char *cname, Type *t);
static void qdbg_leave(void);
static const char *qdbg_type(Type *t) {
    bool nul = t->kind == TY_NULLABLE;
    Type *b = nul ? t->elem : t;
    switch (b->kind) {
    case TY_INT: case TY_LITINT: return nul ? "$xr_dbg_t_int_null" : "$xr_dbg_t_int";
    case TY_NUMBER: return nul ? "$xr_dbg_t_num_null" : "$xr_dbg_t_num";
    case TY_BOOLEAN: return nul ? "$xr_dbg_t_bool_null" : "$xr_dbg_t_bool";
    case TY_ENUM: return b->en->is_string ? "$xr_dbg_t_str" : nul ? "$xr_dbg_t_int_null" : "$xr_dbg_t_int";
    case TY_STRING: return "$xr_dbg_t_str";
    default: return "$xr_dbg_t_ref";
    }
}
static void qdbg_bytes(Buf *b, const char *s) {
    for (const unsigned char *c = (const unsigned char *)s; *c; c++) buf_printf(b, "b %d, ", *c);
    buf_puts(b, "b 0");
}

typedef enum { QS_BLOCK, QS_FUNC, QS_LOOP_BODY, QS_SWITCH } QKind;
typedef struct QScope {
    char **names; Type **types;
    int n, cap;
    bool func_root;
    QKind kind;
    char *break_label, *continue_label;
    struct QScope *parent;
} QScope;
static QScope *qscope;

static void qpush(bool root) {
    QScope *s = xs_alloc(sizeof *s);
    s->func_root = root;
    s->kind = root ? QS_FUNC : QS_BLOCK;
    s->parent = qscope;
    qscope = s;
}
static void qdbg_scope_end(void *sc);
static void qpop(void) { qdbg_scope_end(qscope); qscope = qscope->parent; }
static void qtrack(const char *name, Type *t) {
    if (!qref(t)) return;
    QScope *s = qscope;
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 4;
        s->names = realloc(s->names, sizeof(char *) * (size_t)s->cap);
        s->types = realloc(s->types, sizeof(Type *) * (size_t)s->cap);
    }
    s->names[s->n] = (char *)name;
    s->types[s->n++] = t;
}

static void unsupported(Pos pos, const char *what) {
    xs_error(pos, "build without --backend=qbe to use the C path", "%s is not supported by the qbe backend yet", what);
}

/* QBE base type letter for a xisto type */
static const char *qt(Type *t) {
    if (t->kind == TY_ENUM && t->en->is_string) return "l";
    switch (t->kind) {
    case TY_NUMBER: return "d";
    case TY_INT: case TY_LITINT: return "l";
    case TY_BOOLEAN: return "w";
    case TY_ARRAY: return "l";
    case TY_ENUM: return "l";
    case TY_STREAM: return "l";
    case TY_CHAN: return "l";
    default: return "l";
    }
}

/* ---- stack-trace sites: a `dbgloc <site>` before every call; main.c turns the .loc that qbe emits into a
   label right after the call instruction (see "stack traces" in runtime/xisto_rt.h) ---- */
/* read-only data next to the GOT on macOS: no extra 16 KB page for __DATA */
#if defined(__APPLE__)
#define XS_QRO "section \"__DATA_CONST,__const\" "
#else
#define XS_QRO ""
#endif
static Buf qsites;          /* data items of this unit's site table */
static int nqsites;
static Buf qstrs;           /* the strings the table points to */
static char **qstr_vals;
static int nqstrs, cap_qstrs;
static const char *unit_tag;
static Pos cur_pos;
static Module *cur_mod;
static int last_site = -1;
static Pos last_site_pos;
static Func *last_site_fn;
static char *qstr(const char *v) {
    for (int i = 0; i < nqstrs; i++) if (strcmp(qstr_vals[i], v) == 0) return xs_sprintf("$xs_ss_%s_%d", unit_tag, i);
    if (nqstrs == cap_qstrs) { cap_qstrs = cap_qstrs ? cap_qstrs * 2 : 16; qstr_vals = realloc(qstr_vals, sizeof(char *) * (size_t)cap_qstrs); }
    qstr_vals[nqstrs] = (char *)v;
    buf_printf(&qstrs, "%sdata $xs_ss_%s_%d = { ", XS_QRO, unit_tag, nqstrs);
    for (const unsigned char *c = (const unsigned char *)v; *c; c++) buf_printf(&qstrs, "b %d, ", *c);
    buf_puts(&qstrs, "b 0 }\n");
    return xs_sprintf("$xs_ss_%s_%d", unit_tag, nqstrs++);
}
static int qsite(void) {
    if (last_site > 0 && last_site_fn == cur_func && last_site_pos.line == cur_pos.line && last_site_pos.col == cur_pos.col) return last_site;
    if (!nqsites) { buf_puts(&qsites, "l 0, l 0, l 0, w 0, w 0"); nqsites = 1; }
    const char *fn = "";
    if (cur_func && cur_func->is_ctor && cur_func->cls) fn = xs_sprintf("new %s", cur_func->cls->name);
    else if (cur_func && cur_func->cls) fn = xs_sprintf("%s.%s", cur_func->cls->name, cur_func->name);
    else if (cur_func && strcmp(cur_func->name, "arrow") != 0) fn = cur_func->name;
    Module *m = cur_func && cur_func->mod ? cur_func->mod : cur_mod;
    buf_printf(&qsites, ", l %s, l %s, l 0, w %d, w %d", qstr(fn), qstr(m && m->file ? xs_display_path(m->file) : "?"), cur_pos.line, cur_pos.col);
    last_site_fn = cur_func;
    last_site_pos = cur_pos;
    return last_site = nqsites++;
}
static void qsites_reset(const char *tag) {
    unit_tag = tag;
    qsites.len = 0; if (qsites.data) qsites.data[0] = 0;
    qstrs.len = 0; if (qstrs.data) qstrs.data[0] = 0;
    nqsites = 0; nqstrs = 0; last_site = -1;
}
static void qsites_flush(Buf *b) {
    if (!nqsites) return;
    buf_puts(b, qstrs.data);
    buf_printf(b, "%sdata $xs_sitetab_%s = align 8 { %s }\n", XS_QRO, unit_tag, qsites.data);
}

static char *tmp(void) { return xs_sprintf("%%t%d", ++tmp_id); }
static char *label(const char *hint) { return xs_sprintf("@%s%d", hint, ++label_id); }

static void ins(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    buf_puts(out, "\t");
    char *s = NULL;
    {
        va_list ap2;
        va_copy(ap2, ap);
        int n = vsnprintf(NULL, 0, fmt, ap2);
        va_end(ap2);
        s = xs_alloc((size_t)n + 1);
        vsnprintf(s, (size_t)n + 1, fmt, ap);
    }
    va_end(ap);
    if (strstr(s, "call $") && !getenv("XISTO_NO_TRACE")) buf_printf(out, "dbgloc %d\n\t", qsite());
    buf_puts(out, s);
    buf_puts(out, "\n");
}

static void blk(const char *l) { buf_printf(out, "%s\n", l); }

/* literal names come from the content, so adding a literal elsewhere does not rename this one */
static uint64_t lit_hash(const char *s) {
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ull; }
    return h;
}
static bool *lit_used; /* only literals reached by compiled code get emitted (and exported) */
static char *lit_name(Program *p, int id) {
    if (lit_used) lit_used[id] = true;
    return xs_sprintf("$lit_%016llx", (unsigned long long)lit_hash(p->lits[id]));
}

/* the string object of a literal: an immortal xs_str created once at startup by main */
static char *lit_obj(Program *p, int id) {
    if (lit_used) lit_used[id] = true;
    return xs_sprintf("$s_%016llx", (unsigned long long)lit_hash(p->lits[id]));
}

static const char *arr_suffix(Type *arr) {
    if (qref(arr->elem)) return "ref";
    return arr->elem->kind == TY_NUMBER ? "num" : (arr->elem->kind == TY_INT || arr->elem->kind == TY_ENUM) ? "int" : "bool";
}
static bool is_str_like(Type *t) { return t->kind == TY_STRING || (t->kind == TY_ENUM && t->en->is_string); }

/* object layout: xs_hdr (8 bytes), descriptor pointer (8), then one 8-byte slot per field */
static int field_off(int idx) { return 16 + 8 * idx; } /* 8-byte header, then the field descriptor */
static int obj_size(Class *c) { return 16 + 8 * (c->nfields ? c->nfields : 1); }

/* "xs_str_slice" -> "xr_str_slice": the exported wrappers in runtime/xisto_rt.c */
static char *xr_name(const char *cfn) { return xs_sprintf("xr_%s", strncmp(cfn, "xs_", 3) == 0 ? cfn + 3 : cfn); }

/* Returns the name of a value (temp, constant or global load) holding the expression. */
static char *emit_expr(Expr *e);

static bool is_fresh(Expr *e) {
    if (!qref(e->type)) return false;
    switch (e->kind) {
    case E_ARRAY_LIT: case E_NEW_OBJ: case E_OBJLIT: case E_TEMPLATE: case E_CALL: return true;
    case E_MEMBER: return e->lhs->kind == E_NEW_ARRAY || e->is_call; /* methods return owned references */
    case E_BINARY: return e->op == TK_PLUS; /* string concatenation */
    case E_AWAIT: return is_fresh(e->lhs);
    default: return false;
    }
}

static char *emit_owned(Expr *e) {
    char *v = emit_expr(e);
    if (!qref(e->type) || is_fresh(e)) return v;
    char *r = tmp();
    ins("%s =l call $xr_retain(l %s)", r, v);
    return r;
}

static char *emit_borrowed(Expr *e) {
    char *v = emit_expr(e);
    if (is_fresh(e)) {
        used_ar = true;
        ins("call $xr_ar(l %s)", v);
    }
    return v;
}

static char *emit_number_const(double v) {
    char buf[64];
    if (isinf(v)) return xs_sprintf("d_%s", v > 0 ? "inf" : "-inf");
    if (isnan(v)) return "d_nan";
    snprintf(buf, sizeof buf, "d_%.17g", v);
    return xs_strndup(buf, strlen(buf));
}

/* &a[i], checked in line (XB-006): the length at offset 8 and the data pointer at 16 of every runtime array
   (xs_hdr, len, data); an index outside calls the runtime's accessor, which stops with the usual message.
   Before, every a[i] was a call (nbody through qbe: 1.74 s against 0.41 s for the C backend). */
static char *elem_ptr(Type *arr, const char *a, const char *i) {
    char *lenp = tmp(), *len = tmp(), *in = tmp(), *dp = tmp(), *data = tmp(), *off = tmp(), *p = tmp();
    char *bad = label("oob"), *good = label("inb");
    int sz = qref(arr->elem) || arr->elem->kind == TY_NUMBER || arr->elem->kind == TY_INT || arr->elem->kind == TY_ENUM ? 8 : 1;
    ins("%s =l add %s, 8", lenp, a);
    ins("%s =l loadl %s", len, lenp);
    ins("%s =w cultl %s, %s", in, i, len); /* unsigned: a negative index is out too */
    ins("jnz %s, %s, %s", in, good, bad);
    blk(bad);
    ins("call $xr_at_%s(l %s, l %s)", arr_suffix(arr), a, i);
    ins("hlt");
    blk(good);
    ins("%s =l add %s, 16", dp, a);
    ins("%s =l loadl %s", data, dp);
    if (sz == 8) ins("%s =l mul %s, 8", off, i); else ins("%s =l copy %s", off, i);
    ins("%s =l add %s, %s", p, data, off);
    return p;
}

/* address of an lvalue (array slot or global); locals are temps and have no address */
static char *emit_slot_ptr(Expr *e) {
    if (e->kind == E_IDENT) return xs_sprintf("$%s", e->cname); /* only called for globals */
    if (e->kind == E_MEMBER) { /* object field */
        char *o = emit_borrowed(e->lhs);
        char *p = tmp();
        ins("%s =l add %s, %d", p, o, field_off(e->field));
        return p;
    }
    char *a = emit_borrowed(e->lhs);
    char *i = emit_expr(e->rhs);
    return elem_ptr(e->lhs->type, a, i);
}

/* Compound assignment and ++/-- on a field, element or global: the object/array and the index are evaluated
   once (a[i++] += 5 bumps i once), and the address is recomputed for the read and for the write, so a
   right-hand side that grows the array cannot leave a stale pointer behind. */
typedef struct { char *base, *idx; } Lval;
static Lval lv_capture(Expr *t) {
    Lval lv = {0};
    if (t->kind == E_MEMBER) lv.base = emit_borrowed(t->lhs);
    else if (t->kind == E_INDEX) { lv.base = emit_borrowed(t->lhs); lv.idx = emit_expr(t->rhs); }
    return lv;
}
static char *lv_ptr(Expr *t, Lval lv) {
    if (t->kind == E_IDENT) return xs_sprintf("$%s", t->cname);
    char *p = tmp();
    if (t->kind == E_MEMBER) ins("%s =l add %s, %d", p, lv.base, field_off(t->field));
    else return elem_ptr(t->lhs->type, lv.base, lv.idx);
    return p;
}

static int print_kind(Type *elem) { return is_str_like(elem) ? 1 : 0; } /* how a reference array prints its elements*/

static bool is_global(Expr *e) { return e->kind == E_IDENT && e->cname[0] == 'g' && e->cname[1] >= '0' && e->cname[1] <= '9'; }

static char *load_from(Type *t, const char *p) {
    char *r = tmp();
    switch (t->kind) {
    case TY_NUMBER: ins("%s =d loadd %s", r, p); break;
    case TY_INT: ins("%s =l loadl %s", r, p); break;
    case TY_BOOLEAN: ins("%s =w loadub %s", r, p); break;
    default: ins("%s =l loadl %s", r, p); break;
    }
    return r;
}

static void store_to(Type *t, const char *v, const char *p) {
    switch (t->kind) {
    case TY_NUMBER: ins("stored %s, %s", v, p); break;
    case TY_INT: ins("storel %s, %s", v, p); break;
    case TY_BOOLEAN: ins("storeb %s, %s", v, p); break;
    default: ins("storel %s, %s", v, p); break;
    }
}

static char *emit_read(Expr *e) { /* IDENT or INDEX read */
    if (e->kind == E_IDENT) {
        if (is_global(e)) return load_from(e->type, xs_sprintf("$%s", e->cname));
        return xs_sprintf("%%%s", e->cname);
    }
    return load_from(e->type, emit_slot_ptr(e));
}

/* stores v into the lvalue; for locals, rebinds the temp */
static void emit_write(Expr *target, const char *v) {
    if (target->kind == E_IDENT && !is_global(target)) {
        if (qref(target->type)) {
            char *old = tmp();
            ins("%s =l copy %%%s", old, target->cname);
            ins("%%%s =l copy %s", target->cname, v);
            qdbg_store(target->cname, target->type);
            ins("call $xr_release(l %s)", old);
            return;
        }
        ins("%%%s =%s copy %s", target->cname, qt(target->type), v);
        qdbg_store(target->cname, target->type);
        return;
    }
    if (qref(target->type)) {
        /* release old, store new */
        char *p = target->kind == E_IDENT ? xs_sprintf("$%s", target->cname) : emit_slot_ptr(target);
        char *old = tmp();
        ins("%s =l loadl %s", old, p);
        ins("storel %s, %s", v, p);
        ins("call $xr_release(l %s)", old);
        return;
    }
    store_to(target->type, v, target->kind == E_IDENT ? xs_sprintf("$%s", target->cname) : emit_slot_ptr(target));
}

/* store into an already computed address (non-local lvalue) */
static void emit_write_at(Expr *target, const char *v, const char *p) {
    if (qref(target->type)) {
        char *old = tmp();
        ins("%s =l loadl %s", old, p);
        ins("storel %s, %s", v, p);
        ins("call $xr_release(l %s)", old);
        return;
    }
    store_to(target->type, v, p);
}

static char *checked_add_sub(const char *a, const char *b, bool sub) {
    char *r = tmp(), *x = tmp(), *y = tmp(), *z = tmp(), *c = tmp();
    char *ovf = label("ovf"), *ok = label("ok");
    ins("%s =l %s %s, %s", r, sub ? "sub" : "add", a, b);
    if (sub) {
        ins("%s =l xor %s, %s", x, a, b);
        ins("%s =l xor %s, %s", y, a, r);
    } else {
        ins("%s =l xor %s, %s", x, a, r);
        ins("%s =l xor %s, %s", y, b, r);
    }
    ins("%s =l and %s, %s", z, x, y);
    ins("%s =w csltl %s, 0", c, z);
    ins("jnz %s, %s, %s", c, ovf, ok);
    blk(ovf);
    ins("call $xr_panic_overflow()");
    ins("hlt");
    blk(ok);
    return r;
}

static char *emit_int_arith(TokKind op, const char *a, const char *b) {
    char *r = tmp();
    switch (op) {
    case TK_PLUS: return checked_add_sub(a, b, false);
    case TK_MINUS: return checked_add_sub(a, b, true);
    case TK_STAR: ins("%s =l call $xr_imul(l %s, l %s)", r, a, b); return r;
    case TK_PERCENT: ins("%s =l call $xr_imod(l %s, l %s)", r, a, b); return r;
    default: return r;
    }
}

static const char *cmp_op(TokKind op, Type *t) {
    bool d = t->kind == TY_NUMBER;
    switch (op) {
    case TK_LT: return d ? "cltd" : "csltl";
    case TK_LE: return d ? "cled" : "cslel";
    case TK_GT: return d ? "cgtd" : "csgtl";
    case TK_GE: return d ? "cged" : "csgel";
    case TK_EQ: return d ? "ceqd" : t->kind == TY_BOOLEAN ? "ceqw" : "ceql";
    case TK_NEQ: return d ? "cned" : t->kind == TY_BOOLEAN ? "cnew" : "cnel";
    default: return "?";
    }
}

static void emit_print(Expr *a) {
    if (a->type->kind == TY_STRING && a->kind == E_STRING) {
        ins("call $xr_print_bytes(l %s, l %zu)", lit_name(prog, a->lit_id), strlen(a->str));
        return;
    }
    char *v = emit_borrowed(a);
    switch (a->type->kind) {
    case TY_STRING: ins("call $xr_print_str(l %s)", v); break;
    case TY_OBJECT: ins("call $xr_print_obj(l %s)", v); break;
    case TY_NULLABLE: case TY_NULL:
        if (a->type->kind == TY_NULLABLE && a->type->elem->kind == TY_STRING) ins("call $xr_print_str_or_null(l %s)", v);
        else ins("call $xr_print_obj_or_null(l %s)", v);
        break;
    case TY_NUMBER: ins("call $xr_print_num(d %s)", v); break;
    case TY_INT: ins("call $xr_print_int(l %s)", v); break;
    case TY_ENUM: if (a->type->en->is_string) ins("call $xr_print_str(l %s)", v); else ins("call $xr_print_int(l %s)", v); break;
    case TY_BOOLEAN: ins("call $xr_print_bool(w %s)", v); break;
    case TY_ARRAY: ins("call $xr_print_obj(l %s)", v); break;
    default: unsupported(a->pos, "printing this type");
    }
}

static char *emit_expr_raw(Expr *e);
static char *emit_expr(Expr *e) {
    Pos saved = cur_pos;
    cur_pos = e->pos;
    char *r = emit_expr_raw(e);
    cur_pos = saved;
    return r;
}
static char *emit_expr_raw(Expr *e) {
    switch (e->kind) {
    case E_NUMBER:
        if (e->type->kind == TY_INT) return xs_sprintf("%lld", e->ival);
        return emit_number_const(e->num);
    case E_BOOL: return e->boolean ? "1" : "0";
    case E_IDENT: return emit_read(e);
    case E_AWAIT: return emit_expr(e->lhs);
    case E_STRING: return load_from(ty_string, lit_obj(prog, e->lit_id));
    case E_NULL: return "0";
    case E_THIS: return "%self";
    case E_FUNC_REF: unsupported(e->pos, "function values"); return "";
    case E_NEW_OBJ: {
        Class *c = e->cls;
        if (c->builtin) unsupported(e->pos, "built-in classes");
        Buf args = {0};
        for (int i = 0; i < e->args.len; i++) {
            char *v = emit_borrowed(e->args.items[i]);
            buf_printf(&args, "%s%s %s", i ? ", " : "", qt(c->ctor->params[i].type), v);
        }
        char *r = tmp();
        ins("%s =l call $%s(%s)", r, c->ctor ? c->ctor->cname : xs_sprintf("%s_new", c->cname), args.data ? args.data : "");
        return r;
    }
    case E_OBJLIT: {
        Class *c = e->cls;
        char *o = tmp();
        ins("%s =l call $%s_new()", o, c->cname);
        for (int i = 0; i < e->args.len; i++) {
            int idx = -1;
            for (int k = 0; k < c->nfields; k++) if (strcmp(c->fields[k].name, e->keys[i]) == 0) idx = k;
            Type *ft = c->fields[idx].type;
            char *v = qref(ft) ? emit_owned(e->args.items[i]) : emit_expr(e->args.items[i]);
            char *p = tmp();
            ins("%s =l add %s, %d", p, o, field_off(idx));
            if (qref(ft)) ins("call $xr_assign_slot(l %s, l %s)", p, v);
            else store_to(ft, v, p);
        }
        return o;
    }
    case E_ENUM_VAL:
        if (e->en->is_string) return load_from(ty_string, lit_obj(prog, e->en->members[e->field].lit_id));
        return xs_sprintf("%lld", e->en->members[e->field].ival);
    case E_TEMPLATE: {
        /* the parts as strings in a stack array, joined by the runtime */
        int n = e->args.len;
        char *arr = tmp();
        ins("%s =l add %%tplbuf, %d", arr, tpl_off);
        tpl_off += 8 * (n ? n : 1);
        for (int i = 0; i < n; i++) {
            Expr *part = e->args.items[i];
            char *v;
            switch (part->type->kind) {
            case TY_STRING: v = emit_borrowed(part); break;
            case TY_ENUM: if (part->type->en->is_string) { v = emit_borrowed(part); break; } /* fallthrough */
            case TY_INT: v = tmp(); ins("%s =l call $xr_str_from_int(l %s)", v, emit_expr(part)); ins("call $xr_ar(l %s)", v); used_ar = true; break;
            case TY_NUMBER: v = tmp(); ins("%s =l call $xr_str_from_num(d %s)", v, emit_expr(part)); ins("call $xr_ar(l %s)", v); used_ar = true; break;
            case TY_BOOLEAN: v = tmp(); ins("%s =l call $xr_str_from_bool(w %s)", v, emit_expr(part)); ins("call $xr_ar(l %s)", v); used_ar = true; break;
            default: unsupported(part->pos, "this value in a template literal"); return "";
            }
            char *slot = tmp();
            ins("%s =l add %s, %d", slot, arr, 8 * i);
            ins("storel %s, %s", v, slot);
        }
        char *r = tmp();
        ins("%s =l call $xr_tpl_arr(w %d, l %s)", r, n, arr);
        return r;
    }
    case E_NEW_MAP: unsupported(e->pos, "Map, Set and Channel"); return "";
    case E_ARROW: unsupported(e->pos, "arrow functions"); return "";
    case E_COND: {
        char *res = tmp(), *tl = label("then"), *el = label("else"), *end = label("endc");
        char *c = emit_expr(e->lhs);
        ins("jnz %s, %s, %s", c, tl, el);
        blk(tl);
        char *a = emit_owned(e->args.items[0]);
        ins("%s =%s copy %s", res, qt(e->type), a);
        ins("jmp %s", end);
        blk(el);
        char *b = emit_owned(e->args.items[1]);
        ins("%s =%s copy %s", res, qt(e->type), b);
        ins("jmp %s", end);
        blk(end);
        if (qref(e->type)) { used_ar = true; ins("call $xr_ar(l %s)", res); }
        return res;
    }
    case E_UNARY: {
        if (e->op == TK_TILDE) { unsupported(e->pos, "bitwise operators"); return ""; }
        char *v = emit_expr(e->lhs);
        char *r = tmp();
        if (e->op == TK_NOT) ins("%s =w ceqw %s, 0", r, v);
        else if (e->op == TK_MINUS && e->type->kind == TY_INT) ins("%s =l call $xr_ineg(l %s)", r, v);
        else if (e->op == TK_MINUS) ins("%s =d neg %s", r, v);
        else return v;
        return r;
    }
    case E_BINARY: {
        if (tk_bitop(e->op) || e->op == TK_STARSTAR) { unsupported(e->pos, e->op == TK_STARSTAR ? "**" : "bitwise operators"); return ""; }
        if (e->op == TK_AND || e->op == TK_OR) {
            char *res = tmp(), *rhs_l = label("rhs"), *end = label("end"), *short_l = label("short");
            char *a = emit_expr(e->lhs);
            if (e->op == TK_AND) ins("jnz %s, %s, %s", a, rhs_l, short_l);
            else ins("jnz %s, %s, %s", a, short_l, rhs_l);
            blk(short_l);
            ins("%s =w copy %s", res, e->op == TK_AND ? "0" : "1");
            ins("jmp %s", end);
            blk(rhs_l);
            char *b = emit_expr(e->rhs);
            ins("%s =w copy %s", res, b);
            ins("jmp %s", end);
            blk(end);
            return res;
        }
        if (e->type->kind == TY_STRING) { /* concatenation */
            char *a = emit_borrowed(e->lhs), *b = emit_borrowed(e->rhs), *r = tmp();
            ins("%s =l call $xr_str_concat(l %s, l %s)", r, a, b);
            return r;
        }
        if ((e->op == TK_EQ || e->op == TK_NEQ) && is_str_like(e->lhs->type) && is_str_like(e->rhs->type)) {
            char *a = emit_borrowed(e->lhs), *b = emit_borrowed(e->rhs), *r = tmp();
            ins("%s =w call $xr_str_eq(l %s, l %s)", r, a, b);
            if (e->op == TK_NEQ) { char *n = tmp(); ins("%s =w ceqw %s, 0", n, r); return n; }
            return r;
        }
        char *a = emit_borrowed(e->lhs);
        char *b = emit_borrowed(e->rhs);
        Type *t = e->lhs->type;
        if (t->kind == TY_NULL || t->kind == TY_NULLABLE || t->kind == TY_OBJECT) t = ty_string; /* pointer comparison */
        switch (e->op) {
        case TK_PLUS: case TK_MINUS: case TK_STAR: case TK_PERCENT: {
            if (t->kind == TY_INT) return emit_int_arith(e->op, a, b);
            char *r = tmp();
            if (e->op == TK_PERCENT) ins("%s =d call $fmod(d %s, d %s)", r, a, b);
            else ins("%s =d %s %s, %s", r, e->op == TK_PLUS ? "add" : e->op == TK_MINUS ? "sub" : "mul", a, b);
            return r;
        }
        case TK_SLASH: {
            char *r = tmp();
            if (t->kind == TY_INT) {
                char *da = tmp(), *db = tmp();
                ins("%s =d sltof %s", da, a);
                ins("%s =d sltof %s", db, b);
                ins("%s =d div %s, %s", r, da, db);
            } else {
                ins("%s =d div %s, %s", r, a, b);
            }
            return r;
        }
        default: { /* comparisons */
            char *r = tmp();
            ins("%s =w %s %s, %s", r, cmp_op(e->op, t), a, b);
            return r;
        }
        }
    }
    case E_ASSIGN: {
        Expr *target = e->lhs;
        char *v;
        bool local = target->kind == E_IDENT && !is_global(target);
        Lval lv = {0};
        if (e->op != TK_ASSIGN && !local) lv = lv_capture(target);
        #define CUR() (local ? emit_read(target) : load_from(target->type, lv_ptr(target, lv)))
        if (e->op == TK_ASSIGN) {
            v = emit_owned(e->rhs);
        } else if (target->type->kind == TY_STRING) { /* s += t */
            char *cur = CUR();
            char *rhs = emit_borrowed(e->rhs);
            v = tmp();
            ins("%s =l call $xr_str_concat(l %s, l %s)", v, cur, rhs);
        } else {
            char *cur = CUR();
            char *rhs = emit_expr(e->rhs);
            if (target->type->kind == TY_INT) {
                v = emit_int_arith(e->op == TK_PLUS_ASSIGN ? TK_PLUS : e->op == TK_MINUS_ASSIGN ? TK_MINUS : TK_STAR, cur, rhs);
            } else {
                v = tmp();
                const char *op = e->op == TK_PLUS_ASSIGN ? "add" : e->op == TK_MINUS_ASSIGN ? "sub" : e->op == TK_STAR_ASSIGN ? "mul" : "div";
                ins("%s =d %s %s, %s", v, op, cur, rhs);
            }
        }
        if (e->op != TK_ASSIGN && !local) emit_write_at(target, v, lv_ptr(target, lv));
        else emit_write(target, v);
        return v;
        #undef CUR
    }
    case E_POSTFIX: {
        Expr *target = e->lhs;
        bool local = target->kind == E_IDENT && !is_global(target);
        Lval lv = local ? (Lval){0} : lv_capture(target);
        char *cur = local ? emit_read(target) : load_from(target->type, lv_ptr(target, lv));
        char *nv;
        if (target->type->kind == TY_INT) nv = checked_add_sub(cur, "1", e->op == TK_MINUSMINUS);
        else {
            nv = tmp();
            ins("%s =d %s %s, d_1", nv, e->op == TK_PLUSPLUS ? "add" : "sub", cur);
        }
        if (local) emit_write(target, nv);
        else emit_write_at(target, nv, lv_ptr(target, lv));
        return cur;
    }
    case E_CALL: {
        if (e->builtin.cfn) {
            const char *fn = e->builtin.cfn;
            if (strcmp(fn, "xs_to_number") == 0) {
                char *a = emit_expr(e->args.items[0]);
                char *r = tmp();
                ins("%s =d sltof %s", r, a);
                return r;
            }
            if (strcmp(fn, "xs_to_number_id") == 0) return emit_expr(e->args.items[0]);
            if (strcmp(fn, "xs_str_from_int") == 0 || strcmp(fn, "xs_str_from_num") == 0 || strcmp(fn, "xs_str_from_bool") == 0 ||
                strcmp(fn, "xs_retain_str") == 0 || strcmp(fn, "xs_str_to_number") == 0) {
                Expr *a = e->args.items[0];
                char *v = emit_borrowed(a), *r = tmp();
                ins("%s =%s call $%s(%s %s)", r, qt(e->type), xr_name(fn), qt(a->type), v);
                return r;
            }
            if (strcmp(fn, "xs_assert") == 0) {
                char *c = emit_expr(e->args.items[0]), *m = emit_borrowed(e->args.items[1]);
                ins("call $xr_assert(w %s, l %s)", c, m);
                return "";
            }
            unsupported(e->pos, "this built-in");
        }
        Func *f = e->func;
        if (!f) { unsupported(e->pos, "calling a function value"); return ""; } /* closures are C-backend only, like arrows */
        Buf args = {0};
        for (int i = 0; i < e->args.len; i++) {
            char *v = emit_borrowed(e->args.items[i]);
            buf_printf(&args, "%s%s %s", i ? ", " : "", qt(f->params[i].type), v);
        }
        if (f->ret->kind == TY_VOID) {
            ins("call $%s(%s)", f->cname, args.data ? args.data : "");
            return "";
        }
        char *r = tmp();
        ins("%s =%s call $%s(%s)", r, qt(f->ret), f->cname, args.data ? args.data : "");
        return r;
    }
    case E_INDEX: return emit_read(e);
    case E_MEMBER: {
        Expr *base = e->lhs;
        if (base->kind == E_IDENT && !base->cname && strcmp(base->str, "Math") == 0) {
            if (!e->is_call) {
                const char *c = e->str;
                return !strcmp(c, "PI") ? "d_3.141592653589793" : !strcmp(c, "E") ? "d_2.718281828459045" : !strcmp(c, "LN2") ? "d_0.6931471805599453"
                     : !strcmp(c, "LN10") ? "d_2.302585092994046" : !strcmp(c, "LOG2E") ? "d_1.4426950408889634" : !strcmp(c, "LOG10E") ? "d_0.4342944819032518"
                     : !strcmp(c, "SQRT2") ? "d_1.4142135623730951" : "d_0.7071067811865476";
            }
            const char *fn = e->str;
            if (!strcmp(fn, "imul") || !strcmp(fn, "clz32") || !strcmp(fn, "sign") || !strcmp(fn, "fround")) { unsupported(e->pos, xs_sprintf("Math.%s", fn)); return ""; }
            bool int_args = e->args.len > 0 && e->args.items[0]->type->kind == TY_INT;
            const char *cfn = NULL;
            if ((strcmp(fn, "min") == 0 || strcmp(fn, "max") == 0) && e->args.len != 2) { unsupported(e->pos, "Math.min/max with other than two arguments"); return ""; }
            if (strcmp(fn, "sqrt") == 0) cfn = "sqrt";
            else if (strcmp(fn, "abs") == 0) cfn = int_args ? "xr_iabs" : "fabs";
            else if (strcmp(fn, "min") == 0) cfn = int_args ? "xr_imin" : "xr_fmin";
            else if (strcmp(fn, "max") == 0) cfn = int_args ? "xr_imax" : "xr_fmax";
            else if (strcmp(fn, "round") == 0) cfn = "xr_round_int";
            else if (strcmp(fn, "floor") == 0) cfn = "xr_floor_int";
            else if (strcmp(fn, "ceil") == 0) cfn = "xr_ceil_int";
            else if (strcmp(fn, "trunc") == 0) cfn = "xr_trunc_int";
            else if (strcmp(fn, "random") == 0) cfn = "xr_random"; /* not libc random(), which returns a long */
            else if (strcmp(fn, "cbrt") == 0) cfn = "xr_cbrt"; /* V8's, as on the C path */
            else cfn = fn; /* pow sin cos tan log exp: libm */
            Buf args = {0};
            for (int i = 0; i < e->args.len; i++) {
                char *v = emit_expr(e->args.items[i]);
                buf_printf(&args, "%s%s %s", i ? ", " : "", qt(e->args.items[i]->type), v);
            }
            char *r = tmp();
            ins("%s =%s call $%s(%s)", r, qt(e->type), cfn, args.data ? args.data : "");
            return r;
        }
        if (base->kind == E_IDENT && !base->cname && strcmp(base->str, "console") == 0) {
            for (int i = 0; i < e->args.len; i++) {
                if (i) ins("call $xr_print_sep()");
                emit_print(e->args.items[i]);
            }
            ins("call $xr_print_nl()");
            return "";
        }
        if (base->kind == E_IDENT && !base->cname) { /* other namespaces: Number.isNaN / isInteger */
            const char *fn = e->builtin.cfn;
            if (fn && (strcmp(fn, "xs_is_nan") == 0 || strcmp(fn, "xs_is_integer") == 0 || strcmp(fn, "xs_is_finite") == 0 || strcmp(fn, "xs_is_safe_integer") == 0)) {
                char *v = emit_expr(e->args.items[0]), *r = tmp();
                ins("%s =w call $%s(d %s)", r, xr_name(fn), v);
                return r;
            }
            unsupported(e->pos, xs_sprintf("%s.%s", base->str, e->str));
            return "";
        }
        if (base->kind == E_NEW_ARRAY) {
            char *n = emit_expr(base->lhs);
            char *r = tmp();
            if (qref(e->type->elem)) { /* n copies of the same reference (borrowed; the array retains) */
                char *fill = emit_borrowed(e->args.items[0]);
                ins("%s =l call $xr_arr_ref_fill(l %s, w %d, l %s)", r, n, print_kind(e->type->elem), fill);
                return r;
            }
            char *fill = emit_expr(e->args.items[0]);
            ins("%s =l call $xr_arr_%s_new(l %s, %s %s)", r, arr_suffix(e->type), n, qt(e->type->elem), fill);
            return r;
        }
        if (base->type->kind == TY_ARRAY) {
            char *a = emit_borrowed(base);
            char *r = tmp();
            if (!e->is_call) { ins("%s =l call $xr_arr_len(l %s)", r, a); return r; }
            if (strcmp(e->builtin.cfn, "push") == 0) {
                Type *el = base->type->elem;
                char *v = qref(el) ? emit_owned(e->args.items[0]) : emit_expr(e->args.items[0]);
                ins("%s =l call $xr_arr_%s_push(l %s, %s %s)", r, arr_suffix(base->type), a, qt(el), v);
                return r;
            }
            if (strcmp(e->builtin.cfn, "join") == 0 && is_str_like(base->type->elem)) {
                char *sep = e->args.len ? emit_borrowed(e->args.items[0]) : "0";
                ins("%s =l call $xr_arr_join_str(l %s, l %s)", r, a, sep);
                return r;
            }
            unsupported(e->pos, xs_sprintf("array method '%s'", e->str));
            return "";
        }
        if (base->type->kind == TY_STRING) {
            char *sv = emit_borrowed(base);
            if (!e->is_call) { char *r = tmp(); ins("%s =l call $xr_str_len(l %s)", r, sv); return r; }
            Buf args = {0};
            buf_printf(&args, "l %s", sv);
            for (int i = 0; i < e->args.len; i++) {
                Expr *a = e->args.items[i];
                char *v = emit_borrowed(a);
                buf_printf(&args, ", %s %s", qt(a->type), v);
            }
            char *r = tmp();
            ins("%s =%s call $%s(%s)", r, qt(e->type), xr_name(e->builtin.cfn), args.data);
            return r;
        }
        if (base->type->kind == TY_OBJECT && !e->cls->builtin) {
            if (e->field >= 0) return load_from(e->type, emit_slot_ptr(e));
            Func *m = e->func;
            Buf args = {0};
            buf_printf(&args, "l %s", emit_borrowed(base));
            for (int i = 0; i < e->args.len; i++) {
                char *v = emit_borrowed(e->args.items[i]);
                buf_printf(&args, ", %s %s", qt(m->params[i].type), v);
            }
            if (m->ret->kind == TY_VOID) { ins("call $%s(%s)", m->cname, args.data); return ""; }
            char *r = tmp();
            ins("%s =%s call $%s(%s)", r, qt(m->ret), m->cname, args.data);
            return r;
        }
        unsupported(e->pos, "this member");
        return "";
    }
    case E_ARRAY_LIT: {
        if (qref(e->type->elem)) {
            char *r = tmp();
            ins("%s =l call $xr_arr_ref_new(l %d, w %d)", r, e->args.len, print_kind(e->type->elem));
            for (int i = 0; i < e->args.len; i++) {
                char *v = emit_owned(e->args.items[i]);
                char *n = tmp();
                ins("%s =l call $xr_arr_ref_push(l %s, l %s)", n, r, v);
            }
            return r;
        }
        char *r = tmp();
        ins("%s =l call $xr_arr_%s_new(l %d, %s %s)", r, arr_suffix(e->type), e->args.len, qt(e->type->elem),
            e->type->elem->kind == TY_NUMBER ? "d_0" : "0");
        for (int i = 0; i < e->args.len; i++) {
            char *v = emit_expr(e->args.items[i]);
            char *p = tmp();
            ins("%s =l call $xr_at_%s(l %s, l %d)", p, arr_suffix(e->type), r, i);
            store_to(e->type->elem, v, p);
        }
        return r;
    }
    case E_NEW_ARRAY: return "";
    }
    return "";
}

/* ---------- statements ---------- */

static void emit_stmt(Stmt *s);

static void emit_releases(QScope *s) {
    for (int i = s->n - 1; i >= 0; i--) ins("call $xr_release(l %%%s)", s->names[i]);
}

static void drain_if_needed(void) {
    if (used_ar) {
        ins("call $xr_ar_drain(l %%ar_mark)");
        used_ar = false;
    }
}

static void emit_block(Stmt *b, bool root) {
    qpush(root);
    for (int i = 0; i < b->stmts.len; i++) emit_stmt(b->stmts.items[i]);
    emit_releases(qscope);
    qpop();
}

static void emit_loop_body(Stmt *b, const char *brk, const char *cont) {
    qpush(false);
    qscope->kind = QS_LOOP_BODY;
    qscope->break_label = (char *)brk;
    qscope->continue_label = (char *)cont;
    for (int i = 0; i < b->stmts.len; i++) emit_stmt(b->stmts.items[i]);
    emit_releases(qscope);
    qpop();
}

static void emit_var(Stmt *s) {
    char *v = emit_owned(s->init);
    ins("%%%s =%s copy %s", s->cname, qt(s->type), v);
    qtrack(s->cname, s->type);
    qdbg_local(s->name, s->type, s->cname);
}

static int qdbg_point(Pos pos);
static void qdbg_hook(Pos pos) { if (xs_debug) ins("call $xr_dbg_hook(l %%dbgf, w %d)", qdbg_point(pos)); }
static void emit_stmt(Stmt *s) {
    used_ar = false;
    cur_pos = s->pos;
    if (s->kind != S_BLOCK) qdbg_hook(s->pos);
    switch (s->kind) {
    case S_VAR:
        emit_var(s);
        drain_if_needed();
        return;
    case S_EXPR: {
        char *v = emit_expr(s->expr);
        if (is_fresh(s->expr)) ins("call $xr_release(l %s)", v);
        drain_if_needed();
        return;
    }
    case S_IF: {
        char *then_l = label("then"), *else_l = label("else"), *end = label("endif");
        char *c = emit_expr(s->cond);
        drain_if_needed(); /* the condition's temporaries: the branches may drain nothing */
        ins("jnz %s, %s, %s", c, then_l, s->else_body ? else_l : end);
        blk(then_l);
        emit_block(s->body, false);
        ins("jmp %s", end);
        if (s->else_body) {
            blk(else_l);
            if (s->else_body->kind == S_BLOCK) emit_block(s->else_body, false);
            else emit_stmt(s->else_body);
            ins("jmp %s", end);
        }
        blk(end);
        drain_if_needed();
        return;
    }
    case S_WHILE: {
        char *top = label("loop"), *body = label("body"), *end = label("endloop");
        blk(top);
        char *c = emit_expr(s->cond);
        drain_if_needed(); /* every iteration, not after the loop */
        ins("jnz %s, %s, %s", c, body, end);
        blk(body);
        emit_loop_body(s->body, end, top);
        ins("jmp %s", top);
        blk(end);
        drain_if_needed();
        return;
    }
    case S_FOROF: {
        if (s->expr->type->kind != TY_ARRAY) unsupported(s->pos, "for...of over streams");
        Type *at = s->expr->type, *el = at->elem;
        qpush(false);
        char *arr = xs_sprintf("arr%d", ++tmp_id), *idx = xs_sprintf("i%d", ++tmp_id);
        char *a = emit_owned(s->expr);
        ins("%%%s =l copy %s", arr, a);
        qtrack(arr, at);
        drain_if_needed();
        ins("%%%s =l copy 0", idx);
        char *top = label("forof"), *body = label("body"), *cont = label("next"), *end = label("endfor");
        blk(top);
        char *n = tmp(), *c = tmp();
        ins("%s =l call $xr_arr_len(l %%%s)", n, arr);
        ins("%s =w csltl %%%s, %s", c, idx, n);
        ins("jnz %s, %s, %s", c, body, end);
        blk(body);
        qpush(false);
        qscope->kind = QS_LOOP_BODY;
        qscope->break_label = end;
        qscope->continue_label = cont;
        char *p = elem_ptr(at, xs_sprintf("%%%s", arr), xs_sprintf("%%%s", idx));
        char *v = load_from(el, p);
        if (qref(el)) { char *r = tmp(); ins("%s =l call $xr_retain(l %s)", r, v); v = r; }
        ins("%%%s =%s copy %s", s->cname, qt(el), v);
        qtrack(s->cname, el);
        qdbg_local(s->name, el, s->cname);
        for (int i = 0; i < s->body->stmts.len; i++) emit_stmt(s->body->stmts.items[i]);
        emit_releases(qscope);
        qpop();
        ins("jmp %s", cont);
        blk(cont);
        ins("%%%s =l add %%%s, 1", idx, idx);
        ins("jmp %s", top);
        blk(end);
        emit_releases(qscope);
        qpop();
        return;
    }
    case S_TRY: case S_THROW: unsupported(s->pos, "try/catch"); return;
    case S_BREAK:
    case S_CONTINUE: {
        bool brk = s->kind == S_BREAK;
        for (QScope *sc = qscope; sc; sc = sc->parent) {
            emit_releases(sc);
            if (sc->kind == QS_LOOP_BODY || (brk && sc->kind == QS_SWITCH) || sc->func_root) {
                ins("jmp %s", brk ? sc->break_label : sc->continue_label);
                break;
            }
        }
        blk(label("dead"));
        return;
    }
    case S_SWITCH: {
        Type *t = s->expr->type;
        char *subj = emit_borrowed(s->expr);
        char *end = label("endsw");
        char **labels = xs_alloc(sizeof(char *) * (size_t)(s->ncases + 1));
        for (int i = 0; i < s->ncases; i++) labels[i] = label("case");
        int def = -1;
        for (int i = 0; i < s->ncases; i++) if (!s->cases[i].value) def = i;
        for (int i = 0; i < s->ncases; i++) {
            if (!s->cases[i].value) continue;
            char *v = emit_borrowed(s->cases[i].value);
            char *c = tmp(), *next_l = label("next");
            if (is_str_like(t)) ins("%s =w call $xr_str_eq(l %s, l %s)", c, subj, v);
            else ins("%s =w %s %s, %s", c, cmp_op(TK_EQ, t), subj, v);
            ins("jnz %s, %s, %s", c, labels[i], next_l);
            blk(next_l);
        }
        ins("jmp %s", def >= 0 ? labels[def] : end);
        qpush(false);
        qscope->kind = QS_SWITCH;
        qscope->break_label = end;
        for (int i = 0; i < s->ncases; i++) {
            blk(labels[i]);
            emit_block(s->cases[i].body, false);
        }
        ins("jmp %s", end);
        qpop();
        blk(end);
        drain_if_needed();
        return;
    }
    case S_FOR: {
        qpush(false);
        if (s->for_init) {
            if (s->for_init->kind == S_VAR) emit_var(s->for_init);
            else emit_expr(s->for_init->expr);
            drain_if_needed();
        }
        char *top = label("for"), *body = label("body"), *end = label("endfor");
        blk(top);
        if (s->cond) {
            used_ar = false;
            char *c = emit_expr(s->cond);
            drain_if_needed(); /* every iteration */
            ins("jnz %s, %s, %s", c, body, end);
        } else {
            ins("jmp %s", body);
        }
        char *cont = label("next");
        blk(body);
        emit_loop_body(s->body, end, cont);
        ins("jmp %s", cont);
        blk(cont);
        used_ar = false;
        if (s->for_update) emit_expr(s->for_update);
        drain_if_needed();
        ins("jmp %s", top);
        blk(end);
        emit_releases(qscope);
        qpop();
        return;
    }
    case S_RETURN: {
        char *v = s->expr ? emit_owned(s->expr) : NULL;
        for (QScope *sc = qscope; sc; sc = sc->parent) {
            emit_releases(sc);
            if (sc->func_root) break;
        }
        drain_if_needed();
        qdbg_leave();
        if (cur_func && cur_func->is_ctor) ins("ret %%self");
        else if (v) ins("ret %s", v);
        else ins("ret");
        blk(label("dead"));
        return;
    }
    case S_BLOCK:
        emit_block(s, false);
        return;
    }
}

static int qdbg_point(Pos pos) {
    Module *m = cur_func && cur_func->mod ? cur_func->mod : cur_mod;
    buf_printf(&qdbg_points, "%sl $xs_dbgfile_%d, w %d, w %d", qdbg_n ? ", " : "", m ? m->id : 0, pos.line, pos.col);
    return qdbg_n++;
}
static void qdbg_cell_store(int k, Type *t, const char *v) {
    char *c = tmp();
    ins("%s =l add %%dbgc, %d", c, 8 * k);
    const char *q = qt(t);
    ins("store%s %s, %s", q[0] == 'd' ? "d" : q[0] == 'w' ? "w" : "l", v, c);
}
static void qdbg_local(const char *name, Type *t, const char *cname) {
    if (!xs_debug || !name || !cur_func_has_frame) return;
    qv = realloc(qv, sizeof(QVar) * (size_t)(nqv + 1));
    qv[nqv] = (QVar){name, t, cname, qdbg_n, -1, qscope, false};
    int k = nqv++;
    qdbg_cell_store(k, t, xs_sprintf("%%%s", cname));
    char *c = tmp();
    ins("%s =l add %%dbgc, %d", c, 8 * k);
    ins("call $xr_dbg_declare(l %%dbgf, w %d, l %s)", k, c);
}
static void qdbg_global(const char *name, Type *t, const char *cname) {
    if (!xs_debug || !name) return;
    qv = realloc(qv, sizeof(QVar) * (size_t)(nqv + 1));
    qv[nqv] = (QVar){name, t, cname, qdbg_n, -1, qscope, true};
    ins("call $xr_dbg_declare(l %%dbgf, w %d, l $%s)", nqv++, cname);
}
static void qdbg_store(const char *cname, Type *t) {
    if (!xs_debug) return;
    for (int k = nqv - 1; k >= 0; k--)
        if (!qv[k].global && strcmp(qv[k].cname, cname) == 0) { qdbg_cell_store(k, t, xs_sprintf("%%%s", cname)); return; }
}
static void qdbg_scope_end(void *sc) {
    if (!xs_debug) return;
    for (int k = 0; k < nqv; k++) if (qv[k].scope == sc && qv[k].to < 0) qv[k].to = qdbg_n - 1;
}
static void qdbg_leave(void) { if (xs_debug) ins("call $xr_dbg_leave(l %%dbgf)"); }
/* the frame's prologue at DBG_MARK, and its tables after the function */
static void qdbg_finish(Buf *b, size_t from, const char *name) {
    if (!xs_debug) return;
    int id = qdbg_fn++;
    int n = nqv;
    Buf pro = {0};
    buf_printf(&pro, "\t%%dbgf =l alloc8 128\n\t%%dbgs =l alloc8 %d\n\t%%dbgc =l alloc8 %d\n", 8 * (n ? n : 1), 8 * (n ? n : 1));
    buf_printf(&pro, "\tcall $xr_dbg_enter(l %%dbgf, l $xs_dbgn_%d, l $xs_dbgv_%d, l %%dbgs, w %d)\n", id, id, n);
    char *m = strstr(b->data + from, DBG_MARK);
    if (m) {
        size_t pre = (size_t)(m - b->data), ml = strlen(DBG_MARK);
        Buf nb = {0};
        buf_printf(&nb, "%.*s", (int)pre, b->data);
        buf_puts(&nb, pro.data);
        buf_puts(&nb, m + ml);
        free(b->data);
        *b = nb;
    }
    free(pro.data);
    buf_printf(b, "data $xs_dbgn_%d = { ", id);
    qdbg_bytes(b, name);
    buf_puts(b, " }\n");
    for (int k = 0; k < n; k++) {
        buf_printf(b, "data $xs_dbgvn_%d_%d = { ", id, k);
        qdbg_bytes(b, qv[k].name);
        buf_puts(b, " }\n");
    }
    buf_printf(b, "data $xs_dbgv_%d = { ", id);
    if (!n) buf_puts(b, "l 0");
    for (int k = 0; k < n; k++) {
        int to = qv[k].to < 0 ? qdbg_n - 1 : qv[k].to;
        buf_printf(b, "%sl $xs_dbgvn_%d_%d, l %s, w %d, w %d, w 3, w 0", k ? ", " : "", id, k, qdbg_type(qv[k].t), qv[k].from, to);
    }
    buf_puts(b, " }\n\n");
    nqv = 0;
}
static void qdbg_begin(Func *f) {
    nqv = 0;
    cur_func_has_frame = xs_debug;
    if (!xs_debug) return;
    buf_puts(out, DBG_MARK);
    if (f && f->cls) qdbg_local("this", ty_object_of(f->cls), "self");
    for (int i = 0; f && i < f->nparams; i++) qdbg_local(f->params[i].name, f->params[i].type, f->params[i].cname);
    for (int k = 0; k < nqv; k++) { qv[k].scope = NULL; }
}
static const char *qdbg_fname(Func *f) {
    if (!f) return "(top level)";
    if (f->is_ctor && f->cls) return xs_sprintf("new %s", f->cls->name);
    if (f->cls) return xs_sprintf("%s.%s", f->cls->name, f->name);
    return f->name;
}

static void emit_func(Func *f) {
    cur_func = f;
    tmp_id = 0;
    label_id = 0;
    buf_printf(out, "export function %s%s$%s(", f->ret->kind == TY_VOID ? "" : qt(f->ret), f->ret->kind == TY_VOID ? "" : " ", f->cname);
    bool self = f->cls != NULL;
    if (self) buf_puts(out, "l %self");
    for (int i = 0; i < f->nparams; i++) buf_printf(out, "%s%s %%%s", i || self ? ", " : "", qt(f->params[i].type), f->params[i].cname);
    buf_puts(out, ") {\n@start\n");
    size_t fstart = out->len;
    tpl_off = 0;
    buf_puts(out, TPL_MARK);
    ins("%%ar_mark =l call $xr_ar_mark()");
    qdbg_begin(f);
    emit_block(f->body, true);
    if (f->ret->kind == TY_VOID) { qdbg_leave(); ins("ret"); }
    else ins("hlt"); /* the checker guarantees a return on every path */
    buf_puts(out, "}\n\n");
    tpl_finish(out, fstart);
    qdbg_finish(out, fstart, qdbg_fname(f));
    cur_func = NULL;
}

/* field kinds of the descriptor read by xr_desc_free / xr_desc_print (runtime/xisto_rt.c) */
static int field_kind(Field *f) {
    Type *t = f->type;
    switch (t->kind) {
    case TY_NUMBER: return 0;
    case TY_INT: return 1;
    case TY_ENUM: return t->en->is_string ? 3 : 1;
    case TY_BOOLEAN: return 2;
    case TY_STRING: return 3;
    case TY_NULLABLE: return f->optional ? 5 : t->elem->kind == TY_STRING ? 7 : 6;
    default: return 4;
    }
}

static void emit_class_qbe(Class *c) {
    /* descriptor: name, field count, interface flag, then (name, offset, kind) per field */
    buf_printf(out, "data $dn_%s = { ", c->cname);
    for (const unsigned char *k = (const unsigned char *)c->name; *k; k++) buf_printf(out, "b %d, ", *k);
    buf_puts(out, "b 0 }\n");
    for (int i = 0; i < c->nfields; i++) {
        buf_printf(out, "data $df_%s_%d = { ", c->cname, i);
        for (const unsigned char *k = (const unsigned char *)c->fields[i].name; *k; k++) buf_printf(out, "b %d, ", *k);
        buf_puts(out, "b 0 }\n");
    }
    buf_printf(out, "export data $desc_%s = { l $dn_%s, l %d, l %d", c->cname, c->cname, c->nfields, c->is_interface ? 1 : 0);
    for (int i = 0; i < c->nfields; i++) buf_printf(out, ", l $df_%s_%d, l %d, l %d", c->cname, i, field_off(i), field_kind(&c->fields[i]));
    buf_puts(out, " }\n\n");

    /* constructor: allocate, run field initializers, then the constructor body */
    Func *ctor = c->ctor;
    tmp_id = 0;
    label_id = 0;
    cur_func = ctor;
    buf_printf(out, "export function l $%s(", ctor ? ctor->cname : xs_sprintf("%s_new", c->cname));
    for (int i = 0; ctor && i < ctor->nparams; i++) buf_printf(out, "%s%s %%%s", i ? ", " : "", qt(ctor->params[i].type), ctor->params[i].cname);
    buf_puts(out, ") {\n@start\n");
    size_t cstart = out->len;
    tpl_off = 0;
    buf_puts(out, TPL_MARK);
    ins("%%ar_mark =l call $xr_ar_mark()");
    ins("%%self =l call $xr_obj_new(l %d, l $desc_%s)", obj_size(c), c->cname);
    for (int i = 0; i < c->nfields; i++) {
        Field *f = &c->fields[i];
        if (!f->init) continue;
        used_ar = false;
        char *v = qref(f->type) ? emit_owned(f->init) : emit_expr(f->init);
        char *p = tmp();
        ins("%s =l add %%self, %d", p, field_off(i));
        store_to(f->type, v, p);
        drain_if_needed();
    }
    if (ctor) {
        qdbg_begin(ctor);
        emit_block(ctor->body, true);
        qdbg_leave();
    }
    ins("ret %%self");
    buf_puts(out, "}\n\n");
    tpl_finish(out, cstart);
    if (ctor) qdbg_finish(out, cstart, qdbg_fname(ctor));
    cur_func = NULL;
    for (int k = 0; k < c->nmethods; k++) if (c->methods[k]->used) emit_func(c->methods[k]);
}

static void emit_main_unit(Program *p) {
    buf_puts(out, "# generated by xisto (qbe backend)\n");
    for (int i = 0; i < p->top.len; i++) {
        Stmt *s = p->top.items[i];
        if (s->kind != S_VAR) continue;
        buf_printf(out, "export data $%s = { %s 0 }\n", s->cname, s->type->kind == TY_NUMBER ? "d" : s->type->kind == TY_BOOLEAN ? "w" : "l");
    }
    Buf *head = out;
    Buf body = {0};
    out = &body;
    tmp_id = 0;
    label_id = 0;
    tpl_off = 0;
    cur_func = NULL;
    qdbg_begin(NULL);
    qpush(true);
    for (int i = 0; i < p->top.len; i++) {
        Stmt *s = p->top.items[i];
        cur_mod = s->mod;
        cur_pos = s->pos;
        if (s->kind == S_VAR) {
            used_ar = false;
            qdbg_hook(s->pos);
            char *v = emit_owned(s->init);
            store_to(s->type, v, xs_sprintf("$%s", s->cname));
            qdbg_global(s->name, s->type, s->cname);
            drain_if_needed();
        } else {
            emit_stmt(s);
        }
    }
    emit_releases(qscope);
    qpop();
    for (int i = 0; i < p->top.len; i++) {
        Stmt *s = p->top.items[i];
        if (s->kind == S_VAR && qref(s->type)) {
            char *v = tmp();
            ins("%s =l loadl $%s", v, s->cname);
            ins("call $xr_release(l %s)", v);
        }
    }
    qdbg_leave();
    ins("call $xr_exit()");
    ins("ret 0");
    buf_puts(out, "}\n");
    /* now every unit has said which literals it uses: their bytes, their string objects, and main's prologue */
    out = head;
    for (int i = 0; i < p->nlits; i++) {
        if (!lit_used[i]) continue;
        buf_printf(out, "export data %s = { ", lit_name(p, i));
        for (const unsigned char *c = (const unsigned char *)p->lits[i]; *c; c++) { /* NUL is C0 80 here (xs_lit_len) */
            if (c[0] == 0xC0 && c[1] == 0x80) { buf_puts(out, "b 0, "); c++; } else buf_printf(out, "b %d, ", *c);
        }
        buf_puts(out, "b 0 }\n");
        buf_printf(out, "export data %s = { l 0 }\n", lit_obj(p, i));
    }
    buf_puts(out, "\nexport function w $main() {\n@start\n");
    ins("call $xr_init()");
    for (int i = 0; i < p->nlits; i++) {
        if (!lit_used[i]) continue;
        ins("%%lit%d =l call $xr_str_immortal(l %s, l %zu)", i, lit_name(p, i), xs_lit_len(p->lits[i]));
        ins("storel %%lit%d, %s", i, lit_obj(p, i));
    }
    ins("%%ar_mark =l call $xr_ar_mark()");
    if (tpl_off) ins("%%tplbuf =l alloc8 %d", tpl_off);
    if (xs_debug) ins("call $xr_dbg_init()");
    size_t mstart = out->len;
    buf_puts(out, body.data ? body.data : "");
    free(body.data);
    if (xs_debug) {
        qdbg_finish(out, mstart, "(top level)");
        /* the points of every unit: file names, then the table the agent reads */
        for (int i = 0; i < p->nmodules; i++) {
            buf_printf(out, "data $xs_dbgfile_%d = { ", p->modules[i]->id);
            qdbg_bytes(out, p->modules[i]->path ? p->modules[i]->path : "");
            buf_puts(out, " }\n");
        }
        buf_printf(out, "export data $xs_dbg_points = { %s }\n", qdbg_n ? qdbg_points.data : "l 0, w 0, w 0");
        buf_printf(out, "export data $xs_dbg_npoints = { w %d }\n", qdbg_n);
        qdbg_points.len = 0;
        qdbg_n = 0;
        qdbg_fn = 0;
    }
}

static void check_supported(Program *p) {
    for (int i = 0; i < p->nfuncs; i++)
        if (p->funcs[i]->used && p->funcs[i]->is_gen) unsupported(p->funcs[i]->pos, "generators");
    for (int i = 0; i < p->nclasses; i++)
        if (p->classes[i]->used && (p->classes[i]->base || p->classes[i]->nsubs)) unsupported(p->classes[i]->pos, "inheritance");
    if (p->uses_net) unsupported((Pos){1, 1}, "networking");
    if (p->uses_sqlite) unsupported((Pos){1, 1}, "node:sqlite");
    if (p->uses_regex) unsupported((Pos){1, 1}, "RegExp");
}

/* One unit per module that has reachable functions, plus "main" (literals, globals, top-level code). */
Unit *emit_program_qbe_units(Program *p, int *nunits) {
    prog = p;
    check_supported(p);
    lit_used = xs_alloc(sizeof(bool) * (size_t)(p->nlits ? p->nlits : 1));
    Unit *units = xs_alloc(sizeof(Unit) * (size_t)(p->nmodules + 1));
    int n = 0;
    for (int mi = 0; mi < p->nmodules; mi++) {
        Module *m = p->modules[mi];
        if (m->builtin) continue;
        Buf b = {0};
        out = &b;
        qsites_reset(xs_sprintf("m%d", m->id));
        cur_mod = m;
        bool any = false;
        for (int i = 0; i < p->nclasses; i++) {
            Class *c = p->classes[i];
            if (c->mod != m || !c->used) continue;
            if (!any) buf_printf(out, "# module %s\n", m->file);
            any = true;
            emit_class_qbe(c);
        }
        for (int i = 0; i < p->nfuncs; i++) {
            Func *f = p->funcs[i];
            if (f->mod != m || !f->used) continue;
            if (!any) buf_printf(out, "# module %s\n", m->file);
            any = true;
            emit_func(f);
        }
        if (!any) { free(b.data); continue; }
        qsites_flush(&b);
        units[n].name = xs_sprintf("m%d", m->id);
        units[n].text = b;
        n++;
    }
    Buf b = {0};
    out = &b;
    qsites_reset("main");
    emit_main_unit(p);
    qsites_flush(&b);
    units[n].name = "main";
    units[n].text = b;
    n++;
    *nunits = n;
    return units;
}

/* Whole program in one buffer (for `emit`): the units concatenated. */
void emit_program_qbe(Program *p, Buf *o) {
    int n;
    Unit *units = emit_program_qbe_units(p, &n);
    for (int i = 0; i < n; i++) buf_puts(o, units[i].text.data);
}
