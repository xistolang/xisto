// node:util: parseArgs, styleText, stripVTControlCharacters, promisify/callbackify, debuglog and the small helpers
// (same output as Node)
import {
  parseArgs,
  styleText,
  stripVTControlCharacters,
  toUSVString,
  getSystemErrorName,
  promisify,
  callbackify,
  debuglog,
  TextEncoder,
  TextDecoder,
} from "node:util";

// ---- parseArgs ----
const options = {
  verbose: { type: "boolean", short: "v" },
  file: { type: "string", short: "f" },
  tag: { type: "string", short: "t", multiple: true },
  level: { type: "string", default: "info" },
  dry: { type: "boolean", default: false },
};

function show(args: string[]): void {
  const { values, positionals } = parseArgs({ args, options, allowPositionals: true });
  console.log(JSON.stringify(values), JSON.stringify(positionals));
}

show(["-v", "--file", "a.txt", "build"]);
show(["--file=b.txt", "-t", "x", "--tag", "y", "-ty", "rest", "--", "--not-an-option"]);
show(["-vfconf.json"]);
show(["-f", "-"]);
show(["--level", "debug", "--dry", "one", "two"]);
show([]);

function fails(args: string[], strict: boolean, allowPositionals: boolean): void {
  try {
    const r = parseArgs({ args, options, strict, allowPositionals });
    console.log("ok", JSON.stringify(r.values), JSON.stringify(r.positionals));
  } catch (e) {
    const err = e as Error;
    console.log(err.name, (e as { code: string }).code, "|", err.message);
  }
}

fails(["--unknown"], true, false);
fails(["--unknown"], true, true);
fails(["-x"], true, false);
fails(["--file"], true, false);
fails(["-f"], true, false);
fails(["--verbose=yes"], true, false);
fails(["stray"], true, false);
fails(["--file", "--verbose"], true, false);
fails(["-f", "-v"], true, false);
fails(["--unknown", "--verbose=yes", "stray"], false, true);

const negative = parseArgs({ args: ["--no-color"], options: { color: { type: "boolean" } }, allowNegative: true });
console.log(JSON.stringify(negative.values));

const withTokens = parseArgs({ args: ["-v", "--file", "x", "pos"], options, allowPositionals: true, tokens: true });
const tokens = withTokens.tokens;
if (tokens !== null && tokens !== undefined) {
  for (const t of tokens) console.log(t.kind, t.index, t.name ?? "-", t.rawName ?? "-", t.value ?? "-", t.inlineValue === true ? "true" : t.inlineValue === false ? "false" : "-");
}

// ---- styleText (stdout is not a terminal here: no escapes unless validateStream is false) ----
console.log(styleText("red", "plain"));
console.log(JSON.stringify(styleText("red", "error", { validateStream: false })));
console.log(JSON.stringify(styleText(["bold", "underline", "green"], "ok", { validateStream: false })));
console.log(JSON.stringify(styleText(["none", "grey", "bgBlackBright"], "dim", { validateStream: false })));
try {
  styleText("nope", "x");
} catch (e) {
  console.log((e as Error).name, (e as Error).message.slice(0, 60), (e as Error).message.slice(-16));
}

// ---- stripVTControlCharacters ----
console.log(stripVTControlCharacters("\u001b[31mred\u001b[39m and \u001b[1;4mbold\u001b[0m"));
console.log(stripVTControlCharacters("\u001b]8;;https://example.com\u0007link\u001b]8;;\u0007 done"));
console.log(stripVTControlCharacters("\u001b[2K\u001b[1Gprogress 50%"));
console.log(stripVTControlCharacters("no escapes"));
console.log(JSON.stringify(stripVTControlCharacters("a\u001bb")));
console.log(JSON.stringify(stripVTControlCharacters("\u001b[12345m|\u001b[?25l|\u001b(B|\u009b32m")));

// ---- toUSVString, getSystemErrorName ----
console.log(toUSVString("plain ascii é"));
console.log(getSystemErrorName(-2), getSystemErrorName(-13), getSystemErrorName(-1), getSystemErrorName(-9999));
try {
  getSystemErrorName(2);
} catch (e) {
  console.log((e as Error).name, (e as Error).message);
}

// ---- TextEncoder / TextDecoder from util ----
const bytes = new TextEncoder().encode("olá");
console.log(bytes.length, new TextDecoder().decode(bytes));

// ---- debuglog (NODE_DEBUG is not set: silent) ----
const log = debuglog("app");
log("hidden %d", 1);

// ---- promisify / callbackify ----
function double(n: number, cb: (err: Error | null, result: number) => void): void {
  setTimeout(() => {
    if (n < 0) cb(new Error("negative"), 0);
    else cb(null, n * 2);
  }, 1);
}
async function shout(s: string): Promise<string> {
  if (s === "") throw new Error("empty");
  return s.toUpperCase();
}

async function main(): Promise<void> {
  const doubleAsync = promisify(double);
  console.log(await doubleAsync(21));
  try {
    await doubleAsync(-1);
  } catch (e) {
    console.log("rejected:", (e as Error).message);
  }
  const shoutCb = callbackify(shout);
  await new Promise<void>((resolve) => {
    shoutCb("hey", (err: Error | null, r: string | null) => {
      console.log(err, r);
      shoutCb("", (err2: Error | null, r2: string | null) => {
        console.log(err2 !== null ? err2.message : "no error", r2 ?? "none");
        resolve();
      });
    });
  });
}
main();
