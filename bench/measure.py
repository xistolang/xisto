"""How the runners (bench/vm.py, bench/benchmarksgame/run.py) start a program and read its peak RSS and CPU time.
macOS: os.wait4 on the child. Linux: through bench/rusage.c, because a child's ru_maxrss there starts at its parent's
RSS when it calls exec, so a program started straight from Python would report Python's ~20 MB at least."""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
_launcher = None


def launcher():
    """bench/out/rusage, built once with cc (None on macOS, where wait4 is already right)"""
    global _launcher
    if sys.platform == "darwin":
        return None
    if _launcher is None:
        out = os.path.join(HERE, "out", "rusage")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        subprocess.run(["cc", "-O2", "-o", out, os.path.join(HERE, "rusage.c")], check=True)
        _launcher = out
    return _launcher


def start(cmd, **kw):
    """Popen of cmd, measured: finish(p) gives (status, peak RSS in MB, CPU seconds)"""
    l = launcher()
    if l:
        fd, path = tempfile.mkstemp(prefix="xisto-ru-")
        os.close(fd)
        p = subprocess.Popen([l, path] + cmd, **kw)
        p._ru_file = path
    else:
        p = subprocess.Popen(cmd, **kw)
        p._ru_file = None
    return p


def finish(p):
    _, status, ru = os.wait4(p.pid, 0)
    if p._ru_file is None:
        return status, ru.ru_maxrss / 1048576, ru.ru_utime + ru.ru_stime
    try:
        with open(p._ru_file) as f:
            kb, ut, st = f.read().split()
        return status, int(kb) / 1024, float(ut) + float(st)
    except (OSError, ValueError):  # it did not start
        return status or 127, 0.0, 0.0
    finally:
        os.unlink(p._ru_file)
