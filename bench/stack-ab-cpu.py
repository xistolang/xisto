#!/usr/bin/env python3
"""Cost of stack-trace support, measured as process CPU time (user+sys from wait4), which other load on the
machine barely moves, unlike wall time. Same compiler with and without support (XISTO_NO_TRACE=1), runs
interleaved. HTTP: server CPU per 100k requests under oha."""
import os, subprocess, statistics, sys, json, time, signal, shutil
X = './build/xisto'; O = 'bench/out/stack-ab'; RUNS = int(os.environ.get('RUNS', '21'))
os.makedirs(O, exist_ok=True)
def build(src, out, be, off):
    env = dict(os.environ)
    if off: env['XISTO_NO_TRACE'] = '1'
    shutil.rmtree(os.path.join(os.path.dirname(src), '.xisto'), ignore_errors=True)
    subprocess.run([X, 'build', '--backend=' + be, '-o', out, src], check=True, env=env, capture_output=True)
def cpu(exe, args=()):
    p = subprocess.Popen([exe, *args], stdout=subprocess.DEVNULL)
    _, _, ru = os.wait4(p.pid, 0)
    return ru.ru_utime + ru.ru_stime
print(f"== CPU time of the program (user+sys, {RUNS} interleaved runs): median / min, seconds")
rows = []
for prog in ['fib', 'sieve', 'nbody', 'mandelbrot', 'objects']:
    for be in ['c', 'qbe']:
        src = f'bench/programs/{prog}.ts'
        on, off = f'{O}/{prog}-{be}-on', f'{O}/{prog}-{be}-off'
        build(src, on, be, False); build(src, off, be, True)
        t = {'on': [], 'off': []}
        for i in range(RUNS):
            for v, exe in (('on', on), ('off', off)) if i % 2 == 0 else (('off', off), ('on', on)):
                t[v].append(cpu(exe))
        mo, mf = statistics.median(t['on']), statistics.median(t['off'])
        print(f"{prog:<11} {be:<4} with {mo:7.4f} / {min(t['on']):7.4f}   without {mf:7.4f} / {min(t['off']):7.4f}   median delta {100*(mo/mf-1):+5.1f}%  min delta {100*(min(t['on'])/min(t['off'])-1):+5.1f}%", flush=True)
print("== HTTP server CPU per 100k requests (oha c64, 5 s, 3 interleaved runs each)")
def http(exe, path, n=3):
    res = []
    for _ in range(n):
        p = subprocess.Popen([exe], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); time.sleep(0.4)
        if 'todo' in exe:
            for j in range(1, 51): subprocess.run(['curl', '-s', '-o', '/dev/null', '-X', 'POST', '-d', '{"title":"t%d"}' % j, 'http://127.0.0.1:8080/todos'])
        r = subprocess.run(['oha', '-z', '5s', '-c', '64', '--no-tui', '--output-format', 'json', 'http://127.0.0.1:8080' + path], capture_output=True, text=True)
        reqs = json.loads(r.stdout)['summary']
        total = reqs['requestsPerSec'] * reqs['total']
        p.send_signal(signal.SIGTERM); _, _, ru = os.wait4(p.pid, 0)
        res.append((ru.ru_utime + ru.ru_stime) / total * 1e5)
    return res
for name, src, path in (('ping', 'bench/http/server.xisto.ts', '/ping'), ('todo list', 'bench/http/todo/server.xisto.ts', '/todos')):
    tag = name.split()[0]; build(src, f'{O}/srv-{tag}-on', 'c', False); build(src, f'{O}/srv-{tag}-off', 'c', True)
    on, off = [], []
    for i in range(3):
        on += http(f'{O}/srv-{tag}-on', path, 1); off += http(f'{O}/srv-{tag}-off', path, 1)
    print(f"{name:<10} with {statistics.median(on):.3f} s   without {statistics.median(off):.3f} s   delta {100*(statistics.median(on)/statistics.median(off)-1):+5.1f}%   (runs: {', '.join('%.3f' % x for x in on)} | {', '.join('%.3f' % x for x in off)})", flush=True)
