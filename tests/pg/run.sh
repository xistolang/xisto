#!/bin/sh
# the pg package against a live PostgreSQL (xisto-pg on 127.0.0.1:${PGPORT_TEST:-55432}; skipped without one):
# a pool asked for more connections than it has. Run from the repository root.
cd "$(dirname "$0")" || exit 1
X=../../build/xisto
mkdir -p .xisto/pkgs
ln -sfn ../../../../packages/pg .xisto/pkgs/pg
if ! nc -z 127.0.0.1 "${PGPORT_TEST:-55432}" 2>/dev/null; then echo "ok pg pool (skipped: no server on ${PGPORT_TEST:-55432})"; exit 0; fi
if $X build --no-warnings -o /tmp/xisto-pg-pool src/pool.ts > /tmp/xisto-pg-pool.build 2>&1 \
    && XISTO_RC_STATS=1 /tmp/xisto-pg-pool > /tmp/xisto-pg-pool.out 2> /tmp/xisto-pg-pool.stats \
    && cmp -s /tmp/xisto-pg-pool.out pool.expected && grep -q ' 0 still alive' /tmp/xisto-pg-pool.stats; then
    echo "ok pg pool (12 queries at once on 2 connections)"
else
    echo "FAIL pg pool"; head -5 /tmp/xisto-pg-pool.build; diff pool.expected /tmp/xisto-pg-pool.out | head; tail -2 /tmp/xisto-pg-pool.stats; exit 1
fi
