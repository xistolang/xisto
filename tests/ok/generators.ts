// Generators: function*, yield, yield*, iterable classes ([Symbol.iterator]), infinite sequences stopped with
// break, exceptions thrown inside, and a generator left half way (its locals are released).
function* range(from: int, to: int): Generator<int> {
  for (let i: int = from; i < to; i++) {
    yield i;
  }
}

function* naturals(): Generator<int> {
  let n: int = 1;
  while (true) {
    yield n;
    n++;
  }
}

function* words(text: string): Generator<string> {
  for (const w of text.split(" ")) {
    if (w.length > 0) {
      yield w.toUpperCase();
    }
  }
}

function* both(): Generator<string> {
  yield "start";
  yield* words("a  b c");
  yield "end";
}

class Deck {
  cards: string[] = [];
  add(c: string): Deck {
    this.cards.push(c);
    return this;
  }
  *[Symbol.iterator](): Iterator<string> {
    for (let i: int = this.cards.length - 1; i >= 0; i--) {
      yield `${this.cards[i]}♠`;
    }
  }
}

function* risky(n: int): Generator<int> {
  yield n;
  if (n > 1) {
    throw new Error(`too far: ${n}`);
  }
  yield n + 1;
}

let total: int = 0;
for (const i of range(1, 5)) {
  total += i;
}
console.log("sum", total);

const firsts: int[] = [];
for (const n of naturals()) {
  if (n > 4) {
    break;
  }
  firsts.push(n * n);
}
console.log(firsts);

const all: string[] = [];
for (const w of both()) {
  all.push(w);
}
console.log(all.join(","));

const deck = new Deck().add("A").add("K").add("Q");
const seen: string[] = [];
for (const c of deck) {
  seen.push(c);
}
console.log(seen);

for (const v of risky(1)) {
  console.log("risky1", v);
}
try {
  console.log("risky2", risky(2).toArray());
} catch (e) {
  console.log("caught", e.message);
}
console.log(range(0, 6).filter((x: int): boolean => x % 2 === 0).map((x: int): int => x * 10).toArray());

function firstLong(list: string[]): string {
  for (const w of words(list.join(" "))) {
    if (w.length > 3) {
      return w;
    }
  }
  return "none";
}
console.log(firstLong(["hi", "there", "you"]), firstLong(["a"]));
