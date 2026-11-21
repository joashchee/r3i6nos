-- PortMaster's notice (docs/PORTMASTER.md, "The notice"). PortMaster is third-party and not supported on r3i6nos, so opening it from the menu first
-- shows a dialog: the terms (accepted once, remembered in a file on the system card), a short reminder on every later launch, and each time the
-- checklist of the game-engine runtimes r3i6nos can run (docs/RUNTIMES.md). Pure but for the two file calls, so luajit tests it.
local M = {}

M.TOOLS = os.getenv("R3I6NOS_TOOLS_DIR") or "/opt/loveos/bin"
M.ROMS = os.getenv("R3I6NOS_ROMS") or "/roms"
-- PortMaster-New's runtimes.json as fetched 2026-11-19 and re-checked unchanged on 2026-11-21 (49 runtimes, each with an aarch64 build): name, the aarch64 file PortMaster downloads into
-- <ports>/PortMaster/libs, and cmd for the three r3i6nos can run (extracted packages, docs/RUNTIMES.md). Refresh it by hand when the list changes.
M.LIST = {
  { name = "PMJS v1", file = "pmjs_v1.aarch64.squashfs" },
  { name = "DotNet 3.1.32", file = "dotnet-3.1.32.aarch64.squashfs" },
  { name = "DotNet 8.0.12", file = "dotnet-8.0.12.aarch64.squashfs" },
  { name = "GMToolkit", file = "gmtoolkit.aarch64.squashfs" },
  { name = "Godot/FRT 2.1.6", file = "frt_2.1.6.aarch64.squashfs" },
  { name = "Godot/FRT 3.0.6_v1", file = "frt_3.0.6_v1.aarch64.squashfs" },
  { name = "Godot/FRT 3.1.2", file = "frt_3.1.2.aarch64.squashfs" },
  { name = "Godot/FRT 3.2.3", file = "frt_3.2.3.aarch64.squashfs" },
  { name = "Godot/FRT 3.3.4", file = "frt_3.3.4.aarch64.squashfs" },
  { name = "Godot/FRT 3.4.5", file = "frt_3.4.5.aarch64.squashfs" },
  { name = "Godot/FRT 3.5.2", file = "frt_3.5.2.aarch64.squashfs" },
  { name = "Godot/FRT 3.5.3", file = "frt_3.5.3.aarch64.squashfs" },
  { name = "Godot/FRT 3.6", file = "frt_3.6.aarch64.squashfs" },
  { name = "Godot/FRT 3.7 dev", file = "frt_3.7.aarch64.squashfs" },
  { name = "Godot/FRT 4.0.4", file = "frt_4.0.4.aarch64.squashfs" },
  { name = "Godot/FRT 4.1.3", file = "frt_4.1.3.aarch64.squashfs" },
  { name = "Godot 4.2.2", file = "godot_4.2.2.aarch64.squashfs" },
  { name = "Godot 4.2.2 Mono", file = "godot_4.2.2.mono.aarch64.squashfs" },
  { name = "Godot 4.3", file = "godot_4.3.aarch64.squashfs" },
  { name = "Godot 4.3 Mono", file = "godot_4.3.mono.aarch64.squashfs" },
  { name = "Godot 4.4", file = "godot_4.4.aarch64.squashfs" },
  { name = "Godot 4.4 Mono", file = "godot_4.4.mono.aarch64.squashfs" },
  { name = "Godot 4.4.1", file = "godot_4.4.1.aarch64.squashfs" },
  { name = "Godot 4.4.1 Mono", file = "godot_4.4.1.mono.aarch64.squashfs" },
  { name = "Godot 4.5", file = "godot_4.5.aarch64.squashfs" },
  { name = "Godot 4.5 Mono", file = "godot_4.5.mono.aarch64.squashfs" },
  { name = "Godot 4.6.3", file = "godot_4.6.3.aarch64.squashfs" },
  { name = "Godot 4.6.3 Mono", file = "godot_4.6.3.mono.aarch64.squashfs" },
  { name = "Godot 4.7.1", file = "godot_4.7.1.aarch64.squashfs" },
  { name = "Godot 4.7.1 Mono", file = "godot_4.7.1.mono.aarch64.squashfs" },
  { name = "Adventure Game Studio 3.6", file = "ags_3.6.aarch64.squashfs" },
  { name = "Mesapack 0.1", file = "mesa_pkg_0.1.aarch64.squashfs" },
  { name = "Mono 6.12.0.122", file = "mono-6.12.0.122-aarch64.squashfs" },
  { name = "Pyxel 2.2.8", file = "pyxel_2.2.8_python_3.11_aarch64.squashfs", cmd = "r3i6nos-run-pyxel" },
  { name = "Pyxel 2.3.18", file = "pyxel_2.3.18_python_3.11_aarch64.squashfs" },
  { name = "Pyxel 2.4.6", file = "pyxel_2.4.6_python_3.11_aarch64.squashfs" },
  { name = "Pyxel 2.9.5", file = "pyxel_2.9.5_python_3.11_aarch64.squashfs" },
  { name = "Python 3.11", file = "python_3.11.aarch64.squashfs" },
  { name = "Ren'Py 8.1.3", file = "renpy_8.1.3.aarch64.squashfs" },
  { name = "Ren'Py 8.3.4", file = "renpy_8.3.4.aarch64.squashfs", cmd = "r3i6nos-run-renpy" },
  { name = "Real Live VM", file = "rlvm.aarch64.squashfs" },
  { name = "Solarus 1.6.5", file = "solarus-1.6.5.aarch64.squashfs", cmd = "r3i6nos-run-solarus" },
  { name = "Westonpack 0.2", file = "weston_pkg_0.2.aarch64.squashfs" },
  { name = "Java jdk8.0.452", file = "zulu8.86.0.25-ca-jdk8.0.452-linux.aarch64.squashfs" },
  { name = "Java jdk11.0.11", file = "zulu11.48.21-ca-jdk11.0.11-linux.aarch64.squashfs" },
  { name = "Java jre17.0.13", file = "zulu17.54.21-ca-jre17.0.13-linux.aarch64.squashfs" },
  { name = "Java jdk17.0.10", file = "zulu17.48.15-ca-jdk17.0.10-linux.aarch64.squashfs" },
  { name = "Java jre23.0.2", file = "zulu23.32.11-ca-jre23.0.2-linux.aarch64.squashfs" },
  { name = "Qt6/EGLFS 6.6.3", file = "qt6-eglfs-6.6.3-aarch64.squashfs" },
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
M.LEGEND = "installed and runs"
M.LEGEND2 = "downloaded, cannot run"

-- Rows of the checklist one page holds: the box has 15 rows between its top edge and the two choices; a page is two columns.
M.ROWS = 15
M.COLS = 2

M.sys = {
  ls = function(dir)
    local p = io.popen("ls -1 '" .. dir .. "' 2>/dev/null")
    local out = {}
    if p then for line in p:lines() do out[#out + 1] = line end p:close() end
    return out
  end,
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

--- Every runtime with its state: "ok" (r3i6nos's package for it is installed: it runs), "downloaded" (PortMaster's file is in its libs folder, which
--- r3i6nos cannot mount without root, so it does not run), or "none". The usable ones come first, then the downloaded, then the rest in list order.
function M.runtimes()
  local have = {}
  for _, f in ipairs(M.sys.ls(M.ROMS .. "/ports/PortMaster/libs")) do have[f] = true end
  local out = {}
  for i, r in ipairs(M.LIST) do
    local st = "none"
    if r.cmd and M.sys.exists(M.TOOLS .. "/" .. r.cmd) then st = "ok" elseif have[r.file] then st = "downloaded" end
    out[i] = { name = r.name, state = st, installed = st == "ok", order = i }
  end
  local rank = { ok = 1, downloaded = 2, none = 3 }
  table.sort(out, function(a, b) if rank[a.state] ~= rank[b.state] then return rank[a.state] < rank[b.state] end return a.order < b.order end)
  return out
end

--- How many runtimes fit on each page: page 1 shares the screen with the text (textRows lines, a blank and the two heading rows), the others have it all.
function M.pageSizes(count, textRows)
  local sizes, left = {}, count
  local first = math.max(0, M.ROWS - textRows - 3) * M.COLS
  local later = (M.ROWS - 2) * M.COLS
  sizes[1] = math.min(first, left); left = left - sizes[1]
  while left > 0 do sizes[#sizes + 1] = math.min(later, left); left = left - sizes[#sizes] end
  return sizes
end

--- Is this menu entry PortMaster's launcher (the native app of that name)?
function M.is(entry) return entry and entry.type == "native" and entry.title == "PortMaster" or false end

return M
