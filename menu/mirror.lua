-- Settings > Screen mirror (docs/SCREEN-MIRROR.md, ROADMAP section 28): start and stop the daemon r3i6nos-mirror on a chosen link, and show
-- the address a computer, monitor or TV opens. The daemon is root-free and the menu runs it as itself; a person starts it here and it is
-- gone at reboot (CLAUDE.md rule 6). Pure Lua but for M.sys (files and popen), so luajit tests it (tests/test_settings.lua, tests/test_mirror_menu.lua).

local M = {}

M.DIR = os.getenv("R3I6NOS_MIRROR_DIR") or "/run/loveos/mirror"
M.CMD = os.getenv("R3I6NOS_MIRROR_CMD") or "/usr/local/bin/r3i6nos-mirror"

local function quote(s) return "'" .. tostring(s):gsub("'", "'\\''") .. "'" end

M.sys = {
  read = function(path)
    local f = io.open(path, "r")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
  end,
  --- Run a command, both streams: its output text and whether it succeeded.
  run = function(cmd)
    local p = io.popen(cmd .. " 2>&1")
    if not p then return "", false end
    local out = p:read("*a")
    local ok = p:close()
    return out or "", ok == true or ok == 0
  end,
  alive = function(pid)
    local f = io.open("/proc/" .. pid .. "/stat", "r")
    if f then f:close() return true end
    return false
  end,
}

local function keyvals(s)
  local t = {}
  for k, v in (s or ""):gmatch("([%w_]+)=([^\n]*)") do t[k] = v end
  return t
end

--- { running, url, iface, address, viewers } from the daemon's own files; running is false when none is up (a flag left by a dead one counts for nothing).
function M.status(sys)
  sys = sys or M.sys
  local pid = tonumber((sys.read(M.DIR .. "/on") or ""):match("%d+"))
  if not pid or not sys.alive(pid) then return { running = false } end
  local kv = keyvals(sys.read(M.DIR .. "/info"))
  return { running = true, url = kv.url, iface = kv.iface, address = kv.address, viewers = tonumber(kv.viewers) or 0 }
end

--- The links that could carry it: { { name, addr, kind } }, the ones with an IPv4 address.
function M.interfaces(sys)
  sys = sys or M.sys
  local out, text = {}, sys.run(quote(M.CMD) .. " list")
  for line in text:gmatch("[^\n]+") do
    local name, addr, kind = line:match("^([%w%._:%-]+)\t([%d%.]+)\t(%a+)$")
    if name then out[#out + 1] = { name = name, addr = addr, kind = kind } end
  end
  return out
end

--- Start it on a link. true and the address, or false and why.
function M.start(iface, sys)
  sys = sys or M.sys
  if not (iface and iface:match("^[%w%._:%-]+$")) then return false, "not a network interface" end
  local out, ok = sys.run(quote(M.CMD) .. " start --iface " .. quote(iface))
  local url = out:match("(http://[^%s]+)")
  if ok and url then return true, url end
  local why = out:gsub("^%s+", ""):gsub("%s+$", "")
  return false, (why ~= "" and why) or "it did not start"
end

--- Stop it. true, or false and why.
function M.stop(sys)
  sys = sys or M.sys
  local _, ok = sys.run(quote(M.CMD) .. " stop")
  return ok
end

--- What the screen says once it runs: lines.
function M.addressLines(st)
  local pages = (st.viewers or 0) == 1 and "1 page is watching." or ((st.viewers or 0) .. " pages are watching.")
  return {
    "The screen is being mirrored. On a computer, monitor or",
    "TV on the same link, open this address in a browser:",
    "",
    "  " .. tostring(st.url or "(unknown)"),
    "",
    "Link: " .. tostring(st.iface or "?") .. " (" .. tostring(st.address or "?") .. ")",
    pages,
    "",
    "The page only watches: it cannot press anything.",
    "Stop it here when you are done. A restart ends it too.",
  }
end

M.NO_LINK = {
  "No link is up, so there is nowhere to show the screen.",
  "",
  "Plug in a USB Ethernet adapter with a cable, or a USB",
  "Wi-Fi dongle, and set it up in Settings > Network. When it",
  "has an address, it is listed here.",
  "",
  "See Instructions for the choices.",
}

M.INSTRUCTIONS = {
  {
    "SCREEN MIRROR",
    "",
    "Shows what this unit shows on a bigger screen: a",
    "computer, a monitor or a TV with a browser. It sends only",
    "the characters that change, so an idle screen costs",
    "nothing and a TV gets sharp text at any size.",
    "",
    "1. Get a link up: Settings > Network (a USB Ethernet",
    "   adapter and cable, or a USB Wi-Fi dongle).",
    "2. Settings > Screen mirror > pick the link.",
    "3. Open the address it shows on the other device. The",
    "   computer must be on the same network.",
    "",
    "Nothing starts by itself. It listens only on the link you",
    "pick, behind a random code in the address, and it is off",
    "again after a restart.",
  },
  {
    "GOOD TO KNOW",
    "",
    "- The page is read-only: it sends nothing to the unit.",
    "- A link draws power: a USB adapter or Wi-Fi dongle runs",
    "  from the battery. Stop the mirror when you are done.",
    "- A Rec demo is not mirrored: start it only after you",
    "  stop the mirror, so the unit's full power goes to it.",
    "- Pictures (Commander's picture viewer, video) show as",
    "  the text around them for now.",
    "- One cable to a computer that also powers the unit (the",
    "  USB port acting as a network card) is not built yet.",
  },
}

return M
