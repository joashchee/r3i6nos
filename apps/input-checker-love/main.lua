-- R36S Input Checker for LÖVE 11.5 and 12.0, 640x480.
-- Shows every input the pad reports, raw (button numbers as LÖVE counts them, from 1), so a unit's real
-- layout can be read off the screen. The button numbers of an R36S are in model.lua (LAYOUT), measured on
-- real hardware. Exit: hold Select+Start for 1 s, Esc, or 15 s without input.
-- Spec: docs/R36S_Input_Checker_Specification.md (the layout and palette are from it).

local model = require("model")
local L = model.LAYOUT

local W, H = 640, 480
local C = {
  bg = { 0.094, 0.094, 0.106 }, panel = { 0.14, 0.14, 0.18 }, dim = { 0.3, 0.3, 0.33 },
  active = { 0.133, 0.773, 0.369 }, accent = { 0.22, 0.74, 0.973 }, text = { 0.85, 0.85, 0.85 },
  mute = { 0.55, 0.55, 0.6 }, bad = { 1, 0.35, 0.35 }, warm = { 0.98, 0.62, 0.3 },
  head = { 0.15, 0.15, 0.18 }, foot = { 0.25, 0.25, 0.3 },
}

local fonts = {}
local joystick
local log = model.newLog(6)
local seen = model.newSeen()
local chord = model.newChord()
local idle = model.newIdle()
local axisZone = {}
local keysDown = {}
local lastKey = "-"
local seq = 0
local runtime

local function color(c, a) love.graphics.setColor(c[1], c[2], c[3], a or 1) end
local function lit(on) color(on and C.active or C.dim) end

local function pick(list)
  for _, j in ipairs(list) do return j end
end

local function down(i) return joystick ~= nil and joystick:isDown(i) end

local function event(what, isHeld)
  seq = seq + 1
  log:add(model.eventLine(seq, what, isHeld))
end

function love.load()
  love.mouse.setVisible(false)
  love.graphics.setDefaultFilter("nearest", "nearest")
  fonts.title = love.graphics.newFont(26)
  fonts.body = love.graphics.newFont(22)
  fonts.small = love.graphics.newFont(18)
  fonts.label = love.graphics.newFont(16)
  fonts.tiny = love.graphics.newFont(12)
  runtime = model.runtime(love._version, love._version_major, love._version_minor, love._version_revision)
  joystick = pick(love.joystick.getJoysticks())
end

function love.joystickadded(j)
  joystick = joystick or j
  log:add("pad added")
  idle:touch()
end

function love.joystickremoved(j)
  log:add("pad removed")
  if joystick == j then joystick = pick(love.joystick.getJoysticks()) end
end

function love.joystickpressed(_, b)
  seen:button(b); idle:touch()
  event(("btn %d DOWN"):format(b), down(b))
end

function love.joystickreleased(_, b)
  idle:touch()
  event(("btn %d UP"):format(b), down(b))
end

function love.joystickhat(_, h, dir)
  seen:hatdir(dir); idle:touch()
  event(("hat %d %s"):format(h, dir), dir ~= "c")
end

function love.joystickaxis(_, a, v)
  local z = model.zone(v)
  if z ~= (axisZone[a] or 0) then
    axisZone[a] = z
    if z ~= 0 then seen:axis(a); idle:touch() end
    event(("axis %d %s"):format(a, z == 0 and "centre" or model.fmt(v)), z ~= 0)
  end
end

function love.keypressed(key)
  keysDown[key] = true; seen:key(key); lastKey = key; idle:touch()
  event("key " .. key .. " DOWN")
  if key == "escape" then love.event.quit() end
end

function love.keyreleased(key)
  keysDown[key] = nil; idle:touch()
  event("key " .. key .. " UP")
end

local function gamepadFn()
  if not (joystick and joystick:isGamepad()) then return nil end
  return function(name) return joystick:isGamepadDown(name) end
end

function love.update(dt)
  idle:update(dt)
  local held = joystick ~= nil and model.exitHeld(down, gamepadFn())
  if chord:update(dt, held) or idle:expired() then love.event.quit() end
end

-- ---- drawing ----------------------------------------------------------------------------------

local function dpad(cx, cy)
  local d = model.dpad(joystick:getHat(1), down)
  color(C.panel); love.graphics.rectangle("fill", cx - 48, cy - 48, 96, 96, 8, 8)
  lit(d.up);    love.graphics.rectangle("fill", cx - 13, cy - 44, 26, 32)
  lit(d.down);  love.graphics.rectangle("fill", cx - 13, cy + 12, 26, 32)
  lit(d.left);  love.graphics.rectangle("fill", cx - 44, cy - 13, 32, 26)
  lit(d.right); love.graphics.rectangle("fill", cx + 12, cy - 13, 32, 26)
end

local function round(x, y, label, on, r)
  lit(on); love.graphics.circle("fill", x, y, r or 18)
  color(on and C.bg or C.text); love.graphics.setFont(fonts.label)
  love.graphics.printf(label, x - 22, y - 9, 44, "center")
end

-- A stick: the whole circle lights up while it is pressed in (L3 / R3).
local function stick(cx, cy, ax, ay, pressed, tint, label)
  color(pressed and C.active or C.panel); love.graphics.circle("fill", cx, cy, 38)
  color(pressed and C.active or C.dim); love.graphics.circle("line", cx, cy, 38)
  color(pressed and C.bg or tint); love.graphics.circle("fill", cx + ax * 26, cy + ay * 26, 10)
  color(pressed and C.active or C.mute); love.graphics.setFont(fonts.label)
  love.graphics.printf(label, cx - 60, cy + 42, 120, "center")
