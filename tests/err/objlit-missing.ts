// expect: missing field 'title' of Todo
interface Todo {
  id: int;
  title: string;
}
const t: Todo = { id: 1 };
