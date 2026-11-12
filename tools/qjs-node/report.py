#!/usr/bin/env python3
"""The device report of a command-line tool (docs/DEVICE-REPORTS.md): its package installs this as <command>-report.

On the R36S it runs the tool's own command list (the commands.txt the build compared with Node), each in a fresh folder, timed, with its
peak memory, and writes /roms/<App>/r36s-report.txt for Claude to read: the system, the card, then a line a command. r3i6nos's
Settings > Device reports starts it; over SSH, run `<command>-report`.

After the commands it writes a `== power ==` section (CLAUDE.md rule 14, docs/POWER.md "In a command-line tool"). A tool has no idle state (it is not running
between commands), so only the working one is measured, from the commands above: each command's CPU seconds, wake-ups a second (the child's own context switches),
peak memory and, on battery, the draw in mW read once a second, against a short idle baseline taken first. Overrides, for tests: R3I6NOS_PROC, R3I6NOS_SYS,
R3I6NOS_REPORT_POWER_S (the idle baseline, 5 s).

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
PROC = os.environ.get("R3I6NOS_PROC", "/proc")
SYS = os.environ.get("R3I6NOS_SYS", "/sys")
POWER_IDLE_S = float(os.environ.get("R3I6NOS_REPORT_POWER_S", "5"))   # the idle baseline the draw is compared with
GOAL_WAKEUPS = 200.0     # context switches a second a command that runs a second or more may cause: a first guess, set before any measurement (docs/POWER.md)
LONG_S = 1.0             # a command shorter than this is dominated by its start-up and too short for the battery's once-a-second reading


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
    # The output route, from the root helper's own status (docs/SOUND.md): the codec's Playback Path and the jack.
    try:
        out = subprocess.run([os.environ.get("R3I6NOS_SOUND") or "r3i6nos-sound", "status"], capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        out = ""
    shown = 0
    for ln in out.splitlines():
        if " playback path: " in ln or ln.startswith(("output: ", "headphones: ", "jack: ")):
            key, _, val = ln.partition(": ")
            r.line("INFO", key, val)
            shown += 1
    if not shown:
        r.line("SKIP", "sound route", "r3i6nos-sound status gave nothing (not the R36S image)")


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


# ---- what the power section reads: /proc and /sys only ------------------------------------------------------------------------

CLK = (os.sysconf("SC_CLK_TCK") if hasattr(os, "sysconf") else 100) or 100


def slurp(path):
    try:
        with open(path, encoding="ascii", errors="replace") as f:
            return f.read().replace("\0", "").strip()
    except OSError:
        return None


def num(path):
    try:
        return int(slurp(path))
    except (TypeError, ValueError):
        return None


def supply():
    """The battery's directory under /sys/class/power_supply, or None."""
    base = os.path.join(SYS, "class/power_supply")
    try:
        names = sorted(os.listdir(base))
    except OSError:
        return None
    for n in names:
        if (slurp(os.path.join(base, n, "type")) or "").lower() == "battery":
            return os.path.join(base, n)
    for n in names:
        if "bat" in n.lower() or n.lower().startswith("bms"):
            return os.path.join(base, n)
    return None


def draw_mw(b):
    """The battery's draw now in mW (current x voltage), or None when it does not report both."""
    i, v = num(os.path.join(b, "current_now")), num(os.path.join(b, "voltage_now"))
    return abs(i) * v / 1e9 if i is not None and v else None


def sys_snap():
    """Total and idle CPU ticks, interrupts and context switches since boot, from /proc/stat."""
    out = {"total": 0, "idle": 0, "intr": 0, "ctxt": 0}
    for ln in (slurp(os.path.join(PROC, "stat")) or "").splitlines():
        p = ln.split()
        if p and p[0] == "cpu":
            v = [int(x) for x in p[1:]]
            out["total"], out["idle"] = sum(v), v[3] + (v[4] if len(v) > 4 else 0)
        elif p and p[0] in ("intr", "ctxt") and len(p) > 1:
            out[p[0]] = int(p[1])
    return out


