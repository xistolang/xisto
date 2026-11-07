"""A program's memory, CPU and energy over its run, sampled every INTERVAL seconds (every 0.1 s
in the first second), for charts. One CSV row per
sample:
  runtime,t_s,rss_mb,cpu_s,cpu_pct,watts,joules
- t_s: seconds since the start.
- rss_mb: the resident memory at that moment.
- cpu_s: the user + system CPU used so far.
- cpu_pct: the CPU in that interval (100 = one core busy).
- watts: the machine's power in that interval, above the idle baseline (measured before the run).
- joules: the energy above idle so far.
The last row is the end of the run (t_s = wall time, cpu_s and joules the totals).
Memory and CPU are the process's own: Linux reads /proc/PID, macOS proc_pid_rusage (libproc).
Energy is the whole machine's, so the rest of the machine should be quiet. It comes from:
- macOS: powermetrics, which needs root;
- Linux: RAPL (/sys/class/powercap/intel-rapl:0/energy_uj, readable by root on recent kernels).
Without either (cloud VMs expose no sensor) the two columns are empty: no estimate stands in for a measurement.
Used by bench/benchmarksgame/trace.sh. Alone:
  python3 bench/trace.py OUT.csv LABEL [--interval S] [--stdin FILE] -- PROGRAM [ARGS...]"""
import ctypes, ctypes.util, os, re, subprocess, sys, time

if sys.platform == "darwin":
    _lib = ctypes.CDLL(ctypes.util.find_library("proc") or "/usr/lib/libproc.dylib")

    class _RU(ctypes.Structure):  # struct rusage_info_v2 (sys/resource.h), the fields up to the ones read here
        _fields_ = [("uuid", ctypes.c_uint8 * 16), ("user", ctypes.c_uint64), ("system", ctypes.c_uint64),
                    ("pkg_idle_wkups", ctypes.c_uint64), ("interrupt_wkups", ctypes.c_uint64),
                    ("pageins", ctypes.c_uint64), ("wired", ctypes.c_uint64), ("resident", ctypes.c_uint64),
                    ("phys_footprint", ctypes.c_uint64), ("start", ctypes.c_uint64), ("exit", ctypes.c_uint64),
                    ("rest", ctypes.c_uint64 * 32)]

    class _TB(ctypes.Structure):
        _fields_ = [("numer", ctypes.c_uint32), ("denom", ctypes.c_uint32)]
    _tb = _TB()
    ctypes.CDLL("/usr/lib/libSystem.dylib").mach_timebase_info(ctypes.byref(_tb))
    _TICK = _tb.numer / _tb.denom / 1e9  # ri_user_time / ri_system_time are in mach time units

    def sample(pid):
        ru = _RU()
        if _lib.proc_pid_rusage(pid, 2, ctypes.byref(ru)) != 0:
            return None
        return ru.resident / 1048576, (ru.user + ru.system) * _TICK
else:
    _HZ = os.sysconf("SC_CLK_TCK")
    _PAGE = os.sysconf("SC_PAGE_SIZE")

    def sample(pid):
        try:
            with open(f"/proc/{pid}/stat") as f:
                st = f.read().rsplit(")", 1)[1].split()
            with open(f"/proc/{pid}/statm") as f:
                rss_pages = int(f.read().split()[1])
        except (OSError, IndexError, ValueError):
            return None
        return rss_pages * _PAGE / 1048576, (int(st[11]) + int(st[12])) / _HZ  # utime, stime (fields 14, 15)


RAPL = "/sys/class/powercap/intel-rapl:0/energy_uj"


