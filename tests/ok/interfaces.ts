// interface, type alias, object literals, optional fields, nested arrays.
interface Todo {
  id: int;
  title: string;
  done: boolean;
  tags?: string[];
}

type Point = { x: number; y: number };

interface Page {
  items: Todo[];
  total: int;
  next?: string;
}

function makeTodo(id: int, title: string): Todo {
  return { id, title, done: false };
}

const a: Todo = makeTodo(1, "comprar pão");
const b: Todo = { id: 2, title: "estudar", done: true, tags: ["xisto", "ts"] };
const page: Page = { items: [a, b], total: 2 };
console.log(a, b);
const t: string[] | null = b.tags;
if (t !== null) {
  console.log(t.length, t.join("+"));
}
console.log(JSON.stringify(page));
const back: Page = JSON.parse(JSON.stringify(page));
console.log(back.items.length, back.items[1].title, back.items[0]);
const p: Point = { x: 1.5, y: -2 };
const pts: Point[] = [p, { x: 0, y: 0 }];
console.log(pts.map((q) => q.x + q.y), pts[0]);
const grid: int[][] = [[1, 2], [3, 4, 5]];
grid[1].push(6);
const rows: string[][] = [["a"], ["b", "c"]];
console.log(grid, grid.length, grid[1].length, rows.map((r) => r.join("")).join(","));
const matrix: number[][] = [];
for (let i: int = 0; i < 3; i++) {
  const row: number[] = [];
  for (let j: int = 0; j < 3; j++) {
    row.push(Number(i * 3 + j));
  }
  matrix.push(row);
}
console.log(matrix[2][1], JSON.stringify(matrix));
