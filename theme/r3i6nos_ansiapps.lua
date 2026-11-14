-- r3i6nos-ansiapps: the text-mode theme every r3i6nos app starts from (docs/R3I6NOS-ANSIAPPS-THEME.md).
--
-- A 640x480 screen of 64 columns by 24 rows of 10x20 cells (the IBM VGA 8x16 font, scaled up), in the
-- 16 VGA colours. Row 0 is reserved (the top bar), rows 1-20 are the display view, rows 21-23 are the
-- three-row command line (reply, prompt, status), as in KlamANSI. Nothing scrolls; lists turn whole pages.
--
-- Everything here is pure Lua so luajit can test it, except the last section (T.init, T.draw), which
-- needs LÖVE. Rows and columns are 0-based; glyphs are code page 437 indices (0-255); strings are UTF-8
-- and are turned into code page 437 (an unknown character shows as "?").

local font = require("vga8x16")
local okMirror, mirror = pcall(require, "r3i6nos_mirror") -- an image's theme folder may predate it
if not okMirror then mirror = nil end

local T = {}

-- ---- the grid ---------------------------------------------------------------------------------------------------------------------------

T.COLS, T.ROWS = 64, 24
T.CELL_W, T.CELL_H = 10, 20
T.WIDTH, T.HEIGHT = T.COLS * T.CELL_W, T.ROWS * T.CELL_H -- 640 x 480
T.TOP_ROW = 0                                              -- reserved: the top bar
T.DISPLAY_TOP, T.DISPLAY_ROWS = 1, 20                      -- rows 1-20
T.COMMAND_TOP, T.COMMAND_ROWS = 21, 3                      -- rows 21-23: reply, prompt, status
assert(T.DISPLAY_TOP + T.DISPLAY_ROWS == T.COMMAND_TOP and T.COMMAND_TOP + T.COMMAND_ROWS == T.ROWS)

-- ---- colours: the 16 VGA text-mode colours ------------------------------------------------------------------------------------------

T.BLACK, T.BLUE, T.GREEN, T.CYAN, T.RED, T.MAGENTA, T.BROWN, T.LIGHT_GRAY = 0, 1, 2, 3, 4, 5, 6, 7
T.DARK_GRAY, T.LIGHT_BLUE, T.LIGHT_GREEN, T.LIGHT_CYAN, T.LIGHT_RED, T.LIGHT_MAGENTA, T.YELLOW, T.WHITE = 8, 9, 10, 11, 12, 13, 14, 15

T.RGB = {
  [0] = { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0xAA }, { 0x00, 0xAA, 0x00 }, { 0x00, 0xAA, 0xAA },
  { 0xAA, 0x00, 0x00 }, { 0xAA, 0x00, 0xAA }, { 0xAA, 0x55, 0x00 }, { 0xAA, 0xAA, 0xAA },
  { 0x55, 0x55, 0x55 }, { 0x55, 0x55, 0xFF }, { 0x55, 0xFF, 0x55 }, { 0x55, 0xFF, 0xFF },
  { 0xFF, 0x55, 0x55 }, { 0xFF, 0x55, 0xFF }, { 0xFF, 0xFF, 0x55 }, { 0xFF, 0xFF, 0xFF },
}

--- WCAG contrast ratio of two palette colours.
function T.contrast(fg, bg)
  local function lum(c)
    local out = 0
    for i, w in ipairs({ 0.2126, 0.7152, 0.0722 }) do
      local v = T.RGB[c][i] / 255
      out = out + w * (v <= 0.03928 and v / 12.92 or ((v + 0.055) / 1.055) ^ 2.4)
    end
    return out
  end
  local a, b = lum(fg), lum(bg)
  if a < b then a, b = b, a end
  return (a + 0.05) / (b + 0.05)
end

