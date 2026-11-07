-- The battery, for the launcher: the live reading (the indicator in the top bar) and the history that
-- overlay/usr/local/bin/r3i6nos-battery records (Settings > Battery). Plain Lua with io only (no LÖVE calls), so luajit can test it.
--
-- The history is one tab-separated line per sample (see that script): boot id, uptime, clock, event, detail, percent, status,
-- mV, full, design, deci-C, cycles. The clock can be wrong (no battery-backed clock), so everything here orders by boot and uptime.
-- Every figure that depends on what this battery reports is optional: the R36S's fuel gauge has never been read (untested on hardware).
-- Overrides, for tests: R3I6NOS_POWER_SUPPLY, R3I6NOS_BATTERY_DIR.

local B = {}

B.SUPPLY = os.getenv("R3I6NOS_POWER_SUPPLY") or "/sys/class/power_supply"
B.DIR = os.getenv("R3I6NOS_BATTERY_DIR") or "/var/lib/r3i6nos-battery"
B.LOG = B.DIR .. "/history.tsv"

--- A discharge interval shorter than this, or an app with less recorded time, gives no rate: whole percents are too coarse.
B.MIN_SECONDS = 600

local function slurp(path)
  local f = io.open(path, "r")
  if not f then return nil end
  local s = f:read("*a")
  f:close()
  return s and (s:gsub("%s+$", "")) or nil
end

local function num(path)
  return tonumber(slurp(path) or "")
end

