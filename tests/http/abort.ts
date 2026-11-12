// fetch with an AbortController aborted by a timer while the answer is late, inside a handler (where the app's
// services fetch): the fetch rejects with an AbortError, as Node's
export async function abortFetch(req: HttpRequest): Promise<HttpResponse> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 100);
  try {
    const r: Response = await fetch("http://127.0.0.1:8080/slow?ms=600", { method: "GET", signal: controller.signal });
    return HttpResponse.text(`no abort? ${r.status}`);
  } catch (err) {
    const isAbort = err instanceof Error && err.name === "AbortError";
    return HttpResponse.text(`abort: ${isAbort} ${err.name} ${err.message} ${controller.signal.aborted}`);
  } finally {
    clearTimeout(timer);
  }
}
