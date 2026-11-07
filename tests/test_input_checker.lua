-- Run with: luajit tests/test_input_checker.lua   (from the repo root)
-- Tests the input checker's logic, then runs its real main.lua against a fake LÖVE and a fake
-- gamepad, so a wrong API call or a typo fails here and not on the device.
local dir = "apps/input-checker-love/"
package.path = dir .. "?.lua;" .. package.path
local model = require("model")

local function eq(a, b, msg) assert(a == b, (msg or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

-- ---- model ----
local h = model.hat("ul")
assert(h.up and h.left and not h.down and not h.right, "hat ul")
assert(not model.hat("c").up and not model.hat(nil).left, "hat centre / nil")
eq(model.zone(0.49), 0, "inside zone"); eq(model.zone(0.5), 1, "edge"); eq(model.zone(-0.9), -1, "negative")
eq(model.fmt(0.5), "+0.50"); eq(model.fmt(-1), "-1.00"); eq(model.fmt(nil), "+0.00")
eq(model.clip("short", 10), "short"); eq(model.clip("abcdefghijkl", 8), "abcde...")

local log = model.newLog(3)
for i = 1, 5 do log:add("e" .. i) end
eq(#log.items, 3); eq(log.items[1], "e5", "newest first"); eq(log.items[3], "e3")

local seen = model.newSeen()
seen:button(2); seen:button(2); seen:button(7); seen:hatdir("c"); seen:hatdir("u"); seen:axis(1)
eq(seen:count("buttons"), 2); eq(seen:count("hat"), 1, "centre is not a direction"); eq(seen:count("axes"), 1)

local chord = model.newChord(1.0)
assert(not chord:update(0.6, true)); assert(not chord:update(0.3, false), "release resets")
eq(chord.t, 0); assert(not chord:update(0.9, true)); assert(chord:update(0.2, true), "held long enough")
local c2 = model.newChord(2); c2:update(1, true); eq(c2:progress(), 0.5)

eq(model.IDLE_LIMIT, 15, "idle limit is 15 s"); eq(model.IDLE_WARN, 10)
local idle = model.newIdle()
idle:update(4); assert(not idle:warning() and not idle:expired())
idle:update(2); assert(idle:warning() and not idle:expired(), "warns in the last 10 s")
idle:touch(); eq(idle.t, 0); assert(not idle:warning(), "input resets the countdown")
idle:update(16); assert(idle:expired())

-- the layout measured on a real R36S (LÖVE numbering, from 1)
local Lt = model.LAYOUT
eq(Lt.up, 9); eq(Lt.left, 11); eq(Lt.down, 10); eq(Lt.right, 12)
eq(Lt.a, 2); eq(Lt.b, 1)
eq(Lt.select, 13); eq(Lt.start, 14); eq(Lt.fn, 17); eq(Lt.l3, 15); eq(Lt.r3, 16)

local function held(...)
  local set = {}
  for _, i in ipairs({ ... }) do set[i] = true end
  return function(i) return set[i] == true end
end
local d = model.dpad("c", held(9, 12))
assert(d.up and d.right and not d.down and not d.left, "D-pad from buttons 9 and 12")
d = model.dpad("dl", held())
assert(d.down and d.left and not d.up, "D-pad from a hat still works")
assert(model.exitHeld(held(13, 14)), "Select+Start raw")
assert(not model.exitHeld(held(13)) and not model.exitHeld(held(14)), "one of them is not enough")
assert(model.exitHeld(held(), function(n) return n == "back" or n == "start" end), "SDL mapping also counts")
assert(not model.exitHeld(held(), nil), "no mapping, nothing held")
assert(model.keyHeld({ volumeup = true }, "vol_up") and not model.keyHeld({ volumeup = true }, "vol_down"))
assert(model.keyHeld({ power = true }, "power") and not model.keyHeld({}, "power"))
eq(model.eventLine(7, "btn 11 DOWN", true), "#7 btn 11 DOWN (held)")
eq(model.eventLine(8, "btn 11 UP", false), "#8 btn 11 UP (free)")
eq(model.eventLine(9, "key volumeup DOWN"), "#9 key volumeup DOWN")

eq(model.runtime("11.5", 11, 5, 0), "LÖVE 11.5.0"); eq(model.runtime("12.0"), "LÖVE 12.0")

-- ---- main.lua against a fake LÖVE ----
local function fake_love(joysticks)
  local quit = false
  local noop = function() end
  love = {
    _version = "11.5", _version_major = 11, _version_minor = 5, _version_revision = 0,
    mouse = { setVisible = noop },
    graphics = setmetatable({ newFont = function() return {} end }, { __index = function() return noop end }),
    joystick = { getJoysticks = function() return joysticks end },
    event = { quit = function() quit = true end },
  }
  return function() return quit end
end

local function fake_pad(gamepad, down, trigger, raw)
  local gpdown = {}
  for _, n in ipairs(down or {}) do gpdown[n] = true end
  local rawset = {}
  for _, n in ipairs(raw or {}) do rawset[n] = true end
  local valid = { a = 1, b = 1, x = 1, y = 1, back = 1, guide = 1, start = 1, leftstick = 1, rightstick = 1,
    leftshoulder = 1, rightshoulder = 1, dpup = 1, dpdown = 1, dpleft = 1, dpright = 1 }
  return {
    getName = function() return "Fake R36S pad with a rather long name indeed" end,
    getGUID = function() return "03000000deadbeef" end,
    isGamepad = function() return gamepad end,
    isGamepadDown = function(_, n) assert(valid[n], "not a LÖVE gamepad button: " .. n); return gpdown[n] or false end,
    getGamepadAxis = function(_, n) assert(n == "triggerleft" or n == "triggerright", n); return trigger or 0.9 end,
    getAxisCount = function() return 4 end, getButtonCount = function() return 17 end, getHatCount = function() return 1 end,
    getAxis = function(_, i) return i == 1 and 0.8 or 0 end,
    getHat = function() return "ul" end,
    isDown = function(_, i) return rawset[i] == true end,
  }
end

local function load_main()
  package.loaded["model"] = nil
  local chunk = assert(loadfile(dir .. "main.lua"))
  chunk()
end

-- 1. a pad: draws, logs events, no errors; the real D-pad buttons, sticks pressed and Fn light up without a mapping
local quit = fake_love({ fake_pad(false, nil, nil, { 9, 15, 17, 2 }) })
load_main()
love.load(); love.update(0.016); love.draw()
love.joystickpressed(nil, 3); love.joystickhat(nil, 1, "u"); love.joystickaxis(nil, 1, 0.9); love.joystickaxis(nil, 1, 0)
love.keypressed("a"); love.joystickreleased(nil, 3); love.keyreleased("a")
love.update(0.016); love.draw()
assert(not quit(), "must not quit by itself")

-- 1b. a pad WITH a mapping and triggers released: every gamepad-button name used must be a real LÖVE one
quit = fake_love({ fake_pad(true, nil, 0) })
load_main(); love.load(); love.update(0.016); love.draw()

-- 2. Select+Start (raw 13 and 14, the pad has no mapping) held for a second leaves
quit = fake_love({ fake_pad(false, nil, nil, { 13, 14 }) })
load_main(); love.load()
for _ = 1, 15 do love.update(0.05); love.draw() end
assert(not quit(), "0.75 s is not enough")
for _ = 1, 15 do love.update(0.05) end
assert(quit(), "Select+Start held 1.5 s should quit")
-- ... the SDL mapping works too, and a single one of the two never does
quit = fake_love({ fake_pad(true, { "back", "start" }) })
load_main(); love.load(); for _ = 1, 30 do love.update(0.05) end
assert(quit(), "mapped Select+Start quits")
quit = fake_love({ fake_pad(false, nil, nil, { 13 }) })
load_main(); love.load(); for _ = 1, 100 do love.update(0.05); love.joystickpressed(nil, 13) end
assert(not quit(), "Select alone does not quit (and activity keeps the idle timer reset)")
quit = fake_love({ fake_pad(false) }); load_main(); love.load(); love.keypressed("escape")
assert(quit(), "escape quits")

-- 3. power and volume arrive as keys: they light their pills and are logged, never an error
quit = fake_love({ fake_pad(false) })
load_main(); love.load()
love.keypressed("volumeup"); love.draw(); love.keyreleased("volumeup"); love.keypressed("power"); love.draw()
love.keyreleased("power"); love.keypressed("someunknownkey"); love.update(0.016); love.draw()
assert(not quit())

-- 4. idle: the countdown shows in the last 10 s and the app leaves at 15 s
quit = fake_love({ fake_pad(false) })
load_main(); love.load()
for _ = 1, 6 do love.update(1) end
love.draw()                                              -- the countdown line draws
assert(not quit(), "6 s of 15 is not idle-out")
for _ = 1, 10 do love.update(1) end
assert(quit(), "idle for 15 s leaves")

-- 5. no pad at all: the message screen, and the idle timeout still lets the user out
quit = fake_love({})
load_main(); love.load(); love.update(0.016); love.draw()
assert(not quit()); for _ = 1, 16 do love.update(1) end
assert(quit(), "idle for over 15 s leaves")

print("input checker: ok")
