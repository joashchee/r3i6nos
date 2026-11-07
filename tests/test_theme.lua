-- Run with: luajit tests/test_theme.lua   (from the repo root)
package.path = "theme/?.lua;" .. package.path
local T = require("r3i6nos_ansiapps")
local font = require("vga8x16")

local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

-- the grid: 64 x 24 cells of 10 x 20 is the 640 x 480 screen; row 0 + 20 display rows + 3 command rows
eq(T.COLS * T.CELL_W, 640, "width"); eq(T.ROWS * T.CELL_H, 480, "height")
eq(T.TOP_ROW, 0); eq(T.DISPLAY_TOP, 1); eq(T.DISPLAY_ROWS, 20); eq(T.COMMAND_TOP, 21); eq(T.COMMAND_ROWS, 3)

-- the font's licence terms (CC BY-SA 4.0) travel with the generated file and the shipped folder
local function slurp(p) local f = assert(io.open(p)); local t = f:read("*a"); f:close(); return t end
local head = slurp("theme/vga8x16.lua"):sub(1, 1200)
for _, need in ipairs({ "VileR", "CC BY-SA 4.0", "creativecommons.org/licenses/by-sa/4.0", "int10h.org", "CHANGES", "NOT under the repository" }) do
  assert(head:find(need, 1, true), "theme/vga8x16.lua header lacks: " .. need)
end
head = slurp("theme/r3i6nos_vga8x16.h"):sub(1, 1200)
for _, need in ipairs({ "VileR", "CC BY-SA 4.0", "CHANGES" }) do assert(head:find(need, 1, true), "font header lacks: " .. need) end
assert(slurp("theme/font/LICENSE.TXT"):find("Attribution-ShareAlike 4.0", 1, true), "licence text")
assert(slurp("theme/font/README.txt"):find("Changes made", 1, true), "font README states the changes")
assert(slurp("NOTICE"):find("CC BY-SA 4.0", 1, true), "NOTICE credits the font")

