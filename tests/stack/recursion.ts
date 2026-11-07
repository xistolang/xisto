function down(n: int): int {
  if (n === 0) {
    return (n + 4611686018427387904) * 4;
  }
  return down(n - 1) + 1;
}
console.log(down(3));
