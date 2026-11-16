// expect: setting the length of 'xs' to 5 makes it longer (its length is 3 here): xisto arrays do not grow by their length
// Isa's decision (array-sparse-push): an array does not grow by setting its length; known here, so a compile error
const xs = new Array(3).fill(0);
xs.length = 5;
console.log(xs.length);
