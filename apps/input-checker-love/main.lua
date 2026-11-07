-- R36S Input Checker for LÖVE 11.5 and 12.0, 640x480.
-- Shows every input the pad reports, raw (button numbers, axes, hat) and as SDL's gamepad mapping,
-- so a unit's real layout can be read off the screen. Exit: Select+Start held 1 s, Esc, or 120 s idle.
-- Spec: docs/R36S_Input_Checker_Specification.md (the layout and palette are from it).

local model = require("model")

local W, H = 640, 480
local C = {
  bg = { 0.094, 0.094, 0.106 }, panel = { 0.14, 0.14, 0.18 }, dim = { 0.3, 0.3, 0.33 },
  active = { 0.133, 0.773, 0.369 }, accent = { 0.22, 0.74, 0.973 }, text = { 0.85, 0.85, 0.85 },
  mute = { 0.55, 0.55, 0.6 }, bad = { 1, 0.35, 0.35 }, warm = { 0.98, 0.62, 0.3 },
}

local fonts = {}
local joystick
local log = model.newLog(6)
local seen = model.newSeen()
local chord = model.newChord()
local idle = model.newIdle()
local axisZone = {}
local lastKey = "-"
local runtime

local function color(c, a) love.graphics.setColor(c[1], c[2], c[3], a or 1) end
local function lit(on) color(on and C.active or C.dim) end

local function pick(list)
  for _, j in ipairs(list) do return j end
end

function love.load()
  love.mouse.setVisible(false)
  love.graphics.setDefaultFilter("nearest", "nearest")
  fonts.title = love.graphics.newFont(16)
  fonts.body = love.graphics.newFont(12)
  fonts.small = love.graphics.newFont(10)
  runtime = model.runtime(love._version, love._version_major, love._version_minor, love._version_revision)
  joystick = pick(love.joystick.getJoysticks())
end

function love.joystickadded(j)
  joystick = joystick or j
  log:add("pad added: " .. model.clip(j:getName(), 24))
  idle:touch()
end

function love.joystickremoved(j)
  log:add("pad removed")
  if joystick == j then joystick = pick(love.joystick.getJoysticks()) end
end

function love.joystickpressed(_, b)
  seen:button(b); log:add("button " .. b .. " down"); idle:touch()
end

function love.joystickreleased(_, b)
  log:add("button " .. b .. " up")
end

function love.joystickhat(_, h, dir)
  seen:hatdir(dir)
  log:add("hat " .. h .. " " .. dir)
  idle:touch()
end

function love.joystickaxis(_, a, v)
  local z = model.zone(v)
  if z ~= (axisZone[a] or 0) then
    axisZone[a] = z
    if z ~= 0 then seen:axis(a); idle:touch() end
    log:add(("axis %d %s"):format(a, z == 0 and "centre" or model.fmt(v)))
  end
end

function love.keypressed(key)
  seen:key(key); lastKey = key; log:add("key " .. key); idle:touch()
  if key == "escape" then love.event.quit() end
end

local function exitHeld()
  if not (joystick and joystick:isGamepad()) then return false end
  return joystick:isGamepadDown("back") and joystick:isGamepadDown("start")
end

function love.update(dt)
  idle:update(dt)
  if chord:update(dt, exitHeld()) or idle:expired() then love.event.quit() end
end

-- ---- drawing ----------------------------------------------------------------------------------

local function dpad(cx, cy, hat)
  local h = model.hat(hat)
  color(C.panel); love.graphics.rectangle("fill", cx - 48, cy - 48, 96, 96, 8, 8)
  lit(h.up);    love.graphics.rectangle("fill", cx - 13, cy - 44, 26, 32)
  lit(h.down);  love.graphics.rectangle("fill", cx - 13, cy + 12, 26, 32)
  lit(h.left);  love.graphics.rectangle("fill", cx - 44, cy - 13, 32, 26)
  lit(h.right); love.graphics.rectangle("fill", cx + 12, cy - 13, 32, 26)
end

local function round(x, y, label, on, r)
  lit(on); love.graphics.circle("fill", x, y, r or 14)
  color(on and C.bg or C.text); love.graphics.setFont(fonts.small)
  love.graphics.printf(label, x - 14, y - 6, 28, "center")
end

local function stick(cx, cy, ax, ay, pressed, tint, label)
  color(C.panel); love.graphics.circle("fill", cx, cy, 36)
  color(C.dim); love.graphics.circle("line", cx, cy, 36)
  color(pressed and C.active or tint); love.graphics.circle("fill", cx + ax * 26, cy + ay * 26, 10)
  color(C.mute); love.graphics.setFont(fonts.small)
  love.graphics.printf(label, cx - 50, cy + 42, 100, "center")
end

local function gp(name)
  return joystick and joystick:isGamepad() and joystick:isGamepadDown(name)
end

