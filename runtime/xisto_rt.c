/* The runtime as a linkable object for the QBE backend: exported wrappers over the static header. */
#include "xisto_rt.h"
#ifdef XS_DEBUG /* build/xisto_rt_dbg.o: the runtime of --debug --backend=qbe programs, with the debugger agent */
#include "xisto_dbg_json.h"
#include "xisto_dbg.h"
#endif

void xr_init(void) { xs_init(0, NULL); }
void xr_exit(void) { xs_exit(); }
void xr_panic_overflow(void) { xs_panic("integer overflow"); }
int64_t xr_imul(int64_t a, int64_t b) { return xs_imul(a, b); }
int64_t xr_imod(int64_t a, int64_t b) { return xs_imod(a, b); }
int64_t xr_ineg(int64_t a) { return xs_ineg(a); }
int64_t xr_iabs(int64_t a) { return xs_iabs(a); }
int64_t xr_imin(int64_t a, int64_t b) { return xs_imin(a, b); }
int64_t xr_imax(int64_t a, int64_t b) { return xs_imax(a, b); }
int64_t xr_floor_int(double x) { return xs_floor_int(x); }
int64_t xr_ceil_int(double x) { return xs_ceil_int(x); }
int64_t xr_trunc_int(double x) { return xs_trunc_int(x); }
int64_t xr_round_int(double x) { return xs_round_int(x); }
void xr_print_num(double v) { xs_print_num(v); }
void xr_print_int(int64_t v) { xs_print_int(v); }
void xr_print_bool(int32_t b) { xs_print_bool(b != 0); }
void xr_print_sep(void) { xs_print_sep(); }
void xr_print_nl(void) { xs_print_nl(); }
void *xr_retain(void *p) { return xs_retain_(p); }
void xr_release(void *p) { xs_release_(p); }
int64_t xr_ar_mark(void) { return (int64_t)xs_ar_mark(); }
void xr_ar(void *p) { xs_ar_(p); }
void xr_ar_drain(int64_t mark) { xs_ar_drain((size_t)mark); }
xs_arr_num *xr_arr_num_new(int64_t n, double fill) { return xs_arr_num_new(n, fill); }
xs_arr_int *xr_arr_int_new(int64_t n, int64_t fill) { return xs_arr_int_new(n, fill); }
xs_arr_bool *xr_arr_bool_new(int64_t n, int32_t fill) { return xs_arr_bool_new(n, fill != 0); }
double *xr_at_num(xs_arr_num *a, int64_t i) { return xs_at_num(a, i); }
int64_t *xr_at_int(xs_arr_int *a, int64_t i) { return xs_at_int(a, i); }
bool *xr_at_bool(xs_arr_bool *a, int64_t i) { return xs_at_bool(a, i); }
int64_t xr_arr_len(void *a) { return (int64_t)((xs_arr_num *)a)->len; } /* len sits at the same offset in every array */
void xr_print_arr_num(xs_arr_num *a) { xs_print_arr_num(a); }
void xr_print_arr_int(xs_arr_int *a) { xs_print_arr_int(a); }
void xr_print_arr_bool(xs_arr_bool *a) { xs_print_arr_bool(a); }
void xr_print_bytes(const char *p, int64_t n) { fwrite(p, 1, (size_t)n, stdout); }

