-- Scans the games card for things to launch. Pure Lua, no LÖVE calls, so it
-- can be tested with plain luajit.

local scan = {}

-- What a game is, in each folder: LÖVE games need nothing but themselves. The game-engine runtimes (docs/RUNTIMES.md, ROADMAP section 33) are packages
-- (r3i6nos-runtime-pyxel, -renpy, -solarus, -ags, -godot, installed from native_deb/ like any package) and a game for one is a folder, or an archive, in "Pyxel", "Renpy",
-- "Solarus", "AGS" or "Godot" on a card; run_app.sh runs it with that runtime's command and says which package is missing when it is not installed.
local ROOTS = {
  { dir = "Love 11.5", type = "love115" },
  { dir = "Love 12.0", type = "love120" },
  { dir = "Pyxel", type = "pyxel", valid = function(path, name, isdir, exists, ls)
      return (isdir and exists(path .. "/main.py")) or name:match("%.pyxapp$") ~= nil
    end },
  { dir = "Renpy", type = "renpy", valid = function(path, name, isdir, exists, ls)
      return isdir and #ls(path .. "/game") > 0
    end },
  { dir = "Solarus", type = "solarus", valid = function(path, name, isdir, exists, ls)
      return isdir and (exists(path .. "/data/quest.dat") or exists(path .. "/data.solarus") or exists(path .. "/data.solarus.zip"))
    end },
  { dir = "AGS", type = "ags", valid = function(path, name, isdir, exists, ls)
      if not isdir then return false end
      if exists(path .. "/ac2game.dat") then return true end
      for _, n in ipairs(ls(path)) do local l = n:lower(); if l:match("%.ags$") or l:match("%.exe$") then return true end end
      return false
    end },
  { dir = "Godot", type = "godot", valid = function(path, name, isdir, exists, ls)   -- an exported game: a .pck, or a folder that holds one
      if not isdir then return name:lower():match("%.pck$") ~= nil end
      for _, n in ipairs(ls(path)) do if n:lower():match("%.pck$") then return true end end
      return false
    end },
}

local function ls(path)
  local out = {}
  local p = io.popen('ls -1p "' .. path:gsub('"', '\\"') .. '" 2>/dev/null')
  if not p then return out end
  for line in p:lines() do out[#out + 1] = line end
  p:close()
  return out
end

local function exists(path)
  local f = io.open(path, "r")
  if f then f:close() return true end
  return false
end

local function title(name)
  return (name:gsub("%.love$", ""):gsub("%.pyxapp$", ""):gsub("%.pck$", ""):gsub("/$", ""):gsub("[_]+", " "))
end

--- Returns a sorted list of { title, type, target }.
--- roms: the system card's games partition. launchers: native app launchers. defaults (optional): the same
--- "Love 11.5" / "Love 12.0" folders, built into the system image; a game of the same file name on
--- a card replaces the built-in one. roms2 (optional): the data card in the second slot (docs/STORAGE.md); until step 2
--- tags each entry with its card, a game of the same file name on both cards is listed once, from the system card.
function scan.entries(roms, launchers, defaults, roms2)
  local list, have = {}, {}
  for _, base in ipairs({ roms, roms2 or false, defaults or false }) do
    if base then
    for _, root in ipairs(ROOTS) do
      for _, name in ipairs(ls(base .. "/" .. root.dir)) do
        local path = base .. "/" .. root.dir .. "/" .. name:gsub("/$", "")
        local isdir = name:sub(-1) == "/"
        local key = root.type .. ":" .. name
        local ok
        if root.valid then ok = root.valid(path, name, isdir, exists, ls)
        else ok = (isdir and exists(path .. "/main.lua")) or name:match("%.love$") end
        if not have[key] and ok then
          have[key] = true
          list[#list + 1] = { title = title(name), type = root.type, target = path }
        end
      end
    end
    end
  end
  -- Ports PortMaster installed (docs/PORTMASTER.md): <card>/ports/<Name>.sh, run by run_app.sh as type "port". PortMaster's own launcher
  -- (PortMaster.sh) is the native app of the same name, so it is not listed twice. The same name on both cards shows once, from the system card.
  local ported = {}
  for _, base in ipairs({ roms, roms2 or false }) do
    if base then
      for _, name in ipairs(ls(base .. "/ports")) do
        if name:match("%.sh$") and name ~= "PortMaster.sh" and not ported[name] then
          ported[name] = true
          list[#list + 1] = { title = title((name:gsub("%.sh$", ""))), type = "port", target = base .. "/ports/" .. name }
        end
      end
    end
  end
  for _, name in ipairs(ls(launchers)) do
    if name:match("%.sh$") then
      list[#list + 1] = {
        title = title((name:gsub("%.sh$", ""))),
        type = "native",
        target = launchers .. "/" .. name,
      }
    end
  end
  table.sort(list, function(a, b) return a.title:lower() < b.title:lower() end)
  return list
end

return scan
