-- Settings > Data card (docs/STORAGE.md, "Format the data card"): what the card in the second slot is, and the request to the root helper
-- (overlay/usr/local/bin/r3i6nos-format) that erases it, formats it (exFAT or ext4), makes the folders and mounts it.
-- Pure Lua but for the file reads, so luajit tests it (tests/test_settings.lua).
--
-- Overrides, for tests: R3I6NOS_FORMAT_DIR (/run/loveos/format), R3I6NOS_FORMAT_OUT (/run/r3i6nos-format), R3I6NOS_CARDS_STATE (/run/r3i6nos/cards),
-- R3I6NOS_PERSIST_OUT (/run/r3i6nos/persist: what r3i6nos-persist found, docs/SAVED-SETTINGS.md).

local netclient = require("netclient")

local M = {}

M.STATE = os.getenv("R3I6NOS_CARDS_STATE") or "/run/r3i6nos/cards"
M.client = netclient.new(os.getenv("R3I6NOS_FORMAT_DIR") or "/run/loveos/format", os.getenv("R3I6NOS_FORMAT_OUT") or "/run/r3i6nos-format")
M.PERSIST = os.getenv("R3I6NOS_PERSIST_OUT") or "/run/r3i6nos/persist"
M.WAIT = 1500 -- seconds: a big card takes a while to format (the helper's own limit is the same)

M.FORMATS = {
  { fs = "exfat", label = "exFAT", hint = "Recommended: a Mac, Windows or Linux computer can read it" },
  { fs = "ext4", label = "ext4", hint = "Linux only: a Mac or Windows computer cannot read it" },
}

--- What r3i6nos-cards found at boot, as a table (BOOT_DISK, DATA_DEV, DATA_FS, DATA_AT ...); {} when it has not run.
function M.state()
  local st = {}
  local f = io.open(M.STATE, "r")
  if not f then return st end
  for line in f:lines() do
    local k, v = line:match("^(%u[%u_]*)=(.*)$")
    if k then st[k] = v end
  end
  f:close()
  return st
end

--- What r3i6nos-persist wrote about the saved settings (STATE, ITEMS, LAST, RESTORED, CONFLICTS, SKIPPED, ERRORS, WHY); {} when it has not run.
function M.persistState()
  local ps = {}
  local f = io.open(M.PERSIST, "r")
  if not f then return ps end
  for line in f:lines() do
    local k, v = line:match("^(%u[%u_]*)=(.*)$")
    if k then ps[k] = v end
  end
  f:close()
  return ps
end

M.WORDS = { wifi = "Wi-Fi", timezone = "time zone", ["clock-set"] = "clock", clock = "clock", mixer = "sound levels", battery = "battery history" }

--- "wifi,timezone,clock-set,clock" as "Wi-Fi, time zone, clock": each name once, the clock question's mark left out.
function M.words(csv)
  local out, seen = {}, {}
  for id in (csv or ""):gmatch("[^,]+") do
    local w = M.WORDS[id]
    if w and not seen[w] then seen[w] = true; out[#out + 1] = w end
  end
  return table.concat(out, ", ")
end

local function cut(s, n)
  if #s <= n then return s end
  return s:sub(1, n - 1) .. ">"
end

--- The saved settings' part of the Status screen: where they are kept, what was brought back, what was not.
function M.persistLines(ps)
  ps = ps or M.persistState()
  local lines = {}
  local function add(s) lines[#lines + 1] = cut(s, 46) end
  if ps.STATE == "synced" then
    add("Saved settings are kept on this card too:")
    add(("  %s of 7 items, checked at %s"):format(ps.ITEMS or "?", os.date("%H:%M", tonumber(ps.LAST) or 0)))
    if (ps.RESTORED or "") ~= "" then
      add("  Restored from the card:")
      local line = "   "                                     -- the names, wrapped at the screen's width
      for w in (M.words(ps.RESTORED) .. ","):gmatch("%s*([^,]+),") do
        if #line + #w + 2 > 44 and #line > 3 then add(line); line = "   " end
        line = line .. (#line > 3 and ", " or "") .. w
      end
      add(line)
    end
    if (tonumber(ps.CONFLICTS) or 0) > 0 then add(("  %s older copies kept in .merge-conflicts"):format(ps.CONFLICTS)) end
    if (ps.ERRORS or "") ~= "" then add("  Not kept: " .. ps.ERRORS) end
    if (ps.SKIPPED or ""):find("wifi", 1, true) then add("  The Wi-Fi key is not kept on the card.")
    else add("  The Wi-Fi key (not the password) is kept.") end
  elseif ps.STATE == nil or ps.STATE == "never" then
    add("Saved settings: not checked yet.")
  elseif ps.STATE == "no-card" then
    add("Saved settings are on the system card only:")
    add("  there is no data card to keep them on.")
  else
    add("Saved settings are not kept on the card:")
    add("  " .. ((ps.WHY or "") ~= "" and ps.WHY or ps.STATE))
  end
  return lines
end

--- The Status screen.
function M.statusLines(st)
  st = st or M.state()
  local lines = {}
  local function add(s) lines[#lines + 1] = s end
  if st.DATA_DEV and st.DATA_DEV ~= "none" then
    add("The data card (the second slot, TF2):")
    add("")
    add(("  Device      %s"):format(st.DATA_DEV))
    add(("  Filesystem  %s"):format(st.DATA_FS or "?"))
    add(("  Open at     %s"):format((st.DATA_AT and st.DATA_AT ~= "none") and st.DATA_AT or "not open"))
  else
    add("No data card is open.")
    add("")
    add("Either there is no card in the second slot (TF2),")
    add("or its filesystem is not one this unit opens")
    add("(exFAT, FAT or ext4). Format it from this menu.")
  end
  add("")
  for _, l in ipairs(M.persistLines()) do add(l) end
  add("")
  add("The system card (the first slot, TF1) is never")
  add("formatted from here.")
  return lines
end

--- Why and what, for the "What this does" page.
M.ABOUT = {
  "Format a card for the data card slot (TF2):",
  "it is erased, given one partition, formatted,",
  "given the folders Love 11.5, Love 12.0 and",
  "native_deb, and opened at /roms2.",
  "",
  "Only the card in the second slot can be formatted.",
  "The system card in the first slot is never touched.",
  "",
  "exFAT is read by a Mac, Windows and Linux.",
  "ext4 is faster on the unit but Linux only.",
  "",
  "If the unit cannot see the new partition until it",
  "restarts, it asks to restart and finishes then.",
}

return M
