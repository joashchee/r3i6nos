-- r3i6nos menu: one list of every LÖVE game and native app on the card, in the r3i6nos-ansiapps theme
-- (theme/, docs/R3I6NOS-ANSIAPPS-THEME.md): 64x24 text cells, a top bar, the list, a three-row command line.
-- The input is the mapping's (docs/r3i6nos-mapping.md, theme/r3i6nos_input.lua): D-pad or right stick move, A launches, R1 held pages (up/down)
-- and goes to the first or last game (left/right), Y or L1 + R1 (backquote on a keyboard) opens the main menu: About, Colour theme, Keymap,
-- Device report, Rec demo, Quit (it powers the unit off), then Network, SSH, Battery, Sound mixer, Date and time, reports and the cards.
-- The keyboard is the permanent map (W A S D, I J K L, ...). No button or key quits: the main menu's Quit does. The battery shows at the top right. Never scrolls.

package.path = (os.getenv("R3I6NOS_THEME") or "/opt/loveos/theme") .. "/?.lua;" .. package.path

local scan = require("scan")
local battery = require("battery")
local netstatus = require("netstatus")
local netclient = require("netclient")
local settings = require("settings")
local about = require("about")
local mixer = require("mixer")
local theme = require("r3i6nos_ansiapps")
local input = require("r3i6nos_input") -- the mapping's reader: buttons, chords, the right stick, the keyboard (docs/r3i6nos-mapping.md)
local pad = input.new()
local toast = require("r3i6nos_toast")
local hotplug = toast.new() -- the hot-plug toast over the top bar: headphones, USB, DC power (docs/HOTPLUG.md)
local view = require("view")
local redraw = require("redraw")
local rd = redraw.new() -- what needs drawing (menu/redraw.lua)
local pkgflag = require("pkgflag") -- r3i6nos-pkg left a change (docs/PACKAGES-OTA.md): read the cards again
local pkgs = pkgflag.new(os.getenv("R3I6NOS_PKG_FLAG") or "/run/loveos/pkg-changed")
local sysconfirm = require("sysconfirm") -- after a system update: the first full screen tells the boot step it worked (docs/PACKAGES-OTA.md)
local sysmark = sysconfirm.new()

local ROMS = os.getenv("R3I6NOS_ROMS") or "/roms"
local PKG_PROGRESS = os.getenv("R3I6NOS_PKG_PROGRESS") or "/run/loveos/pkg-progress" -- deb_watcher's bar while it installs (rule 18)
local pkgProgress, pkgProgressAge, pkgProgressShown = nil, 0, ""
local ROMS2 = os.getenv("R3I6NOS_ROMS2") or "/roms2" -- the data card (docs/STORAGE.md)
local LAUNCHERS = os.getenv("R3I6NOS_LAUNCHERS") or "/opt/launchers/native"
local DEFAULTS = os.getenv("R3I6NOS_DEFAULTS") or "/opt/loveos/defaults"
local NEXT = os.getenv("R3I6NOS_NEXT") or "/run/loveos/next"

local entries, selected = {}, 1
local dialog = settings.new(netclient, battery)
dialog.input = pad -- the Keymap test shows what the reader holds

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
local net = nil -- the link for the top bar, read with the battery

-- The first screen goes up before anything slow runs (the card scan forks `ls` for every folder): a black panel for
-- seconds feels like a hang. Shown twice, so both buffers of the display hold it.
local build -- /etc/r3i6nos-release's keys, for the splash's version line (rule 12)
local function readBuild()
  local t = {}
  local f = io.open(os.getenv("R3I6NOS_RELEASE") or "/etc/r3i6nos-release", "r")
  if f then
    for line in f:lines() do local k, v = line:match("^(%u[%u_]*)=(.*)$"); if k then t[k] = v end end
    f:close()
  end
  return t
end

local function splash(done, total, doing)
  view.splash(screen, done, total, doing, build)
  for _ = 1, 2 do
    love.graphics.origin()
    love.graphics.clear(love.graphics.getBackgroundColor())
    theme.draw(screen)
    love.graphics.present()
  end
