#!/usr/bin/env python3
"""TechEmpower runner (phase 1: plaintext and json): each server alone, validated, warmed up, then loaded at each
concurrency level; the server's CPU and memory measured from outside.

Per server: build (xisto apps; references come from refs.sh), start in its own process group on its own port
(9100+, never 8080/8443: the xisto test suite uses those), wait for the port, validate (validate.py: an INVALID
server is still measured, and marked), warm up (5 s at the highest level, as the TFB's warmup), then for each level:
  - the load generator: oha (default; no HTTP pipelining) or wrk with the TFB's pipeline.lua and depth 16 for
    plaintext (--gen wrk, what the TFB uses; needs the TFB clone for pipeline.lua);
  - CPU: user+sys of every process of the server's group (ps), before and after, as ms per 1000 responses;
  - RSS: the sum over the group, sampled every 0.2 s during the load; the peak is reported.
Output, one line per (test, server, level), medians over --repeat runs:
  test-cC side name req/s p50_ms p99_ms cpu_ms_per_1k_req rss_MB ok|INVALID
side is xisto or ref. The same lines go to --out (default bench/out/techempower-<date>.txt).

Usage: run.py [--servers a,b] [--tests plaintext,json] [--levels 16,64,256] [--duration 10] [--repeat 1]
              [--gen oha|wrk] [--gen-ssh user@host --server-host IP] [--xisto-workers N] [--refs DIR] [--tfb DIR]
Topologies: by default the generator runs on this machine against 127.0.0.1. With --gen-ssh the generator runs on
another machine over SSH against --server-host (this machine's address as seen from there); CPU and RSS are still
measured here.

Mac note: oha has no pipelining, so plaintext there is measured without it (the TFB pipelines 16 deep).
xisto runs XISTO_WORKERS event loops (default: the number of CPUs, as every reference uses every core); the native
app runs with XISTO_LOCK=0 (its handlers share no state); the Nest app keeps the default lock (shared singletons).
"""
import argparse
import datetime
import json
import os
import re
import signal
import socket
import statistics
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from validate import validate  # noqa: E402

NCPU = os.cpu_count() or 1
ACCEPT = {
    'plaintext': 'text/plain,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,*/*;q=0.7',
    'json': 'application/json,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,*/*;q=0.7',
}
PATHS = {'plaintext': '/plaintext', 'json': '/json'}


def servers(a):
    """name -> (side, port, cwd, argv, env, build argv or None)."""
    X = os.path.join(ROOT, 'build', 'xisto')
    out = os.path.join(ROOT, 'bench', 'out')
    nest = os.path.join(HERE, 'xisto', 'nest')
    R = a.refs
    xw = str(a.xisto_workers)
    return {
        'xisto-native': ('xisto', 9100, ROOT, [os.path.join(out, 'te-native')],
                         {'PORT': '9100', 'XISTO_WORKERS': xw, 'XISTO_LOCK': '0'},
                         [X, 'build', '-o', os.path.join(out, 'te-native'), os.path.join(HERE, 'xisto', 'native', 'main.ts')]),
        'xisto-nest': ('xisto', 9101, nest, [os.path.join(out, 'te-nest')],
                       {'PORT': '9101', 'XISTO_WORKERS': xw},
                       ['sh', '-c', '(test -d .xisto || %s install) && %s build -o %s' % (X, X, os.path.join(out, 'te-nest'))]),
        # the same Nest binary in the two other modes Isa asked for (14 Nov): workers without the handler lock (its
        # singletons are shared: measured as is, the validator says whether the answers stay right), and N
        # processes of one worker each on one port (SO_REUSEPORT: the kernel spreads the connections, as Node's
        # cluster or Bun's reusePort; each process has its own singletons, nothing shared, no lock)
        'xisto-nest-nolock': ('xisto', 9102, nest, [os.path.join(out, 'te-nest')],
                              {'PORT': '9102', 'XISTO_WORKERS': xw, 'XISTO_LOCK': '0'},
                              ['sh', '-c', '(test -d .xisto || %s install) && %s build -o %s' % (X, X, os.path.join(out, 'te-nest'))]),
        'xisto-nest-procs': ('xisto', 9103, nest,
                             ['sh', '-c', 'for i in $(seq %s); do "%s" & done; wait' % (xw, os.path.join(out, 'te-nest'))],
                             {'PORT': '9103', 'XISTO_WORKERS': '1', 'XISTO_REUSEPORT': '1'},
                             ['sh', '-c', '(test -d .xisto || %s install) && %s build -o %s' % (X, X, os.path.join(out, 'te-nest'))]),
        'actix': ('ref', 9110, R + '/actix', [R + '/actix/target/release/tfb-web'], {}, None),
        'go-std': ('ref', 9111, R + '/go-std/src', [R + '/go-std/src/app', '-bind', ':9111'], {}, None),
        'nodejs': ('ref', 9112, R + '/nodejs', ['node', 'app.js'], {'NODE_ENV': 'production'}, None),
        'fastify': ('ref', 9113, R + '/fastify', ['node', 'app.js'], {'NODE_ENV': 'production'}, None),
        'bun': ('ref', 9114, R + '/bun/src', ['bun', 'spawn.ts'], {'NODE_ENV': 'production'}, None),
    }


