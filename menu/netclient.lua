-- Talks to the root helper (overlay/usr/local/bin/r3i6nos-net) through two files: the menu writes `request` (key=value lines) into
-- /run/loveos/net, which its user owns; the root unit r3i6nos-net.path notices, the helper does one allowlisted verb and writes
-- `result` into /run/r3i6nos-net, which only root can write (so a link planted by a game can never redirect root's write).
-- Plain files only (no LÖVE calls), so luajit can test it. R3I6NOS_NET_DIR and R3I6NOS_NET_OUT move the folders, for tests.

local M = {}

M.DIR = os.getenv("R3I6NOS_NET_DIR") or "/run/loveos/net"
M.OUT = os.getenv("R3I6NOS_NET_OUT") or "/run/r3i6nos-net"

math.randomseed(os.time() + math.floor(os.clock() * 1e6))

local counter = 0

--- Backslash and newline escaped, so a value stays on its line.
function M.escape(v)
  return (tostring(v):gsub("\\", "\\\\"):gsub("\n", "\\n"))
end

--- Ask for a verb with args (a table of strings). Returns the request id, or nil and a reason.
function M.request(verb, args)
  counter = counter + 1
  local id = ("%d-%d-%d"):format(os.time(), counter, math.random(0, 999999)) -- unique, so an old answer is never taken for this one
  local lines = { "id=" .. id, "verb=" .. verb }
  local keys = {}
  for k in pairs(args or {}) do keys[#keys + 1] = k end
  table.sort(keys)
  for _, k in ipairs(keys) do lines[#lines + 1] = k .. "=" .. M.escape(args[k]) end
  local tmp = M.DIR .. "/request.tmp"
  local f = io.open(tmp, "w")
  if not f then return nil, "cannot write " .. tmp end
  f:write(table.concat(lines, "\n"), "\n")
  f:close()
  local ok, err = os.rename(tmp, M.DIR .. "/request") -- one step: the helper never sees half a file
  if not ok then return nil, err end
  return id
end

--- Parse a result file's text: { id, ok, lines = { text }, nets = { { ssid, signal, security } } }.
function M.parse(text)
  local r = { lines = {}, nets = {}, ok = false }
  for line in (text .. "\n"):gmatch("(.-)\n") do
    local tag, rest = line:match("^(%u)\t(.*)$")
    if tag == "L" then r.lines[#r.lines + 1] = rest
    elseif tag == "N" then
      local ssid, signal, sec = rest:match("^(.-)\t(.-)\t(.*)$")
      if ssid then r.nets[#r.nets + 1] = { ssid = ssid, signal = signal, security = sec } end
    else
      local k, v = line:match("^(%w+)=(.*)$")
      if k == "id" then r.id = v elseif k == "ok" then r.ok = (v == "1") end
    end
  end
  return r
end

--- The answer to request id, or nil while there is none yet (or the file is another request's).
function M.poll(id)
  local f = io.open(M.OUT .. "/result", "r")
  if not f then return nil end
  local text = f:read("*a")
  f:close()
  local r = M.parse(text)
  if r.id ~= id then return nil end
  return r
end

return M
