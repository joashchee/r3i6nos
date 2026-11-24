function love.conf(t)
    t.identity = "r3i6nos-font-test"
    t.console = false
    t.window = nil
    for _, m in ipairs({ "audio", "event", "graphics", "image", "joystick", "keyboard", "math", "mouse", "physics", "sound", "system", "thread", "touch", "video", "window" }) do
        t.modules[m] = false
    end
    t.modules.font = true
    t.modules.timer = false
end
