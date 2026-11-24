-- The network indicator for the top bar: "ë" when a wired link is up, "¥" and three cells of signal when Wi-Fi is connected.
-- Plain Lua with io only (no LÖVE calls), so luajit can test it. Reads /sys/class/net and /proc/net/wireless; no network traffic.
-- Overrides, for tests: R3I6NOS_NET_DIR, R3I6NOS_WIRELESS_FILE. Untested on a unit (the R36S's Wi-Fi driver may report the level differently).

local N = {}

N.DIR = os.getenv("R3I6NOS_NET_DIR") or "/sys/class/net"
N.WIRELESS = os.getenv("R3I6NOS_WIRELESS_FILE") or "/proc/net/wireless"

local function slurp(path)
  local f = io.open(path, "r")
  if not f then return nil end
  local s = f:read("*a")
  f:close()
  return s and (s:gsub("%s+$", "")) or nil
end

local function ls(path)
  local out = {}
  local p = io.popen('ls -1 "' .. path:gsub('"', '\\"') .. '" 2>/dev/null')
  if not p then return out end
  for line in p:lines() do out[#out + 1] = line end
  p:close()
  return out
end

--- Signal in cells (1 to 3) from the level in dBm (as /proc/net/wireless gives it, negative) or, when the driver gives only a
--- link quality out of 70, from that. At or above -67 dBm (or 47/70): 3; at or above -80 (or 25/70): 2; otherwise 1.
function N.cells(level, quality)
  if level and level < 0 then return level >= -67 and 3 or (level >= -80 and 2 or 1) end
  if level and level > 0 then quality = quality or level end -- some drivers print the level as a positive number out of 70
  if quality then return quality >= 47 and 3 or (quality >= 25 and 2 or 1) end
  return 1
end

local function wirelessLevels()
  local out = {}
  for line in (slurp(N.WIRELESS) or ""):gmatch("[^\n]+") do
    local name, link, level = line:match("^%s*([%w_%-%.]+):%s+%S+%s+(%-?[%d%.]+)%.?%s+(%-?[%d%.]+)")
    if name then out[name] = { quality = tonumber(link), level = tonumber(level) } end
  end
  return out
end

--- What is connected: { wired = bool, wifi = bool, cells = 1..3 or nil }. nil is never returned; both false means no link.
--- The USB gadget's usb0 (the screen mirror's link) and loopback are not a network the unit joined.
function N.read()
  local r = { wired = false, wifi = false }
  local levels
  for _, n in ipairs(ls(N.DIR)) do
    if n ~= "lo" and not n:find("^usb") and not n:find("^docker") and not n:find("^veth") then
      local up = slurp(N.DIR .. "/" .. n .. "/operstate") == "up"
      if up then
        if slurp(N.DIR .. "/" .. n .. "/wireless/status") or slurp(N.DIR .. "/" .. n .. "/phy80211/name") or n:find("^wl") then
          levels = levels or wirelessLevels()
          local l = levels[n] or {}
          r.wifi = true
          r.cells = math.max(r.cells or 0, N.cells(l.level, l.quality))
        else
          r.wired = true
        end
      end
    end
  end
  return r
end

--- The text for the top bar's right side, or nil with no link. Wired: "ë". Wi-Fi: "¥" and three cells, "█" for each of the signal's cells, "░" for the rest.
function N.indicator(r)
  if not r or not (r.wired or r.wifi) then return nil end
  local parts = {}
  if r.wired then parts[#parts + 1] = "ë" end
  if r.wifi then
    local c = r.cells or 1
    parts[#parts + 1] = "¥" .. string.rep("█", c) .. string.rep("░", 3 - c)
  end
  return table.concat(parts, " ")
end

--- The same as R3i6nos Mono glyphs: { names = {"sys.wifi.3"}, wired = true } (wired is the CP437 "ë", which has no glyph of its own), nil with no link.
function N.glyphs(r)
  if not r or not (r.wired or r.wifi) then return nil end
  local names = {}
  if r.wifi then names[1] = "sys.wifi." .. math.max(0, math.min(3, r.cells or 1)) end
  return { names = names, wired = r.wired and true or false }
end

return N
