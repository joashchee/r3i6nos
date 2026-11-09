-- Run with: luajit tests/test_about.lua   (from the repo root)
-- menu/about.lua: the system's lines (with LÖVE's extra), starting the update check safely, reading its answer, the licence pages.
package.path = "menu/?.lua;" .. package.path
local about = require("about")
local pages = require("netpages")
local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

about.HELPER, about.OUT = "/usr/local/bin/r3i6nos-about", "/run/loveos/it's.txt"
local ran, removed, file = {}, {}, nil
local sys = {
  lines = function(cmd) ran[#ran + 1] = cmd; return { "THIS BUILD", "Version      r3i6nos 0.1.0" } end,
  read = function() return file end,
  remove = function(p) removed[#removed + 1] = p end,
  exec = function(cmd) ran[#ran + 1] = cmd; return 0 end,
}

-- info: the helper's lines, then what LÖVE adds after a blank line; a failing extra is left out, never an error
local l = about.info(sys)
eq(ran[1], "'/usr/local/bin/r3i6nos-about' info 2>/dev/null"); eq(#l, 2)
about.extra = function() return { "MENU", "Runs on      LÖVE 11.5.0" } end
l = about.info(sys); eq(l[3], ""); eq(l[4], "MENU"); eq(#l, 5)
about.extra = function() error("no joystick module") end
eq(#about.info(sys), 2)
about.extra = nil
sys.lines = function() return nil end
eq(about.info(sys)[1], "Could not read the system's details.")

-- startCheck: the old answer goes first, the path is quoted (a ' in it cannot break out), it runs in the background
ran = {}
assert(about.startCheck(sys))
eq(removed[1], about.OUT)
eq(ran[1], [['/usr/local/bin/r3i6nos-about' check '/run/loveos/it'\''s.txt' >/dev/null 2>&1 &]])
sys.exec = function() return false end
local ok, err = about.startCheck(sys); assert(not ok and err)

-- poll: nothing until the whole answer is there; then ok and the lines
file = nil; eq(about.poll(sys), nil)
file = "garbage"; eq(about.poll(sys), nil)
file = "ok=1\nThis is the latest release.\n\nThis build   r3i6nos 0.1.0\n"
local r = about.poll(sys); assert(r.ok); eq(#r.lines, 3); eq(r.lines[1], "This is the latest release."); eq(r.lines[2], "")
file = "ok=0\nCould not reach GitHub:\n"
r = about.poll(sys); assert(not r.ok); eq(r.lines[1], "Could not reach GitHub:")

-- the fixed words fit a page (the menu never scrolls)
for _, pg in ipairs(about.LICENCES) do
  assert(#pg <= pages.PER, "a licence page is too long")
  for _, line in ipairs(pg) do assert(#line:gsub("[\128-\191]", "") <= pages.WIDTH, "too wide: " .. line) end
end
for _, line in ipairs(about.CHECK_LOST) do assert(#line <= pages.WIDTH, "too wide: " .. line) end
print("about: ok")
