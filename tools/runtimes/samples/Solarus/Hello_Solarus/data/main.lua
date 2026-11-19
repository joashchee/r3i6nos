-- Hello_Solarus: the r3i6nos sample for the Solarus runtime (docs/RUNTIMES.md). Original, public domain (CC0).
-- A quest with no maps or fonts: it draws a square with surfaces. Arrow keys or the d-pad move it, any button changes its colour, Start (or Escape) quits.
-- R3I6NOS_SAMPLE_AUTOQUIT=1 quits at once (a self-test hook; unset on the unit).
local screen_w, screen_h = 320, 240
local x, y, step = 150, 110, 4
local colors = { {255, 80, 80}, {80, 255, 80}, {80, 160, 255}, {255, 220, 60} }
local c = 1
local square = sol.surface.create(20, 20)
local background = sol.surface.create(screen_w, screen_h)
background:fill_color({16, 16, 48})

local function recolor()
  square:fill_color(colors[c])
end
recolor()

function sol.main:on_started()
  if os.getenv("R3I6NOS_SAMPLE_AUTOQUIT") then sol.main.exit() end
end

function sol.main:on_draw(screen)
  background:draw(screen)
  square:draw(screen, x, y)
end

function sol.main:on_key_pressed(key)
  if key == "escape" then sol.main.exit() return true end
  if key == "left" then x = math.max(0, x - step) elseif key == "right" then x = math.min(screen_w - 20, x + step)
  elseif key == "up" then y = math.max(0, y - step) elseif key == "down" then y = math.min(screen_h - 20, y + step)
  else c = c % #colors + 1; recolor() end
  return true
end

function sol.main:on_joypad_hat_moved(hat, direction8)
  local dx = { [0] = 1, 0, -1, -1, -1, 0, 1, 1 }
  local dy = { [0] = 0, -1, -1, 0, 1, 1, 1, 0 }
  if direction8 >= 0 then
    x = math.min(screen_w - 20, math.max(0, x + dx[direction8] * step * 2))
    y = math.min(screen_h - 20, math.max(0, y + dy[direction8] * step * 2))
  end
  return true
end

function sol.main:on_joypad_button_pressed(button)
  if button == 11 or button == 7 then sol.main.exit() return true end
  c = c % #colors + 1
  recolor()
  return true
end
