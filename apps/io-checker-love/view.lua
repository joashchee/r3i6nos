-- IO Checker: the screen, composed on the r3i6nos-ansiapps theme (64x24 cells; docs/R3I6NOS-ANSIAPPS-THEME.md).
-- Pure: it fills a theme screen from the state main.lua keeps, so luajit tests it (tests/test_io_checker.lua).
-- The video patterns are not here: they are the one part drawn in pixels (main.lua).

local theme = require("r3i6nos_ansiapps")
local model = require("model")

local V = {}
local T, ST = theme, theme.STYLE

-- A held button: black on light green (passes 4.5:1; tests check).
V.LIT = { fg = T.BLACK, bg = T.LIGHT_GREEN }

V.HOME = {
  { id = "input", label = "Buttons and sticks",
    about = { "Every button, both sticks, the D-pad, and the power and", "volume keys, as the pad reports them. Press each one:",
              "the list of buttons never pressed shrinks to nothing.", "Hold SELECT+START for a second to come back." } },
  { id = "video", label = "Video",
    about = { "Full-screen test patterns: colour bars, solid colours for", "stuck pixels, ramps for colour depth, a pixel grid for",
              "scaling and edges, and a moving bar that measures the", "frame rate." } },
  { id = "sound", label = "Sound",
    about = { "Test tones on the left, the right and both channels, a", "low tone, a sweep, a quiet one. After each, press X if heard",
              "it or Y if not: the answers go in the device report.", "Shows the speaker path the system set." } },
  { id = "files", label = "Files",
    about = { "On each card: makes its IO_Checker folder, writes,", "renames over, reads back and deletes a file, then times",
              "writing and reading 8 MB and shows the free space.", "Nothing else on the card is touched." } },
  { id = "system", label = "System",
    about = { "The r3i6nos build, kernel, board, CPU, memory, battery,", "backlight, the pad's name and GUID, and the video and",
              "sound drivers." } },
  { id = "report", label = "Device report",
    about = { "Runs the checks again and writes everything, with your", "heard / silent answers and the buttons you pressed, to",
              "EASYROMS/IO_Checker/ on the games card, for Claude." } },
  { id = "quit", label = "Quit", about = { "Back to the r3i6nos menu." } },
}

local function bar(scr, s)
  T.topBar(scr, "IO Checker", s.runtime)
end

local function cmd(scr, s, line, left)
  T.commandLine(scr, { reply = s.reply, kind = s.kind, line = line, left = left, right = s.runtime })
end

-- ---- home -------------------------------------------------------------------------------------------------------------------------

