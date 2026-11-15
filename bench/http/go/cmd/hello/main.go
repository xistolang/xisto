// REST server on Go (net/http). Same routes as bench/http/server.xisto.ts.
package main

import (
	"fmt"
	"io"
	"net/http"

	"xistobench/http/internal/common"
)

func main() {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /ping", func(w http.ResponseWriter, r *http.Request) {
		common.Reply(w, http.StatusOK, common.Text, []byte("pong"))
	})
	mux.HandleFunc("GET /hello", func(w http.ResponseWriter, r *http.Request) {
		name := r.URL.Query().Get("name")
		common.Reply(w, http.StatusOK, common.JSON, fmt.Appendf(nil, `{"hello":"%s","len":%d}`, name, common.JSLength(name)))
	})
	mux.HandleFunc("POST /echo", func(w http.ResponseWriter, r *http.Request) {
		body, _ := io.ReadAll(r.Body)
		common.Reply(w, http.StatusOK, common.Text, body)
	})
	// the same paths with any other method, then everything else
	mux.HandleFunc("/ping", common.MethodNotAllowed)
	mux.HandleFunc("/hello", common.MethodNotAllowed)
	mux.HandleFunc("/echo", common.MethodNotAllowed)
	mux.HandleFunc("/", common.NotFound)
	common.Listen("go", mux)
}
