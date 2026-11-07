-- The Settings dialog's logic. Pure Lua, no LÖVE calls, so plain luajit can test it.
-- The dialog is a short list of items, then a confirmation page for anything that
-- cannot be undone. It never scrolls: with more items than fit, use whole pages.

local settings = {}

settings.ITEMS = {
  { id = "shutdown", label = "Shutdown", hint = "Power the device off" },
  { id = "back", label = "Back", hint = "Return to the games" },
}

--- A closed dialog. view is "closed", "list" or "confirm".
function settings.new()
  return { view = "closed", selected = 1, confirm = 1 }
end

function settings.open(s)
  s.view, s.selected, s.confirm = "list", 1, 2 -- confirm starts on "No"
end

function settings.isOpen(s) return s.view ~= "closed" end

--- Feed one action: "up", "down", "accept" or "back".
--- Returns "poweroff" when the user confirmed a shutdown, else nil.
function settings.press(s, action)
  if s.view == "list" then
    local n = #settings.ITEMS
    if action == "up" then s.selected = (s.selected - 2) % n + 1
    elseif action == "down" then s.selected = s.selected % n + 1
    elseif action == "back" then s.view = "closed"
    elseif action == "accept" then
      local id = settings.ITEMS[s.selected].id
      if id == "back" then s.view = "closed"
      elseif id == "shutdown" then s.view, s.confirm = "confirm", 2 end
    end
  elseif s.view == "confirm" then
    if action == "up" or action == "down" then s.confirm = 3 - s.confirm
    elseif action == "back" then s.view = "list"
    elseif action == "accept" then
      if s.confirm == 1 then
        s.view = "closed"
        return "poweroff"
      end
      s.view = "list"
    end
  end
  return nil
end

return settings