--- The pairs the theme itself uses. Every one passes 4.5:1 (tests/test_theme.lua checks). Apps use these
--- names; a new pair must pass too (docs/R3I6NOS-ANSIAPPS-THEME.md).
T.STYLE = {
  bar = { fg = T.WHITE, bg = T.BLUE },              -- the top bar
  body = { fg = T.LIGHT_GRAY, bg = T.BLACK },       -- ordinary text
  strong = { fg = T.WHITE, bg = T.BLACK },          -- titles, the line typed
  select = { fg = T.BLACK, bg = T.CYAN },           -- the highlighted row
  frame = { fg = T.LIGHT_GRAY, bg = T.BLACK },      -- box lines
  hint = { fg = T.LIGHT_CYAN, bg = T.BLACK },       -- keys, tips, the reply
  ok = { fg = T.LIGHT_GREEN, bg = T.BLACK },        -- the prompt character, good news
  warn = { fg = T.YELLOW, bg = T.BLACK },           -- caution
  error = { fg = T.LIGHT_RED, bg = T.BLACK },       -- a mistake (always also in words)
}

-- ---- code page 437 ----------------------------------------------------------------------------------------------------------------------

local REVERSE = {}
for i = 2, 256 do REVERSE[font.unicode[i]] = i - 1 end -- index 0 (NUL) is not a picture
REVERSE[0x3B2], REVERSE[0x3BC] = 225, 230 -- Greek beta and mu stand in for sharp s and micro

