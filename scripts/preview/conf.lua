function love.conf(t)
  t.window.width, t.window.height, t.window.vsync = 640, 480, 0
  t.window.title = "harness"
  t.modules.audio, t.modules.physics = false, false
end
