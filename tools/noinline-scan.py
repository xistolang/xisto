#!/usr/bin/env python3
"""Lists runtime functions that can fail (panic/throw/new Error) or call back into user code (closures),
directly or through other runtime functions. They must not be inlined into user code: a stack trace maps a
return address to the label right after a call in user code, and an inlined body has no such label.
Usage: tools/noinline-scan.py runtime/*.h   (prints the names; --check fails if one lacks XS_NOINLINE)"""
import re, sys
check = '--check' in sys.argv
files = [a for a in sys.argv[1:] if not a.startswith('--')]
defs = {}   # name -> (file, line index, body text, header line)
for f in files:
    lines = open(f).read().split('\n')
    i = 0
    while i < len(lines):
        ln = lines[i]
        head = ln.split('{', 1)[0]
        m = re.match(r'^\s*((?:static|XS_AI|XS_NOINLINE|__attribute__\(\([^)]*\)\))[^;=(]*?)\b([A-Za-z_][\w#]*)\s*\(([^;]*)$', head)
        if m and '{' in ln and not ln.strip().startswith('#define'):
            name = m.group(2)
            depth = 0; body = []; j = i
            while j < len(lines):
                t = lines[j]
                depth += t.count('{') - t.count('}')
                body.append(t)
                if depth <= 0 and j > i or (depth == 0 and t.count('{')):
                    break
                j += 1
            defs[name] = (f, i, '\n'.join(body), ln)
            i = j + 1
            continue
        i += 1
seed_pat = re.compile(r'\b(xs_panic|xs_panicf|xs_panic_at|xs_throw|xs_Error_new|xs_json_fail)\s*\(|->fn\)\s*\(')
def seeds(body):
    body = body.replace('xs_panic("out of memory")', '')  # out of memory needs no source line
    return seed_pat.search(body)
bad = {n for n, d in defs.items() if seeds(d[2].split('\n', 1)[1] if '\n' in d[2] else d[2])}
skip = {'xs_panic', 'xs_panicf', 'xs_panic_at', 'xs_throw', 'xs_trace_capture'}
# reach a failing function only through a path that cannot fail: name##_new's "invalid array length" with the
# length of a literal or of a slice of an existing array
cannot_fail = {'name##_lit', 'xs_arr_slice_##name', 'xs_arr_slice_xs_arr_ref'}
changed = True
while changed:
    changed = False
    for n, d in defs.items():
        if n in bad: continue
        body = (d[2].split('\n', 1)[1] if '\n' in d[2] else d[2]).replace('xs_panic("out of memory")', '')
        for b in list(bad - skip):
            # macro-generated names: "xs_arr_test_##name" is called as written inside the macro body
            pat = re.escape(b).replace(re.escape('##'), r'(?:##)?\w*')
            if re.search(r'\b' + pat + r'\s*\(', body):
                bad.add(n); changed = True; break
# only "oom" panics do not need a site: allocation helpers stay inlinable
oom_only = {n for n in bad if re.findall(r'xs_panic\("([^"]*)"\)', defs[n][2]) and all(x == 'out of memory' for x in re.findall(r'xs_panic\("([^"]*)"\)', defs[n][2])) and not re.search(r'xs_panicf|xs_throw|->fn\)', defs[n][2])}
res = sorted((bad - skip - oom_only - cannot_fail))
missing = [n for n in res if 'XS_NOINLINE' not in defs[n][3] and 'noinline' not in defs[n][3]]
if check:
    for n in missing: print('needs XS_NOINLINE:', n, defs[n][0] + ':' + str(defs[n][1] + 1))
    sys.exit(1 if missing else 0)
for n in res: print(n, defs[n][0] + ':' + str(defs[n][1] + 1), '' if n in missing else '(ok)')
