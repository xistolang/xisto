function inner(n: int): int {
  if (n > 2) {
    throw new Error("too big");
  }
  return n;
}

function middle(n: int): int {
  const v = inner(n);
  return v + 1;
}

function guarded(n: int): int {
  try {
    return middle(n);
  } catch (e) {
    const fallback: int = -1;
    return fallback;
  }
}

const nums: int[] = [1, 2];
const doubled = nums.map((x: int): int => {
  const d = x * 2;
  return d;
});
console.log(guarded(5), doubled);
console.log("end");