-- the font data: 256 glyphs of 16 bytes, and the real shapes of a few
eq(#font.glyphs, 256, "glyph count"); eq(#font.unicode, 256, "unicode count")
for i, g in ipairs(font.glyphs) do eq(#g, 32, "glyph " .. (i - 1) .. " length") end
eq(font.glyphs[66], "00001038" .. "6cc6c6fe" .. "c6c6c6c6" .. "00000000", "VGA 'A'")
eq(font.glyphs[220], "ffffffffffffffffffffffffffffffff", "full block")
eq(font.glyphs[33], string.rep("0", 32), "space")

-- 8x16 scaled to 10x20, nearest neighbour
eq(T.COLMAP[0], 0); eq(T.COLMAP[9], 7); eq(T.ROWMAP[0], 0); eq(T.ROWMAP[19], 15)
local seen = {}
for x = 0, 9 do seen[T.COLMAP[x]] = true end
for s = 0, 7 do assert(seen[s], "source column " .. s .. " is lost") end
seen = {}
for y = 0, 19 do seen[T.ROWMAP[y]] = true end
for s = 0, 15 do assert(seen[s], "source row " .. s .. " is lost") end
local full = T.glyph10x20(219)
for y = 0, 19 do for x = 0, 9 do eq(full[y][x], 1, "full block pixel") end end
local sp = T.glyph10x20(32)
for y = 0, 19 do for x = 0, 9 do eq(sp[y][x], 0, "space pixel") end end
-- box lines join across cells: every row of │ is the same, every column of ─ is the same, and they are not empty
local v, h = T.glyph10x20(179), T.glyph10x20(196)
local on = 0
for y = 0, 19 do for x = 0, 9 do
  eq(v[y][x], v[0][x], "│ row " .. y); eq(h[y][x], h[y][0] == 1 and 1 or h[y][x])
  on = on + v[y][x]
end end
assert(on > 0 and on % 20 == 0, "│ has pixels")
for x = 0, 9 do for y = 0, 19 do eq(h[y][x], h[y][0], "─ column " .. x) end end
local hon = 0
for y = 0, 19 do hon = hon + h[y][0] end
assert(hon > 0, "─ has pixels")
-- A is the 8x16 'A' stretched: row 0 of the source has nothing, and the A is wider than 8 px of nothing
local a = T.glyph10x20(65)
local any = 0
for y = 0, 19 do for x = 0, 9 do any = any + a[y][x] end end
assert(any > 20, "A has pixels")

-- the C theme (theme/r3i6nos_ansiapps.h) scales the font the same way: both hash every 10x20 pixel and must agree
eq(T.glyphHash(), 0x71e726c8, "glyph hash (update r3i6nos_ansiapps.h's R3_GLYPH_HASH too if the font or scaling changes)")

-- UTF-8 to code page 437
local function idx(s) return table.concat(T.decode(s), ",") end
eq(idx("Az"), "65,122"); eq(idx("Ö"), "153"); eq(idx("é"), "130"); eq(idx("─│█"), "196,179,219")
eq(idx("日"), "63", "unknown is ?"); eq(idx("a\tb"), "97,32,98", "tab is a space"); eq(idx("»"), "175")
eq(idx("\xff"), "63", "a bad byte is ?"); eq(idx("\xe2\x94"), "63", "a cut sequence is ?")
eq(T.width("LÖVE 11.5"), 9)
eq(table.concat(T.fit("abcdef", 4), ","), "97,98,99,175", "cut ends in »")
eq(#T.fit("abc", 4), 3)

-- contrast: every pair the theme uses passes 4.5:1; the function itself is right at the extremes
for name, s in pairs(T.STYLE) do assert(T.contrast(s.fg, s.bg) >= 4.5, name .. " fails 4.5:1") end
assert(math.abs(T.contrast(T.WHITE, T.BLACK) - 21) < 0.01)
assert(T.contrast(T.DARK_GRAY, T.BLACK) < 4.5, "dark gray on black must fail")
assert(T.contrast(T.LIGHT_RED, T.BLUE) < 4.5, "light red on blue must fail")

-- the screen
local s = T.screen()
eq(T.COLS * T.ROWS, #s.ch)
s:put(-1, 0, 65); s:put(64, 0, 65); s:put(0, 24, 65) -- off screen: ignored
eq(s:text(62, 5, "abcd"), 64, "text clips at the edge")
eq(s:dump()[6]:sub(63, 64), "ab")
s:box(2, 2, 20, 4, { title = "Hi", right = "1/2", foot = "OK" })
local d = s:dump()
eq(d[3]:sub(3, 3), "?"); eq(s.ch[2 * 64 + 2 + 1], 201, "╔"); eq(s.ch[5 * 64 + 21 + 1], 188, "╝")
assert(d[3]:find("Hi", 1, true) and d[3]:find("1/2", 1, true) and d[6]:find("OK", 1, true), "title, right and foot in the edges")

-- top bar and command line stay in their rows
s = T.screen()
T.topBar(s, "left", "right")
for c = 0, 63 do eq(s.bg[c + 1], T.BLUE, "bar bg") end
eq(s.bg[64 + 1], T.BLACK, "bar is one row")
s = T.screen()
T.commandLine(s, { reply = "reply", line = "run x", caret = true, left = "3 of 9", right = "native" })
local rows = s:dump()
for r = 0, 20 do eq(rows[r + 1], string.rep(" ", 64), "row " .. r .. " untouched") end
assert(rows[22]:find("reply", 1, true) and rows[23]:find("> run x", 1, true) and rows[24]:find("3 of 9", 1, true), "three rows")
assert(rows[24]:sub(-8):find("native", 1, true), "status right")
T.commandLine(s, { reply = string.rep("x", 100) })
eq(s.ch[21 * 64 + 62 + 1], 175, "a long reply is cut with »")

-- list dialog: whole pages, never a scroll
local items = {}
for i = 1, 40 do items[i] = { label = "Item " .. i, value = "v" } end
eq(T.listRows(20), 18)
local page, pages, first, last = T.listWindow(40, 1, 18); eq(page, 1); eq(pages, 3); eq(first, 1); eq(last, 18)
page, pages, first, last = T.listWindow(40, 19, 18); eq(page, 2); eq(first, 19); eq(last, 36)
page, pages, first, last = T.listWindow(40, 40, 18); eq(page, 3); eq(first, 37); eq(last, 40)
page, pages = T.listWindow(0, 0, 18); eq(page, 1); eq(pages, 1)
s = T.screen()
T.listDialog(s, { col = 0, row = 1, w = 64, h = 20, title = "List", items = items, sel = 19, foot = "A Run" })
d = s:dump()
assert(d[2]:find("Page 2/3", 1, true), "page in the top edge"); assert(d[21]:find("A Run", 1, true), "foot in the bottom edge")
assert(d[3]:find("Item 19", 1, true) and not d[3]:find("Item 1 ", 1, true), "page 2 starts at item 19")
eq(s.bg[2 * 64 + 5], T.CYAN, "selected row is on cyan"); eq(s.fg[2 * 64 + 5], T.BLACK)
eq(s.bg[3 * 64 + 5], T.BLACK, "the next row is not")
for r = 1, 20 do eq(s.ch[r * 64 + 1] ~= 32, true, "the frame's left edge, row " .. r) end

-- a list that fits one page has no page number
s = T.screen()
T.listDialog(s, { col = 0, row = 1, w = 40, h = 6, title = "Small", items = { { label = "a" }, { label = "b" } }, sel = 1 })
assert(not table.concat(s:dump(), "\n"):find("Page", 1, true), "no Page x/y for one page")

-- the list rule
local sel, ev = T.listKey(1, 40, 18, "down"); eq(sel, 2); eq(ev, nil)
sel = T.listKey(1, 40, 18, "up"); eq(sel, 40, "up wraps")
sel = T.listKey(40, 40, 18, "down"); eq(sel, 1, "down wraps")
sel = T.listKey(5, 40, 18, "pagedown"); eq(sel, 23)
sel = T.listKey(30, 40, 18, "pagedown"); eq(sel, 40, "page clamps")
sel = T.listKey(5, 40, 18, "pageup"); eq(sel, 1)
sel = T.listKey(20, 40, 18, "left"); eq(sel, 2); sel = T.listKey(20, 40, 18, "right"); eq(sel, 38)
sel = T.listKey(20, 40, 18, "home"); eq(sel, 1); sel = T.listKey(20, 40, 18, "end"); eq(sel, 40)
sel, ev = T.listKey(7, 40, 18, "accept"); eq(sel, 7); eq(ev, "accept")
sel, ev = T.listKey(7, 40, 18, "back"); eq(ev, "back")
sel, ev = T.listKey(7, 40, 18, "more"); eq(ev, "more")
sel, ev = T.listKey(0, 0, 18, "accept"); eq(ev, nil, "nothing to accept in an empty list")
sel, ev = T.listKey(0, 0, 18, "more"); eq(ev, "more", "More works on an empty list")

-- the R36S's buttons (measured; docs/hardware-findings.md)
eq(T.PAD[9], "up"); eq(T.PAD[10], "down"); eq(T.PAD[11], "left"); eq(T.PAD[12], "right")
eq(T.PAD[2], "accept", "A is 2"); eq(T.PAD[1], "back", "B is 1"); eq(T.PAD[14], "more", "Start is 14")
eq(T.PAD[3], "x"); eq(T.PAD[4], "y"); eq(T.PAD[13], "select")
eq(T.PAD[5], "pageup"); eq(T.PAD[6], "pagedown"); eq(T.PAD[7], "home"); eq(T.PAD[8], "end")
eq(T.KEYS["return"], "accept"); eq(T.KEYS.escape, "back")

print("theme: ok")
