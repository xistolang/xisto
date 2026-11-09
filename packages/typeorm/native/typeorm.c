#include <time.h>
/* typeorm for xisto: the JSON work the ORM does on query results and parameters, in C because xisto code holds a
   JSON value (`object`) as text it cannot walk. Each function parses one document at a time (the runtime's JSON
   arena is per parse) and returns new text.

   - remap: driver rows ({"COLUMN": value}) into entity rows ({"property": value}), each value turned into what the
     property's type reads (Oracle NUMBER into a string property, a numeric string into a number, 0/1 into a
     boolean), matching the column name exactly first and then without case (Oracle folds unquoted names).
   - get/at/len/kind/keys: one field, one element, a length, a kind, the keys: what the TypeScript side needs to
     walk a `where`, a `set` or a row. */

static void xn_typeorm_put(xs_sb *b, xs_jv *v) { xs_jv_write(b, v); }

/* a `type: "date"` column (kind "...:day"): the local calendar day of the instant the driver read, as TypeORM's
   mixedDateToDateString does; "YYYY-MM-DD" for a string property, new Date("YYYY-MM-DD") (UTC midnight) for a Date */
static bool xn_typeorm_day(xs_sb *b, xs_jv *v, const char *kind) {
    size_t kl = strlen(kind);
    if (kl < 4 || strcmp(kind + kl - 4, ":day") != 0) return false;
    if (v->kind != XJ_STR || v->slen < 10) { xn_typeorm_put(b, v); return true; }
    char tmp[40];
    size_t n = v->slen < sizeof tmp - 1 ? v->slen : sizeof tmp - 1;
    memcpy(tmp, v->str, n);
    tmp[n] = 0;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    struct tm lt = {0};
    if (sscanf(tmp, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) == 6) { /* an instant (UTC, from the driver's Date) */
        struct tm ut = {0};
        ut.tm_year = y - 1900; ut.tm_mon = mo - 1; ut.tm_mday = d; ut.tm_hour = h; ut.tm_min = mi; ut.tm_sec = se;
        time_t t = timegm(&ut);
        localtime_r(&t, &lt);
    } else if (sscanf(tmp, "%d-%d-%d", &y, &mo, &d) == 3) {
        lt.tm_year = y - 1900; lt.tm_mon = mo - 1; lt.tm_mday = d;
    } else { xn_typeorm_put(b, v); return true; }
    char out[40];
    if (strncmp(kind, "date", 4) == 0) { /* TypeORM hands the property "YYYY-MM-DD": as a Date, new Date of it (UTC midnight) */
        snprintf(out, sizeof out, "%04d-%02d-%02dT00:00:00.000Z", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    } else {
        snprintf(out, sizeof out, "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    }
    xs_sb_jstr(b, out, strlen(out));
    return true;
}

/* the value as the property's kind reads it: string, int, number, boolean, date or other (as it is) */
static void xn_typeorm_coerce(xs_sb *b, xs_jv *v, const char *kind) {
    if (v->kind == XJ_NULL) { xs_sb_puts(b, "null"); return; }
    if (xn_typeorm_day(b, v, kind)) return;
    if (strcmp(kind, "string") == 0) {
        if (v->kind == XJ_NUM) { char buf[32]; xs_num_to_str(v->num, buf); if (v->num == 0) { buf[0] = '0'; buf[1] = 0; } xs_sb_jstr(b, buf, strlen(buf)); return; }
        if (v->kind == XJ_BOOL) { xs_sb_jstr(b, v->b ? "true" : "false", v->b ? 4 : 5); return; }
        xn_typeorm_put(b, v);
        return;
    }
    if (strcmp(kind, "int") == 0 || strcmp(kind, "number") == 0) {
        if (v->kind == XJ_STR) {
            char *end = NULL;
            char tmp[64];
            size_t n = v->slen < sizeof tmp - 1 ? v->slen : sizeof tmp - 1;
            memcpy(tmp, v->str, n);
            tmp[n] = 0;
            double d = strtod(tmp, &end);
            if (end && end != tmp && *end == 0) { if (kind[0] == 'i') xs_sb_int(b, (int64_t)d); else xs_sb_num(b, d); return; }
        }
        if (v->kind == XJ_BOOL) { xs_sb_int(b, v->b ? 1 : 0); return; }
        if (v->kind == XJ_NUM && kind[0] == 'i') { xs_sb_int(b, (int64_t)v->num); return; }
        xn_typeorm_put(b, v);
        return;
    }
    if (strcmp(kind, "boolean") == 0) {
        if (v->kind == XJ_NUM) { xs_sb_bool(b, v->num != 0); return; }
        if (v->kind == XJ_STR) {
            bool t = (v->slen == 1 && (v->str[0] == '1' || v->str[0] == 'Y' || v->str[0] == 'y' || v->str[0] == 'S' || v->str[0] == 's' || v->str[0] == 'T' || v->str[0] == 't')) ||
                     (v->slen == 4 && strncasecmp(v->str, "true", 4) == 0);
            xs_sb_bool(b, t);
            return;
        }
        xn_typeorm_put(b, v);
        return;
    }
    xn_typeorm_put(b, v);
}

/* spec: one line per property, "column\tproperty\tkind\n" */
typedef struct { char *col, *prop, *kind; } xn_typeorm_col;
static int xn_typeorm_spec(xs_str *spec, xn_typeorm_col **out) {
    int n = 0, cap = 8;
    xn_typeorm_col *cols = malloc(sizeof *cols * (size_t)cap);
    const char *p = spec->data, *end = spec->data + spec->len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (!nl) nl = end;
        const char *t1 = memchr(p, '\t', (size_t)(nl - p));
        const char *t2 = t1 ? memchr(t1 + 1, '\t', (size_t)(nl - t1 - 1)) : NULL;
        if (t1 && t2) {
            if (n == cap) { cap *= 2; cols = realloc(cols, sizeof *cols * (size_t)cap); }
            cols[n].col = strndup(p, (size_t)(t1 - p));
            cols[n].prop = strndup(t1 + 1, (size_t)(t2 - t1 - 1));
            cols[n].kind = strndup(t2 + 1, (size_t)(nl - t2 - 1));
            n++;
        }
        p = nl + 1;
    }
    *out = cols;
    return n;
}

static xs_jv *xn_typeorm_field(xs_jv *row, const char *name) {
    for (size_t i = 0; i < row->n; i++) if (strcmp(row->keys[i], name) == 0) return row->items[i];
    for (size_t i = 0; i < row->n; i++) if (strcasecmp(row->keys[i], name) == 0) return row->items[i];
    return NULL;
}

XS_NOINLINE xs_str *xn_typeorm_remap(xs_str *rows, xs_str *spec) {
    xn_typeorm_col *cols;
    int ncol = xn_typeorm_spec(spec, &cols);
    xs_jv *v = xs_json_parse(rows);
    xs_sb b = {0};
    xs_sb_put(&b, "[", 1);
    if (v->kind == XJ_ARR) {
        for (size_t r = 0; r < v->n; r++) {
            xs_jv *row = v->items[r];
            if (r) xs_sb_put(&b, ",", 1);
            xs_sb_put(&b, "{", 1);
            bool first = true;
            for (int c = 0; c < ncol; c++) {
                xs_jv *val = row->kind == XJ_OBJ ? xn_typeorm_field(row, cols[c].col) : NULL;
                if (!val) continue; /* a column the query did not select: the property stays absent */
                if (!first) xs_sb_put(&b, ",", 1);
                first = false;
                xs_sb_jstr(&b, cols[c].prop, strlen(cols[c].prop));
                xs_sb_put(&b, ":", 1);
                xn_typeorm_coerce(&b, val, cols[c].kind);
            }
            xs_sb_put(&b, "}", 1);
        }
    }
    xs_sb_put(&b, "]", 1);
    xs_jv_free(v);
    for (int c = 0; c < ncol; c++) { free(cols[c].col); free(cols[c].prop); free(cols[c].kind); }
    free(cols);
    return xs_sb_finish(&b);
}

/* one field's JSON ("null" when absent) */
XS_NOINLINE xs_str *xn_typeorm_get(xs_str *json, xs_str *key) {
    xs_jv *v = xs_json_parse(json);
    xs_sb b = {0};
    xs_jv *f = NULL;
    if (v->kind == XJ_OBJ)
        for (size_t i = 0; i < v->n; i++)
            if (strlen(v->keys[i]) == (size_t)key->len && memcmp(v->keys[i], key->data, (size_t)key->len) == 0) f = v->items[i];
    if (f) xs_jv_write(&b, f); else xs_sb_puts(&b, "null");
    xs_jv_free(v);
    return xs_sb_finish(&b);
}

/* the i-th element's JSON ("null" past the end) */
XS_NOINLINE xs_str *xn_typeorm_at(xs_str *json, int64_t i) {
    xs_jv *v = xs_json_parse(json);
    xs_sb b = {0};
    if (v->kind == XJ_ARR && i >= 0 && (size_t)i < v->n) xs_jv_write(&b, v->items[i]); else xs_sb_puts(&b, "null");
    xs_jv_free(v);
    return xs_sb_finish(&b);
}

/* an array's length, an object's number of keys, 0 otherwise */
XS_NOINLINE int64_t xn_typeorm_len(xs_str *json) {
    xs_jv *v = xs_json_parse(json);
    int64_t n = v->kind == XJ_ARR || v->kind == XJ_OBJ ? (int64_t)v->n : 0;
    xs_jv_free(v);
    return n;
}

/* "null", "boolean", "number", "string", "array" or "object" */
XS_NOINLINE xs_str *xn_typeorm_kind(xs_str *json) {
    xs_jv *v = xs_json_parse(json);
    const char *k = v->kind == XJ_NULL ? "null" : v->kind == XJ_BOOL ? "boolean" : v->kind == XJ_NUM ? "number" : v->kind == XJ_STR ? "string" : v->kind == XJ_ARR ? "array" : "object";
    xs_jv_free(v);
    return xs_str_new(k, strlen(k));
}

/* an object's keys, in order, as a JSON array of strings */
XS_NOINLINE xs_str *xn_typeorm_keys(xs_str *json) {
    xs_jv *v = xs_json_parse(json);
    xs_sb b = {0};
    xs_sb_put(&b, "[", 1);
    if (v->kind == XJ_OBJ)
        for (size_t i = 0; i < v->n; i++) { if (i) xs_sb_put(&b, ",", 1); xs_sb_jstr(&b, v->keys[i], strlen(v->keys[i])); }
    xs_sb_put(&b, "]", 1);
    xs_jv_free(v);
    return xs_sb_finish(&b);
}
