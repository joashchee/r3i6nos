-- Scans the games card for things to launch. Pure Lua, no LÖVE calls, so it
-- can be tested with plain luajit.

local scan = {}

local ROOTS = {
  { dir = "Love 11.5", type = "love115" },
  { dir = "Love 12.0", type = "love120" },
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
  return (name:gsub("%.love$", ""):gsub("/$", ""):gsub("[_]+", " "))
end

--- Returns a sorted list of { title, type, target }.
function scan.entries(roms, launchers)
  local list = {}
  for _, root in ipairs(ROOTS) do
    for _, name in ipairs(ls(roms .. "/" .. root.dir)) do
      local path = roms .. "/" .. root.dir .. "/" .. name:gsub("/$", "")
      local isdir = name:sub(-1) == "/"
      if (isdir and exists(path .. "/main.lua")) or name:match("%.love$") then
        list[#list + 1] = { title = title(name), type = root.type, target = path }
      end
    end
  end
  for _, name in ipairs(ls(launchers)) do
    if name:match("%.sh$") then
      list[#list + 1] = {
        title = name:gsub("%.sh$", ""),
        type = "native",
        target = launchers .. "/" .. name,
      }
    end
  end
  table.sort(list, function(a, b) return a.title:lower() < b.title:lower() end)
  return list
end

return scan
