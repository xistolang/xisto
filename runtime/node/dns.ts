// xisto:dns (node:dns): lookup() through the system resolver (getaddrinfo), as Node's dns.lookup, and
// dns.promises.lookup. Differences from Node (docs/docs/guides/node-compatibility.md): resolve4/resolve6 also go
// through the system resolver (Node asks a DNS server directly), and the record queries (resolveMx, resolveTxt,
// reverse...) are missing.

declare function dns_lookup_all(host: string, family: int): string[];

/** One address of lookup({ all: true }). */
export class LookupAddress {
  address: string;
  family: int;
  constructor(address: string, family: int) {
    this.address = address;
    this.family = family;
  }
}

/** The options of lookup(). */
export interface LookupOptions {
  family?: int;
  all?: boolean;
}

/** A lookup that found nothing: Node's ENOTFOUND. */
export class DnsError extends Error {
  code: string;
  syscall: string;
  hostname: string;
  constructor(code: string, syscall: string, hostname: string) {
    super(`${syscall} ${code} ${hostname}`);
    this.code = code;
    this.syscall = syscall;
    this.hostname = hostname;
  }
}

function familyOf(a: string): int {
  return a.includes(":") ? 6 : 4;
}
function addresses(hostname: string, family: int): LookupAddress[] {
  const out: LookupAddress[] = [];
  const seen: Set<string> = new Set<string>();
  for (const a of dns_lookup_all(hostname, family)) {
    if (seen.has(a)) continue;
    seen.add(a);
    out.push(new LookupAddress(a, familyOf(a)));
  }
  return out;
}

/** The first address of the name (callback(err, address, family)), after the current code. */
export function lookup(hostname: string, callback: (err: Error | null, address: string, family: int) => void): void {
  const found = addresses(hostname, 0);
  setImmediate(() => {
    if (found.length === 0) callback(new DnsError("ENOTFOUND", "getaddrinfo", hostname), "", 0);
    else callback(null, found[0].address, found[0].family);
  });
}

/** Every IPv4 address of the name. */
export function resolve4(hostname: string, callback: (err: Error | null, addresses: string[]) => void): void {
  const found = addresses(hostname, 4);
  setImmediate(() => {
    const out: string[] = [];
    for (const a of found) out.push(a.address);
    if (out.length === 0) callback(new DnsError("ENOTFOUND", "queryA", hostname), out);
    else callback(null, out);
  });
}

/** Every IPv6 address of the name. */
export function resolve6(hostname: string, callback: (err: Error | null, addresses: string[]) => void): void {
  const found = addresses(hostname, 6);
  setImmediate(() => {
    const out: string[] = [];
    for (const a of found) out.push(a.address);
    if (out.length === 0) callback(new DnsError("ENOTFOUND", "queryAaaa", hostname), out);
    else callback(null, out);
  });
}

/** dns.promises: lookup() as a promise. */
export class DnsPromises {
  /** The first address ({ address, family }), or every one with { all: true }: use lookupAll for that. */
  async lookup(hostname: string, options: LookupOptions | null = null): Promise<LookupAddress> {
    const family: int = options !== null && options.family !== null ? (options.family as int) : 0;
    const found = addresses(hostname, family);
    if (found.length === 0) throw new DnsError("ENOTFOUND", "getaddrinfo", hostname);
    return found[0];
  }
  /** lookup(hostname, { all: true }). */
  async lookupAll(hostname: string): Promise<LookupAddress[]> {
    const found = addresses(hostname, 0);
    if (found.length === 0) throw new DnsError("ENOTFOUND", "getaddrinfo", hostname);
    return found;
  }
  async resolve4(hostname: string): Promise<string[]> {
    const out: string[] = [];
    for (const a of addresses(hostname, 4)) out.push(a.address);
    if (out.length === 0) throw new DnsError("ENOTFOUND", "queryA", hostname);
    return out;
  }
}

export const promises: DnsPromises = new DnsPromises();
