#!/usr/bin/env python3
"""A pprof profile (profile.proto, plain or gzip) decoded by hand, no dependencies.

usage: pprof.py FILE                       prints the sample types, the samples (count, stack innermost first)
       pprof.py FILE --share NAME...       prints, for each NAME, the share of the samples whose stack has it
       pprof.py FILE --leaf NAME...        the same, counting only the innermost frame
       pprof.py FILE --check RATIO CPU_MS  the checks of tests/profiler/cpu.ts: heavy/light as measured (RATIO, within
                                           15%), the CPU time as measured (CPU_MS, within 10%), the other frames
"""
import gzip
import sys


def fields(b):
    """(field, wire type, value) of a message: varint as int, length-delimited as bytes"""
    i, n = 0, len(b)
    while i < n:
        key, i = varint(b, i)
        f, wt = key >> 3, key & 7
        if wt == 0:
            v, i = varint(b, i)
        elif wt == 2:
            ln, i = varint(b, i)
            v, i = b[i:i + ln], i + ln
        elif wt == 1:
            v, i = int.from_bytes(b[i:i + 8], "little"), i + 8
        elif wt == 5:
            v, i = int.from_bytes(b[i:i + 4], "little"), i + 4
        else:
            raise ValueError("wire type %d" % wt)
        yield f, wt, v


def varint(b, i):
    r = s = 0
    while True:
        c = b[i]
        i += 1
        r |= (c & 0x7F) << s
        s += 7
        if c < 0x80:
            return r, i


def packed(v, wt):
    if wt == 0:
        return [v]
    out, i = [], 0
    while i < len(v):
        x, i = varint(v, i)
        out.append(x)
    return out


def decode(data):
    if data[:2] == b"\x1f\x8b":
        data = gzip.decompress(data)
    p = {"sample_type": [], "samples": [], "locations": {}, "functions": {}, "strings": [], "period": 0}
    for f, wt, v in fields(data):
        if f == 1:
            vt = dict((g, x) for g, _, x in fields(v))
            p["sample_type"].append((vt.get(1, 0), vt.get(2, 0)))
        elif f == 2:
            locs, vals = [], []
            for g, w, x in fields(v):
                if g == 1: locs += packed(x, w)
                elif g == 2: vals += packed(x, w)
            p["samples"].append((locs, vals))
        elif f == 4:
            loc = {"id": 0, "lines": []}
            for g, w, x in fields(v):
                if g == 1: loc["id"] = x
                elif g == 4:
                    ln = dict((h, y) for h, _, y in fields(x))
                    loc["lines"].append((ln.get(1, 0), ln.get(2, 0)))
            p["locations"][loc["id"]] = loc
        elif f == 5:
            fn = dict((g, x) for g, _, x in fields(v))
            p["functions"][fn.get(1, 0)] = (fn.get(2, 0), fn.get(4, 0), fn.get(5, 0))
        elif f == 6:
            p["strings"].append(v.decode("utf-8", "replace"))
        elif f == 12:
            p["period"] = v
        elif f == 11:
            pass
    s = p["strings"]
    p["sample_type"] = [(s[a], s[b]) for a, b in p["sample_type"]]
    return p


def frames(p, loc_ids):
    """the stack as (function, file, line), innermost first (inlined frames expanded)"""
    s = p["strings"]
    out = []
    for lid in loc_ids:
        for fid, line in p["locations"][lid]["lines"]:
            name, file, _ = p["functions"][fid]
            out.append((s[name], s[file], line))
    return out


def share(p, name, leaf=False):
    total = sum(v[0] for _, v in p["samples"])
    hit = 0
    for locs, vals in p["samples"]:
        fr = frames(p, locs)
        if name in [f[0] for f in (fr[:1] if leaf else fr)]:
            hit += vals[0]
    return hit / total if total else 0


def check(p, ratio, cpu_ms):
    ok = True

    def expect(what, cond, detail):
        nonlocal ok
        print("%s %s (%s)" % ("ok  " if cond else "FAIL", what, detail))
        ok = ok and cond

    expect("sample types", p["sample_type"] == [("sample", "count"), ("cpu", "nanoseconds")], p["sample_type"])
    h, l = share(p, "heavy"), share(p, "light")
    got = h / l if l else 0
    expect("heavy/light within 15%% of %.2f" % ratio, l > 0 and abs(got - ratio) / ratio <= 0.15, "%.2f" % got)
    spin = share(p, "spin", True)
    expect("spin is the leaf of heavy and light", spin >= 0.95 * (h + l), "%.3f of %.3f" % (spin, h + l))
    scan = share(p, "Parser.scan", True)
    expect("a method in a closure", scan > 0 and share(p, "each") >= scan, "%.3f" % scan)
    deep = max([sum(1 for f in frames(p, locs) if f[0] == "fib") for locs, _ in p["samples"]] + [0])
    expect("recursion keeps its frames", deep >= 5, "fib x%d" % deep)
    files = set(f[1] for locs, _ in p["samples"] for f in frames(p, locs) if f[0] in ("heavy", "spin", "Parser.scan"))
    expect("files", files == {"tests/profiler/cpu.ts"}, files)
    cpu = sum(v[1] for _, v in p["samples"]) / 1e6
    expect("CPU time within 10%% of %d ms" % cpu_ms, abs(cpu - cpu_ms) / cpu_ms <= 0.10, "%.0f ms" % cpu)
    return ok


def main():
    p = decode(open(sys.argv[1], "rb").read())
    if len(sys.argv) == 5 and sys.argv[2] == "--check":
        sys.exit(0 if check(p, float(sys.argv[3]), float(sys.argv[4])) else 1)
    total = sum(v[0] for _, v in p["samples"])
    if len(sys.argv) > 3 and sys.argv[2] in ("--share", "--leaf"):
        leaf = sys.argv[2] == "--leaf"
        for name in sys.argv[3:]:
            hit = 0
            for locs, vals in p["samples"]:
                fr = frames(p, locs)
                names = [f[0] for f in (fr[:1] if leaf else fr)]
                if name in names:
                    hit += vals[0]
            print("%s %.3f" % (name, hit / total if total else 0))
        print("total %d" % total)
        return
    print("sample_type", p["sample_type"], "period", p["period"])
    print("samples", total)
    for locs, vals in sorted(p["samples"], key=lambda x: -x[1][0]):
        print(vals, " <- ".join("%s (%s:%d)" % f for f in frames(p, locs)))


if __name__ == "__main__":
    main()
