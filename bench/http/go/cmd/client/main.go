// REST client on Go (net/http). Same work as bench/http/client.ts: 20000 sequential iterations of
// GET /ping + GET /hello?name=isa, one keep-alive client.
package main

import (
	"fmt"
	"io"
	"net/http"

	"xistobench/http/internal/common"
)

type ApiClient struct {
	base  string
	http  *http.Client
	calls int
}

func (c *ApiClient) get(path string) string {
	c.calls++
	r, err := c.http.Get(c.base + path)
	if err != nil {
		panic(err)
	}
	defer r.Body.Close()
	body, err := io.ReadAll(r.Body)
	if err != nil {
		panic(err)
	}
	return string(body)
}

func (c *ApiClient) Ping() string             { return c.get("/ping") }
func (c *ApiClient) Hello(name string) string { return c.get("/hello?name=" + name) }

func main() {
	client := &ApiClient{base: "http://127.0.0.1:8080", http: &http.Client{}}
	n := 20000
	ok, bytes := 0, 0
	for range n {
		if client.Ping() == "pong" {
			ok++
		}
		bytes += common.JSLength(client.Hello("isa"))
	}
	fmt.Println("calls", client.calls, "ok", ok, "bytes", bytes)
}
