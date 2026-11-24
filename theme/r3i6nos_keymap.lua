-- The Keymap, for LÖVE apps (docs/r3i6nos-mapping.md, "The Keymap"): the app's input map as help pages and a test page. Pure Lua.
-- The Lua twin of theme/r3i6nos_keymap.h: help and test are built; REMAP is not built here (nor in the C theme: FM1-NTT has the only one).
-- An app passes its own one-touch rows (what each button does in its panels); the system's pages are the same everywhere.

local I = require("r3i6nos_input")

local K = {}

K.PER_PAGE = 18   -- lines a page of the display view holds (rows 1-20 less the box's edges)

local function rows(list) local out = {} for _, r in ipairs(list) do out[#out + 1] = ("%-14s%s"):format(r[1], r[2]) end return out end

--- The system's inputs: the same in every app.
K.SYSTEM = {
  rows({
    { "L1 + R1", "Main menu     (key: ` or Q + O)" },
    { "L2 + R2", "Context menu  (key: \\ or E + U)" },
    { "Fn", "Next panel     (key: T)" },
    { "L1 + Fn", "First panel" },
    { "R1 + Fn", "Previous panel" },
    { "R1 held", "Increase: up/down page, left/right home/end" },
    { "D-pad", "Move the selection  (W A S D)" },
    { "Right stick", "Same, by how far it is pushed  (arrows)" },
    { "Left stick", "The cursor or pointer" },
    { "L3", "Virtual mouse, where the app has one  (R)" },
    { "R3", "Virtual keyboard, where the app has one  (Y)" },
  }),
  rows({
    { "A", "Accept  (L, Enter, Space)" },
    { "B", "Back  (K, Esc)" },
    { "X", "Delete  (I, Backspace)" },
    { "Y", "More, next field  (J, Tab)" },
    { "Select", "Left click, or the app's  (F)" },
    { "Start", "Right click, or the app's  (H)" },
    { "L1 L2 R1 R2", "Held: Q E O U" },
    { "", "" },
    { "Never bound", "No key or chord quits an app: only the" },
    { "", "main menu's Quit does. L1, L2, R1, R2, Fn," },
    { "", "L3 and R3 are the system's, not remapped." },
  }),
}

--- The pages of help: the system's, then one page for the app's panel rows ({ { "A", "Run the selected game" }, ... }) when given.
function K.pages(appRows, appTitle)
  local pages = {}
  for _, p in ipairs(K.SYSTEM) do pages[#pages + 1] = p end
  if appRows and #appRows > 0 then
    local lines = { (appTitle or "This app") .. ":", "" }
    for _, l in ipairs(rows(appRows)) do lines[#lines + 1] = l end
    pages[#pages + 1] = lines
  end
  return pages
end

--- The test page: every press with what it does, the buttons held and the sticks; it acts on nothing. t = K.test_new().
function K.test_new() return { log = {}, reader = I.new() } end

local LOG = 8

function K.test_press(t, label)
  t.log[#t.log + 1] = label
  while #t.log > LOG do table.remove(t.log, 1) end
end

--- Feed a raw button to the test: returns the action it makes (the reader's), and logs it.
function K.test_button(t, b, down)
  local act = I.button(t.reader, b, down)
  if down then K.test_press(t, (I.NAME[b] or "?") .. (act and ("  " .. (I.ACTION_NAME[act] or act)) or "")) end
  return act
end

--- The lines of the test page.
function K.test_lines(t)
  local held = {}
  for b = 1, 17 do if t.reader.held[b] then held[#held + 1] = I.NAME[b] end end
  local a = t.reader.axis
  local lines = {
    "Press anything: nothing here does anything.",
    "L1 + R1 leaves this page, as B does.",
    "",
    "Held:    " .. (#held > 0 and table.concat(held, " ") or "-"),
    ("Left:    %+.2f %+.2f   Right: %+.2f %+.2f"):format(a[1], a[2], a[3], a[4]),
    "",
    "Last presses:",
  }
  for i = #t.log, 1, -1 do lines[#lines + 1] = "  " .. t.log[i] end
  return lines
end

return K