local function visual()
  local hat = joystick:getHat(1) or "c"
  local pad = joystick:isGamepad()
  -- shoulders and triggers, by SDL's gamepad mapping
  local lt = pad and joystick:getGamepadAxis("triggerleft") > 0.5
  local rt = pad and joystick:getGamepadAxis("triggerright") > 0.5
  round(40, 70, "L1", gp("leftshoulder"), 15); round(85, 70, "L2", lt, 15)
  round(235, 70, "R2", rt, 15); round(280, 70, "R1", gp("rightshoulder"), 15)

  dpad(105, 160, hat)
  -- face buttons: raw 1..4 as the spec says (A, B, X, Y)
  color(C.panel); love.graphics.circle("fill", 250, 160, 48)
  round(250, 130, "X", joystick:isDown(3)); round(250, 190, "A", joystick:isDown(1))
  round(220, 160, "Y", joystick:isDown(4)); round(280, 160, "B", joystick:isDown(2))

  local n = joystick:getAxisCount()
  local function ax(i) return (i <= n) and joystick:getAxis(i) or 0 end
  stick(105, 290, ax(1), ax(2), gp("leftstick"), C.accent, "Left stick (L3)")
  stick(250, 290, ax(3), ax(4), gp("rightstick"), C.warm, "Right stick (R3)")

  -- Select / Start / FN, by the gamepad mapping
  round(120, 372, "SEL", gp("back"), 15); round(170, 372, "STA", gp("start"), 15); round(220, 372, "FN", gp("guide"), 15)

  if chord.t > 0 then
    color(C.panel); love.graphics.rectangle("fill", 40, 404, 240, 10)
    color(C.warm); love.graphics.rectangle("fill", 40, 404, 240 * chord:progress(), 10)
  end
end

local function telemetry()
  color(C.panel); love.graphics.rectangle("fill", 340, 56, 284, 376, 6, 6)
  local x, y = 352, 66
  local function line(text, c, font)
    love.graphics.setFont(font or fonts.body); color(c or C.accent)
    love.graphics.print(text, x, y); y = y + 16
  end
  line("TELEMETRY", C.text, fonts.title); y = y + 4
  line(("Hat 1 (D-pad): %s"):format(joystick:getHat(1) or "c"))
  for i = 1, math.min(6, joystick:getAxisCount()) do
    line(("Axis %d: %s%s"):format(i, model.fmt(joystick:getAxis(i)), seen.axes[i] and "  seen" or ""))
  end
  y = y + 4
  local down = {}
  for i = 1, joystick:getButtonCount() do
    if joystick:isDown(i) then down[#down + 1] = i end
  end
  line("Buttons down: " .. (#down > 0 and table.concat(down, " ") or "none"), C.active)
  line(("Seen: %d of %d buttons, %d hat dirs, last key %s"):format(
    seen:count("buttons"), joystick:getButtonCount(), seen:count("hat"), lastKey), C.mute, fonts.small)
  line(("Axes %d  Buttons %d  Hats %d  Gamepad map: %s"):format(
    joystick:getAxisCount(), joystick:getButtonCount(), joystick:getHatCount(),
    joystick:isGamepad() and "yes" or "NO"), joystick:isGamepad() and C.mute or C.warm, fonts.small)
  line("GUID " .. joystick:getGUID(), C.mute, fonts.small)
  y = y + 6
  line("LAST EVENTS", C.text)
  for _, e in ipairs(log.items) do line(e, C.mute, fonts.small) end
end

function love.draw()
  love.graphics.clear(C.bg[1], C.bg[2], C.bg[3])
  color({ 0.15, 0.15, 0.18 }); love.graphics.rectangle("fill", 0, 0, W, 45)
  color(C.active); love.graphics.setFont(fonts.title)
  love.graphics.print("R36S INPUT DIAGNOSTIC TOOL", 20, 13)
  color(C.mute); love.graphics.setFont(fonts.small)
  love.graphics.printf(runtime, 0, 8, W - 14, "right")
  love.graphics.printf(joystick and model.clip(joystick:getName(), 38) or "no gamepad", 0, 24, W - 14, "right")

  if not joystick then
    color(C.bad); love.graphics.setFont(fonts.title)
    love.graphics.printf("NO GAMEPAD DETECTED", 0, 200, W, "center")
    color(C.mute); love.graphics.setFont(fonts.body)
    love.graphics.printf("The pad is not reaching SDL. Press a button; this page updates when one appears.", 60, 235, W - 120, "center")
  else
    visual(); telemetry()
  end

  color({ 0.25, 0.25, 0.3 }); love.graphics.rectangle("fill", 0, 440, W, 40)
  love.graphics.setFont(fonts.small)
  if idle:warning() then
    color(C.warm)
    love.graphics.print(("No input: leaving in %d s. Press anything to stay."):format(math.ceil(idle:remaining())), 20, 452)
  else
    color(C.text)
    love.graphics.print("Exit: hold Select+Start for 1 s, or Esc. It also leaves after 2 minutes without input.", 20, 452)
  end
end
