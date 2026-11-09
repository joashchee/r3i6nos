-- Run with: luajit tests/test_mixer.lua   (from the repo root)
-- Settings > Sound mixer (menu/mixer.lua): reading amixer, stepping the controls, the dialog's keys, its screen, the test sounds.
package.path = "menu/?.lua;theme/?.lua;" .. package.path
local mixer = require("mixer")
local settings = require("settings")
local view = require("view")
local theme = require("r3i6nos_ansiapps")

local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

-- A fake amixer with card 0's controls as they are expected on the unit: the two softvols and the RK817's Playback Path.
local state = { Volume = 16, Boost = 3, path = "SPK", fail = false }   -- Boost: -3 to +18 dB, step 3 is +6 dB
local calls = {}
local PATHS = { "OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT", "SPK_HP" }
local function scontents()
  local db = function(v, lo, step) return ("%.2f"):format(lo + v * step) end
  local q = {}
  for i, p in ipairs(PATHS) do q[i] = "'" .. p .. "'" end
  return table.concat({
    "Simple mixer control 'Boost',0",
    "  Capabilities: volume",
    "  Playback channels: Front Left - Front Right",
    "  Limits: 0 - 7",
    ("  Front Left: %d [%d%%] [%sdB]"):format(state.Boost, math.floor(state.Boost / 7 * 100), db(state.Boost, -3, 3)),
    ("  Front Right: %d [%d%%] [%sdB]"):format(state.Boost, math.floor(state.Boost / 7 * 100), db(state.Boost, -3, 3)),
    "Simple mixer control 'Playback Path',0",
    "  Capabilities: enum",
    "  Items: " .. table.concat(q, " "),
    "  Item0: '" .. state.path .. "'",
    "Simple mixer control 'Volume',0",
    "  Capabilities: volume",
    "  Playback channels: Front Left - Front Right",
    "  Limits: 0 - 20",
    ("  Front Left: %d [%d%%] [%sdB]"):format(state.Volume, state.Volume * 5, db(state.Volume, -40, 2)),
    ("  Front Right: %d [%d%%] [%sdB]"):format(state.Volume, state.Volume * 5, db(state.Volume, -40, 2)),
    "Simple mixer control 'Mute',0",
    "  Capabilities: pswitch",
    "  Mono: Playback [on]",
  }, "\n") .. "\n"
