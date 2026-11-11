-- Device reports (docs/DEVICE-REPORTS.md): the command-line tools' <command>-report programs, installed by deb_watcher in the tools folder.
-- Settings > Device reports lists them and starts one in the background; it writes r36s-report.txt in its app's folder on the games card,
-- for the maintainer to give to Claude. Apps with a screen have their own Device report in their menu. Pure but for M.sys, so luajit tests it.

local M = {}

M.DIR = os.getenv("R3I6NOS_TOOLS_DIR") or "/opt/loveos/bin"
M.SYSTEM = "r3i6nos-report" -- the system's own report (overlay/usr/local/bin/r3i6nos-report, linked into M.DIR)
-- Where a running report says how far it has got and, when it ends, how it ended (docs/DEVICE-REPORTS.md, "Progress"):
--   progress  step=, steps=, doing=, file= lines, written by the report itself (newer reports only)
--   done      exit=N, written by the shell that started it, so every report, old or new, says when it has finished
--   running   the command's name, while one runs: one report at a time
M.RUN = os.getenv("R3I6NOS_REPORT_RUN") or "/run/loveos/report"
M.ROMS = os.getenv("R3I6NOS_ROMS") or "/roms"
M.WAIT = 900 -- seconds before the wait gives up (a tool's report runs every command it has; some take minutes)

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
  read = function(path)
    local f = io.open(path, "r")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
  end,
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

local function keyvals(s)
  local t = {}
  for k, v in (s or ""):gmatch("([%w_]+)=([^\n]*)") do t[k] = v end
  return t
end

--- Which report is running now (its command), or nil. A report that has finished is not running.
function M.running(sys)
  sys = sys or M.sys
  local name = sys.read(M.RUN .. "/running")
  if not name or sys.read(M.RUN .. "/done") then return nil end
  return (name:gsub("%s+$", ""))
end

--- Starts one in the background, through a shell that writes M.RUN/done when it ends. true, or false and why.
--- The clearing and the "running" file happen before this returns; only the report and its "done" run in the background
--- (`a && b && ( c ) &` would put the whole list in the background, and a poll could see the last run's "done").
function M.start(r, sys)
  sys = sys or M.sys
  if not (r and r.cmd and valid(r.cmd)) then return false, "not a report command" end
  if package.loaded["demos"] and package.loaded["demos"].active(sys) then return false, "a Rec demo is recording: the unit is busy until it ends" end
  local busy = M.running(sys)
  if busy then return false, busy .. " is still running: wait for it to finish" end
  local run = quote(M.RUN)
  local cmd = "mkdir -p " .. run .. " && rm -f " .. run .. "/progress " .. run .. "/done " .. run .. "/done.tmp && "
    .. "printf '%s\\n' " .. quote(r.cmd) .. " > " .. run .. "/running && "
    .. "{ ( R3I6NOS_REPORT_PROGRESS=" .. quote(M.RUN .. "/progress") .. " " .. quote(M.DIR .. "/" .. r.cmd) .. " --from-settings --quiet >/dev/null 2>&1; "
    .. "echo \"exit=$?\" > " .. run .. "/done.tmp; mv " .. run .. "/done.tmp " .. run .. "/done ) >/dev/null 2>&1 & }"
  local ok = sys.exec(cmd)
  if ok == true or ok == 0 then return true end
  return false, "could not start " .. r.cmd
end

--- Where the report's file goes when the report does not say (an older tool report): the app's folder on the system card.
function M.expectedFile(r)
  if r.system then return nil end
  return M.ROMS .. "/" .. r.app .. "/r36s-report.txt"
end

--- How far it has got: { done, exit, step, steps, doing, file }. step and steps are nil when the report does not say.
function M.poll(r, sys)
  sys = sys or M.sys
  local p = keyvals(sys.read(M.RUN .. "/progress"))
  local d = sys.read(M.RUN .. "/done")
  local res = {
    done = d ~= nil, exit = d and tonumber(keyvals(d).exit) or nil,
    step = tonumber(p.step), steps = tonumber(p.steps), doing = p.doing, file = p.file or M.expectedFile(r),
  }
  if res.step and res.steps and (res.steps < 1 or res.step > res.steps) then res.step, res.steps = nil, nil end
  return res
end

--- The report's own summary line: "PASS 18  FAIL 0  INFO 41  SKIP 0", or nil.
function M.summary(file, sys)
  sys = sys or M.sys
  local s = file and sys.read(file)
  if not s then return nil end
  local pass, fail, info, skip = s:match("PASS (%d+)  FAIL (%d+)  INFO (%d+)  SKIP (%d+)")
  if not pass then return nil end
  return { pass = tonumber(pass), fail = tonumber(fail), info = tonumber(info), skip = tonumber(skip), ended = s:match("\nend%s*$") ~= nil }
end

--- A report's file, as a person finds it: (which card, the path on it).
function M.where(file)
  if not file then return "the games card", "its app's folder" end
  local rest = file:match("^/roms2/(.+)$")
  if rest then return "the data card", rest end
  rest = file:match("^/roms/(.+)$")
  if rest then return "the system card", rest end
  return "this unit", file
end

--- What the screen says when it has finished: (lines, whether it ended well).
function M.doneLines(r, res, sum)
  local name = r.system and "The system report" or (r.app .. "'s report")
  local card, path = M.where(res.file)
  if sum and sum.ended then
    return {
      name .. " is done.",
      "",
      ("PASS %d   FAIL %d   INFO %d   SKIP %d"):format(sum.pass, sum.fail, sum.info, sum.skip),
      "",
      "Saved on " .. card .. " as",
      "  " .. path,
      "",
      "Copy it to a computer and give it to Claude.",
    }, true
  end
  return {
    name .. " stopped without a whole file" .. (res.exit and (" (exit " .. res.exit .. ").") or "."),
    "",
    "What it wrote before it stopped is on " .. card .. " as",
    "  " .. path .. ".tmp",
  }, false
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
