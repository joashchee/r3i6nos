-- r3i6nos input mapping for LÖVE apps: the Lua twin of theme/r3i6nos_input.h, the reader of docs/r3i6nos-mapping.md. MIT, like the rest of theme/.
-- Pure Lua (no LÖVE calls), so luajit tests it with a fake pad (tests/test_input.lua).
--
-- One reader turns the R36S's raw buttons, sticks and the keyboard into the same actions as the C theme's:
--   * the plain inputs (A accept, B back, X "x", Y "y", Select, Start, L3, R3) and the D-pad;
--   * R1 held is "increase": up/down become pageup/pagedown, left/right home/end;
--   * the system chords: L1+R1 "menu", L2+R2 "context", Fn "panelnext", L1+Fn "panelfirst", R1+Fn "panelprev"
--     (a chord fires when its second input goes down; a modifier alone does nothing; Fn is only ever the panel key);
--   * the right stick as the D-pad, stepping at a rate that follows how far it is pushed (dead zone 0.35, at most 6 steps a second);
--   * the left stick's pointer speed (120 px/s at full, 3 times that with R1 held);
--   * the permanent keyboard bindings (2026-11-23): W S A D the D-pad, I/Backspace X, J/Tab Y, K/Esc B, L/Enter/Space A, F Select, H Start,
--     T Fn, Q L1, E L2, O R1, U R2, R L3, Y R3, backquote the menu, backslash the contextual menu, the arrow keys the right stick.
-- Raw buttons as LÖVE counts them (1-based; the pad's SDL mapping is not its layout, docs/hardware-findings.md): B 1, A 2, X 3, Y 4, L1 5, R1 6,
-- L2 7, R2 8, D-pad 9-12 (up, down, left, right), Select 13, Start 14, L3 15, R3 16, Fn 17; axes 1-2 the left stick, 3-4 the right.
-- The four numbers (0.35, 6, 120, 3) are constants here and in r3i6nos_input.h, nowhere else. Untested on a unit.

local I = {}

I.STICK_DEAD = 0.35   -- of the axis's travel: FM1-NTT's STICK_DEAD
I.STICK_RATE = 6      -- steps a second at full deflection
I.POINTER_PPS = 120   -- pixels a second at full deflection
I.R1_POINTER_X = 3    -- R1 held: the pointer this many times as fast

--- Button names to LÖVE's raw numbers.
I.BTN = { B = 1, A = 2, X = 3, Y = 4, L1 = 5, R1 = 6, L2 = 7, R2 = 8, UP = 9, DOWN = 10, LEFT = 11, RIGHT = 12,
          SELECT = 13, START = 14, L3 = 15, R3 = 16, FN = 17 }
I.NAME = {}
for name, n in pairs(I.BTN) do I.NAME[n] = name end
I.AXIS = { LX = 1, LY = 2, RX = 3, RY = 4 }

--- The keyboard bindings: a LÖVE key to the pad button it stands for (docs/r3i6nos-mapping.md, "The keyboard bindings").
I.KEY_BUTTON = {
  w = "UP", s = "DOWN", a = "LEFT", d = "RIGHT",
  i = "X", backspace = "X",
  j = "Y", tab = "Y",
  k = "B", escape = "B",
  l = "A", ["return"] = "A", kpenter = "A", space = "A",
  f = "SELECT", h = "START", t = "FN",
  q = "L1", e = "L2", o = "R1", u = "R2",
  r = "L3", y = "R3",
}
--- Keys that are an action, not a button: the menus, and the arrow keys (the right stick's steps).
I.KEY_ACTION = { ["`"] = "menu", ["\\"] = "context", up = "up", down = "down", left = "left", right = "right",
                 pageup = "pageup", pagedown = "pagedown", home = "home", ["end"] = "end" }

--- The action's name for a screen or a report.
I.ACTION_NAME = {
  up = "Up", down = "Down", left = "Left", right = "Right", pageup = "Page up", pagedown = "Page down", home = "Home", ["end"] = "End",
  accept = "Accept", back = "Back", x = "X (delete)", y = "Y (more)", select = "Select", start = "Start",
  menu = "Main menu", context = "Context menu", panelnext = "Next panel", panelfirst = "First panel", panelprev = "Previous panel",
  l3 = "L3 (mouse)", r3 = "R3 (keyboard)",
}

local PLAIN = { [3] = "x", [4] = "y", [13] = "select", [14] = "start", [15] = "l3", [16] = "r3", [2] = "accept", [1] = "back" }

--- A new reader.
function I.new()
  return { held = {}, axis = { 0, 0, 0, 0 }, zone = { 0, 0 }, frac = { 0, 0 }, keys = {} }
end

function I.held(s, name) return s.held[I.BTN[name]] == true end

--- A raw button went down (down = true) or up. Returns the action it makes (nil for a release, a modifier on its own or an unknown button).
function I.button(s, b, down)
  if not I.NAME[b] then return nil end
  s.held[b] = down and true or nil
  if not down then return nil end
  local h = s.held
  local l1, r1, l2, r2 = h[5], h[6], h[7], h[8]
  if b == 5 or b == 6 then return (l1 and r1) and "menu" or nil end
  if b == 7 or b == 8 then return (l2 and r2) and "context" or nil end
  if b == 17 then return l1 and "panelfirst" or r1 and "panelprev" or "panelnext" end
  if b == 9 then return r1 and "pageup" or "up" end
  if b == 10 then return r1 and "pagedown" or "down" end
  if b == 11 then return r1 and "home" or "left" end
  if b == 12 then return r1 and "end" or "right" end
  return PLAIN[b]
end

--- A key went down or up: the pad button's action, a menu key's, or nil. Letters held as modifiers (Q E O U) are tracked like the buttons.
--- With r1 held the arrow keys page (Up and Down) like the D-pad. A caller in a text field does not call this for letters.
function I.key(s, key, down)
  local name = I.KEY_BUTTON[key]
  if name then
    s.keys[key] = down and true or nil
    return I.button(s, I.BTN[name], down)
  end
  if not down then return nil end
  local act = I.KEY_ACTION[key]
  if act and s.held[6] then
    act = ({ up = "pageup", down = "pagedown", left = "home", right = "end" })[act] or act
  end
  return act
end

--- Every held button and key forgotten (a window lost focus, a dialog that took the pad): modifiers never stick.
function I.release_all(s)
  s.held, s.keys = {}, {}
  s.zone, s.frac = { 0, 0 }, { 0, 0 }
end

--- An axis moved: axis 1-4, v in -1..1.
function I.axis(s, axis, v)
  if axis >= 1 and axis <= 4 then s.axis[axis] = math.max(-1, math.min(1, v or 0)) end
end

--- Steps a second for a deflection v: 0 inside the dead zone, STICK_RATE at full.
function I.stick_rate(v)
  local m = math.abs(v)
  if m <= I.STICK_DEAD then return 0 end
  if m > 1 then m = 1 end
  return (m - I.STICK_DEAD) / (1 - I.STICK_DEAD) * I.STICK_RATE
end

--- Pixels a second for a deflection v: POINTER_PPS at full, R1_POINTER_X times that with R1 held.
function I.pointer_speed(v, r1)
  local m = math.abs(v)
  if m <= I.STICK_DEAD then return 0 end
  if m > 1 then m = 1 end
  return (m - I.STICK_DEAD) / (1 - I.STICK_DEAD) * I.POINTER_PPS * (r1 and I.R1_POINTER_X or 1)
end

--- The left stick's pointer movement over dt seconds, in pixels (signed).
function I.pointer(s, dt)
  local r1 = s.held[6] == true
  local sx, sy = I.pointer_speed(s.axis[1], r1), I.pointer_speed(s.axis[2], r1)
  return (s.axis[1] < 0 and -sx or sx) * dt, (s.axis[2] < 0 and -sy or sy) * dt
end

--- Whether a stick is past the dead zone now (the loop keeps its short frame while one is, so steps are not late).
function I.stick_active(s)
  for i = 1, 4 do if math.abs(s.axis[i]) > I.STICK_DEAD then return true end end
  return false
end

--- Once a frame: the right stick's steps over dt seconds, as a list of actions. Up and down step at the stick's rate (pageup and pagedown with
--- R1 held); left and right likewise, but with R1 held fire home or end once for each push past the dead zone. A push steps at once.
function I.tick(s, dt)
  local out = {}
  if dt > 0.25 then dt = 0.25 end   -- a stalled frame must not turn into a burst of steps
  local r1 = s.held[6] == true
  for k = 1, 2 do
    local v = s.axis[k == 1 and 3 or 4]
    local z = v <= -I.STICK_DEAD and -1 or (v >= I.STICK_DEAD and 1 or 0)
    local skip = false
    if z ~= s.zone[k] then
      s.zone[k] = z
      s.frac[k] = z ~= 0 and 1 or 0     -- a new push steps at once
      if z ~= 0 and k == 1 and r1 then  -- home or end: once for the push, not repeated
        out[#out + 1] = z < 0 and "home" or "end"
        s.frac[k] = 0
        skip = true
      end
    elseif z ~= 0 and not (k == 1 and r1) then
      s.frac[k] = s.frac[k] + dt * I.stick_rate(v)
    end
    if not skip and z ~= 0 and not (k == 1 and r1) then
      local act
      if k == 1 then act = z < 0 and "left" or "right"
      elseif r1 then act = z < 0 and "pageup" or "pagedown"
      else act = z < 0 and "up" or "down" end
      while s.frac[k] >= 1 and #out < 8 do out[#out + 1] = act; s.frac[k] = s.frac[k] - 1 end
      if s.frac[k] >= 1 then s.frac[k] = 0.99 end
    end
  end
  return out
end

--- The list rule on a 1-based selection: up and down wrap, pageup and pagedown move a page and stop at the ends, home and end jump.
--- Left and right are the app's, so they do nothing here. Returns the new selection.
function I.list_step(sel, n, per, action)
  if n <= 0 then return 0 end
  if per < 1 then per = 1 end
  if action == "up" then return (sel - 2) % n + 1
  elseif action == "down" then return sel % n + 1
  elseif action == "pageup" then return math.max(1, sel - per)
  elseif action == "pagedown" then return math.min(n, sel + per)
  elseif action == "home" then return 1
  elseif action == "end" then return n end
  return sel
end

return I