class Energy:
    """the machine's energy so far, in joules (read()), from RAPL or powermetrics; None when there is no sensor"""

    @staticmethod
    def open(interval):
        if sys.platform != "darwin":
            try:
                with open(RAPL) as f:
                    int(f.read())
                return Energy("rapl", interval)
            except (OSError, ValueError):
                return None
        if os.geteuid() != 0 or not os.path.exists("/usr/bin/powermetrics"):
            return None
        return Energy("pm", interval)

    def __init__(self, kind, interval):
        self.kind = kind
        if kind == "rapl":
            self.wrap = int(open(os.path.join(os.path.dirname(RAPL), "max_energy_range_uj")).read())
            self.last, self.total = int(open(RAPL).read()), 0.0
        else:  # powermetrics writes one sample per interval: its power holds for that interval
            self.ms = max(100, int(interval * 1000 / 4))  # (several samples per row: finer integration)
            self.file = f"/tmp/xisto-pm-{os.getpid()}.txt"
            self.pm = subprocess.Popen(["/usr/bin/powermetrics", "--samplers", "cpu_power", "-i", str(self.ms),
                                        "-o", self.file], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self.t0 = time.perf_counter()
            time.sleep(0.3)

    def read(self, now):
        """joules from the start (RAPL) or the samples so far (powermetrics) up to the moment now"""
        if self.kind == "rapl":
            v = int(open(RAPL).read())
            self.total += ((v - self.last) % self.wrap) / 1e6
            self.last = v
            return self.total
        w = self.watts()
        j, end = 0.0, now - self.t0
        for i, x in enumerate(w):  # sample i covers [i, i+1) * ms since powermetrics started
            a, b = i * self.ms / 1000, (i + 1) * self.ms / 1000
            if a >= end:
                break
            j += x * (min(b, end) - a)
        return j

    def watts(self):
        try:
            text = open(self.file).read()
        except OSError:
            return []
        m = re.findall(r"Combined Power \(CPU \+ GPU \+ ANE\): (\d+) mW", text) or re.findall(r"CPU Power: (\d+) mW", text)
        return [int(v) / 1000 for v in m]

    def close(self):
        if self.kind == "pm":
            self.pm.terminate()
            self.pm.wait()
            os.unlink(self.file)


def idle_watts(interval, secs=3.0):
    """the machine's power with nothing of ours running, the baseline the rows are measured above"""
    e = Energy.open(interval)
    if not e:
        return None
    t0 = time.perf_counter()
    j0 = e.read(t0)
    time.sleep(secs)
    t1 = time.perf_counter()
    if e.kind == "pm":
        time.sleep(e.ms / 1000 * 1.5)  # (the last sample is written after its interval)
    j = e.read(t1) - j0 if e.kind == "rapl" else e.read(t1) - e.read(t0)
    e.close()
    return j / (t1 - t0)


def trace(label, cmd, interval=1.0, stdin=None, idle=None):
    """runs cmd (output thrown away) and returns its samples as (t_s, rss_mb, cpu_s, cpu_pct, watts, joules)
    tuples; watts and joules are above the idle baseline `idle` (W), None without a sensor"""
    en = Energy.open(interval) if idle is not None else None
    f = open(stdin, "rb") if stdin else subprocess.DEVNULL
    t0 = time.perf_counter()
    j_start = en.read(t0) if en else 0.0
    p = subprocess.Popen(cmd, stdin=f, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    rows, last_t, last_c, last_j = [], 0.0, 0.0, 0.0

    def power(now, t):  # (watts in the interval, joules so far), above idle
        nonlocal last_j
        if not en:
            return None, None
        j = max(en.read(now) - j_start - idle * t, 0.0)
        w = (j - last_j) / max(t - last_t, 1e-9)
        last_j = j
        return max(w, 0.0), j
    nxt = t0 + min(interval, 0.1)
    while True:
        try:
            pid, status, ru = os.wait4(p.pid, os.WNOHANG)
        except ChildProcessError:
            break
        if pid:
            wall = time.perf_counter() - t0
            cpu = ru.ru_utime + ru.ru_stime
            rss = rows[-1][1] if rows else 0.0  # (gone: its last resident size)
            if en and en.kind == "pm":
                time.sleep(en.ms / 1000 * 1.5)  # (the sample that covers the end is written after it)
            rows.append((wall, rss, cpu, 100 * (cpu - last_c) / max(wall - last_t, 1e-9)) + power(t0 + wall, wall))
            break
        now = time.perf_counter()
        if now >= nxt:
            s = sample(p.pid)
            if s:
                t = now - t0
                pct = 100 * (s[1] - last_c) / max(t - last_t, 1e-9)
                rows.append((t, s[0], s[1], pct) + power(now, t))
                last_t, last_c = t, s[1]
            nxt += interval if now - t0 >= 1.0 else min(interval, 0.1)  # (finer in the first second: short runs get a curve too)
        time.sleep(min(0.01, max(0.0, nxt - time.perf_counter())))
    if stdin:
        f.close()
    if en:
        en.close()
    return rows


def write(path, label, rows, mode="a"):
    new = not os.path.exists(path) or mode == "w"
    with open(path, mode) as out:
        if new:
            out.write("runtime,t_s,rss_mb,cpu_s,cpu_pct,watts,joules\n")
        for t, rss, cpu, pct, w, j in rows:
            e = f"{w:.2f},{j:.3f}" if w is not None else ","
            out.write(f"{label},{t:.3f},{rss:.2f},{cpu:.3f},{pct:.1f},{e}\n")


if __name__ == "__main__":
    a = sys.argv[1:]
    if "--" not in a or len(a) < 4:
        sys.exit(__doc__)
    cut = a.index("--")
    out, label, opts, cmd = a[0], a[1], a[2:cut], a[cut + 1:]
    interval = float(opts[opts.index("--interval") + 1]) if "--interval" in opts else 1.0
    stdin = opts[opts.index("--stdin") + 1] if "--stdin" in opts else None
    write(out, label, trace(label, cmd, interval, stdin, idle_watts(interval)))
