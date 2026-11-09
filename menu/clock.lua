-- The Date and time dialog (docs/CLOCK.md): its fields, the first-boot question, and the request to the root helper
-- (overlay/usr/local/bin/r3i6nos-clock), which sets the clock, the zone and the RTC and keeps the time across restarts.
-- Pure Lua but for the file checks, so luajit tests it (tests/test_clock.lua).
--
-- Overrides, for tests: R3I6NOS_CLOCK_STATE (/var/lib/r3i6nos), R3I6NOS_CLOCK_ASKED (~/.r3i6nos-clock-asked), R3I6NOS_ETC (/etc),
-- R3I6NOS_CLOCK_DIR (/run/loveos/clock), R3I6NOS_CLOCK_OUT (/run/r3i6nos-clock).

local netclient = require("netclient")

local M = {}

M.STATE = os.getenv("R3I6NOS_CLOCK_STATE") or "/var/lib/r3i6nos"
M.ASKED = os.getenv("R3I6NOS_CLOCK_ASKED") or ((os.getenv("HOME") or "/home/loveos") .. "/.r3i6nos-clock-asked")
M.ETC = os.getenv("R3I6NOS_ETC") or "/etc"
M.client = netclient.new(os.getenv("R3I6NOS_CLOCK_DIR") or "/run/loveos/clock", os.getenv("R3I6NOS_CLOCK_OUT") or "/run/r3i6nos-clock")
M.WAIT = 20

M.FIELDS = { "year", "month", "day", "hour", "min", "zone" }
M.RANGE = { year = { 2024, 2099 }, month = { 1, 12 }, hour = { 0, 23 }, min = { 0, 59 }, zone = { -12, 14 } }
M.STEP = { year = 1, month = 3, day = 7, hour = 6, min = 10, zone = 1 } -- L1/R1: a bigger step

local function exists(path)
  local f = io.open(path, "r")
  if f then f:close() return true end
  return false
end

function M.daysIn(y, m)
  local d = ({ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 })[m]
  if m == 2 and (y % 4 == 0 and (y % 100 ~= 0 or y % 400 == 0)) then d = 29 end
  return d
end

local function range(c, f)
  if f == "day" then return 1, M.daysIn(c.year, c.month) end
  return M.RANGE[f][1], M.RANGE[f][2]
end

--- The zone this system is in, from /etc/timezone (Etc/GMT-8 is UTC+8: the Etc zones count the other way); 0 for anything else.
function M.currentZone()
  local f = io.open(M.ETC .. "/timezone", "r")
  local name = f and f:read("*l") or ""
  if f then f:close() end
  local sign, n = name:match("^Etc/GMT([+-])(%d+)$")
  if not sign then return 0 end
  return (sign == "+" and -1 or 1) * tonumber(n)
end

--- A dialog's state from a date table (os.date("*t")) and a zone in hours. The year is kept in range (an unset clock says 1970).
function M.new(t, zone)
  t = t or os.date("*t")
  local c = { year = t.year, month = t.month, day = t.day, hour = t.hour, min = t.min, zone = zone or 0, field = 1 }
  c.year = math.max(M.RANGE.year[1], math.min(M.RANGE.year[2], c.year))
  c.day = math.min(c.day, M.daysIn(c.year, c.month))
  return c
end

--- One action: left/right choose a field (wrapping), up/down change it by one, pageup/pagedown by a bigger step (both wrap in range;
--- the day follows the month). Returns "set" (accept), "cancel" (back), or nil.
function M.press(c, action)
  local f = M.FIELDS[c.field]
  if action == "left" then c.field = (c.field - 2) % #M.FIELDS + 1
  elseif action == "right" then c.field = c.field % #M.FIELDS + 1
  elseif action == "up" or action == "down" or action == "pageup" or action == "pagedown" then
    local d = (action == "up" or action == "pagedown") and 1 or -1
    if action == "pageup" or action == "pagedown" then d = d * M.STEP[f] end
    local lo, hi = range(c, f)
    c[f] = (c[f] - lo + d) % (hi - lo + 1) + lo
    c.day = math.min(c.day, M.daysIn(c.year, c.month))
  elseif action == "accept" then return "set"
  elseif action == "back" then return "cancel" end
  return nil
end

function M.zoneText(z) return z == 0 and "UTC" or ("UTC%+d"):format(z) end

--- The pieces as the screen shows them, in field order.
function M.parts(c)
  return { ("%04d"):format(c.year), ("%02d"):format(c.month), ("%02d"):format(c.day), ("%02d"):format(c.hour), ("%02d"):format(c.min), M.zoneText(c.zone) }
end

--- What the helper is asked for.
function M.request(c)
  return { set = ("%04d-%02d-%02d %02d:%02d"):format(c.year, c.month, c.day, c.hour, c.min), zone = ("%+d"):format(c.zone) }
end

--- Ask on this card's first boot: until a person has set the clock (the helper's mark) or said "not now" (the menu's own mark).
function M.needed()
  return not exists(M.STATE .. "/clock-set") and not exists(M.ASKED)
end

--- "Not now" on the first-boot question: do not ask again (Settings > Date and time is still there).
function M.markAsked()
  local f = io.open(M.ASKED, "w")
  if f then f:write(os.date("%Y-%m-%d %H:%M:%S"), "\n"); f:close() end
end

return M
