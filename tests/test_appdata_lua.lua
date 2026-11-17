-- Run with: luajit tests/test_appdata_lua.lua   (from the repo root; needs python3 for the real engine)
-- theme/r3i6nos_appdata.lua, the Lua frontend of r3i6nos-appdata (CLAUDE.md rule 16, docs/APP-DATA.md), against the real engine on fake cards
-- and against small fake engines (a slow one that can be stopped, a failing one). Nothing is drawn: the screens are read as text.
-- The same cases as tests/test_appdata.c.
package.path = "theme/?.lua;" .. package.path
local appdata = require("r3i6nos_appdata")
local toast = require("r3i6nos_toast")

local fails = 0
local function check(c, m) if not c then fails = fails + 1; io.stderr:write("FAIL: " .. m .. "\n") end end
local function sh(cmd) return os.execute(cmd) end
local function q(s) return "'" .. s:gsub("'", "'\\''") .. "'" end

local root = (os.getenv("TMPDIR") or "/tmp"):gsub("/$", "") .. "/r3-appdata-lua-" .. os.time() .. "-" .. math.random(1e6)
sh("mkdir -p " .. q(root))
local sysAt, dataAt = root .. "/roms", root .. "/roms2"
local realEngine = os.getenv("R3I6NOS_APPDATA_ENGINE") or "overlay/usr/local/bin/r3i6nos-appdata"
local APP = "Commander"

local function wf(path, text)
  sh("mkdir -p " .. q((path:gsub("/[^/]*$", ""))))
  local f = assert(io.open(path, "w"))
  f:write(text)
  f:close()
end
local function rf(path)
  local f = io.open(path, "r")
  if not f then return nil end
  local s = f:read("*a")
  f:close()
  return s
end
local function exists(path) return io.open(path, "r") ~= nil end
local function dirExists(path) return sh("test -d " .. q(path)) == 0 or sh("test -d " .. q(path)) == true end
local function cards(system, data, fs)
  wf(root .. "/cards", ("SYSTEM_AT=%s\nSYSTEM_FS=exfat\nDATA_AT=%s\nDATA_FS=%s\n"):format(system and sysAt or "none", data and dataAt or "none", fs))
end

-- the screens, as text, and the person's presses from a script
local seenText, screens, script, at = "", 0, {}, 1
local ui = {
  now = function() return os.clock() end,
  sleep = function() end,
  present = function() end,
  seen = function(scr)
    screens = screens + 1
    seenText = seenText .. table.concat(scr:dump(), "\n") .. "\n"
  end,
  next = function(wait)
    if at > #script then return wait > 0 and "back" or nil end   -- a dialog waiting for a press gets B (no hang); a bar polling gets nothing
    at = at + 1
    local k = script[at - 1]
    return k ~= "none" and k or nil
  end,
}
local function keys(...) script, at, seenText, screens = { ... }, 1, "", 0 end
local function shows(s) return seenText:find(s, 1, true) ~= nil end

local function fresh()
  sh(("rm -rf %s %s %s"):format(q(root .. "/roms"), q(root .. "/roms2"), q(root .. "/state")))
  sh("mkdir -p " .. q(sysAt) .. " " .. q(dataAt))
  cards(true, true, "exfat")
  appdata.setenv("R3I6NOS_APP_DATA", nil)
  appdata.setenv("R3I6NOS_DATA_CARD", nil)
  appdata.setenv("R3I6NOS_APPDATA", realEngine)
end
local function put(card, rel, text) wf(card .. "/" .. APP .. "/" .. rel, text) end
local function init(opts)
  appdata.setenv("R3I6NOS_APP_DATA", nil)      -- a new launch is a new process: what the last one exported is not there
  appdata.setenv("R3I6NOS_DATA_CARD", nil)
  local o = { toast = nil }
  for k, v in pairs(ui) do o[k] = v end
  for k, v in pairs(opts or {}) do o[k] = v end
  return appdata.init(APP, o)
end
local function fakeEngine(name, body)
  local p = root .. "/" .. name
  wf(p, body)
  sh("chmod 755 " .. q(p))
  return p
end

appdata.setenv("R3I6NOS_CARDS_FILE", root .. "/cards")
appdata.setenv("R3I6NOS_APPDATA_STATE", root .. "/state")
appdata.setenv("R3I6NOS_POWER_SUPPLY", root .. "/nopower")
appdata.setenv("R3I6NOS_APPDATA_MIN_FREE_MB", "0")

-- parsing
local st = appdata.parseState("card=valid\naction=offer-move\nuse=/x/y\ns_files=3\ns_bytes=3000\nconflicts=2\nodd line\n")
check(st.card == "valid" and st.action == "offer-move" and st.use == "/x/y" and st.s_files == 3 and st.s_bytes == 3000 and st.conflicts == 2, "state lines parse")
check(appdata.human(12) == "12 B" and appdata.human(1536) == "1.5 KB", "sizes read well")

