-- Run with: luajit tests/test_battery.lua   (from the repo root)
-- menu/battery.lua: the live reading, the indicator, and the history's figures, on a fake /sys/class/power_supply and a fake history.
local tmp = os.getenv("TMPDIR") or "/tmp"
math.randomseed(os.time() + math.floor(os.clock() * 1e6))
local root = ("%s/r3i6nos-battery-%d"):format(tmp, math.random(1, 1e9))
os.execute('rm -rf "' .. root .. '" && mkdir -p "' .. root .. '/power/battery" "' .. root .. '/power/ac" "' .. root .. '/state"')
local function write(path, text) local f = assert(io.open(path, "w")); f:write(text); f:close() end
write(root .. "/power/ac/type", "Mains\n")
local bat = root .. "/power/battery/"
write(bat .. "type", "Battery\n"); write(bat .. "capacity", "87\n"); write(bat .. "status", "Discharging\n")
write(bat .. "voltage_now", "3912000\n"); write(bat .. "temp", "315\n")
write(bat .. "charge_full", "2880000\n"); write(bat .. "charge_full_design", "3200000\n"); write(bat .. "cycle_count", "42\n")

-- the env is read when the module loads
local real_getenv = os.getenv
os.getenv = function(k)
  if k == "R3I6NOS_POWER_SUPPLY" then return root .. "/power" end
  if k == "R3I6NOS_BATTERY_DIR" then return root .. "/state" end
  return real_getenv(k)
end
package.path = "menu/?.lua;" .. package.path
local B = require("battery")

local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end
local function has(s, t) assert(s:find(t, 1, true), ("%q not in %q"):format(t, s)) end

-- ---- the live reading
local r = B.read()
eq(r.pct, 87); eq(r.status, "Discharging"); eq(r.mv, 3912); eq(r.temp, 315); eq(r.cycles, 42); eq(B.health(r), 90, "2.88/3.2 Ah")
eq(B.find(), root .. "/power/battery", "the Battery supply, not the mains one")
write(bat .. "capacity", "250\n"); eq(B.read().pct, 100, "clamped")
write(bat .. "capacity", "87\n")
os.remove(bat .. "charge_full"); os.remove(bat .. "charge_full_design")
r = B.read(); eq(r.full, nil); eq(B.health(r), nil, "no pair, no health")
write(bat .. "energy_full", "9000\n"); write(bat .. "energy_full_design", "10000\n"); eq(B.health(B.read()), 90, "energy pair")
os.remove(bat .. "capacity"); eq(B.read(), nil, "no capacity, no reading")
write(bat .. "capacity", "87\n")

-- ---- the indicator
local function ind(pct, status) return B.indicator({ pct = pct, status = status }) end
local t, k = ind(87, "Discharging"); eq(t, "BAT █████ 87%"); eq(k, "normal")
t, k = ind(50, "Discharging"); eq(t, "BAT ███░░ 50%")
t, k = ind(15, "Discharging"); eq(k, "low"); eq(t, "BAT █░░░░ 15%")
t, k = ind(16, "Discharging"); eq(k, "normal")
t, k = ind(0, "Discharging"); eq(t, "BAT ░░░░░ 0%"); eq(k, "low")
t, k = ind(8, "Charging"); eq(k, "charging", "charging is never 'low'"); eq(t, "CHG █░░░░ 8%")
t, k = ind(100, "Full"); eq(t, "FULL █████ 100%"); eq(k, "charging")
t, k = ind(40, "Unknown"); eq(t, "BAT ██░░░ 40%")
eq(B.indicator(nil), nil, "no battery, no indicator")

-- ---- the history. Columns: v boot up epoch event detail pct status mV full design temp cycles
local function row(boot, up, event, detail, pct, status)
  return table.concat({ "1", boot, up, "1700000000", event, detail == "" and "-" or detail, pct, status, "3900", "2880", "3200", "-", "-" }, "\t")
