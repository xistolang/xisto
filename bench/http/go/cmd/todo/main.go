// The todo API on Go (net/http, encoding/json). Same routes, bodies and status codes as
// bench/http/todo/server.xisto.ts:
//
//	GET /todos, POST /todos, GET /todos/{id}, PUT /todos/{id}, DELETE /todos/{id}
package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math"
	"net/http"
	"slices"
	"strconv"
	"strings"
	"sync"

	"xistobench/http/internal/common"
)

type Todo struct {
	ID    int64  `json:"id"`
	Title string `json:"title"`
	Done  bool   `json:"done"`
}

// Pointers tell a missing field from its zero value: both fields are required, as in the xisto classes.
type NewTodo struct {
	Title *string `json:"title"`
}

type TodoUpdate struct {
	Title *string `json:"title"`
	Done  *bool   `json:"done"`
}

type Store struct {
	mu     sync.RWMutex
	items  []*Todo
	nextID int64
}

func (s *Store) indexOf(id int64) int {
	return slices.IndexFunc(s.items, func(t *Todo) bool { return t.ID == id })
}

// parseID is Number(s) as JavaScript reads an id: a whole number >= 1, else -1.
func parseID(s string) int64 {
	t := strings.TrimSpace(s)
	n := 0.0
	if t != "" {
		var err error
		if n, err = strconv.ParseFloat(t, 64); err != nil {
			return -1
		}
	}
	if math.IsInf(n, 0) || math.IsNaN(n) || n != math.Trunc(n) || n < 1 {
		return -1
	}
	return int64(n)
}

// The xisto server answers JSON errors through HttpResponse.status, which sends text/plain; its JSON.parse
// failures go out as application/json. Both are kept so the responses match byte for byte.
func todoNotFound(w http.ResponseWriter) {
	common.Reply(w, http.StatusNotFound, common.Text, []byte(`{"error":"not found"}`))
}

func badJSON(w http.ResponseWriter, what string) {
	common.Reply(w, http.StatusBadRequest, common.JSON, common.Marshal(map[string]string{"error": "JSON: " + what}))
}

// decode reads a JSON body into v; on failure it answers 400 with the xisto server's words where they map
// one to one, and returns false.
func decode(w http.ResponseWriter, body []byte, v any) bool {
	err := json.Unmarshal(body, v)
	if err == nil {
		return true
	}
	var syn *json.SyntaxError
	var typ *json.UnmarshalTypeError
	switch {
	case errors.As(err, &syn) && syn.Error() == "unexpected end of JSON input":
		badJSON(w, fmt.Sprintf("unexpected end at offset %d", len(body)))
	case errors.As(err, &typ) && typ.Field == "title":
		badJSON(w, "'title' must be a string")
	case errors.As(err, &typ) && typ.Field == "done":
		badJSON(w, "'done' must be true or false")
	case errors.As(err, &typ) && typ.Field == "":
		badJSON(w, "'value' must be an object")
	default:
		badJSON(w, err.Error())
	}
	return false
}

func main() {
	store := &Store{items: []*Todo{}}
	mux := http.NewServeMux()

	mux.HandleFunc("GET /todos", func(w http.ResponseWriter, r *http.Request) {
		store.mu.RLock()
		out := common.Marshal(store.items)
		store.mu.RUnlock()
		common.Reply(w, http.StatusOK, common.JSON, out)
	})

	mux.HandleFunc("POST /todos", func(w http.ResponseWriter, r *http.Request) {
		body, _ := io.ReadAll(r.Body)
		var input NewTodo
		if !decode(w, body, &input) {
			return
		}
		if input.Title == nil {
			badJSON(w, "missing field 'title'")
			return
		}
		title := strings.TrimSpace(*input.Title)
		if title == "" {
			common.Reply(w, http.StatusBadRequest, common.Text, []byte(`{"error":"title is required"}`))
			return
		}
		store.mu.Lock()
		store.nextID++
		t := &Todo{ID: store.nextID, Title: title}
		store.items = append(store.items, t)
		out := common.Marshal(t)
		store.mu.Unlock()
		common.Reply(w, http.StatusCreated, common.Text, out)
	})

	mux.HandleFunc("GET /todos/{id}", func(w http.ResponseWriter, r *http.Request) {
		id := parseID(r.PathValue("id"))
		store.mu.RLock()
		i := store.indexOf(id)
		var out []byte
		if i >= 0 {
			out = common.Marshal(store.items[i])
		}
		store.mu.RUnlock()
		if i < 0 {
			todoNotFound(w)
			return
		}
		common.Reply(w, http.StatusOK, common.JSON, out)
	})

	mux.HandleFunc("PUT /todos/{id}", func(w http.ResponseWriter, r *http.Request) {
		id := parseID(r.PathValue("id"))
		body, _ := io.ReadAll(r.Body)
		store.mu.Lock()
		defer store.mu.Unlock()
		i := store.indexOf(id)
		if i < 0 {
			todoNotFound(w)
			return
		}
		var input TodoUpdate
		if !decode(w, body, &input) {
			return
		}
		if input.Title == nil {
			badJSON(w, "missing field 'title'")
			return
		}
		if input.Done == nil {
			badJSON(w, "missing field 'done'")
			return
		}
		t := store.items[i]
		if *input.Title != "" {
			t.Title = *input.Title
		}
		t.Done = *input.Done
		common.Reply(w, http.StatusOK, common.JSON, common.Marshal(t))
	})

	mux.HandleFunc("DELETE /todos/{id}", func(w http.ResponseWriter, r *http.Request) {
		id := parseID(r.PathValue("id"))
		store.mu.Lock()
		i := store.indexOf(id)
		if i >= 0 {
			store.items = slices.Delete(store.items, i, i+1)
		}
		store.mu.Unlock()
		if i < 0 {
			todoNotFound(w)
			return
		}
		common.Reply(w, http.StatusNoContent, common.Text, nil)
	})

	// the same paths with any other method, then everything else
	mux.HandleFunc("/todos", common.MethodNotAllowed)
	mux.HandleFunc("/todos/{id}", common.MethodNotAllowed)
	mux.HandleFunc("/", common.NotFound)
	common.Listen("go", mux)
}
