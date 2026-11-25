-- Run with: luajit tests/test_mirror.lua   (from the repo root)
-- theme/r3i6nos_mirror.lua: the datagrams the screen mirror's daemon (overlay/usr/local/bin/r3i6nos-mirror) reads, and when they are sent.
package.path = "theme/?.lua;" .. package.path
local T = require("r3i6nos_ansiapps")
local M = require("r3i6nos_mirror")
local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

local function copy(scr)
  local c = { ch = {}, fg = {}, bg = {} }
  for i = 1, M.CELLS do c.ch[i], c.fg[i], c.bg[i] = scr.ch[i], scr.fg[i], scr.bg[i] end
  return c
end

--- The daemon's side: apply a datagram to `state` (a table of 3072 bytes), as r3i6nos-mirror's parse_datagram does.
local function apply(state, data)
  local kind = data:sub(1, 1)
  if kind == "F" then
    eq(#data, 1 + M.CELLS * 2, "a full frame is 3072 bytes")
    for i = 1, M.CELLS * 2 do state[i] = data:byte(1 + i) end
    return "full"
  end
  eq(kind, "D")
  local i = 2
  while i <= #data do
    local start = data:byte(i) * 256 + data:byte(i + 1)
    local count = data:byte(i + 2)
    assert(count >= 1, "an empty run")
    assert(start + count <= M.CELLS, "a run past the screen")
    i = i + 3
    for k = 0, count * 2 - 1 do
      assert(i <= #data, "a run cut short")
      state[start * 2 + k + 1] = data:byte(i)
      i = i + 1
    end
  end
  return "delta"
end

local function show(scr)   -- what the page would hold for a screen
  local s = {}
  for i = 1, M.CELLS do s[2 * i - 1], s[2 * i] = scr.ch[i], scr.fg[i] + 16 * scr.bg[i] end
  return s
end

local function same(a, b)
  for i = 1, M.CELLS * 2 do if a[i] ~= b[i] then return false, i end end
  return true
end

-- ---- the datagrams ------------------------------------------------------------------------------------------------------------------

do
  local scr = T.screen()
  T.topBar(scr, "r3i6nos", "v1")
  local st = {}
  eq(apply(st, M.full(scr)), "full")
  assert(same(st, show(scr)), "a full frame is the screen")
  eq(st[1], 32); eq(st[2], T.WHITE + 16 * T.BLUE, "cell 0 of the top bar is white on blue")
end

do -- no change, no datagram
  local a = T.screen()
  eq(M.delta(a, copy(a)), nil, "nothing changed, nothing sent")
end

do -- one cell: a header of 3 bytes and 2 bytes of cell
  local a = T.screen()
  local b = copy(a)
  a:put(5, 2, 65, T.YELLOW, T.RED)
  local d = M.delta(a, b)
  eq(#d, 1 + 3 + 2, "one changed cell is 6 bytes")
  eq(d:byte(2) * 256 + d:byte(3), 2 * 64 + 5)
  eq(d:byte(4), 1)
  local st = show(b)
  eq(apply(st, d), "delta")
  assert(same(st, show(a)))
end

do -- a single unchanged cell between two changes is carried along; a wider gap starts a new run
  local base = T.screen()
  local a = copy(base)
  a.ch[11], a.ch[13] = 65, 66
  local d = M.delta(a, base)
  eq(#d, 1 + 3 + 3 * 2, "one run of three cells")
  a = copy(base)
  a.ch[11], a.ch[15] = 65, 66
  d = M.delta(a, base)
  eq(#d, 1 + 2 * (3 + 2), "two runs of one cell")
end

do -- a run never passes 255 cells; most of the screen changing is sent whole
  local base = T.screen()
  local a = copy(base)
  for i = 1, 600 do a.ch[i] = 66 end
  local d = M.delta(a, base)
  eq(d:sub(1, 1), "D")
  eq(d:byte(4), 255)
  local st = show(base)
  apply(st, d)
  assert(same(st, show(a)))
  for i = 1, 1000 do a.ch[i] = 67 end
  eq(M.delta(a, base):sub(1, 1), "F", "more than half changed: the whole screen")
end

do -- a fuzz: any change from any screen replays to the same screen
  math.randomseed(7)
  for round = 1, 300 do
    local a = T.screen()
    for i = 1, M.CELLS do a.ch[i], a.fg[i], a.bg[i] = math.random(0, 255), math.random(0, 15), math.random(0, 15) end
    local b = copy(a)
    for _ = 1, math.random(1, 400) do
      local i = math.random(1, M.CELLS)
      b.ch[i], b.fg[i], b.bg[i] = math.random(0, 255), math.random(0, 15), math.random(0, 15)
    end
    local d = M.delta(b, a)
    if d then
      local st = show(a)
      apply(st, d)
      local ok, at = same(st, show(b))
      assert(ok, "round " .. round .. " differs at byte " .. tostring(at))
    end
  end
end

-- ---- when they are sent ---------------------------------------------------------------------------------------------------------------

local now, flag, sent, looks = 1000, false, {}, 0
M.sys.now = function() return now end
M.sys.on = function() looks = looks + 1; return flag end
M.sys.send = function(d) sent[#sent + 1] = d end

do
  M.reset()
  local a = T.screen()
  M.tap(a, nil)
  eq(#sent, 0, "no mirror, nothing sent")
  eq(looks, 1)
  local b = copy(a); b.ch[1] = 65
  M.tap(b, a)
  eq(looks, 1, "the flag is looked at once a second at most, not on every change")
  now = now + 1
  M.tap(b, a)
  eq(looks, 2)
  eq(#sent, 0)

  flag = true
  now = now + 1
  local c = copy(b); c.ch[2] = 66
  M.tap(c, b)
  eq(#sent, 1)
  eq(sent[1]:sub(1, 1), "F", "a mirror that has just come on is given the whole screen first")
  local d = copy(c); d.ch[3] = 67
  M.tap(d, c)
  eq(#sent, 2)
  eq(sent[2]:sub(1, 1), "D", "then only what changed")
  eq(#sent[2], 6)

  M.tap(T.screen(), nil)
  eq(sent[3]:sub(1, 1), "F", "an app's first screen (no previous one) is sent whole")

  flag = false
  now = now + 1
  M.tap(d, c)
  eq(#sent, 3, "the flag gone: nothing more is sent")
  flag = true
  now = now + 1
  M.tap(d, c)
  eq(sent[4]:sub(1, 1), "F", "and when it comes back, the whole screen again")
end

do -- an app must never be broken by the mirror
  M.reset()
  flag = true
  now = now + 5
  M.sys.send = function() error("socket gone") end
  local a = T.screen()
  eq(pcall(M.tap, a, nil), true, "a failing send raises nothing in the app")
  eq(M.stats.failed, 1, "but it is counted")
  assert(tostring(M.stats.last_error):find("socket gone"), "and says why")
  eq(M.stats.sent, 0)
end

do -- the IPv4 socket is asked for by name: socket.udp() refused every send on the unit's LuaSocket 3.0-rc1
  local made = {}
  local both = { udp = function() made[#made + 1] = "udp" return {} end, udp4 = function() made[#made + 1] = "udp4" return {} end }
  M.newSocket(both)
  eq(made[1], "udp4", "udp4 first")
  local older = { udp = function() made[#made + 1] = "udp" return {} end }
  M.newSocket(older)
  eq(made[2], "udp", "an older luasocket without udp4 still gets a socket")
  eq(M.newSocket({}), nil, "no socket library function: no socket, no error")
end

do -- ANSIapps Mono cells: the frame carries the stand-in 254, an E datagram the code point (daemon: tests/test_r3i6nos_mirror.py)
  eq(M.EXT, T.EXT, "the mirror and the theme agree on the extended cell")
  T.useMono(true)
  local a = T.screen()
  a:glyph(3, 2, "icon.app.hydrolik", T.YELLOW, T.RED)
  local ic = T.glyph("icon.app.hydrolik")
  local f = M.full(a)
  eq(f:byte(1 + 2 * (2 * 64 + 3) + 1), 254, "a full frame holds 254 for an extended cell")
  eq(f:byte(1 + 2 * (2 * 64 + 3) + 2), T.YELLOW + 16 * T.RED)
  local e = M.ext(a, nil)
  eq(e:sub(1, 1), "E"); eq(#e, 1 + 3 + 2 * 4, "one run of two cells")
  eq(e:byte(2) * 256 + e:byte(3), 2 * 64 + 3); eq(e:byte(4), 2)
  local cp = ic[1] - T.EXT
  eq(e:byte(5) * 65536 + e:byte(6) * 256 + e:byte(7), cp); eq(e:byte(8), T.YELLOW + 16 * T.RED)
  eq(e:byte(9) * 65536 + e:byte(10) * 256 + e:byte(11), cp + 1, "the second half of the icon")
  eq(M.ext(T.screen(), nil), nil, "no extended cell, no E")
  local same_ = T.screen(); same_:glyph(3, 2, "icon.app.hydrolik", T.YELLOW, T.RED)
  eq(M.ext(a, same_), nil, "nothing changed, nothing sent")
  local b = T.screen(); b:glyph(3, 2, "icon.app.klamansi", T.YELLOW, T.RED)
  eq(M.ext(b, a):byte(8), T.YELLOW + 16 * T.RED, "a different glyph in the same cells is sent")
  local d = M.delta(b, a)
  assert(d and d:sub(1, 1) == "D", "...with a delta that carries 254")
  -- tap: the E follows the frame that carries the stand-in, a full frame gets every extended cell
  M.reset(); flag = true; now = now + 5; sent = {}
  M.sys.send = function(x) sent[#sent + 1] = x end
  M.tap(a, nil)
  eq(#sent, 2); eq(sent[1]:sub(1, 1), "F"); eq(sent[2]:sub(1, 1), "E")
  M.tap(b, a)
  eq(#sent, 4); eq(sent[3]:sub(1, 1), "D"); eq(sent[4]:sub(1, 1), "E")
  local plain = T.screen(); plain:text(3, 2, "ab", T.WHITE)
  M.tap(plain, b)
  eq(#sent, 5, "extended cells replaced by ordinary ones: the delta alone, the daemon forgets them"); eq(sent[5]:sub(1, 1), "D")
  T.useMono(false)
end

print("ok: test_mirror.lua")
