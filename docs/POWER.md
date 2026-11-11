# Power: what the background work costs the battery

Status (2026-11-11): the measuring is written and tested on a laptop (fake `/proc` and `/sys`, and a real Debian 12 `/proc` under Docker). **Nothing here
has run on a real R36S**: no number below has been measured on the unit. The report is how it gets measured: Settings > Device reports > r3i6nos (this
system), or `r3i6nos-report`, now has a `== power ==` section.

## What runs in the background

| Work | How often | Where | Cost to measure |
| --- | --- | --- | --- |
| `r3i6nos-hotplug` daemon (jack, USB, DC, cards, battery, Wi-Fi, kernel log) | polls every 0.5 s (`R3I6NOS_HOTPLUG_POLL`), always | root, Python | CPU %, wake-ups a second, resident kB, one look timed, the draw with it stopped |
| the toast poll | each frame reads `/run/r3i6nos-hotplug/events` at most every `LOOK` (`theme/r3i6nos_toast.{h,lua}`) | every app and the menu | inside the menu's CPU % (the report measures the menu process) |
| the menu's battery read | every 10 s (`POWER_EVERY`, `menu/main.lua`) | menu | inside the menu's CPU % |
| `r3i6nos-battery sample tick` | every 300 s while an app runs (`loveos-session`) | loveos, Python | CPU per run, runs an hour = share of a core |
| `r3i6nos-bootstate.timer` | a minute after boot, then every 5 min | root, bash + `systemctl` | the same (as the loveos user `systemctl` fails fast: a lower bound) |
| `r3i6nos-clock-save.timer` | every hour | root, writes the card | not run by the report: one small file an hour |
| Rec demo status files, report progress files | only while one runs | | not background work: not measured |

## What the report measures (`power_section` in `overlay/usr/local/bin/r3i6nos-report`)

One idle window (`R3I6NOS_REPORT_POWER_S`, 30 s, the menu showing, nothing pressed), in which it reads, from `/proc` and `/sys` only:

- CPU ticks and context switches (wake-ups) of the hot-plug daemon and of the menu, and of every process (the busiest five are listed, so a surprise shows);
- the system's busy %, interrupts and context switches a second;
- the battery's `current_now` x `voltage_now` once a second, as mW, **only while it says `Discharging`** (on the charger it says SKIP and to unplug);
- the time left at that draw, from `charge_full` and the charge.

Then it costs each periodic job (three runs, `getrusage` of the child): CPU and wall time a run, runs an hour, share of a core. The battery job runs with a
throwaway `R3I6NOS_BATTERY_DIR` and bootstate with a throwaway `R3I6NOS_BOOTSTATE`, so a report never adds to the real history.

**The stop-and-compare (root only):** with the battery discharging and the report started as root (`sudo r3i6nos-report` over SSH; Settings starts it as `loveos`,
which cannot signal a root service, and then says SKIP), the daemon is stopped (`SIGSTOP`), the draw is read for a window, and the daemon is continued
(`SIGCONT`, in a `finally`, so a crash cannot leave it frozen), then a second running window gives the **noise**: how much two windows with the same
daemon differ. The daemon's cost is the difference less the noise.

## Goals (a missed one is a FAIL line)

| Check | Goal |
| --- | --- |
| hot-plug daemon CPU | at most 1% of a core |
| hot-plug daemon wake-ups | at most 4 a second (its poll is 2 a second) |
| periodic jobs together | at most 0.05% of a core averaged |
| hot-plug daemon draw | at most 20 mW beyond the noise |

The goals are first guesses, set before any measurement; change them from what the unit says. The menu's CPU has no goal yet (INFO): there is no baseline.

## Reading a report, and what to do about a FAIL

- Daemon over its goals: lengthen the poll (`R3I6NOS_HOTPLUG_POLL=1` in the unit's environment, a toast then takes up to a second longer), or replace the
  poll of the USB and power directories with `inotify`/uevent (`/proc/net/netlink`) and keep the poll only for what has no event (the jack's switch, Wi-Fi).
- A one-window number is a short test. Repeat at 100%, 50% and 20% charge and on a warm unit, and with the screen's brightness the same, before trusting a small difference.
- `current_now` not reported: only CPU time and wake-ups are measured, and a CPU figure is the only evidence.

## Not measured

- The screen, the audio codec and the CPU's frequency steps: they are the bulk of the draw, and the report only shows the idle total, not a split.
- An app that is running (the menu is replaced by it): the report compares the background work against the idle menu. An app's own toast poll is one file read every `LOOK`; its cost is in that app's report.
- The `r3i6nos-clock-save` write, and the real cost of `systemctl` in the bootstate job (it needs root).