-- a caller that has decided: used as it is, no engine, no question
fresh()
appdata.setenv("R3I6NOS_APPDATA", "/nonexistent/engine")
appdata.setenv("R3I6NOS_APP_DATA", root .. "/decided")
keys("accept")
local d = appdata.init(APP, ui)
check(d.use == root .. "/decided" and dirExists(d.use), "a preset R3I6NOS_APP_DATA is used and made")
check(screens == 0, "and nothing is asked")

-- no engine at all: the old place, said so
fresh()
appdata.setenv("R3I6NOS_APPDATA", "/nonexistent/engine")
appdata.setenv("R3I6NOS_ROMS", sysAt)
d = init()
check(d.use == sysAt .. "/" .. APP and not d.engine and d.notice:find("not available", 1, true), "no engine: the old place and a notice")
check(os.getenv("R3I6NOS_APP_DATA") == sysAt .. "/" .. APP, "no engine: exported")
appdata.setenv("R3I6NOS_ROMS", nil)

-- nothing anywhere, a valid data card: start there, no question
fresh()
keys("accept")
d = init()
check(d.use == dataAt .. "/" .. APP and dirExists(d.use) and d.engine and screens == 0, "no data anywhere: the data card, no dialog")
check(d.dataCard == dataAt and os.getenv("R3I6NOS_DATA_CARD") == dataAt, "the data card's root is exported")

-- quiet: the same answer with no dialog even when there is a question to ask
fresh()
put(sysAt, "save.dat", "mine")
appdata.setenv("R3I6NOS_APP_DATA", nil)
d = appdata.quiet(APP)
check(d.use == sysAt .. "/" .. APP and exists(sysAt .. "/" .. APP .. "/save.dat"), "quiet: data only on the system card is used where it is, never moved")

-- data only on the system card: the offer starts on Not now
fresh()
put(sysAt, "save.dat", "twelve bytes")
keys("accept")
d = init()
check(shows("Move Commander's data to the data card?") and shows("Move all") and shows("Keep on the system card") and shows("Not now"), "the move offer is worded")
check(shows("12 B in 1 files"), "it says how much")
check(d.use == sysAt .. "/" .. APP and exists(sysAt .. "/" .. APP .. "/save.dat"), "Not now (the default): the system card is used, nothing moved")
keys("back")
d = init()
check(d.use == sysAt .. "/" .. APP, "B: not now")

-- Move all: up twice from Not now
keys("up", "up", "accept")
d = init()
check(d.use == dataAt .. "/" .. APP, "after Move all the data card is used")
check(rf(dataAt .. "/" .. APP .. "/save.dat") == "twelve bytes", "the file is on the data card, whole")
check(not dirExists(sysAt .. "/" .. APP), "and gone from the system card")
check(shows("Moving Commander's data") and shows("1 of 1 files"), "a progress screen showed (step, bar, count)")
keys("accept")
d = init()
check(screens == 0 and d.use == dataAt .. "/" .. APP, "settled: nothing is asked again")

-- Keep on the system card, and not asked again
fresh()
put(sysAt, "save.dat", "keep me")
keys("up", "accept")
d = init()
check(d.use == sysAt .. "/" .. APP, "keep: the system card is used")
keys("accept")
d = init()
check(screens == 0 and d.use == sysAt .. "/" .. APP, "keep: not asked again")

-- both cards: the merge dialog, and merge into the data card
fresh()
put(sysAt, "a.txt", "from system")
put(dataAt, "b.txt", "from data")
put(sysAt, "same.txt", "system version")
put(dataAt, "same.txt", "data version!!")
keys("accept")
d = init()
check(shows("Commander has data on both cards:") and shows("Merge into the data card (recommended)") and shows("Merge into the system card") and shows("Don't ask again"), "the merge offer is worded")
check(shows("1 files are on both and differ"), "it counts the files that differ")
check(d.use == dataAt .. "/" .. APP and exists(sysAt .. "/" .. APP .. "/a.txt"), "not now: the data card is used meanwhile, nothing merged")
keys("up", "up", "accept")
d = init()
check(exists(dataAt .. "/" .. APP .. "/a.txt") and not dirExists(sysAt .. "/" .. APP), "merged: the system card's file is on the data card, its folder gone")
check(shows("Merging Commander's data"), "merging has its own title")
check(sh("ls " .. q(dataAt .. "/" .. APP .. "/.merge-conflicts") .. "/*/same.txt >/dev/null 2>&1") == 0
      or sh("ls " .. q(dataAt .. "/" .. APP .. "/.merge-conflicts") .. "/*/same.txt >/dev/null 2>&1") == true, "the older of the two different files was kept, not deleted")

-- merge into the system card: second choice
fresh()
put(sysAt, "a.txt", "s")
put(dataAt, "b.txt", "d")
keys("up", "accept")
d = init()
check(d.use == sysAt .. "/" .. APP and exists(sysAt .. "/" .. APP .. "/b.txt"), "merged into the system card")

-- Don't ask again
fresh()
put(sysAt, "a.txt", "s")
put(dataAt, "b.txt", "d")
keys("down", "accept")
init()
keys("accept")
init()
check(screens == 0, "don't ask again: not asked")

