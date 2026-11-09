-- Settings > Sound mixer (docs/SOUND.md): every mixer control of card 0 (the software Volume and Boost that /etc/asound.conf puts in
-- front of the codec, and whatever the codec itself has, such as its Playback Path), changed with Left/Right while a test sound loops.
-- Pure Lua but for M.sys (amixer through popen), so luajit tests it (tests/test_mixer.lua). The sounds are made here, in code: no
-- recording of anyone's is in the image. main.lua plays them (the only LÖVE part).

local M = {}

M.AMIXER = os.getenv("R3I6NOS_AMIXER") or "amixer"
M.RATE = 22050

--- What touches the system; tests replace it. run(args) gives amixer's output, or nil when it failed.
M.sys = {
  run = function(args)
    local q = {}
    for i, a in ipairs(args) do q[i] = "'" .. tostring(a):gsub("'", "'\\''") .. "'" end
    local p = io.popen(table.concat(q, " ") .. " 2>/dev/null; echo \"rc=$?\"")
    if not p then return nil end
    local out = p:read("*a")
    p:close()
    local rc = tonumber(out:match("rc=(%d+)%s*$"))
    if rc ~= 0 then return nil end
    return (out:gsub("rc=%d+%s*$", ""))
  end,
}

-- ---- reading and changing the controls -------------------------------------------------------------------------------------------

