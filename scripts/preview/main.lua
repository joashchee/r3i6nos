-- Preview harness for scripts/preview_input_checker.sh (a development tool, not part of the image).
-- Runs the real input checker under a real LÖVE with a fake gamepad (the measured R36S layout, a few
-- buttons held, a few events logged), writes one screenshot to CHECKER_OUT, then quits.
local APP, OUT = os.getenv("CHECKER_DIR"), os.getenv("CHECKER_OUT")
assert(APP and OUT, "CHECKER_DIR and CHECKER_OUT must be set (use scripts/preview_input_checker.sh)")
package.path = APP .. "/?.lua;" .. package.path

local raw = { [11] = true, [12] = true, [15] = true, [13] = true, [1] = true, [5] = true } -- up, right, L3, Fn, A, L1
local pad = {
  getName = function() return "retrogame_joypad (fake)" end,
  getGUID = function() return "190000004b4800000010000001010000" end,
  isGamepad = function() return false end,
  isGamepadDown = function() return false end,
  getGamepadAxis = function() return 0 end,
  getAxisCount = function() return 4 end, getButtonCount = function() return 17 end, getHatCount = function() return 0 end,
  getAxis = function(_, i) return ({ 0.67, -0.27, -0.09, 0.52 })[i] or 0 end,
  getHat = function() return "c" end,
  isDown = function(_, i) return raw[i] == true end,
}
love.joystick.getJoysticks = function() return { pad } end
dofile(APP .. "/main.lua")

local appLoad, appDraw = love.load, love.draw
local frames = 0
function love.load()
  appLoad()
  love.joystickpressed(pad, 11); love.joystickpressed(pad, 15); love.joystickaxis(pad, 1, 0.9)
  love.joystickreleased(pad, 5); love.keypressed("volumeup"); love.joystickpressed(pad, 1)
end
function love.draw()
  appDraw()
  frames = frames + 1
  if frames == 3 then
    love.graphics.captureScreenshot(function(img)
      local f = assert(io.open(OUT, "wb"))
      f:write(img:encode("png"):getString())
      f:close()
      print(("preview: wrote %s (LÖVE %s)"):format(OUT, love._version))
      love.event.quit()
    end)
  end
end
