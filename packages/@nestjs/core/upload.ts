// multipart/form-data uploads: what @nestjs/platform-express's FileInterceptor("file") and @UploadedFile() give
// a route, read from the request body.

/**
 * An uploaded file, with Multer's field names (Express.Multer.File).
 * Compatibility: `buffer` holds the bytes as they arrived; `size` is their count.
 */
export class UploadedFileData {
  fieldname: string = "";
  originalname: string = "";
  encoding: string = "7bit";
  mimetype: string = "application/octet-stream";
  size: int = 0;
  buffer: Uint8Array = new Uint8Array(0);
}

/** What FileInterceptor("field") gives UseInterceptors: the multipart field to read. */
export class FileInterceptorSpec {
  field: string = "";
}

/** The field a FileInterceptor reads ("" when the interceptor is something else). */
export function uploadField(x: unknown): string {
  if (x instanceof FileInterceptorSpec) {
    return (x as FileInterceptorSpec).field;
  }
  return "";
}

function attr(header: string, name: string): string {
  const key = name + "=";
  const at = header.indexOf(key);
  if (at < 0) {
    return "";
  }
  let rest = header.slice(at + key.length, header.length);
  if (rest.startsWith("\"")) {
    const end = rest.indexOf("\"", 1);
    return end < 0 ? rest.slice(1, rest.length) : rest.slice(1, end);
  }
  const semi = rest.indexOf(";");
  return (semi < 0 ? rest : rest.slice(0, semi)).trim();
}

/** The file of `field` in a multipart/form-data request, or null. */
export function readUpload(req: HttpRequest, field: string): UploadedFileData | null {
  const ct = req.header("content-type");
  if (!ct.toLowerCase().startsWith("multipart/form-data")) {
    return null;
  }
  const boundary = attr(ct, "boundary");
  if (boundary.length === 0) {
    return null;
  }
  const body = req.body;
  const delim = "--" + boundary;
  let at = body.indexOf(delim);
  while (at >= 0) {
    let start = at + delim.length;
    if (body.slice(start, start + 2) === "--") {
      return null; // the closing delimiter
    }
    if (body.slice(start, start + 2) === "\r\n") {
      start += 2;
    }
    const headEnd = body.indexOf("\r\n\r\n", start);
    if (headEnd < 0) {
      return null;
    }
    const head = body.slice(start, headEnd);
    const next = body.indexOf("\r\n" + delim, headEnd + 4);
    if (next < 0) {
      return null;
    }
    const content = body.slice(headEnd + 4, next);
    let disposition = "";
    let type = "application/octet-stream";
    for (const line of head.split("\r\n")) {
      const lower = line.toLowerCase();
      if (lower.startsWith("content-disposition:")) {
        disposition = line;
      } else if (lower.startsWith("content-type:")) {
        type = line.slice(13, line.length).trim();
      }
    }
    if (attr(disposition, "name") === field && disposition.includes("filename=")) {
      const f = new UploadedFileData();
      f.fieldname = field;
      f.originalname = attr(disposition, "filename");
      f.mimetype = type;
      f.buffer = new TextEncoder().encode(content);
      f.size = f.buffer.length;
      return f;
    }
    at = next + 2;
  }
  return null;
}