end

local function pill(x, y, w, label, on)
  lit(on); love.graphics.rectangle("fill", x, y, w, 24, 6, 6)
  color(on and C.bg or C.text); love.graphics.setFont(fonts.label)
  love.graphics.printf(label, x, y + 4, w, "center")
end

local function visual()
  -- shoulders and triggers: buttons 5-8 are an assumption, the telemetry shows what really arrives
  round(36, 76, "L1", down(L.l1)); round(84, 76, "L2", down(L.l2))
  round(246, 76, "R2", down(L.r2)); round(294, 76, "R1", down(L.r1))

  dpad(95, 175)
  color(C.panel); love.graphics.circle("fill", 250, 175, 52)
  round(250, 143, "X", down(L.x), 17); round(282, 175, "A", down(L.a), 17)
  round(218, 175, "Y", down(L.y), 17); round(250, 207, "B", down(L.b), 17)

  local n = joystick:getAxisCount()
  local function ax(i) return (i <= n) and joystick:getAxis(i) or 0 end
  stick(95, 300, ax(1), ax(2), down(L.l3), C.accent, "L STICK")
  stick(250, 300, ax(3), ax(4), down(L.r3), C.warm, "R STICK")

  -- left to right: Select, Fn, Start
  round(95, 380, "SEL", down(L.select), 20); round(165, 380, "FN", down(L.fn), 20); round(235, 380, "STA", down(L.start), 20)
end

local function sideKeys()
  pill(133, 408, 64, "VOL-", model.keyHeld(keysDown, "vol_down"))
  pill(207, 408, 64, "VOL+", model.keyHeld(keysDown, "vol_up"))
  pill(59, 408, 64, "PWR", model.keyHeld(keysDown, "power"))
end

local function telemetry()
  color(C.panel); love.graphics.rectangle("fill", 340, 56, 284, 376, 6, 6)
  local x, y = 350, 60
  local function line(text, c, font, step)
    love.graphics.setFont(font or fonts.body); color(c or C.accent)
    love.graphics.print(text, x, y); y = y + (step or 24)
  end
  line("TELEMETRY", C.text, fonts.title, 30)

  local n, down_list = joystick:getButtonCount(), {}
  for i = 1, n do
    if joystick:isDown(i) then down_list[#down_list + 1] = i end
  end
  local list = table.concat(down_list, " ")
  line("Down: " .. (#down_list > 0 and model.clip(list, 16) or "none"), C.active)
  line("Hat: " .. (joystick:getHat(1) or "c"))
  local ac = math.min(4, joystick:getAxisCount())
  for i = 1, ac, 2 do
    local a = ("A%d %s"):format(i, model.fmt(joystick:getAxis(i)))
    local b = i + 1 <= ac and ("  A%d %s"):format(i + 1, model.fmt(joystick:getAxis(i + 1))) or ""
    line(a .. b)
  end
  line(("Seen %d/%d buttons"):format(seen:count("buttons"), n), C.mute, fonts.small, 22)
  line("Gamepad map: " .. (joystick:isGamepad() and "yes" or "NO"), joystick:isGamepad() and C.mute or C.warm, fonts.small, 22)
  line("GUID " .. joystick:getGUID(), C.mute, fonts.tiny, 18)
  line("LAST EVENTS", C.text, fonts.small, 22)
  for _, e in ipairs(log.items) do line(e, C.mute, fonts.small, 20) end
end

function love.draw()
  love.graphics.clear(C.bg[1], C.bg[2], C.bg[3])
  color(C.head); love.graphics.rectangle("fill", 0, 0, W, 45)
  color(C.active); love.graphics.setFont(fonts.title)
  love.graphics.print("R36S INPUT CHECKER", 16, 8)
  color(C.mute); love.graphics.setFont(fonts.label)
  love.graphics.printf(runtime, 0, 3, W - 12, "right")
  love.graphics.printf(joystick and model.clip(joystick:getName(), 22) or "no gamepad", 0, 23, W - 12, "right")

  if not joystick then
    color(C.bad); love.graphics.setFont(fonts.title)
    love.graphics.printf("NO GAMEPAD DETECTED", 0, 190, W, "center")
    color(C.mute); love.graphics.setFont(fonts.small)
    love.graphics.printf("The pad is not reaching SDL. Press a button: this page updates when one appears.", 40, 235, W - 80, "center")
  else
    visual(); telemetry()
  end
  sideKeys()

  color(C.foot); love.graphics.rectangle("fill", 0, 440, W, 40)
  love.graphics.setFont(fonts.small)
  if idle:warning() then
    color(C.warm)
    love.graphics.print(("No input: leaving in %d s"):format(math.ceil(idle:remaining())), 16, 448)
  else
    color(C.text)
    love.graphics.print("Hold SEL+STA 1 s to quit. 15 s idle quits.", 16, 448)
  end
  if chord.t > 0 then
    color(C.panel); love.graphics.rectangle("fill", 16, 472, 608, 6)
    color(C.warm); love.graphics.rectangle("fill", 16, 472, 608 * chord:progress(), 6)
  end
end
