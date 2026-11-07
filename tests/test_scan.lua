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

-- built-in defaults: listed, and a card file of the same name replaces them
local defs = root .. "-defaults"
sh(('mkdir -p "%s/Love 11.5" "%s/Love 12.0"'):format(defs, defs))
sh(('touch "%s/Love 11.5/Checker_One.love" "%s/Love 11.5/zeta.love" "%s/Love 12.0/Checker_One.love"'):format(defs, defs, defs))
sh(('touch "%s/launchers/Input_Checker.sh"'):format(root))
list = scan.entries(root, root .. "/launchers", defs)
local titles, count_zeta = {}, 0
for _, e in ipairs(list) do
  titles[#titles + 1] = e.type .. ":" .. e.title
  if e.title == "zeta" then count_zeta = count_zeta + 1; assert(e.target:sub(1, #defs) ~= defs, "the card's zeta wins over the built-in") end
end
local joined = table.concat(titles, ",")
assert(joined:find("love115:Checker One", 1, true), "built-in 11.5 app listed: " .. joined)
assert(joined:find("love120:Checker One", 1, true), "built-in 12.0 app listed: " .. joined)
assert(joined:find("native:Input Checker", 1, true), "native title has underscores cleaned: " .. joined)
assert(count_zeta == 1, "zeta listed once, got " .. count_zeta)
sh(('rm -rf "%s" "%s"'):format(root, defs))
print("scan: ok")
