# R36S Hardware Input Checker — Design Document & LÖVE Implementation

## 1. Hardware Overview & Display Constraints
* **Screen Resolution:** $640 \times 480$ pixels (4:3 aspect ratio).
* **Refresh Rate:** 60 FPS target (`t.window.vsync = 1`).
* **Visual Palette:** Dark mode high-contrast palette (Background `#18181b`, Active `#22c55e`, Accents `#38bdf8`).

---

## 2. Controller Mapping Index (SDL / LÖVE Joystick API)

| Physical Input | Input Type | LÖVE Designation |
| :--- | :--- | :--- |
| **D-Pad** | Hat | `hat 1` (`u`, `d`, `l`, `r`, `ul`, `ur`, `dl`, `dr`) |
| **Left Analog Stick** | Axis 1 (X), Axis 2 (Y) | `axis 1`, `axis 2` + Button `leftstick` |
| **Right Analog Stick**| Axis 3 (X), Axis 4 (Y) | `axis 3`, `axis 4` + Button `rightstick` |
| **Face Buttons (ABXY)**| Buttons | Button IDs `1` (A), `2` (B), `3` (X), `4` (Y) |
| **Shoulders & Triggers**| Buttons / Axes | `l1`, `r1`, `l2`, `r2` |
| **System Keys** | Buttons | `select`, `start`, `guide` (FN) |

---

## 3. UI Layout Architecture ($640 \times 480$)

```
+-------------------------------------------------------------------+
|  R36S INPUT DIAGNOSTIC TOOL               [Connected Gamepad]     | (0,0)->(640,45)
+-----------------------------------+-------------------------------+
| VISUAL MATRIX                     | TELEMETRY DATA                |
|                                   |                               |
|   [D-PAD]         (X)             |  Hat (DPad): u                |
|    [U]         (Y)   (B)          |  Axis 1 (LX): -0.02           |
|  [L] [R]          (A)             |  Axis 2 (LY):  0.00           |
|    [D]                            |  Axis 3 (RX):  0.85           |
|                                   |  Axis 4 (RY): -0.10           |
|   (LS)            (RS)            |                               |
|  [  +  ]         [  +  ]          |  Active Buttons: 1, 3         |
+-----------------------------------+-------------------------------+
| Controls: Press ESC or Select+Start to exit.                      | (0,440)->(640,480)
+-------------------------------------------------------------------+
```

---

## 4. LÖVE Project Files

### `conf.lua`
```lua
function love.conf(t)
    t.window.title = "R36S Input Checker"
    t.window.width = 640
    t.window.height = 480
    t.window.resizable = false
    t.window.vsync = 1
    t.modules.audio = false
    t.modules.physics = false
end
```

