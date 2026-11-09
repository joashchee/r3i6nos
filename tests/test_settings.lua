-- Run with: luajit tests/test_settings.lua   (from the repo root)
-- Settings: the list, Shutdown's confirmation, and the Network and SSH screens, driven with a fake helper client.
package.path = "menu/?.lua;" .. package.path
local settings = require("settings")

local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end

-- A fake client: records requests, answers from a table keyed by the request id.
local function fake()
  local io_ = { sent = {}, answers = {}, n = 0 }
  function io_.request(verb, args)
    io_.n = io_.n + 1
    local id = "id" .. io_.n
    io_.sent[#io_.sent + 1] = { verb = verb, args = args, id = id }
    return id
  end
  function io_.poll(id) return io_.answers[id] end
  function io_.answer(res) io_.answers[io_.sent[#io_.sent].id] = res end
  function io_.last() return io_.sent[#io_.sent] end
  return io_
end

local function new(io_)
  local s = settings.new(io_ or fake())
  assert(not settings.isOpen(s))
  settings.open(s)
  return s
end

local function kind(s) local n = settings.node(s); return n and n.kind end
local function title(s) return settings.node(s).title end
--- Move to the menu item with this label and accept it.
local function choose(s, label)
  local n = settings.node(s)
  assert(n.kind == "menu", "not on a menu: " .. tostring(n.kind))
  for i, it in ipairs(n.items) do if it.label == label then n.sel = i; return settings.press(s, "accept") end end
  error("no item " .. label)
end

-- ---- the list and Shutdown (as before)
local s = new()
eq(title(s), "Settings"); eq(settings.node(s).items[1].label, "Network"); eq(settings.node(s).items[2].label, "SSH")
eq(settings.node(s).items[3].label, "Battery"); eq(settings.node(s).items[4].label, "Device reports")
eq(settings.node(s).items[5].label, "About"); eq(settings.node(s).items[6].label, "Shutdown")
choose(s, "Shutdown")
eq(kind(s), "confirm"); eq(settings.node(s).sel, 2, "the confirmation starts on No")
assert(settings.press(s, "accept") == nil and kind(s) == "menu", "accepting the default (No) must not power off")
choose(s, "Shutdown"); settings.press(s, "up")
eq(settings.node(s).sel, 1)
eq(settings.press(s, "accept"), "poweroff"); assert(not settings.isOpen(s))

s = new(); choose(s, "Shutdown")
assert(settings.press(s, "back") == nil and kind(s) == "menu"); settings.press(s, "back")
assert(not settings.isOpen(s), "B backs out one level at a time")

s = new(); choose(s, "Back"); assert(not settings.isOpen(s))
s = new(); settings.press(s, "up"); eq(settings.node(s).items[settings.node(s).sel].label, "Back", "the list wraps")
s = settings.new(fake()); assert(settings.press(s, "accept") == nil and not settings.isOpen(s), "a closed dialog ignores input")
-- "poweroff" is only ever the answer to a confirmed shutdown, never to opening a screen
s = new(); assert(settings.press(s, "accept") == nil, "Network opens, it does not power off")

-- ---- Battery: Status, Use by app, History and Clear history, with a fake battery
do
  local calls, cleared = {}, false
  local fakebat = {
    read = function() calls[#calls + 1] = "read"; return { pct = 50, status = "Discharging" } end,
    history = function() calls[#calls + 1] = "history"; return { "h" } end,
    statusLines = function(r, h) return { "status " .. r.pct .. " " .. #h } end,
    appLines = function(h) return { "apps " .. #h } end,
    recentLines = function(h, n) return { "recent " .. #h .. " " .. n } end,
    clear = function() cleared = true; return true end,
  }
  local b = settings.new(fake(), fakebat); settings.open(b)
  choose(b, "Battery")
  eq(title(b), "Battery"); eq(kind(b), "menu")
  local labels = {}
  for i, it in ipairs(settings.node(b).items) do labels[i] = it.label end
  eq(table.concat(labels, ","), "Status,Use by app,History,Clear history,Back")
  choose(b, "Status"); eq(kind(b), "pages"); eq(settings.node(b).pages[1][1], "status 50 1")
  settings.press(b, "back"); eq(kind(b), "menu")
  choose(b, "Use by app"); eq(settings.node(b).pages[1][1], "apps 1"); settings.press(b, "back")
  choose(b, "History"); eq(settings.node(b).pages[1][1], "recent 1 60"); settings.press(b, "back")
  choose(b, "Clear history")
  eq(kind(b), "confirm"); eq(settings.node(b).sel, 2, "the confirmation starts on No")
  assert(settings.press(b, "accept") == nil and kind(b) == "menu" and not cleared, "accepting the default (No) must not clear anything")
  choose(b, "Clear history"); settings.press(b, "up")
  assert(settings.press(b, "accept") == nil, "clearing is not a shutdown")
  assert(cleared, "Yes clears"); eq(kind(b), "pages"); eq(settings.node(b).pages[1][1], "The battery history is cleared.")
  settings.press(b, "back"); eq(kind(b), "menu"); choose(b, "Back"); eq(title(b), "Settings")
  -- a failed clear says so
  fakebat.clear = function() return false end
  choose(b, "Battery"); choose(b, "Clear history"); settings.press(b, "up"); settings.press(b, "accept")
  assert(settings.node(b).bad, "a failed clear is shown as a failure")
end

-- ---- Network > Status: a request, a wait, an answer shown, B back
local io_ = fake()
s = new(io_)
choose(s, "Network"); eq(title(s), "Network")
choose(s, "Status")
eq(kind(s), "busy"); eq(io_.last().verb, "status")
settings.tick(s, 0.5); eq(kind(s), "busy", "no answer yet")
io_.answer({ ok = true, lines = { "eth0  wired  up", "  address  192.168.1.20/24" }, nets = {} })
settings.tick(s, 0.5)
eq(kind(s), "pages"); eq(settings.node(s).pages[1][1], "eth0  wired  up")
settings.press(s, "back"); eq(title(s), "Network"); settings.press(s, "back"); eq(title(s), "Settings")

-- giving up: the wait ends with a reason, not a frozen screen
io_ = fake(); s = new(io_); choose(s, "Network"); choose(s, "Test connection")
eq(io_.last().verb, "test")
settings.tick(s, 59); eq(kind(s), "busy"); settings.tick(s, 2)
eq(kind(s), "pages"); assert(settings.node(s).pages[1][1]:find("No answer", 1, true)); assert(settings.node(s).bad)

-- B stops waiting; a late answer is ignored
io_ = fake(); s = new(io_); choose(s, "Network"); choose(s, "Wired setup")
settings.press(s, "back"); eq(title(s), "Network")
io_.answer({ ok = true, lines = { "late" }, nets = {} }); settings.tick(s, 1); eq(title(s), "Network")

-- no client, or a client that cannot write
s = settings.new(nil); settings.open(s); choose(s, "Network"); choose(s, "Status")
eq(kind(s), "pages"); assert(settings.node(s).pages[1][1]:find("not available", 1, true))
local broken = { request = function() return nil, "cannot write" end, poll = function() end }
s = new(broken); choose(s, "Network"); choose(s, "Status"); eq(kind(s), "pages")
assert(settings.node(s).pages[1][2]:find("cannot write", 1, true))

-- each plain action sends its own verb
for label, verb in pairs({ ["Wired setup"] = "wired", ["Wi-Fi: join saved"] = "wifi", ["Forget saved Wi-Fi"] = "forget",
                           ["Test connection"] = "test", ["Disconnect"] = "disconnect" }) do
  io_ = fake(); s = new(io_); choose(s, "Network"); choose(s, label)
  eq(io_.last().verb, verb, label)
end

-- ---- Wi-Fi: scan, pick, password, join
local nets = {
  { ssid = "Home", signal = "-40 dBm", security = "WPA" },
  { ssid = "Guest", signal = "-70 dBm", security = "open" },
  { ssid = "Old", signal = "-75 dBm", security = "WEP" },
}
io_ = fake(); s = new(io_); choose(s, "Network"); choose(s, "Wi-Fi setup")
eq(io_.last().verb, "scan")
io_.answer({ ok = true, lines = { "3 networks" }, nets = nets }); settings.tick(s, 1)
eq(kind(s), "menu"); eq(#settings.node(s).items, 4, "three networks and Other network")
eq(settings.node(s).items[1].label, "Home"); assert(settings.node(s).items[1].value:find("WPA", 1, true))

choose(s, "Home"); eq(kind(s), "osk")
settings.text(s, "short"); settings.press(s, "more")
eq(kind(s), "osk", "a too-short password is not accepted"); assert(settings.node(s).osk.error:find("At least 8", 1, true))
settings.text(s, "ish pass"); settings.press(s, "more")
eq(kind(s), "busy"); eq(io_.last().verb, "wifi"); eq(io_.last().args.ssid, "Home"); eq(io_.last().args.psk, "shortish pass")
io_.answer({ ok = true, lines = { "Connected: Home" }, nets = {} }); settings.tick(s, 1)
eq(kind(s), "pages"); assert(settings.node(s).pages[1][1]:find("Connected", 1, true))

-- B in the keyboard cancels, back to the list
io_ = fake(); s = new(io_); choose(s, "Network"); choose(s, "Wi-Fi setup")
io_.answer({ ok = true, lines = {}, nets = nets }); settings.tick(s, 1)
choose(s, "Home"); settings.press(s, "back"); eq(kind(s), "menu"); eq(#io_.sent, 1, "nothing was sent")
-- open network: no password asked
choose(s, "Guest"); eq(kind(s), "busy"); eq(io_.last().args.ssid, "Guest"); eq(io_.last().args.psk, "")
-- WEP: refused with a reason, nothing sent
io_ = fake(); s = new(io_); choose(s, "Network"); choose(s, "Wi-Fi setup")
io_.answer({ ok = true, lines = {}, nets = nets }); settings.tick(s, 1)
choose(s, "Old"); eq(kind(s), "pages"); eq(#io_.sent, 1); assert(settings.node(s).pages[1][1]:find("WEP", 1, true))
-- a hidden network: name, then password (empty is allowed)
io_ = fake(); s = new(io_); choose(s, "Network"); choose(s, "Wi-Fi setup")
io_.answer({ ok = true, lines = {}, nets = nets }); settings.tick(s, 1)
choose(s, "Other network..."); eq(kind(s), "osk")
settings.press(s, "more"); eq(kind(s), "osk", "an empty name is not accepted")
settings.text(s, "Hidden"); settings.press(s, "more"); eq(kind(s), "osk")
settings.press(s, "more"); eq(kind(s), "busy"); eq(io_.last().args.ssid, "Hidden"); eq(io_.last().args.psk, "")
-- a scan that failed shows why and offers nothing to pick
io_ = fake(); s = new(io_); choose(s, "Network"); choose(s, "Wi-Fi setup")
io_.answer({ ok = false, lines = { "No Wi-Fi adapter found." }, nets = {} }); settings.tick(s, 1)
eq(kind(s), "pages"); assert(settings.node(s).bad)
-- a long list pages with L1/R1
local many = {}
for i = 1, 40 do many[i] = { ssid = "net" .. i, signal = "-50 dBm", security = "WPA" } end
io_ = fake(); s = new(io_); choose(s, "Network"); choose(s, "Wi-Fi setup")
io_.answer({ ok = true, lines = {}, nets = many }); settings.tick(s, 1)
settings.press(s, "pagedown"); eq(settings.node(s).sel, 19); settings.press(s, "end"); eq(settings.node(s).sel, 41)

-- ---- Instructions and long answers page
s = new(); choose(s, "Network"); choose(s, "Instructions")
eq(kind(s), "pages"); local pgs = #settings.node(s).pages; assert(pgs >= 3)
settings.press(s, "right"); eq(settings.node(s).page, 2); settings.press(s, "end"); eq(settings.node(s).page, pgs)
settings.press(s, "right"); eq(settings.node(s).page, pgs, "no page past the last")
settings.press(s, "home"); eq(settings.node(s).page, 1); settings.press(s, "left"); eq(settings.node(s).page, 1)
local lines = {}
for i = 1, 40 do lines[i] = "line " .. i end
io_ = fake(); s = new(io_); choose(s, "Network"); choose(s, "Status")
io_.answer({ ok = true, lines = lines, nets = {} }); settings.tick(s, 1)
eq(#settings.node(s).pages, 3); eq(#settings.node(s).pages[1], 18); eq(settings.node(s).pages[3][4], "line 40")

-- ---- SSH
io_ = fake(); s = new(io_); choose(s, "SSH"); eq(title(s), "SSH")
choose(s, "Status"); eq(io_.last().verb, "ssh-status"); settings.press(s, "back")
choose(s, "Start SSH"); eq(io_.last().verb, "ssh-enable"); settings.press(s, "back")
choose(s, "Stop SSH"); eq(io_.last().verb, "ssh-disable"); settings.press(s, "back")
choose(s, "Instructions"); eq(kind(s), "pages"); settings.press(s, "back")

-- the password is typed twice and must match
choose(s, "Set SSH password"); eq(kind(s), "osk")
settings.text(s, "open sesame"); settings.press(s, "more")
eq(kind(s), "osk"); eq(settings.node(s).title, "Type it again")
settings.text(s, "open sesamX"); local before = #io_.sent; settings.press(s, "more")
eq(kind(s), "pages", "a mismatch is explained"); eq(#io_.sent, before, "and nothing is sent")
settings.press(s, "back")
choose(s, "Set SSH password"); settings.text(s, "open sesame"); settings.press(s, "more")
settings.text(s, "open sesame"); settings.press(s, "more")
eq(kind(s), "busy"); eq(io_.last().verb, "ssh-password"); eq(io_.last().args.password, "open sesame")
-- B in the second entry cancels the whole change
io_ = fake(); s = new(io_); choose(s, "SSH"); choose(s, "Set SSH password")
settings.text(s, "open sesame"); settings.press(s, "more"); settings.press(s, "back")
eq(title(s), "SSH"); eq(#io_.sent, 0)

-- a real keyboard types text only on the keyboard screen
s = new(); settings.text(s, "abc"); eq(title(s), "Settings")

print("settings: ok")

-- ---- Device reports: the installed tools' reports, started in the background (menu/reports.lua has its own test)
do
  local started = {}
  local fakerep = {
    found = { { cmd = "compose-report", tool = "compose", app = "Composer" } },
    ABOUT = { "about" }, NONE = { "none" },
  }
  function fakerep.list() return fakerep.found end
  function fakerep.start(r) started[#started + 1] = r.cmd; return fakerep.ok ~= false, "busy" end
  function fakerep.startedLines(r) return { r.app .. "'s report is running." } end
  local d = settings.new(fake(), nil, fakerep); settings.open(d)
  choose(d, "Device reports")
  eq(title(d), "Device reports"); eq(settings.node(d).items[1].label, "Composer"); eq(settings.node(d).items[1].value, "compose-report")
  choose(d, "Composer")
  eq(started[1], "compose-report", "A starts the report"); eq(kind(d), "pages"); eq(settings.node(d).pages[1][1], "Composer's report is running.")
  settings.press(d, "back"); eq(title(d), "Device reports", "B back to the list")
  fakerep.ok = false; choose(d, "Composer"); assert(settings.node(d).bad, "a report that cannot start says so")
  settings.press(d, "back"); choose(d, "What these are"); eq(settings.node(d).pages[1][1], "about")
  settings.press(d, "back"); choose(d, "Back"); eq(title(d), "Settings")
  fakerep.found = {}
  choose(d, "Device reports"); eq(settings.node(d).items[1].label, "None installed")
  choose(d, "None installed"); eq(settings.node(d).pages[1][1], "none")
  print("settings device reports: ok")
end

-- ---- About: This system, Check for updates (a busy wait on its own poll, not the network helper), Licences
do
  local started, answer = 0, nil
  local fakeab = {
    WAIT = 5, CHECK_LOST = { "lost" }, LICENCES = { { "MIT" }, { "FONT" } },
    info = function() return { "THIS BUILD", "Version      r3i6nos 0.1.0" } end,
    startCheck = function() started = started + 1; return true end,
    poll = function() return answer end,
  }
  local io_ = fake()
  local a = settings.new(io_, nil, nil, fakeab); settings.open(a)
  choose(a, "About")
  eq(title(a), "About")
  local labels = {}
  for i, it in ipairs(settings.node(a).items) do labels[i] = it.label end
  eq(table.concat(labels, ","), "This system,Check for updates,Licences,Back")
  choose(a, "This system"); eq(kind(a), "pages"); eq(settings.node(a).pages[1][2], "Version      r3i6nos 0.1.0")
  settings.press(a, "back")
  eq(started, 0, "nothing reaches the network until Check for updates is chosen (rule 6)")
  choose(a, "Check for updates"); eq(started, 1); eq(kind(a), "busy"); eq(settings.node(a).waiting, "Asking GitHub...")
  settings.tick(a, 1); eq(kind(a), "busy", "no answer yet: still waiting")
  eq(#io_.sent, 0, "the update check never goes through the root network helper")
  answer = { ok = true, lines = { "This is the latest release." } }
  settings.tick(a, 1); eq(kind(a), "pages"); eq(settings.node(a).pages[1][1], "This is the latest release."); assert(not settings.node(a).bad)
  settings.press(a, "back"); eq(title(a), "About")
  answer = nil
  choose(a, "Check for updates"); settings.tick(a, 6)
  eq(kind(a), "pages"); eq(settings.node(a).pages[1][1], "lost"); assert(settings.node(a).bad, "a lost check is shown as a failure")
  settings.press(a, "back")
  answer = { ok = false, lines = { "Could not reach GitHub:" } }
  choose(a, "Check for updates"); settings.tick(a, 0.1); assert(settings.node(a).bad)
  settings.press(a, "back")
  choose(a, "Check for updates"); answer = nil; settings.press(a, "back"); eq(title(a), "About", "B stops waiting")
  fakeab.startCheck = function() return false, "no helper" end
  choose(a, "Check for updates"); eq(kind(a), "pages"); assert(settings.node(a).bad)
  settings.press(a, "back")
  choose(a, "Licences"); eq(#settings.node(a).pages, 2)
  settings.press(a, "back"); choose(a, "Back"); eq(title(a), "Settings")
end
print("settings about: ok")
