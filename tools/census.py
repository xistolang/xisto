#!/usr/bin/env python3
"""Census of a TypeScript project before porting it to Xisto: which libraries, decorators and dynamic
patterns it uses, with counts and locations. Reads files only.
Usage: tools/census.py <project dir>   (scans src/**/*.ts, skips *.spec.ts)"""
import os, re, sys, collections
root = sys.argv[1]
files = []
for d, _, fs in os.walk(os.path.join(root, 'src')):
    files += [os.path.join(d, f) for f in fs if f.endswith('.ts') and not f.endswith('.spec.ts')]
imports, decorators, dynamic = collections.Counter(), collections.Counter(), collections.defaultdict(list)
lines = 0
pats = {
    'any': r':\s*any\b|<any>',
    'untyped parameter': r'@\w+\([^)]*\)\s+\w+\s*[,)]|\(\s*query\s*\)|\(\s*where\s*\)|\(\s*user\s*\)',
    'delete obj.field': r'\bdelete\s+\w+\.\w+',
    'Object.assign': r'Object\.assign',
    "'key' in obj": r"'\w+'\s+in\s+\w+",
    'require()': r'\brequire\(',
    'dynamic index obj[k]': r'\w+\[(data|key|prop|constraint\[0\])\]',
    'destructuring': r'(const|let)\s*\{[^}]*\}\s*=',
    'spread/rest': r'\.\.\.\w',
    'Date': r'\bnew Date\b|:\s*Date\b',
    'query builder': r'createQueryBuilder',
    'relations / eager': r'relations:|eager:|@(ManyToOne|OneToMany|ManyToMany|JoinTable|JoinColumn)',
    'lifecycle hooks': r'@(BeforeInsert|BeforeUpdate|AfterUpdate)',
}
for f in sorted(files):
    src = open(f).read()
    lines += src.count('\n') + 1
    rel = os.path.relpath(f, root)
    for m in re.finditer(r"""(?:from|require\()\s*['"]([^'"]+)['"]""", src):
        mod = m.group(1)
        if not mod.startswith('.'): imports[mod.split('/')[0] if not mod.startswith('@') else '/'.join(mod.split('/')[:2])] += 1
    for m in re.finditer(r'@([A-Z]\w*)\s*\(', src): decorators[m.group(1)] += 1
    for name, p in pats.items():
        for m in re.finditer(p, src):
            dynamic[name].append('%s:%d' % (rel, src.count('\n', 0, m.start()) + 1))
print(f'# Census of {os.path.basename(os.path.abspath(root))}: {len(files)} files, {lines} lines\n')
print('## Packages (import sites)\n')
for k, v in imports.most_common(): print(f'- `{k}`: {v}')
print('\n## Decorators (uses)\n')
for k, v in decorators.most_common(): print(f'- `@{k}`: {v}')
print('\n## Dynamic or untyped patterns\n')
for k, v in sorted(dynamic.items(), key=lambda kv: -len(kv[1])):
    print(f'- {k}: {len(v)} ({", ".join(v[:6])}{", ..." if len(v) > 6 else ""})')
