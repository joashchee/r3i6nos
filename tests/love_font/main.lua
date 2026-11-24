-- LOVE's own FreeType rasteriser against theme/font/r3i6nos-mono/r3i6nos_mono10x20.lua: every code point, at each hinting mode.
-- Run by tests/test_r3font_love.sh with R3F_DIR set to the font folder. Exit code 0 = the TTF draws exactly its source bitmaps in LOVE.
local utf8 = require("utf8")
local dir = os.getenv("R3F_DIR")
local tbl = dofile(dir .. "/r3i6nos_mono10x20.lua")
local W, H, BASE = tbl.width, tbl.height, tbl.base

local function rows_of(hex)
    local rows = {}
    for i = 0, H - 1 do rows[i] = tonumber(hex:sub(i * 3 + 1, i * 3 + 3), 16) end
    return rows
end

local function src_ink(hex)
    local rows = rows_of(hex)
    local x0, y0, x1, y1 = 99, 99, -1, -1
    for y = 0, H - 1 do
        for x = 0, W - 1 do
            if math.floor(rows[y] / 2 ^ (W - 1 - x)) % 2 == 1 then
                x0, y0, x1, y1 = math.min(x0, x), math.min(y0, y), math.max(x1, x), math.max(y1, y)
            end
        end
    end
    if x1 < 0 then return nil end
    local out = {}
    for y = y0, y1 do
        local line = {}
        for x = x0, x1 do line[#line + 1] = (math.floor(rows[y] / 2 ^ (W - 1 - x)) % 2 == 1) and 1 or 0 end
        out[#out + 1] = table.concat(line)
    end
    return { x0 = x0, y0 = y0, w = x1 - x0 + 1, h = y1 - y0 + 1, rows = out }
end

local function read(path)
    local f = assert(io.open(path, "rb")); local s = f:read("*a"); f:close(); return s
end

local fails, total = 0, 0
local ttf = love.filesystem.newFileData(read(dir .. "/R3i6nosMono-Regular.ttf"), "R3i6nosMono-Regular.ttf")
local cps = {}
for cp in pairs(tbl.cmap) do cps[#cps + 1] = cp end
table.sort(cps)

for _, hinting in ipairs({ "normal", "light", "mono", "none" }) do
    local r = love.font.newRasterizer(ttf, 20, hinting)
    local bad, checked = 0, 0
    local first
    if r:getHeight() ~= H then bad = bad + 1; first = first or ("height " .. r:getHeight()) end
    if r:getAscent() ~= BASE then bad = bad + 1; first = first or ("ascent " .. r:getAscent()) end
    for _, cp in ipairs(cps) do
        if cp ~= 0x20 and cp ~= 0xA0 and cp ~= 0 then
            local want = src_ink(tbl.glyphs[tbl.cmap[cp]])
            local ok = r:hasGlyphs(utf8.char(cp))
            local gd = ok and r:getGlyphData(cp)
            checked = checked + 1
            local good = ok and gd:getAdvance() == W
            if good and want then
                local bx, by = gd:getBearing()
                local gw, gh = gd:getWidth(), gd:getHeight()
                local px = gd:getString()
                local bpp = (#px) / (gw * gh)
                good = gw == want.w and gh == want.h
                -- LOVE's bearing is the glyph's offset from the pen: x to the left edge, y up to the top edge (from the baseline)
                if good then good = (bx == want.x0) and (by == BASE - want.y0) end
                if good then
                    for y = 0, gh - 1 do
                        for x = 0, gw - 1 do
                            local a = px:byte((y * gw + x) * bpp + bpp)
                            if ((a >= 128) and "1" or "0") ~= want.rows[y + 1]:sub(x + 1, x + 1) then good = false end
                        end
                    end
                end
            end
            if not good then
                bad = bad + 1
                first = first or string.format("U+%04X", cp)
            end
        end
    end
    print(string.format("hinting %-6s: %d code points checked, %d differ%s", hinting, checked, bad, first and (" (first: " .. first .. ")") or ""))
    total = total + 1
    if bad > 0 then fails = fails + 1 end
end

function love.load() end
function love.run()
    return function() return fails == 0 and 0 or 1 end
end