end

function love.load()
  love.mouse.setVisible(false)
  theme.init()
  theme.useMono(true)   -- R3i6nos Mono: the top bar's battery and Wi-Fi are glyphs, and a title with a letter CP437 lacks is drawn (the table loads on the first glyph)
  screen = theme.screen()
  build = readBuild()
  splash(0, 3, "Starting")
  -- Packages waiting to be extracted (a new card's first boot: the default apps): loveos-session left the watcher to this splash so there is a bar (rule 18)
  local watch = os.getenv("R3I6NOS_MENU_WATCH")
  if watch and watch ~= "" then
    splash(0, 3, "Installing packages")
    pkgflag.watch(watch, function(p) splash(p.step, p.steps, p.doing) end)
  end
  splash(1, 3, "Looking for games and apps")
  pkgflag.take(pkgs) -- a flag from before this start: the scan below sees what it announced
  entries = scan.entries(ROMS, LAUNCHERS, DEFAULTS, ROMS2)
  splash(2, 3, "Reading the battery")
  power = battery.read()
  net = netstatus.read()
  splash(3, 3, "Ready")
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
  if e and require("portmaster").is(e) then -- unofficial and unsupported: the notice first (docs/PORTMASTER.md)
    settings.openPortMaster(dialog, function() request(e.type .. "\t" .. e.target) end)
  elseif e then request(e.type .. "\t" .. e.target) end
end

-- Settings > Sound mixer's test sound: made by mixer.sample, looped while that screen is up and the sound is not Off.
local audio                                      -- love.audio, loaded on first use
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
    audio = audio or love.audio or require("love.audio")   -- conf.lua leaves the module off: loading it opens the device (menu/conf.lua)
    made[want] = audio.newSource(data, "static")
    made[want]:setLooping(true)
  end
  playing = made[want]
  playing:play()
end

function love.update(dt)
  toast.poll(hotplug, love.timer.getTime())
  for _, a in ipairs(input.tick(pad, dt)) do act(a); redraw.mark(rd) end -- a stick step is not an event
  settings.tick(dialog, dt)
  pkgProgressAge = pkgProgressAge + dt
  if pkgProgressAge >= pkgflag.EVERY then -- an install over SSH is extracting: show how far on the command line
    pkgProgressAge = 0
    pkgProgress = pkgflag.progress(PKG_PROGRESS)
    local shown = pkgProgress and (pkgProgress.step .. "/" .. pkgProgress.steps .. pkgProgress.doing) or ""
    if shown ~= pkgProgressShown then pkgProgressShown = shown; redraw.mark(rd) end
  end
  if pkgflag.poll(pkgs, dt) then dialog.rescan = true end -- a package was added or removed over SSH
  if dialog.rescan then -- the data card was formatted (Settings > Data card), or a package changed: read the cards again
    dialog.rescan = nil
    entries = scan.entries(ROMS, LAUNCHERS, DEFAULTS, ROMS2)
    selected = math.max(1, math.min(selected, #entries))
    redraw.mark(rd)
  end
  testSound()
  powerAge = powerAge + dt
  if powerAge >= POWER_EVERY then powerAge, power, net = 0, battery.read(), netstatus.read(); redraw.mark(rd) end
end

-- LÖVE's own loop redraws at the display's rate (60 a second) even when nothing moves: 60% of a core on the unit's idle menu
-- (its device report, 2026-11-12). This loop draws only when something changed (menu/redraw.lua) and otherwise just pumps events
-- and sleeps 50 ms: the menu is text, so a press waits at most that long.
function love.run()
  if love.load then love.load(love.arg.parseGameArguments(arg), arg) end
  love.timer.step()
  return function()
    local started = love.timer.getTime()
    love.event.pump()
    for name, a, b, c, d, e, f in love.event.poll() do
      if name == "quit" then
        if not love.quit or not love.quit() then return a or 0 end
      else
        redraw.event(rd, name)
        love.handlers[name](a, b, c, d, e, f)
      end
    end
    love.update(love.timer.step())
    local n = settings.isOpen(dialog) and settings.node(dialog)
    local moving = n and (n.kind == "mixer" or n.kind == "keytest" or (n.kind == "busy" and "slow")) -- the test sound; the Keymap test's sticks; a helper's wait counts seconds (a report held this at 30 fps: 35% of a core)
    local token = hotplug.showing and ((hotplug.error and "e" or "i") .. hotplug.text) or ""
    local draw, nap = redraw.frame(rd, token, moving)
    if draw and love.graphics.isActive() then
      love.graphics.origin()
      love.graphics.clear(love.graphics.getBackgroundColor())
      love.draw()
      love.graphics.present()
    end
    love.timer.sleep(math.max(0.001, nap - (love.timer.getTime() - started)))
  end
end

local function typing()
  local n = settings.isOpen(dialog) and settings.node(dialog)
  return n and n.kind == "osk"
end

-- Ask the session loop to power the device off: it runs "system<TAB>poweroff", then exits cleanly. "reboot" restarts it the same way
-- (Settings > Data card, when a format needs a restart).
local function poweroff() request("system\tpoweroff") end
local function reboot() request("system\treboot") end

-- One place for every input (the reader's actions: up down left right pageup pagedown home end accept back x y select start menu context ...).
local function act(action)
  if settings.isOpen(dialog) then
    if action == "menu" then -- L1 + R1 closes the main menu again, except where it has a meaning of its own (a keyboard, a test page)
      local n = settings.node(dialog)
      if n.kind == "keytest" then settings.press(dialog, action) elseif n.kind ~= "osk" and n.kind ~= "busy" then dialog.stack = {} end
      return
    end
    local r = settings.press(dialog, action)
    if r == "poweroff" then poweroff() elseif r == "reboot" then reboot() end
    return
  end
  if action == "menu" or action == "y" then settings.open(dialog) return end
  if action == "context" or action == "panelnext" or action == "panelfirst" or action == "panelprev" then return end -- one panel: nothing to turn
  local new = input.list_step(selected, #entries, view.PER_PAGE, action)
  selected = math.max(1, new)
  if action == "accept" then launch() end
end

-- A key that is a pad button goes through the same reader as the pad's buttons (docs/r3i6nos-mapping.md, "The keyboard bindings").
function love.keypressed(key)
  if toast.key(hotplug) then return end -- the key that clears a failure toast is used up
  if typing() then -- letters are text here, not commands
    if key == "backspace" then settings.backspace(dialog)
    elseif key == "return" or key == "kpenter" then settings.press(dialog, "more") -- the keyboard's Enter is Done (the pad's Start)
    elseif key == "escape" then act("back")
    elseif key == "up" or key == "down" or key == "left" or key == "right" then act(key) end
    return
  end
  local a = input.key(pad, key, true)
  if a then act(a) end
end

function love.keyreleased(key) input.key(pad, key, false) end

function love.textinput(text)
  if typing() then settings.text(dialog, text) end
end

-- Raw buttons only: this pad's SDL gamepad mapping does not match its real layout (docs/hardware-findings.md).
function love.joystickpressed(_, button)
  if toast.key(hotplug) then return end
  local a = input.button(pad, button, true)
  if a then
    -- the on-screen keyboard keeps Select and Start (docs/r3i6nos-mapping.md, "What the virtual keyboard overrides"), and Y changes its layer
    if typing() and (a == "start") then a = "more" end
    act(a)
  end
end

function love.joystickreleased(_, button) input.button(pad, button, false) end

-- The right stick is the D-pad at a rate that follows how far it is pushed (dead zone 0.35, at most 6 steps a second).
function love.joystickaxis(_, axis, value) input.axis(pad, axis, value) end

function love.focus(f) if not f then input.release_all(pad) end end

function love.draw()
  view.render(screen, { entries = entries, selected = selected, dialog = dialog, battery = power, net = net, install = pkgProgress })
  toast.draw(hotplug, screen)
  theme.draw(screen)
  sysconfirm.confirm(sysmark) -- the first frame is up: a system update is good (r3i6nos-sysupdate boot would otherwise undo it)
end