--- amixer scontents as a list: { name, caps, min, max, value, percent, db, items, item }.
function M.parse(text)
  local out, cur = {}, nil
  for line in (text or ""):gmatch("[^\n]+") do
    local name = line:match("^Simple mixer control '(.*)',%d+$")
    if name then
      cur = { name = name, caps = "", items = {} }
      out[#out + 1] = cur
    elseif cur then
      local s = line:match("^%s*(.-)%s*$")
      if s:match("^Capabilities:") then cur.caps = s:match(":%s*(.*)")
      elseif s:match("^Limits:") then
        local lo, hi = s:match("(%-?%d+) %- (%-?%d+)")
        cur.min, cur.max = tonumber(lo), tonumber(hi)
      elseif s:match("^Items:") then
        for it in s:gmatch("'([^']*)'") do cur.items[#cur.items + 1] = it end
      elseif s:match("^Item0:") then cur.item = s:match("'([^']*)'")
      elseif cur.value == nil and not s:match("channels:") then
        -- the first channel: "Front Left: 16 [80%] [-8.00dB]" (softvol) or "Mono: Playback 200 [78%] [-10.00dB] [on]"
        local v = s:match("^[%a ]+: Playback (%-?%d+)") or s:match("^[%a ]+: (%-?%d+)")
        if v then
          cur.value = tonumber(v)
          cur.percent = tonumber(s:match("%[(%d+)%%%]"))
          cur.db = tonumber(s:match("%[(%-?[%d%.]+)dB%]"))
        end
      end
    end
  end
  return out
end

function M.read()
  return M.parse(M.sys.run({ M.AMIXER, "-c", "0", "scontents" }))
end

--- Whether a control can be changed here: a level (with limits) or a choice.
function M.adjustable(c) return (c.value ~= nil and c.min ~= nil and c.max ~= nil and c.max > c.min) or #c.items > 0 end

--- The controls in the order shown: Volume, Boost, then the codec's own.
function M.order(controls)
  local first = { Volume = 1, Boost = 2 }
  local list = {}
  for _, c in ipairs(controls) do if M.adjustable(c) then list[#list + 1] = c end end
  table.sort(list, function(a, b)
    local fa, fb = first[a.name] or 3, first[b.name] or 3
    if fa ~= fb then return fa < fb end
    return a.name < b.name
  end)
  return list
end

--- The lowest step a control may be set to: Boost's step 0 is silence (a softvol's lowest step; /etc/asound.conf), so it stops at
--- step 1, 0 dB. Any other control: its own minimum.
M.FLOOR = { Boost = 1 }
local function floor(c) return math.max(c.min or 0, M.FLOOR[c.name] or c.min or 0) end

--- The amixer arguments that move control c by steps (negative: down; a choice moves one item whatever the size), stopping at
--- its ends; nil when it is already at the end it is moving to.
function M.change(c, steps)
  local dir = steps < 0 and -1 or 1
  if #c.items > 0 then
    local i = 1
    for k, it in ipairs(c.items) do if it == c.item then i = k end end
    local j = i + dir
    if j < 1 or j > #c.items then return nil end
    return { M.AMIXER, "-c", "0", "-q", "sset", c.name, c.items[j] }
  end
  local v = math.max(floor(c), math.min(c.max, c.value + steps))
  if v == c.value then return nil end
  return { M.AMIXER, "-c", "0", "-q", "sset", c.name, tostring(v) }
end

--- Move control c and read it back. Returns the new list of controls, or nil and why.
function M.step(c, dir)
  local args = M.change(c, dir)
  if not args then return nil, dir > 0 and "Already at the top." or "Already at the bottom." end
  if not M.sys.run(args) then return nil, "Could not change " .. c.name .. "." end
  return M.read()
end

--- How a control's value reads: "80%  -8 dB", "+6 dB", "SPK".
function M.valueText(c)
  if #c.items > 0 then return c.item or "?" end
  if c.name == "Volume" and c.value == 0 then return "off" end   -- a softvol's step 0 is silence, whatever dB amixer reports
  if c.name == "Boost" and c.db then return c.value == 0 and "silent (step 0)" or ("%+.0f dB"):format(c.db) end
  local parts = {}
  if c.percent then parts[#parts + 1] = ("%d%%"):format(c.percent) end
  if c.db then parts[#parts + 1] = ("%+.0f dB"):format(c.db) end
  if #parts == 0 then parts[1] = ("%d of %d"):format(c.value, c.max) end
  return table.concat(parts, "  ")
end

--- 0..1: how far up a level is (nil for a choice).
function M.fraction(c)
  if #c.items > 0 or not c.max or c.max == c.min then return nil end
  local lo = floor(c)
  return math.max(0, (c.value - lo) / (c.max - lo))
end

M.HINTS = {
  Volume = "Quieter or louder (2 dB a step); the volume keys do the same",
  Boost = "Louder for quiet sound; it distorts sound that is already loud",
  ["Playback Path"] = "Where the sound goes; set by itself from the headphone jack",
}

-- ---- the dialog (Settings > Sound mixer) -------------------------------------------------------------------------------------------

M.PER = 14       -- controls on a page; the test sound's row is always below them
M.BIG = 5        -- L1/R1: steps at a time

--- A new dialog: the controls as read now, the first one chosen, no sound playing.
function M.new()
  return { controls = M.order(M.read()), sel = 1, sound = 1 }
end

--- Rows: each control, then the test sound.
function M.rows(n) return #n.controls + 1 end
function M.onSound(n) return n.sel == M.rows(n) end

--- The page the chosen row is on, and how many there are (the sound's row belongs to every page).
function M.page(n)
  local pages = math.max(1, math.ceil(#n.controls / M.PER))
  if M.onSound(n) then return pages, pages end
  return math.floor((n.sel - 1) / M.PER) + 1, pages
end

--- Feed one action. Returns "close" when B leaves it, else nil. n.msg says what went wrong, or is nil.
function M.press(n, action)
  local rows = M.rows(n)
  n.msg = nil
  if action == "back" then return "close" end
  if action == "up" then n.sel = (n.sel - 2) % rows + 1
  elseif action == "down" then n.sel = n.sel % rows + 1
  elseif action == "home" then n.sel = 1
  elseif action == "end" then n.sel = rows
  else
    local steps = ({ left = -1, right = 1, pageup = -M.BIG, pagedown = M.BIG, accept = 1 })[action]
    if not steps then return nil end
    if M.onSound(n) then
      if action == "accept" then n.sound = n.sound % #M.SOUNDS + 1
      else n.sound = math.max(1, math.min(#M.SOUNDS, n.sound + (steps < 0 and -1 or 1))) end
      return nil
    end
    if action == "accept" then return nil end
    local c = n.controls[n.sel]
    local new, why = M.step(c, steps)
    if not new then n.msg = why; return nil end
    n.controls = M.order(new)
    for i, x in ipairs(n.controls) do if x.name == c.name then n.sel = i end end
  end
  return nil
end

--- The reply line's hint for the chosen row.
function M.hint(n)
  if n.msg then return n.msg end
  if M.onSound(n) then return "A test sound to hear the levels by; it stops when you leave" end
  local c = n.controls[n.sel]
  return M.HINTS[c.name] or ("A control of the sound chip: " .. (c.caps ~= "" and c.caps or "?"))
end

-- ---- the test sounds ------------------------------------------------------------------------------------------------------------

-- 1 kHz and music two octaves up: on the unit 440 Hz tones were not heard from the speaker (2026-11-09, docs/hardware-findings.md).
M.SOUNDS = { "Off", "Tone, left only", "Tone, right only", "Tone, both sides", "Music" }
M.TONE_HZ = 1000

-- The music: four chords (C, A minor, F, G), each 1.6 s: a bass note and an arpeggio of eighth notes. Made here, so it is ours.
local CHORDS = { { 72, 84, 88, 91 }, { 69, 81, 84, 88 }, { 65, 77, 81, 84 }, { 67, 79, 83, 86 } } -- MIDI: bass (349-523 Hz), then the chord
local ARP = { 1, 2, 3, 2, 1, 2, 3, 4 }   -- which chord tone (4 = the top one an octave up) each eighth plays
M.CHORD_SECS = 1.6
local function hz(note) return 440 * 2 ^ ((note - 69) / 12) end

--- Frames in one loop of sound k (2-5): a whole number of cycles, so the loop has no click.
function M.frames(k, rate)
  rate = rate or M.RATE
  if k == 5 then return math.floor(#CHORDS * M.CHORD_SECS * rate) end
  return rate -- one second of the tone: whole cycles, so the loop joins without a click
end

--- Sample i (0-based) of sound k at rate: left, right in -1..1.
function M.sample(k, i, rate)
  rate = rate or M.RATE
  if k >= 2 and k <= 4 then
    local v = math.sin(2 * math.pi * M.TONE_HZ * i / rate) * 0.5
    return (k == 3) and 0 or v, (k == 2) and 0 or v
  end
  if k ~= 5 then return 0, 0 end
  local t = i / rate
  local ci = math.floor(t / M.CHORD_SECS) % #CHORDS + 1
  local chord = CHORDS[ci]
  local tc = t % M.CHORD_SECS
  -- every note starts at its own phase zero and fades to nothing before the next: no clicks, and the loop joins without a seam
  local bass = math.sin(2 * math.pi * hz(chord[1]) * tc) * 0.35 * math.min(1, tc / 0.01) * (1 - tc / M.CHORD_SECS) ^ 2
  local eighth = M.CHORD_SECS / #ARP
  local ai = math.floor(tc / eighth) + 1
  local te = tc % eighth
  local which = ARP[ai]
  local note = which == 4 and chord[2] + 12 or chord[which + 1]
  local f = hz(note)
  local env = math.min(1, te / 0.005) * (1 - te / eighth) ^ 2
  local lead = (math.sin(2 * math.pi * f * te) + 0.3 * math.sin(6 * math.pi * f * te)) * 0.42 * env
  local v = bass + lead
  return v, v
end

return M
