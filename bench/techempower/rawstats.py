#!/usr/bin/env python3
"""Read a round's raw wrk outputs (run.py --raw) again.

  rawstats.py DIR/raw                      every run: req/s, latency distribution, max, errors and timeouts
  rawstats.py DIR/raw --rebuild LOG OUT    rewrite a run.py log as its result file, with p50/p99 taken as the median
                                           of the runs that have them (wrk 4.2 prints some as 0.00us: missing values)

A percentile wrk printed as 0.00us is shown as '-'.
"""
import os
import re
import statistics
import sys

UNIT = {'us': 0.001, 'ms': 1.0, 's': 1000.0, 'm': 60000.0}


def ms(v, u):
    return float(v) * UNIT[u]


def parse(path):
    o = open(path).read()
    r = {'rps': None, 'max': None, 'errors': 0, 'timeouts': 0}
    m = re.search(r'Requests/sec:\s+([\d.]+)', o)
    if m:
        r['rps'] = float(m.group(1))
    m = re.search(r'^\s+Latency\s+([\d.]+)(us|ms|s|m)\s+([\d.]+)(us|ms|s|m)\s+([\d.]+)(us|ms|s|m)', o, re.M)
    if m:
        r['max'] = ms(m.group(5), m.group(6))
    for p in ('50', '75', '90', '99'):
        m = re.search(r'^\s+%s%%\s+([\d.]+)(us|ms|s|m)\s*$' % p, o, re.M)
        r[p] = (ms(m.group(1), m.group(2)) or None) if m else None
    m = re.search(r'Socket errors: connect (\d+), read (\d+), write (\d+), timeout (\d+)', o)
    if m:
        r['errors'] = int(m.group(1)) + int(m.group(2)) + int(m.group(3))
        r['timeouts'] = int(m.group(4))
    m = re.search(r'Non-2xx or 3xx responses: (\d+)', o)
    r['non2xx'] = int(m.group(1)) if m else 0
    return r


def load_dir(d):
    """{(test, level, server): [run dicts in run order]}"""
    out = {}
    for f in sorted(os.listdir(d)):
        m = re.match(r'(json|plaintext)-c(\d+)-(.+)-(\d+)\.txt$', f)
        if m:
            out.setdefault((m.group(1), int(m.group(2)), m.group(3)), []).append((int(m.group(4)), parse(os.path.join(d, f))))
    return {k: [r for _, r in sorted(v)] for k, v in out.items()}


def med(xs):
    xs = [x for x in xs if x is not None]
    return statistics.median(xs) if xs else None


def show(v):
    return '-' if v is None else ('%.0f' % v if v >= 100 else '%.2f' % v)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    runs = load_dir(sys.argv[1])
    if len(sys.argv) == 5 and sys.argv[2] == '--rebuild':
        lines = []
        for line in open(sys.argv[3]):
            p = line.split()
            m = re.match(r'(json|plaintext)-c(\d+)$', p[0]) if p else None
            if m and len(p) == 9:
                rs = runs.get((m.group(1), int(m.group(2)), p[2]), [])
                p50, p99 = med(r['50'] for r in rs), med(r['99'] for r in rs)
                p[4] = '%.3f' % (p50 or 0)
                p[5] = '%.3f' % (p99 or 0)
                lines.append(' '.join(p))
            elif line.startswith('#') or ' crashed ' in line:
                lines.append(line.rstrip('\n') if line.startswith('#') else '# ' + line.rstrip('\n'))
        open(sys.argv[4], 'w').write('\n'.join(lines) + '\n')
        return
    print('%-16s %-18s %3s %9s %8s %8s %8s %8s %8s %6s %6s' % (
        'test', 'server', 'run', 'req/s', 'p50 ms', 'p75', 'p90', 'p99', 'max', 'errs', 'tmo'))
    for (t, c, s), rs in sorted(runs.items()):
        for i, r in enumerate(rs):
            print('%-16s %-18s %3d %9s %8s %8s %8s %8s %8s %6d %6d' % (
                '%s-c%d' % (t, c), s, i + 1, show(r['rps']), show(r['50']), show(r['75']), show(r['90']),
                show(r['99']), show(r['max']), r['errors'], r['timeouts']))


if __name__ == '__main__':
    main()
