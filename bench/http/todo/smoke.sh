#!/bin/sh
# Exercises the todo API on 127.0.0.1:8080 and prints "<status> <body>" per step, so servers can be diffed.
B=http://127.0.0.1:8080
step() { printf '%s -> ' "$1"; shift; curl -s -o /tmp/xs_body -w '%{http_code}' "$@"; printf ' %s\n' "$(cat /tmp/xs_body)"; }
step "list empty"      $B/todos
step "create 1"        -X POST -d '{"title":"  comprar pão  "}' $B/todos
step "create 2"        -X POST -d '{"title":"estudar xisto"}' $B/todos
step "create empty"    -X POST -d '{"title":"   "}' $B/todos
step "create bad json" -X POST -d '{"title": ' $B/todos
step "create bad type" -X POST -d '{"title": 5}' $B/todos
step "list"            $B/todos
step "get 2"           $B/todos/2
step "get 9"           $B/todos/9
step "get abc"         $B/todos/abc
step "put 1"           -X PUT -d '{"title":"","done":true}' $B/todos/1
step "put 2 title"     -X PUT -d '{"title":"estudar QBE","done":false}' $B/todos/2
step "delete 1"        -X DELETE $B/todos/1
step "delete 1 again"  -X DELETE $B/todos/1
step "list final"      $B/todos
step "unknown"         $B/nope
