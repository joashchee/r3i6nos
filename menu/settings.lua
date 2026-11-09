-- The Settings dialog's logic. Pure Lua, no LÖVE calls, so plain luajit can test it.
-- A stack of screens ("nodes"): a menu, pages of text, the on-screen keyboard, a wait for the root network helper, or a confirmation.
-- B pops one; the bottom is the Settings list. Nothing scrolls: more than fits is whole pages.
-- Network and SSH never start by themselves: every request is a person choosing it here (CLAUDE.md rule 6).

local osk = require("osk")
local pages = require("netpages")
local battery = require("battery")
local reports = require("reports")
local about = require("about")

local settings = {}

--- Seconds to wait for the helper before giving up, by verb (a scan or a Wi-Fi join is slow).
settings.TIMEOUT = { default = 30, scan = 45, wifi = 75, wired = 60, test = 60, ["ssh-enable"] = 45 }

--- A closed dialog. io is the helper's client (menu/netclient.lua); bat the battery module (menu/battery.lua); rep the device reports
--- (menu/reports.lua); ab Settings > About (menu/about.lua). Tests pass fakes.
function settings.new(io, bat, rep, ab)
  return { stack = {}, io = io, bat = bat or battery, rep = rep or reports, ab = ab or about }
end

local function top(s) return s.stack[#s.stack] end
local function push(s, node) s.stack[#s.stack + 1] = node; return node end
local function pop(s) s.stack[#s.stack] = nil end
local function replace(s, node) s.stack[#s.stack] = node; return node end

function settings.isOpen(s) return #s.stack > 0 end

--- The node to draw, or nil when closed.
function settings.node(s) return top(s) end

-- ---- node makers -------------------------------------------------------------------------------------------------------------

local function menu(title, items, w) return { kind = "menu", title = title, items = items, sel = 1, w = w } end

local function show(s, title, lines, ok)
  return push(s, { kind = "pages", title = title, pages = pages.paginate(lines), page = 1, bad = (ok == false) })
end

local function showPages(s, title, pgs) return push(s, { kind = "pages", title = title, pages = pgs, page = 1 }) end

local function confirm(s, title, yes, hint, on_yes)
  push(s, { kind = "confirm", title = title, hint = hint, yes = yes, sel = 2, on_yes = on_yes }) -- starts on "No"
end

--- Type something; on_done(s, text) gets the text when it is valid.
local function type_(s, o, on_done)
  push(s, { kind = "osk", title = o.title, osk = osk.new(o), on_done = on_done })
end

--- Ask the helper for a verb and wait. on_result(s, res) builds what comes next (it runs with the wait already removed).
local function ask(s, title, verb, args, on_result)
  if not s.io then return show(s, title, { "The network helper is not available here." }, false) end
  local id, err = s.io.request(verb, args or {})
  if not id then return show(s, title, { "Could not ask for it:", tostring(err) }, false) end
  push(s, {
    kind = "busy", title = title, id = id, verb = verb, elapsed = 0,
    timeout = settings.TIMEOUT[verb] or settings.TIMEOUT.default, on_result = on_result,
  })
end

-- Most answers are just shown.
local function showResult(title)
  return function(s, res) show(s, title, res.lines, res.ok) end
end

-- ---- the screens -------------------------------------------------------------------------------------------------------------

local function joinWith(s, ssid, secured)
  local function go(sx, psk)
    ask(sx, "Joining " .. ssid, "wifi", { ssid = ssid, psk = psk }, showResult("Wi-Fi"))
  end
  if not secured then return go(s, "") end
  type_(s, { title = "Password for " .. ssid, prompt = "Wi-Fi password (8-63 characters)", min = 8, max = 63 }, function(sx, text)
    go(sx, text)
  end)
end

local function wifiList(s, res)
  local items = {}
  for _, n in ipairs(res.nets) do
    items[#items + 1] = {
      label = n.ssid, value = n.signal .. " " .. n.security, hint = (n.security == "open") and "Open network: no password"
        or "Needs a password",
      run = function(sx)
        if n.security == "WEP" then return show(sx, "Wi-Fi", { "WEP networks are not supported.", "", "WEP is broken; use WPA on the router." }, false) end
        joinWith(sx, n.ssid, n.security ~= "open")
      end,
    }
  end
  items[#items + 1] = {
    label = "Other network...", hint = "Type a network name that is not listed",
    run = function(sx)
      type_(sx, { title = "Network name", prompt = "Wi-Fi name (1-32 characters)", min = 1, max = 32 }, function(sy, ssid)
        type_(sy, { title = "Password for " .. ssid, prompt = "Leave empty for an open network", min = 0, max = 63 }, function(sz, psk)
          ask(sz, "Joining " .. ssid, "wifi", { ssid = ssid, psk = psk }, showResult("Wi-Fi"))
        end)
      end)
    end,
  }
  push(s, menu(#res.nets .. " networks", items, 56))
end

local pages_net, pages_ssh = pages.NETWORK, pages.SSH

local function networkMenu(s)
  local function asker(title, verb, args, ok)
    return function(sx) ask(sx, title, verb, args, ok or showResult(title)) end
  end
  push(s, menu("Network", {
    { label = "Status", hint = "Adapters, addresses, the connected network", run = asker("Network status", "status") },
    { label = "Wired setup", hint = "Cable and a USB Ethernet adapter: get an address", run = asker("Wired setup", "wired") },
    { label = "Wi-Fi setup", hint = "Scan, pick a network, type its password", run = function(sx)
        ask(sx, "Scanning for Wi-Fi", "scan", {}, function(sy, res)
          if not res.ok then return show(sy, "Wi-Fi", res.lines, false) end
          wifiList(sy, res)
        end)
      end },
    { label = "Wi-Fi: join saved", hint = "Join the network remembered from last time", run = asker("Wi-Fi", "wifi") },
    { label = "Forget saved Wi-Fi", hint = "Remove the remembered network and its password", run = asker("Forget", "forget") },
    { label = "Test connection", hint = "Link, address, gateway, DNS, internet", run = asker("Connection test", "test") },
    { label = "Disconnect", hint = "Turn the network off", run = asker("Disconnect", "disconnect") },
    { label = "Instructions", hint = "What to plug in and how to set it up", run = function(sx) showPages(sx, "Network help", pages_net) end },
    { label = "Back", hint = "Return to Settings", run = function(sx) pop(sx) end },
  }, 52))
end

local function sshMenu(s)
  local function asker(title, verb, args)
    return function(sx) ask(sx, title, verb, args, showResult(title)) end
  end
  push(s, menu("SSH", {
    { label = "Status", hint = "Running or not, how you can log in, the host key", run = asker("SSH status", "ssh-status") },
    { label = "Set SSH password", hint = "The password for user loveos (8 or more characters)", run = function(sx)
        type_(sx, { title = "SSH password", prompt = "Password for loveos (8-64 characters)", min = 8, max = 64 }, function(sy, first)
          type_(sy, { title = "Type it again", prompt = "Same password, to be sure", min = 8, max = 64 }, function(sz, second)
            if first ~= second then return show(sz, "SSH password", { "The two passwords were different.", "Nothing was changed." }, false) end
            ask(sz, "Setting password", "ssh-password", { password = first }, showResult("SSH password"))
          end)
        end)
      end },
    { label = "Start SSH", hint = "Needs a network and a password or key. Off after reboot", run = asker("Start SSH", "ssh-enable") },
    { label = "Stop SSH", hint = "Turn the SSH server off", run = asker("Stop SSH", "ssh-disable") },
    { label = "Instructions", hint = "How to log in from a computer", run = function(sx) showPages(sx, "SSH help", pages_ssh) end },
    { label = "Back", hint = "Return to Settings", run = function(sx) pop(sx) end },
  }, 56))
end

local function batteryMenu(s)
  local b = s.bat
  push(s, menu("Battery", {
    { label = "Status", hint = "Charge, voltage, health, average drain, time left", run = function(sx)
        show(sx, "Battery", b.statusLines(b.read(), b.history()))
      end },
    { label = "Use by app", hint = "How fast each app drains the battery", run = function(sx)
        show(sx, "Battery use by app", b.appLines(b.history()))
      end },
    { label = "History", hint = "The latest samples: start, apps, shutdown", run = function(sx)
        show(sx, "Battery history", b.recentLines(b.history(), 60))
      end },
    { label = "Clear history", hint = "Forget the recorded samples (nothing else)", run = function(sx)
        confirm(sx, "Clear the battery history?", "Yes, clear it", "Deletes only the recorded battery samples", function(sy)
          if b.clear() then show(sy, "Battery", { "The battery history is cleared.", "", "It starts again at the next app or shutdown." })
          else show(sy, "Battery", { "Could not clear the history." }, false) end
        end)
      end },
    { label = "Back", hint = "Return to Settings", run = function(sx) pop(sx) end },
  }, 48))
end

-- Wait for a running report: its step and a bar when it says them, the time so far, then its summary and where the file is.
-- B leaves the wait; the report goes on, and choosing it again comes back here.
local function waitReport(s, r)
  local rep = s.rep
  push(s, {
    kind = "busy", title = r.app .. " report", elapsed = 0, timeout = rep.WAIT, waiting = "Checking this unit...",
    foot = "B Back (it keeps running)",
    poll = function(n)
      local p = rep.poll(r)
      n.prog = p
      if p.done then return p end
    end,
    lost = { "The report has run for 15 minutes and is not done.", "", "It may still be working. Choose it again later to see." },
    on_result = function(sx, res)
      local lines, ok = rep.doneLines(r, res, rep.summary(res.file))
      show(sx, r.app .. " report", lines, ok)
    end,
  })
end

-- Device reports (docs/DEVICE-REPORTS.md): r3i6nos's own and each installed tool's <command>-report, started in the background.
local function reportsMenu(s)
  local rep = s.rep
  local found = rep.list()
  local running = rep.running and rep.running()
  local items = {}
  for _, r in ipairs(found) do
    local mine = running == r.cmd
    items[#items + 1] = { label = r.app, value = mine and "running" or r.cmd,
      hint = mine and "Running now: choose it to see how far it has got" or ("Check " .. r.app .. " on this unit, for Claude"), run = function(sx)
      if rep.running and rep.running() == r.cmd then return waitReport(sx, r) end
      local ok, err = rep.start(r)
      if ok then waitReport(sx, r)
      else show(sx, r.app .. " report", { "Could not start the report:", tostring(err) }, false) end
    end }
  end
  if #found == 0 then
    items[#items + 1] = { label = "None installed", hint = "No command-line tool with a report", run = function(sx) show(sx, "Device reports", rep.NONE) end }
  end
  items[#items + 1] = { label = "What these are", hint = "A file for Claude about this unit", run = function(sx) show(sx, "Device reports", rep.ABOUT) end }
  items[#items + 1] = { label = "Back", hint = "Return to Settings", run = function(sx) pop(sx) end }
  push(s, menu("Device reports", items, 48))
end

-- About: this build and unit, the update check (the only network use here, and only when chosen), the licences.
local function aboutMenu(s)
  local ab = s.ab
  push(s, menu("About", {
    { label = "This system", hint = "Version, build, board, memory, cards, apps, pads", run = function(sx)
        show(sx, "About this system", ab.info())
      end },
    { label = "Check for updates", hint = "Ask GitHub if a newer build is out (uses the network)", run = function(sx)
        local ok, err = ab.startCheck()
        if not ok then return show(sx, "Updates", { "Could not check:", tostring(err) }, false) end
        push(sx, {
          kind = "busy", title = "Checking for updates", elapsed = 0, timeout = ab.WAIT, waiting = "Asking GitHub...",
          poll = function() return ab.poll() end, lost = ab.CHECK_LOST,
          on_result = function(sy, res) show(sy, "Updates", res.lines, res.ok) end,
        })
      end },
    { label = "Licences", hint = "r3i6nos, the font and the other parts", run = function(sx) showPages(sx, "Licences", ab.LICENCES) end },
    { label = "Back", hint = "Return to Settings", run = function(sx) pop(sx) end },
  }, 48))
end

local function root(s)
  return menu("Settings", {
    { label = "Network", hint = "Wired and Wi-Fi: status, setup, test, instructions", run = networkMenu },
    { label = "SSH", hint = "Log in from a computer (off until you start it)", run = sshMenu },
    { label = "Battery", hint = "Charge, health and how much each app uses", run = batteryMenu },
    { label = "Device reports", hint = "Check this unit for an app's maker (a file for Claude)", run = reportsMenu },
    { label = "About", hint = "Version, updates, this unit and licences", run = aboutMenu },
    { label = "Shutdown", hint = "Power the device off", run = function(sx)
        confirm(sx, "Shut down the device?", "Yes, shut down", "Powers the device off", function() return "poweroff" end)
      end },
    { label = "Back", hint = "Return to the games", run = function(sx) sx.stack = {} end },
  }, 40)
end

function settings.open(s)
  s.stack = {}
  push(s, root(s))
end

-- ---- input -------------------------------------------------------------------------------------------------------------------

--- Feed one action: up down left right pageup pagedown home end accept back more x y select.
--- Returns "poweroff" when the user confirmed a shutdown, else nil.
function settings.press(s, action)
  local n = top(s)
  if not n then return nil end
  if n.kind == "menu" then
    local c = #n.items
    if action == "up" then n.sel = (n.sel - 2) % c + 1
    elseif action == "down" then n.sel = n.sel % c + 1
    elseif action == "home" then n.sel = 1
    elseif action == "end" then n.sel = c
    elseif action == "left" or action == "pageup" then n.sel = math.max(1, n.sel - pages.PER)
    elseif action == "right" or action == "pagedown" then n.sel = math.min(c, n.sel + pages.PER)
    elseif action == "back" then pop(s)
    elseif action == "accept" then
      n.items[n.sel].run(s) -- what it returns is not an answer; only a confirmed shutdown is
    end
  elseif n.kind == "pages" then
    local c = #n.pages
    if action == "back" or action == "accept" then pop(s)
    elseif action == "right" or action == "pagedown" or action == "down" then n.page = math.min(c, n.page + 1)
    elseif action == "left" or action == "pageup" or action == "up" then n.page = math.max(1, n.page - 1)
    elseif action == "home" then n.page = 1
    elseif action == "end" then n.page = c end
  elseif n.kind == "confirm" then
    if action == "up" or action == "down" then n.sel = 3 - n.sel
    elseif action == "back" then pop(s)
    elseif action == "accept" then
      pop(s)
      if n.sel == 1 then
        local r = n.on_yes(s)
        if r == "poweroff" then s.stack = {}; return "poweroff" end
      end
    end
  elseif n.kind == "osk" then
    local ev = osk.press(n.osk, action)
    if ev == "cancel" then pop(s)
    elseif ev == "done" then
      local text = n.osk.text
      pop(s)
      n.on_done(s, text)
    end
  elseif n.kind == "busy" then
    if action == "back" then pop(s) end -- stop waiting; the answer, if it comes, is ignored
  end
  return nil
end

--- Typed characters from a real keyboard, while the on-screen keyboard is up.
function settings.text(s, str)
  local n = top(s)
  if n and n.kind == "osk" then osk.type(n.osk, str) end
end

--- A real keyboard's backspace.
function settings.backspace(s)
  local n = top(s)
  if n and n.kind == "osk" then osk.press(n.osk, "x") end
end

--- Call every frame with the seconds since the last: collects the helper's answer, or gives up waiting.
function settings.tick(s, dt)
  local n = top(s)
  if not n or n.kind ~= "busy" then return end
  n.elapsed = n.elapsed + dt
  local res
  if n.poll then res = n.poll(n) else res = s.io and s.io.poll(n.id) end
  if res then
    pop(s)
    n.on_result(s, res)
  elseif n.elapsed >= n.timeout then
    pop(s)
    show(s, n.title, n.lost or {
      "No answer from the network helper.", "",
      "It may still be working, or it is not running.", "Go back and look at Status, or try again.",
    }, false)
  end
end

return settings
