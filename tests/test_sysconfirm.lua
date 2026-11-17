-- Run with: luajit tests/test_sysconfirm.lua   (from the repo root). menu/sysconfirm.lua: the menu confirms a system update by removing its marker.
package.path = "menu/?.lua;" .. package.path
local S = require("sysconfirm")
local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

local dir = (os.getenv("TMPDIR") or "/tmp") .. "/sysconfirm-test-" .. os.time() .. "-" .. math.random(1e6)
os.execute("mkdir -p '" .. dir .. "'")
local f = assert(io.open(dir .. "/system-pending", "w")); f:write("r3i6nos-system image 1\n"); f:close()

local s = S.new(dir)
eq(S.confirm(s), true, "the marker is there and is removed")
eq(io.open(dir .. "/system-pending", "r"), nil, "and gone")
f = assert(io.open(dir .. "/system-pending", "w")); f:close()
eq(S.confirm(s), false, "only once per start: a marker made later is the helper's, for the next start")
eq(S.confirm(S.new(dir)), true, "a new start removes it")
eq(S.confirm(S.new(dir)), false, "nothing to remove is not an error")
eq(S.confirm(S.new(dir .. "/no/such/folder")), false, "a missing folder is not an error")
os.execute("rm -rf '" .. dir .. "'")
print("sysconfirm: ok")