--- The value column of each home item: how far that check has got.
function V.homeValue(s, id)
  if id == "input" and s.pad then return ("%d/%d buttons"):format(s.input:seen(s.pad.buttons), s.pad.buttons) end
  if id == "video" and s.frames then
    local st = s.frames:stats()
    if st then return ("%.1f fps"):format(st.fps) end
  end
  if id == "sound" then
    local heard, asked = 0, 0
    for _, t in ipairs(model.TONES) do
      local a = s.answers[t.id]
      if a == "heard" or a == "silent" then asked = asked + 1 end
      if a == "heard" then heard = heard + 1 end
    end
    if asked > 0 then return ("%d/%d heard"):format(heard, #model.TONES) end
  end
  if id == "files" and s.files then
    local bad = 0
    for _, r in ipairs(s.files) do if r.status == "FAIL" then bad = bad + 1 end end
    return bad == 0 and "ok" or ("%d FAIL"):format(bad)
  end
  if id == "report" and s.reportFile then return "written" end
  return nil
end

local function home(scr, s)
  local items = {}
  for i, h in ipairs(V.HOME) do items[i] = { label = h.label, value = V.homeValue(s, h.id) } end
  T.listDialog(scr, { col = 0, row = 1, w = T.COLS, h = #items + 2, title = "IO Checker " .. model.VERSION, items = items,
                      sel = s.sel.home, foot = "A open  B quit" })
  local top = #items + 3
  local h = V.HOME[s.sel.home]
  scr:box(0, top, T.COLS, T.DISPLAY_TOP + T.DISPLAY_ROWS - top, { double = false, title = h.label })
  for i, line in ipairs(h.about) do scr:text(2, top + i, T.fit(line, T.COLS - 4), ST.body.fg, ST.body.bg) end
  cmd(scr, s, h.id == "quit" and "quit" or ("open " .. h.label), s.pad and s.pad.name or "no pad found")
end

-- ---- buttons and sticks -----------------------------------------------------------------------------------------------------------

local function key(scr, col, row, label, on)
  local st = on and V.LIT or ST.body
  scr:text(col, row, "[" .. label .. "]", st.fg, st.bg)
end

local ARROW = { up = 24, down = 25, right = 26, left = 27 }

local function stick(scr, col, row, ax, ay, pressed, label)
  local fg = pressed and T.LIGHT_GREEN or ST.frame.fg
  scr:box(col, row, 9, 7, { double = false, fg = fg })
  scr:put(col + 4, row + 3, 250, ST.body.fg)                       -- the centre
  local dx = math.max(-3, math.min(3, math.floor(ax * 3 + 0.5)))
  local dy = math.max(-2, math.min(2, math.floor(ay * 2 + 0.5)))
  scr:put(col + 4 + dx, row + 3 + dy, 254, pressed and T.LIGHT_GREEN or T.LIGHT_CYAN)
  local st = pressed and V.LIT or ST.body
  scr:text(col, row + 7, pressed and (label .. " IN") or label, st.fg, st.bg)
end

local function input(scr, s)
  local p = s.pad
  if not p then
    scr:box(0, 1, T.COLS, T.DISPLAY_ROWS, { title = "Buttons and sticks" })
    scr:text(2, 3, "No gamepad has reached SDL.", ST.error.fg, ST.error.bg)
    scr:text(2, 5, "Press a button: this page changes when a pad appears.", ST.body.fg, ST.body.bg)
    scr:text(2, 6, "The power and volume keys are still shown below.", ST.body.fg, ST.body.bg)
    key(scr, 2, 9, "PWR", model.keyHeld(s.keysDown, "power"))
    key(scr, 9, 9, "VOL-", model.keyHeld(s.keysDown, "vol_down"))
    key(scr, 16, 9, "VOL+", model.keyHeld(s.keysDown, "vol_up"))
  else
    local L, down = model.LAYOUT, p.down
    key(scr, 1, 2, "L1", down(L.l1)); key(scr, 6, 2, "L2", down(L.l2))
    key(scr, 23, 2, "R2", down(L.r2)); key(scr, 28, 2, "R1", down(L.r1))
    local d = model.dpad(p.hat, down)
    key(scr, 5, 4, string.char(0), d.up); scr:put(6, 4, ARROW.up, d.up and V.LIT.fg or ST.body.fg)
    key(scr, 2, 5, string.char(0), d.left); scr:put(3, 5, ARROW.left, d.left and V.LIT.fg or ST.body.fg)
    key(scr, 8, 5, string.char(0), d.right); scr:put(9, 5, ARROW.right, d.right and V.LIT.fg or ST.body.fg)
    key(scr, 5, 6, string.char(0), d.down); scr:put(6, 6, ARROW.down, d.down and V.LIT.fg or ST.body.fg)
    key(scr, 26, 4, "X", down(L.x)); key(scr, 23, 5, "Y", down(L.y)); key(scr, 29, 5, "A", down(L.a)); key(scr, 26, 6, "B", down(L.b))
    stick(scr, 2, 8, p.axis(1), p.axis(2), down(L.l3), "L stick")
    stick(scr, 23, 8, p.axis(3), p.axis(4), down(L.r3), "R stick")
    key(scr, 2, 17, "SELECT", down(L.select)); key(scr, 12, 17, "FN", down(L.fn)); key(scr, 18, 17, "START", down(L.start))
    key(scr, 2, 19, "PWR", model.keyHeld(s.keysDown, "power"))
    key(scr, 9, 19, "VOL-", model.keyHeld(s.keysDown, "vol_down"))
    key(scr, 16, 19, "VOL+", model.keyHeld(s.keysDown, "vol_up"))

    -- the raw numbers, as the pad sends them
    local bx, w = 36, 28
    scr:box(bx, 1, w, T.DISPLAY_ROWS, { title = "Raw input" })
    local x, y, room = bx + 2, 2, w - 4
    local function row(label, value, st)
      st = st or ST.body
      scr:text(x, y, T.fit(("%-6s%s"):format(label, value), room), st.fg, st.bg)
      y = y + 1
    end
    local held = {}
    for i = 1, p.buttons do if down(i) then held[#held + 1] = tostring(i) end end
    row("Down", #held > 0 and table.concat(held, " ") or "none", #held > 0 and ST.ok or ST.body)
    row("Hat", p.hats > 0 and (p.hat or "c") or "none")
    for a = 1, math.min(4, p.axes) do
      local r = s.input.axes[a]
      row("A" .. a, model.fmt(p.axis(a)) .. (r and ("  %s %s"):format(model.fmt(r.min), model.fmt(r.max)) or ""))
    end
    local missing = s.input:missing(p.buttons)
    local miss = {}
    for i, b in ipairs(missing) do miss[i] = tostring(b) end
    row("Seen", ("%d/%d buttons"):format(p.buttons - #missing, p.buttons), #missing == 0 and ST.ok or ST.body)
    row("Never", #miss > 0 and table.concat(miss, " ") or "-", #missing > 0 and ST.warn or ST.body)
    local keys = {}
    for k in pairs(s.input.keys) do keys[#keys + 1] = k end
    table.sort(keys)
    row("Keys", #keys > 0 and table.concat(keys, " ") or "none yet")
    local chat = 0
    for _, n in pairs(s.input.chatter) do chat = chat + n end
    row("Chat", chat == 0 and "none" or (chat .. " (bouncing)"), chat == 0 and ST.body or ST.warn)
    row("Map", p.gamepad and "SDL has one" or "none")
    y = y + 1
    scr:text(x, y, "Last events", ST.strong.fg, ST.strong.bg)
    y = y + 1
    for _, e in ipairs(s.input.log) do
      if y > 19 then break end
      scr:text(x, y, T.fit(e, room), ST.hint.fg, ST.hint.bg)
      y = y + 1
    end
  end
  local reply, kind = "Press every button. Hold SELECT+START to go back.", "hint"
  if s.chord and s.chord.t > 0 then reply, kind = ("Going back... %d%%"):format(math.floor(s.chord:progress() * 100)), "warn"
  elseif s.idle and s.idle:warning() then reply, kind = ("No input: back to the list in %d s"):format(math.ceil(s.idle:remaining())), "warn" end
  T.commandLine(scr, { reply = reply, kind = kind, line = p and p.name or "no pad", right = s.runtime,
                       left = p and ("Seen %d/%d buttons"):format(s.input:seen(p.buttons), p.buttons) or nil })
end

-- ---- video ------------------------------------------------------------------------------------------------------------------------

local function video(scr, s)
  local items = {}
  for i, p in ipairs(model.PATTERNS) do items[i] = { label = p.label } end
  items[#items].value = s.frames and s.frames:stats() and ("%.1f fps"):format(s.frames:stats().fps) or nil
  T.listDialog(scr, { col = 0, row = 1, w = T.COLS, h = #items + 2, title = "Video", items = items, sel = s.sel.video,
                      foot = "A show  L/R next  B back" })
  local top = #items + 3
  scr:box(0, top, T.COLS, T.DISPLAY_TOP + T.DISPLAY_ROWS - top, { double = false, title = "This screen" })
  local v = s.video or {}
  local rows = {
    { "Mode", v.mode }, { "Renderer", v.renderer }, { "Backlight", s.backlightText },
    { "Frame rate", model.framesLine(s.frames and s.frames:stats()) },
    { "Look for", model.PATTERNS[s.sel.video].look },
  }
  for i, r in ipairs(rows) do
    if top + i >= T.DISPLAY_TOP + T.DISPLAY_ROWS - 1 then break end
    scr:text(2, top + i, T.fit(("%-11s%s"):format(r[1], r[2] or "?"), T.COLS - 4), ST.body.fg, ST.body.bg)
  end
  cmd(scr, s, "show " .. model.PATTERNS[s.sel.video].label, ("%d of %d"):format(s.sel.video, #model.PATTERNS))
end

-- ---- sound ------------------------------------------------------------------------------------------------------------------------

local function sound(scr, s)
  local items = {}
  for i, t in ipairs(model.TONES) do items[i] = { label = t.label, value = model.answerText(s.answers[t.id]) } end
  T.listDialog(scr, { col = 0, row = 1, w = T.COLS, h = #items + 2, title = "Sound", items = items, sel = s.sel.sound,
                      foot = "A play  X heard  Y silent  B back" })
  local top = #items + 3
  scr:box(0, top, T.COLS, T.DISPLAY_TOP + T.DISPLAY_ROWS - top, { double = false, title = "The sound card" })
  local snd = s.sound or {}
  local y = top + 1
  local function row(label, value, st)
    st = st or ST.body
    scr:text(2, y, T.fit(("%-14s%s"):format(label, value or "?"), T.COLS - 4), st.fg, st.bg)
    y = y + 1
  end
  row("Card", snd.card)
  local path = snd.path
  if path == "OFF" then row("Speaker path", "OFF: the codec is silent (r3i6nos-sound did not run)", ST.error)
  elseif path then row("Speaker path", path .. (path == "SPK" and " (speaker)" or (path:match("HP") and " (headphones)" or "")), ST.ok)
  else row("Speaker path", "unknown (no r3i6nos-sound, or another codec)") end
  row("Headphones", snd.headphones and (snd.headphones .. (snd.jack and (", " .. snd.jack) or "")) or (snd.jack or "no jack switch found"))
  row("Output", s.audioDevice or "?", s.audioError and ST.error or ST.body)
  y = y + 1
  scr:text(2, y, "Play a tone, then press X if you heard it, Y if you did not.", ST.hint.fg, ST.hint.bg)
  scr:text(2, y + 1, "Try the speaker first, then again with headphones in.", ST.hint.fg, ST.hint.bg)
  cmd(scr, s, "play " .. model.TONES[s.sel.sound].label, ("%d of %d"):format(s.sel.sound, #model.TONES))
end

-- ---- files and system -------------------------------------------------------------------------------------------------------------

local function files(scr, s)
  local items = {}
  for i, r in ipairs(s.files or {}) do
    items[i] = { label = r.label, value = T.fit((r.status == "FAIL" and "FAIL " or "") .. r.value, 34) } -- the label always shows
  end
  T.listDialog(scr, { col = 0, row = 1, w = T.COLS, h = T.DISPLAY_ROWS, title = "Files", items = items, sel = s.sel.files,
                      foot = "A test again  B back",
                      empty = { "Press A to test every card: it writes, renames, reads and", "deletes small files in an IO_Checker folder, then times 8 MB." } })
  cmd(scr, s, "test every card", #items > 0 and ("%d of %d"):format(s.sel.files, #items) or nil)
end

local function system(scr, s)
  T.listDialog(scr, { col = 0, row = 1, w = T.COLS, h = T.DISPLAY_ROWS, title = "System", items = s.system or {}, sel = s.sel.system,
                      foot = "B back" })
  cmd(scr, s, "", ("%d of %d"):format(s.sel.system, #(s.system or {})))
end

--- Fill scr from the state. s.page: home, input, video, sound, files or system.
function V.render(scr, s)
  scr:clear()
  bar(scr, s)
  local page = ({ home = home, input = input, video = video, sound = sound, files = files, system = system })[s.page] or home
  page(scr, s)
  return scr
end

return V
