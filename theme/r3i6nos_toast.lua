-- r3i6nos hot-plug toast for LÖVE apps and the menu (docs/HOTPLUG.md); the C twin is r3i6nos_toast.h. MIT, like the rest of theme/.
--
-- r3i6nos-hotplug (a root service) writes one line for each change of what is plugged in (headphones, a USB device, the DC power port) to
-- /run/r3i6nos-hotplug/events: "<nanoseconds>\t<info|error>\t<text>". This shows each new line over the top bar: a change flashes for 3 seconds; a
-- failure stays until a key is pressed, and that key is used up (key() returns true) so it cannot also press a button in the app.
--
--   local toast = require("r3i6nos_toast")
--   local t = toast.new()                    -- at start: what is in the file is history, not shown
--   function love.update(dt) toast.poll(t, love.timer.getTime()) ... end
--   function love.keypressed(k) if toast.key(t) then return end ... end     -- and the same for a pad button
--   -- in love.draw, after the screen is composed and before theme.draw(screen):  toast.draw(t, screen)
--
-- R3I6NOS_HOTPLUG_DIR overrides the folder (tests). Pure but for io.open, so luajit tests it.

local T = require("r3i6nos_ansiapps")
local M = {}

M.SECONDS = 3
M.LOOK = 0.25      -- seconds between looks at the file
M.QUEUE = 8

local function lines(path)
  local f = io.open(path, "r")
  if not f then return {} end
  local out = {}
  for line in f:lines() do
    local ns, level, text = line:match("^(%d+)\t([^\t]*)\t(.*)$")
    if ns then out[#out + 1] = { ns = tonumber(ns), error = level == "error", text = text } end
  end
  f:close()
  return out
end

--- A new toast reader. `dir` is the folder of the events file (default $R3I6NOS_HOTPLUG_DIR or /run/r3i6nos-hotplug).
function M.new(dir)
  dir = dir or os.getenv("R3I6NOS_HOTPLUG_DIR") or "/run/r3i6nos-hotplug"
  local t = { path = dir .. "/events", seen = 0, queue = {}, showing = false, error = false, text = "", at = -1e9 }
  for _, l in ipairs(lines(t.path)) do t.seen = math.max(t.seen, l.ns) end   -- the history is not news
  return t
end

function M.active(t) return t.showing or #t.queue > 0 end

local function advance(t, now)
  if #t.queue == 0 then t.showing = false return end
  local pick = 1
  for i, l in ipairs(t.queue) do if l.error then pick = i break end end    -- a failure goes first
  local l = table.remove(t.queue, pick)
  t.showing, t.error, t.text, t.untilAt = true, l.error, l.text, now + M.SECONDS
end

--- While a Rec demo records the toast is held: nothing is shown, a change is dropped, a failure waits in the queue and shows when the hold ends.
function M.hold(t, on)
  if on and not t.held and t.showing and t.error then table.insert(t.queue, 1, { error = true, text = t.text }) end
  if on then
    t.showing = false
    local keep = {}
    for _, q in ipairs(t.queue) do if q.error then keep[#keep + 1] = q end end   -- queued changes are dropped, failures kept
    t.queue = keep
  end
  t.held = on and true or false
end

--- Call every frame with a clock in seconds.
function M.poll(t, now)
  if now - t.at >= M.LOOK then
    t.at = now
    for _, l in ipairs(lines(t.path)) do
      if l.ns > t.seen then
        t.seen = l.ns
        if t.held and not l.error then goto skip end
        if #t.queue >= M.QUEUE then   -- full: drop the oldest change, never a failure
          for i, q in ipairs(t.queue) do if not q.error then table.remove(t.queue, i) break end end
        end
        if #t.queue < M.QUEUE then t.queue[#t.queue + 1] = l end
      end
      ::skip::
    end
  end
  if t.held then t.showing = false return end
  if t.showing and not t.error and now >= t.untilAt then t.showing = false end
  if not t.showing then advance(t, now)
  elseif not t.error then   -- a failure does not wait for a flash to end
    for _, q in ipairs(t.queue) do if q.error then advance(t, now) break end end
  end
end

--- A key or pad button went down: true when it cleared a failure toast (used up), false otherwise.
function M.key(t)
  if t.showing and t.error then t.showing = false return true end
  return false
end

--- The ANSIapps Mono status glyph (by name) a toast's text calls for, or nil: the hot-plug service's words (r3i6nos-hotplug) tell what changed.
--- The C twin is r3_toast_icon in r3i6nos_toast.h; both are tested with the same texts.
function M.icon(text)
  local w = (text or ""):lower()
  if w:find("^headphone") then return "sys.headphones" end
  if w:find("^battery") then return w:find("full", 1, true) and "sys.bat.4" or "sys.bat.0" end
  if w:find("^dc power") or w:find("^usb power") or w:find("^charg") then return "sys.dc" end
  if w:find("^usb") then return "sys.usb" end
  if w:find("^volume") then
    local n = tonumber(w:match("(%d+)%%"))
    if not n then return "sys.vol.2" end
    return n == 0 and "sys.mute" or ("sys.vol." .. (n < 34 and 1 or (n < 67 and 2 or 3)))
  end
  if w:find("^pad") or w:find("^gamepad") or w:find("^controller") then return "sys.pad" end
  if w:find("^tf") or w:find("^card") or w:find("^sd ") then return "sys.card" end
  if w:find("^wi%-?fi") or w:find("^network") then return "sys.wifi.3" end
  return nil
end

M.icons = true     -- an app may turn the glyph off; it also needs the theme's font folder (an older image has none: the text is shown alone)

--- Over the top bar (row 0): black on light green for a change, white on red for a failure, which says how to clear it.
function M.draw(t, scr)
  if not t.showing then return end
  local fg, bg = t.error and T.WHITE or T.BLACK, t.error and T.RED or T.LIGHT_GREEN
  scr:fill(0, T.TOP_ROW, T.COLS, 1, 32, fg, bg)
  local col, room = 1, t.error and T.COLS - 18 or T.COLS - 2
  local name = M.icons and T.mono() and M.icon(t.text)
  if name then col, room = scr:glyph(1, T.TOP_ROW, name, fg, bg) + 1, room - 2 end   -- the glyph, then a space
  scr:text(col, T.TOP_ROW, T.fit(t.text, room), fg, bg)
  if t.error then scr:textRight(T.COLS - 2, T.TOP_ROW, T.decode("Press a key"), fg, bg) end
end

return M