def group_stats(pgid):
    """(cpu seconds user+sys, rss bytes) summed over the live processes of a process group."""
    if os.path.isdir('/proc/self'):  # Linux: /proc in clock ticks (procps' ps prints whole seconds only)
        tick = os.sysconf('SC_CLK_TCK')
        page = os.sysconf('SC_PAGE_SIZE')
        cpu = 0.0
        rss = 0
        for d in os.listdir('/proc'):
            if not d.isdigit():
                continue
            try:
                f = open('/proc/%s/stat' % d).read().rsplit(')', 1)[1].split()
            except OSError:
                continue
            if int(f[2]) != pgid:  # fields after the name: state ppid pgrp ...
                continue
            cpu += (int(f[11]) + int(f[12])) / tick  # utime stime
            rss += int(f[21]) * page
        return cpu, rss
    o = subprocess.run(['ps', '-A', '-o', 'pgid=,rss=,time='], capture_output=True, text=True).stdout
    cpu = 0.0
    rss = 0
    for line in o.splitlines():
        p = line.split()
        if len(p) != 3 or int(p[0]) != pgid:
            continue
        rss += int(p[1]) * 1024
        t = p[2]
        days = 0
        if '-' in t:
            d, t = t.split('-', 1)
            days = int(d)
        v = 0.0
        for part in t.split(':'):
            v = v * 60 + float(part)
        cpu += v + days * 86400
    return cpu, rss


def wait_port(port, timeout=60):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            socket.create_connection(('127.0.0.1', port), timeout=0.2).close()
            return True
        except OSError:
            time.sleep(0.05)
    return False


def unit_ms(v, u):
    return float(v) * {'us': 0.001, 'ms': 1.0, 's': 1000.0, 'm': 60000.0}[u]


def load(a, test, url, c, dur):
    """(req/s, p50 ms, p99 ms, responses) from one load run."""
    if a.gen == 'oha':
        cmd = ['oha', '-z', '%ds' % dur, '-c', str(c), '--no-tui', '--output-format', 'json',
               '-H', 'Accept: ' + ACCEPT[test], url]
    else:
        threads = min(c, a.gen_threads)
        cmd = ['wrk', '-H', 'Accept: ' + ACCEPT[test], '-H', 'Connection: keep-alive', '--latency',
               '-d', '%ds' % dur, '-c', str(c), '--timeout', '8', '-t', str(threads), url]
        if test == 'plaintext':
            cmd += ['-s', a.pipeline_lua, '--', '16']
    if a.gen_ssh:
        cmd = ['ssh', a.gen_ssh, ' '.join("'%s'" % x for x in cmd)]
    o = subprocess.run(cmd, capture_output=True, text=True).stdout
    if a.gen == 'oha':
        j = json.loads(o)
        s = j['summary']
        lp = j['latencyPercentiles']
        n = sum(j.get('statusCodeDistribution', {}).values())
        return s['requestsPerSec'], (lp['p50'] or 0) * 1000, (lp['p99'] or 0) * 1000, n
    rps = float(re.search(r'Requests/sec:\s+([\d.]+)', o).group(1))
    n = int(re.search(r'(\d+) requests in', o).group(1))
    lat = dict((m.group(1), unit_ms(m.group(2), m.group(3)))
               for m in re.finditer(r'^\s+(50|99)%\s+([\d.]+)(us|ms|s|m)\s*$', o, re.M))
    return rps, lat.get('50', 0.0), lat.get('99', 0.0), n


