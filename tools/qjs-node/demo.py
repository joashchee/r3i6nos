#!/usr/bin/env python3
# The values package-tool.sh bakes in (`--bake`) come first: the menu reads the app's name from here (menu/demos.lua looks in the first 40 lines).
TOOL = "@TOOL@"          # the command, e.g. compose
APP = "@APP@"            # the app's name and its folder on the card, e.g. Composer
VERSION = "@VERSION@"
PACKAGE = "@PACKAGE@"
COMMIT = "@COMMIT@"      # the short git hash the package was built from (+dirty), and that commit's date: the splash's version line (rule 12)
DATE = "@DATE@"
TAGLINE = "@TAGLINE@"    # the splash's tagline: the package description after the app's name
FONT_HEX = "@FONT_HEX@"  # r3i6nos_vga8x16 as hex (256 glyphs of 16 bytes), baked in by --bake
CP437 = "@CP437@"        # r3i6nos_cp437 as comma-separated code points

"""The Rec demo of a command-line tool (CLAUDE.md rule 11, docs/DEMO-RECORDINGS.md): its package installs this as <command>-demo.

A tool has no screen of its own, so the demo draws the tool's commands and what they print on the theme's 64x24 text-mode screen (the IBM VGA
8x16 font scaled to 10x20, row 0 the top bar, rows 1-20 the display, rows 21-23 the command line), frame by frame, and records it like an app does:
numbered PNG screenshots, `demo-<timestamp>.mp4` (H.264 and AAC, 640x480, 30 fps; the tool's own .wav outputs are the sound), copies of the files
the tour made, and `rec-demo.txt` (the log, in the device report's grammar, with a `== power ==` section; rule 14). Everything lands in
/roms/<App>/Demo/ (R3I6NOS_ROMS for tests).

The tour is the tool's own commands.txt (the list the device report times), with directives in its comments, which report.py ignores:

    #@hero draw: Draw an item, a character or a scene     the hero features (the README's list), in the order they are played
    #@feature draw                                        the commands from here belong to this feature (`#@feature -` ends it)
    #@say Draws a pixel item from a recipe.               the caption of the next command (the log and the screen's reply row)
    draw item --out o/item.png                            a command, run as `<tool> <line>` in a work folder with an o/ for files

A command marked !SLOW is left out. A feature's commands must work alone: an unticked feature's commands do not run, so a command may read only
files an earlier command of its own feature made (the self-test runs every feature alone).

The checklist: the menu starts a tool's demo without a dialog (Settings > Rec demos), so every feature is ticked; over SSH `--features a,b` picks.
`--no-video` writes the frames and the sound and no mp4 (tests, and the packaging smoke test). The tour is deterministic: the screens are drawn
from what the tool printed, held for a fixed number of frames, so two runs make the same frames, sound and screenshots whatever the unit's speed.
While it runs it owns the unit: the lock file, `running`, `progress` and `done` in $R3I6NOS_RUN/demo/ (default /run/loveos/demo/) that the menu and the
helpers read; a hot-plug failure, SIGTERM/SIGINT or Select+Start held for a second on the pad (/dev/input/js0; untested) cancels it.

package-tool.sh fills in the values below and the font (`--bake`). Python 3 standard library only (the rootfs has nothing else).
"""
import array
import os
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import wave
import zlib

FPS = 30
MAX_SECONDS = 90
COLS, ROWS, CELL_W, CELL_H = 64, 24, 10, 20
W, H = COLS * CELL_W, ROWS * CELL_H
SPLASH_FRAMES = 75       # the video opens on the splash for 2.5 s
RATE = 44100
TIMEOUT = 300            # seconds a command may take before it counts as hung
GOAL_WAKEUPS = 200.0     # context switches a second a phase of a second or more may cause (docs/POWER.md): a first guess, set before any measurement
LONG_S = 1.0
RUN = os.environ.get("R3I6NOS_RUN", "/run/loveos")
POWER_IDLE_S = float(os.environ.get("R3I6NOS_REPORT_POWER_S", "5"))
HERE = os.path.dirname(os.path.realpath(__file__))

# the theme's pairs (theme/r3i6nos_ansiapps.h, r3_styles): every one passes 4.5:1
RGB = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
       (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255), (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]
BAR, BODY, STRONG, FRAME, HINT, OK, WARN, ERROR = (15, 1), (7, 0), (15, 0), (7, 0), (11, 0), (10, 0), (14, 0), (12, 0)


# ---- the font and the screen -------------------------------------------------------------------------------------------------------

