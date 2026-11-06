-- Run with: luajit tests/test_scan.lua   (from the repo root)
package.path = "menu/?.lua;" .. package.path
local scan = require("scan")

local tmp = os.getenv("TMPDIR") or "/tmp"
local root = tmp .. "/r3i6nos-scan-" .. os.time()
local function sh(cmd) assert(os.execute(cmd)) end

sh(('mkdir -p "%s/Love 11.5/Some_Game" "%s/Love 11.5/NoMain" "%s/Love 12.0" "%s/launchers"')
  :format(root, root, root, root))
sh(('touch "%s/Love 11.5/Some_Game/main.lua" "%s/Love 11.5/zeta.love" "%s/Love 12.0/alpha.love" "%s/launchers/tux.sh" "%s/Love 11.5/notes.txt"')
  :format(root, root, root, root, root))

local list = scan.entries(root, root .. "/launchers")
local got = {}
for _, e in ipairs(list) do got[#got + 1] = e.type .. ":" .. e.title end

local want = { "love120:alpha", "love115:Some Game", "native:tux", "love115:zeta" }
assert(#got == #want, "expected " .. #want .. " entries, got " .. #got .. ": " .. table.concat(got, ", "))
for i, w in ipairs(want) do assert(got[i] == w, ("entry %d: want %s, got %s"):format(i, w, got[i])) end

sh(('rm -rf "%s"'):format(root))
print("scan: ok")
