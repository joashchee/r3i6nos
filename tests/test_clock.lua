-- Run with: luajit tests/test_clock.lua   (from the repo root)
-- The Date and time dialog (menu/clock.lua): its fields, the first-boot question, the request; then Settings driving it against a fake
-- clock helper, and the dialog as drawn (docs/CLOCK.md).
package.path = "menu/?.lua;theme/?.lua;" .. package.path

local tmp = (os.getenv("TMPDIR") or "/tmp"):gsub("/$", "") .. ("/r3i6nos-clock-%d"):format(os.time())
assert(os.execute(("mkdir -p '%s/state' '%s/etc'"):format(tmp, tmp)))
local clock = require("clock")
clock.STATE, clock.ASKED, clock.ETC = tmp .. "/state", tmp .. "/asked", tmp .. "/etc"

local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end
local function write(p, t) local f = assert(io.open(p, "w")); f:write(t); f:close() end

-- days and leap years
eq(clock.daysIn(2026, 2), 28); eq(clock.daysIn(2028, 2), 29); eq(clock.daysIn(2100, 2), 28); eq(clock.daysIn(2000, 2), 29); eq(clock.daysIn(2026, 11), 30)

-- a clock that was never set (1970) starts at the first allowed year; the day follows the month
local c = clock.new({ year = 1970, month = 1, day = 31, hour = 0, min = 0 }, 0)
eq(c.year, 2024, "an unset clock is lifted into range")
c = clock.new({ year = 2026, month = 11, day = 9, hour = 21, min = 30 }, 8)
eq(table.concat(clock.parts(c), " "), "2026 11 09 21 30 UTC+8")
local r = clock.request(c); eq(r.set, "2026-11-09 21:30"); eq(r.zone, "+8")
-- left/right choose a field and wrap; up/down change it and wrap in range
eq(c.field, 1); clock.press(c, "left"); eq(c.field, 6, "left from the year wraps to the zone")
clock.press(c, "up"); clock.press(c, "up"); clock.press(c, "up"); clock.press(c, "up"); clock.press(c, "up"); clock.press(c, "up"); clock.press(c, "up")
eq(c.zone, -12, "the zone wraps from +14 to -12"); eq(clock.zoneText(c.zone), "UTC-12"); eq(clock.zoneText(0), "UTC")
eq(clock.request(c).zone, "-12")
c = clock.new({ year = 2026, month = 1, day = 31, hour = 23, min = 59 }, 0)
clock.press(c, "right"); clock.press(c, "up"); eq(c.month, 2); eq(c.day, 28, "31 January to February: the day follows")
clock.press(c, "right"); clock.press(c, "up"); eq(c.day, 1, "the day wraps at the month's end")
clock.press(c, "right"); clock.press(c, "up"); eq(c.hour, 0, "23 wraps to 0")
clock.press(c, "right"); clock.press(c, "down"); eq(c.min, 58)
clock.press(c, "pagedown"); eq(c.min, 8, "R1: ten minutes, wrapping")
eq(clock.press(c, "accept"), "set"); eq(clock.press(c, "back"), "cancel"); eq(clock.press(c, "x"), nil)

-- the zone this system is in
eq(clock.currentZone(), 0, "no /etc/timezone: UTC")
write(tmp .. "/etc/timezone", "Etc/GMT-8\n"); eq(clock.currentZone(), 8, "Etc/GMT-8 is UTC+8")
write(tmp .. "/etc/timezone", "Etc/GMT+5\n"); eq(clock.currentZone(), -5)
write(tmp .. "/etc/timezone", "Asia/Singapore\n"); eq(clock.currentZone(), 0, "a named zone: not one this dialog sets")

-- the first-boot question: until a person sets the clock (the helper's mark) or says Not now (the menu's mark)
assert(clock.needed(), "a fresh card asks")
clock.markAsked(); assert(not clock.needed(), "Not now is remembered"); os.remove(clock.ASKED)
write(clock.STATE .. "/clock-set", "2026-11-09 21:30:00 +0800\n"); assert(not clock.needed(), "set: never asked again")
os.remove(clock.STATE .. "/clock-set")

-- ---- Settings with a fake helper
local settings = require("settings")
local asked, answer = {}, nil
clock.client = {
  request = function(verb, args) asked[#asked + 1] = { verb = verb, args = args }; return "id" .. #asked end,
  poll = function(id) return answer and answer.id == id and answer or nil end,
}
local s = settings.new({})
settings.openClock(s)
eq(settings.node(s).kind, "clock"); assert(settings.node(s).first)
settings.press(s, "back")
assert(not settings.isOpen(s), "B on the first-boot question closes it"); assert(not clock.needed(), "and it is not asked again")
os.remove(clock.ASKED)

settings.openClock(s)
settings.node(s).c = clock.new({ year = 2026, month = 11, day = 9, hour = 21, min = 30 }, 8)
settings.press(s, "accept")
eq(#asked, 1); eq(asked[1].args.set, "2026-11-09 21:30"); eq(asked[1].args.zone, "+8")
eq(settings.node(s).kind, "busy")
settings.tick(s, 0.1); eq(settings.node(s).kind, "busy", "waiting for the helper")
answer = { id = "id1", ok = true, lines = { "The clock is now 2026-11-09 21:30 (UTC+8)." } }
settings.tick(s, 0.1)
eq(settings.node(s).kind, "pages"); assert(not settings.node(s).bad)
settings.press(s, "back"); assert(not settings.isOpen(s), "the answer, then back to the games")
assert(clock.needed(), "the menu leaves the set mark to the helper (here: none written)")

-- from Settings, B goes back to the list and marks nothing; a helper that never answers says so
settings.open(s)
local n = settings.node(s)
for i, it in ipairs(n.items) do if it.label == "Date and time" then n.sel = i end end
settings.press(s, "accept"); eq(settings.node(s).title, "Date and time"); assert(not settings.node(s).first)
settings.press(s, "back"); eq(settings.node(s).title, "Settings"); assert(clock.needed(), "B in Settings is not Not now")
settings.press(s, "accept"); settings.press(s, "accept")
settings.tick(s, clock.WAIT + 1)
eq(settings.node(s).kind, "pages"); assert(settings.node(s).bad, "no answer: shown as a failure")

-- ---- the dialog as drawn
local theme = require("r3i6nos_ansiapps")
local view = require("view")
local scr = theme.screen()
settings.openClock(s)
settings.node(s).c = clock.new({ year = 2026, month = 11, day = 9, hour = 21, min = 30 }, 8)
view.render(scr, { entries = {}, selected = 1, dialog = s })
local text = table.concat(scr:dump(), "\n")
for _, want in ipairs({ "Set the date and time", "2026-11-09  21:30   UTC+8", "A Set  B Not now", "does not keep the time while it is off",
                        "Changing the year" }) do
  assert(text:find(want, 1, true), ("%q not on screen:\n%s"):format(want, text))
end

os.execute(("rm -rf '%s'"):format(tmp))
print("clock: ok")
