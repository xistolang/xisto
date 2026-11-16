const xs: string[] = ["\u{1F600}", "", "￿", "a", "é", "\u{10000}", "abcdefgh\u{1F600}", "abcdefgh�", "abcdefghij", "abcdefgh", "", "abé", "abê", "퟿", "zzzzzzzzzzzzzzzzzz1", "zzzzzzzzzzzzzzzzzz0"];
console.log(JSON.stringify(xs.slice().sort()));