--- A UTF-8 string as a list of code page 437 indices. Tabs and other controls are spaces.
function T.decode(s)
  if type(s) == "table" then return s end
  local out, i, n = {}, 1, #s
  while i <= n do
    local b = s:byte(i)
    local cp, len = b, 1
    if b >= 0xC0 then
      len = b < 0xE0 and 2 or (b < 0xF0 and 3 or 4)
      cp = b - (len == 2 and 0xC0 or (len == 3 and 0xE0 or 0xF0))
      for k = 1, len - 1 do
        local c = s:byte(i + k)
        if not c or c < 0x80 or c > 0xBF then cp, len = 63, k; break end
        cp = cp * 64 + (c - 0x80)
      end
    elseif b >= 0x80 then cp = 63 end
    i = i + len
    if cp < 0x20 or cp == 0x7F then out[#out + 1] = 32
    elseif cp < 0x7F then out[#out + 1] = cp
    else out[#out + 1] = REVERSE[cp] or 63 end
  end
  return out
end

--- How many cells a string takes.
function T.width(s) return #T.decode(s) end

--- The string cut to at most max cells, ending in » when it was cut.
function T.fit(s, max)
  local g = T.decode(s)
  if #g <= max then return g end
  local out = {}
  for i = 1, math.max(0, max - 1) do out[i] = g[i] end
  if max > 0 then out[max] = 175 end
  return out
end

-- ---- the font: 8x16 scaled to 10x20 -----------------------------------------------------------------------------------------------

-- Nearest neighbour: dest pixel x takes source pixel floor(x * 8 / 10), y takes floor(y * 16 / 20). So source
-- columns 0 and 4 and rows 0, 4, 8 and 12 appear twice. The same map for every glyph keeps box lines and
-- blocks joined across cells.
T.COLMAP, T.ROWMAP = {}, {}
for x = 0, T.CELL_W - 1 do T.COLMAP[x] = math.floor(x * 8 / T.CELL_W) end
for y = 0, T.CELL_H - 1 do T.ROWMAP[y] = math.floor(y * 16 / T.CELL_H) end

--- The 8x16 bitmap of a character: 16 rows, each a list of 8 ints (0 or 1).
function T.glyph8(index)
  local hex, rows = font.glyphs[index + 1], {}
  for r = 0, 15 do
    local byte, row = tonumber(hex:sub(r * 2 + 1, r * 2 + 2), 16), {}
    for x = 0, 7 do row[x] = math.floor(byte / 2 ^ (7 - x)) % 2 end
    rows[r] = row
  end
  return rows
end

--- The 10x20 bitmap of a character: rows[0..19][0..9] of 0 or 1.
function T.glyph10x20(index)
  local src, out = T.glyph8(index), {}
  for y = 0, T.CELL_H - 1 do
    local row, s = {}, src[T.ROWMAP[y]]
    for x = 0, T.CELL_W - 1 do row[x] = s[T.COLMAP[x]] end
    out[y] = row
  end
  return out
end

--- FNV-1a over every pixel (0 or 1) of every 10x20 glyph, in the order ch, y, x. The C theme computes the same number
--- (r3_glyph_hash in r3i6nos_ansiapps.h), so the two implementations cannot drift apart unnoticed.
function T.glyphHash()
  local h = 2166136261
  for ch = 0, 255 do
    local g = T.glyph10x20(ch)
    for y = 0, T.CELL_H - 1 do
      for x = 0, T.CELL_W - 1 do
        if g[y][x] == 1 then h = h + ((h % 2 == 0) and 1 or -1) end -- xor with 1 flips bit 0
        h = ((h % 256) * 16777216 + h * 403) % 4294967296            -- times 16777619 (2^24 + 403) mod 2^32, exact in a double
      end
    end
  end
  return h
end

-- ---- the screen: 64 x 24 cells -----------------------------------------------------------------------------------------------------

local Screen = {}
Screen.__index = Screen

function T.screen()
  return setmetatable({ ch = {}, fg = {}, bg = {} }, Screen):clear()
end

local function at(col, row) return row * T.COLS + col + 1 end

--- Blank the whole screen (light gray on bg, default black).
function Screen:clear(bg)
  for i = 1, T.COLS * T.ROWS do self.ch[i], self.fg[i], self.bg[i] = 32, T.LIGHT_GRAY, bg or T.BLACK end
  return self
end

--- One cell. ch is an index (0-255); bg nil keeps the cell's background. Off-screen is ignored.
function Screen:put(col, row, ch, fg, bg)
  if col < 0 or col >= T.COLS or row < 0 or row >= T.ROWS then return end
  local i = at(col, row)
  self.ch[i], self.fg[i] = ch, fg or T.LIGHT_GRAY
  if bg then self.bg[i] = bg end
end

--- A string (or list of indices) from (col, row), clipped at the right edge. Returns the next column.
function Screen:text(col, row, s, fg, bg)
  for _, ch in ipairs(T.decode(s)) do
    if col >= T.COLS then break end
    self:put(col, row, ch, fg, bg)
    col = col + 1
  end
  return col
end

--- Text whose last cell is at col.
function Screen:textRight(col, row, s, fg, bg)
  local g = T.decode(s)
  return self:text(col - #g + 1, row, g, fg, bg)
end

function Screen:fill(col, row, w, h, ch, fg, bg)
  for r = row, row + h - 1 do for c = col, col + w - 1 do self:put(c, r, ch or 32, fg, bg) end end
end

local LINES = {
  single = { 218, 196, 191, 179, 192, 217 }, -- ┌ ─ ┐ │ └ ┘
  double = { 201, 205, 187, 186, 200, 188 }, -- ╔ ═ ╗ ║ ╚ ╝
}

--- A frame with its inside cleared. o: double (default true), fg, bg, title (set into the top edge),
--- right (also in the top edge, at the right: "Page 1/2"), foot (in the bottom edge), titleFg, footFg.
function Screen:box(col, row, w, h, o)
  o = o or {}
  local st, bg = T.STYLE, o.bg or T.BLACK
  local fg = o.fg or st.frame.fg
  local L = (o.double == false) and LINES.single or LINES.double
  self:fill(col, row, w, h, 32, fg, bg)
  for c = col + 1, col + w - 2 do self:put(c, row, L[2], fg, bg); self:put(c, row + h - 1, L[2], fg, bg) end
  for r = row + 1, row + h - 2 do self:put(col, r, L[4], fg, bg); self:put(col + w - 1, r, L[4], fg, bg) end
  self:put(col, row, L[1], fg, bg); self:put(col + w - 1, row, L[3], fg, bg)
  self:put(col, row + h - 1, L[5], fg, bg); self:put(col + w - 1, row + h - 1, L[6], fg, bg)
  local room = w - 6
  if o.title then
    local g = T.fit(o.title, room - 2 - (o.right and (T.width(o.right) + 1) or 0)) -- never runs into the right-hand text
    self:put(col + 2, row, 32, fg, bg)
    self:text(col + 3, row, g, o.titleFg or st.strong.fg, bg)
    self:put(col + 3 + #g, row, 32, fg, bg)
  end
  if o.right then
    local g = T.fit(o.right, room - 2)
    self:put(col + w - 3 - #g, row, 32, fg, bg)
    self:text(col + w - 2 - #g, row, g, o.titleFg or st.strong.fg, bg)
    self:put(col + w - 2, row, 32, fg, bg)
  end
  if o.foot then
    local g = T.fit(o.foot, w - 6)
    self:put(col + 2, row + h - 1, 32, fg, bg)
    self:text(col + 3, row + h - 1, g, o.footFg or st.hint.fg, bg)
    self:put(col + 3 + #g, row + h - 1, 32, fg, bg)
  end
  return self
end

--- The screen as 24 lines of text, for tests and logs (indices back to Unicode; pictures as "?").
function Screen:dump()
  local lines = {}
  for r = 0, T.ROWS - 1 do
    local line = {}
    for c = 0, T.COLS - 1 do
      local ch = self.ch[at(c, r)]
      line[#line + 1] = (ch >= 32 and ch < 127) and string.char(ch) or (ch == 0 and " " or "?")
    end
    lines[#lines + 1] = table.concat(line)
  end
  return lines
end

-- ---- the three windows --------------------------------------------------------------------------------------------------------------

--- Row 0: white on blue. left from column 1; right ends at column 62.
function T.topBar(scr, left, right)
  local s = T.STYLE.bar
  scr:fill(0, T.TOP_ROW, T.COLS, 1, 32, s.fg, s.bg)
  if left then scr:text(1, T.TOP_ROW, T.fit(left, T.COLS - 2), s.fg, s.bg) end
  if right then scr:textRight(T.COLS - 2, T.TOP_ROW, T.fit(right, T.COLS - 2), s.fg, s.bg) end
end

--- Rows 21-23, as the KlamANSI command line. o:
---   reply  row 21: the answer to the last line (kind: "info" (default), "ok", "warn", "error"); cut with » when long
---   mode   the mode character on row 22 (default ">"), line the text after it, caret true for a block caret
---   left, right  row 23: the status
function T.commandLine(scr, o)
  local st = T.STYLE
  local top = T.COMMAND_TOP
  scr:fill(0, top, T.COLS, T.COMMAND_ROWS, 32, st.body.fg, st.body.bg)
  if o.reply then
    local s = st[o.kind or "hint"] or st.hint
    scr:text(1, top, T.fit(o.reply, T.COLS - 2), s.fg, s.bg)
  end
  scr:put(0, top + 1, T.decode(o.mode or ">")[1], st.ok.fg, st.ok.bg)
  local x = scr:text(2, top + 1, T.fit(o.line or "", T.COLS - 4), st.strong.fg, st.strong.bg)
  if o.caret then scr:put(x, top + 1, 219, st.strong.fg, st.strong.bg) end
  local right = o.right and T.decode(o.right) or {}
  if o.left then scr:text(1, top + 2, T.fit(o.left, T.COLS - 3 - #right), st.body.fg, st.body.bg) end
  if o.right then scr:textRight(T.COLS - 2, top + 2, right, st.hint.fg, st.hint.bg) end
end

-- ---- list dialogs -----------------------------------------------------------------------------------------------------------------

--- Whole pages: for n items, per page, the selected item (1-based): page, pages, first, last.
function T.listWindow(n, sel, per)
  local pages = math.max(1, math.ceil(n / per))
  local page = math.max(1, math.min(pages, math.ceil(math.max(sel, 1) / per)))
  local first = (page - 1) * per + 1
  return page, pages, first, math.min(n, first + per - 1)
end

--- Rows of items a list dialog of height h shows.
function T.listRows(h) return h - 2 end

--- The standard list dialog: a double-line box, the title and Page x/y in the top edge, one row an item (label at the left,
--- value at the right, the selected row black on cyan), a foot of keys in the bottom edge.
--- o: col, row, w, h, title, items = { { label, value }... }, sel (1-based), foot, empty (lines shown when no items).
function T.listDialog(scr, o)
  local st = T.STYLE
  local per = T.listRows(o.h)
  local items = o.items or {}
  local page, pages, first, last = T.listWindow(#items, o.sel or 1, per)
  scr:box(o.col, o.row, o.w, o.h, { title = o.title, right = pages > 1 and ("Page %d/%d"):format(page, pages) or nil, foot = o.foot })  -- "Page x/y" only when there is more than one page
  if #items == 0 then
    for i, line in ipairs(o.empty or {}) do scr:text(o.col + 2, o.row + i, T.fit(line, o.w - 4), st.body.fg, st.body.bg) end
    return page, pages
  end
  for i = first, last do
    local item = items[i]
    local y = o.row + 1 + (i - first)
    local s = (i == o.sel) and st.select or st.body
    scr:fill(o.col + 1, y, o.w - 2, 1, 32, s.fg, s.bg)
    local value = item.value and T.decode(item.value) or {}
    scr:text(o.col + 2, y, T.fit(item.label, o.w - 5 - #value - (#value > 0 and 1 or 0)), s.fg, s.bg)
    if #value > 0 then scr:textRight(o.col + o.w - 3, y, value, s.fg, s.bg) end
  end
  return page, pages
end

--- The rule every list dialog follows. Returns the new selection and an event: "accept" (Enter), "back" (Esc) or "more"
--- (More..., ContextMenu), or nil. Up and Down wrap; left/pageup and right/pagedown turn a page; home and end jump.
function T.listKey(sel, n, per, key)
  local event
  if key == "accept" or key == "back" or key == "more" then event = key end
  if n == 0 then return 0, (key ~= "accept") and event or nil end
  if key == "up" then sel = (sel - 2) % n + 1
  elseif key == "down" then sel = sel % n + 1
  elseif key == "pageup" or key == "left" then sel = math.max(1, sel - per)
  elseif key == "pagedown" or key == "right" then sel = math.min(n, sel + per)
  elseif key == "home" then sel = 1
  elseif key == "end" then sel = n end
  return sel, event
end

-- ---- input: the R36S's buttons and keys as list-dialog actions ---------------------------------------------------------------------

-- Raw joystick buttons as LÖVE counts them (docs/hardware-findings.md): the pad's SDL mapping does not match, so apps read raw buttons.
T.PAD = {
  [9] = "up", [10] = "down", [11] = "left", [12] = "right",
  [2] = "accept", [1] = "back",                 -- A, B
  [5] = "pageup", [6] = "pagedown",             -- L1, R1
  [7] = "home", [8] = "end",                    -- L2, R2
  [14] = "more",                                -- Start
  [3] = "x", [4] = "y", [13] = "select",        -- X, Y, Select: for apps that need them (a file manager tags with X)
}
T.KEYS = {
  up = "up", down = "down", left = "left", right = "right", pageup = "pageup", pagedown = "pagedown",
  home = "home", ["end"] = "end", ["return"] = "accept", z = "accept", escape = "back", x = "back",
  tab = "more", f1 = "more", s = "more",
}

-- ---- drawing (needs LÖVE) -----------------------------------------------------------------------------------------------------------

local gfx = { }

--- Build the glyph atlas and the canvas. Call once, from love.load.
function T.init()
  local aw, ah = 16 * T.CELL_W, 16 * T.CELL_H
  local data = love.image.newImageData(aw, ah)
  for i = 0, 255 do
    local ox, oy = (i % 16) * T.CELL_W, math.floor(i / 16) * T.CELL_H
    local g = T.glyph10x20(i)
    for y = 0, T.CELL_H - 1 do
      for x = 0, T.CELL_W - 1 do
        if g[y][x] == 1 then data:setPixel(ox + x, oy + y, 1, 1, 1, 1) end
      end
    end
  end
  gfx.image = love.graphics.newImage(data)
  gfx.image:setFilter("nearest", "nearest")
  gfx.quads = {}
  for i = 0, 255 do
    gfx.quads[i] = love.graphics.newQuad((i % 16) * T.CELL_W, math.floor(i / 16) * T.CELL_H, T.CELL_W, T.CELL_H, aw, ah)
  end
  gfx.batch = love.graphics.newSpriteBatch(gfx.image, T.COLS * T.ROWS, "stream")
  gfx.canvas = love.graphics.newCanvas(T.WIDTH, T.HEIGHT)
  gfx.canvas:setFilter("nearest", "nearest")
  gfx.prev = nil
end

local function same(scr, prev)
  if not prev then return false end
  for i = 1, T.COLS * T.ROWS do
    if scr.ch[i] ~= prev.ch[i] or scr.fg[i] ~= prev.fg[i] or scr.bg[i] ~= prev.bg[i] then return false end
  end
  return true
end

local function render(scr)
  love.graphics.setCanvas(gfx.canvas)
  love.graphics.clear(0, 0, 0, 1)
  love.graphics.setBlendMode("alpha")
  for row = 0, T.ROWS - 1 do -- backgrounds, one rectangle for each run of the same colour
    local col = 0
    while col < T.COLS do
      local c = scr.bg[at(col, row)]
      local run = 1
      while col + run < T.COLS and scr.bg[at(col + run, row)] == c do run = run + 1 end
      if c ~= T.BLACK then
        local rgb = T.RGB[c]
        love.graphics.setColor(rgb[1] / 255, rgb[2] / 255, rgb[3] / 255, 1)
        love.graphics.rectangle("fill", col * T.CELL_W, row * T.CELL_H, run * T.CELL_W, T.CELL_H)
      end
      col = col + run
    end
  end
  gfx.batch:clear()
  for row = 0, T.ROWS - 1 do
    for col = 0, T.COLS - 1 do
      local i = at(col, row)
      local ch = scr.ch[i]
      if ch ~= 32 and ch ~= 0 and ch ~= 255 then
        local rgb = T.RGB[scr.fg[i]]
        gfx.batch:setColor(rgb[1] / 255, rgb[2] / 255, rgb[3] / 255, 1)
        gfx.batch:add(gfx.quads[ch], col * T.CELL_W, row * T.CELL_H)
      end
    end
  end
  love.graphics.setColor(1, 1, 1, 1)
  love.graphics.draw(gfx.batch)
  love.graphics.setCanvas()
end

--- Draw the screen to the window: 1:1 on the R36S's 640x480, a whole-number scale (or the best fit under 1x) centred elsewhere.
--- The canvas is redrawn only when a cell changed.
function T.draw(scr)
  if not same(scr, gfx.prev) then
    render(scr)
    if mirror then mirror.tap(scr, gfx.prev) end -- a mirror, when one is on, gets the cells that changed (docs/SCREEN-MIRROR.md)
    gfx.prev = gfx.prev or { ch = {}, fg = {}, bg = {} }
    for i = 1, T.COLS * T.ROWS do gfx.prev.ch[i], gfx.prev.fg[i], gfx.prev.bg[i] = scr.ch[i], scr.fg[i], scr.bg[i] end
  end
  local w, h = love.graphics.getDimensions()
  local s = math.min(w / T.WIDTH, h / T.HEIGHT)
  if s >= 1 then s = math.floor(s) end
  love.graphics.clear(0, 0, 0, 1)
  love.graphics.setColor(1, 1, 1, 1)
  love.graphics.draw(gfx.canvas, math.floor((w - T.WIDTH * s) / 2), math.floor((h - T.HEIGHT * s) / 2), 0, s, s)
end

return T
