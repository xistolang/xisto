#!/bin/sh
# Prints "<step> -> <status> [<content-type>] <body>" for every route of one section, against the server on
# 127.0.0.1:8080, so two servers can be diffed: probe.sh todo > a.txt (server A), probe.sh todo > b.txt (server B).
# Usage: bench/http/probe.sh hello|todo|deps   (deps also needs the fake database on 8081)
B=http://127.0.0.1:8080
tmp=$(mktemp); trap 'rm -f "$tmp"' EXIT
step() {
    printf '%s -> ' "$1"; shift
    curl -s -o "$tmp" -w '%{http_code} [%{content_type}]' "$@"
    printf ' %s\n' "$(cat "$tmp")"
}
case "$1" in
hello)
    step "ping"            $B/ping
    step "ping query"      "$B/ping?x=1"
    step "ping slash"      $B/ping/
    step "hello"           "$B/hello?name=isa"
    step "hello encoded"   "$B/hello?name=a%20b%C3%A9"
    step "hello plus"      "$B/hello?name=a+b"
    step "hello astral"    "$B/hello?name=%F0%9F%98%80"
    step "hello none"      $B/hello
    step "hello twice"     "$B/hello?name=x&name=y"
    step "echo"            -X POST -d 'abc' $B/echo
    step "echo empty"      -X POST $B/echo
    step "echo utf8"       -X POST -d 'pão 😀' $B/echo
    step "post ping"       -X POST $B/ping
    step "get echo"        $B/echo
    step "unknown"         $B/nope
    ;;
todo)
    step "list empty"      $B/todos
    step "create 1"        -X POST -d '{"title":"  comprar pão  "}' $B/todos
    step "create 2"        -X POST -d '{"title":"estudar xisto"}' $B/todos
    step "create escapes"  -X POST -d '{"title":"a\"b\\c\n\t\u0001 é 😀 / ok"}' $B/todos
    step "create extra"    -X POST -d '{"title":"x","foo":[1,{"a":2}],"done":true,"id":99}' $B/todos
    step "create empty"    -X POST -d '{"title":"   "}' $B/todos
    step "create bad json" -X POST -d '{"title": ' $B/todos
    step "create bad type" -X POST -d '{"title": 5}' $B/todos
    step "create missing"  -X POST -d '{}' $B/todos
    step "list"            $B/todos
    step "get 2"           $B/todos/2
    step "get 9"           $B/todos/9
    step "get abc"         $B/todos/abc
    step "get 2.0"         $B/todos/2.0
    step "get 0"           $B/todos/0
    step "get -1"          $B/todos/-1
    step "put 1"           -X PUT -d '{"title":"","done":true}' $B/todos/1
    step "put 2 title"     -X PUT -d '{"title":"estudar QBE","done":false}' $B/todos/2
    step "put 9"           -X PUT -d '{"title":"x","done":true}' $B/todos/9
    step "put bad json"    -X PUT -d '{"title":' $B/todos/2
    step "delete 1"        -X DELETE $B/todos/1
    step "delete 1 again"  -X DELETE $B/todos/1
    step "delete abc"      -X DELETE $B/todos/abc
    step "list final"      $B/todos
    step "unknown"         $B/nope
    ;;
todo-edge) # malformed input: for syntax errors other than a truncated body, Rust and Go use their parser's words
    step "create 1"        -X POST -d '{"title":"one"}' $B/todos
    step "create null"     -X POST -d '{"title":null}' $B/todos
    step "create array"    -X POST -d '[]' $B/todos
    step "create trailing" -X POST -d '{"title":"y"} x' $B/todos
    step "create garbage"  -X POST -d 'garbage' $B/todos
    step "create no body"  -X POST $B/todos
    step "put no done"     -X PUT -d '{"title":"t"}' $B/todos/1
    step "put bad done"    -X PUT -d '{"title":"t","done":"yes"}' $B/todos/1
    step "put bad title"   -X PUT -d '{"title":1,"done":true}' $B/todos/1
    step "patch"           -X PATCH $B/todos/1
    step "delete list"     -X DELETE $B/todos
    step "get trailing /"  $B/todos/
    step "get deep"        $B/todos/1/x
    ;;
deps)
    step "one 0"           "$B/one?ms=0"
    step "one 5"           "$B/one?ms=5"
    step "three 1"         "$B/three?ms=1"
    step "mixed 1"         "$B/mixed?ms=1"
    step "unknown"         $B/nope
    ;;
*) echo "usage: $0 hello|todo|deps" >&2; exit 2 ;;
esac
