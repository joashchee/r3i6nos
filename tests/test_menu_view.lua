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
-- the network: wired "ë" (CP437 137), Wi-Fi "¥" (157) and three cells, left of the battery, never over it
view.render(scr, { entries = entries, selected = 1, dialog = dialog, battery = { pct = 62, status = "Discharging" }, net = { wifi = true, cells = 2 } })
rows = scr:dump()
local pos = 51 -- "BAT █░░░░ 62%" is 13 cells ending at the second to last column, so it starts at cell 51 (1-based)
local want = { 157, 219, 219, 176 } -- ¥ █ █ ░
for i, ch in ipairs(want) do eq(scr.ch[pos - 6 + i], ch, "Wi-Fi mark and bars just before BAT, cell " .. i) end
has(rows[1], "BAT")
view.render(scr, { entries = entries, selected = 1, dialog = dialog, net = { wired = true } })
assert(scr.ch[64 - 1] == 137, "no battery: the wired mark ends at the battery's place, got " .. tostring(scr.ch[63]))
view.render(scr, { entries = entries, selected = 1, dialog = dialog })
rows = scr:dump()
-- ANSIapps Mono on (the menu turns it on at start): the battery and the network are status glyphs, the text stand-ins are gone
do
  eq(theme.useMono(true), true)
  local function top(state) state.entries, state.selected, state.dialog = entries, 1, dialog; view.render(scr, state); return scr end
  top({ battery = { pct = 62, status = "Discharging" } })
  local bat3 = theme.glyph("sys.bat.3")[1]
  eq(scr.ch[64 - 5], bat3, "62% is three quarters: sys.bat.3, then a space and 62%")
  assert(scr:dump()[1]:find("62%%"), "the percent follows")
  eq(scr.bg[64 - 3], theme.BLUE, "a normal battery keeps the bar's blue")
  top({ battery = { pct = 9, status = "Discharging" } })
  eq(scr.bg[64 - 3], theme.RED, "low: red"); eq(scr.ch[64 - 4], theme.glyph("sys.bat.1")[1], "9% is one quarter")
  top({ battery = { pct = 9, status = "Charging" } })
  eq(scr.bg[64 - 3], theme.BLUE, "charging is not low"); eq(scr.fg[64 - 3], theme.LIGHT_GREEN)
  eq(scr.ch[64 - 5], theme.glyph("sys.bat.charge")[1], "a bolt before the battery while it charges")
  eq(scr.ch[64 - 4], theme.glyph("sys.bat.1")[1])
  top({ battery = { pct = 100, status = "Full" } })
  eq(scr.ch[64 - 6], theme.glyph("sys.bat.4")[1], "full: four quarters"); eq(scr.bg[64 - 3], theme.BLUE)
  top({})
  assert(not scr:dump()[1]:find("%%"), "no battery, no indicator")
  top({ battery = { pct = 62, status = "Discharging" }, net = { wifi = true, cells = 2 } })
  local wifi = theme.glyph("sys.wifi.2")[1]
  eq(scr.ch[64 - 7], wifi, "Wi-Fi: sys.wifi.2 just before the battery, a space between")
  eq(scr.ch[64 - 6], 32)
  top({ battery = { pct = 62, status = "Discharging" }, net = { wired = true, wifi = true, cells = 3 } })
  eq(scr.ch[64 - 9], 137, "wired: the CP437 mark, then the Wi-Fi glyph"); eq(scr.ch[64 - 7], theme.glyph("sys.wifi.3")[1])
  top({ net = { wired = true } })
  eq(scr.ch[64 - 1], 137, "no battery: the wired mark ends at the battery's place")
  theme.useMono(false)
  view.render(scr, { entries = entries, selected = 1, dialog = dialog, battery = { pct = 62, status = "Discharging" } })
  assert(scr:dump()[1]:match("BAT .+ 62%%%s$"), "mono off: the text stand-in is back")
  view.render(scr, { entries = entries, selected = 1, dialog = dialog })
  rows = scr:dump()
end
-- the list: rows 1-20, 18 games a page, the page in the top edge (the dump shows non-ASCII as ?)
eq(view.PER_PAGE, 18)
has(rows[2], "Games and apps"); has(rows[2], "Page 1/3")
has(rows[3], "Game 01"); has(rows[3], "L?VE 11.5"); has(rows[5], "native")
has(rows[20], "Game 18"); assert(not rows[20]:find("Game 19", 1, true), "18 a page")
has(rows[21], "Y Menu")
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
has(joined, "Main menu"); has(joined, "About"); has(joined, "Colour theme"); has(joined, "Keymap"); has(joined, "Device report"); has(joined, "Rec demo"); has(joined, "Quit"); has(joined, "A Choose  B Back")
has(rows[22], "Version, updates"); has(rows[23], "> main menu")
has(rows[1], "r3i6nos")
-- each item's hint on the command line, walking down the main menu from About
for _, hint in ipairs({ "one theme so far", "input map", "Check this unit, for Claude", "Not built for the menu yet", "powers it off",
  "Wired and Wi-Fi", "off until you start it", "how much each app uses", "with a test sound", "Set the clock and time zone",
  "a file for Claude", "a video of an app", "Show this screen on a computer", "Format the card in the second slot" }) do
  settings.press(dialog, "down")
  view.render(scr, { entries = entries, selected = 1, dialog = dialog })
  has(scr:dump()[22], hint)
