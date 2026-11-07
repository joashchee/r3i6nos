-- The words of Settings > Network and SSH: instructions the person reads on the pad, and the paging of any result. Pure Lua.
-- Every page is at most PER lines of at most WIDTH characters (the menu never scrolls); tests/test_netpages.lua checks it.
-- Untested on a real R36S: which USB adapters the stock 4.4 kernel has drivers for is not known yet.

local P = {}

P.PER, P.WIDTH = 18, 60

P.NETWORK = {
  {
    "This unit has no network built in. To get online, plug a USB",
    "adapter into its USB port (this needs a port that can act as",
    "a USB host; a charge-only port cannot):",
    "",
    "  Wired:  a USB Ethernet adapter and a cable to your router.",
    "  Wi-Fi:  a USB Wi-Fi dongle.",
    "",
    "Whether an adapter works depends on the kernel having its",
    "driver. If Status lists no adapter after you plug it in,",
    "that one is not supported: try another model.",
    "",
    "Nothing connects by itself, ever. The network is off until",
    "you set it up here, and it is off again after a reboot.",
  },
  {
    "WIRED",
    "",
    "1. Plug the adapter in, then the cable to your router.",
    "2. Network > Wired setup. It waits for the cable, then asks",
    "   the router for an address (DHCP).",
    "3. Network > Test connection checks each step and says what",
    "   to try when one fails.",
    "",
    "A fixed (static) address is not offered: use a router that",
    "hands addresses out, or reserve one for this unit's MAC",
    "address (shown in Status).",
  },
  {
    "WI-FI",
    "",
    "1. Plug the dongle in. Network > Status should list wlan0.",
    "2. Network > Wi-Fi setup scans and lists networks, strongest",
    "   first. Choose yours and type its password with the pad:",
    "   D-pad move, A type, X delete, Y change layer, Select",
    "   show or hide, Start done, B cancel.",
    "3. A joined network is remembered: Wi-Fi: join saved joins",
    "   it again later with no typing. Forget saved removes it.",
    "",
    "Supported: open and WPA/WPA2 passphrase networks.",
    "Not offered: WEP and enterprise (802.1X). For a network",
    "that is not in the list, choose Other network.",
  },
  {
    "IF IT DOES NOT WORK",
    "",
    "- No adapter in Status: the driver is missing; try another.",
    "- Wired says no link: check the cable and the router lights.",
    "- Wi-Fi will not join: re-check the password (capitals",
    "  count), get closer, and use a 2.4 GHz network if the",
    "  dongle is single-band.",
    "- Joined but no address: the router's DHCP is off or full.",
    "- Test fails at DNS or Internet: the router has no internet.",
  },
}

P.SSH = {
  {
    "SSH lets you log in from a computer on the same network, to",
    "copy files to and from the games card or to run commands.",
    "",
    "It is OFF. It never starts by itself and is off again after",
    "a reboot. Start it only on a network you trust.",
    "",
    "You need one way to log in first:",
    "  - Set SSH password (8 or more characters), or",
    "  - a key: put your public key in a file named",
    "    authorized_keys in a folder named ssh on the games card",
    "    (EASYROMS). It is copied when SSH starts; the card is",
    "    only read.",
  },
  {
    "Then choose Start SSH. The screen shows the command to type",
    "on your computer, like:",
    "",
    "    ssh loveos@192.168.1.20",
    "",
    "The first time, your computer asks you to trust the unit's",
    "host key; Status shows its fingerprint to compare. Each card",
    "makes its own host keys the first time SSH is started.",
    "",
    "Only the user loveos can log in (never root), with at most 3",
    "tries, and no port forwarding. Stop SSH when you are done.",
    "",
    "Note: anything you run on this unit as loveos, games",
    "included, can ask for SSH to be started. Only run software",
    "you trust.",
  },
}

--- Split a flat list of lines (a helper's answer) into pages of PER lines. Long lines are cut when drawn (theme.fit), not here.
function P.paginate(lines)
  local pages, cur = {}, {}
  for _, l in ipairs(lines) do
    cur[#cur + 1] = l
    if #cur == P.PER then pages[#pages + 1] = cur; cur = {} end
  end
  if #cur > 0 or #pages == 0 then pages[#pages + 1] = cur end
  return pages
end

return P
