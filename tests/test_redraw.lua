-- Run with: luajit tests/test_redraw.lua   (from the repo root). menu/redraw.lua: when the event-driven menu draws.
package.path = "menu/?.lua;" .. package.path
local R = require("redraw")
local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

local r = R.new()
local d, nap = R.frame(r, "", false)
eq(d, true, "the first frame draws"); eq(nap, R.ACTIVE, "a drawn frame takes the active pace")
d, nap = R.frame(r, "", false)
eq(d, false, "nothing changed: no draw"); eq(nap, R.IDLE, "and the slow pace")
for _ = 1, 100 do d = R.frame(r, "", false) end
eq(d, false, "still idle after many loops")

eq(R.event(r, "joystickaxis"), false, "a stick moving is not news")
eq(R.event(r, "mousemoved"), false)
d = R.frame(r, "", false); eq(d, false, "quiet events do not draw")
eq(R.event(r, "keypressed"), true, "a key redraws")
d = R.frame(r, "", false); eq(d, true); d = R.frame(r, "", false); eq(d, false, "once per event")
R.event(r, "joystickpressed"); eq(select(1, R.frame(r, "", false)), true, "a pad button redraws")
R.event(r, "focus"); eq(select(1, R.frame(r, "", false)), true, "a window event redraws")

d = R.frame(r, "iHeadphones in", false); eq(d, true, "a toast appearing redraws")
d = R.frame(r, "iHeadphones in", false); eq(d, false, "the same toast: no redraw")
d = R.frame(r, "", false); eq(d, true, "the toast going redraws")

R.mark(r); eq(select(1, R.frame(r, "", false)), true, "a marked change redraws (the battery read)")
for _ = 1, 3 do d, nap = R.frame(r, "", true); eq(d, true, "a moving screen draws every loop"); eq(nap, R.ACTIVE) end
d = R.frame(r, "", false); eq(d, true, "the loop it stops moving draws once more (the answer lands in update)")
d = R.frame(r, "", false); eq(d, false, "then rests")

local drawn = 0
for _ = 1, 20 do if R.frame(r, "", "slow") then drawn = drawn + 1 end end
eq(drawn, 4, "a slow screen (a helper's wait) draws 1 loop in 5, not every loop")
d = R.frame(r, "", false); eq(d, true, "and once more when the wait ends")
print("test_redraw: ok")
