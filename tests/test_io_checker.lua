-- Run with: luajit tests/test_io_checker.lua   (from the repo root)
-- Tests the IO Checker's logic (model.lua), its screen (view.lua, as text), what it reads from the system and the cards (probe.lua,
-- against temporary folders), then runs its real main.lua against a fake LÖVE and a fake pad: every page, the patterns, the tones,
-- the files check and the device report, so a wrong call or a typo fails here and not on the device.
local dir = "apps/io-checker-love/"
package.path = dir .. "?.lua;theme/?.lua;" .. package.path

local tmp = os.getenv("TMPDIR") or "/tmp"
local root = ("%s/io-checker-test-%d"):format(tmp:gsub("/$", ""), os.time() * 1000 + math.random(999))
local function sh(cmd) assert(os.execute(cmd) == 0 or os.execute(cmd) == true, cmd) end
local function write(path, text)
  local f = assert(io.open(path, "wb")); f:write(text); f:close()
end
sh(("mkdir -p '%s'"):format(root))

-- the system as the probes see it
for _, d in ipairs({ "proc/sys/kernel", "proc/device-tree", "proc/asound", "sys/class/power_supply/battery", "sys/class/backlight/backlight",
                     "sys/devices/system/cpu/cpu0/cpufreq", "etc", "roms", "roms2", "bin" }) do
  sh(("mkdir -p '%s/%s'"):format(root, d))
