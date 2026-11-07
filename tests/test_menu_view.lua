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
-- the battery sits at the top right and the bar's own colours hold
view.render(scr, { entries = entries, selected = 1, dialog = dialog, battery = { pct = 62, status = "Discharging" } })
rows = scr:dump()
assert(rows[1]:match("BAT .+ 62%%%s$"), "battery at the right edge of the top bar: " .. rows[1])
has(rows[1], "r3i6nos"); has(rows[1], "40 apps")
eq(scr.bg[64 - 3], theme.BLUE, "a normal battery keeps the bar's blue")
view.render(scr, { entries = entries, selected = 1, dialog = dialog, battery = { pct = 9, status = "Discharging" } })
eq(scr.bg[64 - 3], theme.RED, "low: red"); eq(scr.fg[64 - 3], theme.WHITE)
assert(theme.contrast(theme.WHITE, theme.RED) >= 4.5)
view.render(scr, { entries = entries, selected = 1, dialog = dialog, battery = { pct = 9, status = "Charging" } })
has(scr:dump()[1], "CHG"); eq(scr.bg[64 - 3], theme.BLUE, "charging is not low")
assert(theme.contrast(theme.LIGHT_GREEN, theme.BLUE) >= 4.5)
view.render(scr, { entries = entries, selected = 1, dialog = dialog })
assert(not scr:dump()[1]:find("BAT", 1, true), "no battery, no indicator")
view.render(scr, { entries = entries, selected = 1, dialog = dialog })
rows = scr:dump()
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
has(joined, "Settings"); has(joined, "Network"); has(joined, "SSH"); has(joined, "Shutdown"); has(joined, "Back"); has(joined, "A Choose  B Back")
has(rows[22], "Wired and Wi-Fi"); has(rows[23], "> settings")
has(rows[1], "r3i6nos")
settings.press(dialog, "down"); settings.press(dialog, "down"); settings.press(dialog, "down")
view.render(scr, { entries = entries, selected = 1, dialog = dialog })
has(scr:dump()[22], "Power the device off")
settings.press(dialog, "accept")
view.render(scr, { entries = entries, selected = 1, dialog = dialog })
rows = scr:dump()
joined = table.concat(rows, "\n")
has(joined, "Shut down the device?"); has(joined, "Yes, shut down"); has(joined, "No")
-- the highlighted row is "No" (the safe default)
local no_row
for r, line in ipairs(rows) do if line:find("No ", 1, true) and line:find("|", 1, true) == nil and r > 1 and r < 21 and scr.bg[(r - 1) * 64 + 24] == theme.CYAN then no_row = r end end
assert(no_row, "No is highlighted when the confirmation opens")