### `main.lua`
```lua
-- R36S Input Checker for LÖVE 11.x
-- Designed for 640x480 resolution targets (ArkOS / Linux handhelds)

local joystick = nil
local axisValues = {}
local hatState = "c"

function love.load()
    love.graphics.setDefaultFilter("nearest", "nearest")
    local joysticks = love.joystick.getJoysticks()
    if #joysticks > 0 then
        joystick = joysticks[1]
    end
end

function love.joystickadded(j)
    if not joystick then joystick = j end
end

function love.joystickremoved(j)
    if joystick == j then
        joystick = nil
        local joysticks = love.joystick.getJoysticks()
        if #joysticks > 0 then joystick = joysticks[1] end
    end
end

function love.update(dt)
    if joystick then
        for i = 1, joystick:getAxisCount() do
            axisValues[i] = joystick:getAxis(i)
        end
        hatState = joystick:getHat(1)
    end
end

function love.draw()
    love.graphics.clear(0.09, 0.09, 0.11)

    -- Header
    love.graphics.setColor(0.15, 0.15, 0.18)
    love.graphics.rectangle("fill", 0, 0, 640, 45)
    love.graphics.setColor(0.2, 0.8, 0.4)
    love.graphics.setFont(love.graphics.newFont(16))
    love.graphics.print("R36S INPUT DIAGNOSTIC TOOL", 20, 12)

    if not joystick then
        love.graphics.setColor(1, 0.3, 0.3)
        love.graphics.printf("NO GAMEPAD DETECTED. Connect controller or check bindings.", 0, 220, 640, "center")
        return
    end

    love.graphics.setColor(0.6, 0.6, 0.6)
    love.graphics.setFont(love.graphics.newFont(12))
    love.graphics.print("Connected: " .. joystick:getName(), 350, 14)

    drawVisualizer()
    drawTelemetry()

    -- Footer
    love.graphics.setColor(0.25, 0.25, 0.3)
    love.graphics.rectangle("fill", 0, 440, 640, 40)
    love.graphics.setColor(0.8, 0.8, 0.8)
    love.graphics.setFont(love.graphics.newFont(11))
    love.graphics.print("Controls: D-Pad, Sticks, L1/L2/R1/R2, ABXY, Start/Select/FN. Press ESC to exit.", 20, 453)
end

function drawVisualizer()
    -- D-Pad Mockup
    love.graphics.setColor(0.2, 0.2, 0.25)
    love.graphics.rectangle("fill", 70, 140, 90, 90, 8, 8)
    
    local up = (hatState == "u" or hatState == "ul" or hatState == "ur")
    local down = (hatState == "d" or hatState == "dl" or hatState == "dr")
    local left = (hatState == "l" or hatState == "ul" or hatState == "dl")
    local right = (hatState == "r" or hatState == "ur" or hatState == "dr")

    love.graphics.setColor(up and 0.2 or 0.3, up and 0.9 or 0.3, up and 0.4 or 0.3)
    love.graphics.rectangle("fill", 102, 145, 26, 30)
    love.graphics.setColor(down and 0.2 or 0.3, down and 0.9 or 0.3, down and 0.4 or 0.3)
    love.graphics.rectangle("fill", 102, 195, 26, 30)
    love.graphics.setColor(left and 0.2 or 0.3, left and 0.9 or 0.3, left and 0.4 or 0.3)
    love.graphics.rectangle("fill", 75, 172, 30, 26)
    love.graphics.setColor(right and 0.2 or 0.3, right and 0.9 or 0.3, right and 0.4 or 0.3)
    love.graphics.rectangle("fill", 125, 172, 30, 26)

    -- Face Buttons (ABXY)
    local xButton = joystick:isDown(3)
    local aButton = joystick:isDown(1)
    local bButton = joystick:isDown(2)
    local yButton = joystick:isDown(4)

    love.graphics.setColor(0.2, 0.2, 0.25)
    love.graphics.circle("fill", 250, 185, 48)

    drawButtonIndicator(250, 155, "X", xButton)
    drawButtonIndicator(250, 215, "A", aButton)
    drawButtonIndicator(220, 185, "Y", yButton)
    drawButtonIndicator(280, 185, "B", bButton)

    -- Analog Sticks
    local lx, ly = axisValues[1] or 0, axisValues[2] or 0
    love.graphics.setColor(0.2, 0.2, 0.25)
    love.graphics.circle("line", 115, 330, 35)
    love.graphics.setColor(0.3, 0.7, 0.9)
    love.graphics.circle("fill", 115 + (lx * 25), 330 + (ly * 25), 10)
    love.graphics.setColor(0.7, 0.7, 0.7)
    love.graphics.setFont(love.graphics.newFont(10))
    love.graphics.print("Left Stick (L3)", 80, 375)

    local rx, ry = axisValues[3] or 0, axisValues[4] or 0
    love.graphics.setColor(0.2, 0.2, 0.25)
    love.graphics.circle("line", 250, 330, 35)
    love.graphics.setColor(0.9, 0.5, 0.3)
    love.graphics.circle("fill", 250 + (rx * 25), 330 + (ry * 25), 10)
    love.graphics.setColor(0.7, 0.7, 0.7)
    love.graphics.print("Right Stick (R3)", 215, 375)
end

function drawButtonIndicator(x, y, label, active)
    love.graphics.setColor(active and 0.2 or 0.3, active and 0.9 or 0.3, active and 0.4 or 0.3)
    love.graphics.circle("fill", x, y, 13)
    love.graphics.setColor(1, 1, 1)
    love.graphics.setFont(love.graphics.newFont(10))
    love.graphics.printf(label, x - 10, y - 5, 20, "center")
end

function drawTelemetry()
    love.graphics.setColor(0.14, 0.14, 0.18)
    love.graphics.rectangle("fill", 340, 65, 280, 355, 6, 6)

    love.graphics.setColor(0.9, 0.9, 0.9)
    love.graphics.setFont(love.graphics.newFont(12))
    love.graphics.print("TELEMETRY READOUT", 360, 80)

    love.graphics.setFont(love.graphics.newFont(11))
    love.graphics.setColor(0.6, 0.8, 1)
    
    local yOffset = 110
    love.graphics.print(string.format("Hat (DPad): %s", hatState), 360, yOffset)
    
    yOffset = yOffset + 25
    for i = 1, (joystick and joystick:getAxisCount() or 0) do
        love.graphics.print(string.format("Axis %d: %+.2f", i, axisValues[i] or 0), 360, yOffset)
        yOffset = yOffset + 20
    end

    yOffset = yOffset + 15
    love.graphics.print("Pressed Buttons:", 360, yOffset)
    yOffset = yOffset + 20

    local activeButtonsStr = ""
    for i = 1, (joystick and joystick:getButtonCount() or 0) do
        if joystick:isDown(i) then
            activeButtonsStr = activeButtonsStr .. i .. " "
        end
    end
    love.graphics.setColor(0.2, 0.9, 0.4)
    love.graphics.print(activeButtonsStr ~= "" and activeButtonsStr or "None", 360, yOffset)
end

function love.keypressed(key)
    if key == "escape" then
        love.event.quit()
    end
end
```