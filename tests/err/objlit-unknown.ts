// expect: did you mean 'title'?
interface Todo {
  id: int;
  title: string;
}
const t: Todo = { id: 1, titel: "x", title: "y" };
