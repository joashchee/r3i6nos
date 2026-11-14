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

local function attr(scr, i) return (scr.fg[i] % 16) + (scr.bg[i] % 16) * 16 end

--- The whole screen as one datagram.
function M.full(scr)
  local out = { "F" }
  for i = 1, M.CELLS do out[#out + 1] = char(scr.ch[i] % 256, attr(scr, i)) end
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
      for k = first, last do cells[#cells + 1] = char(scr.ch[k] % 256, attr(scr, k)) end
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

-- ---- the world (tests replace it) -------------------------------------------------------------------------------------------------

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
      M.udp = (ok and socket.udp and socket.udp()) or false
      if M.udp then M.udp:settimeout(0) end
    end
    if M.udp then M.udp:sendto(data, "127.0.0.1", M.PORT) end
  end,
}

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
  local ok = pcall(function()
    if not mirrored() then return end
    local data
    if prev and state.sent_full then data = M.delta(scr, prev) else data = M.full(scr); state.sent_full = true end
    if data then M.sys.send(data) end
  end)
  return ok
end

--- For tests: forget what was learned.
function M.reset() state.on, state.checked, state.sent_full = false, -math.huge, false end

return M
