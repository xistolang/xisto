// Template literals and enums.
enum Color {
  Red,
  Green = 5,
  Blue,
}

enum Status {
  Active = "active",
  Done = "done",
}

class Task {
  title: string;
  status: Status = Status.Active;
  color: Color = Color.Blue;
  constructor(title: string) {
    this.title = title;
  }
  describe(): string {
    return `${this.title} [${this.status}] color=${this.color}`;
  }
}

function label(c: Color): string {
  switch (c) {
    case Color.Red:
      return "red";
    case Color.Green:
      return "green";
    default:
      return "other";
  }
}

const n: int = 42;
const x: number = 2.5;
const ok: boolean = true;
const who: string = "Isa";
console.log(`n=${n}, x=${x}, ok=${ok}, who=${who}!`, `${n + 1}${n * 2}`, `a\`b`, `${"nested " + `inner ${n}`}`);
console.log(`multi
line`, ``, `only ${who}`);
console.log(Color.Red, Color.Green, Color.Blue, Status.Active, Status.Done, label(Color.Green), label(Color.Blue));
const t: Task = new Task("ship it");
console.log(t.describe(), t.status === Status.Active, t.color !== Color.Red);
t.status = Status.Done;
console.log(JSON.stringify(t));
const back: Task = JSON.parse("{\"title\":\"parsed\",\"status\":\"done\",\"color\":5}");
console.log(back.title, back.status, back.color, back.status === Status.Done, String(back.color), String(back.status));
