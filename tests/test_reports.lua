-- Run with: luajit tests/test_reports.lua   (from the repo root)
-- menu/reports.lua: finding the tools' <command>-report programs (through deb_watcher's wrappers), starting one safely, and its words.
package.path = "menu/?.lua;" .. package.path
local reports = require("reports")
local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

reports.DIR = "/opt/loveos/bin"
local files = {
  ["/opt/loveos/bin/compose-report"] = { "#!/bin/sh", "# r3i6nos-tool r36s-composer", 'LD_LIBRARY_PATH="/x${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" exec "/opt/native_apps/Composer/usr/bin/compose-report" "$@"' },
  ["/opt/native_apps/Composer/usr/bin/compose-report"] = { "#!/usr/bin/env python3", '"""doc"""', 'TOOL = "compose"', 'APP = "Composer"          # the app' },
  ["/opt/loveos/bin/animate-report"] = { "#!/bin/sh", 'exec "/opt/native_apps/Animator/usr/bin/animate-report" "$@"' },
  ["/opt/native_apps/Animator/usr/bin/animate-report"] = { 'APP = "Animator"' },
  ["/opt/loveos/bin/mystery-report"] = { "#!/bin/sh", "echo no app line" },
}
local ran = {}
local sys = {
  ls = function(dir) eq(dir, "/opt/loveos/bin"); return { "compose", "compose-report", "animate-report", "bad name-report", "x;rm -rf-report", "mystery-report", "report" } end,
  head = function(path) return files[path] or {} end,
  exec = function(cmd) ran[#ran + 1] = cmd; return true end,
  read = function() return nil end,
}

local list = reports.list(sys)
eq(#list, 3, "only safe *-report names")
eq(list[1].app, "Animator"); eq(list[2].app, "Composer"); eq(list[2].tool, "compose"); eq(list[2].cmd, "compose-report")
eq(list[3].app, "mystery", "no APP line: the tool's name")

local ok = reports.start(list[2], sys)
assert(ok, "starts")
assert(ran[1]:find("R3I6NOS_REPORT_PROGRESS='/run/loveos/report/progress' '/opt/loveos/bin/compose-report' --from-settings --quiet", 1, true), ran[1])
assert(ran[1]:find("echo \"exit=$?\" > '/run/loveos/report'/done.tmp; mv", 1, true) and ran[1]:sub(-3) == "& }", "only the report in the background, and it says when it ends")
local bad, why = reports.start({ cmd = "x;reboot" }, sys)
assert(not bad and why, "a name that is not a report command never reaches the shell")
eq(#ran, 1)
sys.exec = function() return nil end
assert(not reports.start(list[2], sys), "a failed start is false")

local text = table.concat(reports.ABOUT, "\n") .. table.concat(reports.NONE, "\n")
assert(not text:find("docs/") and not text:find("%.md"), "names no repository file")
for _, l in ipairs(reports.ABOUT) do assert(#l <= 60, "fits the pages: " .. l) end
-- r3i6nos's own report (the system, both cards) comes first, under its own name, with its own words
do
  local f2 = {
    ["/opt/loveos/bin/r3i6nos-report"] = { "#!/usr/bin/env python3", '"""doc"""', "import os", 'APP = "r3i6nos"          # read by the menu' },
    ["/opt/loveos/bin/compose-report"] = files["/opt/loveos/bin/compose-report"],
    ["/opt/native_apps/Composer/usr/bin/compose-report"] = files["/opt/native_apps/Composer/usr/bin/compose-report"],
  }
  local sys2 = { ls = function() return { "compose-report", "r3i6nos-report" } end, head = function(p) return f2[p] or {} end, exec = sys.exec, read = sys.read }
  local l2 = reports.list(sys2)
  eq(#l2, 2); eq(l2[1].cmd, "r3i6nos-report"); eq(l2[1].app, "r3i6nos (this system)"); assert(l2[1].system); eq(l2[2].app, "Composer")
end
-- progress, the end, the summary and where the file is (from fake files)
do
  local files3 = {}
  local fs = { read = function(p) return files3[p] end, exec = function() return true end }
  local r = { cmd = "compose-report", tool = "compose", app = "Composer" }
  eq(reports.running(fs), nil)
  files3["/run/loveos/report/running"] = "compose-report\n"
  eq(reports.running(fs), "compose-report")
  local ok, why = reports.start(r, fs); assert(not ok and why:find("still running", 1, true), "one report at a time")
  local p = reports.poll(r, fs)
  assert(not p.done and p.step == nil and p.file == "/roms/Composer/r36s-report.txt", "an old report: no steps, the expected file")
  files3["/run/loveos/report/progress"] = "step=3\nsteps=7\ndoing=compose song --bars 8\nfile=/roms2/Composer/r36s-report.txt\n"
  p = reports.poll(r, fs)
  eq(p.step, 3); eq(p.steps, 7); eq(p.doing, "compose song --bars 8"); eq(p.file, "/roms2/Composer/r36s-report.txt")
  files3["/run/loveos/report/progress"] = "step=9\nsteps=7\n"
  assert(reports.poll(r, fs).step == nil, "nonsense steps are not shown")
  files3["/run/loveos/report/done"] = "exit=0\n"
  p = reports.poll(r, fs); assert(p.done and p.exit == 0); eq(reports.running(fs), nil, "a finished report is not running")
  files3["/roms/Composer/r36s-report.txt"] = "# r3i6nos device report\n\n== summary ==\nPASS 18  FAIL 0  INFO 41  SKIP 0\nend\n"
  local sum = reports.summary("/roms/Composer/r36s-report.txt", fs)
  eq(sum.pass, 18); eq(sum.info, 41); assert(sum.ended)
  eq(reports.summary("/nowhere", fs), nil)
  local card, path = reports.where("/roms2/r3i6nos/r36s-report.txt"); eq(card, "the data card"); eq(path, "r3i6nos/r36s-report.txt")
  card, path = reports.where("/roms/Composer/r36s-report.txt"); eq(card, "the system card"); eq(path, "Composer/r36s-report.txt")
  local lines, good = reports.doneLines(r, { file = "/roms/Composer/r36s-report.txt" }, sum)
  assert(good); eq(lines[1], "Composer's report is done."); eq(lines[3], "PASS 18   FAIL 0   INFO 41   SKIP 0"); eq(lines[6], "  Composer/r36s-report.txt")
  lines, good = reports.doneLines({ cmd = "r3i6nos-report", app = "r3i6nos (this system)", system = true }, { file = "/roms2/r3i6nos/r36s-report.txt", exit = 1 }, nil)
  assert(not good); assert(lines[1]:find("The system report stopped", 1, true) and lines[1]:find("exit 1", 1, true))
  for _, l in ipairs(lines) do assert(#l <= 60, "fits the pages: " .. l) end
end

-- the real shell: a fake report that says its progress; the wrapper writes "done" with its exit status when it ends
do
  local tmp = (os.getenv("TMPDIR") or "/tmp"):gsub("/$", "") .. "/r3i6nos-reports-" .. os.time() .. "-" .. math.random(1e6)
  os.execute("mkdir -p '" .. tmp .. "/bin'")
  local f = io.open(tmp .. "/bin/fake-report", "w")
  f:write('#!/bin/sh\nprintf "step=1\\nsteps=2\\ndoing=one\\nfile=%s\\n" "$R3I6NOS_REPORT_PROGRESS.file" > "$R3I6NOS_REPORT_PROGRESS"\n',
          'echo "$@" > "$R3I6NOS_REPORT_PROGRESS.args"\nexit 3\n')
  f:close()
  os.execute("chmod +x '" .. tmp .. "/bin/fake-report'")
  local dir0, run0 = reports.DIR, reports.RUN
  reports.DIR, reports.RUN = tmp .. "/bin", tmp .. "/run"
  local r = { cmd = "fake-report", tool = "fake", app = "Fake" }
  assert(reports.start(r), "starts")
  eq(reports.running(), "fake-report", "running is written before start returns")
  local res
  for _ = 1, 200 do res = reports.poll(r); if res.done then break end; os.execute("sleep 0.05") end
  assert(res.done, "the wrapper wrote done"); eq(res.exit, 3, "with the report's exit status")
  eq(res.step, 1); eq(res.doing, "one"); eq(res.file, tmp .. "/run/progress.file")
  local a = io.open(tmp .. "/run/progress.args"):read("*l"); eq(a, "--from-settings --quiet")
  eq(reports.running(), nil)
  assert(reports.start(r), "a finished report can run again")
  assert(not reports.poll(r).done, "the last run's done is cleared before start returns")
  for _ = 1, 200 do res = reports.poll(r); if res.done then break end; os.execute("sleep 0.05") end
  assert(res.done)
  reports.DIR, reports.RUN = dir0, run0
  os.execute("rm -rf '" .. tmp .. "'")
end

print("reports: ok")
