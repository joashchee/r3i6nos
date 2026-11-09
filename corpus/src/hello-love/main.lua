-- r3i6nos test corpus: the smallest LÖVE app that shows it ran. Any button or key leaves (back to the r3i6nos menu).
-- Runs on LÖVE 11.5 and 12.0. Made for testing; no one else's content.
local shown = 0

function love.update(dt) shown = shown + dt end

function love.draw()
  love.graphics.clear(0, 0, 0.67)
  love.graphics.setColor(1, 1, 1)
  local major, minor = love.getVersion()
  love.graphics.print(("r3i6nos test corpus: a .love app\nLOVE %d.%d, %.0f s\n\nPress any button to go back."):format(major, minor, shown), 40, 40, 0, 2, 2)
end

function love.keypressed() love.event.quit() end
function love.joystickpressed() love.event.quit() end