/* ---- strings and objects for the QBE backend ---- */
void *xr_str_immortal(const char *p, int64_t n) { return xs_str_immortal(p, (size_t)n); }
void *xr_str_concat(xs_str *a, xs_str *b) { return xs_str_concat(a, b); }
int32_t xr_str_eq(xs_str *a, xs_str *b) { return xs_str_eq(a, b); }
int64_t xr_str_len(xs_str *s) { return xs_str_len(s); }
void *xr_str_from_int(int64_t v) { return xs_str_from_int(v); }
void *xr_str_from_num(double v) { return xs_str_from_num(v); }
void *xr_str_from_bool(int32_t b) { return xs_str_from_bool(b != 0); }
void *xr_retain_str(xs_str *s) { return xs_retain(s); }
double xr_str_to_number(xs_str *s) { return xs_str_to_number(s); }
void xr_assert(int32_t c, xs_str *m) { xs_assert(c != 0, m); }
void xr_print_str(xs_str *s) { xs_print_str(s); }
void xr_print_obj(void *p) { xs_print_obj(p); }
void xr_print_obj_or_null(void *p) { xs_print_obj_or_null(p); }
void xr_print_str_or_null(xs_str *s) { xs_print_str_or_null(s); }
void *xr_tpl_arr(int32_t n, xs_str **parts) {
    int64_t total = 0;
    for (int i = 0; i < n; i++) total += parts[i]->len;
    xs_str *r = xs_str_alloc(total);
    char *p = r->data;
    for (int i = 0; i < n; i++) { memcpy(p, parts[i]->data, (size_t)parts[i]->len); p += parts[i]->len; }
    *p = 0;
    return r;
}
int32_t xr_str_starts_with(xs_str *s, xs_str *p) { return xs_str_starts_with(s, p); }
int32_t xr_str_ends_with(xs_str *s, xs_str *p) { return xs_str_ends_with(s, p); }
int32_t xr_str_includes(xs_str *s, xs_str *p) { return xs_str_includes(s, p); }
int64_t xr_str_index_of(xs_str *s, xs_str *p, int64_t from) { return xs_str_index_of(s, p, from); }
int64_t xr_str_last_index_of(xs_str *s, xs_str *p) { return xs_str_last_index_of(s, p); }
void *xr_str_slice(xs_str *s, int64_t a, int64_t b) { return xs_str_slice(s, a, b); }
void *xr_str_substring(xs_str *s, int64_t a, int64_t b) { return xs_str_substring(s, a, b); }
void *xr_str_trim(xs_str *s) { return xs_str_trim(s); }
void *xr_str_trim_start(xs_str *s) { return xs_str_trim_start(s); }
void *xr_str_trim_end(xs_str *s) { return xs_str_trim_end(s); }
void *xr_str_upper(xs_str *s) { return xs_str_upper(s); }
void *xr_str_lower(xs_str *s) { return xs_str_lower(s); }
void *xr_str_replace(xs_str *s, xs_str *a, xs_str *b) { return xs_str_replace(s, a, b); }
void *xr_str_replace_all(xs_str *s, xs_str *a, xs_str *b) { return xs_str_replace_all(s, a, b); }
void *xr_str_pad_start(xs_str *s, int64_t n, xs_str *f) { return xs_str_pad_start(s, n, f); }
void *xr_str_pad_end(xs_str *s, int64_t n, xs_str *f) { return xs_str_pad_end(s, n, f); }
void *xr_str_repeat(xs_str *s, int64_t n) { return xs_str_repeat(s, n); }
void *xr_str_char_at(xs_str *s, int64_t i) { return xs_str_char_at(s, i); }
double xr_str_code_at(xs_str *s, int64_t i) { return xs_str_code_at(s, i); }

/* Objects built by QBE code: xs_hdr, then a descriptor pointer, then 8-byte field slots.
   desc = { name, nfields, is_interface, (field name, offset, kind)... }
   kinds: 0 number, 1 int, 2 boolean, 3 string, 4 other reference, 5 optional reference, 6 nullable reference, 7 nullable string */
typedef struct { const char *name; int64_t nfields, is_interface; struct { const char *name; int64_t off, kind; } f[]; } xr_desc;
static void xr_desc_free(void *p) {
    xr_desc *d = *(xr_desc **)((char *)p + sizeof(xs_hdr));
    for (int64_t i = 0; i < d->nfields; i++)
        if (d->f[i].kind >= 3) xs_release_(*(void **)((char *)p + d->f[i].off));
    xs_obj_free(p);
}
static void xr_desc_print(void *p) {
    xr_desc *d = *(xr_desc **)((char *)p + sizeof(xs_hdr));
    if (d->is_interface) fputs("{", stdout); else printf("%s {", d->name);
    bool first = true;
    for (int64_t i = 0; i < d->nfields; i++) {
        char *slot = (char *)p + d->f[i].off;
        void *ref = *(void **)slot;
        if (d->f[i].kind == 5 && !ref) continue; /* optional and absent */
        printf(first ? " %s: " : ", %s: ", d->f[i].name);
        first = false;
        switch (d->f[i].kind) {
        case 0: xs_print_num(*(double *)slot); break;
        case 1: xs_print_int(*(int64_t *)slot); break;
        case 2: xs_print_bool(*(uint8_t *)slot != 0); break;
        case 3: xs_print_str_quoted(ref); break;
        case 7: if (ref) xs_print_str_quoted(ref); else fputs("null", stdout); break;
        default: if (ref) xs_print_obj(ref); else fputs("null", stdout); break;
        }
    }
    fputs(first ? "}" : " }", stdout);
}
void *xr_obj_new(int64_t size, void *desc) {
    void *o = xs_obj_alloc((size_t)size, xr_desc_free, xr_desc_print);
    *(void **)((char *)o + sizeof(xs_hdr)) = desc;
    return o;
}
void xr_assign_slot(void **slot, void *v) { void *old = *slot; *slot = v; xs_release_(old); }

