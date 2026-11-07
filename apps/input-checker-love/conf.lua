-- R36S Input Checker: one app, packed twice (Love 11.5/ and Love 12.0/); it prints the runtime it runs on.
function love.conf(t)
  t.identity = "r36s-input-checker"
  t.window.title = "R36S Input Checker"
  t.window.width = 640
  t.window.height = 480
  t.window.resizable = false
  t.window.fullscreen = true
  t.window.fullscreentype = "desktop"
  t.window.vsync = 1
  t.modules.audio = false
  t.modules.physics = false
  t.modules.video = false
  t.modules.touch = false
  t.modules.thread = false
end
