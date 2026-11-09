-- Device reports (docs/DEVICE-REPORTS.md): the command-line tools' <command>-report programs, installed by deb_watcher in the tools folder.
-- Settings > Device reports lists them and starts one in the background; it writes r36s-report.txt in its app's folder on the games card,
-- for the maintainer to give to Claude. Apps with a screen have their own Device report in their menu. Pure but for M.sys, so luajit tests it.

local M = {}

M.DIR = os.getenv("R3I6NOS_TOOLS_DIR") or "/opt/loveos/bin"
M.SYSTEM = "r3i6nos-report" -- the system's own report (overlay/usr/local/bin/r3i6nos-report, linked into M.DIR)

local function quote(s) return "'" .. tostring(s):gsub("'", "'\\''") .. "'" end

--- What touches the system; tests replace it.
M.sys = {
  ls = function(dir)
    local p = io.popen("ls -1 " .. quote(dir) .. " 2>/dev/null")
    if not p then return {} end
    local out = {}
    for line in p:lines() do out[#out + 1] = line end
    p:close()
    return out
  end,
  -- the first lines of a file (a wrapper or the report itself), for its app's name
  head = function(path, n)
    local f = io.open(path, "r")
    if not f then return {} end
    local out = {}
    for line in f:lines() do
      out[#out + 1] = line
      if #out >= n then break end
    end
    f:close()
    return out
  end,
  exec = function(cmd) return os.execute(cmd) end,
}

--- A safe command name: letters, digits, dot, dash, underscore, ending in -report.
local function valid(name) return name:match("^[%w%._%-]+%-report$") ~= nil end

--- The app a report is for: its APP = "..." line, following deb_watcher's wrapper to the script it runs.
local function appOf(sys, path, depth)
  for _, line in ipairs(sys.head(path, 40)) do
    local app = line:match('^APP%s*=%s*"([^"]+)"')
    if app then return app end
    local target = line:match('exec%s+"([^"]+)"')
    if target and (depth or 0) < 2 then return appOf(sys, target, (depth or 0) + 1) end
  end
  return nil
end

--- The installed reports, sorted: { { cmd = "compose-report", tool = "compose", app = "Composer" }, ... }.
function M.list(sys)
  sys = sys or M.sys
  local out = {}
  for _, name in ipairs(sys.ls(M.DIR)) do
    if valid(name) then
      local tool = name:gsub("%-report$", "")
      out[#out + 1] = { cmd = name, tool = tool, app = appOf(sys, M.DIR .. "/" .. name) or tool }
    end
  end
  for _, r in ipairs(out) do if r.cmd == M.SYSTEM then r.app, r.system = "r3i6nos (this system)", true end end
  -- r3i6nos's own report first, then the tools by name
  table.sort(out, function(a, b)
    if a.system ~= b.system then return a.system == true end
    return a.app:lower() < b.app:lower()
  end)
  return out
end

--- Starts one in the background (it takes a minute or more). true, or false and why.
function M.start(r, sys)
  sys = sys or M.sys
  if not (r and r.cmd and valid(r.cmd)) then return false, "not a report command" end
  local ok = sys.exec(quote(M.DIR .. "/" .. r.cmd) .. " --from-settings --quiet >/dev/null 2>&1 &")
  if ok == true or ok == 0 then return true end
  return false, "could not start " .. r.cmd
end

--- What the screen says once one is started.
function M.startedLines(r)
  if r.system then
    return {
      "The system report is running.",
      "",
      "It checks this unit's build, boot and both card slots,",
      "and times each card. It takes about a minute.",
      "",
      "Then, on a computer, copy this file from the data card",
      "(or the system card, when there is no data card):",
      "  r3i6nos/r36s-report.txt",
      "and give it to Claude.",
    }
  end
  return {
    r.app .. "'s report is running.",
    "",
    "It runs " .. r.tool .. "'s own commands on this unit, timed, and",
    "checks the games card. It takes a minute or two.",
    "",
    "Then, on a computer, copy this file from the games card:",
    "  " .. r.app .. "/r36s-report.txt",
    "and give it to Claude.",
  }
end

M.ABOUT = {
  "A device report checks this unit for an app's maker:",
  "the processor, memory, sound, the pad, the games card,",
  "and how fast the app runs here. It is one text file,",
  "r36s-report.txt, in the app's folder on the games card.",
  "",
  "Copy it to a computer and give it to Claude, which reads",
  "it and fixes the app for this handheld.",
  "",
  "Listed here: r3i6nos itself, and command-line tools.",
  "Apps with a screen have Device report in their Start menu.",
  "",
  "Nothing is sent anywhere: the file stays on the card.",
}

M.NONE = {
  "No command-line tool with a report is installed.",
  "",
  "Tools (Composer, Painter, Animator) install from .deb files",
  "in native_deb on the games card. Apps with a screen have",
  "Device report in their own Start menu.",
}

return M
