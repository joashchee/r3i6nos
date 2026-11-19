-- Run with: luajit tests/test_datacard.lua   (from the repo root)
-- Settings > Data card's Status screen (menu/datacard.lua): the card, and the saved settings that r3i6nos-persist keeps on it (docs/SAVED-SETTINGS.md).
package.path = "menu/?.lua;theme/?.lua;" .. package.path

local tmp = (os.getenv("TMPDIR") or "/tmp"):gsub("/$", "") .. ("/r3i6nos-datacard-%d"):format(os.time())
assert(os.execute(("mkdir -p '%s'"):format(tmp)))
local dc = require("datacard")
dc.STATE, dc.PERSIST = tmp .. "/cards", tmp .. "/persist"

local function eq(a, b, m) assert(a == b, (m or "") .. (" want %s, got %s"):format(tostring(b), tostring(a))) end
local function write(p, t) local f = assert(io.open(p, "w")); f:write(t); f:close() end
local function has(lines, needle)
  for _, l in ipairs(lines) do if l:find(needle, 1, true) then return true end end
  return false
end

-- before r3i6nos-persist has run
eq(dc.persistState().STATE, nil, "no file, no state")
assert(has(dc.persistLines(), "not checked yet"))

-- the words: each name once, the clock question's mark left out, unknown names ignored
eq(dc.words("wifi,timezone,clock-set,clock,mixer,battery,clock-asked"), "Wi-Fi, time zone, clock, sound levels, battery history")
eq(dc.words("wifi,ssh"), "Wi-Fi"); eq(dc.words(""), ""); eq(dc.words(nil), "")

-- synced, with things brought back after a reflash
write(dc.PERSIST, "STATE=synced\nAT=/roms2/r3i6nos/settings\nCARD=abcd\nLAST=1800000000\nITEMS=7\nSAVED=\nRESTORED=wifi,timezone,clock\nCONFLICTS=0\nSKIPPED=\nERRORS=\n")
local l = dc.persistLines()
assert(has(l, "kept on this card too"))
assert(has(l, "7 of 7 items, checked at"))
assert(has(l, "Restored from the card:")); assert(has(l, "Wi-Fi, time zone, clock"))
assert(has(l, "The Wi-Fi key (not the password) is kept."), "what is stored is said plainly")
assert(not has(l, "merge-conflicts"), "no conflicts, no line")
write(dc.PERSIST, "STATE=synced\nLAST=1800000000\nITEMS=7\nRESTORED=wifi,timezone,clock-set,clock,mixer,battery,clock-asked\nCONFLICTS=0\nSKIPPED=\nERRORS=\n")
local all = table.concat(dc.persistLines(), "|")
assert(all:find("Wi-Fi, time zone, clock, sound levels", 1, true) and all:find("battery history", 1, true), "every restored name is shown, wrapped: " .. all)
write(dc.PERSIST, "STATE=synced\nAT=/roms2/r3i6nos/settings\nCARD=abcd\nLAST=1800000000\nITEMS=7\nSAVED=\nRESTORED=wifi,timezone,clock\nCONFLICTS=0\nSKIPPED=\nERRORS=\n")
for _, line in ipairs(l) do assert(#line <= 46, "fits the screen: " .. line) end

-- conflicts, errors and a forgotten Wi-Fi key
write(dc.PERSIST, "STATE=synced\nLAST=1800000000\nITEMS=5\nRESTORED=\nCONFLICTS=2\nSKIPPED=wifi\nERRORS=mixer: the card's copy is not usable: not a mixer state\n")
l = dc.persistLines()
assert(has(l, "2 older copies kept in .merge-conflicts"))
assert(has(l, "Not kept: mixer"))
assert(has(l, "The Wi-Fi key is not kept on the card."))
for _, line in ipairs(l) do assert(#line <= 46, "a long error is cut to the screen: " .. line) end

-- no data card, and a card that cannot be used
write(dc.PERSIST, "STATE=no-card\nWHY=No data card is in.\n")
l = dc.persistLines()
assert(has(l, "on the system card only")); assert(has(l, "no data card to keep them on"))
write(dc.PERSIST, "STATE=readonly\nWHY=The data card cannot be written (Read-only file system).\n")
l = dc.persistLines()
assert(has(l, "not kept on the card")); assert(has(l, "cannot be written"))

-- the whole Status screen: the card, then the saved settings, then the reminder about the system card
write(dc.STATE, "DATA_DEV=/dev/mmcblk1p1\nDATA_FS=exfat\nDATA_AT=/roms2\n")
write(dc.PERSIST, "STATE=synced\nLAST=1800000000\nITEMS=7\nRESTORED=\nCONFLICTS=0\nSKIPPED=\nERRORS=\n")
l = dc.statusLines()
assert(has(l, "Filesystem  exfat")); assert(has(l, "kept on this card too")); assert(has(l, "never"))
eq(l[#l], "formatted from here.")
os.execute(("rm -rf '%s'"):format(tmp))
print("datacard ok")
