function love.conf(t)
  t.identity = "r3i6nos-menu"
  t.version = "11.5"
  t.window.title = "r3i6nos"
  t.window.width = 640
  t.window.height = 480
  t.window.fullscreen = true
  t.window.fullscreentype = "desktop"
  t.modules.physics = false
  t.modules.video = false
  t.modules.touch = false
  t.modules.thread = false
  -- LÖVE's audio module opens the sound device and starts OpenAL's mixer thread at once: 43 wake-ups a second mixing silence, 6 to 8%
  -- of a core on the idle menu, with the PCM held open (measured on the unit over SSH, 2026-11-15; ROADMAP section 23). The menu's only sound
  -- is Settings > Sound mixer's test sound, so main.lua loads the module the first time that is wanted (love.sound, which makes the
  -- samples and has no device, stays on).
  t.modules.audio = false
end
