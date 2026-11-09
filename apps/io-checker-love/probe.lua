-- IO Checker: what it reads from the system and the cards, with plain Lua io (LÖVE's own filesystem only reaches its save
-- folder). No LÖVE calls, so luajit tests it against temporary folders (tests/test_io_checker.lua).
-- Overrides, for tests and laptops: R3I6NOS_PROC (/proc), R3I6NOS_SYS (/sys), R3I6NOS_ETC (/etc), R3I6NOS_ROMS (/roms),
-- R3I6NOS_ROMS2 (/roms2), R3I6NOS_SOUND (r3i6nos-sound), R3I6NOS_IO_MB (8: the speed test's size).

local model = require("model")

local P = {}
local env = os.getenv
P.PROC = env("R3I6NOS_PROC") or "/proc"
P.SYS = env("R3I6NOS_SYS") or "/sys"
P.ETC = env("R3I6NOS_ETC") or "/etc"
P.ROMS = env("R3I6NOS_ROMS") or "/roms"
P.ROMS2 = env("R3I6NOS_ROMS2") or "/roms2"
P.SOUND = env("R3I6NOS_SOUND") or "/usr/local/bin/r3i6nos-sound"
P.MB = tonumber(env("R3I6NOS_IO_MB") or "") or 8
P.FOLDER = "IO_Checker"
P.clock = os.clock -- main.lua sets love.timer.getTime: wall time, which the speed test needs

local function quote(s) return "'" .. tostring(s):gsub("'", "'\\''") .. "'" end

function P.read(path)
  local f = io.open(path, "rb")
  if not f then return nil end
  local s = f:read("*a")
  f:close()
  return s
end

local function first(path)
  local s = P.read(path)
  return s and (s:gsub("%z", ""):match("^%s*(.-)%s*$")) or nil
end

--- A command's output (nil when it could not run).
function P.run(cmd)
  local p = io.popen(cmd .. " 2>/dev/null")
  if not p then return nil end
  local out = p:read("*a")
  p:close()
  return out
end

function P.ls(dir)
  local out = {}
  for name in (P.run("ls -1 " .. quote(dir)) or ""):gmatch("[^\n]+") do out[#out + 1] = name end
  return out
end

-- ---- the system ---------------------------------------------------------------------------------------------------------------------

local function keyvals(text)
  local out = {}
  for k, v in (text or ""):gmatch("([%w_]+)=([^\n]*)") do out[k] = v end
  return out
end

--- The battery: { pct, status } or nil.
function P.battery()
  local base = P.SYS .. "/class/power_supply"
  for _, name in ipairs(P.ls(base)) do
    if first(base .. "/" .. name .. "/type") == "Battery" then
      return { pct = tonumber(first(base .. "/" .. name .. "/capacity") or ""), status = first(base .. "/" .. name .. "/status") }
    end
  end
end

--- The backlight: { now, max, name } or nil.
function P.backlight()
  local base = P.SYS .. "/class/backlight"
  for _, name in ipairs(P.ls(base)) do
    local now, max = tonumber(first(base .. "/" .. name .. "/brightness") or ""), tonumber(first(base .. "/" .. name .. "/max_brightness") or "")
    if now and max then return { now = now, max = max, name = name } end
  end
end

--- The system as label/value rows: the build, kernel, board, CPU, memory, battery, backlight.
function P.system()
  local rows = {}
  local function add(label, value) rows[#rows + 1] = { label = label, value = value or "?" } end
  local rel = keyvals(P.read(P.ETC .. "/r3i6nos-release"))
  add("r3i6nos", rel.VERSION and ("%s, %s"):format(rel.VERSION, (rel.COMMIT or "?"):sub(1, 7)) or "not r3i6nos")
  add("Kernel", first(P.PROC .. "/sys/kernel/osrelease"))
  add("Board", first(P.PROC .. "/device-tree/model"))
  local cpus = 0
  for _ in (P.read(P.PROC .. "/cpuinfo") or ""):gmatch("\nprocessor%s*:") do cpus = cpus + 1 end
  if (P.read(P.PROC .. "/cpuinfo") or ""):match("^processor%s*:") then cpus = cpus + 1 end
  local khz = tonumber(first(P.SYS .. "/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq") or "")
  add("CPU", ("%d cores"):format(cpus) .. (khz and (", %d MHz max"):format(math.floor(khz / 1000)) or ""))
  local mem = P.read(P.PROC .. "/meminfo") or ""
  local total, avail = tonumber(mem:match("MemTotal:%s*(%d+)")), tonumber(mem:match("MemAvailable:%s*(%d+)"))
  add("Memory", total and ("%d MB, %d MB free"):format(math.floor(total / 1024), math.floor((avail or 0) / 1024)))
  local b = P.battery()
  add("Battery", b and ("%s%%, %s"):format(b.pct or "?", b.status or "?") or "none found")
  local l = P.backlight()
  add("Backlight", l and ("%d of %d"):format(l.now, l.max) or "none found")
  return rows
end

-- ---- sound --------------------------------------------------------------------------------------------------------------------------

--- The sound card and the RK817's output path, from r3i6nos-sound status (key: value lines), and /proc/asound/cards.
function P.sound()
  local out = {}
  local cards = P.read(P.PROC .. "/asound/cards") or ""
  out.card = cards:match("^%s*%d+ %[[^%]]*%]:%s*([^\n]+)") or "none"
  local st = P.run(quote(P.SOUND) .. " status") or ""
  out.path = st:match("card %d+ playback path: (%S+)")
  out.jack = st:match("\njack: ([^\n]+)") or st:match("^jack: ([^\n]+)")
  out.headphones = st:match("headphones: (%S+)")
  return out
end

-- ---- the cards ----------------------------------------------------------------------------------------------------------------------

--- The cards to test: the games card always (it is where the report goes), the data card when something is mounted there.
function P.cards()
  local mounts = P.read(P.PROC .. "/mounts") or ""
  local list = { { label = "Games card", path = P.ROMS } }
  local m = model.mountOf(P.ROMS2, mounts)
  if (m and m.at == P.ROMS2) or env("R3I6NOS_ROMS2") then list[2] = { label = "Data card", path = P.ROMS2 } end
  for _, c in ipairs(list) do c.mount = model.mountOf(c.path, mounts) end
  return list
end

--- Free bytes under path (df), or nil.
function P.free(path)
  local out = P.run("df -Pk " .. quote(path))
  local avail = out and out:match("\n%S+%s+%d+%s+%d+%s+(%d+)")
  return avail and tonumber(avail) * 1024 or nil
end

local function chunk()
  local block = {}
  for i = 0, 255 do block[#block + 1] = string.char(i) end
  return string.rep(table.concat(block), 4096) -- 1 MiB, every byte value
end

--- The checks on one card, in its IO_Checker folder: make the folder, write a file, write a second and rename it over the first,
--- read it back, delete it; then the speed. Returns rows { status, label, value } (status PASS, FAIL, INFO or SKIP).
function P.cardCheck(card)
  local rows = {}
  local function add(status, label, value) rows[#rows + 1] = { status = status, label = card.label .. ": " .. label, value = value } end
  local m = card.mount
  add("INFO", "where", ("%s, %s, %s"):format(card.path, m and m.fs or "?", m and m.dev or "?"))
  local dir = card.path .. "/" .. P.FOLDER
  os.execute("mkdir -p " .. quote(dir) .. " 2>/dev/null")
  local a, b = dir .. "/.io-check-a.tmp", dir .. "/.io-check-b.tmp"
  local function write(path, text)
    local f = io.open(path, "wb")
    if not f then return false end
    local ok = f:write(text) and f:close()
    return ok and true or false
  end
  local step
  if not write(a, "first\n") then step = "cannot make or write in " .. dir
  elseif not write(b, "second\n") then step = "cannot write a second file"
  elseif not os.rename(b, a) then step = "cannot rename over a file"
  elseif P.read(a) ~= "second\n" then step = "read back the wrong text"
  elseif not os.remove(a) then step = "cannot delete"
  elseif P.read(a) then step = "deleted, but still there" end
  os.remove(a); os.remove(b)
  add(step and "FAIL" or "PASS", "write, rename over, read, delete", step or "ok")
  if step then return rows end

  local data, path = chunk(), dir .. "/.io-check-speed.tmp"
  local t0 = P.clock()
  local f = io.open(path, "wb")
  local ok = f ~= nil
  for _ = 1, P.MB do ok = ok and f:write(data) ~= nil end
  ok = ok and f:close()
  os.execute("sync")
  local tw = P.clock() - t0
  if not ok then
    os.remove(path)
    add("FAIL", "write speed", ("could not write %d MB (card full?)"):format(P.MB))
    return rows
  end
  add("INFO", "write speed", ("%.1f MB/s (%d MB + sync)"):format(P.MB * 1.048576 / math.max(tw, 1e-6), P.MB))
  t0 = P.clock()
  f = io.open(path, "rb")
  local same, n = f ~= nil, 0
  while f do
    local s = f:read(#data)
    if not s then break end
    n = n + #s
    if s ~= data:sub(1, #s) then same = false end
  end
  if f then f:close() end
  local tr = P.clock() - t0
  os.remove(path)
  add((same and n == #data * P.MB) and "PASS" or "FAIL", "read back", (same and n == #data * P.MB) and "every byte the same" or ("%d of %d bytes, %s"):format(n, #data * P.MB, same and "same" or "DIFFERENT"))
  add("INFO", "read speed", ("%.1f MB/s (may be cached)"):format(P.MB * 1.048576 / math.max(tr, 1e-6)))
  local free = P.free(card.path)
  add("INFO", "free", free and model.gb(free) or "?")
  return rows
end

--- Every card's checks, one list.
function P.files()
  local rows = {}
  for _, c in ipairs(P.cards()) do
    for _, r in ipairs(P.cardCheck(c)) do rows[#rows + 1] = r end
  end
  return rows
end

return P
