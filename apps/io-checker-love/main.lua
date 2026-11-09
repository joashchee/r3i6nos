-- IO Checker for LÖVE 11.5 and 12.0 (one app, packed twice; it says which runtime it runs on). docs/IO-CHECKER.md is the spec.
-- Checks what goes in (every button, both sticks, the D-pad, the power and volume keys) and what comes out (the screen with
-- test patterns and a measured frame rate, sound with test tones you mark heard or silent, files on both cards), shows the
-- system, and writes it all to a device report on the games card (docs/DEVICE-REPORTS.md).
-- Drawn on the r3i6nos-ansiapps theme (64x24 cells) except the video patterns, which have to be pixels.
-- The list pages follow the theme's keys (D-pad, A, B, L1/R1, L2/R2); hold Select+Start for a second to go back or quit.

package.path = (os.getenv("R3I6NOS_THEME") or "/opt/loveos/theme") .. "/?.lua;" .. package.path

local theme = require("r3i6nos_ansiapps")
local model = require("model")
local probe = require("probe")
local view = require("view")

local L = model.LAYOUT
local s = {
  page = "home",
  sel = { home = 1, video = 1, sound = 1, files = 1, system = 1 },
  input = model.newInput(),
  keysDown = {},
  idle = model.newIdle(),
  chord = model.newChord(),
  frames = model.newFrames(),
  answers = {},
  reply = "Pick a check with the D-pad and A.",
  kind = "hint",
}
local screen, joy
local latch = false       -- the exit chord fired: it must be let go before it can fire again
local pending             -- a slow job ("files", "report") that runs after the screen has said so
local pendingShown = false
local motion = { x = 0, n = 0 }
local tones, source = {}, nil
local seen = {}           -- the patterns looked at, for the report

local function say(text, kind) s.reply, s.kind = text, kind or "hint" end
local function now() return love.timer and love.timer.getTime() or os.clock() end

-- ---- the pad ----------------------------------------------------------------------------------------------------------------------

local function padOf(j)
  if not j then return nil end
  local nb, na, nh = j:getButtonCount(), j:getAxisCount(), j:getHatCount()
  return {
    name = j:getName(), guid = j:getGUID(), buttons = nb, axes = na, hats = nh, gamepad = j:isGamepad(),
    down = function(i) return i <= nb and j:isDown(i) end,
    axis = function(i) return i <= na and j:getAxis(i) or 0 end,
    hat = nh > 0 and j:getHat(1) or "c",
  }
end

local function pick()
  joy = love.joystick.getJoysticks()[1]
  s.pad = padOf(joy)
end

-- ---- what the system says ---------------------------------------------------------------------------------------------------------

local function videoInfo()
  local w, h, flags = love.window.getMode()
  local ok, name, version, vendor, device = pcall(love.graphics.getRendererInfo)
  s.video = {
    mode = ("%dx%d, %s Hz"):format(w, h, flags and flags.refreshrate and flags.refreshrate > 0 and flags.refreshrate or "?"),
    renderer = ok and ("%s %s, %s %s"):format(name, version, vendor or "", device or "") or "?",
    w = w, h = h, refresh = flags and flags.refreshrate,
  }
  local b = probe.backlight()
  s.backlightText = b and ("%d of %d (%s)"):format(b.now, b.max, b.name) or "none found"
end

local function audioInfo()
  s.sound = probe.sound()
  if not love.audio then
    s.audioDevice, s.audioError = "LÖVE's audio module did not start", true
    return
  end
  local ok, dev = pcall(function() return love.audio.getPlaybackDevice and love.audio.getPlaybackDevice() end)
  s.audioDevice = "OpenAL" .. ((ok and dev) and (", " .. dev) or "") .. (", %d Hz"):format(model.RATE)
end

