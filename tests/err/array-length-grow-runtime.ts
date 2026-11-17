// expect: runtime error: RangeError: xisto cannot make an array longer by setting its length (from 2 to 3): add the elements with push(...), or make the array at its size with new Array(n).fill(value)
// the length known only at run time: the RangeError then, with what to write instead
function grow(xs: number[], n: int): void {
  xs.length = n;
}
const a = [1, 2];
grow(a, 1);
grow(a, 1);
a.push(9);
grow(a, 3);
console.log(a);
