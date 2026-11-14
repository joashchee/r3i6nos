-- Run with: luajit tests/test_mirror_menu.lua   (from the repo root)
-- menu/mirror.lua: what Settings > Screen mirror reads from the daemon and the commands it runs, with a fake world.
package.path = "menu/?.lua;" .. package.path
local mirror = require("mirror")
local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

local files, alive, ran, answers = {}, {}, {}, {}
local sys = {
  read = function(p) return files[p] end,
  alive = function(pid) return alive[pid] == true end,
  run = function(cmd)
    ran[#ran + 1] = cmd
    for key, res in pairs(answers) do if cmd:find(key, 1, true) then return res[1], res[2] end end
    return "", true
  end,
}

-- status: the daemon's files, and a flag left by a dead daemon counts for nothing
eq(mirror.status(sys).running, false, "no flag, not running")
files[mirror.DIR .. "/on"] = "4242\n"
eq(mirror.status(sys).running, false, "a flag whose process is gone is not a running mirror")
alive[4242] = true
files[mirror.DIR .. "/info"] = "url=http://192.168.1.50:36363/abcd1234/\niface=eth0\naddress=192.168.1.50\nport=36363\nviewers=2\npid=4242\n"
local st = mirror.status(sys)
eq(st.running, true); eq(st.url, "http://192.168.1.50:36363/abcd1234/"); eq(st.iface, "eth0"); eq(st.viewers, 2)

-- interfaces: the daemon's list, parsed, and nothing else trusted
answers["' list"] = { "eth0\t192.168.1.50\twired\nwlan0\t10.0.0.7\twifi\nnot a line\n../x\t1.2.3.4\twired\n", true }
local links = mirror.interfaces(sys)
eq(#links, 2); eq(links[1].name, "eth0"); eq(links[2].addr, "10.0.0.7"); eq(links[2].kind, "wifi")

-- start: the command is quoted, the name checked, the answer read
ran = {}
answers["start"] = { "http://192.168.1.50:36363/abcd1234/\n", true }
local ok, url = mirror.start("eth0", sys)
eq(ok, true); eq(url, "http://192.168.1.50:36363/abcd1234/")
assert(ran[1]:find("start --iface 'eth0'", 1, true), "the interface is quoted: " .. ran[1])
ran = {}
local bad, why = mirror.start("eth0; rm -rf /", sys)
eq(bad, false); eq(why, "not a network interface"); eq(#ran, 0, "a hostile name never reaches a shell")
answers["start"] = { "r3i6nos-mirror: eth0 has no IPv4 address (is the link up?)\n", false }
ok, why = mirror.start("eth0", sys)
eq(ok, false); assert(why:find("no IPv4 address"), why)
answers["start"] = { "", false }
ok, why = mirror.start("eth0", sys); eq(why, "it did not start")

-- stop
answers["stop"] = { "stopping\n", true }
eq(mirror.stop(sys), true)
answers["stop"] = { "", false }
eq(mirror.stop(sys), false)

-- the words fit the screen (the menu never scrolls: pages of at most 18 lines of 60 characters)
local function fits(lines, what)
  assert(#lines <= 18, what .. " has " .. #lines .. " lines")
  for _, l in ipairs(lines) do assert(#l <= 60, what .. " line too long: " .. l) end
end
for i, p in ipairs(mirror.INSTRUCTIONS) do fits(p, "instructions page " .. i) end
fits(mirror.NO_LINK, "no link")
local lines = mirror.addressLines(st)
fits(lines, "address"); eq(lines[4], "  http://192.168.1.50:36363/abcd1234/"); eq(lines[7], "2 pages are watching.")
eq(mirror.addressLines({ url = "u", viewers = 1 })[7], "1 page is watching.")

print("ok: test_mirror_menu.lua")
