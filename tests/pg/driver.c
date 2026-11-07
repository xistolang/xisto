/* The pg package's C against a live server (tests/pg/run.sh starts one): login, types, parameters, errors. */
#define XS_THREADS 1
#include "../../runtime/xisto_rt.h"
#include "../../runtime/xisto_net.h"
#include "../../runtime/xisto_crypto.h"
#include "../../packages/pg/native/pg.c"
static void run(xn_pg_NativePool *p, const char *sql, xs_arr_ref *values) {
    xs_str *r = xn_pg_NativePool_query(p, xs_str_new(sql, strlen(sql)), values);
    printf("%s\n", r->data);
    xs_release(r);
}
int main(int argc, char **argv) {
    xs_init(argc, argv);
    const char *port = getenv("PGPORT_TEST");
    char cfg[200];
    snprintf(cfg, sizeof cfg, "{\"host\":\"127.0.0.1\",\"port\":%s,\"user\":\"xisto\",\"password\":\"xisto\",\"database\":\"conduit\",\"max\":2}", port ? port : "55432");
    xn_pg_NativePool *p = xn_pg_NativePool_new(xs_str_new(cfg, strlen(cfg)));
    run(p, "SELECT 1::int AS one, 'x' AS s, true AS b, NULL AS n, ARRAY['a','b,c'] AS arr, 2.5::float8 AS f, 9007199254740993::int8 AS big, 1.50::numeric AS dec", NULL);
    xs_arr_ref *v = xs_arr_ref_new(3, NULL);
    xs_arr_ref_push(v, xs_str_new("o'neil \"q\"", 10));
    xs_arr_ref_push(v, xs_box_int(42));
    xs_arr_ref_push(v, NULL);
    run(p, "SELECT $1::text AS name, $2::int + 1 AS next, $3::text IS NULL AS was_null", v);
    run(p, "CREATE TEMP TABLE t (id serial, v text)", NULL);
    run(p, "INSERT INTO t (v) VALUES ('a'), ('b'), ('c')", NULL);
    run(p, "UPDATE t SET v = v || '!' WHERE id > 1 RETURNING id, v", NULL);
    jmp_buf jb;
    xs_try_push(&jb);
    if (_setjmp(jb) == 0) {
        run(p, "SELECT * FROM missing_table", NULL);
        xs_try_pop();
    } else {
        xs_Error *e = xs_caught();
        printf("error: %s\n", e->f_message->data);
    }
    run(p, "SELECT 'still usable' AS after_error", NULL);
    return 0;
}
