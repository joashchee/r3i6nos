# Network and SSH (Settings > Network, Settings > SSH)

Status (2026-11-07): written and tested on a laptop with a fake system. **Nothing here has run on a real R36S**: whether any USB adapter has a
driver in the stock 4.4.189 kernel, whether the commands behave on that userland, and whether systemd 252 starts the path unit are all unknown.

## What the person sees

Start opens Settings: **Network**, **SSH**, **Battery** (`docs/BATTERY.md`), **Shutdown**, **Back**. Everything is a screen on the pad, never scrolling, with whole pages.

- **Network:** Status (adapters, address, gateway, DNS, the joined Wi-Fi, MAC), Wired setup, Wi-Fi setup (scan, list by signal, pick, type the
  password on the on-screen keyboard, or *Other network...* for a name that is not listed), Wi-Fi: join saved, Forget saved Wi-Fi, Test connection,
  Disconnect, Instructions (what to plug in, wired, Wi-Fi, if it does not work).
- **SSH:** Status, Set SSH password (typed twice), Start SSH, Stop SSH, Instructions.
- **On-screen keyboard:** D-pad moves, A types, X deletes, Y changes layer (abc, ABC, symbols), Select shows or hides the text, Start is done, B cancels.
  A real keyboard types too.
- **Test connection** reports each step as `[ok]` or `[FAIL]` with what to try: link, address, gateway and a ping to it, a DNS lookup of
  `deb.debian.org`, and a ping to `1.1.1.1`. Those two targets are the only outside hosts it ever contacts, only when asked, and the screen names them.

## Rules it keeps (CLAUDE.md rule 6: no telemetry, no network at boot, no accounts)

- **Nothing connects at boot or by itself.** The network is off until a person sets it up, and off after a reboot. A saved Wi-Fi network is only
  *joined again when the person chooses "Wi-Fi: join saved"*; there is no auto-connect. (If auto-connect is ever wanted, that changes rule 6: ask first.)
- **SSH is off** in the image: the service is not enabled, and **the build's host keys are deleted** (`configure_rootfs.sh`), so each card makes its own
  the first time SSH is started. Starting it needs a network and a way to log in; it stops at reboot or on Stop SSH.
- Only user `loveos` can log in (never root), 3 tries, no forwarding, key or password. The `loveos` account is `nologin` until SSH starts, then
  `/bin/bash`; Stop SSH puts it back. A password is set only through Settings (typed twice); the helper passes it to `chpasswd` on stdin.
  A key login: put the public key in `ssh/authorized_keys` on the games card; it is **read only** and copied into `~loveos/.ssh` when SSH starts.
- The games card is only read (rule 7).

## How it works

The menu runs as the unprivileged `loveos` user, so it cannot touch interfaces or services. Instead:

1. The menu writes `/run/loveos/net/request` (`key=value` lines, written then renamed, so the helper never sees half a file).
2. `r3i6nos-net.path` (`PathExists=` that file, enabled; it only watches) starts `r3i6nos-net.service` (root, oneshot, **not wanted by any target**).
3. `/usr/local/bin/r3i6nos-net` (Python 3 stdlib) reads and **deletes** the request, runs one verb from an allowlist
   (`status scan wired wifi disconnect test forget ssh-status ssh-password ssh-enable ssh-disable`), and writes `/run/r3i6nos-net/result`.
4. The menu polls for the result with its request id and shows the lines; it gives up after a timeout (30 to 75 s by verb) with a reason, and B stops waiting.

Tools used (in the rootfs from `build_rootfs.sh`): `iproute2` (`ip`), `iw`, `wpasupplicant`, `udhcpc` (DHCP), `iputils-ping`, `openssh-server`. Wi-Fi: a
passphrase is turned into a WPA key (PBKDF2, checked against the IEEE test vector) so the plain password is never written; the key goes in a root-only
config in `/run/r3i6nos-net/private` and the saved network in `/var/lib/r3i6nos/wifi.json` (0600). Open and WPA/WPA2 personal networks only; WEP and
enterprise are refused with a reason.

## Threat model: what is and is not defended

The request folder belongs to `loveos`, which also runs every game and app on the card, audited or not. So the helper treats the folder as hostile
(`tests/test_r3i6nos_net.py` tries each):

- The request is opened with `O_NOFOLLOW | O_NONBLOCK` and must be a regular file under 16 KB: a planted symlink is never read, a planted pipe never
  hangs root.
- The answer is written only into a **root-owned** folder, created exclusively, never beside the request: a link planted there cannot make root
  overwrite or re-own another file. (The first draft wrote the answer next to the request and `chown`ed it; that was a privilege-escalation hole and was removed.)
- Only allowlisted verbs run; SSID, key, password, interface and package-like arguments are validated; Wi-Fi names are hex-encoded in the config, so
  nothing in them can inject a setting; nothing from a request is echoed back except through the display filter.

**Not defended:** any process running as `loveos` can still *ask* for any allowlisted verb, including Start SSH (and Set SSH password or a key on the card).
A malicious game on a connected unit could therefore open SSH to itself. The mitigations are that SSH is off after a reboot, nothing is connected unless the
person connected it, and the Instructions tell the person to run only software they trust. A stronger fix (a physical confirmation the game cannot
fake) needs a trusted input path and is not built.

## Not tried

- On the R36S: adapters and drivers, the unit's USB port in host mode, `wpa_supplicant` and `udhcpc` on that userland (does `udhcpc` write
  `/etc/resolv.conf`? its default script is expected to), the path unit under systemd 252 / kernel 4.4, `usermod` and `chpasswd` on the rootfs.
- The rootfs build with the new packages (`build_rootfs.sh` needs Linux and root; CI builds it). `shellcheck` was not installed here.
- A static IP, hidden-network scans, WEP and 802.1X are not offered.
