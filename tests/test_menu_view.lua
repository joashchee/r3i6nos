-- Run with: luajit tests/test_menu_view.lua   (from the repo root)
package.path = "menu/?.lua;theme/?.lua;" .. package.path
local theme = require("r3i6nos_ansiapps")
local settings = require("settings")
local view = require("view")

local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end
local function has(line, text) assert(line:find(text, 1, true), ("%q not in %q"):format(text, line)) end

local entries = {}
for i = 1, 40 do entries[i] = { title = ("Game %02d"):format(i), type = i % 2 == 0 and "love120" or "love115", target = "/roms/Love/g" .. i .. ".love" } end
entries[3].type = "native"

local scr = theme.screen()
local dialog = settings.new()
view.render(scr, { entries = entries, selected = 1, dialog = dialog })
local rows = scr:dump()
eq(#rows, 24)
-- the reserved top row
has(rows[1], "r3i6nos"); has(rows[1], "40 apps")
for c = 1, 64 do eq(scr.bg[c], theme.BLUE, "top bar is blue") end
-- the list: rows 1-20, 18 games a page, the page in the top edge (the dump shows non-ASCII as ?)
eq(view.PER_PAGE, 18)
has(rows[2], "Games and apps"); has(rows[2], "Page 1/3")
has(rows[3], "Game 01"); has(rows[3], "L?VE 11.5"); has(rows[5], "native")
has(rows[20], "Game 18"); assert(not rows[20]:find("Game 19", 1, true), "18 a page")
has(rows[21], "L2/R2")
-- the command line: the selected game's path, the line that would run it, the place in the list
has(rows[22], "/roms/Love/g1.love"); has(rows[23], "> run Game 01"); has(rows[24], "1 of 40"); has(rows[24], "L?VE 11.5")
for r = 22, 24 do eq(scr.bg[(r - 1) * 64 + 1], theme.BLACK) end
eq(rows[23]:sub(1, 1), ">")
-- the highlighted row is black on cyan
eq(scr.bg[2 * 64 + 5], theme.CYAN)

-- later pages: the selection decides the page
view.render(scr, { entries = entries, selected = 20, dialog = dialog })
rows = scr:dump()
has(rows[2], "Page 2/3"); has(rows[3], "Game 19"); has(rows[24], "20 of 40")
view.render(scr, { entries = entries, selected = 40, dialog = dialog })
has(scr:dump()[2], "Page 3/3")

-- nothing to list
view.render(scr, { entries = {}, selected = 1, dialog = dialog })
rows = scr:dump()
has(rows[1], "0 apps"); has(rows[3], "Nothing here yet."); has(rows[24], "0 of 0")
view.render(scr, { entries = { entries[1] }, selected = 1, dialog = dialog })
has(scr:dump()[1], "1 app")

-- Settings opens over the list, in the same style, and the command line follows it
settings.open(dialog)
view.render(scr, { entries = entries, selected = 1, dialog = dialog })
rows = scr:dump()
local joined = table.concat(rows, "\n")
has(joined, "Settings"); has(joined, "Shutdown"); has(joined, "Back"); has(joined, "A Choose  B Back")
has(rows[22], "Power the device off"); has(rows[23], "> settings")
has(rows[1], "r3i6nos")
settings.press(dialog, "accept")
view.render(scr, { entries = entries, selected = 1, dialog = dialog })
rows = scr:dump()
joined = table.concat(rows, "\n")
has(joined, "Shut down the device?"); has(joined, "Yes, shut down"); has(joined, "No")
-- the highlighted row is "No" (the safe default)
local no_row
for r, line in ipairs(rows) do if line:find("No ", 1, true) and line:find("|", 1, true) == nil and r > 1 and r < 21 and scr.bg[(r - 1) * 64 + 24] == theme.CYAN then no_row = r end end
assert(no_row, "No is highlighted when the confirmation opens")

-- every cell is a code page 437 index, every colour a palette colour
for i = 1, 64 * 24 do
  assert(scr.ch[i] >= 0 and scr.ch[i] <= 255 and theme.RGB[scr.fg[i]] and theme.RGB[scr.bg[i]], "cell " .. i)
end
-- every text/background pair on the screen passes 4.5:1 (blank cells show no text)
for i = 1, 64 * 24 do
  if scr.ch[i] ~= 32 and scr.ch[i] ~= 0 then
    assert(theme.contrast(scr.fg[i], scr.bg[i]) >= 4.5, ("low contrast at cell %d: %d on %d"):format(i, scr.fg[i], scr.bg[i]))
  end
end

print("menu view: ok")
