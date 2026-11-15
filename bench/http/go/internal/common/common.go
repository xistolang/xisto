// Package common holds what the Go servers share: the response helpers, the JSON writer and the listener.
// Threads: GOMAXPROCS (an environment variable the Go runtime reads) sets how many OS threads run Go code;
// the bench scripts set GOMAXPROCS=1 for the one-event-loop sections and GOMAXPROCS=N for the threads section.
package common

import (
	"bytes"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"runtime"
	"unicode/utf16"
)

const (
	Text = "text/plain; charset=utf-8"
	JSON = "application/json"
)

// Reply writes one complete response.
func Reply(w http.ResponseWriter, status int, contentType string, body []byte) {
	w.Header().Set("Content-Type", contentType)
	w.WriteHeader(status)
	w.Write(body)
}

func NotFound(w http.ResponseWriter, r *http.Request) {
	Reply(w, http.StatusNotFound, "text/plain", []byte("not found"))
}

func MethodNotAllowed(w http.ResponseWriter, r *http.Request) {
	Reply(w, http.StatusMethodNotAllowed, "text/plain", []byte("method not allowed"))
}

// Marshal is json.Marshal as JSON.stringify writes: no HTML escaping of <, > and &.
func Marshal(v any) []byte {
	var buf bytes.Buffer
	enc := json.NewEncoder(&buf)
	enc.SetEscapeHTML(false)
	if err := enc.Encode(v); err != nil {
		panic(err)
	}
	return bytes.TrimSuffix(buf.Bytes(), []byte("\n"))
}

// JSLength is the length JavaScript reports for a string: UTF-16 code units.
func JSLength(s string) int {
	n := 0
	for _, r := range s {
		n += utf16.RuneLen(r)
	}
	return n
}

// Listen serves mux on :8080.
func Listen(name string, mux *http.ServeMux) {
	fmt.Fprintf(os.Stderr, "%s: listening on http://127.0.0.1:8080 (GOMAXPROCS=%d)\n", name, runtime.GOMAXPROCS(0))
	log.Fatal(http.ListenAndServe(":8080", mux))
}
