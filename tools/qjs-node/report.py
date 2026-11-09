#!/usr/bin/env python3
"""The device report of a command-line tool (docs/DEVICE-REPORTS.md): its package installs this as <command>-report.

On the R36S it runs the tool's own command list (the commands.txt the build compared with Node), each in a fresh folder, timed, with its
peak memory, and writes /roms/<App>/r36s-report.txt for Claude to read: the system, the card, then a line a command. r3i6nos's
Settings > Device reports starts it; over SSH, run `<command>-report`.

package-tool.sh fills in the four values below. Python 3 standard library only (the rootfs has nothing else).
"""
import os
import shlex
import subprocess
import sys
import tempfile
import time

TOOL = "@TOOL@"          # the command, e.g. compose
APP = "@APP@"            # the app's name and its folder on the card, e.g. Composer
VERSION = "@VERSION@"
PACKAGE = "@PACKAGE@"
TIMEOUT = 300            # seconds a command may take before it counts as hung
PROGRESS = os.environ.get("R3I6NOS_REPORT_PROGRESS")   # set by r3i6nos's menu: where to say how far it has got


class Report:
    def __init__(self, folder, how, quiet=False):
        self.folder, self.quiet = folder, quiet
        self.counts = {"PASS": 0, "FAIL": 0, "INFO": 0, "SKIP": 0}
        os.makedirs(folder, exist_ok=True)
        self.path = os.path.join(folder, "r36s-report.txt")
        self.tmp = self.path + ".tmp"
        self.f = open(self.tmp, "w", encoding="ascii", errors="replace", newline="\n")
        self.done_steps, self.steps = 0, 1
        self.raw("# r3i6nos device report\n"
                 f"app: {APP} {VERSION} ({PACKAGE})\n"
                 f"made: {time.strftime('%Y-%m-%d %H:%M:%S')}\n"
                 f"how: {how}\n"
                 f"for: Claude. Copy EASYROMS/{os.path.basename(folder)}/r36s-report.txt to a computer and give it to Claude in the {APP} repo.\n")

    def step(self, doing):
        """Say what is being checked now, for the menu's progress bar (r3i6nos docs/DEVICE-REPORTS.md, "Progress")."""
        self.done_steps += 1
        if not PROGRESS:
            return
        try:
            tmp = PROGRESS + ".tmp"
            with open(tmp, "w") as f:
                f.write(f"step={min(self.done_steps, self.steps)}\nsteps={self.steps}\ndoing={' '.join(doing.split())}\nfile={self.path}\n")
            os.replace(tmp, PROGRESS)
        except OSError:
            pass

    def raw(self, text):
        self.f.write(text)
        if not self.quiet:
            sys.stdout.write(text)
            sys.stdout.flush()

    def line(self, status, key, value):
        key = " ".join(str(key).replace(":", " ").split())
        value = " ".join(str(value).split())
        self.counts[status] += 1
        self.raw(f"{status} {key}: {value}\n")

    def section(self, name):
        self.raw(f"\n== {name} ==\n")

    def close(self):
        c = self.counts
        self.section("summary")
        self.raw(f"PASS {c['PASS']}  FAIL {c['FAIL']}  INFO {c['INFO']}  SKIP {c['SKIP']}\nend\n")
        self.f.flush()
        os.fsync(self.f.fileno())
        self.f.close()
        os.replace(self.tmp, self.path)


def field(path, key):
    try:
        with open(path, encoding="ascii", errors="replace") as f:
            for line in f:
                if line.startswith(key):
                    return line[len(key):].strip(" \t:\n")
    except OSError:
        return None
    return None


