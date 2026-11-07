-- r3i6nos menu: one list of every LÖVE game and native app on the card, in the r3i6nos-ansiapps theme
-- (theme/, docs/R3I6NOS-ANSIAPPS-THEME.md): 64x24 text cells, a top bar, the list, a three-row command line.
-- D-pad up/down move, A launches, L1/R1 (or left/right) turn a page, L2/R2 jump to the top/end, Start opens
-- Settings (S / F1 on a keyboard). Never scrolls.

package.path = (os.getenv("R3I6NOS_THEME") or "/opt/loveos/theme") .. "/?.lua;" .. package.path

local scan = require("scan")
local settings = require("settings")
local theme = require("r3i6nos_ansiapps")
local view = require("view")

local ROMS = os.getenv("R3I6NOS_ROMS") or "/roms"
local LAUNCHERS = os.getenv("R3I6NOS_LAUNCHERS") or "/opt/launchers/native"
local DEFAULTS = os.getenv("R3I6NOS_DEFAULTS") or "/opt/loveos/defaults"
local NEXT = os.getenv("R3I6NOS_NEXT") or "/run/loveos/next"

local entries, selected = {}, 1
local dialog = settings.new()
local screen

function love.load()
  love.mouse.setVisible(false)
  theme.init()
  screen = theme.screen()
  entries = scan.entries(ROMS, LAUNCHERS, DEFAULTS)
end

local function request(line)
  local f = io.open(NEXT, "w")
  if f then
    f:write(line, "\n")
    f:close()
  end
  love.event.quit()
end

local function launch()
  local e = entries[selected]
  if e then request(e.type .. "\t" .. e.target) end
end

-- Ask the session loop to power the device off: it runs "system<TAB>poweroff", then exits cleanly.
local function poweroff() request("system\tpoweroff") end

-- One place for every input (a list dialog's actions: up, down, left, right, pageup, pagedown, home, end, accept, back, more).
local function act(action)
  if settings.isOpen(dialog) then
    if settings.press(dialog, action) == "poweroff" then poweroff() end
    return
  end
  local new, event = theme.listKey(selected, #entries, view.PER_PAGE, action)
  selected = math.max(1, new)
  if event == "accept" then launch()
  elseif event == "more" then settings.open(dialog) end
end

function love.keypressed(key)
  if theme.KEYS[key] then act(theme.KEYS[key]) end
end

-- Raw buttons only: this pad's SDL gamepad mapping does not match its real layout (docs/hardware-findings.md).
function love.joystickpressed(_, button)
  if theme.PAD[button] then act(theme.PAD[button]) end
end

function love.draw()
  view.render(screen, { entries = entries, selected = selected, dialog = dialog })
  theme.draw(screen)
end
