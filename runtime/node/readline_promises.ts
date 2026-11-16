// xisto:readline/promises (node:readline/promises): createInterface whose question() gives a promise, over
// xisto:readline (runtime/node/readline.ts), and Node's Readline class (cursor moves queued, then commit()).
import { createPromisesInterface, PromisesInterface } from "xisto:readline";
import type { InterfaceOptions } from "xisto:readline";
import { Readable, Writable } from "xisto:stream";

export class Interface extends PromisesInterface {}

/** createInterface({ input, output, ... }) or createInterface(input, output): question() gives a promise. */
export function createInterface(input: InterfaceOptions | Readable, output: Writable | null = null, completer: any = null, terminal: boolean | null = null): PromisesInterface {
  return createPromisesInterface(input, output, completer, terminal);
}

/** Cursor and clearing moves for a stream, sent together by commit() (or at once with autoCommit). */
export class Readline {
  private stream: Writable;
  private autoCommit: boolean;
  private todo: string[] = [];
  constructor(stream: Writable, options: { autoCommit?: boolean } | null = null) {
    this.stream = stream;
    const a = options !== null ? options.autoCommit : undefined;
    this.autoCommit = a !== undefined && a !== null ? (a as boolean) : false;
  }
  private add(seq: string): Readline {
    if (this.autoCommit) this.stream.write(seq);
    else this.todo.push(seq);
    return this;
  }
  clearLine(dir: int): Readline {
    return this.add(dir < 0 ? "\x1b[1K" : dir > 0 ? "\x1b[0K" : "\x1b[2K");
  }
  clearScreenDown(): Readline {
    return this.add("\x1b[0J");
  }
  cursorTo(x: int, y: int = -1): Readline {
    return this.add(y < 0 ? `\x1b[${x + 1}G` : `\x1b[${y + 1};${x + 1}H`);
  }
  moveCursor(dx: int, dy: int): Readline {
    let seq = "";
    if (dx < 0) seq += `\x1b[${-dx}D`;
    else if (dx > 0) seq += `\x1b[${dx}C`;
    if (dy < 0) seq += `\x1b[${-dy}A`;
    else if (dy > 0) seq += `\x1b[${dy}B`;
    return this.add(seq);
  }
  commit(): Promise<void> {
    const s = this.todo.join("");
    this.todo = [];
    return new Promise<void>((resolve: () => void): void => {
      if (s.length === 0) {
        resolve();
        return;
      }
      this.stream.write(s, (e?: Error | null): void => resolve());
    });
  }
  rollback(): Readline {
    this.todo = [];
    return this;
  }
}
