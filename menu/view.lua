-- The menu's screen in the r3i6nos-ansiapps theme: pure Lua (no LÖVE calls), so plain luajit can test it.
-- Row 0 the top bar; rows 1-20 the games list (a list dialog; Settings opens over it); rows 21-23 the command line.

local theme = require("r3i6nos_ansiapps")
local settings = require("settings")

local view = {}

local TYPE_LABEL = { love115 = "LÖVE 11.5", love120 = "LÖVE 12.0", native = "native" }

--- Games shown on a page, and the list's place on the screen.
view.LIST = { col = 0, row = theme.DISPLAY_TOP, w = theme.COLS, h = theme.DISPLAY_ROWS }
view.PER_PAGE = theme.listRows(view.LIST.h)

local function typeLabel(e) return TYPE_LABEL[e.type] or e.type end

--- Draw the whole screen. state: entries, selected (1-based), dialog (settings state), reply (optional { text, kind }).
function view.render(scr, state)
  scr:clear()
  local entries, sel = state.entries, state.selected
  local n = #entries
  theme.topBar(scr, "r3i6nos", ("%d %s"):format(n, n == 1 and "app" or "apps"))

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
    local confirm = d.view == "confirm"
    local items2, title, hint
    if confirm then
      items2 = { { label = "Yes, shut down" }, { label = "No" } }
      title, hint = "Shut down the device?", "Powers the device off"
    else
      items2 = {}
      for i, it in ipairs(settings.ITEMS) do items2[i] = { label = it.label } end
      title, hint = "Settings", settings.ITEMS[d.selected].hint
    end
    local w, h = 40, #items2 + 2
    theme.listDialog(scr, {
      col = math.floor((theme.COLS - w) / 2), row = theme.DISPLAY_TOP + math.floor((theme.DISPLAY_ROWS - h) / 2), w = w, h = h,
      title = title, items = items2, sel = confirm and d.confirm or d.selected, foot = "A Choose  B Back",
    })
    reply, kind = hint, nil
    line, left, right = (confirm and "shutdown" or "settings"), nil, nil
  end

  theme.commandLine(scr, { reply = reply, kind = kind, line = line, caret = true, left = left, right = right })
end

return view
