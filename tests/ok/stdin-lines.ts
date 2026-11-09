// readStdinParts with splitAfter: standard input as lines, each with its line break (the last one may have none);
// tests/run.sh feeds stdin-lines.stdin.
import { readStdinParts } from "xisto:io";

const decoder = new TextDecoder();
for (const line of readStdinParts({ splitAfter: "\n" })) console.log(JSON.stringify(decoder.decode(line)));
