// `reverse(part, ...)` right before `writeStdout(part)` in a loop read in place: the compiler lends the part before
// the reverse (writeStdoutLentBegin), so a big part's front can go out while the rest is turned. Only final bytes go
// out: the output is Node's, header lines, lines of several widths, a part with no body and an empty last line too.
import { readStdinParts, writeStdout } from "xisto:io";
import { byteMap, indexOf, reverse } from "xisto:bytes";

const COMPLEMENT = byteMap("ACGT", "TGCA", { ignoreCase: true });
for (const part of readStdinParts({ splitBefore: ">" })) {
  const body = indexOf(part, "\n") + 1;
  reverse(part, { map: COMPLEMENT, skip: "\n", from: body });
  writeStdout(part);
}
