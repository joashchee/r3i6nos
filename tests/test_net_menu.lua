-- Run with: luajit tests/test_net_menu.lua   (from the repo root)
-- The on-screen keyboard, the helper's file protocol (over a temporary folder) and the help pages.
package.path = "menu/?.lua;" .. package.path
local osk = require("osk")
local netclient = require("netclient")
local netpages = require("netpages")

local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

-- ---- on-screen keyboard
local o = osk.new({ title = "t", min = 3, max = 6 })
eq(osk.key(o), "1")
osk.press(o, "right"); eq(osk.key(o), "2"); osk.press(o, "left"); osk.press(o, "left"); eq(osk.key(o), "0", "columns wrap")
osk.press(o, "up"); eq(o.row, 4, "rows wrap"); osk.press(o, "down"); eq(o.row, 1)
o.row, o.col = 2, 1; osk.press(o, "accept"); eq(o.text, "q")
osk.press(o, "y"); eq(osk.LAYERS[o.layer].name, "ABC"); osk.press(o, "accept"); eq(o.text, "qQ")
osk.press(o, "y"); osk.press(o, "y"); eq(o.layer, 1, "layers cycle")
osk.press(o, "x"); eq(o.text, "q"); osk.press(o, "x"); osk.press(o, "x"); eq(o.text, "", "delete at empty is fine")
eq(osk.press(o, "more"), nil); assert(o.error:find("At least 3", 1, true), "done is refused while too short")
osk.type(o, "abc"); eq(osk.press(o, "more"), "done"); eq(o.text, "abc")
osk.type(o, "defghi"); eq(o.text, "abcdef", "the maximum holds for typed text")
osk.press(o, "accept"); eq(o.text, "abcdef", "and for the key"); assert(o.error)
eq(osk.press(o, "back"), "cancel")
osk.press(o, "select"); eq(o.hidden, true)
o = osk.new({}); osk.type(o, "a\tb\n\200c"); eq(o.text, "abc", "only printable ASCII is taken")
-- every layer is a full 4 x 10 grid, and a space can be typed on each
for _, layer in ipairs(osk.LAYERS) do
  eq(#layer.rows, 4); for _, r in ipairs(layer.rows) do eq(#r, 10, layer.name) end
  assert(table.concat(layer.rows):find(" ", 1, true), layer.name .. " has a space key")
end
-- the symbols a Wi-Fi password may need are reachable
local all = table.concat((function() local t = {} for _, l in ipairs(osk.LAYERS) do t[#t + 1] = table.concat(l.rows) end return t end)())
for ch in ("abcxyzABCXYZ0123456789!@#$%^&*()-_=+[]{};:'\",<>/?\\|`~."):gmatch(".") do assert(all:find(ch, 1, true), "missing key " .. ch) end

-- ---- the file protocol
math.randomseed(os.time())
local dir = (os.getenv("TMPDIR") or "/tmp"):gsub("/$", "") .. ("/r3i6nos-netclient-%d-%d"):format(os.time(), math.random(1000000))
os.execute("mkdir -p '" .. dir .. "'")
netclient.DIR = dir
netclient.OUT = dir .. "/out"
os.execute("mkdir -p '" .. dir .. "/out'")
local function read(path) local f = io.open(path); if not f then return nil end local t = f:read("*a"); f:close(); return t end
local id = assert(netclient.request("wifi", { ssid = "My\nNet", psk = "a\\b" }))
local req = read(dir .. "/request")
assert(req:find("id=" .. id, 1, true) and req:find("verb=wifi", 1, true))
assert(req:find("ssid=My\\nNet", 1, true), "a newline is escaped"); assert(req:find("psk=a\\\\b", 1, true), "a backslash is escaped")
eq(select(2, req:gsub("\n", "\n")), 4, "one line a key")
assert(not read(dir .. "/request.tmp"), "the temporary file is renamed away")
eq(netclient.poll(id), nil, "no answer yet")
local f = io.open(dir .. "/out/result", "w")
f:write("id=" .. id .. "\nok=1\nL\tConnected: Home\nL\t  Address  192.168.1.20/24\nN\tHome\t-40 dBm\tWPA\nN\tGuest\t-70 dBm\topen\n"); f:close()
local r = assert(netclient.poll(id))
eq(r.ok, true); eq(#r.lines, 2); eq(r.lines[2], "  Address  192.168.1.20/24")
eq(#r.nets, 2); eq(r.nets[1].ssid, "Home"); eq(r.nets[1].signal, "-40 dBm"); eq(r.nets[2].security, "open")
eq(netclient.poll("some-other-id"), nil, "another request's answer is not taken")
local id2 = netclient.request("status", {})
assert(id2 ~= id, "every request has its own id")
eq(netclient.poll(id2), nil, "the earlier request's answer is not taken for the new one")
f = io.open(dir .. "/out/result", "w"); f:write("id=" .. id2 .. "\nok=0\nL\tNo adapter\n"); f:close()
eq(netclient.poll(id2).ok, false)
netclient.DIR = dir .. "/missing"
local bad, err = netclient.request("status", {}); eq(bad, nil); assert(err)
os.execute("rm -rf '" .. dir .. "'")

-- ---- the help pages fit the screen
for _, set in ipairs({ { "NETWORK", netpages.NETWORK }, { "SSH", netpages.SSH } }) do
  for i, page in ipairs(set[2]) do
    assert(#page <= netpages.PER, ("%s page %d has %d lines"):format(set[1], i, #page))
    for _, line in ipairs(page) do assert(#line <= netpages.WIDTH, ("%s page %d: %q is %d wide"):format(set[1], i, line, #line)) end
  end
end
local pg = netpages.paginate({})
eq(#pg, 1); eq(#pg[1], 0)
local forty = {}
for i = 1, 40 do forty[i] = tostring(i) end
pg = netpages.paginate(forty); eq(#pg, 3); eq(#pg[1], 18); eq(#pg[3], 4)

print("net menu: ok")