end
local D = "Discharging"
local log = table.concat({
  row("aaaa", 10, "session", "", 100, D),
  row("aaaa", 20, "app-start", "Game A", 100, D),
  row("aaaa", 320, "tick", "Game A", 97, D),            -- 300 s, 3 %   -> Game A
  row("aaaa", 620, "tick", "Game A", 94, D),            -- 300 s, 3 %   -> Game A
  row("aaaa", 700, "app-end", "Game A", 93, D),         -- 80 s, 1 %    -> Game A
  row("aaaa", 1300, "app-start", "Game B", 91, D),      -- 600 s, 2 %   -> (menu)
  row("aaaa", 1900, "app-end", "Game B", 89, D),        -- 600 s, 2 %   -> Game B
  row("aaaa", 2000, "shutdown", "", 89, D),             -- 100 s, 0 %   -> (menu)
  row("bbbb", 5, "session", "", 60, "Charging"),
  row("bbbb", 400, "tick", "Game A", 70, "Charging"),   -- charging: not a discharge interval
  row("bbbb", 500, "app-end", "Game A", 72, D),         -- one end is charging: skipped
  row("cccc", 5, "session", "", 12, D),
  row("cccc", 200, "tick", "Game A", 9, D),             -- last sample of an unfinished boot
  "garbage line", "1\tx\n",
}, "\n") .. "\n"
local s = B.parse(log)
eq(#s, 13, "garbage and short lines are skipped")
eq(s[2].event, "app-start"); eq(s[2].detail, "Game A"); eq(s[1].detail, ""); eq(s[3].pct, 97); eq(s[3].up, 320)

local u = B.usage(s)
eq(u.apps["Game A"].drop, 7); eq(u.apps["Game A"].secs, 680)
eq(u.apps["Game B"].drop, 2); eq(u.apps["Game B"].secs, 600)
eq(u.apps["(menu)"].drop, 2 + 3, "idle in the menu counts as (menu); so does a boot with no app-start")
eq(u.all.drop, 7 + 2 + 5)
assert(B.rate(u.apps["Game A"]), "680 s is enough for a rate")
assert(math.abs(B.rate(u.apps["Game A"]) - 7 / (680 / 3600)) < 1e-9)
eq(B.rate(u.apps["Game B"]), 2 / (600 / 3600), "exactly the minimum counts")
eq(B.rate({ drop = 5, secs = 599 }), nil, "too short"); eq(B.rate({ drop = 0, secs = 5000 }), nil, "no drop, no rate"); eq(B.rate(nil), nil)

local starts, unclean, last = B.endings(s)
eq(starts, 3); eq(unclean, 1, "boot bbbb ended with no shutdown; the running boot is not counted"); eq(last.pct, 72)
eq(select(2, B.endings({})), 0)

-- the screens' words
local lines = B.statusLines(B.read(), s)
local all = table.concat(lines, "\n")
has(all, "87 %  (Discharging)"); has(all, "3.91 V"); has(all, "31.5 C"); has(all, "90 % of its design capacity"); has(all, "42")
has(all, "Average drain"); has(all, "Time left"); has(all, "3 starts"); has(all, "1 of 2"); has(all, "72 %")
for _, l in ipairs(lines) do assert(#l <= 60, "too wide: " .. l) end
local gone = B.statusLines(nil, {})
has(table.concat(gone, "\n"), "reports no battery"); has(table.concat(gone, "\n"), "not enough use recorded")
has(table.concat(B.statusLines({ pct = 5, status = "Charging" }, s), "\n"), "not reported by this battery")
assert(not table.concat(B.statusLines({ pct = 5, status = "Charging" }, s), "\n"):find("Time left", 1, true), "no time left while charging")
-- the last unclean boot ended low: say so
local flat = B.parse(row("a1", 5, "session", "", 50, D) .. "\n" .. row("a1", 900, "tick", "X", 3, D) .. "\n" .. row("b1", 5, "session", "", 90, D) .. "\n")
has(table.concat(B.statusLines(nil, flat), "\n"), "ran flat")

local apps = B.appLines(s)
has(apps[1], "App"); has(apps[3], "(menu)"); has(apps[4], "Game A"); has(apps[4], "11 min"); has(apps[4], "7%"); has(apps[4], "37.1 %/h")
has(apps[5], "Game B"); has(apps[5], "12.0 %/h")
for _, l in ipairs(apps) do assert(#l <= 60, "too wide: " .. l) end
has(B.appLines({})[1], "Nothing recorded")

local recent = B.recentLines(s, 4)
eq(#recent, 6, "header, blank, 4 newest"); has(recent[3], "tick"); has(recent[3], "Game A"); has(recent[3], "9%")
for _, l in ipairs(B.recentLines(s, 60)) do assert(#l <= 60, "too wide: " .. l) end
has(B.recentLines({})[1], "Nothing recorded")

eq(B.dur(40), "40 s"); eq(B.dur(60), "1 min"); eq(B.dur(3900), "1 h 05 min"); eq(B.dur(7200), "2 h 00 min")

-- ---- the file: read back, and clearing
write(B.LOG, log)
eq(#B.history(), 13)
assert(B.clear()); eq(#B.history(), 0); assert(B.clear(), "clearing nothing is fine")
os.execute('rm -rf "' .. root .. '"')
print("battery: ok")