def load_font(header=None):
    """(glyphs, to_cp437): 256 x 16 bytes, and {unicode code point: glyph index}. From the baked-in values, else from theme/r3i6nos_vga8x16.h."""
    import re
    if not FONT_HEX.startswith("@"):
        glyphs = bytes.fromhex(FONT_HEX)
        cps = [int(x) for x in CP437.split(",")]
    else:
        path = header or os.environ.get("R3I6NOS_THEME_FONT") or os.path.join(HERE, "..", "..", "theme", "r3i6nos_vga8x16.h")
        with open(path, encoding="utf-8") as f:
            text = f.read()
        body = text.split("r3i6nos_vga8x16[256][16] = {", 1)[1].split("};", 1)[0]
        glyphs = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body))
        cp = text.split("r3i6nos_cp437[256] = {", 1)[1].split("};", 1)[0]
        cps = [int(x, 16) if x.lower().startswith("0x") else int(x) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", cp)]
    assert len(glyphs) == 4096 and len(cps) == 256, (len(glyphs), len(cps))
    to = {}
    for i, c in enumerate(cps):
        to.setdefault(c, i)
    for c in range(32, 127):
        to[c] = c
    return glyphs, to


class Screen:
    """64 x 24 cells: a glyph index, a foreground and a background colour each."""

    def __init__(self, to_cp437):
        self.to = to_cp437
        self.ch = [bytearray(b" " * COLS) for _ in range(ROWS)]
        self.fg = [bytearray([7] * COLS) for _ in range(ROWS)]
        self.bg = [bytearray(COLS) for _ in range(ROWS)]

    def glyphs(self, text):
        return [self.to.get(ord(c), 63) if ord(c) >= 32 else 32 for c in str(text)]

    def put(self, col, row, text, style=BODY, width=None):
        """Write text from (col, row), cut at `width` cells (default: the screen's edge) with ...; returns the column after it."""
        g = self.glyphs(text)
        room = (width if width is not None else COLS - col)
        room = max(0, min(room, COLS - col))
        if len(g) > room:
            g = g[:max(0, room - 3)] + [46, 46, 46][:min(3, room)]
        for i, v in enumerate(g):
            self.ch[row][col + i], self.fg[row][col + i], self.bg[row][col + i] = v, style[0], style[1]
        return col + len(g)

    def fill(self, col, row, w, h, style=BODY, ch=32):
        for r in range(row, row + h):
            for c in range(col, col + w):
                self.ch[r][c], self.fg[r][c], self.bg[r][c] = ch, style[0], style[1]

    def box(self, col, row, w, h, title=None, right=None):
        self.fill(col, row, w, h, FRAME)
        for c in range(col + 1, col + w - 1):
            self.ch[row][c] = self.ch[row + h - 1][c] = 196
        for r in range(row + 1, row + h - 1):
            self.ch[r][col] = self.ch[r][col + w - 1] = 179
        self.ch[row][col], self.ch[row][col + w - 1], self.ch[row + h - 1][col], self.ch[row + h - 1][col + w - 1] = 218, 191, 192, 217
        if title:
            self.put(col + 2, row, " " + title + " ", STRONG, w - 6)
        if right:
            n = len(self.glyphs(right)) + 2
            self.put(col + w - 2 - n, row, " " + right + " ", STRONG, n)

    def top_bar(self, left, right):
        self.fill(0, 0, COLS, 1, BAR)
        self.put(1, 0, left, BAR, COLS - 2)
        n = len(self.glyphs(right))
        self.put(COLS - 1 - n, 0, right, BAR, n)

    def progress(self, col, row, width, percent):
        full = width * percent // 100
        for i in range(width):
            self.ch[row][col + i], self.fg[row][col + i], self.bg[row][col + i] = (219 if i < full else 176), HINT[0], HINT[1]

    def command_line(self, reply, prompt, left, right, reply_style=HINT):
        """The theme's three-row command line (rows 21-23): the reply, the prompt, the status."""
        self.fill(0, 21, COLS, 3, BODY)
        self.put(1, 21, reply, reply_style, COLS - 2)
        self.put(0, 22, ">", OK)
        self.put(2, 22, prompt, STRONG, COLS - 4)
        rn = len(self.glyphs(right))
        self.put(1, 23, left, BODY, COLS - 3 - rn)
        self.put(COLS - 1 - rn, 23, right, HINT, rn)

    def key(self):
        return tuple(bytes(self.ch[r]) + bytes(self.fg[r]) + bytes(self.bg[r]) for r in range(ROWS))

    def text(self):
        return "\n".join(bytes(self.ch[r]).decode("latin-1") for r in range(ROWS))


class Renderer:
    """Screens to indexed 4-bit PNGs, 640 x 480, fast enough for a unit: a glyph's 20 scanlines are made once and a row of cells is cached whole."""

    def __init__(self, glyphs):
        self.glyphs, self.cells, self.rows, self.pngs = glyphs, {}, {}, {}

    def cell(self, ch, fg, bg):
        k = (ch, fg, bg)
        c = self.cells.get(k)
        if c is None:
            c = []
            for y in range(CELL_H):
                bits = self.glyphs[ch * 16 + (y * 16) // CELL_H]
                px = [fg if (bits >> (7 - (x * 8) // CELL_W)) & 1 else bg for x in range(CELL_W)]
                c.append(bytes((px[i] << 4) | px[i + 1] for i in range(0, CELL_W, 2)))
            self.cells[k] = c
        return c

    def row_lines(self, key_row, ch, fg, bg):
        lines = self.rows.get(key_row)
        if lines is None:
            cells = [self.cell(ch[i], fg[i], bg[i]) for i in range(COLS)]
            lines = [b"\0" + b"".join(c[y] for c in cells) for y in range(CELL_H)]
            self.rows[key_row] = lines
        return lines

    def png(self, screen):
        key = screen.key()
        p = self.pngs.get(key)
        if p is None:
            raw = b"".join(b"".join(self.row_lines(key[r], screen.ch[r], screen.fg[r], screen.bg[r])) for r in range(ROWS))
            p = png_bytes(W, H, raw)
            self.pngs[key] = p
        return p


def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def png_bytes(w, h, raw):
    plte = b"".join(bytes(c) for c in RGB)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 4, 3, 0, 0, 0)) + chunk(b"PLTE", plte)
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


# ---- the tour: commands.txt with directives ---------------------------------------------------------------------------------------

def parse_tour(path):
    """(heroes, steps): heroes [(id, label)]; steps [{"feature", "say", "cmd"}] of the commands under a #@feature, !SLOW ones left out."""
    heroes, steps = [], []
    feature, say = None, None
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("#@hero "):
                k, _, label = line[7:].partition(":")
                heroes.append((k.strip(), label.strip() or k.strip()))
            elif line.startswith("#@feature "):
                feature = line[10:].strip()
                if feature in ("-", "none", ""):
                    feature = None   # the commands from here are not part of the tour (the report still times them)
            elif line.startswith("#@say "):
                say = line[6:].strip()
            elif line.startswith("#"):
                continue
            elif line.startswith("!SLOW "):
                say = None
            elif feature:
                steps.append({"feature": feature, "say": say, "cmd": line})
                say = None
    return heroes, steps


# ---- what it reads of the unit: /proc and /sys only --------------------------------------------------------------------------------

def load_report_module():
    """report.py's readers (the battery, /proc snapshots), from the <command>-report installed beside this script, else report.py beside it."""
    import importlib.machinery
    import importlib.util
    for name in (TOOL + "-report", "report.py"):
        path = os.path.join(HERE, name)
        if os.path.isfile(path):
            loader = importlib.machinery.SourceFileLoader("r3_tool_report", path)
            spec = importlib.util.spec_from_loader("r3_tool_report", loader)
            mod = importlib.util.module_from_spec(spec)
            loader.exec_module(mod)
            return mod
    return None


def read(path, limit=65536):
    try:
        with open(path, "rb") as f:
            return f.read(limit).decode("utf-8", "replace")
    except OSError:
        return None


def snapshot(rp):
    """The unit's state now: memory, load, temperature, the battery's charge and draw. A dict of what could be read."""
    proc, sysd = os.environ.get("R3I6NOS_PROC", "/proc"), os.environ.get("R3I6NOS_SYS", "/sys")
    out = {}
    mem = {}
    for ln in (read(proc + "/meminfo") or "").splitlines():
        k, _, v = ln.partition(":")
        if v.split():
            mem[k] = int(v.split()[0])
    out["avail_kb"], out["total_kb"] = mem.get("MemAvailable"), mem.get("MemTotal")
    ld = (read(proc + "/loadavg") or "").split()
    out["load"] = " ".join(ld[:3]) if len(ld) >= 3 else None
    t = (read(sysd + "/class/thermal/thermal_zone0/temp") or "").strip()
    out["temp_c"] = int(t) / 1000.0 if t.lstrip("-").isdigit() else None
    bat = rp.supply() if rp else None
    if bat:
        out["capacity"] = rp.num(os.path.join(bat, "capacity"))
        out["status"] = rp.slurp(os.path.join(bat, "status"))
        out["mw"] = rp.draw_mw(bat) if out["status"] == "Discharging" else None
    return out


def snap_text(s):
    parts = []
    if s.get("avail_kb") is not None:
        parts.append(f"{s['avail_kb'] / 1024:.0f} MB free of {(s.get('total_kb') or 0) / 1024:.0f} MB")
    if s.get("temp_c") is not None:
        parts.append(f"{s['temp_c']:.1f} C")
    if s.get("load"):
        parts.append("load " + s["load"])
    if s.get("capacity") is not None:
        parts.append(f"battery {s['capacity']}% {s.get('status') or '?'}" + (f", {s['mw']:.0f} mW" if s.get("mw") else ""))
    return "; ".join(parts) or "nothing could be read"


# ---- running the tool's commands ------------------------------------------------------------------------------------------------------

def run_command(tool, line, work, bat, rp):
    """Run `tool <line>` in `work`; stdout and stderr to files (a pipe could fill while we wait). A record: text, files, exit, wall, CPU, switches, peak KB, draws."""
    out_path, err_path = os.path.join(work, ".stdout"), os.path.join(work, ".stderr")
    rec = {"cmd": line, "exit": None, "wall": 0.0, "cpu": 0.0, "ctx": 0, "kb": 0, "draws": [], "stdout": "", "stderr": "", "new": []}
    before = set(list_files(work))
    start = time.monotonic()
    try:
        with open(out_path, "wb") as fo, open(err_path, "wb") as fe:
            p = subprocess.Popen([tool] + shlex.split(line), cwd=work, stdout=fo, stderr=fe, stdin=subprocess.DEVNULL)
    except OSError as e:
        rec["stderr"], rec["exit"] = f"cannot start {tool}: {e}", 127
        return rec
    status = rusage = None
    next_read = start + 1.0
    while True:
        pid, status, rusage = os.wait4(p.pid, os.WNOHANG)
        if pid:
            break
        now = time.monotonic()
        if now - start > TIMEOUT:
            p.kill()
            _, status, rusage = os.wait4(p.pid, 0)
            break
        if bat is not None and now >= next_read and rp:
            next_read = now + 1.0
            d = rp.draw_mw(bat)
            if d is not None:
                rec["draws"].append(d)
        if CANCEL["now"]:
            p.kill()
            _, status, rusage = os.wait4(p.pid, 0)
            break
        time.sleep(0.01)
    rec["wall"] = time.monotonic() - start
    if rusage:
        rec["cpu"], rec["ctx"], rec["kb"] = rusage.ru_utime + rusage.ru_stime, rusage.ru_nvcsw + rusage.ru_nivcsw, rusage.ru_maxrss
    rec["exit"] = os.WEXITSTATUS(status) if os.WIFEXITED(status) else 128 + (os.WTERMSIG(status) if os.WIFSIGNALED(status) else 0)
    rec["stdout"] = (read(out_path, 1 << 20) or "")
    rec["stderr"] = (read(err_path, 1 << 20) or "")
    for n in (out_path, err_path):
        try:
            os.remove(n)
        except OSError:
            pass
    rec["new"] = sorted(f for f in list_files(work) if f not in before)
    return rec


def list_files(work):
    out = []
    for d, _, files in os.walk(work):
        for n in files:
            out.append(os.path.relpath(os.path.join(d, n), work))
    return out


# ---- cancel, lock, progress ----------------------------------------------------------------------------------------------------------

CANCEL = {"now": False, "why": ""}


def on_signal(signum, frame):
    CANCEL["now"], CANCEL["why"] = True, f"signal {signum}"


class Pad:
    """Select+Start held for a second, read from the pad's joystick device (a js_event is 8 bytes: time, value, type, number). The raw numbers of
    Select and Start (12 and 13 here) are the IO Checker's 13 and 14 less one: UNTESTED on the unit (R3I6NOS_PAD_SELECT / _START change them)."""

    def __init__(self):
        self.fd, self.held, self.since = None, set(), None
        self.select = int(os.environ.get("R3I6NOS_PAD_SELECT", "12"))
        self.start = int(os.environ.get("R3I6NOS_PAD_START", "13"))
        try:
            self.fd = os.open(os.environ.get("R3I6NOS_JS", "/dev/input/js0"), os.O_RDONLY | os.O_NONBLOCK)
        except OSError:
            self.fd = None

    def poll(self):
        if self.fd is None:
            return False
        while True:
            try:
                data = os.read(self.fd, 8)
            except OSError:
                break
            if len(data) < 8:
                break
            _, value, typ, num = struct.unpack("<IhBB", data)
            if typ & 0x01 and not typ & 0x80:
                (self.held.add if value else self.held.discard)(num)
        both = self.select in self.held and self.start in self.held
        if both and self.since is None:
            self.since = time.monotonic()
        if not both:
            self.since = None
        return both and time.monotonic() - self.since >= 1.0

    def close(self):
        if self.fd is not None:
            os.close(self.fd)


def pid_alive(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def atomic(path, text):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, path)


class Status:
    """The lock and the three status files (docs/DEVICE-REPORTS.md, "Progress"): the menu and the helpers read them."""

    def __init__(self, logpath):
        self.dir = os.path.join(RUN, "demo")
        self.log, self.ok = logpath, True
        self.last = 0.0
        try:
            os.makedirs(self.dir, exist_ok=True)
        except OSError:
            self.ok = False

    def holder(self):
        t = (read(os.path.join(self.dir, "lock"), 32) or "").strip()
        return int(t) if t.isdigit() and pid_alive(int(t)) else None

    def take(self):
        h = self.holder()
        if h and h != os.getpid():
            return h
        if self.ok:
            try:
                atomic(os.path.join(self.dir, "lock"), f"{os.getpid()}\n")
                atomic(os.path.join(self.dir, "running"), APP + "\n")
                try:
                    os.remove(os.path.join(self.dir, "done"))
                except OSError:
                    pass
            except OSError:
                self.ok = False
        return None

    def progress(self, percent, doing, force=False):
        if not self.ok or (not force and time.monotonic() - self.last < 1.0):
            return
        self.last = time.monotonic()
        try:
            atomic(os.path.join(self.dir, "progress"), f"step={int(percent)}\nsteps=100\ndoing={' '.join(doing.split())}\nfile={self.log}\n")
        except OSError:
            pass

    def finish(self, code):
        if not self.ok:
            return
        for n in ("lock", "running"):
            try:
                os.remove(os.path.join(self.dir, n))
            except OSError:
                pass
        try:
            atomic(os.path.join(self.dir, "done"), f"exit={code}\n")
        except OSError:
            pass


def hotplug_failures(offset_ns):
    """Error events written since `offset_ns` (the numbers only rise): the texts, and the newest number seen. /run/r3i6nos-hotplug/events, docs/HOTPLUG.md."""
    path = os.environ.get("R3I6NOS_HOTPLUG_EVENTS", "/run/r3i6nos-hotplug/events")
    texts, newest = [], offset_ns
    for ln in (read(path) or "").splitlines():
        p = ln.split("\t", 2)
        if len(p) == 3 and p[0].isdigit():
            n = int(p[0])
            newest = max(newest, n)
            if n > offset_ns and p[1] == "error":
                texts.append(p[2])
    return texts, newest


# ---- the screens of the tour ---------------------------------------------------------------------------------------------------------

def size_text(n):
    return f"{n} B" if n < 1024 else (f"{n / 1024:.1f} KB" if n < 1 << 20 else f"{n / (1 << 20):.1f} MB")


def clean_lines(text, width):
    """Printable lines of `text`, wrapped to `width` cells."""
    out = []
    for ln in text.replace("\t", "    ").splitlines():
        ln = "".join(c if 32 <= ord(c) < 127 or ord(c) > 160 else "?" for c in ln).rstrip()
        while len(ln) > width:
            out.append(ln[:width])
            ln = ln[width:]
        out.append(ln)
    return out


def splash_screen(to, percent, what):
    s = Screen(to)
    s.top_bar(APP, VERSION)
    s.box(12, 8, 40, 7, APP)
    s.put(15, 10, TAGLINE, STRONG, 34)
    s.put(15, 11, what, BODY, 34)
    s.progress(15, 12, 34, percent)
    s.put(15, 13, build_line(), HINT, 34)
    return s


def build_line():
    return f"v{VERSION} {COMMIT} {DATE}"


def step_screen(to, hero_label, i, n, step, typed, rec=None, caret=True):
    """One step of the tour: the feature, the command (typed so far), and once it has run what it printed and the files it made."""
    s = Screen(to)
    s.top_bar(APP, "v" + VERSION)
    s.box(0, 1, COLS, 20, hero_label, f"{i} of {n}")
    s.put(2, 3, "$", OK)
    x = s.put(4, 3, f"{TOOL} {typed}", STRONG, COLS - 6)
    if caret and rec is None and x < COLS - 2:
        s.ch[3][x], s.fg[3][x], s.bg[3][x] = 219, STRONG[0], STRONG[1]
    if rec is not None:
        row = 5
        lines = [(ln, BODY) for ln in clean_lines(rec["stdout"], COLS - 6)] + [(ln, ERROR) for ln in clean_lines(rec["stderr"], COLS - 6)]
        shown = lines[:9]
        for ln, st in shown:
            s.put(3, row, ln, st, COLS - 6)
            row += 1
        if len(lines) > len(shown):
            s.put(3, row, f"... {len(lines) - len(shown)} more lines", HINT, COLS - 6)
            row += 1
        row = max(row + 1, 15)
        for f in rec["new"][:3]:
            sz = rec["sizes"].get(f, 0)
            s.put(3, row, f"{f}", HINT, COLS - 18)
            n_ = len(s.glyphs(size_text(sz)))
            s.put(COLS - 3 - n_, row, size_text(sz), HINT, n_)
            row += 1
        if len(rec["new"]) > 3:
            s.put(3, row, f"... and {len(rec['new']) - 3} more files", HINT, COLS - 6)
        s.put(3, 19, f"exit {rec['exit']}", OK if rec["exit"] == 0 else ERROR, COLS - 6)
    caption = step["say"] or hero_label
    s.command_line(caption, f"{TOOL} {step['cmd']}" if rec is not None else f"{TOOL} {typed}", f"{TOOL} demo", f"step {i} of {n}")
    return s


# ---- the sound: the tool's own .wav outputs, mixed at the moment their step shows ---------------------------------------------------

def read_wav(path):
    """A 16-bit PCM .wav as (rate, channels, array('h') of interleaved samples), or None."""
    try:
        with wave.open(path, "rb") as w:
            if w.getsampwidth() != 2 or w.getnchannels() not in (1, 2) or w.getcomptype() != "NONE":
                return None
            a = array.array("h")
            a.frombytes(w.readframes(w.getnframes()))
            if sys.byteorder == "big":
                a.byteswap()
            return w.getframerate(), w.getnchannels(), a
    except (OSError, wave.Error, EOFError):
        return None


def to_stereo44(rate, channels, a):
    """The samples as 44.1 kHz stereo (nearest-neighbour resampling: it is a demo's soundtrack, not a mastering step)."""
    frames = len(a) // channels
    out = array.array("h")
    if rate == RATE:
        idx = range(frames)
    else:
        n = int(frames * RATE / rate)
        idx = [min(frames - 1, int(i * rate / RATE)) for i in range(n)]
    if channels == 1:
        for i in idx:
            v = a[i]
            out.append(v)
            out.append(v)
    else:
        for i in idx:
            out.append(a[2 * i])
            out.append(a[2 * i + 1])
    return out


def mix_audio(total_frames, cues):
    """A 44.1 kHz stereo WAV for `total_frames` video frames: silence, with each cue (video frame, path) mixed in at its moment. (bytes, number of cues mixed)."""
    n = total_frames * (RATE // FPS)
    mix = array.array("i", [0]) * (2 * n)
    used = 0
    for frame, path in cues:
        w = read_wav(path)
        if not w:
            continue
        s = to_stereo44(*w)
        at = frame * (RATE // FPS) * 2
        for i in range(min(len(s), len(mix) - at)):
            mix[at + i] += s[i]
        used += 1
    out = array.array("h", [max(-32768, min(32767, v)) for v in mix])
    if sys.byteorder == "big":
        out.byteswap()
    data = out.tobytes()
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 2, RATE, RATE * 4, 4, 16) + b"data" + struct.pack("<I", len(data))
    return hdr + data, used


# ---- the log -----------------------------------------------------------------------------------------------------------------------------

class Log:
    def __init__(self, path, how, quiet):
        self.path, self.quiet = path, quiet
        self.counts = {"PASS": 0, "FAIL": 0, "INFO": 0, "SKIP": 0}
        self.lines = []
        self.raw("# r3i6nos rec demo\n"
                 f"app: {APP} {VERSION} ({PACKAGE})\n"
                 f"made: {time.strftime('%Y-%m-%d %H:%M:%S')}\n"
                 f"how: {how}\n"
                 "for: Claude and the maintainer. The numbered PNGs, the demo-*.mp4 and demo-files/ are in this folder; look at them and say whether they show the tool well.\n")

    def raw(self, t):
        self.lines.append(t)

    def section(self, name):
        self.raw(f"\n== {name} ==\n")

    def line(self, status, key, value):
        key = " ".join(str(key).replace(":", " ").split())
        value = " ".join(str(value).split())
        self.counts[status] += 1
        self.raw(f"{status} {key}: {value}\n")

    def close(self):
        c = self.counts
        self.raw(f"\n== summary ==\nPASS {c['PASS']}  FAIL {c['FAIL']}  INFO {c['INFO']}  SKIP {c['SKIP']}\nend\n")
        text = "".join(self.lines)
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="ascii", errors="replace", newline="\n") as f:
            f.write(text)
        os.replace(tmp, self.path)
        if not self.quiet:
            sys.stdout.write(text)


def clock_text(frame):
    s = frame // FPS
    return f"{s // 60}m{s % 60:02d}s"


# ---- the power section (docs/POWER.md): the tour's commands, then the drawing, then the encode -------------------------------------

def phase_record(key, wall, cpu, ctx, kb, draws):
    return {"key": key, "wall": wall, "cpu": cpu, "ctx": ctx, "kb": kb, "draws": draws}


def mean(xs):
    return sum(xs) / len(xs) if xs else None


def power_section(log, rp, bat, idle, phases, before, after, wall, status):
    """`== power ==`: the battery, then each phase of the run (the tool's commands, drawing the frames, the encode) with its CPU, wake-ups, peak memory and draw."""
    log.section("power")
    if rp is None or bat is None:
        log.line("SKIP", "battery", "no battery supply found, so no draw can be measured (CPU time and wake-ups below still are)")
    else:
        log.line("INFO", "battery supply", f"{os.path.basename(bat)}: {rp.slurp(os.path.join(bat, 'capacity')) or '?'}%, {status or 'status not reported'}")
        if status != "Discharging":
            log.line("SKIP", "draw", f"the battery says {status or 'nothing'}, not Discharging: unplug the power and run again for the draw in mW")
    log.line("INFO", "idle state", "a tool is not running between commands; the demo's own work is measured as three phases: the tool's commands, drawing the frames, the encode")
    phases = [p for p in phases if p]
    if not phases:
        log.line("SKIP", "working", "nothing ran, so there is nothing to measure")
        return
    total_cpu = sum(p["cpu"] for p in phases)
    total_wall = sum(p["wall"] for p in phases)
    log.line("INFO", "working window", f"{len(phases)} phases, {total_wall:.1f} s of running, {wall:.1f} s from start to the log")
    log.line("INFO", "working cpu", f"{total_cpu:.1f} CPU s in {total_wall:.1f} s: {total_cpu / total_wall * 100 if total_wall else 0:.0f}% of a core while running")
    worst = None
    for p in phases:
        p["wake"] = p["ctx"] / p["wall"] if p["wall"] > 0 else 0.0
        line = f"{p['wall']:.2f} s, {p['cpu']:.2f} CPU s ({p['cpu'] / p['wall'] * 100 if p['wall'] else 0:.0f}% of a core), {p['wake']:.0f} wake-ups/s, peak {p['kb']} KB"
        if p["draws"]:
            line += f", draw {mean(p['draws']):.0f} mW"
        log.line("INFO", f"phase {p['key']}", line)
        if p["wall"] >= LONG_S and (worst is None or p["wake"] > worst["wake"]):
            worst = p
    if worst is None:
        log.line("SKIP", "working wakeups", f"no phase ran {LONG_S:.0f} s or more: a shorter one is mostly its own start-up")
    else:
        log.line("PASS" if worst["wake"] <= GOAL_WAKEUPS else "FAIL", "working wakeups",
                 f"worst {worst['wake']:.1f}/s ({worst['key']}; goal at most {GOAL_WAKEUPS:.0f} for a phase of {LONG_S:.0f} s or more: more means it polls or sleeps in a loop)")
    if rp is not None and before and after:
        dt = after[0]["total"] - before[0]["total"]
        if dt > 0 and wall > 0:
            busy = 100.0 * (dt - (after[0]["idle"] - before[0]["idle"])) / dt
            log.line("INFO", "working system busy", f"{busy:.1f}% of all CPUs, {(after[0]['intr'] - before[0]['intr']) / wall:.0f} interrupts/s, {(after[0]['ctxt'] - before[0]['ctxt']) / wall:.0f} context switches/s (the demo's own work included)")
        top = sorted(((max(0, after[1][p][0] - before[1][p][0]) / rp.CLK / wall * 100, after[1][p][1], p) for p in after[1] if p in before[1] and p != os.getpid()), reverse=True)[:3]
        for pct, name, pid in top:
            if pct > 0:
                log.line("INFO", f"working busiest other {name}", f"{pct:.2f}% of a core (pid {pid})")
    draws = [d for p in phases for d in p["draws"]]
    if status == "Discharging" and draws:
        log.line("INFO", "working draw", f"{mean(draws):.0f} mW mean (min {min(draws):.0f}, max {max(draws):.0f}, {len(draws)} readings)")
        if idle:
            base = mean(idle)
            extra = mean(draws) - base
            log.line("INFO", "idle draw", f"{base:.0f} mW with nothing running ({len(idle)} readings): the demo costs {extra:+.0f} mW over it")
            log.line("INFO", "working energy", f"about {extra * total_wall / 1000:.1f} J over idle for the {total_wall:.0f} s the phases ran")
        full, volt, cap = rp.num(os.path.join(bat, "charge_full")), rp.num(os.path.join(bat, "voltage_now")), rp.num(os.path.join(bat, "capacity"))
        if full and volt and cap is not None and mean(draws) > 0:
            log.line("INFO", "working time left", f"{full * volt / 1e12 * cap / 100 / (mean(draws) / 1000):.1f} h at this draw ({cap}% of {full / 1000:.0f} mAh)")
    elif status == "Discharging":
        log.line("SKIP", "working draw", f"no phase ran {LONG_S:.0f} s or more, so the battery was never read during one")
    big = max(phases, key=lambda p: p["cpu"])
    if worst is not None and worst["wake"] > GOAL_WAKEUPS:
        log.line("INFO", "cut first", f"wake-ups: phase `{worst['key']}` woke {worst['wake']:.0f} times a second: find the poll or the sleep in a loop and block on it")
    else:
        log.line("INFO", "cut first", f"CPU time is the energy of a recording: phase `{big['key']}` took {big['cpu']:.1f} of the {total_cpu:.1f} CPU s ({big['cpu'] / total_cpu * 100 if total_cpu else 0:.0f}%): "
                 "make it faster, or record less (fewer features, a shorter tour)")
    log.line("INFO", "reading this", "one run on one battery: a short test. Repeat at 100%, 50% and 20% charge and on a warm unit before trusting a small difference")


# ---- the encode ------------------------------------------------------------------------------------------------------------------------

def find_ffmpeg():
    env = os.environ.get("R3I6NOS_FFMPEG")
    if env:
        return env if os.path.isfile(env) else None
    if os.path.isfile("/usr/bin/ffmpeg"):
        return "/usr/bin/ffmpeg"
    return shutil.which("ffmpeg")


def ffmpeg_version(ff):
    try:
        return subprocess.run([ff, "-version"], capture_output=True, text=True, timeout=20).stdout.splitlines()[0]
    except (OSError, subprocess.SubprocessError, IndexError):
        return None


def encode(ff, frames_path, wav_path, out_path, total, status, base_pct):
    """The mp4 from the PNG stream and the WAV (the same line as NoCoGaMa's). Returns (ok, message, rusage of the child)."""
    log_path = out_path[:-4] + "-ffmpeg.txt"
    cmd = [ff, "-y", "-loglevel", "error", "-nostdin", "-progress", "pipe:1", "-f", "image2pipe", "-framerate", str(FPS), "-c:v", "png", "-i", frames_path, "-i", wav_path,
           "-vf", "scale=640:480:flags=neighbor,format=yuv420p", "-c:v", "libx264", "-preset", "veryfast", "-crf", "20", "-r", str(FPS), "-c:a", "aac", "-ar", "44100", "-ac", "2",
           "-shortest", out_path]
    try:
        with open(log_path, "wb") as le:
            p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=le, stdin=subprocess.DEVNULL)
            for raw in p.stdout:
                if raw.startswith(b"frame="):
                    try:
                        n = int(raw.split(b"=", 1)[1])
                    except ValueError:
                        continue
                    status.progress(base_pct + (100 - base_pct) * min(n, total) / max(total, 1), f"Encoding {100 * min(n, total) // max(total, 1)}%")
                if CANCEL["now"]:
                    p.kill()
                    break
            _, st, ru = os.wait4(p.pid, 0)
    except OSError as e:
        return False, f"ffmpeg could not be started: {e}", None
    if CANCEL["now"]:
        _remove(out_path)
        return False, "cancelled", ru
    code = os.WEXITSTATUS(st) if os.WIFEXITED(st) else 255
    if code != 0 or not os.path.isfile(out_path):
        _remove(out_path)
        return False, f"ffmpeg failed (exit {code}; its messages are in {log_path}); the frames and the sound are kept in {os.path.dirname(out_path)}", ru
    _remove(log_path)
    return True, "ok", ru


def _remove(path):
    try:
        os.remove(path)
    except OSError:
        pass


# ---- the run -----------------------------------------------------------------------------------------------------------------------------

def plan_timeline(steps, recs, compact):
    """Frame counts of each step's parts: (caption hold, typing frames per screen, hold after typing, output hold per line, output hold base)."""
    return (8, 3, 6, 2, 30) if compact else (15, 2, 12, 3, 45)


def main(argv):
    quiet = "--quiet" in argv
    no_video = "--no-video" in argv
    how = "Settings > Rec demos" if "--from-settings" in argv else f"{TOOL}-demo"
    features = None
    for i, a in enumerate(argv):
        if a == "--features" and i + 1 < len(argv):
            features = [x.strip() for x in argv[i + 1].split(",") if x.strip()]
        elif a.startswith("--features="):
            features = [x.strip() for x in a.split("=", 1)[1].split(",") if x.strip()]
    tool = os.environ.get("R3I6NOS_REPORT_TOOL") or os.path.join(HERE, TOOL)
    listing = os.environ.get("R3I6NOS_REPORT_COMMANDS") or os.path.join(HERE, "..", "share", PACKAGE, "commands.txt")
    folder = os.path.join(os.environ.get("R3I6NOS_ROMS", "/roms"), APP)
    demo = os.path.join(folder, "Demo")
    logpath = os.path.join(demo, "rec-demo.txt")
    if not os.path.isfile(listing):
        print(f"{TOOL}-demo: no command list at {listing}", file=sys.stderr)
        return 1
    heroes, steps = parse_tour(listing)
    if not heroes or not steps:
        print(f"{TOOL}-demo: {listing} has no #@hero / #@feature lines: nothing to show", file=sys.stderr)
        return 1
    ids = [h[0] for h in heroes]
    if features is not None:
        bad = [f for f in features if f not in ids]
        if bad:
            print(f"{TOOL}-demo: unknown feature(s): {', '.join(bad)} (the tour's: {', '.join(ids)})", file=sys.stderr)
            return 2
    on = [h for h in heroes if features is None or h[0] in features]
    if not on:
        print(f"{TOOL}-demo: at least one feature must stay ticked", file=sys.stderr)
        return 2
    try:
        os.makedirs(demo, exist_ok=True)
    except OSError as e:
        print(f"{TOOL}-demo: cannot write in {demo}: {e}", file=sys.stderr)
        return 1
    status = Status(logpath)
    holder = status.take()
    if holder:
        print(f"{TOOL}-demo: another Rec demo is recording (pid {holder})", file=sys.stderr)
        return 75
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, on_signal)
    pad = Pad()
    code = 1
    try:
        code = record(argv, quiet, no_video, how, tool, listing, demo, logpath, heroes, steps, on, status, pad)
    finally:
        pad.close()
        status.finish(code)
    return code


def record(argv, quiet, no_video, how, tool, listing, demo, logpath, heroes, steps, on, status, pad):
    t_start = time.monotonic()
    stamp = time.strftime("%Y%m%d-%H%M%S")
    log = Log(logpath, how, quiet)
    rp = None
    try:
        rp = load_report_module()
    except Exception:  # a broken report module must not stop a demo
        rp = None
    glyphs, to = load_font()
    rend = Renderer(glyphs)
    status.progress(1, "Starting", force=True)
    # an earlier run's screenshots, copies, log, frames and sound go; every demo-*.mp4 stays
    for name in os.listdir(demo):
        full = os.path.join(demo, name)
        if (len(name) > 4 and name[:2].isdigit() and name[2] == "-" and name.endswith(".png")) or name in ("rec-demo.txt", "rec-demo.txt.tmp") \
                or (name.startswith("demo-") and (name.endswith("-frames.pngs") or name.endswith(".wav") or name.endswith("-ffmpeg.txt"))):
            _remove(full)
        elif name == "demo-files" and os.path.isdir(full):
            shutil.rmtree(full, ignore_errors=True)
    bat = rp.supply() if rp else None
    snap0 = snapshot(rp)
    discharging = bat is not None and snap0.get("status") == "Discharging" and rp.draw_mw(bat) is not None
    phases = []
    # the power baseline: the unit idle for a few seconds (only on battery)
    status.progress(2, "The unit idle: the draw to compare with", force=True)
    idle = rp.idle_baseline(bat if discharging else None, POWER_IDLE_S) if rp else []
    before = (rp.sys_snap(), rp.all_snaps()) if rp else None
    t_work = time.monotonic()
    _, hp_ns = hotplug_failures(0)   # no history is replayed: only events newer than the newest one now there count
    # ---- the tool's commands, run once, off the frame clock
    work = tempfile.mkdtemp(prefix=f"{TOOL}-demo-")
    os.makedirs(os.path.join(work, "o"), exist_ok=True)
    chosen = [s for s in steps if s["feature"] in [h[0] for h in on]]
    records = []
    cmd_phase = phase_record("the tool's commands", 0.0, 0.0, 0, 0, [])
    for k, step in enumerate(chosen):
        status.progress(5 + 40 * k / max(len(chosen), 1), f"Running: {TOOL} {step['cmd']}"[:60], force=True)
        if CANCEL["now"] or pad.poll():
            CANCEL["now"], CANCEL["why"] = True, CANCEL["why"] or "Select+Start held"
            break
        fails, _ = hotplug_failures(hp_ns)
        if fails:
            for f in fails:
                log.line("FAIL", "hotplug failure", f)
            CANCEL["now"], CANCEL["why"] = True, "a hot-plug failure"
            break
        rec = run_command(tool, step["cmd"], work, bat if discharging else None, rp)
        rec["sizes"] = {f: os.path.getsize(os.path.join(work, f)) for f in rec["new"] if os.path.isfile(os.path.join(work, f))}
        rec["step"] = step
        records.append(rec)
        cmd_phase["wall"] += rec["wall"]
        cmd_phase["cpu"] += rec["cpu"]
        cmd_phase["ctx"] += rec["ctx"]
        cmd_phase["kb"] = max(cmd_phase["kb"], rec["kb"])
        cmd_phase["draws"] += rec["draws"]
    phases.append(cmd_phase if records else None)
    cancelled = CANCEL["now"]
    # ---- the tour: screens to frames
    log_tour, shots, timeline, cues, chapters = [], [], [], [], []
    self_cpu0 = time.process_time()
    ru0 = _self_usage()
    t_draw = time.monotonic()
    frame = 0

    def hold(screen, n, shot=None):
        nonlocal frame
        timeline.append((screen, n))
        if shot:
            shots.append((shot, frame))
        frame += n

    compact = False
    for attempt in range(2):
        timeline, shots, log_tour, cues, chapters, frame = [], [], [], [], [], 0
        cap_hold, type_per, after_type, line_hold, out_base = plan_timeline(steps, records, compact)
        # the splash: the version line, the progress, shown from the first frame
        for pct, what, n in ((10, "Reading the tour", 25), (60, "Checking the tool", 25), (100, "Ready", SPLASH_FRAMES - 50)):
            hold(splash_screen(to, pct, what), n, "splash" if pct == 100 else None)
        log_tour.append((0, "splash", "The splash: the name, the build (version, commit, date) and the load's progress"))
        n_steps = len(records)
        label = dict(heroes)
        shown_feature = None
        for i, rec in enumerate(records, 1):
            step, lab = rec["step"], label.get(rec["step"]["feature"], rec["step"]["feature"])
            if step["feature"] != shown_feature:
                shown_feature = step["feature"]
                log_tour.append((frame, shown_feature, lab))
                chapters.append((shown_feature, frame))
            log_tour.append((frame, step["feature"], (step["say"] + " -- " if step["say"] else "") + f"{TOOL} {step['cmd']}"))
            hold(step_screen(to, lab, i, n_steps, step, "", None), cap_hold)
            cmd = step["cmd"]
            stride = max(1, -(-len(cmd) // 45))
            for c in range(stride, len(cmd), stride):
                hold(step_screen(to, lab, i, n_steps, step, cmd[:c], None), type_per)
            hold(step_screen(to, lab, i, n_steps, step, cmd, None, caret=True), after_type)
            n_lines = len(clean_lines(rec["stdout"], COLS - 6)) + len(clean_lines(rec["stderr"], COLS - 6))
            wavs = [f for f in rec["new"] if f.lower().endswith(".wav")]
            if wavs:
                cues.append((frame, os.path.join(work, wavs[0])))
            hold(step_screen(to, lab, i, n_steps, step, cmd, rec), min(120, out_base + line_hold * min(n_lines, 9)), f"{step['feature']}-{i}")
        # the end: back on the first screen of the tour, as every demo ends where it began
        tour_end = frame
        hold(splash_screen(to, 100, "Done"), 30, "end")
        if frame <= MAX_SECONDS * FPS or compact:
            break
        compact = True
    total_frames = frame
    # ---- screenshots, frames and sound
    for i, (name, at) in enumerate(shots, 1):
        sc = _screen_at(timeline, at)
        with open(os.path.join(demo, f"{i:02d}-{name}.png"), "wb") as f:
            f.write(rend.png(sc))
    frames_path = os.path.join(demo, f"demo-{stamp}-frames.pngs")
    wav_path = os.path.join(demo, f"demo-{stamp}.wav")
    video_path = os.path.join(demo, f"demo-{stamp}.mp4")
    frames_bytes = 0
    with open(frames_path, "wb") as f:
        for k, (sc, n) in enumerate(timeline):
            png = rend.png(sc)
            for _ in range(n):
                f.write(png)
            frames_bytes += len(png) * n
            status.progress(48 + 20 * (k + 1) / len(timeline), f"Drawing the frames {100 * (k + 1) // len(timeline)}%")
    wav_data, cues_used = mix_audio(total_frames, cues)
    with open(wav_path, "wb") as f:
        f.write(wav_data)
    status.progress(70, "Mixing the sound", force=True)
    ru1 = _self_usage()
    draw_phase = phase_record("drawing the frames", time.monotonic() - t_draw, time.process_time() - self_cpu0, ru1[0] - ru0[0], ru1[1], [])
    phases.append(draw_phase)
    # ---- the files the tour made, copied (small ones)
    kept = []
    fdir = os.path.join(demo, "demo-files")
    for i, rec in enumerate(records, 1):
        for f in rec["new"]:
            src = os.path.join(work, f)
            if os.path.isfile(src) and os.path.getsize(src) <= (2 << 20) and len(kept) < 40:
                os.makedirs(fdir, exist_ok=True)
                dst = os.path.join(fdir, f"{i:02d}-{f.replace(os.sep, '_')}")
                shutil.copyfile(src, dst)
                kept.append(dst)
    # ---- the encode
    enc_ok, enc_msg, enc_ru, t_enc, enc_wall = None, "", None, time.monotonic(), 0.0
    ff = None if no_video or cancelled else find_ffmpeg()
    if not no_video and not cancelled:
        if ff is None:
            enc_ok, enc_msg = False, "ffmpeg is not installed here, so no mp4 was made. The frames (" + frames_path + ") and the sound (" + wav_path + ") are kept"
        else:
            status.progress(72, "Encoding 0%", force=True)
            enc_ok, enc_msg, enc_ru = encode(ff, frames_path, wav_path, video_path, total_frames, status, 72)
            if CANCEL["now"]:
                cancelled = True
        enc_wall = time.monotonic() - t_enc
        if enc_ru is not None:
            phases.append(phase_record("encoding the video", enc_wall, enc_ru.ru_utime + enc_ru.ru_stime, enc_ru.ru_nvcsw + enc_ru.ru_nivcsw, enc_ru.ru_maxrss, []))
    after = (rp.sys_snap(), rp.all_snaps()) if rp else None
    snap1 = snapshot(rp)
    shutil.rmtree(work, ignore_errors=True)
    if enc_ok:
        _remove(frames_path)   # the video has them; without ffmpeg (or on a failure) they stay, so nothing is lost
        _remove(wav_path)
    total_wall = time.monotonic() - t_start
    # ---- the log
    log.section("build")
    log.line("INFO", "build", build_line())
    log.section("runtime")
    log.line("INFO", "tool", f"{tool}")
    log.line("INFO", "command list", f"{os.path.realpath(listing)}")
    log.line("INFO", "python", sys.version.split()[0])
    ver = read("/proc/version")
    if ver and "Linux version" in ver:
        log.line("INFO", "kernel", ver.split("Linux version", 1)[1].split()[0])
    log.line("INFO", "ffmpeg", (ffmpeg_version(ff) or ff) if ff else ("not looked for (--no-video)" if no_video else "none found"))
    log.line("INFO", "screen", f"{COLS}x{ROWS} cells at {W}x{H}, {FPS} fps, the IBM VGA 8x16 font scaled to {CELL_W}x{CELL_H}")
    log.section("shows")
    log.line("INFO", "shows", ", ".join(h[0] for h in on))
    for h in heroes:
        if h not in on:
            log.line("SKIP", h[0], "not chosen")
    log.section("tour")
    for at, feat, text in log_tour:
        log.line("INFO", f"{clock_text(at)} [{feat}]", text)
    log.section("steps")
    for i, rec in enumerate(records, 1):
        log.line("INFO", f"{i} {rec['step']['feature']}: {TOOL} {rec['cmd']}",
                 f"exit {rec['exit']}, {rec['wall']:.2f} s, peak {rec['kb']} KB, {len(rec['stdout'].splitlines())} lines out, {len(rec['stderr'].splitlines())} lines of stderr, "
                 f"{len(rec['new'])} files ({size_text(sum(rec['sizes'].values()))})")
    log.section("files")
    if cancelled:
        log.line("FAIL", "cancelled", f"{CANCEL['why'] or 'cancelled'}: the files so far are kept and no video was made")
    else:
        secs = total_frames / FPS
        log.line("INFO", "length", f"{total_frames} frames, {secs:.1f} s at {FPS} fps")
        log.line("PASS" if secs <= MAX_SECONDS else "FAIL", "video length", f"{secs:.1f} s (at most {MAX_SECONDS} s)")
        covered = {r["step"]["feature"] for r in records}
        missing = [h[0] for h in on if h[0] not in covered]
        log.line("PASS" if not missing else "FAIL", "hero features shown", f"{len(on)} of {len(on)} have a step in the tour" if not missing else f"no step for {', '.join(missing)}")
        log.line("PASS" if shots else "FAIL", "screenshots", f"{len(shots)} numbered PNGs in {demo}")
        first = timeline[0][0].text() if timeline else ""
        log.line("PASS" if build_line() in first else "FAIL", "first frame", f"the splash, with the version line {build_line()}" if build_line() in first else "the first frame is not the splash with the version line")
        failed_cmds = [r for r in records if r["exit"] not in (0,) and not _expects_failure(r)]
        log.line("INFO", "commands", f"{len(records)} run, {len(failed_cmds)} with a non-zero exit (a tour may show an error on purpose)")
        if cues_used:
            log.line("PASS", "audio", f"{cues_used} of the tool's own .wav outputs mixed in at the moment each shows, {RATE} Hz stereo")
        else:
            log.line("SKIP", "audio", "this tool made no .wav in the tour: the video's sound is silent")
        log.line("INFO", "demo files", f"{len(kept)} files the tour made copied to {fdir}" if kept else "the tour made no files to copy")
        if no_video:
            log.line("SKIP", "video", "not made (--no-video); the frames and the sound are kept")
        elif enc_ok:
            log.line("PASS", "video", f"{video_path}: H.264 and AAC, 640x480, {FPS} fps, {secs:.1f} s")
        else:
            log.line("FAIL", "video", enc_msg)
    log.section("timing")
    log.line("INFO", "commands", f"{cmd_phase['wall']:.1f} s of the tool's own running")
    log.line("INFO", "drawing and sound", f"{draw_phase['wall']:.1f} s to draw {len(timeline)} screens ({len(rend.pngs)} distinct PNGs) and mix the sound")
    if enc_wall:
        log.line("INFO", "encoding", f"{enc_wall:.1f} s for {total_frames} frames ({enc_wall / max(total_frames / FPS, 1e-9):.2f}x real time)")
    log.line("INFO", "all together", f"{total_wall:.1f} s from start to the log")
    log.line("INFO", "headroom", f"the video is {total_frames / FPS:.1f} s of the {MAX_SECONDS} s allowed: {MAX_SECONDS - total_frames / FPS:.1f} s to spare")
    log.section("video")
    mp4 = os.path.getsize(video_path) if enc_ok and os.path.isfile(video_path) else None
    if mp4:
        log.line("INFO", "mp4", f"{size_text(mp4)}, {mp4 * 8 / max(total_frames / FPS, 1e-9) / 1000:.0f} kbit/s on average")
    log.line("INFO", "frames", f"{total_frames} frames from {len(rend.pngs)} distinct screens, {size_text(frames_bytes)} of PNG stream")
    log.line("INFO", "screenshots", f"{len(shots)}: " + ", ".join(f"{i:02d} {n} at {clock_text(a)}" for i, (n, a) in enumerate(shots, 1)))
    log.section("chapters")
    for k, (feat, at) in enumerate(chapters):
        end = chapters[k + 1][1] if k + 1 < len(chapters) else tour_end
        log.line("INFO", feat, f"{(end - at) / FPS:.1f} s ({end - at} frames, {100 * (end - at) // max(total_frames, 1)}% of the video)")
    log.section("unit")
    log.line("INFO", "at the start", snap_text(snap0))
    log.line("INFO", "at the end", snap_text(snap1))
    if snap0.get("capacity") is not None and snap1.get("capacity") is not None:
        log.line("INFO", "battery change", f"{snap0['capacity']}% to {snap1['capacity']}% ({snap1['capacity'] - snap0['capacity']:+d}) in {total_wall:.0f} s")
    if snap0.get("temp_c") is not None and snap1.get("temp_c") is not None:
        log.line("INFO", "temperature change", f"{snap0['temp_c']:.1f} C to {snap1['temp_c']:.1f} C ({snap1['temp_c'] - snap0['temp_c']:+.1f})")
    if rp is not None:
        power_section(log, rp, bat, idle, phases, before, after, max(time.monotonic() - t_work, 1e-6), snap0.get("status"))
    else:
        log.section("power")
        log.line("SKIP", "power", "the device report's readers (<command>-report) are not beside this script, so nothing was measured")
    status.progress(100, "Done", force=True)
    log.close()
    if not quiet:
        print(f"\n{TOOL}-demo: written to {demo}")
    return 0 if log.counts["FAIL"] == 0 else 1


def _expects_failure(rec):
    return False


def _self_usage():
    import resource
    r = resource.getrusage(resource.RUSAGE_SELF)
    return (r.ru_nvcsw + r.ru_nivcsw, r.ru_maxrss)


def _screen_at(timeline, frame):
    at = 0
    for sc, n in timeline:
        if at <= frame < at + n:
            return sc
        at += n
    return timeline[-1][0]


# ---- baking the package's copy: the values and the font ---------------------------------------------------------------------------------

def bake(out, tool, app, version, package, commit, date, tagline, header):
    """Write the installed script: this file with its values and the font filled in (package-tool.sh calls it)."""
    glyphs, to = load_font(header)
    import re
    text = open(__file__, encoding="utf-8").read()
    with open(header, encoding="utf-8") as f:
        h = f.read()
    cp = h.split("r3i6nos_cp437[256] = {", 1)[1].split("};", 1)[0]
    cps = [int(x, 16) if x.lower().startswith("0x") else int(x) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", cp)]
    for token, value in (("@TOOL@", tool), ("@APP@", app), ("@VERSION@", version), ("@PACKAGE@", package), ("@COMMIT@", commit), ("@DATE@", date),
                         ("@TAGLINE@", tagline), ("@FONT_HEX@", glyphs.hex()), ("@CP437@", ",".join(str(c) for c in cps))):
        # only the assignment lines: the module's own text mentions the tokens in comments and in load_font's check
        text = re.sub(r'^(\w+ = )"' + re.escape(token) + '"', lambda m, v=value: m.group(1) + '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"', text, count=1, flags=re.M)
    with open(out, "w", encoding="utf-8") as f:
        f.write(text)
    os.chmod(out, 0o755)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--bake":
        bake(*sys.argv[2:])
        sys.exit(0)
    sys.exit(main(sys.argv[1:]))
