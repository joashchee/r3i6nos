-- Preview harness for scripts/preview_io_checker.sh (a development tool, not part of the image).
-- Runs the real IO Checker under a real LÖVE with a fake gamepad (the measured R36S layout, a few buttons held, a few
-- events logged), opens the page CHECKER_PAGE names, writes one screenshot to CHECKER_OUT, then quits.
local APP, OUT, PAGE = os.getenv("CHECKER_DIR"), os.getenv("CHECKER_OUT"), os.getenv("CHECKER_PAGE") or "home"
assert(APP and OUT, "CHECKER_DIR and CHECKER_OUT must be set (use scripts/preview_io_checker.sh)")
package.path = APP .. "/?.lua;" .. package.path

local raw = {}
local pad = {
  getName = function() return "GO-Super Gamepad (fake)" end,
  getGUID = function() return "1900bb3e4b4800000011000000010000" end,
  isGamepad = function() return false end, isGamepadDown = function() return false end, isConnected = function() return true end,
  getAxisCount = function() return 4 end, getButtonCount = function() return 17 end, getHatCount = function() return 0 end,
  getAxis = function(_, i) return ({ 0.67, -0.27, -0.09, 0.52 })[i] or 0 end,
  getHat = function() return "c" end,
  isDown = function(_, i) return raw[i] == true end,
}
love.joystick.getJoysticks = function() return { pad } end
dofile(APP .. "/main.lua")

local function press(b) raw[b] = true; love.joystickpressed(pad, b); raw[b] = nil; love.joystickreleased(pad, b) end
local DOWNS = { home = nil, input = 0, video = 1, sound = 2, files = 3, system = 4 }

local appLoad, appDraw = love.load, love.draw
local frames = 0
function love.load()
  appLoad()
  local n = DOWNS[PAGE]
  if n then
    for _ = 1, n do press(10) end
    press(2)
  end
  if PAGE == "input" then
    press(9); press(15); love.joystickaxis(pad, 1, 0.9); love.keypressed("volumeup")
    raw[9], raw[12], raw[15], raw[2] = true, true, true, true -- up, right, L3, A held for the picture
  end
end
function love.draw()
  appDraw()
  frames = frames + 1
  if frames == 5 then
    love.graphics.captureScreenshot(function(img)
      local f = assert(io.open(OUT, "wb"))
      f:write(img:encode("png"):getString())
      f:close()
      print(("preview: wrote %s (LÖVE %s)"):format(OUT, love._version))
      love.event.quit()
    end)
  end
end