end
settings.press(dialog, "down"); settings.press(dialog, "down") -- the list wraps to About, then Colour theme
settings.press(dialog, "down"); settings.press(dialog, "down"); settings.press(dialog, "down"); settings.press(dialog, "down") -- Keymap, Device report, Rec demo, Quit
view.render(scr, { entries = entries, selected = 1, dialog = dialog })
has(scr:dump()[22], "powers it off")
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
has(table.concat(rows, "\n"), "Network"); has(table.concat(rows, "\n"), "Wired setup"); has(table.concat(rows, "\n"), "Test connection"); has(rows[23], "> main menu > network")
snap("network menu")
pick("Status"); rows = draw()                                   -- waiting
has(table.concat(rows, "\n"), "Waiting for the network helper"); has(table.concat(rows, "\n"), "B Stop waiting"); has(rows[22], "Working")
has(table.concat(rows, "\n"), "does not say how far"); has(table.concat(rows, "\n"), "0 s")   -- no total: a moving indicator, the seconds and why (rule 18)
local first = draw(); settings.node(dialog).elapsed = 3; local later = draw()
assert(table.concat(first, "\n") ~= table.concat(later, "\n"), "the indicator moves with the seconds")
snap("busy")
settings.node(dialog).prog = { step = 4, steps = 6, doing = "Making the exFAT file system" }; settings.node(dialog).elapsed = 12
rows = draw(); has(table.concat(rows, "\n"), "step 4 of 6   12 s"); has(table.concat(rows, "\n"), "Making the exFAT file system")
snap("busy with steps")
settings.node(dialog).prog = nil; settings.node(dialog).elapsed = 0
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
  has(all, "Battery"); has(all, "Use by app"); has(all, "Clear history"); has(rows[23], "> main menu > battery")
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

-- About: its menu, the system's pages, the update wait (its own words, not the network helper's), the answer, the licences
do
  local ab = require("about")
  local fakeab = {
    WAIT = 45, CHECK_LOST = ab.CHECK_LOST, LICENCES = ab.LICENCES,
    info = function() return { "THIS BUILD", "Version      r3i6nos 0.1.0", "Commit       278a84f1c2 (2026-11-09)" } end,
    startCheck = function() return true end, poll = function() return nil end,
  }
  dialog = settings.new(io_, nil, nil, fakeab); settings.open(dialog)
  pick("About"); rows = draw(); has(table.concat(rows, "\n"), "Check for updates"); has(rows[23], "> main menu > about")
  snap("about menu")
  pick("This system"); rows = draw(); has(table.concat(rows, "\n"), "r3i6nos 0.1.0")
  snap("about system")
  settings.press(dialog, "back"); pick("Check for updates"); rows = draw()
  has(table.concat(rows, "\n"), "Asking GitHub..."); assert(not table.concat(rows, "\n"):find("network helper", 1, true))
  snap("about checking")
  fakeab.poll = function() return { ok = true, lines = { "This is the latest release." } } end
  settings.tick(dialog, 1); rows = draw(); has(table.concat(rows, "\n"), "This is the latest release.")
  snap("about answer")
  settings.press(dialog, "back"); pick("Licences"); rows = draw(); has(table.concat(rows, "\n"), "MIT licence")
  snap("about licences")
  settings.press(dialog, "pagedown"); rows = draw(); has(table.concat(rows, "\n"), "SIL Open Font License 1.1")
  snap("about licences 2")
end

-- Device reports: the wait shows what it is doing, a bar and the steps; a report that says no steps still shows the time
do
  local prog = { step = 2, steps = 5, doing = "the system card: speed (16 MB)", file = "/roms/r3i6nos/r36s-report.txt" }
  local fakerep = {
    WAIT = 900, ABOUT = { "about" }, NONE = { "none" },
    list = function() return { { cmd = "r3i6nos-report", tool = "r3i6nos", app = "r3i6nos (this system)", system = true } } end,
    running = function() return nil end, start = function() return true end,
    poll = function() return prog end, summary = function() return nil end, doneLines = function() return { "done" }, true end,
  }
  dialog = settings.new(io_, nil, fakerep); settings.open(dialog)
  pick("Device reports"); pick("r3i6nos (this system)"); settings.tick(dialog, 12)
  rows = draw(); local j = table.concat(rows, "\n")
  has(j, "Checking this unit..."); has(j, "the system card: speed (16 MB)"); has(j, "step 2 of 5   12 s"); has(j, "B Back (it keeps running)")
  local steprow
  for r, line in ipairs(rows) do if line:find("step 2 of 5", 1, true) then steprow = r end end
  local full, empty = 0, 0
  for c = 1, 64 do
    local ch = scr.ch[(steprow - 2) * 64 + c]
    if ch == 219 then full = full + 1 elseif ch == 176 then empty = empty + 1 end
  end
  assert(full == math.floor(44 * 2 / 5 + 0.5) and full + empty == 44, ("the bar is 2/5 full: %d full, %d empty"):format(full, empty))
  snap("report wait")
  prog = { doing = nil }
  settings.tick(dialog, 1); rows = draw(); j = table.concat(rows, "\n")
  has(j, "This one does not say its steps."); has(j, "13 s")      -- a moving indicator and the seconds (rule 18)
  snap("report wait, no steps")
