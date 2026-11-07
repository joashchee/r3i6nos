-- r3i6nos menu: one list of every LÖVE game and native app on the card.
-- Up/Down move, A / Enter launches, PgUp/PgDn (L/R) change page, Start / S opens Settings. Never scrolls.

local scan = require("scan")
local settings = require("settings")

local ROMS = os.getenv("R3I6NOS_ROMS") or "/roms"
local LAUNCHERS = os.getenv("R3I6NOS_LAUNCHERS") or "/opt/launchers/native"
local DEFAULTS = os.getenv("R3I6NOS_DEFAULTS") or "/opt/loveos/defaults"
local NEXT = os.getenv("R3I6NOS_NEXT") or "/run/loveos/next"
local PER_PAGE = 12

local entries, selected = {}, 1
local font
local dialog = settings.new()
local last_action, last_time = nil, -1

function love.load()
  love.mouse.setVisible(false)
  font = love.graphics.newFont(20)
  love.graphics.setFont(font)
  entries = scan.entries(ROMS, LAUNCHERS, DEFAULTS)
end

local function launch()
  local e = entries[selected]
  if not e then return end
  local f = io.open(NEXT, "w")
  if f then
    f:write(e.type, "\t", e.target, "\n")
    f:close()
  end
  love.event.quit()
end

-- Ask the session loop to power the device off: it runs "system<TAB>poweroff", then exits cleanly.
local function poweroff()
  local f = io.open(NEXT, "w")
  if f then
    f:write("system\tpoweroff\n")
    f:close()
  end
  love.event.quit()
end

local function move(delta)
  if #entries == 0 then return end
  selected = (selected - 1 + delta) % #entries + 1
end

local function page(delta)
  if #entries == 0 then return end
  selected = math.max(1, math.min(#entries, selected + delta * PER_PAGE))
end

-- One place for every input. A pad can report the same press as a gamepad button and as a raw
-- joystick button (the R36S's SDL mapping does not match its raw layout), so the same action
-- twice within 50 ms counts once.
local function act(action)
  local now = love.timer.getTime()
  if action == last_action and now - last_time < 0.05 then return end
  last_action, last_time = action, now
  if settings.isOpen(dialog) then
    if settings.press(dialog, action) == "poweroff" then poweroff() end
    return
  end
  if action == "up" then move(-1)
  elseif action == "down" then move(1)
  elseif action == "pageup" then page(-1)
  elseif action == "pagedown" then page(1)
  elseif action == "accept" then launch()
  elseif action == "settings" then settings.open(dialog)
  end
end

local KEYS = {
  up = "up", down = "down", pageup = "pageup", pagedown = "pagedown",
  ["return"] = "accept", z = "accept", escape = "back", x = "back",
  s = "settings", f1 = "settings",
}
local GAMEPAD = {
  dpup = "up", dpdown = "down", leftshoulder = "pageup", rightshoulder = "pagedown",
  a = "accept", b = "back", start = "settings",
}
-- The real R36S layout (docs/hardware-findings.md; L1/R1 confirmed): D-pad up 9 / down 10, A 2, B 1, Start 14.
local RAW = { [9] = "up", [10] = "down", [2] = "accept", [1] = "back", [5] = "pageup", [6] = "pagedown", [14] = "settings" }

function love.keypressed(key)
  if KEYS[key] then act(KEYS[key]) end
end

function love.gamepadpressed(_, button)
  if GAMEPAD[button] then act(GAMEPAD[button]) end
end

function love.joystickpressed(_, button)
  if RAW[button] then act(RAW[button]) end
end

local function draw_dialog()
  love.graphics.setColor(0, 0, 0, 0.85)
  love.graphics.rectangle("fill", 0, 0, 640, 480)
  love.graphics.setColor(1, 1, 1)
  love.graphics.rectangle("line", 80, 100, 480, 260)
  love.graphics.print("Settings", 100, 112)
  if dialog.view == "confirm" then
    love.graphics.print("Shut down the device?", 100, 170)
    for i, label in ipairs({ "Yes, shut down", "No" }) do
      local y = 230 + (i - 1) * 40
      if i == dialog.confirm then
        love.graphics.rectangle("fill", 96, y - 2, 448, 32)
        love.graphics.setColor(0, 0, 0)
      end
      love.graphics.print(label, 108, y)
      love.graphics.setColor(1, 1, 1)
    end
    love.graphics.print("A: choose   B: back", 100, 322)
    return
  end
  for i, item in ipairs(settings.ITEMS) do
    local y = 160 + (i - 1) * 40
    if i == dialog.selected then
      love.graphics.rectangle("fill", 96, y - 2, 448, 32)
      love.graphics.setColor(0, 0, 0)
    end
    love.graphics.print(item.label, 108, y)
    love.graphics.printf(item.hint, 108, y, 424, "right")
    love.graphics.setColor(1, 1, 1)
  end
  love.graphics.print("A: choose   B: close", 100, 322)
end

function love.draw()
  love.graphics.print("r3i6nos", 16, 12)
  if #entries == 0 then
    love.graphics.print("Nothing here yet.", 16, 60)
    love.graphics.print("Drop a .love into Love 11.5 or Love 12.0,", 16, 90)
    love.graphics.print("or an ARM64 .deb into native_deb, and reboot.", 16, 114)
    love.graphics.print("Start: Settings", 16, 446)
    if settings.isOpen(dialog) then draw_dialog() end
    return
  end
  local pages = math.ceil(#entries / PER_PAGE)
  local current = math.ceil(selected / PER_PAGE)
  local first = (current - 1) * PER_PAGE + 1
  for i = first, math.min(#entries, first + PER_PAGE - 1) do
    local y = 50 + (i - first) * 30
    if i == selected then
      love.graphics.rectangle("fill", 8, y - 2, 624, 28)
      love.graphics.setColor(0, 0, 0)
    end
    love.graphics.print(entries[i].title, 16, y)
    love.graphics.printf(entries[i].type, 16, y, 608, "right")
    love.graphics.setColor(1, 1, 1)
  end
  love.graphics.print(("Page %d/%d"):format(current, pages), 16, 446)
  love.graphics.printf("Start: Settings", 16, 446, 608, "right")
  if settings.isOpen(dialog) then draw_dialog() end
end
