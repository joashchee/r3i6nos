-- Smoke test config: no window, no GL, no audio device. This checks that the
-- runtime links and its non-graphical modules work, not that it draws.
function love.conf(t)
  t.identity = "r3i6nos-smoke"
  t.console = false
  for _, m in ipairs({ "window", "graphics", "audio", "joystick", "video", "touch", "mouse", "keyboard" }) do
    t.modules[m] = false
  end
end