def all_snaps():
    """{pid: (CPU ticks, name)} of every process, for the busiest-others lines."""
    out = {}
    try:
        names = [n for n in os.listdir(PROC) if n.isdigit()]
    except OSError:
        return out
    for n in names:
        stat = slurp(os.path.join(PROC, n, "stat"))
        if not stat or ")" not in stat:
            continue
        rest = stat.rsplit(")", 1)[1].split()
        try:
            ticks = int(rest[11]) + int(rest[12])
        except (IndexError, ValueError):
            continue
        name = n
        for ln in (slurp(os.path.join(PROC, n, "status")) or "").splitlines():
            if ln.startswith("Name:"):
                name = ln.split(":", 1)[1].strip() or n
                break
        out[int(n)] = (ticks, name)
    return out


def mean(xs):
    return sum(xs) / len(xs) if xs else None


def idle_baseline(bat, seconds):
    """The draw in mW with nothing running, read once a second for `seconds` (only on battery: otherwise there is no draw to compare, so no wait)."""
    draws = []
    if bat is None or seconds <= 0:
        return draws
    end = time.monotonic() + seconds
    while True:
        d = draw_mw(bat)
        if d is not None:
            draws.append(d)
        left = end - time.monotonic()
        if left <= 0:
            return draws
        time.sleep(min(1.0, left))


def run_commands(r, tool, listed, run_slow, bat=None):
    """Runs each command; returns what the power section needs: one dict a command that ran (key, wall, cpu seconds, context switches, peak KB, draws)."""
    runs = []
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
        draws, next_read = [], start + 1.0
        while True:
            pid, status, rusage = os.wait4(p.pid, os.WNOHANG)
            if pid:
                break
            now = time.monotonic()
            if now > deadline:
                p.kill()
                _, status, rusage = os.wait4(p.pid, 0)
                break
            if bat is not None and now >= next_read:      # the battery's own reading changes about once a second
                next_read = now + 1.0
                d = draw_mw(bat)
                if d is not None:
                    draws.append(d)
            time.sleep(0.01)
        took = time.monotonic() - start
        total += took
        err = (p.stderr.read() or b"").decode("utf-8", "replace").strip().splitlines()
        p.stderr.close()
        peak_kb = rusage.ru_maxrss if rusage else 0  # KB on Linux
        if rusage and not os.WIFSIGNALED(status) and took <= TIMEOUT:
            runs.append({"key": key, "wall": took, "cpu": rusage.ru_utime + rusage.ru_stime, "ctx": rusage.ru_nvcsw + rusage.ru_nivcsw, "kb": peak_kb, "draws": draws})
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
    return runs


