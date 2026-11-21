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
sh(('touch "%s/launchers/IO_Checker.sh"'):format(root))
list = scan.entries(root, root .. "/launchers", defs)
local titles, count_zeta = {}, 0
for _, e in ipairs(list) do
  titles[#titles + 1] = e.type .. ":" .. e.title
  if e.title == "zeta" then count_zeta = count_zeta + 1; assert(e.target:sub(1, #defs) ~= defs, "the card's zeta wins over the built-in") end
end
local joined = table.concat(titles, ",")
assert(joined:find("love115:Checker One", 1, true), "built-in 11.5 app listed: " .. joined)
assert(joined:find("love120:Checker One", 1, true), "built-in 12.0 app listed: " .. joined)
assert(joined:find("native:IO Checker", 1, true), "native title has underscores cleaned: " .. joined)
assert(count_zeta == 1, "zeta listed once, got " .. count_zeta)
-- the data card (docs/STORAGE.md): its games are listed too; the same file name on both cards shows once, from the system card
local data = root .. "-data"
sh(('mkdir -p "%s/Love 12.0/Data_Game" "%s/Love 11.5"'):format(data, data))
sh(('touch "%s/Love 12.0/Data_Game/main.lua" "%s/Love 11.5/zeta.love"'):format(data, data))
list = scan.entries(root, root .. "/launchers", defs, data)
local seen, zetas = {}, 0
for _, e in ipairs(list) do
  seen[e.type .. ":" .. e.title] = e.target
  if e.title == "zeta" then zetas = zetas + 1 end
end
assert(seen["love120:Data Game"] == data .. "/Love 12.0/Data_Game", "a data card game is listed")
assert(zetas == 1 and seen["love115:zeta"] == root .. "/Love 11.5/zeta.love", "the system card's copy wins until step 2")
assert(#scan.entries(root, root .. "/launchers", defs, root .. "-no-such-card") > 0, "an absent data card is fine")
-- game-engine runtimes (docs/RUNTIMES.md): a Pyxel folder with main.py or a .pyxapp, a Ren'Py folder with game/, a Solarus quest folder; anything else is not listed
local rt = root .. "-rt"
sh(('mkdir -p "%s/Pyxel/Jump_Game" "%s/Pyxel/Empty" "%s/Renpy/Story/game" "%s/Renpy/NoGame" "%s/Solarus/Quest_A/data" "%s/Solarus/Quest_B" "%s/Solarus/Bad" "%s/launchers" "%s/AGS/Quest_One" "%s/AGS/Empty" "%s/AGS/Old_Game" "%s/Godot/Platformer" "%s/Godot/Bare"')
  :format(rt, rt, rt, rt, rt, rt, rt, rt, rt, rt, rt, rt, rt))
sh(('touch "%s/Pyxel/Jump_Game/main.py" "%s/Pyxel/Shooter.pyxapp" "%s/Pyxel/notes.txt" "%s/Renpy/Story/game/script.rpy" "%s/Solarus/Quest_A/data/quest.dat" "%s/Solarus/Quest_B/data.solarus" "%s/AGS/Quest_One/ac2game.dat" "%s/AGS/Old_Game/Game.EXE" "%s/Godot/Platformer/p.pck" "%s/Godot/Single_Game.pck" "%s/Godot/notes.txt"')
  :format(rt, rt, rt, rt, rt, rt, rt, rt, rt, rt, rt))
list = scan.entries(rt, rt .. "/launchers")
local rts = {}
for _, e in ipairs(list) do rts[#rts + 1] = e.type .. ":" .. e.title end
table.sort(rts)
local want_rt = { "ags:Old Game", "ags:Quest One", "godot:Platformer", "godot:Single Game", "pyxel:Jump Game", "pyxel:Shooter", "renpy:Story", "solarus:Quest A", "solarus:Quest B" }
assert(table.concat(rts, ",") == table.concat(want_rt, ","), "runtime games: " .. table.concat(rts, ","))
for _, e in ipairs(list) do
  if e.type == "pyxel" and e.title == "Shooter" then assert(e.target == rt .. "/Pyxel/Shooter.pyxapp", "a .pyxapp is its own target") end
  if e.type == "godot" and e.title == "Single Game" then assert(e.target == rt .. "/Godot/Single_Game.pck", "a .pck is its own target") end
  if e.type == "renpy" then assert(e.target == rt .. "/Renpy/Story", "a Ren'Py game's target is its folder") end
end
sh(('rm -rf "%s"'):format(rt))
sh(('rm -rf "%s"'):format(data))
sh(('rm -rf "%s" "%s"'):format(root, defs))
print("scan: ok")
