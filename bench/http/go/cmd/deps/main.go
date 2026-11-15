// An API whose handlers wait on a slow dependency (the fake database on 8081), on Go (net/http).
// Same routes and bodies as bench/http/deps/app.xisto.ts. One shared http.Client: a keep-alive pool.
package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"time"

	"xistobench/http/internal/common"
)

type Row struct {
	ID   int64  `json:"id"`
	Name string `json:"name"`
}

type DbReply struct {
	Rows []Row `json:"rows"`
	Ms   int64 `json:"ms"`
}

type Item struct {
	ID    int64  `json:"id"`
	Title string `json:"title"`
}

// the default transport keeps only 2 idle connections per host; 64 concurrent requests need a bigger pool
var client = func() *http.Client {
	t := http.DefaultTransport.(*http.Transport).Clone()
	t.MaxIdleConns = 1024
	t.MaxIdleConnsPerHost = 1024
	t.IdleConnTimeout = 60 * time.Second
	return &http.Client{Transport: t}
}()

func query(ms string) (DbReply, error) {
	var reply DbReply
	r, err := client.Get("http://127.0.0.1:8081/q?ms=" + ms)
	if err != nil {
		return reply, err
	}
	defer r.Body.Close()
	text, err := io.ReadAll(r.Body)
	if err != nil {
		return reply, err
	}
	err = json.Unmarshal(text, &reply)
	return reply, err
}

func fail(w http.ResponseWriter, err error) {
	common.Reply(w, http.StatusInternalServerError, "text/plain", []byte(err.Error()))
}

func main() {
	items := make([]Item, 50)
	for i := range items {
		items[i] = Item{ID: int64(i + 1), Title: fmt.Sprintf("item %d", i+1)}
	}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /one", func(w http.ResponseWriter, r *http.Request) {
		db, err := query(r.URL.Query().Get("ms"))
		if err != nil {
			fail(w, err)
			return
		}
		common.Reply(w, http.StatusOK, common.JSON, common.Marshal(db))
	})
	mux.HandleFunc("GET /three", func(w http.ResponseWriter, r *http.Request) {
		ms := r.URL.Query().Get("ms")
		total := 0
		for range 3 {
			db, err := query(ms)
			if err != nil {
				fail(w, err)
				return
			}
			total += len(db.Rows)
		}
		common.Reply(w, http.StatusOK, common.JSON, fmt.Appendf(nil, `{"total":%d}`, total))
	})
	mux.HandleFunc("GET /mixed", func(w http.ResponseWriter, r *http.Request) {
		db, err := query(r.URL.Query().Get("ms"))
		if err != nil {
			fail(w, err)
			return
		}
		// the 50 items are serialized on every request, as JSON.stringify(items) on the other servers
		common.Reply(w, http.StatusOK, common.JSON, fmt.Appendf(nil, `{"db":%d,"items":%s}`, len(db.Rows), common.Marshal(items)))
	})
	mux.HandleFunc("/one", common.MethodNotAllowed)
	mux.HandleFunc("/three", common.MethodNotAllowed)
	mux.HandleFunc("/mixed", common.MethodNotAllowed)
	mux.HandleFunc("/", common.NotFound)
	common.Listen("go", mux)
}