-- ---- every Network and SSH screen draws, in the same style, and passes the contrast rule
local io_ = { sent = {}, answers = {} }
function io_.request(verb, args) io_.sent[#io_.sent + 1] = { verb = verb, id = "i" .. #io_.sent + 1 }; return "i" .. #io_.sent end
function io_.poll(id) return io_.answers[id] end
local function pick(label)
  local n = settings.node(dialog)
  for i, it in ipairs(n.items) do if it.label == label then n.sel = i end end
  settings.press(dialog, "accept")
end
local function draw() view.render(scr, { entries = entries, selected = 1, dialog = dialog }); return scr:dump() end
local screens = {}
local function copy(t) local c = {} for i = 1, 64 * 24 do c[i] = t[i] end return c end
local function snap(name) screens[#screens + 1] = { name = name, rows = draw(), bg = copy(scr.bg), fg = copy(scr.fg), ch = copy(scr.ch) } end

dialog = settings.new(io_); settings.open(dialog)
pick("Network"); rows = draw()
has(table.concat(rows, "\n"), "Network"); has(table.concat(rows, "\n"), "Wired setup"); has(table.concat(rows, "\n"), "Test connection"); has(rows[23], "> settings > network")
snap("network menu")
pick("Status"); rows = draw()                                   -- waiting
has(table.concat(rows, "\n"), "Waiting for the network helper"); has(table.concat(rows, "\n"), "B Stop waiting"); has(rows[22], "Working")
snap("busy")
io_.answers.i1 = { ok = true, lines = { "eth0  wired  up", "  cable    plugged in", "[ok] looks like a test line", "[FAIL] and a failed one" }, nets = {} }
settings.tick(dialog, 1); rows = draw()
has(rows[3], "eth0  wired  up"); has(rows[5], "[ok]"); has(rows[6], "[FAIL]")
assert(not rows[2]:find("Page", 1, true), "one page shows no Page number")
snap("result page")
settings.press(dialog, "back")
pick("Instructions"); rows = draw()
has(rows[2], "Network help"); has(rows[2], "Page 1/4"); has(rows[3], "no network built in"); has(rows[21], "L1/R1 Page")
snap("instructions")
settings.press(dialog, "back")
pick("Wi-Fi setup")
io_.answers.i2 = { ok = true, lines = {}, nets = { { ssid = "Home", signal = "-40 dBm", security = "WPA" }, { ssid = "Guest", signal = "-70 dBm", security = "open" } } }
settings.tick(dialog, 1); rows = draw()
has(table.concat(rows, "\n"), "Home"); has(table.concat(rows, "\n"), "-40 dBm WPA"); has(table.concat(rows, "\n"), "Other network...")
snap("wifi list")
pick("Home"); settings.text(dialog, "my pass"); rows = draw()
has(rows[2], "Password for Home"); has(table.concat(rows, "\n"), "> my pass"); has(table.concat(rows, "\n"), "7/63"); has(table.concat(rows, "\n"), "Keys: abc")
has(table.concat(rows, "\n"), "q"); has(rows[21], "Start Done")
settings.press(dialog, "more"); rows = draw()                   -- too short? "my pass" is 7
has(table.concat(rows, "\n"), "At least 8")
snap("keyboard with an error")
settings.press(dialog, "select"); rows = draw()
has(table.concat(rows, "\n"), "> *******")
settings.press(dialog, "y"); rows = draw(); has(table.concat(rows, "\n"), "Keys: ABC"); has(table.concat(rows, "\n"), "Q   W   E")
settings.press(dialog, "y"); rows = draw(); has(table.concat(rows, "\n"), "Keys: #+=")
snap("keyboard symbols")

-- ---- Settings > Battery: the menu and its pages draw in the same style
do
  local bat = require("battery")
  local fake = {
    read = function() return { pct = 40, status = "Discharging", mv = 3800, full = 90, design = 100 } end,
    history = function() return {} end, clear = function() return true end,
    statusLines = bat.statusLines, appLines = bat.appLines, recentLines = bat.recentLines,
  }
  dialog = settings.new(io_, fake); settings.open(dialog)
  pick("Battery"); rows = draw()
  local all = table.concat(rows, "\n")
  has(all, "Battery"); has(all, "Use by app"); has(all, "Clear history"); has(rows[23], "> settings > battery")
  has(rows[22], "Charge, voltage, health")
  snap("battery menu")
  pick("Status"); rows = draw(); all = table.concat(rows, "\n")
  has(rows[2], "Battery"); has(all, "40 %  (Discharging)"); has(all, "3.80 V"); has(all, "90 % of its design capacity")
  snap("battery status")
  settings.press(dialog, "back"); pick("Use by app"); rows = draw(); has(table.concat(rows, "\n"), "Nothing recorded yet.")
  snap("battery apps")
  settings.press(dialog, "back"); pick("Clear history"); rows = draw(); has(table.concat(rows, "\n"), "Clear the battery history?")
  has(table.concat(rows, "\n"), "Yes, clear it"); has(rows[22], "Deletes only the recorded battery samples")
  snap("battery clear")
end

-- every cell is a code page 437 index, every colour a palette colour, every pair passes 4.5:1, on every screen above
for _, sc in ipairs(screens) do
  for i = 1, 64 * 24 do
    assert(sc.ch[i] >= 0 and sc.ch[i] <= 255 and theme.RGB[sc.fg[i]] and theme.RGB[sc.bg[i]], sc.name .. " cell " .. i)
    if sc.ch[i] ~= 32 and sc.ch[i] ~= 0 then
      assert(theme.contrast(sc.fg[i], sc.bg[i]) >= 4.5, ("%s: low contrast at cell %d"):format(sc.name, i))
    end
  end
end

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
