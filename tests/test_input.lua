-- Run with: luajit tests/test_input.lua   (from the repo root)
-- The Lua input reader (theme/r3i6nos_input.lua) against docs/r3i6nos-mapping.md, driven with a fake pad: the plain inputs, R1 held as "increase",
-- the chords, the right stick's dead zone and rate, the pointer's speed, the permanent keyboard bindings (one key for each input, no two sharing
-- one, nothing quits) and the Keymap's pages and test.
package.path = "theme/?.lua;" .. package.path
local I = require("r3i6nos_input")
local K = require("r3i6nos_keymap")

local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end
local function near(a, b, m) assert(math.abs(a - b) < 1e-6, (m or "") .. (" want %s, got %s"):format(b, a)) end
local B = I.BTN
local function press(s, name) local a = I.button(s, B[name], true); I.button(s, B[name], false); return a end

-- the plain inputs (raw numbers from the measured mapping: A is 2 and B is 1)
local s = I.new()
eq(press(s, "A"), "accept"); eq(press(s, "B"), "back"); eq(press(s, "X"), "x"); eq(press(s, "Y"), "y")
eq(press(s, "SELECT"), "select"); eq(press(s, "START"), "start"); eq(press(s, "L3"), "l3"); eq(press(s, "R3"), "r3")
eq(press(s, "UP"), "up"); eq(press(s, "DOWN"), "down"); eq(press(s, "LEFT"), "left"); eq(press(s, "RIGHT"), "right")
eq(B.A, 2); eq(B.B, 1); eq(B.FN, 17); eq(I.button(s, 99, true), nil, "an unknown button does nothing")

-- R1 held is "increase": page, home and end; released, the plain actions return
I.button(s, B.R1, true)
eq(press(s, "UP"), "pageup"); eq(press(s, "DOWN"), "pagedown"); eq(press(s, "LEFT"), "home"); eq(press(s, "RIGHT"), "end")
I.button(s, B.R1, false)
eq(press(s, "UP"), "up")

-- the chords fire when the second input goes down; a modifier alone does nothing; Fn is only ever the panel key
s = I.new()
eq(I.button(s, B.L1, true), nil, "L1 alone does nothing"); eq(I.button(s, B.R1, true), "menu", "L1 + R1: the main menu")
I.release_all(s)
eq(I.button(s, B.L2, true), nil); eq(I.button(s, B.R2, true), "context", "L2 + R2: the contextual menu")
I.release_all(s)
eq(press(s, "FN"), "panelnext")
I.button(s, B.L1, true); eq(press(s, "FN"), "panelfirst", "L1 + Fn: the first panel"); I.button(s, B.L1, false)
I.button(s, B.R1, true); eq(press(s, "FN"), "panelprev", "R1 + Fn: the previous panel"); I.button(s, B.R1, false)
eq(press(s, "FN"), "panelnext", "released modifiers do not stick")

