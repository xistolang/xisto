// return, break and continue leaving a try (or its catch) run the finally first, as in JavaScript
const log: string[] = [];
function early(x: int): int {
  try {
    if (x > 0) {
      return x * 2;
    }
    log.push("try end");
  } finally {
    log.push(`finally ${x}`);
  }
  return -1;
}
console.log(early(3), early(0), log.join(" | "));

function fromCatch(): string {
  try {
    throw new Error("bad");
  } catch (e) {
    return `caught ${e.message}`;
  } finally {
    log.push("after catch");
  }
}
console.log(fromCatch(), log[log.length - 1]);

let seen: int[] = [];
for (let i: int = 0; i < 6; i++) {
  try {
    if (i === 1) {
      continue;
    }
    if (i === 4) {
      break;
    }
    seen.push(i);
  } finally {
    seen.push(100 + i);
  }
}
console.log(seen.join(","));

function nested(): int {
  let steps: string[] = [];
  try {
    try {
      return 7;
    } finally {
      steps.push("inner");
    }
  } finally {
    steps.push("outer");
    console.log(steps.join(">"));
  }
}
console.log(nested());

function overrides(): int {
  try {
    return 1;
  } finally {
    log.push("o");
  }
}
console.log(overrides());

function withLocals(n: int): string {
  const items: string[] = ["a", "b"];
  try {
    for (const it of items) {
      if (it === "b") {
        return `${it}${n}`;
      }
    }
    return "none";
  } finally {
    items.push("c");
    console.log("items", items.length);
  }
}
console.log(withLocals(5));