end

-- the setup dialog of a report that needs one: its lines, and the keys in the foot (A Ready, B Cancel)
do
  local fakerep = { WAIT = 10, ABOUT = { "about" }, NONE = { "none" }, running = function() return nil end,
    list = function() return { { cmd = "r3i6nos-report", app = "r3i6nos (this system)", system = true,
      needs = { "Run this once for each setup:", "", " 1. nothing in the USB-C (OTG) port, charger out", " 3. a plain USB cable from the port to a computer" } } } end }
  dialog = settings.new(io_, nil, fakerep); settings.open(dialog)
  pick("Device reports"); pick("r3i6nos (this system)"); rows = draw()
  local j = table.concat(rows, "\n")
  has(j, "r3i6nos (this system) report: set up"); has(j, "Set this up first, then press A:"); has(j, "1. nothing in the USB-C (OTG) port, charger out")
  has(j, "A Ready  B Cancel")
  snap("report setup dialog")
end

-- the setup dialog of a report to run several times: the count in the title and the Y key in the foot
do
  local fakerep = { WAIT = 10, ABOUT = { "about" }, NONE = { "none" }, running = function() return nil end,
    nextRun = function() return 2 end, currentRun = function() return 1 end,
    list = function() return { { cmd = "r3i6nos-report", app = "r3i6nos (this system)", system = true, runs = 3, runtext = { { "one" }, { "the adapter in the port" }, { "three" } } } } end }
  dialog = settings.new(io_, nil, fakerep); settings.open(dialog)
  pick("Device reports"); rows = draw()
  has(table.concat(rows, "\n"), "run 2 of 3")
  pick("r3i6nos (this system)"); rows = draw()
  local j = table.concat(rows, "\n")
  has(j, "run 2 of 3"); has(j, "the adapter in the port"); has(j, "A Ready  Y Other run  B Cancel")
  snap("report setup dialog, run 2 of 3")
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

-- the start-up screen: up before the card is scanned, with a progress bar and the step
do
  local sp = theme.screen()
  view.splash(sp, 1, 3, "Looking for games and apps")
  local r = sp:dump()
  eq(#r, 24)
  has(r[1], "r3i6nos")
  local text = table.concat(r, "\n")
  has(text, "Looking for games and apps"); has(text, "Starting")
  local lit = select(2, text:gsub("\219", "")) -- the dump may show the bar as ?; count either form
  assert(text:find("[\219?]"), "a progress bar is drawn")
  has(text, "unknown", "no release file: the version line says unknown, never nothing")
  view.splash(sp, 3, 3, "Ready", { VERSION = "0.4.2", COMMIT = ("a1b2c3d4e5"):rep(4), COMMIT_DATE = "2026-11-24" })
  local ready = table.concat(sp:dump(), "\n")
  has(ready, "Ready"); has(ready, "v0.4.2 a1b2c3d4e5 2026-11-24") -- rule 12: the version, the commit and the date
  eq(view.buildLine({ VERSION = "1", COMMIT = "unknown", BUILT = "2026-01-02", MODIFIED = "1" }), "v1 unknown 2026-01-02 +changes")
end
-- rule 18: an install over SSH shows its bar on the command line
view.render(scr, { entries = entries, selected = 1, dialog = settings.new(), install = { step = 1, steps = 4, doing = "Installing pyxel" } })
local last = scr:dump()
local found = false
for _, r in ipairs(last) do if r:find("Installing pyxel", 1, true) and r:find("[##", 1, true) and r:find("1 of 4", 1, true) then found = true end end
assert(found, "the install bar is on the command line")

-- the Keymap's help and test pages draw, in the same style, and the test shows what the reader holds
do
  local input = require("r3i6nos_input")
  local d = settings.new()
  d.input = input.new()
  settings.open(d)
  local function pick(label) local n = settings.node(d); for i, it in ipairs(n.items) do if it.label == label then n.sel = i; return settings.press(d, "accept") end end error(label) end
  pick("Keymap"); pick("Help")
  view.render(scr, { entries = entries, selected = 1, dialog = d })
  local help = table.concat(scr:dump(), "\n")
  has(help, "Keymap help"); has(help, "Page 1/3"); has(help, "L1 + R1")
  settings.press(d, "back"); pick("Test")
  input.button(d.input, input.BTN.R1, true)
  settings.press(d, "up")
  view.render(scr, { entries = entries, selected = 1, dialog = d })
  local test = table.concat(scr:dump(), "\n")
  has(test, "Keymap test"); has(test, "Held:    R1"); has(test, "Up"); has(test, "B or L1+R1 Back")
end

print("splash ok")