-- the right stick: dead zone 0.35, a push steps at once, then up to 6 a second
s = I.new()
I.axis(s, 4, 0.3); eq(#I.tick(s, 0.1), 0, "inside the dead zone nothing moves")
near(I.stick_rate(0.35), 0); near(I.stick_rate(1), 6); near(I.stick_rate(-1), 6); near(I.stick_rate(0.675), 3)
I.axis(s, 4, 1)
local t = I.tick(s, 0.1); eq(#t, 1); eq(t[1], "down", "the first step of a push comes at once")
local n = 0
for _ = 1, 10 do n = n + #I.tick(s, 0.1) end
eq(n, 6, "full deflection: 6 steps in a second")
I.axis(s, 4, -1); t = I.tick(s, 0.1); eq(t[1], "up")
I.axis(s, 4, 0); I.axis(s, 3, 1); t = I.tick(s, 0.1); eq(t[1], "right")
local big = I.tick(s, 10); assert(#big <= 8, "a stalled frame is not a burst")
-- with R1 held the stick pages (up/down) and goes Home or End once for each push (left/right)
I.release_all(s); I.axis(s, 3, 0); I.axis(s, 4, 0); I.tick(s, 0.1)
I.button(s, B.R1, true)
I.axis(s, 4, 1); t = I.tick(s, 0.1); eq(t[1], "pagedown")
I.axis(s, 4, 0); I.tick(s, 0.1)
I.axis(s, 3, 1); t = I.tick(s, 0.1); eq(t[1], "end"); eq(#I.tick(s, 1.0), 0, "End fires once for a push, not repeatedly")
I.axis(s, 3, -1); I.tick(s, 0.1); I.axis(s, 3, 0); I.tick(s, 0.1); I.axis(s, 3, -1); eq(I.tick(s, 0.1)[1], "home")
eq(I.stick_active(s), true); I.axis(s, 3, 0); I.axis(s, 4, 0); eq(I.stick_active(s), false)

-- the pointer: 120 px/s at full, 3 times that with R1
s = I.new(); I.axis(s, 1, 1); I.axis(s, 2, -1)
local dx, dy = I.pointer(s, 1); near(dx, 120); near(dy, -120)
I.button(s, B.R1, true); dx = I.pointer(s, 1); near(dx, 360)
I.axis(s, 1, 0.2); dx = I.pointer(s, 1); near(dx, 0, "inside the dead zone")

-- the list rule
eq(I.list_step(1, 40, 18, "up"), 40); eq(I.list_step(40, 40, 18, "down"), 1); eq(I.list_step(5, 40, 18, "pagedown"), 23)
eq(I.list_step(30, 40, 18, "pagedown"), 40); eq(I.list_step(5, 40, 18, "pageup"), 1); eq(I.list_step(9, 40, 18, "home"), 1); eq(I.list_step(9, 40, 18, "end"), 40)
eq(I.list_step(9, 40, 18, "left"), 9, "left and right are the app's, not paging"); eq(I.list_step(1, 0, 18, "down"), 0)

-- the permanent keyboard bindings: every pad input has exactly one primary key, no key is two inputs, and nothing quits
local wanted = { w = "UP", s = "DOWN", a = "LEFT", d = "RIGHT", i = "X", j = "Y", k = "B", l = "A", f = "SELECT", h = "START", t = "FN",
                 q = "L1", e = "L2", o = "R1", u = "R2", r = "L3", y = "R3" }
for key, btn in pairs(wanted) do eq(I.KEY_BUTTON[key], btn, "key " .. key) end
local perButton = {}
for key, btn in pairs(I.KEY_BUTTON) do perButton[btn] = (perButton[btn] or 0) + 1; assert(B[btn], "a key for no button: " .. key) end
for _, btn in pairs(wanted) do assert(perButton[btn] >= 1) end
for _, key in ipairs({ "escape", "q", "f10", "f4" }) do
  local k = I.new(); local a = I.key(k, key, true); assert(a ~= "quit", "no key quits"); eq(I.ACTION_NAME.quit, nil)
end
s = I.new()
eq(I.key(s, "l", true), "accept"); eq(I.key(s, "return", true), "accept"); eq(I.key(s, "space", true), "accept"); eq(I.key(s, "k", true), "back"); eq(I.key(s, "escape", true), "back")
eq(I.key(s, "i", true), "x"); eq(I.key(s, "backspace", true), "x"); eq(I.key(s, "j", true), "y"); eq(I.key(s, "tab", true), "y")
eq(I.key(s, "w", true), "up"); eq(I.key(s, "down", true), "down", "the arrow keys are the right stick's steps")
eq(I.key(s, "`", true), "menu", "backquote: the main menu"); eq(I.key(s, "\\", true), "context", "backslash: the contextual menu")
eq(I.key(s, "t", true), "panelnext", "T is Fn, a tap")
-- held letters are modifiers: Q + O is the main menu, E + U the contextual menu; Q + T the first panel
eq(I.key(s, "q", true), nil); eq(I.key(s, "o", true), "menu"); I.key(s, "q", false); I.key(s, "o", false)
eq(I.key(s, "e", true), nil); eq(I.key(s, "u", true), "context"); I.key(s, "e", false); I.key(s, "u", false)
I.key(s, "q", true); eq(I.key(s, "t", true), "panelfirst"); I.key(s, "q", false)
I.key(s, "o", true); eq(I.key(s, "up", true), "pageup", "R1 held: the arrow keys page"); eq(I.key(s, "w", true), "pageup"); eq(I.key(s, "left", true), "home"); I.key(s, "o", false)
eq(I.key(s, "up", true), "up"); eq(I.key(s, "z", true), nil, "an unbound key does nothing")

-- the Keymap: the system's pages are the same everywhere, an app's rows are one more page, and the test logs presses and acts on nothing
local pages = K.pages(); eq(#pages, 2)
eq(#K.pages({ { "A", "Run" } }, "The games list"), 3)
for _, p in ipairs(K.pages({ { "A", "Run" } })) do assert(#p <= K.PER_PAGE, "a Keymap page never scrolls") end
local text = {}
for _, p in ipairs(pages) do for _, l in ipairs(p) do text[#text + 1] = l end end
text = table.concat(text, "\n")
for _, need in ipairs({ "L1 + R1", "L2 + R2", "L1 + Fn", "R1 + Fn", "R1 held", "Never bound" }) do assert(text:find(need, 1, true), need) end
local test = K.test_new()
eq(K.test_button(test, B.R1, true), nil); eq(K.test_button(test, B.UP, true), "pageup")
local lines = table.concat(K.test_lines(test), "\n")
assert(lines:find("Held:    R1 UP", 1, true), lines); assert(lines:find("Page up", 1, true), lines)
for i = 1, 12 do K.test_press(test, "press " .. i) end
eq(#test.log, 8, "the log keeps the last few presses, so the page never scrolls")

print("input ok")
