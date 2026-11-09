-- IO Checker: the pure logic (no LÖVE calls), so plain luajit tests it (tests/test_io_checker.lua).
-- The pad's layout, the input record (presses, chatter, axis ranges, keys), the exit chord and idle timer, the frame-time
-- statistics of the motion test, the test tones, the video patterns and the device report's text.
-- docs/IO-CHECKER.md is the spec; the native build (apps/io-checker-native/io_checker.c) follows the same numbers.

local M = {}

M.VERSION = "2.0"
M.ZONE = 0.5            -- an axis counts as "pushed" past this
M.EXIT_HOLD = 1.0       -- seconds Select+Start must be held
M.IDLE_LIMIT = 15       -- seconds without input before the input page goes back to the list by itself
M.IDLE_WARN = 10        -- ... and starts counting down
M.CHATTER = 0.03        -- a press this soon (s) after the same button's release counts as chatter (a bouncing contact)
M.LOG_LINES = 6

-- The R36S's raw button numbers as LÖVE reports them (1-based; SDL's own index is one less), measured on a real unit on
-- 2026-11-07 (docs/hardware-findings.md): the D-pad is four buttons, not a hat, and SDL's mapping for the pad is wrong.
M.LAYOUT = {
  b = 1, a = 2, x = 3, y = 4,
  l1 = 5, r1 = 6, l2 = 7, r2 = 8,
  up = 9, down = 10, left = 11, right = 12,
  select = 13, start = 14, l3 = 15, r3 = 16, fn = 17,
}
-- The button names in LÖVE's numbering, for the report and the missing list.
M.NAMES = { "B", "A", "X", "Y", "L1", "R1", "L2", "R2", "Up", "Down", "Left", "Right", "Select", "Start", "L3", "R3", "Fn" }

-- Power and the volume keys are not on the joystick (17 buttons, all taken): they arrive as keyboard keys.
M.KEYS = {
  vol_down = { "volumedown", "audiolower" },
  vol_up = { "volumeup", "audioraise" },
  power = { "power", "sleep" },
}

--- Hat direction string ("u", "dl", "c", ...) to four booleans.
function M.hat(dir)
  dir = dir or "c"
  return {
    up = dir:find("u", 1, true) ~= nil, down = dir:find("d", 1, true) ~= nil,
    left = dir:find("l", 1, true) ~= nil, right = dir:find("r", 1, true) ~= nil,
  }
end

--- Which D-pad directions are pressed: the four buttons, or a hat if the pad has one. down(i) says whether raw button i is held.
function M.dpad(hatDir, down)
  local h, L = M.hat(hatDir), M.LAYOUT
  return { up = h.up or down(L.up), down = h.down or down(L.down), left = h.left or down(L.left), right = h.right or down(L.right) }
end

