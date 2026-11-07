interface Todo {
  id: int;
  title: string;
  note?: string;
}
const todos: Todo[] = [{ id: 1, title: "a" }, { id: 2, title: "b", note: "urgent" }];
const noted: Todo[] = todos.filter((t) => t.note !== null);
assert(noted.length === 1, "one todo with a note");
assert(JSON.stringify(todos[0]) === "{\"id\":1,\"title\":\"a\"}", "optional omitted in JSON");
let threw: boolean = false;
try {
  assert(false, "boom");
} catch (e) {
  threw = e.message === "assertion failed: boom";
}
assert(threw, "assert throws a catchable Error");
