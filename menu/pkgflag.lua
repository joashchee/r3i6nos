-- menu/pkgflag.lua: has r3i6nos-pkg installed or removed something while the menu was open? (docs/PACKAGES-OTA.md)
-- r3i6nos-pkg (run over SSH as the login user) leaves a small file, /run/loveos/pkg-changed, after it adds or removes a package. The menu
-- looks for it about once a second (one failed open, no process, no scan) and, when it is there, takes it away and reads the cards again,
-- so a pushed app shows in the list at once. A flag left from before the menu started is taken at the start (the first scan sees it all).
local M = {}

M.EVERY = 1 -- seconds between looks

function M.new(path)
  return { path = path, age = 0 }
end

--- Look for the flag and remove it. True when it was there.
function M.take(s)
  local f = io.open(s.path, "r")
  if not f then return false end
  f:close()
  os.remove(s.path)
  return true
end

--- Call every frame with the time since the last. True (once) when the list should be read again.
function M.poll(s, dt)
  s.age = s.age + dt
  if s.age < M.EVERY then return false end
  s.age = 0
  return M.take(s)
end

--- Rule 18: while deb_watcher.py extracts packages (a push over SSH, r3i6nos-pkg) it keeps /run/loveos/pkg-progress (step=, steps=, doing=). The menu looks
--- every EVERY seconds like the flag above (one failed open when nothing runs) and shows a bar on its command line. Returns { step, steps, doing } or nil.
function M.progress(path)
  local f = io.open(path, "r")
  if not f then return nil end
  local text = f:read("*a") or ""
  f:close()
  local step, steps = tonumber(text:match("step=(%d+)")), tonumber(text:match("steps=(%d+)"))
  if not step or not steps or steps < 1 then return nil end
  return { step = math.min(step, steps), steps = steps, doing = text:match("doing=([^\n]*)") or "" }
end

return M
