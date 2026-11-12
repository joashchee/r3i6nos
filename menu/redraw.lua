-- When the menu draws (the idle menu was 60% of a core on the unit, 2026-11-12, because LÖVE redraws every frame).
-- Event-driven: a frame is drawn only when something changed (an input, a toast appearing or going, the battery read, a
-- window event), or while a screen that moves on its own is up (waiting for a helper, the mixer). Otherwise the loop only
-- pumps events and sleeps IDLE seconds, so a press waits at most that long. Pure: main.lua's love.run calls it.

local M = {}

M.ACTIVE = 1 / 30 -- seconds a frame takes while something is changing
M.IDLE = 1 / 20 -- seconds between looks at the event queue while nothing is
M.SLOW_EVERY = 5 -- a "slow" screen (elapsed seconds, a step bar) draws every this many loops: 4 a second, not 30

-- Events that do not change what is on the screen (the menu reads raw buttons, so a stick moving is not news).
local QUIET = { joystickaxis = true, mousemoved = true, joystickhat = true }

function M.new() return { dirty = true, token = nil, moving = false } end

--- An event came in from LÖVE: true when it should redraw.
function M.event(r, name)
  if QUIET[name] then return false end
  r.dirty = true
  return true
end

--- Something changed that the caller knows about (the battery, a dialog opened by a helper's answer).
function M.mark(r) r.dirty = true end

--- Once a loop, after update. `token` is a string for what the toast shows (same string, nothing to redraw);
--- `moving` is true while a screen animates by itself (the mixer: every loop), or "slow" while it only counts seconds (waiting for a helper). Returns true to draw now, and the seconds to sleep after.
function M.frame(r, token, moving)
  if token ~= r.token then r.token, r.dirty = token, true end
  if moving == "slow" then
    r.slow = (r.slow or 0) + 1
    if r.slow >= M.SLOW_EVERY or r.moving ~= "slow" then r.slow, r.dirty = 0, true end
  else
    r.slow = nil
    moving = moving and true or false
    if moving or moving ~= r.moving then r.dirty = true end -- and once more when it stops: its answer lands inside update
  end
  r.moving = moving
  local draw = r.dirty
  r.dirty = false
  return draw, draw and M.ACTIVE or M.IDLE
end

return M
