/* SQLite for Xisto, with the API of Node's node:sqlite (DatabaseSync / StatementSync).
   SQLite itself is compiled from source (third_party/sqlite, the amalgamation) once into build/sqlite3.o
   and linked only into programs that import node:sqlite. Rows come back through the JSON readers:
   each row becomes an object keyed by column name and is converted to the declared interface or class. */
#include "sqlite3.h"

typedef struct xs_DatabaseSync { xs_hdr hdr; sqlite3 *db; } xs_DatabaseSync;
typedef struct xs_StatementSync { xs_hdr hdr; sqlite3_stmt *st; xs_DatabaseSync *owner; } xs_StatementSync;
typedef struct xs_RunResult { xs_hdr hdr; int64_t f_changes; int64_t f_lastInsertRowid; } xs_RunResult;

XS_NOINLINE void xs_sqlite_fail(sqlite3 *db, const char *what) {
    static XS_TLS char buf[600];
    snprintf(buf, sizeof buf, "SQLite: %s: %s", what, db ? sqlite3_errmsg(db) : "error");
    xs_panic(buf);
}

static void xs_DatabaseSync_free(void *p) { xs_DatabaseSync *d = p; if (d->db) sqlite3_close_v2(d->db); xs_obj_free(p); }
static void xs_DatabaseSync_print(void *p) { (void)p; fputs("DatabaseSync {}", stdout); }
XS_NOINLINE xs_DatabaseSync *xs_DatabaseSync_new(xs_str *path) {
    xs_DatabaseSync *d = xs_obj_alloc(sizeof *d, xs_DatabaseSync_free, xs_DatabaseSync_print);
    if (sqlite3_open_v2(path->data, &d->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        xs_str_print_(path);
        xs_sqlite_fail(d->db, "cannot open database");
    }
    return d;
}
XS_NOINLINE void xs_DatabaseSync_exec(xs_DatabaseSync *d, xs_str *sql) {
    char *err = NULL;
    if (sqlite3_exec(d->db, sql->data, NULL, NULL, &err) != SQLITE_OK) {
        static XS_TLS char buf[600];
        snprintf(buf, sizeof buf, "SQLite: %s", err ? err : "exec failed");
        sqlite3_free(err);
        xs_panic(buf);
    }
}
static void xs_DatabaseSync_close(xs_DatabaseSync *d) { if (d->db) { sqlite3_close_v2(d->db); d->db = NULL; } }

static void xs_StatementSync_free(void *p) { xs_StatementSync *s = p; sqlite3_finalize(s->st); xs_release(s->owner); xs_obj_free(p); }
static void xs_StatementSync_print(void *p) { (void)p; fputs("StatementSync {}", stdout); }
XS_NOINLINE xs_StatementSync *xs_DatabaseSync_prepare(xs_DatabaseSync *d, xs_str *sql) {
    xs_StatementSync *s = xs_obj_alloc(sizeof *s, xs_StatementSync_free, xs_StatementSync_print);
    if (sqlite3_prepare_v2(d->db, sql->data, (int)sql->len, &s->st, NULL) != SQLITE_OK) {
        sqlite3_stmt *none = s->st;
        s->st = NULL;
        sqlite3_finalize(none);
        xs_obj_free(s);
        xs_sqlite_fail(d->db, "prepare");
    }
    s->owner = xs_retain(d);
    return s;
}

XS_NOINLINE void xs_sqlite_bind(xs_StatementSync *s, int n, const xs_sqarg *args) {
    sqlite3_reset(s->st);
    sqlite3_clear_bindings(s->st);
    if (n != sqlite3_bind_parameter_count(s->st)) {
        static XS_TLS char buf[200];
        snprintf(buf, sizeof buf, "SQLite: the statement has %d parameter(s), %d given", sqlite3_bind_parameter_count(s->st), n);
        xs_panic(buf);
    }
    for (int i = 0; i < n; i++) {
        int rc = SQLITE_OK;
        switch (args[i].kind) {
        case 1: case 4: rc = sqlite3_bind_int64(s->st, i + 1, args[i].i); break;
        case 2: rc = sqlite3_bind_double(s->st, i + 1, args[i].d); break;
        case 3: rc = args[i].s ? sqlite3_bind_text(s->st, i + 1, args[i].s->data, (int)args[i].s->len, SQLITE_TRANSIENT) : sqlite3_bind_null(s->st, i + 1); break;
        default: rc = sqlite3_bind_null(s->st, i + 1); break;
        }
        if (rc != SQLITE_OK) xs_sqlite_fail(s->owner->db, "bind");
    }
}

static void xs_RunResult_print(void *p) {
    xs_RunResult *r = p;
    printf("{ changes: %lld, lastInsertRowid: %lld }", (long long)r->f_changes, (long long)r->f_lastInsertRowid);
}
XS_NOINLINE xs_RunResult *xs_StatementSync_run(xs_StatementSync *s, int n, const xs_sqarg *args) {
    xs_sqlite_bind(s, n, args);
    int rc;
    while ((rc = sqlite3_step(s->st)) == SQLITE_ROW) {}
    if (rc != SQLITE_DONE) xs_sqlite_fail(s->owner->db, "run");
    xs_RunResult *r = xs_obj_alloc(sizeof *r, xs_obj_free, xs_RunResult_print);
    r->f_changes = sqlite3_changes64(s->owner->db);
    r->f_lastInsertRowid = sqlite3_last_insert_rowid(s->owner->db);
    sqlite3_reset(s->st);
    return r;
}

/* one row as a JSON-like object (arena allocated, like a JSON parse) */
static xs_jv *xs_sqlite_row(sqlite3_stmt *st) {
    int nc = sqlite3_column_count(st);
    xs_jv *o = xs_jv_new(XJ_OBJ);
    o->items = xs_jalloc(sizeof(xs_jv *) * (size_t)(nc ? nc : 1));
    o->keys = xs_jalloc(sizeof(char *) * (size_t)(nc ? nc : 1));
    for (int c = 0; c < nc; c++) {
        char *k = (char *)sqlite3_column_name(st, c); /* valid until the statement is finalized */
        xs_jv *v;
        switch (sqlite3_column_type(st, c)) {
        case SQLITE_INTEGER: v = xs_jv_new(XJ_NUM); v->num = (double)sqlite3_column_int64(st, c); break;
        case SQLITE_FLOAT: v = xs_jv_new(XJ_NUM); v->num = sqlite3_column_double(st, c); break;
        case SQLITE_NULL: v = xs_jv_new(XJ_NULL); break;
        default: {
            v = xs_jv_new(XJ_STR);
            const unsigned char *t = sqlite3_column_text(st, c);
            int len = sqlite3_column_bytes(st, c);
            v->str = xs_jalloc((size_t)len + 1);
            memcpy(v->str, t, (size_t)len);
            v->str[len] = 0;
            v->slen = (size_t)len;
        }
        }
        o->keys[o->n] = k;
        o->items[o->n++] = v;
    }
    return o;
}

/* all(): an array of row objects; get(): the first row or JSON null */
XS_NOINLINE xs_jv *xs_sqlite_rows(xs_StatementSync *s, int n, const xs_sqarg *args, bool first_only) {
    xs_jarena_free();
    xs_sqlite_bind(s, n, args);
    xs_jv *arr = xs_jv_new(XJ_ARR);
    size_t cap = 0;
    int rc;
    while ((rc = sqlite3_step(s->st)) == SQLITE_ROW) {
        xs_jv *row = xs_sqlite_row(s->st);
        if (first_only) { sqlite3_reset(s->st); return row; }
        if (arr->n == cap) { size_t nc = cap ? cap * 2 : 16; arr->items = xs_jgrow(arr->items, arr->n, nc, sizeof(void *)); cap = nc; }
        arr->items[arr->n++] = row;
    }
    if (rc != SQLITE_DONE) xs_sqlite_fail(s->owner->db, "query");
    sqlite3_reset(s->st);
    return first_only ? xs_jv_new(XJ_NULL) : arr;
}

/* row-by-row: start binds, step returns the next row (arena reset each time) or NULL at the end */
XS_NOINLINE void xs_sqlite_start(xs_StatementSync *s, int n, const xs_sqarg *args) { xs_sqlite_bind(s, n, args); }
XS_NOINLINE xs_jv *xs_sqlite_step_row(xs_StatementSync *s) {
    xs_jarena_reset();
    int rc = sqlite3_step(s->st);
    if (rc == SQLITE_ROW) return xs_sqlite_row(s->st);
    sqlite3_reset(s->st);
    if (rc != SQLITE_DONE) xs_sqlite_fail(s->owner->db, "query");
    return NULL;
}
