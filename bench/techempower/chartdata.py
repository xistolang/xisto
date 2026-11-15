#!/usr/bin/env python3
"""The TechEmpower charts' data: reads a round's json.txt and plaintext.txt (bench/techempower/run.py) and writes the
JSON the site's charts load (docs/src/data/techempower.json). Every number comes from those files as they are.

usage: python3 bench/techempower/chartdata.py bench/results/techempower-DATE docs/src/data/techempower.json
"""
import json, sys, os

NAMES = {
    'xisto-native': 'Xisto (native)', 'xisto-nest': 'Xisto + NestJS (lock)', 'xisto-nest-procs': 'Xisto + NestJS (2 processes)',
    'xisto-nest-nolock': 'Xisto + NestJS (no lock)', 'actix': 'actix-web (Rust)', 'go-std': 'Go net/http', 'nodejs': 'Node.js http',
    'fastify': 'Fastify (Node.js)', 'bun': 'Bun',
    'nest-node': 'NestJS on Node.js', 'nest-bun': 'NestJS on Bun',
}
# the line charts: six series at most (the categorical slots validated for the site), Xisto first
LINES = ['xisto-native', 'xisto-nest-procs', 'actix', 'bun', 'go-std', 'nodejs']


def read(path):
    rows, crashed = {}, []
    for line in open(path):
        if line.startswith('#'):
            if 'crashed' in line:
                crashed.append(line.split()[3])
            continue
        p = line.split()
        if len(p) < 9 or p[8] != 'ok':
            continue
        test, level = p[0].rsplit('-c', 1)
        rows.setdefault(p[2], {})[int(level)] = {'rps': float(p[3]), 'p50': float(p[4]), 'p99': float(p[5]) or None,
                                                 'cpu': float(p[6]), 'rss': float(p[7])}
    return rows, crashed


def test(path):
    rows, crashed = read(path)
    levels = sorted({l for r in rows.values() for l in r})
    servers = []
    for name, by in rows.items():
        best = max(by, key=lambda l: by[l]['rps'])
        servers.append({'id': name, 'name': NAMES.get(name, name), 'best': by[best]['rps'], 'bestLevel': best,
                        'p99AtBest': by[best]['p99'], 'cpuAtBest': by[best]['cpu'],
                        'peakRss': max(v['rss'] for v in by.values()),
                        'values': [by[l]['rps'] if l in by else None for l in levels],
                        # per level, aligned with 'levels' (None where the server has no valid run): the subpages'
                        # latency, CPU and memory charts and tables
                        'p50': [by[l]['p50'] if l in by else None for l in levels],
                        'p99': [by[l]['p99'] if l in by else None for l in levels],
                        'cpu': [by[l]['cpu'] if l in by else None for l in levels],
                        'rss': [by[l]['rss'] if l in by else None for l in levels]})
    return {'levels': levels, 'servers': servers, 'crashed': [NAMES.get(c, c) for c in crashed], 'lines': LINES}


def main():
    src, out = sys.argv[1], sys.argv[2]
    data = {'source': src.rstrip('/'), 'json': test(os.path.join(src, 'json.txt')),
            'plaintext': test(os.path.join(src, 'plaintext.txt'))}
    env = os.path.join(src, 'env.txt')
    if os.path.exists(env):
        data['env'] = open(env).read().strip().splitlines()
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'w') as f:
        json.dump(data, f, indent=1)
        f.write('\n')


if __name__ == '__main__':
    main()
