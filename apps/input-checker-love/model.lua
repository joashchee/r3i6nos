-- Pure logic of the input checker (no LÖVE calls), so it is tested with plain luajit:
-- tests/test_input_checker.lua.

local M = {}

M.ZONE = 0.5            -- an axis counts as "pushed" past this
M.EXIT_HOLD = 1.0       -- seconds Select+Start must be held
M.IDLE_LIMIT = 15       -- seconds without input before the app leaves by itself
M.IDLE_WARN = 10        -- ... and starts counting down on screen

-- The R36S's raw button numbers as LÖVE reports them (1-based; SDL's own index is one less). Measured on
-- a real unit on 2026-11-07: the D-pad is four buttons, not a hat, and the pad has no SDL gamepad mapping.
-- l1, r1, l2, r2 are a guess (the numbers left over): the telemetry shows the truth.
M.LAYOUT = {
  a = 1, b = 2, x = 3, y = 4,
  l1 = 5, r1 = 6, l2 = 7, r2 = 8,          -- assumed
  left = 9, down = 10, up = 11, right = 12,
  fn = 13, select = 14, l3 = 15, r3 = 16, start = 17,
}

-- Power and the volume keys are not on the joystick (17 buttons, all taken): they arrive as keyboard keys.
M.KEYS = {
  vol_down = { "volumedown", "audiolower" },
  vol_up = { "volumeup", "audioraise" },
  power = { "power", "sleep" },
}

--- Hat direction string ("u", "dl", "c", ...) to four booleans.
function M.hat(dir)
  dir = dir or "c"
  return {
    up = dir:find("u", 1, true) ~= nil,
    down = dir:find("d", 1, true) ~= nil,
    left = dir:find("l", 1, true) ~= nil,
    right = dir:find("r", 1, true) ~= nil,
  }
end

--- Which D-pad directions are pressed: the four buttons, or a hat if the pad has one.
--- down(i) says whether raw button i is held.
function M.dpad(hatDir, down)
  local h = M.hat(hatDir)
  local L = M.LAYOUT
  return {
    up = h.up or down(L.up), down = h.down or down(L.down),
    left = h.left or down(L.left), right = h.right or down(L.right),
  }
end

--- Is Select+Start held? Raw buttons first (the pad has no mapping); the SDL mapping also counts.
--- gamepad(name) is optional: nil when the pad has no mapping.
function M.exitHeld(down, gamepad)
  if down(M.LAYOUT.select) and down(M.LAYOUT.start) then return true end
  return gamepad ~= nil and gamepad("back") and gamepad("start") or false
end

--- Is one of these key names down? keys is the set of names currently held.
function M.keyHeld(held, group)
  for _, name in ipairs(M.KEYS[group]) do
    if held[name] then return true end
  end
  return false
end

--- One line of the event log. isHeld is what the pad says right now: a DOWN event with "free" (or an UP
--- with "held") would mean the event stream and the state disagree.
function M.eventLine(seq, what, isHeld)
  if isHeld == nil then return ("#%d %s"):format(seq, what) end   -- keys have no state to compare with
  return ("#%d %s (%s)"):format(seq, what, isHeld and "held" or "free")
end

--- -1, 0 or 1: which side of the zone an axis value is on.
function M.zone(v)
  if v <= -M.ZONE then return -1 end
  if v >= M.ZONE then return 1 end
  return 0
end

function M.fmt(v) return string.format("%+.2f", v or 0) end

--- Cuts a string to fit n characters, with an ellipsis.
function M.clip(s, n)
  if #s <= n then return s end
  return s:sub(1, n - 3) .. "..."
end

--- Last few input events, newest first.
function M.newLog(max)
  local log = { max = max, items = {} }
  function log:add(text)
    table.insert(self.items, 1, text)
    while #self.items > self.max do table.remove(self.items) end
  end
  return log
end

--- Which raw buttons, hat directions and axes have ever been used: the checklist of a hardware test.
function M.newSeen()
  local seen = { buttons = {}, axes = {}, hat = {}, keys = {} }
  function seen:button(i) self.buttons[i] = true end
  function seen:axis(i) self.axes[i] = true end
  function seen:hatdir(d) if d and d ~= "c" then self.hat[d] = true end end
  function seen:key(k) self.keys[k] = true end
  function seen:count(t)
    local n = 0
    for _ in pairs(self[t]) do n = n + 1 end
    return n
  end
  return seen
end

--- Exit chord: held() must stay true for EXIT_HOLD seconds in a row.
function M.newChord(hold)
  local c = { hold = hold or M.EXIT_HOLD, t = 0 }
  function c:update(dt, held)
    if held then self.t = self.t + dt else self.t = 0 end
    return self.t >= self.hold
  end
  function c:progress() return math.min(1, self.t / self.hold) end
  return c
end

--- Leaves by itself after limit seconds with no input, so a unit with an unmapped Select/Start cannot get stuck.
function M.newIdle(limit, warn)
  local idle = { limit = limit or M.IDLE_LIMIT, warn = warn or M.IDLE_WARN, t = 0 }
  function idle:touch() self.t = 0 end
  function idle:update(dt) self.t = self.t + dt end
  function idle:remaining() return math.max(0, self.limit - self.t) end
  function idle:warning() return self:remaining() <= self.warn end
  function idle:expired() return self.t >= self.limit end
  return idle
end

--- Runtime name for the header: "LÖVE 11.5.0" from the version strings LÖVE gives.
function M.runtime(version, major, minor, revision)
  if major then return ("LÖVE %d.%d.%d"):format(major, minor or 0, revision or 0) end
  return "LÖVE " .. tostring(version or "?")
end

return M
