-- Hello_Love for LÖVE 12.0: 640x480, the R36S screen. CC0.
function love.conf(t)
  t.identity = "r3i6nos-hello-love"
  t.version = "12.0"
  t.window.title = "Hello LÖVE"
  t.window.width = 640
  t.window.height = 480
  t.modules.physics = false
  t.modules.video = false
end
