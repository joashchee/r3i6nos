-- Run with: luajit tests/test_settings.lua   (from the repo root)
package.path = "menu/?.lua;" .. package.path
local settings = require("settings")

local function new()
  local s = settings.new()
  assert(not settings.isOpen(s))
  settings.open(s)
  assert(settings.isOpen(s) and s.view == "list" and s.selected == 1)
  return s
end

-- Shutdown needs a confirmation, and the confirmation starts on "No".
local s = new()
assert(settings.press(s, "accept") == nil and s.view == "confirm")
assert(settings.press(s, "accept") == nil and s.view == "list", "accepting the default (No) must not power off")

-- Yes powers off and closes the dialog.
settings.press(s, "accept")
settings.press(s, "up")
assert(s.confirm == 1)
assert(settings.press(s, "accept") == "poweroff" and not settings.isOpen(s))

-- B steps back one level at a time; nothing powers off.
s = new()
settings.press(s, "accept")
assert(settings.press(s, "back") == nil and s.view == "list")
assert(settings.press(s, "back") == nil and not settings.isOpen(s))

-- The list wraps, and "Back" closes.
s = new()
settings.press(s, "up")
assert(settings.ITEMS[s.selected].id == "back")
assert(settings.press(s, "accept") == nil and not settings.isOpen(s))
s = new(); settings.press(s, "down"); settings.press(s, "down")
assert(s.selected == 1)

-- A closed dialog ignores input.
s = settings.new()
assert(settings.press(s, "accept") == nil and not settings.isOpen(s))

-- Reopening starts fresh on "Shutdown" with "No" ahead of "Yes".
s = new(); settings.press(s, "down"); settings.press(s, "back"); settings.open(s)
assert(s.selected == 1)

print("settings: ok")
