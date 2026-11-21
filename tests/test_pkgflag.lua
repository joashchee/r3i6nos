-- Run with: luajit tests/test_pkgflag.lua   (from the repo root). menu/pkgflag.lua: r3i6nos-pkg's "something changed" flag.
package.path = "menu/?.lua;" .. package.path
local P = require("pkgflag")
local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

local path = (os.getenv("TMPDIR") or "/tmp") .. "/pkgflag-test-" .. os.time() .. "-" .. math.random(1e6)
os.remove(path)
local s = P.new(path)

eq(P.poll(s, 0.4), false, "less than a second: no look")
eq(P.poll(s, 0.7), false, "a look, nothing there")

local f = assert(io.open(path, "w")); f:write("changed\n"); f:close()
eq(P.poll(s, 0.3), false, "not yet time to look")
eq(P.poll(s, 0.8), true, "the flag is seen")
eq(io.open(path, "r"), nil, "and taken away")
eq(P.poll(s, 1.5), false, "once per change")

f = assert(io.open(path, "w")); f:close()
eq(P.take(s), true, "take at the start takes a stale flag")
eq(P.take(s), false)
-- rule 18: the extraction's progress file
local pp = path .. ".progress"
os.remove(pp)
eq(P.progress(pp), nil, "no file, no bar")
f = assert(io.open(pp, "w")); f:write("step=1\nsteps=3\ndoing=Installing pyxel\n"); f:close()
local pr = P.progress(pp)
eq(pr.step, 1); eq(pr.steps, 3); eq(pr.doing, "Installing pyxel")
f = assert(io.open(pp, "w")); f:write("step=9\nsteps=3\ndoing=x\n"); f:close()
eq(P.progress(pp).step, 3, "never past the total")
f = assert(io.open(pp, "w")); f:write("garbage"); f:close()
eq(P.progress(pp), nil, "a half-written file is no bar")
os.remove(pp)
print("pkgflag: ok")
