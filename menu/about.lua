-- Settings > About: this build and this unit (r3i6nos-about info), whether a newer build is out (r3i6nos-about check, the
-- only network use, started only when a person picks Check for updates: CLAUDE.md rule 6), and the licences.
-- Pure but for M.sys, so luajit tests it. main.lua sets M.extra to the lines only LÖVE knows (its version, the pads).

local M = {}

M.HELPER = os.getenv("R3I6NOS_ABOUT") or "/usr/local/bin/r3i6nos-about"
M.OUT = os.getenv("R3I6NOS_ABOUT_OUT") or "/run/loveos/about-check.txt"
M.WAIT = 45 -- seconds: two questions to GitHub, 15 s each at most, and the start of Python

local function quote(s) return "'" .. tostring(s):gsub("'", "'\\''") .. "'" end

--- What touches the system; tests replace it.
M.sys = {
  lines = function(cmd)
    local p = io.popen(cmd)
    if not p then return nil end
    local out = {}
    for line in p:lines() do out[#out + 1] = line end
    local ok = p:close()
    if ok == nil or ok == false then return nil end
    return out
  end,
  read = function(path)
    local f = io.open(path, "r")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
  end,
  remove = function(path) os.remove(path) end,
  exec = function(cmd) return os.execute(cmd) end,
}

--- Extra lines from LÖVE (set by main.lua); nil in tests.
M.extra = nil

--- The system's details, for pages.
function M.info(sys)
  sys = sys or M.sys
  local lines = sys.lines(quote(M.HELPER) .. " info 2>/dev/null")
  if not lines or #lines == 0 then lines = { "Could not read the system's details." } end
  if M.extra then
    local ok, more = pcall(M.extra)
    if ok and type(more) == "table" and #more > 0 then
      lines[#lines + 1] = ""
      for _, l in ipairs(more) do lines[#lines + 1] = l end
    end
  end
  return lines
end

--- Starts the update check in the background. true, or false and why.
function M.startCheck(sys)
  sys = sys or M.sys
  sys.remove(M.OUT)
  local ok = sys.exec(quote(M.HELPER) .. " check " .. quote(M.OUT) .. " >/dev/null 2>&1 &")
  if ok == true or ok == 0 then return true end
  return false, "could not start the check"
end

--- The check's answer once it is written: { ok = bool, lines = {...} }, else nil.
function M.poll(sys)
  sys = sys or M.sys
  local s = sys.read(M.OUT)
  if not s then return nil end
  local first, rest = s:match("^ok=([01])\n?(.*)$")
  if not first then return nil end
  local lines = {}
  for l in rest:gmatch("([^\n]*)\n") do lines[#lines + 1] = l end
  return { ok = first == "1", lines = lines }
end

M.CHECK_LOST = {
  "No answer from the update check.",
  "",
  "GitHub may be slow, or the network is down.",
  "Try again, or look at Settings > Network > Status.",
}

M.LICENCES = {
  {
    "r3i6nos is free software under the MIT licence.",
    "",
    "It has no accounts and no telemetry. Nothing connects to",
    "the network unless you choose it in Settings, and Check",
    "for updates asks GitHub only about the newest version.",
    "",
    "Your games card is yours: r3i6nos only reads it, and",
    "never formats or deletes anything on it.",
    "",
    "The graphics driver (Mali), kernel modules and boot files",
    "come from your own card when it is flashed, and are not",
    "part of r3i6nos.",
  },
  {
    "FONT",
    "",
    "WebPlus IBM VGA 8x16, from The Ultimate Oldschool PC",
    "Font Pack v2.2 by VileR, (c) 2016-2020 VileR,",
    "int10h.org/oldschool-pc-fonts",
    "Licence: CC BY-SA 4.0,",
    "creativecommons.org/licenses/by-sa/4.0",
    "Changed: drawn as 8x16 bitmaps, shown at 10x20.",
    "",
    "OTHER PARTS",
    "",
    "LÖVE (love2d.org) is under the zlib licence. Debian and",
    "the other runtimes keep their own licences; their texts",
    "are in /usr/share/doc on the system card.",
  },
}

return M