-- the data card is out after being used: Needs the data card
fresh()
put(dataAt, "save.dat", "on the data card")
keys("accept")
init()
cards(true, false, "exfat")
keys("accept")
d = init()
check(shows("Needs the data card.") and shows("Use the system card (new, separate data)") and shows("Quit"), "needs-card is worded")
check(d.quit, "the default is Quit")
keys("up", "accept")
d = init()
check(not d.quit and d.use == sysAt .. "/" .. APP, "the system card for now")

-- a card that cannot be used: a notice, no dialog
fresh()
put(sysAt, "save.dat", "x")
appdata.setenv("R3I6NOS_APPDATA_MIN_FREE_MB", "999999999")
keys("accept")
d = init()
appdata.setenv("R3I6NOS_APPDATA_MIN_FREE_MB", "0")
check(screens == 0 and d.use == sysAt .. "/" .. APP and d.notice:find("free", 1, true) and d.notice:find("system card", 1, true), "a full data card: the system card, with a notice")
cards(true, true, "ntfs")
d = init()
check(d.notice:find("filesystem", 1, true), "an unsupported filesystem is said")

-- a failing engine: the reason is shown, the app goes on
fresh()
local stateScript = [[
  state) printf 'card=valid\naction=offer-move\nuse=%s/Commander\ns_at=%s/Commander\nd_at=%s/Commander\ns_files=3\ns_bytes=3000\n' "$R3_T_SYS" "$R3_T_SYS" "$R3_T_DATA" ;;
]]
appdata.setenv("R3_T_SYS", sysAt)
appdata.setenv("R3_T_DATA", dataAt)
appdata.setenv("R3I6NOS_APPDATA", fakeEngine("fail-engine", "#!/bin/sh\ncase \"$1\" in\n" .. stateScript ..
  "  move) echo 'STEP Planning'; echo 'FAIL The target card has 1 KB free'; exit 1 ;;\n  *) exit 0 ;;\nesac\n"))
keys("up", "up", "accept", "accept")
d = init()
check(d.use ~= "" and shows("It did not finish") and shows("The target card has 1 KB free"), "the engine's reason is shown")

-- B stops the work: the engine is asked to, and the person is told
fresh()
appdata.setenv("R3I6NOS_APPDATA", fakeEngine("slow-engine", "#!/bin/sh\ncase \"$1\" in\n" .. stateScript ..
  "  move) trap 'echo \"FAIL stopped; what is done stays done\"; exit 1' TERM\n" ..
  "        echo 'STEP Copying'; echo 'PROGRESS 1/3'\n" ..
  "        i=0; while [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done; echo 'DONE copied=3 deleted=3 conflicts=0 skipped=0' ;;\n" ..
  "  *) exit 0 ;;\nesac\n"))
-- the bar polls with nothing pressed for a while (the engine prints its first lines), then B, then the OK of "It did not finish"
local slowKeys = { "up", "up", "accept" }
for _ = 1, 8 do slowKeys[#slowKeys + 1] = "none" end
slowKeys[#slowKeys + 1] = "back"
ui.sleep = function() sh("sleep 0.05") end                          -- real time passes so the engine can print and die
local t0 = os.time()
keys((table.unpack or unpack)(slowKeys))
-- the OK press has to come after the bar is gone: a script cannot know when, so answer a waiting dialog (wait > 0) with accept
local plain = ui.next
ui.next = function(wait)
  if at > #script then return wait > 0 and "accept" or nil end
  return plain(wait)
end
d = init()
ui.next, ui.sleep = plain, function() end
check(os.time() - t0 < 8, "a stopped move does not run to its end")
check(shows("1 of 3 files"), "the bar shows the work")
check(shows("stopped; what is done stays done"), "the engine's stop is told in its own words")
check(shows("It did not finish"), "a stopped move is told")

-- the hot-plug toast keeps showing during a dialog and its key is used up
fresh()
put(sysAt, "save.dat", "x")
local tdir = root .. "/hotplug"
sh("mkdir -p " .. q(tdir))
local tt = toast.new(tdir)
wf(tdir .. "/events", ("%d\terror\tUSB device failed\n"):format(9e9 - 1))
keys("accept")
local sawToast = false
local seenPlain = ui.seen
ui.seen = function(scr) seenPlain(scr) if scr:dump()[1]:find("USB device failed", 1, true) then sawToast = true end end
local lastKey = ui.next
local pressed = 0
ui.next = function(wait)
  pressed = pressed + 1
  if pressed == 1 then return nil end          -- one frame, for the toast to appear
  if pressed == 2 then return "accept" end     -- this press clears the failure toast and does nothing else (toast.key is what the LÖVE default calls)
  return lastKey(wait)
end
init({ toast = tt })
ui.next, ui.seen = lastKey, seenPlain
check(sawToast, "a failure toast shows over the top bar during the dialog")

sh("rm -rf " .. q(root))
if fails > 0 then io.stderr:write(("test_appdata_lua: %d failure(s)\n"):format(fails)); os.exit(1) end
print("test_appdata_lua: ok")
