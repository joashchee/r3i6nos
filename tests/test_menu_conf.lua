-- Run with: luajit tests/test_menu_conf.lua   (from the repo root)
-- menu/conf.lua must leave LÖVE's audio module off: loading it opens the sound device and starts OpenAL's mixer thread (43 wake-ups a second,
-- 6 to 8% of a core on the idle menu, measured on the unit 2026-11-15). The menu loads it on first use (menu/main.lua, testSound).
local t = { window = {}, modules = {} }
local love = { conf = nil }
_G.love = love
dofile("menu/conf.lua")
love.conf(t)
assert(t.modules.audio == false, "the menu must not load love.audio at start (the idle mixer thread)")
assert(t.modules.sound ~= false, "love.sound makes the test sound's samples and opens no device: keep it")

local f = assert(io.open("menu/main.lua")):read("*a")
assert(f:find('require%("love%.audio"%)'), "main.lua loads love.audio when the test sound is first wanted")
assert(not f:find("love%.audio%.newSource"), "no direct love.audio use at load: it would be nil with the module off")
print("ok: test_menu_conf.lua")