local function ls(path)
  local out = {}
  local p = io.popen('ls -1 "' .. path:gsub('"', '\\"') .. '" 2>/dev/null')
  if not p then return out end
  for line in p:lines() do out[#out + 1] = line end
  p:close()
  return out
end

local cached
--- The battery's folder: the first supply of type Battery, else one whose name has "bat" in it or starts with bms. nil when there is none.
function B.find()
  if cached and slurp(cached .. "/capacity") then return cached end
  cached = nil
  local names = ls(B.SUPPLY)
  for _, n in ipairs(names) do
    if (slurp(B.SUPPLY .. "/" .. n .. "/type") or ""):lower() == "battery" then cached = B.SUPPLY .. "/" .. n; return cached end
  end
  for _, n in ipairs(names) do
    local l = n:lower()
    if l:find("bat", 1, true) or l:find("^bms") then cached = B.SUPPLY .. "/" .. n; return cached end
  end
  return nil
end

--- The live reading: { pct, status, mv, temp (deci-C), full, design, cycles }, or nil when no battery is reported.
function B.read()
  local d = B.find()
  if not d then return nil end
  local pct = num(d .. "/capacity")
  if not pct then return nil end
  local uv = num(d .. "/voltage_now")
  local full, design = num(d .. "/charge_full"), num(d .. "/charge_full_design")
  if not (full and design and full > 0 and design > 0) then
    full, design = num(d .. "/energy_full"), num(d .. "/energy_full_design")
    if not (full and design and full > 0 and design > 0) then full, design = nil, nil end
  end
  return {
    pct = math.max(0, math.min(100, math.floor(pct))), status = slurp(d .. "/status") or "Unknown",
    mv = uv and math.floor(uv / 1000) or nil, temp = num(d .. "/temp"), full = full, design = design, cycles = num(d .. "/cycle_count"),
  }
end

-- ---- the indicator ---------------------------------------------------------------------------------------------------------

B.LOW = 15 -- percent at or under which a discharging battery is shown as low

--- What the top bar shows at the right: the text, and "low" (discharging at or under B.LOW), "charging" or "normal". Five cells fill up, one per 20 %.
function B.indicator(r)
  if not r then return nil end
  local cells = math.min(5, math.ceil(r.pct / 20))
  local bar = string.rep("█", cells) .. string.rep("░", 5 - cells)
  local status = (r.status or ""):lower()
  if status == "charging" then return ("CHG %s %d%%"):format(bar, r.pct), "charging" end
  if status == "full" then return ("FULL %s %d%%"):format(bar, r.pct), "charging" end
  return ("BAT %s %d%%"):format(bar, r.pct), r.pct <= B.LOW and "low" or "normal"
end

-- ---- the history -----------------------------------------------------------------------------------------------------------

--- Parse the history's text into a list of samples, oldest first. Lines that do not parse are skipped.
function B.parse(text)
  local out = {}
  for line in (text or ""):gmatch("[^\n]+") do
    local f = {}
    for field in (line .. "\t"):gmatch("([^\t]*)\t") do f[#f + 1] = field end
    local pct, up = tonumber(f[7]), tonumber(f[3])
    if f[1] == "1" and #f >= 13 and pct and up then
      out[#out + 1] = {
        boot = f[2], up = up, epoch = tonumber(f[4]), event = f[5], detail = (f[6] ~= "-" and f[6]) or "", pct = pct, status = f[8],
        mv = tonumber(f[9]), full = tonumber(f[10]), design = tonumber(f[11]), temp = tonumber(f[12]), cycles = tonumber(f[13]),
      }
    end
  end
  return out
end

--- The recorded samples, oldest first (empty when nothing has been recorded).
function B.history()
  return B.parse(slurp(B.LOG))
end

--- Delete the history. Returns true when it is gone (or was never there).
function B.clear()
  local ok, err = os.remove(B.LOG)
  return ok ~= nil or (err or ""):find("No such file", 1, true) ~= nil
end

--- "1 h 05 min", "12 min", "40 s".
function B.dur(s)
  s = math.floor(s + 0.5)
  if s >= 3600 then return ("%d h %02d min"):format(math.floor(s / 3600), math.floor(s % 3600 / 60)) end
  if s >= 60 then return ("%d min"):format(math.floor(s / 60)) end
  return ("%d s"):format(s)
end

--- Time of the uptime clock: "0:12" is 12 minutes after the start.
local function clock(up)
  return ("%d:%02d"):format(math.floor(up / 3600), math.floor(up % 3600 / 60))
end

local function discharging(s) return (s.status or ""):lower() == "discharging" end

--- Walk the samples and total the discharge by what was running. An interval counts when both ends are in the same boot,
--- both discharging, time passed and the percent did not rise. It belongs to the app that was running after its first sample
--- (app-start and tick: that app; anything else: the menu). Returns { all = {drop, secs}, apps = { name = {drop, secs} }, order = { names } }.
function B.usage(samples)
  local u = { all = { drop = 0, secs = 0 }, apps = {}, order = {} }
  local prev, ctx
  for _, s in ipairs(samples) do
    if prev and prev.boot == s.boot and s.up > prev.up and discharging(prev) and discharging(s) and s.pct <= prev.pct then
      local name = ctx or "(menu)"
      local a = u.apps[name]
      if not a then a = { drop = 0, secs = 0 }; u.apps[name] = a; u.order[#u.order + 1] = name end
      a.drop, a.secs = a.drop + (prev.pct - s.pct), a.secs + (s.up - prev.up)
      u.all.drop, u.all.secs = u.all.drop + (prev.pct - s.pct), u.all.secs + (s.up - prev.up)
    end
    if s.event == "app-start" or s.event == "tick" then ctx = (s.detail ~= "" and s.detail) or ctx
    else ctx = nil end
    prev = s
  end
  return u
end

--- Percent per hour, or nil when too little was recorded to say.
function B.rate(t)
  if not t or t.secs < B.MIN_SECONDS or t.drop <= 0 then return nil end
  return t.drop / (t.secs / 3600)
end

--- Boots, and how many ended without a shutdown being recorded (the last boot is the one running, so it is not counted).
function B.endings(samples)
  local boots, order = {}, {}
  for _, s in ipairs(samples) do
    if not boots[s.boot] then boots[s.boot] = {}; order[#order + 1] = s.boot end
    boots[s.boot].last = s
  end
  local unclean, lastUnclean = 0, nil
  for i = 1, #order - 1 do
    local last = boots[order[i]].last
    if last.event ~= "shutdown" then unclean, lastUnclean = unclean + 1, last end
  end
  return #order, unclean, lastUnclean
end

-- ---- the screens' words ----------------------------------------------------------------------------------------------------

local function field(label, value) return ("%-15s%s"):format(label, value) end

--- Health as a percent of the design capacity, or nil.
function B.health(r)
  if r and r.full and r.design and r.design > 0 then return math.floor(r.full * 100 / r.design + 0.5) end
  return nil
end

--- Lines for Settings > Battery > Status. r is the live reading (or nil), samples the history.
function B.statusLines(r, samples)
  local lines = {}
  local last
  for i = #samples, 1, -1 do
    if samples[i].full and samples[i].design then last = samples[i]; break end
  end
  if not r then
    lines[#lines + 1] = "This unit reports no battery."
    lines[#lines + 1] = ""
  else
    lines[#lines + 1] = field("Charge", ("%d %%  (%s)"):format(r.pct, r.status))
    lines[#lines + 1] = field("Voltage", r.mv and ("%.2f V"):format(r.mv / 1000) or "not reported")
    lines[#lines + 1] = field("Temperature", r.temp and ("%.1f C"):format(r.temp / 10) or "not reported")
    local h = B.health(r) or B.health(last)
    lines[#lines + 1] = field("Health", h and ("%d %% of its design capacity"):format(h) or "not reported by this battery")
    lines[#lines + 1] = field("Charge cycles", r.cycles and tostring(r.cycles) or "not reported by this battery")
    lines[#lines + 1] = ""
  end
  local u = B.usage(samples)
  local rate = B.rate(u.all)
  if rate then
    lines[#lines + 1] = field("Average drain", ("%.1f %% an hour  (over %s)"):format(rate, B.dur(u.all.secs)))
    if r and r.pct > 0 and discharging(r) then
      lines[#lines + 1] = field("Time left", ("about %s at that rate"):format(B.dur(r.pct / rate * 3600)))
    end
  else
    lines[#lines + 1] = "Average drain: not enough use recorded yet."
    lines[#lines + 1] = "(It needs about 10 minutes on battery, not charging.)"
  end
  lines[#lines + 1] = ""
  local starts, unclean, lastU = B.endings(samples)
  lines[#lines + 1] = field("Recorded", ("%d samples, %d %s"):format(#samples, starts, starts == 1 and "start" or "starts"))
  if unclean > 0 then
    lines[#lines + 1] = field("Ended abruptly", ("%d of %d (no shutdown recorded)"):format(unclean, starts - 1))
    lines[#lines + 1] = field("Last one at", ("%d %%%s"):format(lastU.pct, lastU.pct <= B.LOW and "  (the battery probably ran flat)" or ""))
  else
    lines[#lines + 1] = field("Ended abruptly", starts > 1 and "none" or "nothing to compare yet")
  end
  return lines
end

--- Lines for Settings > Battery > Use by app. A header, then one row per app, the most used first.
function B.appLines(samples)
  local u = B.usage(samples)
  if #u.order == 0 then
    return { "Nothing recorded yet.", "", "Play something on battery (not charging) for a while,", "then look here." }
  end
  table.sort(u.order, function(a, b) return u.apps[a].secs > u.apps[b].secs end)
  local lines = { ("%-26s%-11s%-6s%s"):format("App", "Time", "Used", "Drain"), "" }
  for _, name in ipairs(u.order) do
    local a = u.apps[name]
    local r = B.rate(a)
    lines[#lines + 1] = ("%-26s%-11s%-6s%s"):format(name:sub(1, 25), B.dur(a.secs), a.drop .. "%", r and ("%.1f %%/h"):format(r) or "too short")
  end
  return lines
end

--- Lines for Settings > Battery > History: the newest samples first, at most `limit`.
function B.recentLines(samples, limit)
  if #samples == 0 then return { "Nothing recorded yet.", "", "A sample is taken when the system starts, when an app", "starts and ends, every few minutes in an app, and at", "shutdown." } end
  local lines = { ("%-6s%-10s%-20s%-5s%s"):format("Up", "Event", "App", "Chg", "State"), "" }
  for i = #samples, math.max(1, #samples - (limit or 60) + 1), -1 do
    local s = samples[i]
    lines[#lines + 1] = ("%-6s%-10s%-20s%-5s%s"):format(clock(s.up), s.event, s.detail:sub(1, 19), s.pct .. "%", (s.status or ""):sub(1, 11))
  end
  return lines
end

return B
