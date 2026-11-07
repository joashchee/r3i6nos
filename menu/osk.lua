-- The on-screen keyboard for typing a Wi-Fi name or a password with the pad. Pure Lua, no LÖVE calls.
-- D-pad moves over the keys, A types the key, X deletes, Y changes the layer, Select shows or hides the text, Start is done, B cancels.

local osk = {}

osk.COLS, osk.ROWS = 10, 4

osk.LAYERS = {
  { name = "abc", rows = { "1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm., " } },
  { name = "ABC", rows = { "1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM., " } },
  { name = "#+=", rows = { "!@#$%^&*()", "-_=+[]{};:", "'\",.<>/?\\|", "`~ .,;:!?#" } },
}

--- A new entry. o: title, prompt, min (default 0), max (default 63), text (start).
function osk.new(o)
  return {
    title = o.title or "Type", prompt = o.prompt or "", min = o.min or 0, max = o.max or 63, text = o.text or "",
    layer = 1, row = 1, col = 1, hidden = false,
  }
end

--- The character under the cursor.
function osk.key(o)
  local row = osk.LAYERS[o.layer].rows[o.row]
  return row:sub(o.col, o.col)
end

--- Is the text acceptable? Returns true, or false and why.
function osk.valid(o)
  local n = #o.text
  if n < o.min then return false, ("At least %d characters (now %d)"):format(o.min, n) end
  if n > o.max then return false, ("At most %d characters"):format(o.max) end
  return true
end

--- Append typed text (a real keyboard). Anything that is not printable ASCII is ignored.
function osk.type(o, s)
  for ch in tostring(s):gmatch(".") do
    if ch:byte() >= 32 and ch:byte() < 127 and #o.text < o.max then o.text = o.text .. ch end
  end
end

--- Feed one action. Returns "done" (valid text), "cancel", or nil. Invalid "done" stays open with o.error set.
function osk.press(o, action)
  o.error = nil
  if action == "left" then o.col = (o.col - 2) % osk.COLS + 1
  elseif action == "right" then o.col = o.col % osk.COLS + 1
  elseif action == "up" then o.row = (o.row - 2) % osk.ROWS + 1
  elseif action == "down" then o.row = o.row % osk.ROWS + 1
  elseif action == "accept" then
    if #o.text < o.max then o.text = o.text .. osk.key(o) else o.error = ("At most %d characters"):format(o.max) end
  elseif action == "x" then o.text = o.text:sub(1, -2)
  elseif action == "y" then o.layer = o.layer % #osk.LAYERS + 1
  elseif action == "select" then o.hidden = not o.hidden
  elseif action == "more" then
    local ok, why = osk.valid(o)
    if ok then return "done" end
    o.error = why
  elseif action == "back" then return "cancel" end
  return nil
end

return osk
