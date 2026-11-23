-- Hello_Love: the r3i6nos sample for the LÖVE runtimes (docs/RUNTIMES.md). Original, public domain (CC0). The same file runs on LÖVE 11.5 and 12.0.
-- D-pad (or the arrow keys, or the left stick) moves the square, A (or Z) changes its colour and plays a note, B (or X) does the same one colour back,
-- Start (or Esc) quits. The screen shows the LÖVE version, the renderer the unit gives it and the last raw button pressed, which is what a tester needs.
-- R3I6NOS_SAMPLE_FRAMES=n quits after n frames (a self-test hook; unset on the unit).
-- Raw buttons, as LÖVE counts them (this pad's SDL gamepad mapping is wrong, docs/hardware-findings.md): B 1, A 2, D-pad 9-12, Select 13, Start 14.
local BTN = { B = 1, A = 2, UP = 9, DOWN = 10, LEFT = 11, RIGHT = 12, START = 14 }
local AUTO = tonumber(os.getenv("R3I6NOS_SAMPLE_FRAMES") or "") or 0
local x, y, hue, frames, last, since = 300, 220, 1, 0, "none yet", 0
local COLORS = { {1, .3, .3}, {1, .7, .2}, {1, 1, .3}, {.4, 1, .4}, {.3, .9, 1}, {.5, .5, 1}, {1, .5, 1} }
local note, info

local function tone(freq)
  local rate, n = 22050, 5512
  local sd = love.sound.newSoundData(n, rate, 16, 1)
  for i = 0, n - 1 do sd:setSample(i, math.sin(2 * math.pi * freq * i / rate) * 0.4 * (1 - i / n)) end
  return love.audio.newSource(sd)
end

function love.load()
  love.graphics.setBackgroundColor(0.05, 0.05, 0.2)
  local ok, src = pcall(tone, 523)
  note = ok and src or nil
  local name, version, vendor, device = love.graphics.getRendererInfo()
  local major, minor = love.getVersion()
  info = ("LÖVE %d.%d\n%s %s\n%s, %s"):format(major, minor, name, version, vendor, device)
end

local function pressed(b) last = "button " .. b; end

local function change(step)
  hue = (hue - 1 + step) % #COLORS + 1
  if note then note:stop(); note:play() end
end

function love.joystickpressed(_, b)
  pressed(b)
  if b == BTN.START then love.event.quit() elseif b == BTN.A then change(1) elseif b == BTN.B then change(-1) end
end

function love.keypressed(k)
  last = "key " .. k
  if k == "escape" then love.event.quit() elseif k == "z" then change(1) elseif k == "x" then change(-1) end
end

local function held(pad, b, key)
  return love.keyboard.isDown(key) or (pad and pad:isDown(b))
end

function love.update(dt)
  frames, since = frames + 1, since + dt
  if AUTO > 0 and frames >= AUTO then love.event.quit() end
  local pad = love.joystick.getJoysticks()[1]
  local dx = (held(pad, BTN.RIGHT, "right") and 1 or 0) - (held(pad, BTN.LEFT, "left") and 1 or 0)
  local dy = (held(pad, BTN.DOWN, "down") and 1 or 0) - (held(pad, BTN.UP, "up") and 1 or 0)
  if pad then
    local ax, ay = pad:getAxis(1), pad:getAxis(2)
    if math.abs(ax) > 0.35 then dx = ax end
    if math.abs(ay) > 0.35 then dy = ay end
  end
  x = math.max(0, math.min(600, x + dx * 240 * dt))
  y = math.max(120, math.min(440, y + dy * 240 * dt))
end

function love.draw()
  love.graphics.setColor(1, 1, 1)
  love.graphics.print("Hello LÖVE", 24, 16, 0, 3, 3)
  love.graphics.print(info .. "\nlast input: " .. last .. "\n\nD-pad moves, A/B colour and a note, Start quits", 24, 60, 0, 1, 1)
  love.graphics.setColor(COLORS[hue])
  love.graphics.rectangle("fill", x, y, 40, 40)
  love.graphics.setColor(1, 1, 1)
  love.graphics.print(("%d fps"):format(love.timer.getFPS()), 560, 16)
end