end
mixer.sys.run = function(args)
  calls[#calls + 1] = table.concat(args, " ")
  if state.fail then return nil end
  if args[#args] == "scontents" then return scontents() end
  local name, value = args[#args - 1], args[#args]
  if name == "Playback Path" then state.path = value else state[name] = tonumber(value) end
  return ""
end

-- ---- parsing
local list = mixer.read()
eq(#list, 4, "controls read")
local by = {}
for _, c in ipairs(list) do by[c.name] = c end
eq(by.Volume.value, 16); eq(by.Volume.min, 0); eq(by.Volume.max, 20); eq(by.Volume.percent, 80); eq(by.Volume.db, -8)
eq(by.Boost.db, 6)
eq(by["Playback Path"].item, "SPK"); eq(#by["Playback Path"].items, 7)
eq(by.Mute.value, nil, "a switch has no level")
assert(mixer.parse("Simple mixer control 'X',0\n  Mono: Playback 200 [78%] [-10.00dB] [on]\n")[1].value == 200, "a codec level")
eq(#mixer.parse(nil), 0, "no amixer: no controls")

-- ---- order and text: Volume, Boost, then the codec's own; a switch with nothing to change is left out
local ord = mixer.order(list)
eq(#ord, 3); eq(ord[1].name, "Volume"); eq(ord[2].name, "Boost"); eq(ord[3].name, "Playback Path")
eq(mixer.valueText(by.Volume), "80%  -8 dB"); eq(mixer.valueText(by.Boost), "+6 dB"); eq(mixer.valueText(by["Playback Path"]), "SPK")
eq(mixer.fraction(by.Volume), 0.8); eq(mixer.fraction(by["Playback Path"]), nil)

-- ---- changes: a level moves by steps and stops at its ends; a choice moves one item
eq(table.concat(mixer.change(by.Volume, 1), " "), "amixer -c 0 -q sset Volume 17")
eq(table.concat(mixer.change(by.Volume, 10), " "), "amixer -c 0 -q sset Volume 20", "clamped to the top")
eq(table.concat(mixer.change(by.Boost, -5), " "), "amixer -c 0 -q sset Boost 1", "Boost stops at step 1 (0 dB): step 0 is silence")
eq(mixer.fraction({ name = "Boost", items = {}, value = 1, min = 0, max = 7 }), 0, "Boost's bar is empty at 0 dB")
eq(mixer.valueText({ name = "Boost", items = {}, value = 1, min = 0, max = 7, db = 0 }), "+0 dB")
eq(mixer.valueText({ name = "Boost", items = {}, value = 0, min = 0, max = 7, db = -3 }), "silent (step 0)", "says so if it is ever there")
eq(mixer.valueText({ name = "Volume", items = {}, value = 0, min = 0, max = 20, percent = 0, db = -40 }), "off", "step 0 is silence, not -40 dB")
eq(table.concat(mixer.change(by["Playback Path"], -5), " "), "amixer -c 0 -q sset Playback Path RCV", "one item")
local top = { name = "V", items = {}, value = 20, min = 0, max = 20 }
eq(mixer.change(top, 1), nil, "already at the top")
eq(select(2, mixer.step(top, 1)), "Already at the top.")

-- ---- the dialog through Settings
local s = settings.new(nil)
settings.open(s)
local root = settings.node(s)
for i, it in ipairs(root.items) do if it.label == "Sound mixer" then root.sel = i end end
eq(root.items[root.sel].label, "Sound mixer", "in the Settings list")
settings.press(s, "accept")
local n = settings.node(s)
eq(n.kind, "mixer"); eq(n.title, "Sound mixer"); eq(n.sel, 1); eq(mixer.rows(n), 4)

settings.press(s, "right")
eq(state.Volume, 17, "Right: louder")
settings.press(s, "pageup")
eq(state.Volume, 12, "L1: five down")
eq(n.controls[1].value, 12, "read back")
settings.press(s, "down"); settings.press(s, "left"); settings.press(s, "left"); settings.press(s, "left")
eq(state.Boost, 1, "down to 0 dB, not to silence"); eq(n.msg, "Already at the bottom.", "says why nothing changed")
eq(mixer.hint(n), "Already at the bottom.")
settings.press(s, "down"); settings.press(s, "right")
eq(state.path, "HP", "a choice")
eq(n.msg, nil, "the message goes with the next key")
state.fail = true
settings.press(s, "right")
eq(n.msg, "Could not change Playback Path."); eq(state.path, "HP")
state.fail = false

-- the test sound: the last row; Left/Right move along the list and stop at its ends, A goes round it
settings.press(s, "end")
assert(mixer.onSound(n)); eq(n.sound, 1)
settings.press(s, "left"); eq(n.sound, 1)
settings.press(s, "right"); eq(n.sound, 2)
for _ = 1, 10 do settings.press(s, "right") end
eq(n.sound, #mixer.SOUNDS)
settings.press(s, "accept"); eq(n.sound, 1, "A wraps")
settings.press(s, "accept"); eq(n.sound, 2)
settings.press(s, "down"); eq(n.sel, 1, "down wraps to the top")

-- the screen: names, bars, values and the sound's row; nothing outside the display
local scr = theme.screen()
view.render(scr, { entries = {}, selected = 1, dialog = s })
local function row(r)
  local t = {}
  for c = 0, theme.COLS - 1 do t[#t + 1] = string.char(scr.ch[r * theme.COLS + c + 1] % 128) end
  return table.concat(t)
end
local all = {}
for r = 0, theme.ROWS - 1 do all[#all + 1] = row(r) end
local text = table.concat(all, "\n")
assert(text:find("Sound mixer", 1, true), "title")
assert(text:find("Volume", 1, true) and text:find("60%  -16 dB", 1, true), "Volume and its value")
assert(text:find("< HP >", 1, true), "the path")
assert(text:find("Test sound", 1, true) and text:find("< Tone, left only >", 1, true), "the sound row")

-- B leaves
settings.press(s, "back")
eq(settings.node(s).title, "Settings")

-- many controls: whole pages, the sound's row on every one
local many = { controls = {}, sel = 1, sound = 1 }
for i = 1, 20 do many.controls[i] = { name = "C" .. i, items = {}, value = 1, min = 0, max = 2, caps = "volume" } end
eq(select(2, mixer.page(many)), 2)
many.sel = 15; eq(mixer.page(many), 2)
many.sel = 21; eq(mixer.page(many), 2); assert(mixer.onSound(many))

-- no controls at all (a laptop, or no /etc/asound.conf): only the test sound
state.fail = true
local none = mixer.new()
state.fail = false
eq(#none.controls, 0); eq(mixer.rows(none), 1); assert(mixer.onSound(none))

-- ---- the test sounds: in range, the tone on the side asked for, loops that join
for k = 2, 5 do
  local frames = mixer.frames(k)
  assert(frames > 0)
  for _, i in ipairs({ 0, 1, 1000, frames - 1 }) do
    local l, r = mixer.sample(k, i)
    assert(math.abs(l) <= 1 and math.abs(r) <= 1, "in range")
  end
end
local l, r = mixer.sample(2, 50); assert(l ~= 0 and r == 0, "left only")
l, r = mixer.sample(3, 50); assert(l == 0 and r ~= 0, "right only")
l, r = mixer.sample(4, 50); assert(l ~= 0 and l == r, "both")
assert(math.abs(mixer.sample(2, 0)) < 1e-9 and math.abs(mixer.sample(2, mixer.frames(2))) < 1e-6, "the tone's loop joins at zero")
assert(math.abs(mixer.sample(5, 0)) < 1e-9, "the music starts from silence")
local last = mixer.sample(5, mixer.frames(5) - 1)
assert(math.abs(last) < 0.01, "the music ends near silence: " .. last)
eq(select(1, mixer.sample(1, 10)), 0, "Off is silence")

print("test_mixer.lua: ok")
