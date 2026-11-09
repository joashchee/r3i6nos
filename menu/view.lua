-- The menu's screen in the r3i6nos-ansiapps theme: pure Lua (no LÖVE calls), so plain luajit can test it.
-- Row 0 the top bar; rows 1-20 the games list (a list dialog; Settings opens over it); rows 21-23 the command line.

local theme = require("r3i6nos_ansiapps")
local settings = require("settings")
local osk = require("osk")
local battery = require("battery")

local view = {}

local TYPE_LABEL = { love115 = "LÖVE 11.5", love120 = "LÖVE 12.0", native = "native" }

--- Games shown on a page, and the list's place on the screen.
view.LIST = { col = 0, row = theme.DISPLAY_TOP, w = theme.COLS, h = theme.DISPLAY_ROWS }
view.PER_PAGE = theme.listRows(view.LIST.h)

local function typeLabel(e) return TYPE_LABEL[e.type] or e.type end

local FULL = { col = 0, row = theme.DISPLAY_TOP, w = theme.COLS, h = theme.DISPLAY_ROWS }

local function centered(w, h)
  return math.floor((theme.COLS - w) / 2), theme.DISPLAY_TOP + math.floor((theme.DISPLAY_ROWS - h) / 2)
end

local function drawMenu(scr, n)
  local items = {}
  for i, it in ipairs(n.items) do items[i] = { label = it.label, value = it.value } end
  local w, h = n.w or 40, math.min(#items + 2, theme.DISPLAY_ROWS)
  local col, row = centered(w, h)
  theme.listDialog(scr, { col = col, row = row, w = w, h = h, title = n.title, items = items, sel = n.sel, foot = "A Choose  B Back" })
  return n.items[n.sel].hint
end

local function lineStyle(l)
  local st = theme.STYLE
  if l:find("^%[ok%]") then return st.ok end
  if l:find("^%[FAIL%]") then return st.error end
  return st.body
end

local function drawPages(scr, n)
  local pages = #n.pages
  scr:box(FULL.col, FULL.row, FULL.w, FULL.h, {
    title = n.title, right = pages > 1 and ("Page %d/%d"):format(n.page, pages) or nil,
    foot = pages > 1 and "L1/R1 Page  B Back" or "B Back",
  })
  for i, l in ipairs(n.pages[n.page]) do
    local s = lineStyle(l)
    scr:text(FULL.col + 2, FULL.row + i, theme.fit(l, FULL.w - 4), s.fg, s.bg)
  end
  return n.bad and "It did not work: see the lines above" or nil
end

local function drawOsk(scr, n)
  local o, st = n.osk, theme.STYLE
  scr:box(FULL.col, FULL.row, FULL.w, FULL.h, { title = n.title, foot = "A Type  X Del  Y Layer  Select Hide  Start Done  B Esc" })
  scr:text(2, FULL.row + 2, theme.fit(o.prompt, FULL.w - 4), st.hint.fg, st.hint.bg)
  local shown = o.hidden and string.rep("*", #o.text) or o.text
  local count = ("%d/%d"):format(#o.text, o.max)
  scr:fill(2, FULL.row + 4, FULL.w - 4, 1, 32, st.strong.fg, st.strong.bg)
  local x = scr:text(2, FULL.row + 4, theme.fit(">" .. " " .. shown, FULL.w - 4 - #count - 2), st.strong.fg, st.strong.bg)
  scr:put(x, FULL.row + 4, 219, st.strong.fg, st.strong.bg)
  scr:textRight(FULL.w - 3, FULL.row + 4, count, st.body.fg, st.body.bg)
  if o.error then scr:text(2, FULL.row + 6, theme.fit(o.error, FULL.w - 4), st.error.fg, st.error.bg) end
  local layer = osk.LAYERS[o.layer]
  scr:text(2, FULL.row + 8, "Keys: " .. layer.name, st.body.fg, st.body.bg)
  for r = 1, osk.ROWS do
    for c = 1, osk.COLS do
      local ch = layer.rows[r]:sub(c, c)
      local label = (ch == " ") and "sp" or ch
      local sel = (r == o.row and c == o.col)
      local s = sel and st.select or st.body
      local x0 = 12 + (c - 1) * 4
      scr:text(x0, FULL.row + 9 + (r - 1) * 2, (" %-2s"):format(label), s.fg, s.bg)
    end
  end
  return o.error or "Type with the pad, or a keyboard"
end

-- A wait. With n.prog (a device report's progress): what it is doing, a bar when it says its steps, and the time so far.
local function drawBusy(scr, n)
  local p = n.prog
  local w, h = 50, p and 9 or 7
  local col, row = centered(w, h)
  scr:box(col, row, w, h, { title = n.title, foot = n.foot or "B Stop waiting" })
  local st = theme.STYLE
  scr:text(col + 3, row + 2, n.waiting or "Waiting for the network helper...", st.body.fg, st.body.bg)
  local secs = ("%d s"):format(math.floor(n.elapsed))
  if not p then
    scr:text(col + 3, row + 4, secs, st.hint.fg, st.hint.bg)
    return "Working: " .. n.title
  end
  local doing = p.doing and p.doing ~= "done" and p.doing or "starting"
  if #doing > w - 6 then doing = doing:sub(1, w - 7) .. "~" end
  scr:text(col + 3, row + 3, doing, st.hint.fg, st.hint.bg)
  local bw = w - 6
  if p.step and p.steps then
    local filled = math.floor(bw * p.step / p.steps + 0.5)
    for i = 0, bw - 1 do scr:put(col + 3 + i, row + 5, i < filled and 219 or 176, st.body.fg, st.body.bg) end
    scr:text(col + 3, row + 6, ("step %d of %d   %s"):format(p.step, p.steps, secs), st.hint.fg, st.hint.bg)
  else
    scr:text(col + 3, row + 5, "This report does not say its steps.", st.hint.fg, st.hint.bg)
    scr:text(col + 3, row + 6, secs .. "; this screen changes when it is done.", st.hint.fg, st.hint.bg)
  end
  return "Working: " .. n.title
end

local function drawConfirm(scr, n)
  local items = { { label = n.yes }, { label = "No" } }
  local w, h = 40, 4
  local col, row = centered(w, h)
  theme.listDialog(scr, { col = col, row = row, w = w, h = h, title = n.title, items = items, sel = n.sel, foot = "A Choose  B Back" })
  return n.hint
end

-- Date and time: six fields on one row, the chosen one black on cyan.
local function drawClock(scr, n)
  local clock = require("clock")
  local st = theme.STYLE
  local w, h = 52, n.first and 11 or 9
  local col, row = centered(w, h)
  scr:box(col, row, w, h, { title = n.title, foot = n.first and "A Set  B Not now" or "A Set  B Back" })
  local y = row + 2
  if n.first then
    scr:text(col + 3, y, "This unit does not keep the time while it is off,", st.body.fg, st.body.bg)
    scr:text(col + 3, y + 1, "and the reports are stamped with it. Set it once:", st.body.fg, st.body.bg)
    y = y + 3
  end
  local parts, seps = clock.parts(n.c), { "-", "-", "  ", ":", "   ", "" }
  local x = col + 3
  for i, p in ipairs(parts) do
    local s = (i == n.c.field) and st.select or st.strong
    x = scr:text(x, y, p, s.fg, s.bg)
    x = scr:text(x, y, seps[i], st.body.fg, st.body.bg)
  end
  scr:text(col + 3, y + 2, "Left/Right: field  Up/Down: change  L1/R1: faster", st.hint.fg, st.hint.bg)
  local names = { "the year", "the month", "the day", "the hour", "the minute", "the time zone (hours from UTC)" }
  return "Changing " .. names[n.c.field]
end

--- Draw the Settings dialog's top screen. Returns the reply (a hint) and the breadcrumb for the command line.
function view.dialog(scr, d)
  local n = settings.node(d)
  local hint
  if n.kind == "menu" then hint = drawMenu(scr, n)
  elseif n.kind == "pages" then hint = drawPages(scr, n)
  elseif n.kind == "osk" then hint = drawOsk(scr, n)
  elseif n.kind == "busy" then hint = drawBusy(scr, n)
  elseif n.kind == "confirm" then hint = drawConfirm(scr, n)
  elseif n.kind == "clock" then hint = drawClock(scr, n) end
  local titles = {}
  for i, node in ipairs(d.stack) do titles[i] = node.title end
  return hint, table.concat(titles, " > "):lower()
end

--- The battery at the top right (state.battery is a reading from battery.read(), nil when there is none). Low and discharging: white on red.
function view.battery(scr, reading)
  local text, kind = battery.indicator(reading)
  if not text then return end
  local fg, bg = theme.STYLE.bar.fg, theme.STYLE.bar.bg
  if kind == "low" then fg, bg = theme.WHITE, theme.RED
  elseif kind == "charging" then fg = theme.LIGHT_GREEN end
  scr:textRight(theme.COLS - 2, theme.TOP_ROW, text, fg, bg)
end

--- Draw the whole screen. state: entries, selected (1-based), dialog (settings state), reply (optional { text, kind }).
function view.render(scr, state)
  scr:clear()
  local entries, sel = state.entries, state.selected
  local n = #entries
  theme.topBar(scr, ("r3i6nos  %d %s"):format(n, n == 1 and "app" or "apps"))
  view.battery(scr, state.battery)

  local items = {}
  for i, e in ipairs(entries) do items[i] = { label = e.title, value = typeLabel(e) } end
  theme.listDialog(scr, {
    col = view.LIST.col, row = view.LIST.row, w = view.LIST.w, h = view.LIST.h,
    title = "Games and apps", items = items, sel = sel,
    foot = "A Run  L1/R1 Page  L2/R2 Top/End  Start Settings",
    empty = { "Nothing here yet.", "", "Drop a .love into Love 11.5 or Love 12.0,", "or an ARM64 .deb into native_deb, and reboot." },
  })

  local reply, kind = state.reply and state.reply.text, state.reply and state.reply.kind
  local line, left, right
  local e = entries[sel]
  if e then
    reply = reply or e.target
    line, left, right = "run " .. e.title, ("%d of %d"):format(sel, n), typeLabel(e)
  else
    left = "0 of 0"
  end

  local d = state.dialog
  if d and settings.isOpen(d) then
    local hint, crumb = view.dialog(scr, d)
    reply, kind = hint, nil
    line, left, right = crumb, nil, nil
  end

  theme.commandLine(scr, { reply = reply, kind = kind, line = line, caret = true, left = left, right = right })
end

return view