def power_section(r, runs, bat, idle, before, after, wall):
    """The `== power ==` section of a tool (docs/POWER.md): the working state only, from the commands that just ran."""
    r.section("power")
    status = None
    if bat is None:
        r.line("SKIP", "battery", "no battery supply found, so no draw can be measured (CPU time and wake-ups below still are)")
    else:
        status = slurp(os.path.join(bat, "status"))
        r.line("INFO", "battery supply", f"{os.path.basename(bat)}: {slurp(os.path.join(bat, 'capacity')) or '?'}%, {status or 'status not reported'}")
        if status != "Discharging":
            r.line("SKIP", "draw", f"the battery says {status or 'nothing'}, not Discharging: unplug the power and run again for the draw in mW")
    r.line("INFO", "idle state", f"a command-line tool is not running between commands, so only the working state is measured ({TOOL} commands, above)")
    if not runs:
        r.line("SKIP", "working", "no command ran, so there is nothing to measure")
        return
    cpu = sum(x["cpu"] for x in runs)
    wall_runs = sum(x["wall"] for x in runs)
    r.line("INFO", "working window", f"{len(runs)} commands, {wall_runs:.1f} s of running, {wall:.1f} s with the waits between them")
    r.line("INFO", "working cpu", f"{cpu:.1f} CPU s in {wall_runs:.1f} s: {cpu / wall_runs * 100 if wall_runs else 0:.0f}% of a core while running (a tool is one thread)")
    longs = [x for x in runs if x["wall"] >= LONG_S]
    worst = None
    for x in longs:
        x["wake"] = x["ctx"] / x["wall"]
        if worst is None or x["wake"] > worst["wake"]:
            worst = x
        line = f"{x['wall']:.2f} s, {x['cpu']:.2f} CPU s ({x['cpu'] / x['wall'] * 100:.0f}% of a core), {x['wake']:.0f} wake-ups/s, peak {x['kb']} KB"
        if x["draws"]:
            line += f", draw {mean(x['draws']):.0f} mW"
        r.line("INFO", f"command {x['key']}", line)
    if worst is None:
        r.line("SKIP", "working wakeups", f"no command ran {LONG_S:.0f} s or more: a shorter one is mostly its own start-up")
    else:
        r.line("PASS" if worst["wake"] <= GOAL_WAKEUPS else "FAIL", "working wakeups",
               f"worst {worst['wake']:.1f}/s ({worst['key']}; goal at most {GOAL_WAKEUPS:.0f} for a command of {LONG_S:.0f} s or more: more means it polls or sleeps in a loop)")
    dt = after[0]["total"] - before[0]["total"]
    if dt > 0:
        busy = 100.0 * (dt - (after[0]["idle"] - before[0]["idle"])) / dt
        r.line("INFO", "working system busy", f"{busy:.1f}% of all CPUs, {(after[0]['intr'] - before[0]['intr']) / wall:.0f} interrupts/s, {(after[0]['ctxt'] - before[0]['ctxt']) / wall:.0f} context switches/s (the report's own wait loop included)")
    top = sorted(((max(0, after[1][p][0] - before[1][p][0]) / CLK / wall * 100, after[1][p][1], p) for p in after[1] if p in before[1] and p != os.getpid()), reverse=True)[:3]
    for pct, name, pid in top:
        if pct > 0:
            r.line("INFO", f"working busiest other {name}", f"{pct:.2f}% of a core (pid {pid})")
    draws = [d for x in runs for d in x["draws"]]
    if status == "Discharging" and draws:
        r.line("INFO", "working draw", f"{mean(draws):.0f} mW mean (min {min(draws):.0f}, max {max(draws):.0f}, {len(draws)} readings, taken only in commands of {LONG_S:.0f} s or more)")
        if idle:
            base = mean(idle)
            extra = mean(draws) - base
            r.line("INFO", "idle draw", f"{base:.0f} mW with nothing running ({len(idle)} readings): the commands cost {extra:+.0f} mW over it")
            if longs:
                r.line("INFO", "working energy", f"about {extra * sum(x['wall'] for x in longs) / 1000:.1f} J over idle for the {len(longs)} command{'' if len(longs) == 1 else 's'} of {LONG_S:.0f} s or more")
        full, volt, capacity = num(os.path.join(bat, "charge_full")), num(os.path.join(bat, "voltage_now")), num(os.path.join(bat, "capacity"))
        if full and volt and capacity is not None and mean(draws) > 0:
            r.line("INFO", "working time left", f"{full * volt / 1e12 * capacity / 100 / (mean(draws) / 1000):.1f} h at this draw ({capacity}% of {full / 1000:.0f} mAh)")
    elif status == "Discharging":
        r.line("SKIP", "working draw", f"no command ran {LONG_S:.0f} s or more, so the battery was never read during one")
    big = max(longs or runs, key=lambda x: x["cpu"])   # the energy of a tool is its CPU time: start with the biggest of the commands long enough to count
    if worst is not None and worst["wake"] > GOAL_WAKEUPS:
        r.line("INFO", "cut first", f"wake-ups: `{worst['key']}` woke {worst['wake']:.0f} times a second: find the poll or the sleep in a loop and block on it")
    else:
        r.line("INFO", "cut first", f"CPU time is the energy of a tool: `{big['key']}` took {big['cpu']:.1f} of the {cpu:.1f} CPU s ({big['cpu'] / cpu * 100 if cpu else 0:.0f}%): "
               "make it faster, or make its output ahead of time instead of while a person waits")
    r.line("INFO", "reading this", "one run on one battery: a short test. Repeat at 100%, 50% and 20% charge and on a warm unit before trusting a small difference")


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
    r.steps = 4 + len(listed)
    r.step("the system")
    system(r)
    r.step("the card")
    card(r, folder)
    bat = supply()
    discharging = bat is not None and slurp(os.path.join(bat, "status")) == "Discharging" and draw_mw(bat) is not None
    if os.path.exists(listing):
        r.step(f"the unit idle for {POWER_IDLE_S:.0f} s: the draw to compare with" if discharging else "the power baseline")
        idle = idle_baseline(bat if discharging else None, POWER_IDLE_S)
        before, started = (sys_snap(), all_snaps()), time.monotonic()
        runs = run_commands(r, tool, listed, run_slow, bat if discharging else None)
        after = (sys_snap(), all_snaps())
        r.step("the power section")
        power_section(r, runs, bat, idle, before, after, max(time.monotonic() - started, 1e-6))
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
