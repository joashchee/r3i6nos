# Settings > About

What the unit is running, whether it is the newest, and the licences. Code: `menu/about.lua` (the screens' data),
`menu/settings.lua` (`aboutMenu`), `overlay/usr/local/bin/r3i6nos-about` (Python stdlib, runs as `loveos`, no root).
Tests: `tests/test_about.lua`, `tests/test_r3i6nos_about.py` (a fake GitHub on 127.0.0.1), `tests/test_settings.lua`,
`tests/test_menu_view.lua`, `tests/test_configure_rootfs.sh`.

## Which build this is

`scripts/configure_rootfs.sh` writes `/etc/r3i6nos-release` into every image:

| Key | What |
| --- | --- |
| `VERSION` | the repo's `VERSION` file: the release this build belongs to (`0.1.0` until the first release) |
| `COMMIT` | the full commit it was built from (`git rev-parse HEAD`; `R3I6NOS_COMMIT` overrides it) |
| `COMMIT_DATE` | that commit's date |
| `MODIFIED` | `1` when the tree had uncommitted changes to tracked files |
| `BUILT`, `BUILT_BY` | the build's date (UTC) and `github` (Actions) or `local` |
| `REPO` | where to ask about newer builds: `joashchee/r3i6nos` |

Bump `VERSION` in the commit that makes a release, and tag that commit `v<VERSION>`.

## The screens

- **This system** (`r3i6nos-about info`, files only, no network): version, commit and build; board, Debian, kernel,
  uptime; cores and top speed, memory and free memory, temperature, whether the Mali driver from the user's card is
  there; system and games card space and the games card's filesystem; LÖVE runtimes, native app count, tools;
  host name, addresses, SSH on or off. Then what only LÖVE knows (`main.lua` sets `about.extra`): the menu's LÖVE
  version, the screen size, and every pad with its name, **GUID**, buttons, axes and hats (the GUID is what
  `run_app.sh`'s SDL mapping still needs).
- **Check for updates** (`r3i6nos-about check OUT`, in the background; the menu waits up to 45 s, B stops waiting):
  the only network use in About, and only when a person picks it (CLAUDE.md rule 6). Two questions to
  `api.github.com`, sending nothing about the unit (the User-Agent is `r3i6nos-about`):
  1. `releases/latest`: newer, the same, or older than `VERSION` (a newer one says how to update: flash the system card).
  2. `compare/<COMMIT>...main`: how many commits main has that this build lacks. A 404 means the build's commit was
     never pushed (a local build of unpushed work), which is said in those words.
  Unreachable, rate-limited, or a private repository (which answers 404 to both) are each reported as such.
  HTTPS needs `ca-certificates` in the rootfs (`build_rootfs.sh`).
- **Licences**: MIT, the VGA font's CC BY-SA 4.0 credit (as in `NOTICE`), LÖVE's zlib, the privacy rules.

## Untested on the R36S

All of it: the pages on the panel, `ip` and `systemctl` from `loveos`, the check over a USB adapter, the CA bundle,
the pads' GUIDs as LÖVE reads them. The repository is public (confirmed 2026-11-09), so both questions need no
token; made private, the check could only say "no release published yet" and "not on GitHub".
