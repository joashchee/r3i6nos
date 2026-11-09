-- r3i6nos menu: one list of every LÖVE game and native app on the card, in the r3i6nos-ansiapps theme
-- (theme/, docs/R3I6NOS-ANSIAPPS-THEME.md): 64x24 text cells, a top bar, the list, a three-row command line.
-- D-pad up/down move, A launches, L1/R1 (or left/right) turn a page, L2/R2 jump to the top/end, Start opens
-- Settings (S / F1 on a keyboard): Network, SSH, Battery, Sound mixer, Date and time, reports, About and Shutdown. The battery shows at the top right. Never scrolls.

package.path = (os.getenv("R3I6NOS_THEME") or "/opt/loveos/theme") .. "/?.lua;" .. package.path

local scan = require("scan")
local battery = require("battery")
local netclient = require("netclient")
local settings = require("settings")
local about = require("about")
local mixer = require("mixer")
local theme = require("r3i6nos_ansiapps")
local view = require("view")

local ROMS = os.getenv("R3I6NOS_ROMS") or "/roms"
local ROMS2 = os.getenv("R3I6NOS_ROMS2") or "/roms2" -- the data card (docs/STORAGE.md)
local LAUNCHERS = os.getenv("R3I6NOS_LAUNCHERS") or "/opt/launchers/native"
local DEFAULTS = os.getenv("R3I6NOS_DEFAULTS") or "/opt/loveos/defaults"
local NEXT = os.getenv("R3I6NOS_NEXT") or "/run/loveos/next"

local entries, selected = {}, 1
local dialog = settings.new(netclient, battery)

-- Settings > About: what only LÖVE knows. The pads' GUIDs are what the SDL mapping in run_app.sh needs (still unread on a unit).
about.extra = function()
  local ma, mi, re = love.getVersion()
  local lines = { "MENU", ("%-13s%s"):format("Runs on", ("LÖVE %d.%d.%d"):format(ma, mi, re)) }
  local w, h = love.graphics.getDimensions()
  lines[#lines + 1] = ("%-13s%dx%d"):format("Screen", w, h)
  local pads = love.joystick.getJoysticks()
  lines[#lines + 1] = ("%-13s%d"):format("Pads", #pads)
  for i, j in ipairs(pads) do
    lines[#lines + 1] = ("  %d %s"):format(i, j:getName()):sub(1, 60)
    lines[#lines + 1] = ("    %s"):format(j:getGUID())
    lines[#lines + 1] = ("    %d buttons, %d axes, %d hats"):format(j:getButtonCount(), j:getAxisCount(), j:getHatCount())
  end
  return lines
end
local screen
local power, powerAge = nil, 0 -- the battery reading for the top bar, re-read every few seconds
local POWER_EVERY = 10

function love.load()
  love.mouse.setVisible(false)
  theme.init()
  screen = theme.screen()
  entries = scan.entries(ROMS, LAUNCHERS, DEFAULTS, ROMS2)
  power = battery.read()
  if require("clock").needed() then settings.openClock(dialog) end -- the card's first boot: ask for the date and time (docs/CLOCK.md)
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

-- Settings > Sound mixer's test sound: made by mixer.sample, looped while that screen is up and the sound is not Off.
local playing, playingKind, made = nil, 1, {} -- made: each sound's Source, built once (the music takes a moment on the unit)
local function testSound()
  local n = settings.isOpen(dialog) and settings.node(dialog)
  local want = (n and n.kind == "mixer") and n.sound or 1
  if want == playingKind then return end
  if playing then playing:stop(); playing = nil end
  playingKind = want
  if want == 1 then return end
  if not made[want] then
    local frames = mixer.frames(want)
    local data = love.sound.newSoundData(frames, mixer.RATE, 16, 2)
    for i = 0, frames - 1 do
      local l, r = mixer.sample(want, i)
      data:setSample(i, 1, l)
      data:setSample(i, 2, r)
    end
    made[want] = love.audio.newSource(data, "static")
    made[want]:setLooping(true)
  end
  playing = made[want]
  playing:play()
end

function love.update(dt)
  settings.tick(dialog, dt)
  testSound()
  powerAge = powerAge + dt
  if powerAge >= POWER_EVERY then powerAge, power = 0, battery.read() end
end

local function typing()
  local n = settings.isOpen(dialog) and settings.node(dialog)
  return n and n.kind == "osk"
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
  if typing() then -- letters are text here, not commands
    if key == "backspace" then settings.backspace(dialog)
    elseif key == "return" or key == "kpenter" then act("more")
    elseif key == "escape" then act("back")
    elseif key == "up" or key == "down" or key == "left" or key == "right" then act(key) end
    return
  end
  if theme.KEYS[key] then act(theme.KEYS[key]) end
end

function love.textinput(text)
  if typing() then settings.text(dialog, text) end
end

-- Raw buttons only: this pad's SDL gamepad mapping does not match its real layout (docs/hardware-findings.md).
function love.joystickpressed(_, button)
  if theme.PAD[button] then act(theme.PAD[button]) end
end

function love.draw()
  view.render(screen, { entries = entries, selected = selected, dialog = dialog, battery = power })
  theme.draw(screen)
end
