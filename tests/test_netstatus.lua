-- Run with: luajit tests/test_netstatus.lua   (from the repo root)
-- menu/netstatus.lua on a fake /sys/class/net and /proc/net/wireless.
local tmp = os.getenv("TMPDIR") or "/tmp"
math.randomseed(os.time() + math.floor(os.clock() * 1e6))
local root = ("%s/r3i6nos-net-%d"):format(tmp, math.random(1, 1e9))
local function write(path, text) local f = assert(io.open(path, "w")); f:write(text); f:close() end
local function iface(n, state, wireless)
  os.execute(('mkdir -p "%s/net/%s%s"'):format(root, n, wireless and "/wireless" or ""))
  write(("%s/net/%s/operstate"):format(root, n), state .. "\n")
end
os.execute('rm -rf "' .. root .. '"')
iface("lo", "unknown"); iface("usb0", "up"); iface("eth0", "down"); iface("wlan0", "down", true)
write(root .. "/wireless", "Inter-| sta-|   Quality        |   Discarded packets\n face | tus | link level noise |  nwid\n wlan0: 0000   60.  -52.  -256        0\n")
local real = os.getenv
os.getenv = function(k)
  if k == "R3I6NOS_NET_DIR" then return root .. "/net" end
  if k == "R3I6NOS_WIRELESS_FILE" then return root .. "/wireless" end
  return real(k)
end
package.path = "menu/?.lua;" .. package.path
local N = require("netstatus")
local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

local r = N.read()
eq(r.wired, false, "usb0 (the gadget) and a down eth0 are no link"); eq(r.wifi, false); eq(N.indicator(r), nil)
iface("eth0", "up")
r = N.read(); eq(r.wired, true); eq(N.indicator(r), "ë")
iface("wlan0", "up", true)
r = N.read(); eq(r.wifi, true); eq(r.cells, 3, "-52 dBm"); eq(N.indicator(r), "ë ¥███")
iface("eth0", "down")
eq(N.indicator(N.read()), "¥███")
eq(N.cells(-75), 2); eq(N.cells(-90), 1); eq(N.cells(nil, 30), 2); eq(N.cells(nil, 10), 1); eq(N.cells(nil, nil), 1)
eq(N.indicator({ wifi = true, cells = 1 }), "¥█░░"); eq(N.indicator({ wifi = true, cells = 2 }), "¥██░")
os.execute('rm -rf "' .. root .. '"')
-- as ANSIapps Mono glyph names
eq(N.glyphs(nil), nil); eq(N.glyphs({ wired = false, wifi = false }), nil)
eq(N.glyphs({ wifi = true, cells = 2 }).names[1], "sys.wifi.2"); eq(N.glyphs({ wifi = true, cells = 2 }).wired, false)
eq(N.glyphs({ wifi = true, cells = 9 }).names[1], "sys.wifi.3", "clamped")
eq(#N.glyphs({ wired = true }).names, 0); eq(N.glyphs({ wired = true }).wired, true)

print("netstatus: ok")
