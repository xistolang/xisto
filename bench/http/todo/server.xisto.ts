// A complete REST API in Xisto: an in-memory todo list.
//   GET    /todos           list
//   POST   /todos           create  {"title": "..."}
//   GET    /todos/:id       read
//   PUT    /todos/:id       update  {"title": "...", "done": true}
//   DELETE /todos/:id       delete
//   GET    /__metrics       built-in indicators
class Todo {
  id: int;
  title: string;
  done: boolean = false;
  constructor(id: int, title: string) {
    this.id = id;
    this.title = title;
  }
}

class NewTodo {
  title: string = "";
}

class TodoUpdate {
  title: string = "";
  done: boolean = false;
}

class TodoStore {
  items: Todo[] = [];
  nextId: int = 1;

  add(title: string): Todo {
    const t: Todo = new Todo(this.nextId, title);
    this.nextId++;
    this.items.push(t);
    return t;
  }

  indexOf(id: int): int {
    for (let i: int = 0; i < this.items.length; i++) {
      if (this.items[i].id === id) {
        return i;
      }
    }
    return -1;
  }

  remove(id: int): boolean {
    const i: int = this.indexOf(id);
    if (i < 0) {
      return false;
    }
    this.items.splice(i, 1);
    return true;
  }
}

const store: TodoStore = new TodoStore();

function parseId(req: HttpRequest): int {
  const n: number = Number(req.param("id"));
  if (Number.isNaN(n) || !Number.isInteger(n) || n < 1) {
    return -1;
  }
  return Math.trunc(n);
}

function notFound(): HttpResponse {
  return HttpResponse.status(404, "{\"error\":\"not found\"}");
}

function listTodos(req: HttpRequest): HttpResponse {
  return HttpResponse.json(JSON.stringify(store.items));
}

function createTodo(req: HttpRequest): HttpResponse {
  const input: NewTodo = JSON.parse(req.body);
  if (input.title.trim().length === 0) {
    return HttpResponse.status(400, "{\"error\":\"title is required\"}");
  }
  const t: Todo = store.add(input.title.trim());
  return HttpResponse.status(201, JSON.stringify(t));
}

function getTodo(req: HttpRequest): HttpResponse {
  const i: int = store.indexOf(parseId(req));
  if (i < 0) {
    return notFound();
  }
  return HttpResponse.json(JSON.stringify(store.items[i]));
}

function updateTodo(req: HttpRequest): HttpResponse {
  const i: int = store.indexOf(parseId(req));
  if (i < 0) {
    return notFound();
  }
  const input: TodoUpdate = JSON.parse(req.body);
  const t: Todo = store.items[i];
  if (input.title.length > 0) {
    t.title = input.title;
  }
  t.done = input.done;
  return HttpResponse.json(JSON.stringify(t));
}

function deleteTodo(req: HttpRequest): HttpResponse {
  if (!store.remove(parseId(req))) {
    return notFound();
  }
  return HttpResponse.status(204, "");
}

const app: Server = new Server();
app.get("/todos", listTodos);
app.post("/todos", createTodo);
app.get("/todos/:id", getTodo);
app.put("/todos/:id", updateTodo);
app.delete("/todos/:id", deleteTodo);
app.listen(8080);
