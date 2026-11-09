# The clock

The R36S boots with no idea of the time: the board has no battery-backed clock that this kernel reads at boot, and r3i6nos never goes on
the network by itself (CLAUDE.md rule 6), so it cannot ask a time server. Every boot used to start from an old time, so the device reports'
`made:` lines and the battery log's clock drifted. The fix has three parts.

## 1. The menu asks, once a card

On a card's **first boot** the menu opens **Set the date and time** before the game list: the year, month, day, hour, minute and time
zone on one row. Left/Right choose a field, Up/Down change it (wrapping; the day follows the month), L1/R1 change it faster, **A sets it**,
**B is "Not now"**. Either answer is remembered and it is not asked again on that card; **Settings > Date and time** opens the same dialog
any time (there B just goes back).

The time zone is whole hours from UTC (UTC-12 to UTC+14), set as the matching `Etc/GMT` zone (`Etc/GMT-8` is UTC+8: those zones count the
other way), so local times in reports and the battery log are right. Half-hour zones are not offered.

- Code: `menu/clock.lua` (the fields, the first-boot check, the request), `menu/settings.lua` (the `clock` node), `menu/view.lua` (drawn).
- "Asked": the helper's mark `/var/lib/r3i6nos/clock-set` (set) or the menu's `~/.r3i6nos-clock-asked` (Not now).

## 2. A root helper sets it

The menu runs as `loveos` and cannot set the clock. It writes `/run/loveos/clock/request` (`id=`, `set=YYYY-MM-DD HH:MM`, `zone=+8`);
`r3i6nos-clock-set.path` starts `r3i6nos-clock apply` as root, which deletes the request, checks every field, sets the zone
(`/etc/localtime`, `/etc/timezone`), the clock, and the **RTC** (`/dev/rtc0`, when the kernel has one), saves the time, writes the
"set" mark and answers in `/run/r3i6nos-clock/result` (only root writes there). The request is treated as hostile, as `r3i6nos-net`'s
is: a link is refused and deleted, a big or odd file is refused, nothing outside the checked fields is used.

## 3. The time is kept across restarts

- `r3i6nos-clock.service` (early, before anything stamps a time): **restore** brings the clock *forward* to the newest of the time
  saved on the card, the RTC, and the image's build date; it never moves it back. At shutdown it **saves**.
- `r3i6nos-clock-save.timer`: saves every hour, so a flat battery or a crash loses at most an hour of saved time.

**What it cannot do:** time spent switched off is not counted unless the RTC keeps running. If the RK817's RTC holds its time while the
unit is off (likely while the battery has charge, but **untested**), the clock is right after every boot; if not, it restarts from the
last saved time and runs behind by however long the unit was off. Set it again in Settings > Date and time when that matters.

## Checking it on a unit

`r3i6nos-clock status` (over SSH, and in r3i6nos's device report, section `clock`, and the boot log): the time and zone now, whether
a person set it and when, the saved time, the RTC (its name and how far it is from the system clock), and what restore did this boot.
A unit's report answers the open questions: whether `/sys/class/rtc/rtc0` exists, and whether it kept time while the unit was off.

**First answer (2026-11-09 22:30, image `5f4d67c`, r3i6nos's report):** the kernel has an RTC, **`rk808-rtc`** (the RK817's), and after the time
was set in the dialog at 22:13 it read 1 s from the system clock, so setting it reaches the RTC. That boot restore left the clock as it was
("the system clock, not moved"). **Still open:** whether the RTC keeps time while the unit is off; power off for an hour, boot, and read the
report's `clock` section before setting anything.

**Not run on a real R36S.** Tested here: the helper against temporary folders with the clock-setting call replaced
(`tests/test_r3i6nos_clock.py`), the dialog and Settings against a fake helper (`tests/test_clock.lua`), the units installed and
enabled (`tests/test_configure_rootfs.sh`), the report's section (`tests/test_r3i6nos_report.py`).
