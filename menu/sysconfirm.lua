-- menu/sysconfirm.lua: the menu tells r3i6nos-sysupdate that it started well (docs/PACKAGES-OTA.md, "System updates").
-- After a system update the root helper leaves /var/lib/r3i6nos/pending/system-pending (a folder the login user owns, R3I6NOS_PENDING_DIR). The
-- menu removes it once, after it has drawn its first full screen. If it is still there at the second boot, r3i6nos-sysupdate puts the old files
-- back before the menu starts, so a menu that cannot draw cannot leave a unit that needs a card taken out.
local M = {}

M.FILE = "system-pending"

function M.new(dir)
  dir = dir or os.getenv("R3I6NOS_PENDING_DIR") or "/var/lib/r3i6nos/pending"
  return { path = dir .. "/" .. M.FILE, done = false }
end

--- Call after a frame has been drawn. Removes the marker the first time; true when there was one to remove.
function M.confirm(s)
  if s.done then return false end
  s.done = true
  return os.remove(s.path) and true or false
end

return M
