// expect: package 'intl' is not installed
// Intl is not built into xisto: it is the intl package's (xisto add intl). Without it, naming Intl is this error, whose
// help says how to install it.
const f = new Intl.NumberFormat("pt-BR", { style: "currency", currency: "BRL" });
console.log(f.format(1234.5));
