-- Rec demos (docs/DEMO-RECORDINGS.md): the command-line tools' <command>-demo programs, and the lock and status files every recording writes
-- in /run/loveos/demo/ (lock, running, progress, done). Settings > Rec demos lists the tools' demos, starts one, and shows how far a recording
-- has got, whoever started it (an app's own Rec demo item too). While a recording runs, nothing else starts: the unit's whole power is its.
-- Pure but for the reports module's sys (luajit tests it).

local reports = require("reports")
local M = {}

M.RUN = (os.getenv("R3I6NOS_RUN") or "/run/loveos") .. "/demo"
M.WAIT = 900

local function quote(s) return "'" .. tostring(s):gsub("'", "'\\''") .. "'" end
local function valid(name) return name:match("^[%w%._%-]+%-demo$") ~= nil end

local function keyvals(s)
  local t = {}
  for k, v in (s or ""):gmatch("([%w_]+)=([^\n]*)") do t[k] = v end
  return t
end

--- Whether a process is alive: /proc/<pid> (tests replace sys.alive).
local function alive(sys, pid)
  if sys.alive then return sys.alive(pid) end
  local f = io.open("/proc/" .. pid .. "/stat", "r")
  if f then f:close() return true end
  return false
end

--- The pid in the lock if that process is alive, else nil (a lock left by a dead recording does not block anything).
function M.lockHolder(sys)
  sys = sys or reports.sys
  local s = sys.read(M.RUN .. "/lock")
  local pid = s and tonumber(s:match("%d+"))
  if pid and alive(sys, pid) then return pid end
  return nil
end

--- A recording is under way: true. Refuse to start a report, a network helper or an update check while it is.
function M.active(sys) return M.lockHolder(sys) ~= nil end

--- The installed demos, sorted: { { cmd = "compose-demo", tool = "compose", app = "Composer" }, ... }.
function M.list(sys)
  sys = sys or reports.sys
  local out = {}
  for _, name in ipairs(sys.ls(reports.DIR)) do
    if valid(name) then
      local tool = name:gsub("%-demo$", "")
      local app
      for _, line in ipairs(sys.head(reports.DIR .. "/" .. name, 40)) do
        app = app or line:match('^APP%s*=%s*"([^"]+)"')
      end
      out[#out + 1] = { cmd = name, tool = tool, app = app or tool }
    end
  end
  table.sort(out, function(a, b) return a.app:lower() < b.app:lower() end)
  return out
end

--- The recording now: { running, app, percent, doing, file, done, exit } (running is nil when none is under way).
function M.poll(sys)
  sys = sys or reports.sys
  local holder = M.lockHolder(sys)
  local p = keyvals(sys.read(M.RUN .. "/progress"))
  local d = sys.read(M.RUN .. "/done")
  local app = (sys.read(M.RUN .. "/running") or ""):match("^[^\n]+")
  local step, steps = tonumber(p.step), tonumber(p.steps)
  return {
    running = holder ~= nil, app = app, doing = p.doing, file = p.file,
    percent = step and steps and steps > 0 and math.floor(step * 100 / steps) or nil,
    done = d ~= nil and holder == nil, exit = d and tonumber(keyvals(d).exit) or nil,
  }
end

--- Starts a tool's demo in the background. true, or false and why. Refused while another recording runs.
function M.start(r, sys)
  sys = sys or reports.sys
  if not (r and r.cmd and valid(r.cmd)) then return false, "not a demo command" end
  if M.active(sys) then return false, "a Rec demo is already recording: wait for it to finish" end
  local ok = sys.exec(quote(reports.DIR .. "/" .. r.cmd) .. " --from-settings --quiet >/dev/null 2>&1 &")
  if ok == true or ok == 0 then return true end
  return false, "could not start " .. r.cmd
end

--- What the screen says about a recording: lines.
function M.statusLines(p)
  if p.running then
    return {
      "A Rec demo is recording" .. (p.app and (": " .. p.app) or "") .. ".",
      "",
      p.percent and ("Progress  %d %%"):format(p.percent) or "Progress  not reported",
      p.doing and p.doing ~= "" and ("Doing     " .. p.doing) or "",
      "",
      "The unit is busy until it ends: reports, the network and",
      "updates wait. Hold Select+Start on the unit to cancel.",
    }
  end
  if p.done then
    return {
      (p.app or "The last Rec demo") .. " finished" .. ((p.exit or 0) == 0 and "." or (" with exit " .. tostring(p.exit) .. ".")),
      "",
      p.file and ("Its log: " .. p.file) or "Its files are in its Demo folder on the games card.",
      "Copy the Demo folder to a computer to see it.",
    }
  end
  return { "No Rec demo is recording." }
end

M.ABOUT = {
  "A Rec demo records screenshots and a short video of an",
  "app, with its own sound, for its store page. It runs the",
  "app through a tour of the features you tick, on a demo",
  "file made for it, and saves them in the app's Demo folder",
  "on the games card.",
  "",
  "While it records, the unit is locked so all its power goes",
  "to the recording: nothing else starts. Hold Select+Start",
  "to cancel. Apps with a screen have Rec demo in their Start",
  "menu; command-line tools are listed here.",
  "",
  "Nothing is sent anywhere: the files stay on the card.",
}

M.NONE = {
  "No command-line tool with a Rec demo is installed.",
  "",
  "Apps with a screen have Rec demo in their own Start menu.",
}

return M
