-- Run with: luajit tests/test_demos.lua   (from the repo root)
-- menu/demos.lua: the lock and status files of a Rec demo, the tools' <command>-demo, starting one, and the guard on device reports.
package.path = "menu/?.lua;" .. package.path
local reports = require("reports")
local demos = require("demos")
local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

reports.DIR = "/opt/loveos/bin"
demos.RUN = "/run/loveos/demo"
local files, live, ran = {}, { [4242] = true }, {}
local sys = {
  ls = function() return { "compose-demo", "animate-demo", "bad name-demo", "x;rm-demo", "compose-report", "demo" } end,
  head = function(path)
    if path:find("compose-demo", 1, true) then return { "#!/bin/sh", 'APP = "Composer"' } end
    return {}
  end,
  read = function(p) return files[p] end,
  alive = function(pid) return live[pid] == true end,
  exec = function(cmd) ran[#ran + 1] = cmd; return true end,
}

-- the installed demos: only safe *-demo names, by app name
local l = demos.list(sys)
eq(#l, 2); eq(l[1].app, "animate"); eq(l[2].app, "Composer"); eq(l[2].cmd, "compose-demo")

-- no lock: nothing is recording
eq(demos.active(sys), false); eq(demos.poll(sys).running, false)
eq(demos.statusLines(demos.poll(sys))[1], "No Rec demo is recording.")

-- a live holder: recording, and the progress is read in the report's keys
files["/run/loveos/demo/lock"] = "4242\n"
files["/run/loveos/demo/running"] = "TinyNTT\n"
files["/run/loveos/demo/progress"] = "step=43\nsteps=100\ndoing=Showing: the piano roll\nfile=/roms/TinyNTT/Demo/rec-demo.txt\n"
eq(demos.active(sys), true)
local p = demos.poll(sys)
eq(p.running, true); eq(p.app, "TinyNTT"); eq(p.percent, 43); eq(p.doing, "Showing: the piano roll"); eq(p.file, "/roms/TinyNTT/Demo/rec-demo.txt")
local text = table.concat(demos.statusLines(p), "\n")
assert(text:find("TinyNTT", 1, true) and text:find("43 %", 1, true) and text:find("piano roll", 1, true) and text:find("Select+Start", 1, true), text)

-- while it records, a second start, and a device report, are refused
local ok, why = demos.start(l[2], sys)
assert(not ok and why:find("already recording", 1, true), tostring(why))
eq(#ran, 0)
local rok, rwhy = reports.start({ cmd = "compose-report" }, sys)
assert(not rok and rwhy:find("Rec demo is recording", 1, true), tostring(rwhy))

-- a lock left by a dead process blocks nothing
live[4242] = nil
eq(demos.active(sys), false)
ok = demos.start(l[2], sys)
assert(ok, "starts when nothing is recording")
assert(ran[1]:find("'/opt/loveos/bin/compose-demo' --from-settings --quiet", 1, true) and ran[1]:sub(-1) == "&", ran[1])
ok = demos.start({ cmd = "x;reboot" }, sys)
assert(not ok, "a name that is not a demo command never reaches the shell")
eq(#ran, 1)

-- it has ended: done, with its exit code and its log
files["/run/loveos/demo/lock"] = nil
files["/run/loveos/demo/done"] = "exit=0\n"
p = demos.poll(sys)
eq(p.running, false); eq(p.done, true); eq(p.exit, 0)
assert(table.concat(demos.statusLines(p), "\n"):find("finished", 1, true))
files["/run/loveos/demo/done"] = "exit=1\n"
assert(table.concat(demos.statusLines(demos.poll(sys)), "\n"):find("exit 1", 1, true))

for _, line in ipairs(demos.ABOUT) do assert(#line <= 60, "fits the pages: " .. line) end
local words = table.concat(demos.ABOUT, "\n") .. table.concat(demos.NONE, "\n") .. table.concat(demos.statusLines({ running = true }), "\n")
assert(not words:find("docs/") and not words:find("%.md"), "names no repository file")
print("demos: ok")
