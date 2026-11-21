# Saved settings on the data card (2026-11-19)

Status: **built and tested against fakes (`tests/test_r3i6nos_persist.py`, 23 tests; the report's section, 8; the menu's Status screen, `tests/test_datacard.lua`); never run on a real R36S.**
Maintainer's request: when a data card is detected, the menu's saved data goes there, so settings, options and the Wi-Fi network persist.

## Why

The system card (TF1) is the one a reflash wipes and the one that should almost never come out (`docs/STORAGE.md`). What a person set in the menu (their Wi-Fi network, their time zone, the clock,
the volume) lived only on it, so every reflash or new system card started from nothing. Now the data card holds them too, and a fresh system card gets them back at its first boot, before the menu starts.

## What is kept, and what is not

An explicit list (`r3i6nos-persist list`); nothing else is ever read or written.

| Item | On the unit | On the card (`<data card>/r3i6nos/settings/`) | After a restore |
|---|---|---|---|
| `wifi` | `/var/lib/r3i6nos/wifi.json` (the saved network and its key: see below) | `wifi.json` | nothing more: the menu reads it |
| `timezone` | `/etc/timezone` | `timezone` | `/etc/localtime` is pointed at the zone |
| `clock-set` | `/var/lib/r3i6nos/clock-set` (a person set the clock) | `clock-set` | the menu stops asking for the date |
| `clock` | `/var/lib/r3i6nos/clock` (the last saved time) | `clock` | `r3i6nos-clock restore` runs again (it only moves the clock forward) |
| `mixer` | `/var/lib/alsa/asound.state` (Volume, Boost) | `mixer.state` | the sound service restores it at its start |
| `battery` | `/var/lib/r3i6nos-battery/history.tsv` | `battery-history.tsv` | Settings > Battery shows it |
| `clock-asked` | `~/.r3i6nos-clock-asked` | `clock-asked` | |

**Not kept, on purpose:**
- **The SSH password and the host keys.** A card cannot keep a key private (below), and a new system card should make its own host keys (the image has none shared). SSH is off at every boot, and the
  public keys in `ssh/authorized_keys` are on the games card already.
- **Each app's data.** Rule 16 has its own way (`docs/APP-DATA.md`): the data card first, with offers to move and merge.
- **The system's package and update state** (`/var/lib/r3i6nos/system-packages` and the rest of that folder): it describes the system card's own build, and restoring it onto a new one would lie.
- **The games and the packages**: on the cards already.

## How it works