--- Is Select+Start held? Raw buttons first (the pad's mapping is wrong); SDL's mapping also counts. gamepad(name) may be nil.
function M.exitHeld(down, gamepad)
  if down(M.LAYOUT.select) and down(M.LAYOUT.start) then return true end
  return gamepad ~= nil and gamepad("back") and gamepad("start") or false
end

--- Is one of a group's key names down? held is the set of names currently held.
function M.keyHeld(held, group)
  for _, name in ipairs(M.KEYS[group]) do
    if held[name] then return true end
  end
  return false
end

--- -1, 0 or 1: which side of the zone an axis value is on.
function M.zone(v)
  if v <= -M.ZONE then return -1 end
  if v >= M.ZONE then return 1 end
  return 0
end

function M.fmt(v) return string.format("%+.2f", v or 0) end

--- Runtime name: "LÖVE 11.5.0" from the version numbers LÖVE gives.
function M.runtime(version, major, minor, revision)
  if major then return ("LÖVE %d.%d.%d"):format(major, minor or 0, revision or 0) end
  return "LÖVE " .. tostring(version or "?")
end

--- The report's file name for a runtime: one file a build, so the three builds never overwrite each other.
function M.reportName(major, minor)
  if major then return ("r36s-report-%d.%d.txt"):format(major, minor or 0) end
  return "r36s-report-native.txt"
end

-- ---- timers -----------------------------------------------------------------------------------------------------------------------------

--- Exit chord: held() must stay true for hold seconds in a row.
function M.newChord(hold)
  local c = { hold = hold or M.EXIT_HOLD, t = 0 }
  function c:update(dt, held)
    if held then self.t = self.t + dt else self.t = 0 end
    return self.t >= self.hold
  end
  function c:progress() return math.min(1, self.t / self.hold) end
  return c
end

--- No input for limit seconds: the input page goes back by itself, so a unit whose Select or Start is dead cannot get stuck there.
function M.newIdle(limit, warn)
  local idle = { limit = limit or M.IDLE_LIMIT, warn = warn or M.IDLE_WARN, t = 0 }
  function idle:touch() self.t = 0 end
  function idle:update(dt) self.t = self.t + dt end
  function idle:remaining() return math.max(0, self.limit - self.t) end
  function idle:warning() return self:remaining() <= self.warn end
  function idle:expired() return self.t >= self.limit end
  return idle
end

-- ---- the input record -------------------------------------------------------------------------------------------------------------------

--- Everything the pad and keys did since the app started: the checklist of a hardware test.
--- press/release take the time in seconds (for chatter); axis keeps each axis's range; the log keeps the last events, newest first.
function M.newInput()
  local r = { presses = {}, chatter = {}, lastUp = {}, axes = {}, hats = {}, keys = {}, log = {}, seq = 0, maxLog = M.LOG_LINES }
  function r:event(what, isHeld)
    self.seq = self.seq + 1
    local line = ("#%d %s"):format(self.seq, what)
    if isHeld ~= nil then line = line .. (isHeld and " (held)" or " (free)") end -- a DOWN that says "free" means events and state disagree
    table.insert(self.log, 1, line)
    while #self.log > self.maxLog do table.remove(self.log) end
  end
  function r:note(text)
    table.insert(self.log, 1, text)
    while #self.log > self.maxLog do table.remove(self.log) end
  end
  function r:press(b, t)
    self.presses[b] = (self.presses[b] or 0) + 1
    if self.lastUp[b] and t and t - self.lastUp[b] < M.CHATTER then self.chatter[b] = (self.chatter[b] or 0) + 1 end
  end
  function r:release(b, t) self.lastUp[b] = t end
  function r:axis(a, v)
    local x = self.axes[a]
    if not x then x = { min = v, max = v }; self.axes[a] = x end
    if v < x.min then x.min = v end
    if v > x.max then x.max = v end
  end
  function r:hat(dir) if dir and dir ~= "c" then self.hats[dir] = true end end
  function r:key(name) self.keys[name] = (self.keys[name] or 0) + 1 end
  --- Buttons 1..n never pressed.
  function r:missing(n)
    local out = {}
    for i = 1, n do
      if not self.presses[i] then out[#out + 1] = i end
    end
    return out
  end
  function r:seen(n) return n - #self:missing(n) end
  --- Did a key of this group (M.KEYS) ever arrive?
  function r:keySeen(group)
    for _, name in ipairs(M.KEYS[group]) do
      if self.keys[name] then return true end
    end
    return false
  end
  return r
end

--- "Up(9)": a button's name and number, for the missing list and the report.
function M.buttonName(i)
  return M.NAMES[i] and ("%s(%d)"):format(M.NAMES[i], i) or tostring(i)
end

-- ---- the motion test: frame times -------------------------------------------------------------------------------------------------------

M.WARMUP = 10           -- frames ignored at the start (the first after a page change are slow for reasons of their own)

--- Frame intervals of the motion pattern. add(dt) each frame; stats() once there are enough.
function M.newFrames()
  local f = { dts = {}, skipped = 0 }
  function f:reset() self.dts, self.skipped = {}, 0 end
  function f:add(dt)
    if self.skipped < M.WARMUP then self.skipped = self.skipped + 1; return end
    self.dts[#self.dts + 1] = dt
    if #self.dts > 600 then table.remove(self.dts, 1) end -- the last 10 s at 60 fps
  end
  --- nil until a second of frames; else { frames, fps, mean (ms), worst (ms), late (frames over 1.5 x the mean) }.
  function f:stats()
    local n, sum, worst = #self.dts, 0, 0
    for _, d in ipairs(self.dts) do sum = sum + d; if d > worst then worst = d end end
    if n < 30 or sum < 1 then return nil end
    local mean, late = sum / n, 0
    for _, d in ipairs(self.dts) do if d > mean * 1.5 then late = late + 1 end end
    return { frames = n, fps = n / sum, mean = mean * 1000, worst = worst * 1000, late = late }
  end
  return f
end

--- The motion stats as one line.
function M.framesLine(s)
  if not s then return "not measured" end
  return ("%.1f fps, %.1f ms mean, %.1f ms worst, %d late of %d"):format(s.fps, s.mean, s.worst, s.late, s.frames)
end

-- ---- video patterns ---------------------------------------------------------------------------------------------------------------------

-- Full-screen test patterns: the one place the app leaves the text grid (docs/IO-CHECKER.md says why).
M.PATTERNS = {
  { id = "bars", label = "Colour bars", look = "8 bars, then the 16 VGA colours: none missing, none the same" },
  { id = "white", label = "White", look = "stuck dark pixels, an even backlight" },
  { id = "red", label = "Red", look = "stuck pixels; no green or blue tint" },
  { id = "green", label = "Green", look = "stuck pixels; no red or blue tint" },
  { id = "blue", label = "Blue", look = "stuck pixels; no red or green tint" },
  { id = "black", label = "Black", look = "stuck bright pixels, backlight bleed" },
  { id = "ramps", label = "Grey and colour ramps", look = "smooth ramps; steps mean fewer colour bits" },
  { id = "grid", label = "Pixel grid and edges", look = "an even grey checker (1:1 pixels), all four edge lines" },
  { id = "motion", label = "Motion and frame rate", look = "a smooth bar; the rate is measured" },
}

-- ---- test tones -------------------------------------------------------------------------------------------------------------------------

M.RATE = 44100
M.TONES = {
  -- 1 kHz, not 440 Hz: on the unit every 440 Hz tone was marked silent from the speaker and the sweep heard (2026-11-09,
  -- docs/hardware-findings.md); a small speaker gives little below about 500 Hz. "low" keeps 440 Hz to measure that.
  { id = "left", label = "Left channel, 1 kHz", freq = 1000, left = 1, right = 0, amp = 0.5, secs = 1.5 },
  { id = "right", label = "Right channel, 1 kHz", freq = 1000, left = 0, right = 1, amp = 0.5, secs = 1.5 },
  { id = "both", label = "Both channels, 1 kHz", freq = 1000, left = 1, right = 1, amp = 0.5, secs = 1.5 },
  { id = "low", label = "Low, 440 Hz", freq = 440, left = 1, right = 1, amp = 0.5, secs = 1.5 },
  { id = "sweep", label = "Sweep, 100 Hz to 8 kHz", from = 100, to = 8000, left = 1, right = 1, amp = 0.5, secs = 4 },
  { id = "quiet", label = "Quiet, 1 kHz at 10%", freq = 1000, left = 1, right = 1, amp = 0.1, secs = 1.5 },
}
M.FADE = 0.02           -- seconds of fade in and out, so a tone starts and stops without a click

--- Sample i (0-based) of a tone at rate: left, right in -1..1. The sweep rises exponentially (equal time per octave).
function M.toneSample(tone, i, rate)
  rate = rate or M.RATE
  local t, len = i / rate, tone.secs
  local phase
  if tone.from then
    local k = math.log(tone.to / tone.from)
    phase = 2 * math.pi * tone.from * len / k * (math.exp(k * t / len) - 1)
  else
    phase = 2 * math.pi * tone.freq * t
  end
  local env = math.min(1, t / M.FADE, math.max(0, len - t) / M.FADE)
  local v = math.sin(phase) * tone.amp * env
  return v * tone.left, v * tone.right
end

function M.toneFrames(tone, rate) return math.floor(tone.secs * (rate or M.RATE)) end

-- ---- the device report ------------------------------------------------------------------------------------------------------------------

--- The report in the grammar of docs/DEVICE-REPORTS.md: header, sections of "STATUS key: value" lines, a summary, "end".
function M.newReport(o)
  local r = { lines = {}, counts = { PASS = 0, FAIL = 0, INFO = 0, SKIP = 0 } }
  r.lines[1] = "# r3i6nos device report"
  r.lines[2] = ("app: IO Checker %s (%s)"):format(o.version or M.VERSION, o.package or "r36s-io-checker")
  r.lines[3] = "made: " .. (o.made or "?")
  r.lines[4] = "how: " .. (o.how or "Device report")
  r.lines[5] = ("for: Claude. Copy EASYROMS/IO_Checker/%s to a computer and give it to Claude in the r3i6nos repo."):format(o.file or "r36s-report.txt")
  function r:section(name)
    self.lines[#self.lines + 1] = ""
    self.lines[#self.lines + 1] = ("== %s =="):format(name)
  end
  function r:line(status, key, value)
    assert(self.counts[status], "status must be PASS, FAIL, INFO or SKIP: " .. tostring(status))
    key = tostring(key):gsub("[\r\n\t]", " "):gsub("%s*:%s*", " ") -- the first colon ends the key
    value = tostring(value):gsub("[\r\n\t]", " ")
    self.counts[status] = self.counts[status] + 1
    self.lines[#self.lines + 1] = ("%s %s: %s"):format(status, key, value)
  end
  function r:check(ok, key, value) self:line(ok and "PASS" or "FAIL", key, value) end
  function r:text()
    local c = self.counts
    local out = {}
    for i, l in ipairs(self.lines) do out[i] = l end
    out[#out + 1] = ""
    out[#out + 1] = "== summary =="
    out[#out + 1] = ("PASS %d  FAIL %d  INFO %d  SKIP %d"):format(c.PASS, c.FAIL, c.INFO, c.SKIP)
    out[#out + 1] = "end"
    return table.concat(out, "\n") .. "\n"
  end
  return r
end

--- The input section of the report, from the record and the pad's shape (pad: name, guid, buttons, axes, hats, gamepad).
function M.reportInput(r, input, pad)
  r:section("input")
  if not pad then
    r:line("FAIL", "pad", "none: the pad did not reach SDL")
  else
    r:line("INFO", "pad", pad.name)
    r:line("INFO", "guid", pad.guid)
    r:check(pad.buttons == 17 and pad.axes == 4, "shape", ("%d buttons, %d axes, %d hats (R36S: 17, 4, 0)"):format(pad.buttons, pad.axes, pad.hats))
    r:line("INFO", "sdl gamepad mapping", pad.gamepad and "yes" or "no")
    local missing = input:missing(pad.buttons)
    local names = {}
    for _, i in ipairs(missing) do names[#names + 1] = M.buttonName(i) end
    if input.seq == 0 then
      r:line("SKIP", "buttons pressed", "none: open Buttons and sticks and press every button first")
    else
      r:check(#missing == 0, "buttons pressed", #missing == 0 and ("all %d"):format(pad.buttons)
        or ("%d of %d; never: %s"):format(pad.buttons - #missing, pad.buttons, table.concat(names, " ")))
    end
    local presses, chat = {}, {}
    for i = 1, pad.buttons do
      if input.presses[i] then presses[#presses + 1] = ("%s=%d"):format(M.buttonName(i), input.presses[i]) end
      if input.chatter[i] then chat[#chat + 1] = ("%s=%d"):format(M.buttonName(i), input.chatter[i]) end
    end
    if #presses > 0 then r:line("INFO", "presses", table.concat(presses, " ")) end
    if input.seq > 0 then
      r:check(#chat == 0, "chatter", #chat == 0 and ("none (no press within %d ms of a release)"):format(M.CHATTER * 1000)
        or table.concat(chat, " "))
    end
    for a = 1, pad.axes do
      local x = input.axes[a]
      if x then
        r:check(x.min <= -0.9 and x.max >= 0.9, ("axis %d range"):format(a), ("%s to %s (goal: past -0.90 and +0.90)"):format(M.fmt(x.min), M.fmt(x.max)))
      else
        r:line("SKIP", ("axis %d range"):format(a), "never moved")
      end
      if pad.axisNow then
        local v = pad.axisNow[a] or 0
        r:check(math.abs(v) < 0.1, ("axis %d at rest"):format(a), ("%s when the report ran (goal: within 0.10 of centre; hands off the sticks)"):format(M.fmt(v)))
      end
    end
    local dirs = {}
    for d in pairs(input.hats) do dirs[#dirs + 1] = d end
    table.sort(dirs)
    r:line("INFO", "hat directions", #dirs > 0 and table.concat(dirs, " ") or "none")
  end
  for _, g in ipairs({ { "power", "power key" }, { "vol_down", "volume down key" }, { "vol_up", "volume up key" } }) do
    if input:keySeen(g[1]) then r:line("PASS", g[2], "arrived as a key")
    else r:line("SKIP", g[2], "not pressed on Buttons and sticks (or it never reaches apps)") end
  end
  local keys = {}
  for k, n in pairs(input.keys) do keys[#keys + 1] = ("%s=%d"):format(k, n) end
  table.sort(keys)
  if #keys > 0 then r:line("INFO", "keys", table.concat(keys, " ")) end
end

--- The sound answers: heard (PASS), silent (FAIL), played but not answered or never played (SKIP).
function M.reportSound(r, answers)
  for _, t in ipairs(M.TONES) do
    local a = answers[t.id]
    if a == "heard" then r:line("PASS", "heard " .. t.label, "yes")
    elseif a == "silent" then r:line("FAIL", "heard " .. t.label, "no (silent)")
    elseif a == "played" then r:line("SKIP", "heard " .. t.label, "played, not answered (X heard, Y silent)")
    else r:line("SKIP", "heard " .. t.label, "not played") end
  end
end

--- What the "Sound" list shows in each tone's value column.
function M.answerText(a)
  return ({ heard = "heard", silent = "SILENT", played = "played", playing = "playing" })[a or ""] or ""
end

-- ---- small helpers ----------------------------------------------------------------------------------------------------------------------

--- A byte count as "117.0 GB" (10^9, as the system's report and About count).
function M.gb(n)
  if not n then return "?" end
  if n >= 1e9 then return ("%.1f GB"):format(n / 1e9) end
  return ("%.0f MB"):format(n / 1e6)
end

--- The mount that holds path, from /proc/mounts text: device, mount point, filesystem type (the longest matching mount point).
function M.mountOf(path, mounts)
  local best
  for line in (mounts or ""):gmatch("[^\n]+") do
    local dev, at, fs = line:match("^(%S+)%s+(%S+)%s+(%S+)")
    if at then
      at = at:gsub("\\040", " ")
      local inside = path == at or (at == "/" ) or path:sub(1, #at + 1) == at .. "/"
      if inside and (not best or #at > #best.at) then best = { dev = dev, at = at, fs = fs } end
    end
  end
  return best
end

return M
