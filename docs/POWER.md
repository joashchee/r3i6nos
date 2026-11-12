# Power: what the background work costs the battery

Status (2026-11-12): the measuring is written and tested on a laptop (fake `/proc` and `/sys`, and a real Debian 12 `/proc` under Docker), and **ran once on a real unit**
(100% charge, one 30 s window, no root: no stop-and-compare). That run: idle draw 1461 mW (8.9 h left), hot-plug daemon 1.63% of a core and 5.1 wake-ups/s at the old
0.5 s poll (FAIL both), jobs 0.131% (FAIL), and **the menu 60% of a core**, the biggest background cost by far (LÖVE redrew at 60 fps with nothing moving). Changed from it:
the daemon polls every 1 s, the battery tick runs every 900 s, bootstate every 15 min, the menu draws only when something changed (`menu/redraw.lua`: an input, a toast, the battery read; 30 fps only while waiting for a helper or on the mixer) and otherwise sleeps 50 ms a loop, so a press waits at most 50 ms. **Second run (2026-11-12, 69% charge, build 87df064, with a USB hub, keyboard, mouse and card reader plugged in): 14 PASS, 2 FAIL.** Idle draw 2963 mW (3.0 h left; the first run's 1461 mW had nothing on the OTG port, so the two are not comparable: a powered hub and its devices are most of the difference, unproven). Hot-plug daemon 2.10% CPU (FAIL, goal 1%), 2.7 wake-ups/s (PASS). Jobs summed 0.1356% (FAIL) because the report counted 12 runs an hour each where the tick runs 4 (900 s) and bootstate 4 (15 min): corrected, 0.045%. The menu read 34.8% because the menu was showing the report's own wait screen at 30 fps: waiting screens now draw 4 a second. Fixed from it: the daemon caches each USB device's name and kind (one read a device a poll), the wait screen's rate, the job rates. **None of it re-measured yet**; the bare idle menu has never been measured (read it over SSH with no dialog open). The report is how it gets measured: Settings > Device reports > r3i6nos (this
system), or `r3i6nos-report`, now has a `== power ==` section.

**Every port, not only this system (CLAUDE.md rule 14):** each R36S port's device report has a `== power ==` section built the same way (its own CPU, wake-ups and memory idle and working, the draw in mW on battery, goals, what to cut first); this file's grammar and goals are the reference, and `power_section` in `r3i6nos-report` the code. Shared code for native, tool and LÖVE ports: ROADMAP section 23.

## In a command-line tool (`tools/qjs-node/report.py`, tested by `tests/test_tool_report.py`)

Every tool's `<command>-report` (generated from `report.py` by `package-tool.sh`, so every tool gets it after a rebuild) writes a `== power ==` section after
its `== commands ==`. **A tool has no idle state** (it is not running between commands), so only the working one is measured, from the commands the report just ran:

| Line | What it says |
| --- | --- |
| `battery supply`, `draw` | the charge and status; **SKIP "unplug"** unless the battery says `Discharging` |
| `idle state` | INFO: there is none; the working state only |
| `working window`, `working cpu` | commands run, seconds running, CPU seconds, % of a core (a tool is one thread) |
| `command <line>` | for each command of **1 s or more** (a shorter one is its own start-up): wall, CPU seconds, % of a core, **wake-ups a second** (the child's own context switches from `wait4`), peak KB, and its draw |
| `working wakeups` | **PASS or FAIL**: the worst of those commands against **at most 200 a second** (a first guess, set before any measurement): more means it polls or sleeps in a loop |
| `working system busy`, `working busiest other <name>` | the system's busy %, interrupts and context switches a second over the whole run, and the three busiest other processes (the report's own wait loop is in the first) |
| `working draw`, `idle draw`, `working energy`, `working time left` | the battery's draw in mW **read once a second during commands of 1 s or more**, a 5 s idle baseline taken first (`R3I6NOS_REPORT_POWER_S`), the difference as mW and as **J over idle**, and the hours left at that draw |
| `cut first` | the command with the most CPU seconds (a tool's energy is its CPU time), or the one that wakes too often: make it faster, or make its output ahead of time |

Nothing is read but `/proc`, `/sys` and the child's `rusage` (`R3I6NOS_PROC`, `R3I6NOS_SYS` name other roots). A command under a second has no battery reading
(the gauge changes about once a second), so a tool whose commands are all short gets CPU time and wake-ups only, and `SKIP working wakeups`.
**Tested** with a fake tool, a fake battery (discharging and charging) and no battery (`tests/test_tool_report.py`) on a Mac and in Debian 12. **Untested on the
unit**; the first run there sets the real wake-up goal. Composer's `make ambient` (16 s) and Painter's `draw character` and `convert` (13-18 s) are the commands that will show a figure.

## In a native port (`theme/r3i6nos_report.h`, tested by `tests/test_report.c`)

```c
r3_report_power_section(&r);                                    // == power ==, the battery's state (SKIP and "unplug" when it is not Discharging)
R3PowerResult idle, work;
r3_report_power_idle(&r, 30, tick, arg, &idle);                 // the app open and waiting; goals R3_POWER_IDLE_CPU (2%) and _WAKE (25 a second)
R3Power p; r3_power_begin(&p);                                  // the working state: Rec demo's tour runs, and the code calls r3_power_sample(&p)
/* ... the tour, calling r3_power_sample(&p) about each second ... */  // about once a second (a sooner call is ignored)
r3_report_power_end(&r, &p, "working", 40.0, 120.0, &work);    // CPU % of a core and wake-ups a second the app may use; 0 or less: no goal, INFO
r3_report_power_advice(&r, &idle, &work);                       // the last line, "cut first": what to fix
```

Each window writes, under its state (`idle`, `working`): `window`, `cpu` and `wakeups` (PASS or FAIL against the goals), `busiest thread`, `memory`,
`system busy`, the three `busiest other` processes, `draw` (mW mean, min, max, readings) and `time left`. It reads `/proc` and `/sys` only
(`R3I6NOS_PROC`, `R3I6NOS_SYS` name other roots). Wake-ups are the context switches of this process's threads. **`tick`** is one turn of the app's own
idle loop and must block (`SDL_WaitEventTimeout` up to a second); with `NULL` the report sleeps a second at a time, which counts as about **1 wake-up a
second** in the idle figure. A `tick` that returns at once is a busy loop, and the numbers say so (that is the point). Tested against a fake `/proc`
and `/sys` with a fake clock (exact figures), against Debian 12's real `/proc` under GCC with `-Werror`, and with AddressSanitizer and UBSan.
**Callers (2026-11-12):** the native IO Checker (`power_windows` in `apps/io-checker-native/io_checker.c`: idle = the list as its main loop draws it, working = the Motion pattern, goals 2% / 25 a second and 40% / 180 a second) and Commander (`commander --report`: idle = its real loop, working = identifying the test corpus, goal 20 files a second). Both are tested by `--selftest` (a windowed report with 0.2 s windows must contain the section). **Neither has run on a unit**, and the battery lines are only tested against fakes.

**First findings, on a laptop's Debian 12 with SDL's dummy video driver (not a unit):**
- **The IO Checker's idle was 4.7% of a core and 104 wake-ups a second** (FAIL against 2% and 25): its main loop composed a frame and slept 16 ms, 60 times a second. **Fixed the same day:** it sleeps 100 ms a pass on a quiet page and 16 ms only while something changes (a screen still being presented, a toast, the exit chord, a job about to run) or on a page where a person is pressing things (the button test, the live USB checks); and it **closes the sound output when a tone has played out**: it stayed open, and SDL's audio thread woke about 40 times a second feeding it silence (and on the unit would keep the amplifier powered). Now **0.00% and 18 wake-ups a second**, six runs out of six; one run in seven read 3.65% once, not repeated. Not measured on a unit.
- **Commander's idle is 1% of a core but 747 wake-ups a second**, although it waits with `SDL_WaitEventTimeout(250)`: SDL2 falls back to a 1 ms `SDL_Delay` polling loop on a video driver with no native wait, and the dummy one has none. Whether KMSDRM on the unit does the same is for the unit's report to say. The IO Checker's loop therefore uses `SDL_PollEvent` and `SDL_Delay`, which is the same on every driver. **Commander is not changed yet:** if the unit shows hundreds of wake-ups a second, give it the same loop.

## What runs in the background

| Work | How often | Where | Cost to measure |
| --- | --- | --- | --- |
| `r3i6nos-hotplug` daemon (jack, USB, DC, cards, battery, Wi-Fi, kernel log) | polls every 1 s (`R3I6NOS_HOTPLUG_POLL`; every 0.25 s while a change or a volume press is pending), always | root, Python | CPU %, wake-ups a second, resident kB, one look timed, the draw with it stopped |
| the toast poll | each frame reads `/run/r3i6nos-hotplug/events` at most every `LOOK` (`theme/r3i6nos_toast.{h,lua}`) | every app and the menu | inside the menu's CPU % (the report measures the menu process; the toast's state change redraws) |
| the menu's battery read | every 10 s (`POWER_EVERY`, `menu/main.lua`) | menu | inside the menu's CPU % |
| `r3i6nos-battery sample tick` | every 900 s while an app runs (`loveos-session`) | loveos, Python | CPU per run, runs an hour = share of a core |
| `r3i6nos-bootstate.timer` | a minute after boot, then every 15 min | root, bash + `systemctl` | the same (as the loveos user `systemctl` fails fast: a lower bound) |
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
| hot-plug daemon wake-ups | at most 4 a second (its poll is once a second) |
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