def measure(a, name, spec, tests, levels):
    side, port, cwd, argv, env, build = spec
    if build and not a.no_build:
        os.makedirs(os.path.join(ROOT, 'bench', 'out'), exist_ok=True)
        r = subprocess.run(build, cwd=cwd, capture_output=True, text=True)
        if r.returncode != 0:
            print('# %s: build failed\n%s' % (name, (r.stdout + r.stderr)[-2000:]), file=sys.stderr)
            return []
    if not os.path.isdir(cwd):
        print('# %s: %s missing (run refs.sh)' % (name, cwd), file=sys.stderr)
        return []
    e = dict(os.environ)
    e.update(env)
    log = open(os.path.join(ROOT, 'bench', 'out', 'te-%s.log' % name), 'w')
    p = subprocess.Popen(argv, cwd=cwd, env=e, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    rows = []
    try:
        if not wait_port(port):
            print('# %s: no answer on port %d' % (name, port), file=sys.stderr)
            return []
        time.sleep(0.5)  # cluster workers and spawned processes come up after the first listener
        host = a.server_host if a.gen_ssh else '127.0.0.1'
        valid = {}
        for t in tests:
            valid[t] = validate('http://127.0.0.1:%d' % port, (t,), out=sys.stderr)
        for t in tests:
            url = 'http://%s:%d%s' % (host, port, PATHS[t])
            load(a, t, url, max(levels), a.warmup)  # warmup
            for c in levels:
                runs = []
                for _ in range(a.repeat):
                    cpu0, _ = group_stats(p.pid)
                    peak = [0]
                    stop = threading.Event()

                    def sample():
                        while not stop.is_set():
                            peak[0] = max(peak[0], group_stats(p.pid)[1])
                            stop.wait(0.2)
                    th = threading.Thread(target=sample, daemon=True)
                    th.start()
                    try:
                        rps, p50, p99, n = load(a, t, url, c, a.duration)
                    except Exception as e:  # (the load generator could not read the server: it died under load)
                        print('# %s: the load failed at %s-c%d (%s)' % (name, t, c, e), file=sys.stderr)
                        return rows
                    finally:
                        stop.set()
                        th.join()
                    cpu1, _ = group_stats(p.pid)
                    runs.append((rps, p50, p99, (cpu1 - cpu0) * 1000 / max(n, 1) * 1000, peak[0] / 1048576))
                    time.sleep(1)
                med = [statistics.median(r[i] for r in runs) for i in range(5)]
                line = '%s-c%d %s %s %.0f %.3f %.3f %.1f %.1f %s' % (
                    t, c, side, name, med[0], med[1], med[2], med[3], med[4], 'ok' if valid[t] else 'INVALID')
                print(line, flush=True)
                rows.append(line)
    finally:
        crashed = p.poll() is not None
        try:
            os.killpg(p.pid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):  # (the group is gone: the server died, macOS says EPERM)
            pass
        try:
            p.wait(5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(p.pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass
            p.wait()
        if crashed:
            line = '%s crashed %s during the load (exit %s): see bench/out/te-%s.log' % (side, name, p.returncode, name)
            print(line, flush=True)
            rows.append('# ' + line)
        time.sleep(1)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--servers', default='xisto-native,xisto-nest,xisto-nest-nolock,xisto-nest-procs,actix,go-std,nodejs,fastify,bun')
    ap.add_argument('--tests', default='plaintext,json')
    ap.add_argument('--levels', default='16,64,256')
    ap.add_argument('--duration', type=int, default=10)
    ap.add_argument('--warmup', type=int, default=5)
    ap.add_argument('--repeat', type=int, default=1)
    ap.add_argument('--gen', choices=['oha', 'wrk'], default='oha')
    ap.add_argument('--gen-threads', type=int, default=NCPU, help='wrk threads (the TFB: nproc of the generator)')
    ap.add_argument('--gen-ssh', default='', help='user@host: run the load generator there over SSH')
    ap.add_argument('--server-host', default='127.0.0.1', help='this machine as the remote generator reaches it')
    ap.add_argument('--xisto-workers', type=int, default=NCPU)
    ap.add_argument('--refs', default=os.path.expanduser('~/workspace/tessarotto/port/tfb-refs'))
    ap.add_argument('--tfb', default=os.path.expanduser('~/workspace/tessarotto/port/FrameworkBenchmarks'))
    ap.add_argument('--pipeline-lua', default='', help="the TFB's pipeline.lua on the generator (default: in --tfb)")
    ap.add_argument('--no-build', action='store_true')
    ap.add_argument('--out', default='')
    a = ap.parse_args()
    a.pipeline_lua = a.pipeline_lua or os.path.join(a.tfb, 'toolset', 'wrk', 'pipeline.lua')
    tests = a.tests.split(',')
    levels = [int(x) for x in a.levels.split(',')]
    all_s = servers(a)
    out = a.out or os.path.join(ROOT, 'bench', 'out', 'techempower-%s.txt' % datetime.date.today())
    os.makedirs(os.path.dirname(out), exist_ok=True)
    header = '# %s gen=%s levels=%s duration=%ds repeat=%d xisto_workers=%d cpus=%d\n' % (
        datetime.datetime.now().isoformat(timespec='seconds'), a.gen, a.levels, a.duration, a.repeat,
        a.xisto_workers, NCPU)
    print(header, end='')
    print('# test side name req/s p50_ms p99_ms cpu_ms_per_1k_req rss_MB valid')
    rows = []
    for name in a.servers.split(','):
        rows += measure(a, name, all_s[name], tests, levels)
    with open(out, 'w') as f:
        f.write(header + '# test side name req/s p50_ms p99_ms cpu_ms_per_1k_req rss_MB valid\n')
        f.write('\n'.join(rows) + '\n')
    print('# saved %s' % out)


if __name__ == '__main__':
    main()
