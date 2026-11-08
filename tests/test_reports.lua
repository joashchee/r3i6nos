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
}

local list = reports.list(sys)
eq(#list, 3, "only safe *-report names")
eq(list[1].app, "Animator"); eq(list[2].app, "Composer"); eq(list[2].tool, "compose"); eq(list[2].cmd, "compose-report")
eq(list[3].app, "mystery", "no APP line: the tool's name")

local ok = reports.start(list[2], sys)
assert(ok, "starts")
eq(ran[1], "'/opt/loveos/bin/compose-report' --from-settings --quiet >/dev/null 2>&1 &", "quoted, in the background, from Settings")
local bad, why = reports.start({ cmd = "x;reboot" }, sys)
assert(not bad and why, "a name that is not a report command never reaches the shell")
eq(#ran, 1)
sys.exec = function() return nil end
assert(not reports.start(list[2], sys), "a failed start is false")

local lines = reports.startedLines(list[2])
eq(lines[1], "Composer's report is running.")
local text = table.concat(lines, "\n") .. table.concat(reports.ABOUT, "\n") .. table.concat(reports.NONE, "\n")
assert(text:find("Composer/r36s%-report%.txt"), "says where the file is")
assert(not text:find("docs/") and not text:find("%.md"), "names no repository file")
for _, l in ipairs(lines) do assert(#l <= 60, "fits the pages: " .. l) end
for _, l in ipairs(reports.ABOUT) do assert(#l <= 60, "fits the pages: " .. l) end
print("reports: ok")
