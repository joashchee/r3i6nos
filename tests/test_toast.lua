-- Run with: luajit tests/test_toast.lua   (from the repo root)
-- theme/r3i6nos_toast.lua: the hot-plug toast, the same behaviour as theme/r3i6nos_toast.h (tests/test_toast.c).
package.path = "theme/?.lua;" .. package.path
local T = require("r3i6nos_ansiapps")
local toast = require("r3i6nos_toast")
local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

local dir = (os.getenv("TMPDIR") or "/tmp") .. "/r3-toast-lua-" .. tostring(os.time()) .. "-" .. tostring(math.random(1e6))
os.execute("mkdir -p '" .. dir .. "'")
local clock = 1000
local function add(level, text)
  local f = assert(io.open(dir .. "/events", "a"))
  f:write(("%d\t%s\t%s\n"):format(clock, level, text))
  clock = clock + 1
  f:close()
end
local function row0(t)
  local scr = T.screen()
  T.topBar(scr, "App", "1.0")
  toast.draw(t, scr)
  return scr:dump()[1]
end

add("info", "Headphones connected")
local t = toast.new(dir)
toast.poll(t, 0)
eq(toast.active(t), false, "history is not shown")

add("info", "USB audio connected: Zeta Headset")
toast.poll(t, 1)
assert(t.showing and not t.error and row0(t):find("USB audio connected: Zeta Headset", 1, true), "a toast over the top bar")
assert(not row0(t):find("App", 1, true), "it covers the bar's text")
eq(toast.key(t), false, "a key does not clear a change toast"); assert(t.showing)
toast.poll(t, 3.9); assert(t.showing, "still up")
toast.poll(t, 4.01); assert(not t.showing and row0(t):find("App", 1, true), "gone after 3 seconds")

add("info", "DC power connected"); add("info", "Headphones removed")
toast.poll(t, 5); assert(row0(t):find("DC power connected", 1, true))
toast.poll(t, 8.1); assert(row0(t):find("Headphones removed", 1, true), "the next, each for its 3 seconds")
toast.poll(t, 11.2); assert(not t.showing)

add("error", "A USB device did not start")
toast.poll(t, 12)
assert(t.showing and t.error and row0(t):find("did not start", 1, true) and row0(t):find("Press a key", 1, true))
toast.poll(t, 6000); assert(t.showing and t.error, "a failure stays")
eq(toast.key(t), true, "a key clears it and is used up"); assert(not t.showing)
eq(toast.key(t), false)

add("info", "USB storage connected: Stick"); toast.poll(t, 7000); assert(t.showing and not t.error)
add("error", "USB audio Dead DAC gives no sound output"); toast.poll(t, 7000.4)
assert(t.error and row0(t):find("Dead DAC", 1, true), "a failure does not wait for a flash to end")
toast.key(t); toast.poll(t, 7001); assert(not t.showing, "the interrupted change is not brought back")

for _ = 1, 12 do add("info", "noise") end
add("error", "the one that matters")
toast.poll(t, 8000)
assert(t.error and row0(t):find("the one that matters", 1, true), "a failure survives a full queue")
toast.key(t); toast.poll(t, 8001)
toast.hold(t, true)
add("info", "Headphones connected"); add("error", "TF2 card removed while in use")
toast.poll(t, 9000)
assert(not t.showing and not row0(t):find("TF2", 1, true), "held: nothing is drawn")
eq(#t.queue, 1, "the change was dropped, the failure waits"); eq(toast.key(t), false, "held: a key is the app's own")
toast.hold(t, false); toast.poll(t, 9001)
assert(t.showing and t.error and row0(t):find("TF2 card removed", 1, true), "the failure shows when the hold ends")
toast.hold(t, true); toast.hold(t, false); toast.poll(t, 9002)
assert(t.showing and t.error, "a failure that was up comes back")
assert(T.contrast(T.BLACK, T.LIGHT_GREEN) >= 4.5 and T.contrast(T.WHITE, T.RED) >= 4.5, "readable")

os.execute("rm -rf '" .. dir .. "'")
print("toast: ok")
