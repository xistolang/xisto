// The part of Express's Request that request handlers read. Built by a framework (Nest) from xisto's HttpRequest.
export interface IncomingHttpHeaders {
  authorization?: string;
}

export interface Request {
  method: string;
  url: string;
  headers: IncomingHttpHeaders;
  body: string;
}