def system(r):
    r.section("system")
    u = os.uname()
    r.line("INFO", "kernel", f"{u.sysname} {u.release} {u.machine}")
    part = field("/proc/cpuinfo", "CPU part")
    mhz = field("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "")
    cpu = f"{os.cpu_count()} CPUs"
    if part:
        cpu += f", part {part}"
    if mhz and mhz.isdigit():
        cpu += f", {int(mhz) // 1000} MHz max"
    r.line("INFO", "cpu", cpu)
    total = field("/proc/meminfo", "MemTotal")
    if total:
        r.line("INFO", "memory", f"total {total}, available {field('/proc/meminfo', 'MemAvailable') or '?'}")
    else:
        r.line("SKIP", "memory", "no /proc/meminfo here (not Linux)")


def fstype(path):
    best, found = -1, None
    try:
        with open("/proc/mounts", encoding="ascii", errors="replace") as f:
            for line in f:
                dev, mnt, typ = line.split()[:3]
                if (path == mnt or path.startswith(mnt.rstrip("/") + "/")) and len(mnt) > best:
                    best, found = len(mnt), f"{typ} ({dev} on {mnt})"
    except OSError:
        return None
    return found


def card(r, folder):
    r.section("card")
    r.line("PASS", "mkdir", folder)
    a, b = os.path.join(folder, ".r36s-report-test"), os.path.join(folder, ".r36s-report-test.tmp")
    steps = [
        ("write a file", lambda: open(a, "w").write("first\n")),
        ("rename over a file", lambda: (open(b, "w").write("second\n"), os.replace(b, a))),
        ("read back", lambda: open(a).read() == "second\n" or (_ for _ in ()).throw(OSError("different text"))),
        ("delete", lambda: os.remove(a)),
    ]
    for name, step in steps:
        try:
            step()
            r.line("PASS", name, "ok")
        except OSError as e:
            r.line("FAIL", name, e)
    for p in (a, b):
        try:
            os.remove(p)
        except OSError:
            pass
    fs = fstype(folder)
    r.line("INFO" if fs else "SKIP", "filesystem", fs or "no /proc/mounts here")
    st = os.statvfs(folder)
    r.line("INFO", "free", f"{st.f_bavail * st.f_frsize / 1e9:.1f} GB of {st.f_blocks * st.f_frsize / 1e9:.1f} GB")


def commands(path):
    """(line, slow) for each command: '#' comments and blank lines are left out; '!SLOW ' ones are listed, not run by default."""
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("!SLOW "):
                out.append((line[6:].strip(), True))
            else:
                out.append((line, False))
    return out


def run_commands(r, tool, listed, run_slow):
    r.section("commands")
    r.line("INFO", "what", f"each line of commands.txt run as `{TOOL} <line>` in a fresh folder (o/ for files), timed, with its peak memory")
    work = tempfile.mkdtemp(prefix=f"{TOOL}-report-")
    os.makedirs(os.path.join(work, "o"), exist_ok=True)
    total = 0.0
    worst = (0, "")
    for line, slow in listed:
        key = f"{TOOL} {line}"
        r.step(key[:60])
        if slow and not run_slow:
            r.line("SKIP", key, "slow under QuickJS on a laptop (minutes); run with --all to time it")
            continue
        start = time.monotonic()
        try:
            p = subprocess.Popen([tool] + shlex.split(line), cwd=work, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        except OSError as e:
            r.line("FAIL", key, f"cannot start: {e}")
            continue
        deadline = start + TIMEOUT
        status = rusage = None
        while True:
            pid, status, rusage = os.wait4(p.pid, os.WNOHANG)
            if pid:
                break
            if time.monotonic() > deadline:
                p.kill()
                _, status, rusage = os.wait4(p.pid, 0)
                break
            time.sleep(0.01)
        took = time.monotonic() - start
        total += took
        err = (p.stderr.read() or b"").decode("utf-8", "replace").strip().splitlines()
        p.stderr.close()
        peak_kb = rusage.ru_maxrss if rusage else 0  # KB on Linux
        if peak_kb > worst[0]:
            worst = (peak_kb, key)
        if took > TIMEOUT:
            r.line("FAIL", key, f"still running after {TIMEOUT} s, stopped; peak {peak_kb} KB")
        elif os.WIFSIGNALED(status):
            r.line("FAIL", key, f"killed by signal {os.WTERMSIG(status)} after {took:.2f} s; peak {peak_kb} KB; {err[-1] if err else ''}")
        else:
            code = os.WEXITSTATUS(status)
            note = f"; stderr: {err[-1][:120]}" if (code and err) else ""
            r.line("PASS", key, f"{took:.2f} s, peak {peak_kb} KB, exit {code}{note}")
    r.line("INFO", "total time", f"{total:.1f} s")
    if worst[0]:
        r.line("INFO", "most memory", f"{worst[0]} KB ({worst[1]})")
    subprocess.run(["rm", "-rf", work], check=False)


def main(argv):
    run_slow = "--all" in argv
    how = "Settings > Device reports" if "--from-settings" in argv else f"{TOOL}-report{' --all' if run_slow else ''}"
    quiet = "--quiet" in argv
    here = os.path.dirname(os.path.realpath(__file__))
    tool = os.environ.get("R3I6NOS_REPORT_TOOL") or os.path.join(here, TOOL)
    listing = os.environ.get("R3I6NOS_REPORT_COMMANDS") or os.path.join(here, "..", "share", PACKAGE, "commands.txt")
    folder = os.path.join(os.environ.get("R3I6NOS_ROMS", "/roms"), APP)
    try:
        r = Report(folder, how, quiet)
    except OSError as e:
        print(f"{TOOL}-report: cannot write in {folder}: {e}", file=sys.stderr)
        return 1
    listed = commands(listing) if os.path.exists(listing) else []
    r.steps = 2 + len(listed)
    r.step("the system")
    system(r)
    r.step("the card")
    card(r, folder)
    if os.path.exists(listing):
        run_commands(r, tool, listed, run_slow)
    else:
        r.section("commands")
        r.line("FAIL", "commands.txt", f"not found at {listing}")
    r.close()
    r.step("done")
    if not quiet:
        print(f"\n{TOOL}-report: written to {r.path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
