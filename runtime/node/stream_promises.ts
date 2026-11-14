// xisto:stream/promises (node:stream/promises): pipeline and finished as promises, over xisto:stream.

import { __finishedPromise, __pipelinePromise } from "xisto:stream";
import type { FinishedOptions, Stream } from "xisto:stream";

/** Pipes the streams into each other; resolves once the last one is done, rejects with the first error. */
export function pipeline(...streams: Stream[]): Promise<void> {
  return __pipelinePromise(streams, true);
}

/** Resolves once the stream is done (ended, finished, closed); rejects with its error. */
export function finished(stream: Stream, options: FinishedOptions | null = null): Promise<void> {
  return __finishedPromise(stream, options);
}
