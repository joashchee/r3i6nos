-- PortMaster's notice (docs/PORTMASTER.md, "The notice"). PortMaster is third-party and not supported on r3i6nos, so opening it from the menu first
-- shows a dialog: the terms (accepted once, remembered in a file on the system card), a short reminder on every later launch, and each time the
-- checklist of the game-engine runtimes r3i6nos can run (docs/RUNTIMES.md). Pure but for the two file calls, so luajit tests it.
local M = {}

M.TOOLS = os.getenv("R3I6NOS_TOOLS_DIR") or "/opt/loveos/bin"
M.ROMS = os.getenv("R3I6NOS_ROMS") or "/roms"
M.RUNTIMES = {
  { name = "Pyxel", cmd = "r3i6nos-run-pyxel" },
  { name = "Ren'Py", cmd = "r3i6nos-run-renpy" },
  { name = "Solarus", cmd = "r3i6nos-run-solarus" },
}

-- The terms, as one screen each. Lines are at most 52 cells (the box is 58 wide).
M.TERMS = {
  "PortMaster is NOT supported on r3i6nos.",
  "",
  "It is a third-party app. Do not ask for help with it",
  "on the PortMaster site or Discord: they do not",
  "support this firmware.",
  "",
  "r3i6nos stays slim on purpose and does not provide",
  "all that PortMaster asks for, so a number of ports",
  "will not run. Which ones is found by trying them.",
}
M.REMINDER = {
  "PortMaster is unofficial here and not supported.",
  "Do not ask the PortMaster site or Discord for help.",
  "A number of ports will not run on this slim system.",
}
M.OTHER = "Other PortMaster runtimes are not supported."

M.sys = {
  exists = function(path) local f = io.open(path, "r"); if f then f:close() return true end return false end,
  write = function(path, dir)
    os.execute("mkdir -p '" .. dir .. "' 2>/dev/null")
    local f = io.open(path, "w")
    if not f then return false end
    f:write("accepted\n")
    f:close()
    return true
  end,
}

function M.termsFile() return M.ROMS .. "/ports/.r3i6nos-portmaster-terms" end

function M.accepted() return M.sys.exists(M.termsFile()) end

--- Remember the person accepted. False when the card cannot be written (the dialog then asks again next time).
function M.accept() return M.sys.write(M.termsFile(), M.ROMS .. "/ports") end

--- The runtimes with whether each is installed (its command is on the tools path).
function M.runtimes()
  local out = {}
  for _, r in ipairs(M.RUNTIMES) do out[#out + 1] = { name = r.name, installed = M.sys.exists(M.TOOLS .. "/" .. r.cmd) } end
  return out
end

--- Is this menu entry PortMaster's launcher (the native app of that name)?
function M.is(entry) return entry and entry.type == "native" and entry.title == "PortMaster" or false end

return M
