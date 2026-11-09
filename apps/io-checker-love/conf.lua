-- IO Checker: one app, packed twice (Love 11.5/ and Love 12.0/); it says which runtime it runs on. docs/IO-CHECKER.md.
function love.conf(t)
  t.identity = "r36s-io-checker"
  t.window.title = "IO Checker"
  t.window.width = 640
  t.window.height = 480
  t.window.resizable = false
  t.window.fullscreen = true
  t.window.fullscreentype = "desktop"
  t.window.vsync = 1
  t.modules.audio = true      -- the sound page's test tones
  t.modules.sound = true
  t.modules.physics = false
  t.modules.video = false
  t.modules.touch = false
  t.modules.thread = false
end
