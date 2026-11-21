-- Run with: luajit tests/test_portmaster_notice.lua   (from the repo root)
-- PortMaster's notice: the terms once, a reminder after, the runtimes' checklist, nothing starts until A on the accepting choice.
package.path = "menu/?.lua;theme/?.lua;" .. package.path
local theme = require("r3i6nos_ansiapps")
local settings = require("settings")
local view = require("view")
local pm = require("portmaster")

local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end
local function has(rows, text)
  for _, r in ipairs(rows) do if r:find(text, 1, true) then return true end end
  error(("%q not on the screen:\n%s"):format(text, table.concat(rows, "\n")))
end

-- a fake card and tools folder
local files, writes = {}, 0
pm.sys = {
  exists = function(path) return files[path] == true end,
  write = function(path) if files.readonly then return false end files[path] = true writes = writes + 1 return true end,
}
pm.TOOLS, pm.ROMS = "/tools", "/roms"
eq(pm.is({ type = "native", title = "PortMaster" }), true)
eq(pm.is({ type = "native", title = "Commander" }), false)
eq(pm.is({ type = "port", title = "PortMaster" }), false)

local function render(s)
  local scr = theme.screen()
  view.render(scr, { entries = {}, selected = 1, dialog = s })
  return scr:dump(), scr
end
for _, l in ipairs(pm.TERMS) do assert(theme.width(l) <= 52, "a terms line fits the box: " .. l) end

-- first launch: terms, checklist, nothing started until A on the accepting choice
files["/tools/r3i6nos-run-pyxel"] = true
local s, went = settings.new(), 0
settings.openPortMaster(s, function() went = went + 1 end)
eq(settings.node(s).kind, "notice"); eq(settings.node(s).first, true)
local rows, scr = render(s)
has(rows, "NOT supported"); has(rows, "Discord"); has(rows, "slim on purpose"); has(rows, "Pyxel"); has(rows, "Ren'Py"); has(rows, "Solarus")
has(rows, "I understand: accept and open"); has(rows, "Not now"); has(rows, "not installed"); has(rows, " installed")
assert(#rows == 24)
settings.press(s, "back")                       -- B: declined, nothing remembered, nothing started
eq(settings.isOpen(s), false); eq(went, 0); eq(writes, 0); eq(pm.accepted(), false)
settings.openPortMaster(s, function() went = went + 1 end)
settings.press(s, "down"); settings.press(s, "accept")      -- "Not now"
eq(went, 0); eq(writes, 0); eq(settings.isOpen(s), false)
settings.openPortMaster(s, function() went = went + 1 end)
settings.press(s, "accept")                      -- accept
eq(went, 1); eq(writes, 1); eq(pm.accepted(), true); eq(settings.isOpen(s), false)

-- later launches: a short reminder, the checklist, no terms, no second write
files["/tools/r3i6nos-run-renpy"] = true
settings.openPortMaster(s, function() went = went + 1 end)
eq(settings.node(s).first, false)
rows = render(s)
has(rows, "unofficial"); has(rows, "Open PortMaster"); has(rows, "Pyxel")
assert(not (function() for _, r in ipairs(rows) do if r:find("I understand", 1, true) then return true end end end)(), "no terms again")
local st = pm.runtimes()
eq(st[1].installed, true); eq(st[2].installed, true); eq(st[3].installed, false)
settings.press(s, "accept")
eq(went, 2); eq(writes, 1)

-- the card cannot keep the answer: it still goes on, and asks again next time
files["/roms/ports/.r3i6nos-portmaster-terms"] = nil; files.readonly = true
settings.openPortMaster(s, function() went = went + 1 end)
settings.press(s, "accept")
eq(went, 3); eq(pm.accepted(), false)

-- every pair on the notice passes 4.5:1 in the theme's styles
for _, k in ipairs({ "warn", "body", "strong", "hint", "select" }) do
  assert(theme.contrast(theme.STYLE[k].fg, theme.STYLE[k].bg) >= 4.5, k)
end
print("test_portmaster_notice: ok")
