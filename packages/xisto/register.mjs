// Node: `node --import <this file> program.ts` resolves xisto:io, xisto:bytes, xisto:parallel, xisto:collections, xisto:fractal and xisto:random to the reference implementations
// next to this file, and xisto:fs (and the other names xisto gives Node's modules) to node:*.
import { registerHooks } from "node:module";
const here = new URL("./", import.meta.url);
const own = new Set(["io", "bytes", "parallel", "collections", "fractal", "random"]);
registerHooks({
  resolve(specifier, context, next) {
    if (specifier.startsWith("xisto:")) {
      const name = specifier.slice(6);
      if (own.has(name)) return { url: new URL(name + ".ts", here).href, shortCircuit: true };
      return next("node:" + name, context);
    }
    return next(specifier, context);
  },
});
