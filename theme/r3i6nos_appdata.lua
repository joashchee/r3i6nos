-- r3i6nos app data for LÖVE apps: CLAUDE.md rule 16, docs/APP-DATA.md. MIT, like the rest of theme/. The C twin is r3i6nos_appdata.h.
--
-- Where an app keeps its data is decided once, by the engine `r3i6nos-appdata` (written once, for every app, native or LÖVE). This is the
-- frontend: it asks the engine (`state`), shows the question in the theme when there is one, runs the move or merge behind a progress bar the
-- person can stop, and gives the app the folder to use.
--
--   local appdata = require("r3i6nos_appdata")
--   -- in love.load, after theme.init() and the splash (rule 12), before heavy work:
--   local data = appdata.init("IO_Checker", { toast = hotplug })     -- the toast is optional: it keeps showing and eats its key
--   if data.quit then love.event.quit() return end                    -- the person chose Quit ("Needs the data card")
--   -- write user data only under data.use (also $R3I6NOS_APP_DATA; $R3I6NOS_DATA_CARD is the data card's root or ""), with io.open:
--   -- love.filesystem writes only to LÖVE's save directory, in the root filesystem, which a reflash replaces and which is not on a card.
--   if data.notice ~= "" then say(data.notice, "warn") end             -- the data card is read-only, full, ...
--
-- An unattended run (a report, a Rec demo, a screenshot) calls appdata.quiet("IO_Checker"): no dialog, nothing moved, the folder the engine
-- resolves (the data card's when it is valid), made. If $R3I6NOS_APP_DATA is already set (a test, a caller that decided) it is used as it is,
-- by both. No engine (a laptop, an old image): the old place, $R3I6NOS_ROMS or /roms, plus "/<App>", and a notice says so.
--
-- init is modal: it runs its own loop (love.event.pump, theme.draw, love.graphics.present) until the question is answered and any move or merge
-- is done, as C's does, so the caller's code reads the same in both. The engine is run, never linked: `R3I6NOS_APPDATA` names another one
-- (tests). A move or merge runs in the background (sh, one file for its output, one for its pid) and is polled, so the screen keeps drawing, B
-- can stop it, and no LÖVE module is needed (love.thread may be off). Pure Lua but for io.popen, os.execute and, to export the folder, ffi
-- setenv (LuaJIT), so luajit tests it; opts.next, opts.present and opts.sleep replace LÖVE's parts there.

local T = require("r3i6nos_ansiapps")
local M = {}

M.NAMES = { none = "none", offerMove = "offer-move", offerMerge = "offer-merge", needsCard = "needs-card", resume = "resume" }

-- ---- the shell, the environment ---------------------------------------------------------------------------------------------------

local function q(s) return "'" .. tostring(s):gsub("'", "'\\''") .. "'" end

local okffi, ffi = pcall(require, "ffi")
if okffi then
  pcall(ffi.cdef, "int setenv(const char *name, const char *value, int overwrite); int unsetenv(const char *name);")
end

--- Set (or, with nil, remove) an environment variable of this process, so what the app starts and os.getenv see it. false when it cannot be.
function M.setenv(name, value)
  if not okffi then return false end
  if value == nil then return ffi.C.unsetenv(name) == 0 end
  return ffi.C.setenv(name, value, 1) == 0
end

local function env(name)
  local v = os.getenv(name)
  if v and v ~= "" then return v end
end

local function engine() return env("R3I6NOS_APPDATA") or "r3i6nos-appdata" end

local function ok(r) return r == true or r == 0 end

--- Run the engine to the end and keep what it printed. Returns the text, and false when it could not be started.
function M.capture(...)
  local cmd = q(engine())
  for _, a in ipairs({ ... }) do cmd = cmd .. " " .. q(a) end
  local p = io.popen(cmd .. " 2>/dev/null", "r")
  if not p then return "", false end
  local out = p:read("*a") or ""
  p:close()                  -- the exit status is not told by every LuaJIT; the caller reads what was printed (state's action, a FAIL line)
  return out, true
end

local function mkdirs(path)
  return ok(os.execute("mkdir -p " .. q(path) .. " 2>/dev/null"))
end
M.mkdirs = mkdirs

local function human(n)
  local unit, u = { "B", "KB", "MB", "GB", "TB" }, 1
  while n >= 1024 and u < 5 do n, u = n / 1024, u + 1 end
  if u == 1 then return ("%.0f %s"):format(n, unit[u]) end
  return ("%.1f %s"):format(n, unit[u])
end
M.human = human

-- ---- state ------------------------------------------------------------------------------------------------------------------------

--- Parse `state`'s key=value lines. Numbers (s_files, d_files, s_bytes, d_bytes, conflicts) are numbers; everything else a string.
function M.parseState(text)
  local st = { card = "", action = "", use = "", s_at = "", d_at = "", reason = "", s_files = 0, d_files = 0, s_bytes = 0, d_bytes = 0, conflicts = 0 }
  for line in (text or ""):gmatch("[^\n]+") do
    local k, v = line:match("^([%w_]+)=(.*)$")
    if k then
      if st[k] ~= nil and type(st[k]) == "number" then st[k] = tonumber(v) or 0 else st[k] = v end
    end
  end
  return st
end

--- `state APP`: the table, or nil when the engine did not answer with an action.
function M.state(app)
  local out, good = M.capture("state", app)
  if not good then return nil end
  local st = M.parseState(out)
  if st.action == "" then return nil end
  return st
end

-- ---- settling on a folder ---------------------------------------------------------------------------------------------------------

local function dirOf(path)
  return (path:gsub("/[^/]*$", ""))
end

-- Make the folder, tell the engine where the app is using its data (for "Needs the data card" later) and export it.
local function settle(d, app, use, dataAppDir)
  d.use = use
  mkdirs(use)
  d.dataCard = ""
  if dataAppDir and dataAppDir ~= "" then d.dataCard = dirOf(dataAppDir) end
  if d.engine then
    local onData = dataAppDir and dataAppDir ~= "" and use == dataAppDir
    M.capture("used", app, onData and "data" or "system")
  end
  M.setenv("R3I6NOS_APP_DATA", d.use)
  M.setenv("R3I6NOS_DATA_CARD", d.dataCard)
  return d
end

local function newData() return { use = "", dataCard = "", notice = "", quit = false, engine = false } end

-- The old place, when there is no engine: $R3I6NOS_ROMS or /roms, plus the app's folder.
local function legacy(d, app)
  local use = (env("R3I6NOS_ROMS") or "/roms") .. "/" .. app
  d.engine = false
  d.notice = "r3i6nos-appdata is not available; keeping data in " .. use
  return settle(d, app, use, nil)
end

local function preset(d)
  local p = env("R3I6NOS_APP_DATA")
  if not p then return nil end
  d.engine, d.use, d.dataCard = false, p, env("R3I6NOS_DATA_CARD") or ""
  mkdirs(p)
  return d
end

--- No dialog, nothing moved: the folder the engine resolves (the data card's when it is valid), made and exported.
function M.quiet(app)
  local d = newData()
  if preset(d) then return d end
  local st = M.state(app)
  if not st or st.use == "" then return legacy(d, app) end
  d.engine = true
  if st.card ~= "valid" and st.card ~= "none" and st.reason ~= "" then d.notice = st.reason .. " Using the system card." end
  return settle(d, app, st.use, st.d_at)
end

-- ---- a move or merge, in the background ---------------------------------------------------------------------------------------------

-- A name for a scratch file: os.tmpname() (in /tmp), else under $TMPDIR (a laptop's /tmp may not be writable); the file is not made.
local seq = 0
local function tmpname()
  local ok1, n = pcall(os.tmpname)
  if ok1 and n then os.remove(n) return n end
  seq = seq + 1
  return ("%s/r3i6nos-appdata-%d-%d-%d"):format((env("TMPDIR") or "/tmp"):gsub("/$", ""), os.time(), math.random(1e6), seq)
end

--- Start the engine's `verb APP [arg]` in the background. Returns a job: poll it, stop it, and read its lines.
function M.start(verb, app, arg)
  local out, pidf = tmpname(), tmpname()
  local args = q(verb) .. " " .. q(app) .. (arg and (" " .. q(arg)) or "")
  -- exec: the pid written is the engine's own, so a kill reaches it (it stops between files on SIGTERM)
  local script = ("echo $$ > %s; exec %s %s > %s 2>/dev/null"):format(q(pidf), q(engine()), args, q(out))
  os.execute(("sh -c %s </dev/null >/dev/null 2>&1 &"):format(q(script)))
  return { out = out, pidf = pidf, step = "Starting", done = 0, total = 0, finished = false, ok = false, why = "r3i6nos-appdata could not run",
           stopped = false, startedAt = os.time(), clock0 = os.clock() }
end

local function slurp(path)
  local f = io.open(path, "r")
  if not f then return "" end
  local s = f:read("*a") or ""
  f:close()
  return s
end

-- Running, and not a zombie (a parent that does not reap, as a container's first process, leaves the dead engine answering kill -0).
local function alive(pid)
  if not ok(os.execute("kill -0 " .. pid .. " 2>/dev/null")) then return false end
  local stat = slurp("/proc/" .. pid .. "/stat")
  return stat:match("^%d+ %b() (%a)") ~= "Z"
end

--- Read what the engine has printed so far and decide whether it is over. Safe to call every frame.
function M.poll(job)
  if job.finished then return job end
  for line in slurp(job.out):gmatch("[^\n]+") do
    if line:sub(1, 5) == "STEP " then job.step = line:sub(6)
    elseif line:sub(1, 9) == "PROGRESS " then
      local a, b = line:match("^PROGRESS (%d+)/(%d+)")
      if a then job.done, job.total = tonumber(a), tonumber(b) end
    elseif line:sub(1, 4) == "DONE" then job.ok, job.why, job.finished = true, (line:gsub("^DONE ?", "")), true
    elseif line:sub(1, 5) == "FAIL " then job.ok, job.why, job.finished = false, line:sub(6), true
    end
  end
  if not job.finished then
    job.pid = job.pid or (slurp(job.pidf):match("^(%d+)"))
    if job.pid and job.stopped and not job.killed then M.stop(job) end   -- B came before the engine had told its pid
    if job.pid then
      if not alive(job.pid) then                       -- gone without DONE or FAIL: killed or crashed (read the file once more first)
        local late = slurp(job.out)
        if late:find("\nDONE") or late:find("^DONE") or late:find("\nFAIL ") then return M.poll(job) end
        job.finished, job.ok = true, false
        job.why = job.stopped and "Stopped. What was done stays done; the next launch offers to finish." or "r3i6nos-appdata stopped before it finished"
      end
    elseif os.time() - job.startedAt > 5 then
      job.finished, job.ok, job.why = true, false, "r3i6nos-appdata could not run"
    end
  end
  if job.finished then os.remove(job.out); os.remove(job.pidf) end
  return job
end

--- Ask the engine to stop between files (SIGTERM). What is done stays done; the next launch offers to finish.
function M.stop(job)
  job.stopped = true
  job.pid = job.pid or (slurp(job.pidf):match("^(%d+)"))
  if job.pid and not job.finished and not job.killed then
    job.killed = true
    os.execute("kill -TERM " .. job.pid .. " 2>/dev/null")
  end
end

-- ---- the screens --------------------------------------------------------------------------------------------------------------------

-- The defaults of opts, for LÖVE: the next press (a theme action: "up", "accept", "back", ...), drawing, and napping.
local function loveNext(opts)
  return function()
    love.event.pump()
    for name, a, b in love.event.poll() do
      if name == "quit" then return "back" end
      if name == "keypressed" or name == "joystickpressed" then
        if opts.toast and require("r3i6nos_toast").key(opts.toast) then return nil end   -- the key that clears a failure toast is used up
        local act = name == "keypressed" and T.KEYS[a] or T.PAD[b]
        if act then return act end
      end
    end
  end
end

local function lovePresent(scr)
  T.draw(scr)
  love.graphics.present()
end

local function loveSleep(sec) love.timer.sleep(sec) end

local function ui(opts)
  opts = opts or {}
  local u = { toast = opts.toast, seen = opts.seen }
  u.next = opts.next or loveNext(opts)
  u.present = opts.present or lovePresent
  u.sleep = opts.sleep or loveSleep
  u.now = opts.now or function() return love.timer.getTime() end
  return u
end

local function show(u, scr)
  if u.toast then
    local toast = require("r3i6nos_toast")
    toast.poll(u.toast, u.now())
    toast.draw(u.toast, scr)
  end
  if u.seen then u.seen(scr) end      -- what the person sees, the toast over the top bar included
  u.present(scr)
end

--- A question: lines of text over a list of choices. Returns the chosen index (1-based); `sel` is the default; B answers nil.
local function ask(u, app, title, lines, options, sel)
  local n = #options
  while true do
    local scr = T.screen()
    T.topBar(scr, app, "data")
    local h = #lines + n + 6
    scr:box(3, 3, 58, h, { title = title })
    for i, l in ipairs(lines) do scr:text(5, 4 + i, T.fit(l, 54), T.STYLE.body.fg, T.STYLE.body.bg) end
    local items = {}
    for i, o in ipairs(options) do items[i] = { label = o } end
    T.listDialog(scr, { col = 5, row = 6 + #lines, w = 54, h = n + 2, title = "Choose", items = items, sel = sel, foot = "A choose   B not now" })
    show(u, scr)
    local act = u.next(0.05)
    if act == "accept" then return sel end
    if act == "back" then return nil end
    if act == "up" or act == "down" or act == "pageup" or act == "pagedown" or act == "home" or act == "end" then
      sel = T.listKey(sel, n, n, act)
    elseif not act then u.sleep(0.02) end                     -- nap: no busy loop for a still screen (docs/POWER.md)
  end
end

local progress = T.progressBar          -- the theme's one bar (rule 18)

--- Run the engine's move or merge behind a progress bar. B asks it to stop. Returns true when it finished, and the reason or counts.
local function work(u, app, verb, arg)
  local job = M.start(verb, app, arg)
  local t0 = u.now()
  while true do
    M.poll(job)
    local scr = T.screen()
    T.topBar(scr, app, "data")
    scr:box(6, 6, 52, 11, { title = ("%s %s's data"):format(verb == "move" and "Moving" or "Merging", app), foot = "B stops; what is done stays done" })
    scr:text(8, 8, T.fit(job.step, 48), T.STYLE.body.fg, T.STYLE.body.bg)
    progress(scr, 8, 10, 48, job.total > 0 and math.floor(job.done * 100 / job.total) or 0)
    local secs = math.floor(u.now() - t0)
    local clock = ("%d:%02d"):format(math.floor(secs / 60), secs % 60)
    scr:text(8, 12, T.fit(("%d of %d files"):format(job.done, job.total), 30), T.STYLE.strong.fg, T.STYLE.strong.bg)
    scr:textRight(56, 12, clock, T.STYLE.hint.fg, T.STYLE.hint.bg)
    if job.stopped and not job.finished then scr:text(8, 14, "Stopping after this file...", T.STYLE.hint.fg, T.STYLE.hint.bg) end
    show(u, scr)
    if job.finished then break end
    if u.next(0) == "back" and not job.stopped then M.stop(job) end
    u.sleep(0.05)
  end
  return job.ok, job.why
end

-- A message and one button, wrapped at 54 cells at a space when there is one.
local function tell(u, app, title, text)
  local lines = {}
  local at = 1
  while at <= #text and #lines < 3 do
    local take = math.min(54, #text - at + 1)
    if at + take - 1 < #text then
      while take > 20 and text:sub(at + take, at + take) ~= " " do take = take - 1 end
    end
    lines[#lines + 1] = text:sub(at, at + take - 1)
    at = at + take + (text:sub(at + take, at + take) == " " and 1 or 0)
  end
  ask(u, app, title, lines, { "OK" }, 1)
end

-- ---- the launch check -----------------------------------------------------------------------------------------------------------------

--- On every launch, after the splash and before heavy work. Returns { use, dataCard, notice, quit, engine }; see the top of this file.
--- opts: toast (a r3i6nos_toast), and, for tests, next(wait) -> action|nil, present(screen), sleep(seconds), now() -> seconds, seen(screen).
function M.init(app, opts)
  local d = newData()
  if preset(d) then return d end
  local st = M.state(app)
  if not st or st.use == "" then return legacy(d, app) end
  d.engine = true
  local u = ui(opts)
  local sb, db = human(st.s_bytes), human(st.d_bytes)
  local title = app .. "'s data"

  if st.action == M.NAMES.offerMove then
    local pick = ask(u, app, title, {
      ("Move %s's data to the data card?"):format(app), ("%s in %d files."):format(sb, st.s_files), "The system card can then stay out." },
      { "Move all", "Keep on the system card", "Not now" }, 3)
    if pick == 1 then
      local good, why = work(u, app, "move")
      if not good then tell(u, app, "It did not finish", why) end
    elseif pick == 2 then
      M.capture("choice", app, "keep")
    end
  elseif st.action == M.NAMES.offerMerge then
    local third = st.conflicts > 0 and ("%d files are on both and differ (the newer is kept)."):format(st.conflicts) or "Nothing differs; the copies are joined."
    local pick = ask(u, app, title, {
      ("%s has data on both cards:"):format(app), ("%s on the system card, %s on the data card."):format(sb, db), third },
      { "Merge into the data card (recommended)", "Merge into the system card", "Not now", "Don't ask again" }, 3)
    if pick == 1 or pick == 2 then
      local good, why = work(u, app, "merge", pick == 1 and "data" or "system")
      if not good then tell(u, app, "It did not finish", why) end
    elseif pick == 4 then
      M.capture("choice", app, "keep")
    end
  elseif st.action == M.NAMES.resume then
    local pick = ask(u, app, title, { ("Finish moving %s's data?"):format(app), "It was stopped part way; nothing is lost." },
      { "Finish now", "Not now" }, 2)
    if pick == 1 then
      local good, why = work(u, app, "merge", "data")
      if not good then tell(u, app, "It did not finish", why) end
    end
  elseif st.action == M.NAMES.needsCard then
    local pick = ask(u, app, title, { "Needs the data card.", ("%s's data is on it, and it is not in."):format(app) },
      { "Use the system card (new, separate data)", "Quit" }, 2)
    if pick == 1 then
      d.use = st.use
      mkdirs(d.use)
      M.capture("used", app, "system")
    else
      d.quit = true
      return d
    end
  end

  -- what is where now: the engine's answer after whatever was chosen
  local after = M.state(app)
  if not after or after.use == "" then after = st end
  if after.card ~= "valid" and after.card ~= "none" and after.reason ~= "" then d.notice = after.reason .. " Using the system card." end
  return settle(d, app, after.use, after.d_at)
end

return M
