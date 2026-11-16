// typeof of an `object`: what the value holds (an object holds any JSON value, a text too), as in JavaScript
class Failure {
  readonly body: object;
  constructor(body: object) {
    this.body = body;
  }
  getBody(): object {
    return this.body;
  }
}

const values: object[] = [JSON.parse('"text"'), JSON.parse('{"a":1}'), JSON.parse("[1,2]"), JSON.parse("3.5"), JSON.parse("-2"), JSON.parse("true"), JSON.parse("null")];
for (const v of values) {
  console.log(typeof v, JSON.stringify(v));
}

for (const f of [new Failure("Too Many Requests"), new Failure({ message: "Not here", statusCode: 404 })]) {
  const body = f.getBody();
  let message = "";
  if (typeof body === "string") {
    message = body;
  } else {
    const fields = body as Record<string, unknown>;
    message = String(fields["message"]);
  }
  console.log(typeof f.getBody() === "object", message);
}

const maybe: object | null = null;
console.log(typeof maybe);
