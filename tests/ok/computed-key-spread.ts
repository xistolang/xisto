// an object with a computed key and a conditional spread (a find order: { [field]: dir, ...(c ? {} : { cd: 'ASC' }) })
function order(field: string, dir: string): object {
  return { [field]: dir, ...(field === "cd" ? {} : { cd: "ASC" }) };
}
console.log(JSON.stringify(order("nome", "DESC")), JSON.stringify(order("cd", "DESC")));