end
write(root .. "/proc/sys/kernel/osrelease", "4.4.189\n")
write(root .. "/proc/device-tree/model", "Game Console R35S/R36S fix by lxc\0")
write(root .. "/proc/cpuinfo", "processor\t: 0\nprocessor\t: 1\nprocessor\t: 2\nprocessor\t: 3\n")
write(root .. "/proc/meminfo", "MemTotal:  919232 kB\nMemAvailable:  848420 kB\n")
write(root .. "/proc/asound/cards", " 0 [rockchiprk817c]: rockchip_rk817- - rockchip,rk817-codec\n")
write(root .. "/proc/mounts", ("/dev/mmcblk0p3 %s/roms exfat rw 0 0\n/dev/mmcblk1p1 %s/roms2 vfat rw 0 0\n"):format(root, root))
write(root .. "/sys/class/power_supply/battery/type", "Battery\n")
write(root .. "/sys/class/power_supply/battery/capacity", "87\n")
write(root .. "/sys/class/power_supply/battery/status", "Discharging\n")
write(root .. "/sys/class/backlight/backlight/brightness", "128\n")
write(root .. "/sys/class/backlight/backlight/max_brightness", "255\n")
write(root .. "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "1512000\n")
write(root .. "/etc/r3i6nos-release", "VERSION=0.1.0\nCOMMIT=9e7a6b6abcdef\n")
write(root .. "/bin/r3i6nos-sound", "#!/bin/sh\necho 'cards: 1'\necho 'card 0 playback path: SPK (of OFF SPK HP)'\n"
  .. "echo 'jack: rk817-sound Headphone Jack'\necho 'headphones: out'\n")
sh(("chmod +x '%s/bin/r3i6nos-sound'"):format(root))

-- the probes read these when they are loaded (os.getenv); luajit has no setenv, so they are set on the module afterwards
local model = require("model")
local probe = require("probe")
probe.PROC, probe.SYS, probe.ETC = root .. "/proc", root .. "/sys", root .. "/etc"
probe.ROMS, probe.ROMS2, probe.SOUND, probe.MB = root .. "/roms", root .. "/roms2", root .. "/bin/r3i6nos-sound", 1
local theme = require("r3i6nos_ansiapps")
local view = require("view")

local function eq(a, b, msg) assert(a == b, (msg or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end
local function has(text, want, msg) assert(tostring(text):find(want, 1, true), (msg or "") .. (" %q not in %q"):format(want, tostring(text))) end

-- ---- model ----
local h = model.hat("ul")
assert(h.up and h.left and not h.down and not h.right, "hat ul")
eq(model.zone(0.49), 0); eq(model.zone(0.5), 1); eq(model.zone(-0.9), -1)
eq(model.fmt(0.5), "+0.50"); eq(model.fmt(nil), "+0.00")
eq(model.runtime("11.5", 11, 5, 0), "LÖVE 11.5.0")
eq(model.reportName(11, 5), "r36s-report-11.5.txt"); eq(model.reportName(12, 0), "r36s-report-12.0.txt"); eq(model.reportName(), "r36s-report-native.txt")

-- the layout measured on a real R36S (docs/hardware-findings.md, LÖVE numbering)
local Lt = model.LAYOUT
eq(Lt.b, 1); eq(Lt.a, 2); eq(Lt.l1, 5); eq(Lt.r2, 8); eq(Lt.up, 9); eq(Lt.right, 12); eq(Lt.select, 13); eq(Lt.start, 14); eq(Lt.fn, 17)
eq(#model.NAMES, 17); eq(model.buttonName(9), "Up(9)"); eq(model.buttonName(17), "Fn(17)")
-- and the theme's list keys agree with it
eq(theme.PAD[Lt.a], "accept"); eq(theme.PAD[Lt.b], "back"); eq(theme.PAD[Lt.up], "up"); eq(theme.PAD[Lt.start], "more")

local function held(...)
  local set = {}
  for _, i in ipairs({ ... }) do set[i] = true end
  return function(i) return set[i] == true end
end
local d = model.dpad("c", held(9, 12))
assert(d.up and d.right and not d.down and not d.left, "D-pad from buttons 9 and 12")
assert(model.exitHeld(held(13, 14)) and not model.exitHeld(held(13)), "Select+Start, raw")
assert(model.exitHeld(held(), function(n) return n == "back" or n == "start" end), "SDL's mapping also counts")
assert(model.keyHeld({ volumeup = true }, "vol_up") and not model.keyHeld({ volumeup = true }, "vol_down"))

local chord = model.newChord(1.0)
assert(not chord:update(0.6, true)); assert(not chord:update(0.3, false), "release resets")
assert(not chord:update(0.9, true)); assert(chord:update(0.2, true))
local idle = model.newIdle()
idle:update(6); assert(idle:warning() and not idle:expired()); idle:touch(); idle:update(16); assert(idle:expired())

-- the input record: presses, chatter, ranges, keys, missing, the log
local inp = model.newInput()
inp:press(2, 1.00); inp:release(2, 1.10); inp:press(2, 1.12)          -- 20 ms after a release: chatter
inp:press(3, 2.00); inp:release(3, 2.10); inp:press(3, 2.50)          -- 400 ms: a real second press
eq(inp.presses[2], 2); eq(inp.chatter[2], 1); eq(inp.chatter[3], nil)
inp:axis(1, 0.2); inp:axis(1, -0.95); inp:axis(1, 0.97)
eq(inp.axes[1].min, -0.95); eq(inp.axes[1].max, 0.97)
inp:key("volumeup"); assert(inp:keySeen("vol_up") and not inp:keySeen("power"))
eq(#inp:missing(17), 15); eq(inp:seen(17), 2)
for i = 1, 8 do inp:event("btn " .. i .. " DOWN", true) end
eq(#inp.log, model.LOG_LINES); eq(inp.log[1], "#8 btn 8 DOWN (held)"); inp:event("key x DOWN"); eq(inp.log[1], "#9 key x DOWN")

-- frame times: nothing until a second of frames, the warm-up ignored, late frames counted
local fr = model.newFrames()
for _ = 1, model.WARMUP do fr:add(1.0) end                           -- ignored
eq(fr:stats(), nil)
for _ = 1, 119 do fr:add(1 / 60) end
fr:add(3 / 60)
local st = fr:stats()
eq(st.frames, 120); eq(st.late, 1); assert(st.fps > 58 and st.fps < 60, "fps " .. st.fps); assert(math.abs(st.worst - 50) < 0.01)
has(model.framesLine(st), "late of 120"); eq(model.framesLine(nil), "not measured")

-- tones: the right channel is silent on "left", both start and end at zero (fades), the sweep stays in range
local left = model.TONES[1]
eq(left.id, "left")
local l, r = model.toneSample(left, 1000)
assert(l ~= 0 and r == 0, "left only")
local l0 = model.toneSample(left, 0); eq(l0, 0, "fades in from zero")
local le = model.toneSample(left, model.toneFrames(left)); assert(math.abs(le) < 1e-9, "fades out to zero")
for i = 0, model.toneFrames(model.TONES[5]) - 1, 997 do
  local a, b = model.toneSample(model.TONES[5], i)
  assert(math.abs(a) <= 0.5 and a == b, "sweep in range, both channels")
end
eq(model.toneFrames(left), 66150)

-- the report's grammar
local rep = model.newReport({ made = "2026-11-09 10:00:00", file = "r36s-report-11.5.txt" })
rep:section("x"); rep:line("PASS", "a: b", "c\nd"); rep:check(false, "e", "f"); rep:line("SKIP", "g", "h")
local text = rep:text()
local lines = {}
for ln in text:gmatch("([^\n]*)\n") do lines[#lines + 1] = ln end
eq(lines[1], "# r3i6nos device report"); eq(lines[#lines], "end")
has(text, "PASS a b: c d"); has(text, "FAIL e: f"); has(text, "PASS 1  FAIL 1  INFO 0  SKIP 1"); has(text, "EASYROMS/IO_Checker/r36s-report-11.5.txt")
assert(not pcall(rep.line, rep, "OK", "k", "v"), "only the four statuses")

local rp = model.newReport({})
model.reportInput(rp, inp, { name = "GO-Super Gamepad", guid = "1900bb3e", buttons = 17, axes = 4, hats = 0, gamepad = false,
                             axisNow = { 0.02, 0.3, 0, 0 } })
model.reportSound(rp, { left = "heard", right = "silent", both = "played" })
text = rp:text()
has(text, "PASS shape: 17 buttons, 4 axes, 0 hats"); has(text, "FAIL buttons pressed: 2 of 17; never: B(1) Y(4) L1(5)")
has(text, "FAIL chatter: A(2)=1"); has(text, "PASS axis 1 range: -0.95 to +0.97"); has(text, "SKIP axis 2 range: never moved")
has(text, "PASS axis 1 at rest: +0.02"); has(text, "FAIL axis 2 at rest: +0.30")
has(text, "PASS volume up key: arrived as a key"); has(text, "SKIP power key")
has(text, "PASS heard Left channel, 1 kHz: yes"); has(text, "FAIL heard Right channel, 1 kHz: no (silent)")
has(text, "SKIP heard Both channels, 1 kHz: played, not answered"); has(text, "SKIP heard Quiet, 1 kHz at 10%: not played")
rp = model.newReport({}); model.reportInput(rp, model.newInput(), nil); has(rp:text(), "FAIL pad: none")

-- mounts: the longest mount point that holds the path
local mounts = "/dev/root / ext4 rw 0 0\n/dev/mmcblk0p3 /roms exfat rw 0 0\n/dev/mmcblk1p1 /roms2 vfat rw 0 0\n"
eq(model.mountOf("/roms/IO_Checker", mounts).fs, "exfat"); eq(model.mountOf("/roms2", mounts).dev, "/dev/mmcblk1p1")
eq(model.mountOf("/romsX", mounts).at, "/", "a prefix is not a parent"); eq(model.gb(117.04e9), "117.0 GB"); eq(model.gb(5e8), "500 MB")

-- ---- probe ----
local sys = {}
for _, row in ipairs(probe.system()) do sys[row.label] = row.value end
eq(sys.r3i6nos, "0.1.0, 9e7a6b6"); eq(sys.Kernel, "4.4.189"); eq(sys.Board, "Game Console R35S/R36S fix by lxc")
eq(sys.CPU, "4 cores, 1512 MHz max"); eq(sys.Memory, "897 MB, 828 MB free"); eq(sys.Battery, "87%, Discharging"); eq(sys.Backlight, "128 of 255")
local snd = probe.sound()
eq(snd.card, "rockchip_rk817- - rockchip,rk817-codec"); eq(snd.path, "SPK"); eq(snd.headphones, "out"); eq(snd.jack, "rk817-sound Headphone Jack")

local cards = probe.cards()
eq(#cards, 2, "the data card is mounted"); eq(cards[1].mount.fs, "exfat"); eq(cards[2].mount.fs, "vfat")
local rows = probe.files()
local by = {}
for _, row in ipairs(rows) do by[row.label] = row end
eq(by["Games card: write, rename over, read, delete"].status, "PASS")
eq(by["Games card: read back"].status, "PASS"); eq(by["Data card: read back"].value, "every byte the same")
has(by["Games card: write speed"].value, "MB/s (1 MB + sync)"); has(by["Games card: where"].value, "exfat, /dev/mmcblk0p3")
local leftovers = io.popen(("ls -A '%s/roms/IO_Checker'"):format(root)):read("*a")
eq(leftovers, "", "the test files are deleted")
-- a card that cannot be written: one FAIL row, and no speed test
local bad = probe.cardCheck({ label = "Gone", path = root .. "/etc/r3i6nos-release/card", mount = nil })
eq(bad[2].status, "FAIL"); eq(#bad, 2)

-- ---- view: the screen as text ----
assert(theme.contrast(view.LIT.fg, view.LIT.bg) >= 4.5, "the held-button colours pass 4.5:1")
local scr = theme.screen()
local function pad(downs, axes)
  local set = {}
  for _, i in ipairs(downs or {}) do set[i] = true end
  return { name = "GO-Super Gamepad", guid = "1900bb3e4b4800000011000000010000", buttons = 17, axes = 4, hats = 0, gamepad = false, hat = "c",
           down = function(i) return set[i] == true end, axis = function(i) return (axes or {})[i] or 0 end }
end
local state = { page = "home", runtime = "LÖVE 11.5.0", sel = { home = 1, video = 1, sound = 1, files = 1, system = 1 }, input = inp,
                keysDown = {}, answers = { left = "heard" }, frames = fr, pad = pad(), reply = "hello", kind = "hint",
                video = { mode = "640x480, 60 Hz", renderer = "OpenGL ES" }, sound = snd, audioDevice = "OpenAL" }
local function dump() return view.render(scr, state):dump() end
local function cells(rowsText) return table.concat(rowsText, "\n") end
local out = dump()
eq(#out, 24); has(out[1], "IO Checker"); has(out[1], "LÖVE 11.5.0" == "" and "" or "11.5.0")
for c = 1, 64 do eq(scr.bg[c], theme.BLUE, "top bar") end
has(out[3], "Buttons and sticks"); has(out[3], "2/17 buttons"); has(out[4], "Video"); has(out[4], "59.0 fps")
has(out[5], "1/6 heard"); has(cells(out), "Every button, both sticks"); has(out[23], "open Buttons and sticks")
state.sel.home = 6; out = dump(); has(cells(out), "EASYROMS/IO_Checker/"); has(out[23], "open Device report")

state.page = "input"; state.pad = pad({ 2, 9, 15 }, { 0.9, -0.6, 0, 0 }); state.idle = model.newIdle(); state.chord = model.newChord()
out = dump()
has(out[3], "[L2]"); has(out[3], "[R2]"); has(out[6], "[Y]"); has(out[6], "[A]"); has(out[16], "L stick IN"); has(out[18], "[SELECT]")
has(out[20], "[VOL-]"); has(out[3], "Down  2 9 15"); has(cells(out), "Never 1 4 5"); has(cells(out), "Last events")
has(cells(out), "A1    +0.90  -0.95 +0.97")
-- the A button (col 29, row 5) is lit, the X button (col 26, row 4) is not
eq(scr.bg[5 * 64 + 29 + 1], theme.LIGHT_GREEN); eq(scr.bg[4 * 64 + 26 + 1], theme.BLACK)
-- the L stick's dot sits right of and above centre (axis 0.9, -0.6), and its frame is lit while L3 is in
eq(scr.ch[(8 + 3 - 1) * 64 + (2 + 4 + 3) + 1], 254, "stick dot")
state.idle:update(9); out = dump(); has(out[22], "back to the list in 6 s")
state.pad = nil; out = dump(); has(cells(out), "No gamepad has reached SDL.")
state.pad = pad()

state.page = "video"; out = dump()
has(out[2], "Video"); has(out[3], "Colour bars"); has(out[11], "Motion and frame rate"); has(out[11], "59.0 fps")
has(cells(out), "Mode       640x480, 60 Hz"); has(cells(out), "Look for   8 bars"); has(out[23], "show Colour bars")

state.page = "sound"; state.answers = { left = "heard", right = "silent", both = "played" }; out = dump()
has(out[3], "heard"); has(out[4], "SILENT"); has(out[5], "played"); has(cells(out), "Speaker path  SPK (speaker)")
has(cells(out), "A play  X heard  Y silent  B back")
state.sound = { card = "x", path = "OFF" }; out = dump(); has(cells(out), "OFF: the codec is silent")
local off = cells(out):find("OFF: the codec", 1, true); assert(off)
state.sound = snd

state.page = "files"; state.files = nil; out = dump(); has(cells(out), "Press A to test every card")
state.files = rows; out = dump(); has(out[3], "Games card: where"); has(cells(out), "Data card: read back")
state.page = "system"; state.system = probe.system(); out = dump(); has(out[3], "r3i6nos"); has(out[3], "0.1.0, 9e7a6b6")
-- nothing on any page runs past 64 columns or under row 23
for _, p in ipairs({ "home", "input", "video", "sound", "files", "system" }) do
  state.page = p
  eq(#dump(), 24)
end

-- ---- main.lua against a fake LÖVE ----
local function obj() return setmetatable({}, { __index = function() return function() end end }) end
local quitCalls, played = 0, {}
local function fake_love(joysticks)
  quitCalls = 0
  local clock = 0
  love = {
    _version = "11.5", _version_major = 11, _version_minor = 5, _version_revision = 0,
    mouse = { setVisible = function() end },
    graphics = setmetatable({
      getDimensions = function() return 640, 480 end,
      getRendererInfo = function() return "OpenGL ES", "2.0", "ARM", "Mali-G31" end,
      newImage = obj, newQuad = obj, newSpriteBatch = obj, newCanvas = obj,
    }, { __index = function() return function() end end }),
    image = { newImageData = function() return obj() end },
    window = { getMode = function() return 640, 480, { refreshrate = 60 } end },
    timer = { getTime = function() clock = clock + 0.05; return clock end }, -- 50 ms a call: no chatter between the test's presses
    audio = { newSource = function(sd) local src = obj(); src.play = function() played[#played + 1] = sd end; return src end },
    sound = { newSoundData = function(n, rate, bits, ch)
      assert(rate == 44100 and bits == 16 and ch == 2, "stereo 16-bit")
      return { n = n, setSample = function(_, i, c, v) assert(i >= 0 and i < n and (c == 1 or c == 2) and math.abs(v) <= 1) end }
    end },
    joystick = { getJoysticks = function() return joysticks end },
    event = { quit = function() quitCalls = quitCalls + 1 end },
  }
end

local function fake_pad(raw)
  local set = {}
  for _, n in ipairs(raw or {}) do set[n] = true end
  return {
    set = set,
    getName = function() return "GO-Super Gamepad" end, getGUID = function() return "1900bb3e4b4800000011000000010000" end,
    isGamepad = function() return false end, isGamepadDown = function() return false end, isConnected = function() return true end,
    getAxisCount = function() return 4 end, getButtonCount = function() return 17 end, getHatCount = function() return 0 end,
    getAxis = function(_, i) return i == 1 and 0.01 or 0 end, getHat = function() return "c" end,
    isDown = function(_, i) return set[i] == true end,
  }
end

local function load_main()
  for _, m in ipairs({ "model", "probe", "view" }) do package.loaded[m] = nil end
  local chunk = assert(loadfile(dir .. "main.lua"))
  chunk()
  -- point the freshly loaded probe at the fake system
  local p = require("probe")
  p.PROC, p.SYS, p.ETC = root .. "/proc", root .. "/sys", root .. "/etc"
  p.ROMS, p.ROMS2, p.SOUND, p.MB = root .. "/roms", root .. "/roms2", root .. "/bin/r3i6nos-sound", 1
end

local function frame(dt) love.update(dt or 0.016); love.draw() end
local function press(j, b) j.set[b] = true; love.joystickpressed(j, b); j.set[b] = nil; love.joystickreleased(j, b) end

-- 1. home, then every page and back with B; the screen draws on each
local j = fake_pad()
fake_love({ j }); load_main(); love.load(); frame()
press(j, 10); frame()                                    -- down: Video
press(j, 9); frame()                                     -- up: Buttons and sticks
press(j, 2); frame()                                     -- A: open it
for b = 1, 17 do press(j, b) end                         -- every button: on this page none of them navigates
love.joystickaxis(j, 1, 0.95); love.joystickaxis(j, 1, -0.95); love.joystickaxis(j, 1, 0)
love.keypressed("volumeup"); love.keyreleased("volumeup"); love.keypressed("power"); love.keyreleased("power")
frame(); eq(quitCalls, 0, "B on the input page does not leave the app")
-- hold Select+Start a second: back to the list, and still held it does not quit as well
j.set[13], j.set[14] = true, true
for _ = 1, 25 do frame(0.05) end
eq(quitCalls, 0, "back to the list, not out of the app")
for _ = 1, 25 do frame(0.05) end
eq(quitCalls, 0, "a held chord fires once")
j.set[13], j.set[14] = nil, nil; frame()

-- 2. video: show every pattern (left/right), the motion one measures, B comes back
press(j, 10); press(j, 2); frame()                      -- Video
press(j, 2); frame()                                     -- Colour bars
for _ = 1, #model.PATTERNS - 1 do press(j, 12); frame() end      -- right, right, ... to Motion
for _ = 1, 80 do frame(1 / 60) end
press(j, 1); frame()                                     -- B: back to the Video list, with the measurement
press(j, 1); frame()                                     -- B: home

-- 3. sound: play left, mark it heard; play right, mark it silent
press(j, 10); press(j, 2); frame()                       -- Sound (third item)
press(j, 2); frame()                                     -- A: play left
eq(#played, 1); eq(played[1].n, 66150)
press(j, 3); frame()                                     -- X: heard
press(j, 10); press(j, 2); press(j, 4); frame()          -- right: play, Y: silent
eq(#played, 2)
press(j, 1); frame()

-- 4. files: opening runs the test after the screen says so
press(j, 10); press(j, 2); frame(); frame(); frame()
press(j, 1); frame()

-- 5. system, then the device report
press(j, 10); press(j, 2); frame(); press(j, 1); frame()
press(j, 10); press(j, 2); frame(); frame(); frame()
local f = assert(io.open(root .. "/roms/IO_Checker/r36s-report-11.5.txt", "rb"), "the report is written")
local report = f:read("*a"); f:close()
local rl = {}
for ln in report:gmatch("([^\n]*)\n") do rl[#rl + 1] = ln end
eq(rl[1], "# r3i6nos device report"); eq(rl[#rl], "end")
has(report, "app: IO Checker 2.0 (r36s-io-checker, LÖVE 11.5.0)")
for _, sec in ipairs({ "== system ==", "== input ==", "== video ==", "== sound ==", "== files ==", "== summary ==" }) do has(report, sec) end
has(report, "PASS buttons pressed: all 17"); has(report, "PASS chatter: none"); has(report, "PASS power key"); has(report, "PASS volume up key")
has(report, "PASS axis 1 range: -0.95 to +0.95"); has(report, "PASS axis 1 at rest: +0.01")
has(report, "PASS window: 640x480"); has(report, "motion: "); has(report, "INFO patterns shown: bars white red green blue black ramps grid motion")
has(report, "PASS speaker path: SPK"); has(report, "PASS heard Left channel, 1 kHz: yes"); has(report, "FAIL heard Right channel, 1 kHz: no (silent)")
has(report, "PASS Games card read back"); has(report, "PASS Data card write, rename over, read, delete")
has(report, "INFO Kernel: 4.4.189"); has(report, "INFO GUID: 1900bb3e4b4800000011000000010000")
for ln in report:gmatch("([^\n]*)\n") do
  assert(ln == "" or ln == "end" or ln:match("^#") or ln:match("^%a+: ") or ln:match("^== .+ ==$") or ln:match("^PASS %d+  FAIL")
    or ln:match("^PASS [^:]+: ") or ln:match("^FAIL [^:]+: ") or ln:match("^INFO [^:]+: ") or ln:match("^SKIP [^:]+: "),
    "a line in the grammar: " .. ln)
end
eq(io.open(root .. "/roms/IO_Checker/r36s-report-11.5.txt.tmp"), nil, "renamed into place")

-- 6. Quit from the list, B from the list, and Select+Start on the list all leave
press(j, 10); press(j, 2); eq(quitCalls, 1, "Quit")
fake_love({ j }); load_main(); love.load(); frame(); press(j, 1); eq(quitCalls, 1, "B on the home list quits")
fake_love({ j }); load_main(); love.load(); j.set[13], j.set[14] = true, true
for _ = 1, 25 do frame(0.05) end
eq(quitCalls, 1, "Select+Start on the home list quits"); j.set[13], j.set[14] = nil, nil

-- 7. idle on the input page goes back to the list by itself (a dead Select or Start cannot trap anyone)
fake_love({ j }); load_main(); love.load(); press(j, 2); frame()
for _ = 1, 16 do frame(1) end
press(j, 1); eq(quitCalls, 1, "back on the list after 15 s idle, so B quits")

-- 8. no pad at all: the pages still draw, the keyboard works, and nothing crashes
fake_love({}); load_main(); love.load(); frame()
love.keypressed("return"); frame(); love.keypressed("escape"); frame()
love.keypressed("down"); love.keypressed("down"); love.keypressed("return"); frame()
love.keypressed("h"); frame()
love.keypressed("escape"); love.keypressed("escape"); eq(quitCalls, 1, "Esc twice: back, then out")

-- 9. a keyboard on the Sound page: Enter plays, H marks heard
fake_love({ j }); load_main(); love.load(); played = {}
love.keypressed("down"); love.keypressed("down"); love.keypressed("return"); love.keypressed("return"); love.keypressed("h"); frame()
eq(#played, 1)

sh(("rm -rf '%s'"):format(root))
print("io checker: ok")
