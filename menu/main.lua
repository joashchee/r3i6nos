-- r3i6nos menu: one list of every LÖVE game and native app on the card.
-- Up/Down move, A / Enter launches, PgUp/PgDn (L/R) change page. Never scrolls.

local scan = require("scan")

local ROMS = os.getenv("R3I6NOS_ROMS") or "/roms"
local LAUNCHERS = os.getenv("R3I6NOS_LAUNCHERS") or "/opt/launchers/native"
local NEXT = os.getenv("R3I6NOS_NEXT") or "/run/loveos/next"
local PER_PAGE = 12

local entries, selected = {}, 1
local font

function love.load()
  love.mouse.setVisible(false)
  font = love.graphics.newFont(20)
  love.graphics.setFont(font)
  entries = scan.entries(ROMS, LAUNCHERS)
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

local function move(delta)
  if #entries == 0 then return end
  selected = (selected - 1 + delta) % #entries + 1
end

local function page(delta)
  if #entries == 0 then return end
  selected = math.max(1, math.min(#entries, selected + delta * PER_PAGE))
end

function love.keypressed(key)
  if key == "up" then move(-1)
  elseif key == "down" then move(1)
  elseif key == "pageup" then page(-1)
  elseif key == "pagedown" then page(1)
  elseif key == "return" or key == "z" then launch()
  end
end

function love.gamepadpressed(_, button)
  if button == "dpup" then move(-1)
  elseif button == "dpdown" then move(1)
  elseif button == "leftshoulder" then page(-1)
  elseif button == "rightshoulder" then page(1)
  elseif button == "a" then launch()
  end
end

function love.draw()
  love.graphics.print("r3i6nos", 16, 12)
  if #entries == 0 then
    love.graphics.print("Nothing here yet.", 16, 60)
    love.graphics.print("Drop a .love into Love 11.5 or Love 12.0,", 16, 90)
    love.graphics.print("or an ARM64 .deb into native_deb, and reboot.", 16, 114)
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
end
