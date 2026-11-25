-- The screen mirror's tap (docs/SCREEN-MIRROR.md, ROADMAP section 28): when a mirror is on, hand the CELLS THAT CHANGED to the daemon
-- r3i6nos-mirror as UDP datagrams to 127.0.0.1. The C twin is theme/r3i6nos_mirror.h; both speak the daemon's datagrams:
--   F + 3072 bytes                                        the whole screen: cell i is bytes 2i (character), 2i+1 (attribute = fg + 16 * bg)
--   D + runs of (u16 big-endian start, u8 count, count x 2 bytes)   the changed cells
-- A screen is drawn (and so tapped) only when a cell changed, and with no mirror the cost of that is one file test, at most once a
-- second: no timer, no keyframe on a timer, no polling. Nothing here may break an app: every failure is swallowed.
-- Everything is plain Lua but M.sys.send, which uses LÖVE's bundled luasocket, so luajit can test it (tests/test_mirror.lua).

local M = {}

M.DIR = os.getenv("R3I6NOS_MIRROR_DIR") or "/run/loveos/mirror"
M.PORT = tonumber(os.getenv("R3I6NOS_MIRROR_INGEST") or "") or 36364
M.COLS, M.ROWS = 64, 24
M.CELLS = M.COLS * M.ROWS
M.RECHECK = 1        -- seconds between looks for the flag file while a screen keeps changing

local char = string.char
M.EXT = 0x110000     -- the theme's extended cell (T.EXT): EXT + a code point of ANSIapps Mono; the datagrams carry 254 in its place, and E the code point

local function byteOf(ch) return ch >= M.EXT and 254 or ch % 256 end

local function attr(scr, i) return (scr.fg[i] % 16) + (scr.bg[i] % 16) * 16 end

--- The whole screen as one datagram.
function M.full(scr)
  local out = { "F" }
  for i = 1, M.CELLS do out[#out + 1] = char(byteOf(scr.ch[i]), attr(scr, i)) end
  return table.concat(out)
end

--- The cells that differ from prev as one datagram (nil when none do). A run is contiguous changed cells, at most 255; a single unchanged
--- cell between two changes is carried along, because a new run costs 3 bytes and a cell 2.
function M.delta(scr, prev)
  local out, changed, i = { "D" }, 0, 1
  local function differs(k) return scr.ch[k] ~= prev.ch[k] or scr.fg[k] ~= prev.fg[k] or scr.bg[k] ~= prev.bg[k] end
  while i <= M.CELLS do
    if differs(i) then
      local first, last = i, i
      local j = i + 1
      while j <= M.CELLS and j - first < 255 do
        if differs(j) then last = j
        elseif not (j + 1 <= M.CELLS and j + 1 - first < 255 and differs(j + 1)) then break end
        j = j + 1
      end
      local cells = {}
      for k = first, last do cells[#cells + 1] = char(byteOf(scr.ch[k]), attr(scr, k)) end
      out[#out + 1] = char(math.floor((first - 1) / 256), (first - 1) % 256, last - first + 1) .. table.concat(cells)
      changed = changed + (last - first + 1)
      i = last + 1
    else
      i = i + 1
    end
  end
  if changed == 0 then return nil end
  if changed > M.CELLS / 2 then return M.full(scr) end
  return table.concat(out)
end

--- The extended cells as one datagram (nil when there are none): `E` and runs of (u16 start, u8 count, count x (u24 code point, u8 attribute)).
--- With prev, only the extended cells that differ from it; without, every one (after a full frame, which makes every cell ordinary).
function M.ext(scr, prev)
  local out, n, run = { "E" }, 0, nil
  local function flush() if run then out[#out + 1] = char(math.floor(run.first / 256), run.first % 256, #run.cells) .. table.concat(run.cells); run = nil end end
  for i = 1, M.CELLS do
    local ch = scr.ch[i]
    if ch >= M.EXT and (not prev or ch ~= prev.ch[i] or scr.fg[i] ~= prev.fg[i] or scr.bg[i] ~= prev.bg[i]) then
      local cp = ch - M.EXT
      if run and (run.first + #run.cells ~= i - 1 or #run.cells >= 255) then flush() end
      run = run or { first = i - 1, cells = {} }
      run.cells[#run.cells + 1] = char(math.floor(cp / 65536), math.floor(cp / 256) % 256, cp % 256, attr(scr, i))
      n = n + 1
    end
  end
  flush()
  if n == 0 then return nil end
  return table.concat(out)
end

-- ---- the world (tests replace it) -------------------------------------------------------------------------------------------------

--- An IPv4 UDP socket from a luasocket module. LÖVE 11.5's LuaSocket 3.0-rc1 on the R36S answers "refused" to every `sendto` on a socket made by
--- `socket.udp()` (seen on the unit 2026-11-15, with the daemon listening), while `socket.udp4()` sends: ask for IPv4 by name when there is a way to.
function M.newSocket(socket)
  if socket.udp4 then return socket.udp4() end
  if socket.udp then return socket.udp() end
  return nil
end

M.sys = {
  now = os.time,
  on = function()
    local f = io.open(M.DIR .. "/on", "r")
    if f then f:close() end
    return f ~= nil
  end,
  send = function(data)
    if M.udp == nil then
      local ok, socket = pcall(require, "socket")
      M.udp = (ok and M.newSocket(socket)) or false
      if M.udp then M.udp:settimeout(0) end
    end
    if not M.udp then error("no socket library in this LÖVE", 0) end   -- level 0: no file:line prefix, the menu shows only 58 characters
    local ok, err = M.udp:sendto(data, "127.0.0.1", M.PORT)
    if not ok then error("sendto: " .. tostring(err), 0) end
  end,
}

--- What the tap did, for Settings > Screen mirror: how many datagrams it sent, how many failed and the last failure. Without it a mirror
--- that shows nothing cannot say whether the app never sent or the daemon never got it.
M.stats = { sent = 0, failed = 0, last_error = nil }

local state = { on = false, checked = -math.huge, sent_full = false }

--- Is a mirror on? Looks at the flag file at most once every M.RECHECK seconds.
local function mirrored()
  local now = M.sys.now()
  if now - state.checked >= M.RECHECK or now < state.checked then
    state.checked = now
    local was = state.on
    state.on = M.sys.on() and true or false
    if state.on and not was then state.sent_full = false end -- a mirror just came on: it needs the whole screen first
  end
  return state.on
end

--- Call when a screen has changed and is about to be remembered as `prev` (nil on an app's first screen). Sends the change when a mirror is on.
function M.tap(scr, prev)
  local ok, err = pcall(function()
    if not mirrored() then return end
    local data
    if prev and state.sent_full then data = M.delta(scr, prev) else data = M.full(scr); state.sent_full = true end
    if data then
      M.sys.send(data); M.stats.sent = M.stats.sent + 1
      -- cells from ANSIapps Mono follow the frame that carries their stand-in (a delta that fell back to a full frame has made every cell ordinary)
      local e = M.ext(scr, (data:sub(1, 1) == "D") and prev or nil)
      if e then M.sys.send(e); M.stats.sent = M.stats.sent + 1 end
    end
  end)
  if not ok then
    M.stats.failed = M.stats.failed + 1
    M.stats.last_error = tostring(err):sub(1, 80)
    state.sent_full = false   -- whatever failed, the next screen goes out whole
  end
  return ok
end

--- For tests: forget what was learned.
function M.reset()
  state.on, state.checked, state.sent_full = false, -math.huge, false
  M.stats.sent, M.stats.failed, M.stats.last_error = 0, 0, nil
end

return M