/* reference arrays and the rest of the array surface for QBE */
void *xr_arr_ref_new(int64_t cap, int32_t strings) { return xs_arr_ref_new((size_t)cap, strings ? xs_print_str_quoted_ : xs_print_obj); }
int64_t xr_arr_ref_push(xs_arr_ref *a, void *v) { return xs_arr_ref_push(a, v); }
void *xr_arr_ref_fill(int64_t n, int32_t strings, void *v) { return xs_arr_ref_fill(xs_arr_ref_new(0, strings ? xs_print_str_quoted_ : xs_print_obj), n, v); }
void **xr_at_ref(xs_arr_ref *a, int64_t i) { return xs_at_ref(a, i); }
int64_t xr_arr_num_push(xs_arr_num *a, double v) { return xs_arr_num_push(a, v); }
int64_t xr_arr_int_push(xs_arr_int *a, int64_t v) { return xs_arr_int_push(a, v); }
int64_t xr_arr_bool_push(xs_arr_bool *a, int32_t v) { return xs_arr_bool_push(a, v != 0); }
void *xr_arr_join_str(xs_arr_ref *a, xs_str *sep) { return xs_arr_join_str(a, sep); }
void *xr_str_split(xs_str *s, xs_str *sep) { return xs_str_split(s, sep); }
int32_t xr_is_nan(double x) { return xs_is_nan(x); }
int32_t xr_is_integer(double x) { return xs_is_integer(x); }
double xr_fmin(double a, double b) { return xs_fmin(a, b); }
double xr_cbrt(double x) { return xs_cbrt(x); }
double xr_fmax(double a, double b) { return xs_fmax(a, b); }
int32_t xr_is_finite(double x) { return xs_is_finite(x); }
int32_t xr_is_safe_integer(double x) { return xs_is_safe_integer(x); }

double xr_random(void) { return xs_random(); }

#ifdef XS_DEBUG
/* ---- debugger entry points for QBE code (docs/debugger.md, F6). QBE locals are SSA values: the emitter keeps
   a shadow cell per local, stored at every assignment, so the debugger reads them (and does not edit them). ---- */
void xr_dbg_init(void) { xs_dbg_init(); }
void xr_dbg_enter(xs_dbg_frame *f, const char *name, const xs_dbg_var *vars, void **slots, int32_t n) {
    if (!xs_dbg_tid) xs_dbg_register();
    memset(f, 0, sizeof *f);
    f->parent = xs_dbg_top;
    f->name = name;
    f->depth = xs_dbg_top ? xs_dbg_top->depth + 1 : 0;
    f->last = -1;
    f->serial = ++xs_dbg_serial;
    f->vars = vars;
    f->slots = slots;
    f->nvars = n;
    for (int i = 0; i < n; i++) slots[i] = NULL;
    xs_dbg_top = f;
}
void xr_dbg_leave(xs_dbg_frame *f) { xs_dbg_top = f->parent; }
void xr_dbg_hook(xs_dbg_frame *f, int32_t id) {
    xs_dbg_top = f;
    f->point = id;
    if (__builtin_expect(xs_dbg_armed, 0) && (xs_dbg_armed > 1 || xs_dbg_bpv[id])) xs_dbg_hit(f);
}
void xr_dbg_declare(xs_dbg_frame *f, int32_t k, void *addr) { f->slots[k] = addr; }

/* how QBE values look: by kind, read-only */
static void xr_dbgr_int(void *p) { xs_print_int(*(int64_t *)p); }
static void xr_dbgr_int_null(void *p) { int64_t v = *(int64_t *)p; if (v == XS_NULL_INT) fputs("null", stdout); else xs_print_int(v); }
static void xr_dbgr_num(void *p) { xs_print_num(*(double *)p); }
static void xr_dbgr_num_null(void *p) { double v = *(double *)p; if (xs_isnull_num(v)) fputs("null", stdout); else xs_print_num(v); }
static void xr_dbgr_bool(void *p) { xs_print_bool(*(uint8_t *)p != 0); } /* the low byte: cells take storew, globals storeb */
static void xr_dbgr_bool_null(void *p) { uint8_t v = *(uint8_t *)p; if (v == (uint8_t)XS_NULL_BOOL) fputs("null", stdout); else xs_print_bool(v != 0); }
static void xr_dbgr_str(void *p) { void *v = *(void **)p; if (v) xs_print_str_quoted_(v); else fputs("null", stdout); }
static void xr_dbgr_ref(void *p) { xs_print_obj_or_null(*(void **)p); }
const xs_dbg_type xr_dbg_t_int = {"int", xr_dbgr_int, NULL, NULL, NULL};
const xs_dbg_type xr_dbg_t_int_null = {"int | null", xr_dbgr_int_null, NULL, NULL, NULL};
const xs_dbg_type xr_dbg_t_num = {"number", xr_dbgr_num, NULL, NULL, NULL};
const xs_dbg_type xr_dbg_t_num_null = {"number | null", xr_dbgr_num_null, NULL, NULL, NULL};
const xs_dbg_type xr_dbg_t_bool = {"boolean", xr_dbgr_bool, NULL, NULL, NULL};
const xs_dbg_type xr_dbg_t_bool_null = {"boolean | null", xr_dbgr_bool_null, NULL, NULL, NULL};
const xs_dbg_type xr_dbg_t_str = {"string", xr_dbgr_str, NULL, NULL, NULL};
const xs_dbg_type xr_dbg_t_ref = {"object", xr_dbgr_ref, NULL, NULL, NULL};
#endif