local function systemRows()
  local rows = probe.system()
  local function add(label, value) rows[#rows + 1] = { label = label, value = value or "?" } end
  add("Runs on", s.runtime)
  add("Screen", s.video.mode)
  add("Renderer", s.video.renderer)
  local p = s.pad
  add("Pad", p and p.name or "none")
  add("GUID", p and p.guid or "-")
  add("Pad shape", p and ("%d buttons, %d axes, %d hats"):format(p.buttons, p.axes, p.hats) or "-")
  add("SDL mapping", p and (p.gamepad and "yes" or "no (raw buttons are right)") or "-")
  add("Sound card", s.sound.card)
  add("Speaker path", s.sound.path or "unknown")
  return rows
end

-- ---- sound ------------------------------------------------------------------------------------------------------------------------

local function play(i)
  local t = model.TONES[i]
  if not love.audio or not love.sound then say("No sound: LÖVE's audio module did not start.", "error"); return end
  if not tones[t.id] then
    local n = model.toneFrames(t)
    local sd = love.sound.newSoundData(n, model.RATE, 16, 2)
    for k = 0, n - 1 do
      local l, r = model.toneSample(t, k)
      sd:setSample(k, 1, l)
      sd:setSample(k, 2, r)
    end
    tones[t.id] = sd
  end
  if source then source:stop() end
  local ok, err = pcall(function()
    source = love.audio.newSource(tones[t.id], "static")
    source:play()
  end)
  if not ok then say("Could not play: " .. tostring(err), "error"); s.audioError = true; return end
  s.answers[t.id] = "played"
  say(("Playing %s. Heard it? X yes, Y no."):format(t.label))
end

local function answer(i, a)
  local t = model.TONES[i]
  s.answers[t.id] = a
  say(("%s: %s."):format(t.label, a == "heard" and "heard" or "silent"), a == "heard" and "ok" or "warn")
end

-- ---- files and the report ---------------------------------------------------------------------------------------------------------

local function runFiles()
  s.files = probe.files()
  local bad = 0
  for _, r in ipairs(s.files) do if r.status == "FAIL" then bad = bad + 1 end end
  say(bad == 0 and "Every card passed." or ("%d check(s) failed: see the list."):format(bad), bad == 0 and "ok" or "error")
end

local function writeReport()
  local file = model.reportName(love._version_major, love._version_minor)
  local r = model.newReport({ made = os.date("%Y-%m-%d %H:%M:%S"), how = "Device report (" .. s.runtime .. ")", file = file,
                              package = "r36s-io-checker, " .. s.runtime })
  r:section("system")
  for _, row in ipairs(systemRows()) do r:line("INFO", row.label, row.value) end
  local p = s.pad
  if p then
    p.axisNow = {}
    for a = 1, p.axes do p.axisNow[a] = p.axis(a) end
  end
  model.reportInput(r, s.input, p)

  r:section("video")
  r:line("INFO", "mode", s.video.mode)
  r:line("INFO", "renderer", s.video.renderer)
  r:check(s.video.w == 640 and s.video.h == 480, "window", ("%dx%d (goal 640x480)"):format(s.video.w, s.video.h))
  local st = s.frames:stats()
  if st then r:check(st.fps >= 55 and st.late <= st.frames / 100, "motion", model.framesLine(st) .. " (goal 55+ fps, under 1% late)")
  else r:line("SKIP", "motion", "not measured: show Video > Motion and frame rate for a few seconds first") end
  local looked = {}
  for _, pat in ipairs(model.PATTERNS) do if seen[pat.id] then looked[#looked + 1] = pat.id end end
  r:line("INFO", "patterns shown", #looked > 0 and table.concat(looked, " ") or "none")
  r:line("INFO", "how they looked", "not judged by a program: say in words what was wrong, if anything")
  r:line("INFO", "backlight", s.backlightText)

  r:section("sound")
  audioInfo()
  r:line("INFO", "card", s.sound.card)
  local path = s.sound.path
  if path then r:check(path ~= "OFF", "speaker path", path .. (path == "OFF" and " (the codec is silent)" or ""))
  else r:line("SKIP", "speaker path", "unknown (no r3i6nos-sound, or another codec)") end
  r:line("INFO", "headphones", (s.sound.headphones or "?") .. ", jack " .. (s.sound.jack or "none"))
  r:check(not s.audioError and love.audio ~= nil, "output opened", s.audioDevice or "?")
  model.reportSound(r, s.answers)

  r:section("files")
  runFiles()
  for _, row in ipairs(s.files) do r:line(row.status, row.label, row.value) end
  local hwm = (probe.read("/proc/self/status") or ""):match("VmHWM:%s*(%d+)")
  r:line("INFO", "peak memory", hwm and ("%d MB"):format(math.floor(tonumber(hwm) / 1024)) or "?")

  local dir = probe.ROMS .. "/" .. probe.FOLDER
  os.execute("mkdir -p '" .. dir:gsub("'", "'\\''") .. "' 2>/dev/null")
  local path, tmp = dir .. "/" .. file, dir .. "/" .. file .. ".tmp"
  local f = io.open(tmp, "wb")
  local ok = f and f:write(r:text()) and f:close()
  if ok and os.rename(tmp, path) then
    s.reportFile = path
    local c = r.counts
    say(("Written: EASYROMS/%s/%s (PASS %d FAIL %d SKIP %d)"):format(probe.FOLDER, file, c.PASS, c.FAIL, c.SKIP), c.FAIL == 0 and "ok" or "warn")
  else
    os.remove(tmp)
    say("Could not write the report in " .. dir, "error")
  end
end

-- ---- pages and actions ------------------------------------------------------------------------------------------------------------

local function go(page)
  s.page = page
  s.chord.t = 0
  s.idle:touch()
  if page == "input" then say("Press every button. Hold SELECT+START to go back.") end
  if page == "system" then s.system = systemRows() end
  if page == "sound" then audioInfo() end
  if page == "video" then videoInfo() end
end

local function startJob(job, text)
  pending, pendingShown = job, false
  say(text, "warn")
end

local PER = { home = #view.HOME, video = #model.PATTERNS, sound = #model.TONES, files = theme.listRows(theme.DISPLAY_ROWS),
              system = theme.listRows(theme.DISPLAY_ROWS) }

local function count(page)
  if page == "home" then return #view.HOME end
  if page == "video" then return #model.PATTERNS end
  if page == "sound" then return #model.TONES end
  if page == "files" then return #(s.files or {}) end
  if page == "system" then return #(s.system or {}) end
  return 0
end

local function showPattern(i)
  s.sel.video = (i - 1) % #model.PATTERNS + 1
  local p = model.PATTERNS[s.sel.video]
  seen[p.id] = true
  if p.id == "motion" then s.frames:reset(); motion.x, motion.n = 0, 0 end
  s.page = "pattern"
end

local function act(a)
  local page = s.page
  if page == "pattern" then
    if a == "left" or a == "pageup" or a == "up" then showPattern(s.sel.video - 1)
    elseif a == "right" or a == "pagedown" or a == "down" then showPattern(s.sel.video + 1)
    elseif a == "back" or a == "accept" then
      local p = model.PATTERNS[s.sel.video]
      s.page = "video"
      if p.id == "motion" then say("Measured: " .. model.framesLine(s.frames:stats()), s.frames:stats() and "ok" or "warn") end
    end
    return
  end
  if page == "input" then return end
  local sel, ev = theme.listKey(s.sel[page], count(page), PER[page], a)
  s.sel[page] = sel
  if page == "home" then
    if ev == "back" then love.event.quit() return end
    if ev == "accept" then
      local id = view.HOME[sel].id
      if id == "quit" then love.event.quit()
      elseif id == "report" then startJob("report", "Writing the device report (the card test takes a few seconds)...")
      elseif id == "files" then go("files"); if not s.files then startJob("files", "Testing the cards...") end
      else go(id) end
    end
  elseif page == "video" then
    if ev == "back" then go("home") elseif ev == "accept" then showPattern(sel) end
  elseif page == "sound" then
    if ev == "back" then go("home")
    elseif ev == "accept" then play(sel)
    elseif a == "x" then answer(sel, "heard")
    elseif a == "y" then answer(sel, "silent") end
  elseif page == "files" then
    if ev == "back" then go("home") elseif ev == "accept" then startJob("files", "Testing the cards...") end
  elseif ev == "back" then
    go("home")
  end
end

-- ---- LÖVE callbacks ---------------------------------------------------------------------------------------------------------------

function love.load()
  love.mouse.setVisible(false)
  theme.init()
  screen = theme.screen()
  s.runtime = model.runtime(love._version, love._version_major, love._version_minor, love._version_revision)
  probe.clock = now
  pick()
  videoInfo()
  audioInfo()
end

function love.joystickadded(j)
  if not joy then pick() end
  s.input:note("pad added: " .. j:getName())
  s.idle:touch()
end

function love.joystickremoved(j)
  s.input:note("pad removed")
  if joy == j then pick() end
end

function love.joystickpressed(j, b)
  s.idle:touch()
  s.input:press(b, now())
  s.input:event(("btn %d DOWN"):format(b), j:isDown(b))
  if theme.PAD[b] then act(theme.PAD[b]) end
end

function love.joystickreleased(j, b)
  s.idle:touch()
  s.input:release(b, now())
  s.input:event(("btn %d UP"):format(b), j:isDown(b))
end

function love.joystickhat(_, h, dir)
  s.idle:touch()
  s.input:hat(dir)
  s.input:event(("hat %d %s"):format(h, dir), dir ~= "c")
end

local zones = {}
function love.joystickaxis(_, a, v)
  s.input:axis(a, v)
  local z = model.zone(v)
  if z ~= (zones[a] or 0) then
    zones[a] = z
    if z ~= 0 then s.idle:touch() end
    s.input:event(("axis %d %s"):format(a, z == 0 and "centre" or model.fmt(v)), z ~= 0)
  end
end

-- On a keyboard: the theme's keys, H heard and N not heard on Sound. Every key is recorded (power and volume arrive as keys).
function love.keypressed(k)
  s.keysDown[k] = true
  s.input:key(k)
  s.input:event("key " .. k .. " DOWN")
  s.idle:touch()
  if s.page == "input" then
    if k == "escape" then go("home") end
    return
  end
  if k == "h" then act("x") elseif k == "n" then act("y")
  elseif theme.KEYS[k] then act(theme.KEYS[k]) end
end

function love.keyreleased(k)
  s.keysDown[k] = nil
  s.input:event("key " .. k .. " UP")
end

function love.update(dt)
  if joy and not joy:isConnected() then pick() end
  if joy then s.pad.hat = s.pad.hats > 0 and joy:getHat(1) or "c" end
  if pending and pendingShown then
    local job = pending
    pending = nil
    if job == "files" then runFiles() else writeReport() end
  end
  local held = s.pad ~= nil and model.exitHeld(s.pad.down, s.pad.gamepad and function(n) return joy:isGamepadDown(n) end or nil)
  if not held then latch = false end
  if s.chord:update(dt, held and not latch) then
    latch = true
    s.chord.t = 0
    if s.page == "home" then love.event.quit() else go("home"); say("Back.") end
  end
  if s.page == "input" then
    s.idle:update(dt)
    if s.idle:expired() then go("home"); say("No input for 15 s: back to the list.", "warn") end
  end
  if s.page == "pattern" and model.PATTERNS[s.sel.video].id == "motion" then s.frames:add(dt) end
end

-- ---- the video patterns (pixels, not cells) ---------------------------------------------------------------------------------------

local W, H = 640, 480
local checker
local SOLID = { white = { 1, 1, 1 }, red = { 1, 0, 0 }, green = { 0, 1, 0 }, blue = { 0, 0, 1 }, black = { 0, 0, 0 } }
local BARS = { { 1, 1, 1 }, { 1, 1, 0 }, { 0, 1, 1 }, { 0, 1, 0 }, { 1, 0, 1 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0, 0 } }

local function rect(r, g, b, x, y, w, h)
  love.graphics.setColor(r, g, b, 1)
  love.graphics.rectangle("fill", x, y, w, h)
end

local function pattern(id)
  love.graphics.clear(0, 0, 0, 1)
  if SOLID[id] then
    local c = SOLID[id]
    love.graphics.clear(c[1], c[2], c[3], 1)
  elseif id == "bars" then
    for i, c in ipairs(BARS) do rect(c[1], c[2], c[3], (i - 1) * 80, 0, 80, 320) end
    for i = 0, 15 do
      local c = theme.RGB[i]
      rect(c[1] / 255, c[2] / 255, c[3] / 255, i * 40, 320, 40, 160)
    end
  elseif id == "ramps" then
    for x = 0, W - 1 do
      local v = x / (W - 1)
      rect(v, v, v, x, 0, 1, 120); rect(v, 0, 0, x, 120, 1, 120); rect(0, v, 0, x, 240, 1, 120); rect(0, 0, v, x, 360, 1, 120)
    end
  elseif id == "grid" then
    if not checker then
      local d = love.image.newImageData(W / 2, H)
      for y = 0, H - 1 do for x = 0, W / 2 - 1 do
        local on = (x + y) % 2 == 0 and 1 or 0
        d:setPixel(x, y, on, on, on, 1)
      end end
      checker = love.graphics.newImage(d)
      checker:setFilter("nearest", "nearest")
    end
    love.graphics.setColor(1, 1, 1, 1)
    love.graphics.draw(checker, 0, 0)
    for x = W / 2, W - 1, 10 do rect(0.5, 0.5, 0.5, x, 0, 1, H) end
    for y = 0, H - 1, 10 do rect(0.5, 0.5, 0.5, W / 2, y, W / 2, 1) end
    rect(1, 1, 1, 0, 0, W, 1); rect(1, 1, 1, 0, H - 1, W, 1); rect(1, 1, 1, 0, 0, 1, H); rect(1, 1, 1, W - 1, 0, 1, H)
    rect(1, 0, 0, 0, 0, 4, 4); rect(1, 0, 0, W - 4, 0, 4, 4); rect(1, 0, 0, 0, H - 4, 4, 4); rect(1, 0, 0, W - 4, H - 4, 4, 4)
  elseif id == "motion" then
    motion.n = motion.n + 1
    motion.x = (motion.x + 8) % W
    rect(1, 1, 1, motion.x, 0, 16, H - 40)
    for i = 0, 59 do rect(0.33, 0.33, 0.33, i * 10 + 20, H - 28, 8, 16) end
    rect(1, 1, 0, (motion.n % 60) * 10 + 20, H - 28, 8, 16)  -- one box a frame: a gap in a photo is a dropped frame
  end
  love.graphics.setColor(1, 1, 1, 1)
end

function love.draw()
  if s.page == "pattern" then
    pattern(model.PATTERNS[s.sel.video].id)
    return
  end
  view.render(screen, s)
  theme.draw(screen)
  if pending then pendingShown = true end
end
