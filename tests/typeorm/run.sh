#!/bin/sh
# typeorm: the SQL the repository and the query builder write (always), and the package against a live PostgreSQL
# (xisto-pg on 127.0.0.1:${PGPORT_TEST:-55432}; skipped without one). Run from the repository root.
cd "$(dirname "$0")" || exit 1
X=../../build/xisto
mkdir -p .xisto/pkgs # (the packages from this repository, as examples/conduit/setup.sh links them)
ln -sfn ../../../../packages/typeorm .xisto/pkgs/typeorm
ln -sfn ../../../../packages/pg .xisto/pkgs/pg
ln -sfn ../../../../packages/oracledb .xisto/pkgs/oracledb
for p in express class-validator class-transformer rxjs; do ln -sfn "../../../../packages/$p" ".xisto/pkgs/$p"; done
mkdir -p .xisto/pkgs/@nestjs
for p in common core typeorm config; do ln -sfn "../../../../../packages/@nestjs/$p" ".xisto/pkgs/@nestjs/$p"; done
fail=0
if $X run --no-warnings src/sql.ts > /tmp/xisto-typeorm-sql.out 2>&1 && cmp -s /tmp/xisto-typeorm-sql.out sql.expected; then echo "ok typeorm sql"; else echo "FAIL typeorm sql"; diff sql.expected /tmp/xisto-typeorm-sql.out | head; fail=1; fi
if $X run --no-warnings src/nest-inject.ts > /tmp/xisto-typeorm-inject.out 2>&1 && cmp -s /tmp/xisto-typeorm-inject.out nest-inject.expected; then echo "ok typeorm forRootAsync inject"; else echo "FAIL typeorm forRootAsync inject"; diff nest-inject.expected /tmp/xisto-typeorm-inject.out | head; fail=1; fi
if $X run --no-warnings src/nest-optional.ts > /tmp/xisto-typeorm-optional.out 2>&1 && cmp -s /tmp/xisto-typeorm-optional.out nest-optional.expected; then echo "ok nest optional provider"; else echo "FAIL nest optional provider"; diff nest-optional.expected /tmp/xisto-typeorm-optional.out | head; fail=1; fi
if $X run --no-warnings src/nest-iface.ts > /tmp/xisto-typeorm-iface.out 2>&1 && cmp -s /tmp/xisto-typeorm-iface.out nest-iface.expected; then echo "ok nest provider by interface shape"; else echo "FAIL nest provider by interface shape"; diff nest-iface.expected /tmp/xisto-typeorm-iface.out | head; fail=1; fi
if $X run --no-warnings src/nest-datasource-dep.ts > /tmp/xisto-typeorm-dsdep.out 2>&1 && cmp -s /tmp/xisto-typeorm-dsdep.out nest-datasource-dep.expected; then echo "ok nest provider over the DataSource"; else echo "FAIL nest provider over the DataSource"; diff nest-datasource-dep.expected /tmp/xisto-typeorm-dsdep.out | head; fail=1; fi
if $X run --no-warnings src/oracle.ts > /tmp/xisto-typeorm-oracle.out 2>&1 && cmp -s /tmp/xisto-typeorm-oracle.out oracle.expected; then echo "ok typeorm oracle driver"; else echo "FAIL typeorm oracle driver"; diff oracle.expected /tmp/xisto-typeorm-oracle.out | head; fail=1; fi
# type "oracle" against a live Oracle Database (XISTO_ORACLE_URL=oracle://user:password@host:port/service, as
# tests/oracledb/run.sh); oracle-live.expected is what TypeORM 0.3 + node-oracledb 6 print for the same steps
if [ -n "$XISTO_ORACLE_URL" ]; then
    if $X build --no-warnings -o /tmp/xisto-typeorm-oracle-live src/oracle-live.ts > /tmp/xisto-typeorm-oracle-live.build 2>&1 \
        && TZ=America/Sao_Paulo XISTO_RC_STATS=1 /tmp/xisto-typeorm-oracle-live > /tmp/xisto-typeorm-oracle-live.out 2> /tmp/xisto-typeorm-oracle-live.stats \
        && cmp -s /tmp/xisto-typeorm-oracle-live.out oracle-live.expected && grep -q ' 0 still alive' /tmp/xisto-typeorm-oracle-live.stats; then
        echo "ok typeorm oracle (live)"
    else
        echo "FAIL typeorm oracle (live)"; head -5 /tmp/xisto-typeorm-oracle-live.build; diff oracle-live.expected /tmp/xisto-typeorm-oracle-live.out | head; tail -1 /tmp/xisto-typeorm-oracle-live.stats; fail=1
    fi
    # relations (@ManyToOne/@OneToMany, find's relations): relations-live.expected is TypeORM 0.3's output
    if $X run --no-warnings src/relations-live.ts > /tmp/xisto-typeorm-relations.out 2>&1 && cmp -s /tmp/xisto-typeorm-relations.out relations-live.expected; then
        echo "ok typeorm relations (live)"
    else
        echo "FAIL typeorm relations (live)"; diff relations-live.expected /tmp/xisto-typeorm-relations.out | head; fail=1
    fi
else
    echo "ok typeorm oracle live (skipped: XISTO_ORACLE_URL is not set)"
fi
if nc -z 127.0.0.1 "${PGPORT_TEST:-55432}" 2>/dev/null; then
    if $X run --no-warnings src/pg.ts > /tmp/xisto-typeorm-pg.out 2>&1 && cmp -s /tmp/xisto-typeorm-pg.out pg.expected; then echo "ok typeorm postgres"; else echo "FAIL typeorm postgres"; diff pg.expected /tmp/xisto-typeorm-pg.out | head; fail=1; fi
    # @nestjs/typeorm: a Nest application on the same server, its routes asked over HTTP
    PORT=${NEST_PORT:-38411}
    if $X build --no-warnings -o /tmp/xisto-typeorm-nest src/nest.ts > /tmp/xisto-typeorm-nest.build 2>&1; then
        NEST_PORT=$PORT /tmp/xisto-typeorm-nest > /tmp/xisto-typeorm-nest.log 2>&1 &
        pid=$!
        for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do nc -z 127.0.0.1 "$PORT" 2>/dev/null && break; sleep 0.25; done
        : > /tmp/xisto-typeorm-nest.out
        for u in items/setup items items/2 items/9 items/count items/teardown; do
            echo "GET /$u -> $(curl -s -w ' [%{http_code}]' "http://127.0.0.1:$PORT/$u")" >> /tmp/xisto-typeorm-nest.out
        done
        kill $pid 2>/dev/null
        wait $pid 2>/dev/null
        if cmp -s /tmp/xisto-typeorm-nest.out nest.expected; then echo "ok @nestjs/typeorm"; else echo "FAIL @nestjs/typeorm"; diff nest.expected /tmp/xisto-typeorm-nest.out | head; fail=1; fi
    else
        echo "FAIL @nestjs/typeorm: build"; head -5 /tmp/xisto-typeorm-nest.build; fail=1
    fi
else
    echo "ok typeorm postgres (skipped: no server on ${PGPORT_TEST:-55432})"
fi
exit $fail