`overlay/usr/local/bin/r3i6nos-persist` (Python standard library, root). **The card is the home of the settings and the unit keeps a working copy**, so with the card out it still boots, joins its network and plays
sound; the working copies are not deleted (rule 16's "move" deletes the source, but the clock, the sound and the network start before any card is open, so they need a local file).

It uses `r3i6nos-appdata`'s own idea of a valid data card (mounted by role, exFAT, FAT or ext4, writable, 64 MiB free), loaded from that file so there is one definition; with no usable card it changes nothing and says why.

Per item, comparing the unit's file, the card's file and **`base`**, the content both had at the last sync (kept in `/var/lib/r3i6nos-persist/synced.json`, for each card's id, so a reflash forgets it):

| | |
|---|---|
| only the unit has it | **save** it to the card |
| only the card has it (a fresh flash) | **restore** it |
| the same | nothing |
| the unit deleted it since the last sync (Forget in Settings > Network, Clear the battery history) | **delete the card's copy too** |
| one side changed since the last sync | the other side takes it |
| both changed | the **newer file wins and the other is kept** in `r3i6nos/settings/.merge-conflicts/`, never deleted |
| no last sync to go by (a card this unit has not met) and they differ | **the card wins**, and the unit's copy goes to `.merge-conflicts/` |

**Everything that comes off the card is checked first**: a time zone must be a zone in `/usr/share/zoneinfo`, the Wi-Fi file must hold a name (1-32 characters) and a key, the clock a number, the mixer state must look like one.
A bad file is left alone and reported. Files are written to a temporary name, synced, compared by SHA-256 and renamed into place. A symbolic link or a file over 8 MiB is never read or written. Nothing outside
`r3i6nos/settings/` on the card is touched.

### When it runs

- **At boot**, after the cards are mounted and before the sound service and the menu (`r3i6nos-persist.service`), so a reflashed unit already has its Wi-Fi, its zone and its volume at the first screen.
- **At shutdown** (the same unit's `ExecStop`): the working copies go to the card; the sound service has saved its levels by then.
- **When a change is made** (`r3i6nos-persist.path`): the Wi-Fi network, the clock's mark and the time zone change, or the card state (`/run/r3i6nos/cards`) is rewritten because a card was mounted or formatted.
- **Not on every change**: the mixer, the clock's hourly save and the battery history change all the time, so they are saved at shutdown and at the next boot (a crash can lose the last session's changes to those three).

`r3i6nos-persist status [--items]` shows what the last sync did and how each item compares now; `forget ITEM` takes an item off the card and keeps it off (`keep ITEM` undoes that). The status goes to
`/run/r3i6nos/persist`, which Settings > Data card > Status and the device report's `== saved settings ==` read.

## What is stored for Wi-Fi, and who can read it

**A restored network is confirmed before it is joined** (2026-11-21): when this boot restored the Wi-Fi item (`RESTORED` has `wifi`), Settings > Network > Wi-Fi: join saved first asks "Join <name>? It was restored from the data card." (default No, once per menu session), so a network planted on the card is seen. Untested on the unit.

**The password itself is never written**: `r3i6nos-net` turns it into the 256-bit WPA key (PBKDF2, 4096 rounds, the network's name as the salt) and stores that, as `wpa_supplicant` wants it (`docs/NETWORK.md`).
That key **cannot be turned back into the password** (a weak password can still be guessed from it offline, as from a captured handshake), but **it lets anyone join that network**, and on a WPA2 network read the traffic of
anyone who joins it. Do not treat it as harmless.

On an **ext4** data card the file is root-only (mode 0600). **exFAT and FAT have no file permissions**, so anyone who holds the card, **and any game or port the unit runs** (they run as `loveos` and read `/roms2`), can read
`wifi.json`. The Status screen says so in words, and the device report says which it is. To keep the key off the card: `r3i6nos-persist forget wifi` over SSH (it deletes the card's copy and stops syncing it; the unit keeps its
own network). The cost of that is a reflash then forgets the network. **Hardening this properly (a unit-bound key and a check that the card's data is this unit's) is proposed in ROADMAP section 37, step 6.**

## Hardening, planned (accepted 2026-11-19; ROADMAP section 38; nothing of it is built)

What is on a card today is only as private as the card. The threats, in order of how likely they are:

| Who | What they get today | Fixed by |
|---|---|---|
| Anyone who holds the data card (lost, stolen, shared, read on a computer) | the Wi-Fi key, on exFAT or FAT | encrypting it with a key that is on neither card |
| **A game or port on the unit** (runs as `loveos`, reads `/roms2`) | the same | the same: it cannot derive the key |
| **A card from elsewhere** (this unit has not met it, so it wins) | it can plant a Wi-Fi network the unit later joins, or a time far ahead | authenticating every item with the unit's key; only an authenticated copy can win |
| Someone with the whole unit | the Wi-Fi anyway: they can use it | nothing (an optional PIN makes them work for it) |

**Why not just a PIN.** Whoever holds the card can try every PIN offline with no limit on guesses: a 4-digit PIN is 10,000 candidates and a 6-digit one a million; scrypt at a fraction of a second a guess on a PC makes that
minutes to about a day for 6 digits, and the unit cannot afford a much slower unlock. A PIN stops casual snooping, not a determined attacker with the card. Needing it every boot would also be friction for little gain (the Wi-Fi is not used at boot).

**The plan: a unit-bound key, no PIN needed.** A secret that is on neither card and that only root can read, ideally the SoC's per-unit id (the one-time-programmable block is in the stock device tree and its driver ships; whether the unit lets root read it,
and nobody else, is **to be checked on the unit first**), feeds an HKDF; the card holds only authenticated files (the Wi-Fi key also encrypted), made with `openssl` and Python's standard library. It survives a reflash of the same unit and is useless on
another. **An optional PIN or passphrase** (off by default, scrypt, mixed into the same key, asked once per boot when a saved network is joined) only matters to someone who has taken the whole unit, and the dialog will say so. Smaller fixes: a confirmation
naming the network before the first join of one that came from the card, no hash of a secret item in the report, and a menu switch to keep the key off the card. If the unit has no usable id, a passphrase is **required**, or the Wi-Fi key stays off the card; it is never
silently in the clear on a card without file permissions. The steps, the tests and the decisions that are the maintainer's are in ROADMAP section 38.

## Not done, and what each needs

| | |
|---|---|
| **Everything on a real R36S** | a reflash with the card in (does the first boot know its Wi-Fi, zone and volume?), the path unit firing on a Forget and on a new network, exFAT behaviour for the temporary-name rename and for `chmod`, the clock restore after the card is opened, the card pulled while a sync runs: **blocked on the maintainer's unit** (the checks are in the ROADMAP section 37) |
| **A menu switch for the Wi-Fi key** | today it is the command above, over SSH |
| **Hot insertion** | a card mounted while the unit runs is picked up when `/run/r3i6nos/cards` changes (a format does that); the slot's own insert event (storage step 5) is not built |
| **Other menu data** | the menu has no other saved preferences today; if one is added it joins the list in `r3i6nos-persist` |
