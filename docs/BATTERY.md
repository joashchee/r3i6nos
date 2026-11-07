# Battery (the launcher's indicator and Settings > Battery)

Status (2026-11-08): written and tested on a laptop with a fake `/sys/class/power_supply`. **Nothing here has run on a real R36S**: which supply the
unit's fuel gauge shows up as, and which of the values below it reports (voltage, temperature, full and design capacity, cycle count) are all unknown.
Every figure that depends on them says "not reported" when the battery does not give it.

## What the person sees

- **Top right of the launcher:** `BAT █████ 87%` (five cells, one per 20 %). While charging it is `CHG ...` in green, when full `FULL ...`. Discharging at 15 % or
  less it turns white on red. It is read again every 10 seconds. A unit with no battery shows nothing.
- **Settings > Battery:**
  - **Status:** charge and state, voltage, temperature, health (the battery's full capacity as a share of its design capacity), charge cycles, the average drain
    in % an hour, the time left at that rate, how many samples and starts are recorded, and how many starts ended without a shutdown (the last one's charge,
    and "the battery probably ran flat" when it was 15 % or less).
  - **Use by app:** time on battery, percent used and % an hour for each app and for the menu, the most used first.
  - **History:** the newest 60 samples (uptime, event, app, charge, state).
  - **Clear history:** asks with No first; deletes only the recorded samples.

## What is recorded, and when

`overlay/usr/local/bin/r3i6nos-battery sample EVENT [DETAIL]` appends one line to `/var/lib/r3i6nos-battery/history.tsv`, called by `loveos-session`:

| Event | When |
| --- | --- |
| `session` | the session starts: boot (and again if the session restarts after a failure) |
| `app-start`, `app-end` | around every app, named by its file name |
| `tick` | every 300 s while an app runs (`R3I6NOS_BATTERY_INTERVAL`) |
| `shutdown` | just before Settings > Shutdown powers off |

Each line holds the boot id, the uptime, the clock, the event, the app, the charge, the state, millivolts, the full and design capacity, the temperature and the cycle
count, with `-` for what the battery does not report. The clock can be wrong (no battery-backed clock), so everything is ordered by boot and uptime. The log keeps
about the newest 1500 lines (trimmed in place when it passes 2000), so it stays small on the SD card; samples are a few lines an hour of play.

The state folder belongs to user `loveos` (`StateDirectory=r3i6nos-battery` in `loveos.service`), so no root helper is needed and nothing is written to the games card
(CLAUDE.md rule 7). Nothing is sent anywhere (rule 6): the history never leaves the unit.

## How the figures are worked out

- **Drain** counts only intervals between two samples of the same boot where both are `Discharging`, time passed and the charge did not rise. The interval belongs to the
  app that was running after its first sample (`app-start`, `tick`), else to `(menu)`. A rate needs at least 10 minutes of such time, because the charge is a whole percent.
- **Time left** is the charge divided by the overall drain, shown only while discharging. It is a rough guess: a heavy game drains faster than the menu.
- **Ended abruptly:** a start whose last sample is not `shutdown` (a flat battery, the power switch or a crash). The running start is not counted.
- **Health** is `charge_full / charge_full_design` (or the `energy_` pair), when both are reported.

## Not done

- A "battery low" warning inside a running app (apps own the screen). The indicator is only in the launcher.
- Charge-cycle counting from the history when the gauge reports none.
- Graphs; the pages are text, as everywhere in the menu.
