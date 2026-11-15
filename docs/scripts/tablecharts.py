#!/usr/bin/env python3
"""The data of the speedup and memory charts on the Benchmarks Game and PolyBench pages, read from the pages' own
tables, so a chart never says something its table does not. Run after editing a table:

    python3 docs/scripts/tablecharts.py   (writes docs/src/data/tablecharts.json)

Each page's tables are keyed by the section heading above them (the machine). For every row: the problem or kernel,
Xisto's time and memory, the other side's time and memory; the charts draw other ÷ Xisto ("times faster").
"""
import json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.join(os.path.dirname(HERE), 'docs', 'benchmarks')
OUT = os.path.join(os.path.dirname(HERE), 'src', 'data', 'tablecharts.json')
PAGES = {
    'bg-vs-rust': ('benchmarks-game/vs-rust.md', 'Rust'),
    'bg-vs-node': ('benchmarks-game/vs-node.md', 'Node'),
    'bg-vs-bun': ('benchmarks-game/vs-bun.md', 'Bun'),
    'bg-vs-go': ('benchmarks-game/vs-go.md', 'Go'),
    'pb-vs-rust': ('polybench/vs-rust.md', 'Rust'),
    'pb-vs-go': ('polybench/vs-go.md', 'Go'),
    'pb-vs-node': ('polybench/vs-node.md', 'Node'),
    'pb-vs-bun': ('polybench/vs-bun.md', 'Bun'),
}


def num(cell):
    """seconds or MB from '**620 ms**', '2.67 s', '1,194 MB'; None when the cell has no value"""
    c = re.sub(r'[*`†²¹]', '', cell).replace(',', '').strip()
    m = re.match(r'^([\d.]+)\s*(ms|s|MB|KB|GB)?$', c)
    if not m:
        return None
    v = float(m.group(1))
    return {'ms': v / 1000, 'KB': v / 1024, 'GB': v * 1024}.get(m.group(2), v)


def name(cell):
    return re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', cell).replace('`', '').replace('*', '').strip()


def tables(text, other):
    out, section, header = {}, None, None
    lo = other.lower()
    for line in text.splitlines():
        if line.startswith('## '):
            section, header = line[3:].strip(), None
            continue
        if not line.startswith('|'):
            header = None
            continue
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        if header is None:
            header = [c.lower() for c in cells]
            continue
        if set(cells[0]) <= set('-: '):
            continue
        h = header

        def col(*names):
            for n in names:
                if n in h:
                    return h.index(n)
            return None
        key = col('problem', 'kernel')
        xt, ot = col('xisto time', 'xisto kernel'), col(f'{lo} time', f'{lo} kernel')
        xm, om = col('xisto mem', 'xisto memory'), col(f'{lo} mem', f'{lo} memory')
        if key is None or xt is None or ot is None or section is None:
            continue
        row = {'name': name(cells[key]), 'x': num(cells[xt]), 'o': num(cells[ot]),
               'xm': num(cells[xm]) if xm is not None else None, 'om': num(cells[om]) if om is not None else None}
        if row['x'] and row['o']:
            rows = out.setdefault(section, [])
            if all(r['name'] != row['name'] for r in rows):  # the first row of a problem (the fastest pair)
                rows.append(row)
    return out


def main():
    data = {}
    for key, (path, other) in PAGES.items():
        p = os.path.join(DOCS, path)
        if os.path.exists(p):
            data[key] = {'other': other, 'sections': tables(open(p).read(), other)}
    with open(OUT, 'w') as f:
        json.dump(data, f, indent=1)
        f.write('\n')
    for k, v in data.items():
        print(k, {s: len(r) for s, r in v['sections'].items()})


if __name__ == '__main__':
    main()
