# Packages over the network: adding and upgrading native `.deb` bundles without taking a card out

Status: **phases 1 and 2 are built (2026-11-17, below, "Built"); the catalog, Settings > Packages and system packages are still a proposal.** None of it has run on a unit. It rests on what works on the unit as of today (the maintainer, 2026-11-17: Wi-Fi and wired network, the data-card format, the text-mode screen mirror and SSH all work). Whatever behaviour on the unit this document describes is untested until a report says otherwise (CLAUDE.md rule 1). Roadmap: section 29.

## The goal

After the first flash, **nothing a person does day to day needs a reflash or a card out of its slot**: a new app, a new version of an app, a fix to the menu or a helper, a new corpus. The card comes out only for what the card holds that the running system cannot change (the table at the end).

## The one idea: a package is a `.deb` in `native_deb/`

Today the card's `native_deb/` folder **is** the list of installed packages: `deb_watcher.py` extracts each `.deb` into `/opt/native_apps/<name>/`, writes a launcher or tool wrappers, and removes what's gone. It runs only at session start and reads only `/roms/native_deb`. That model stays. **Every channel below does the same two things: get a checked `.deb` into a card's `native_deb/`, then run the watcher's code at once.** A computer that copies a file onto the card (the old way) and the unit downloading one end in the same state, so they never disagree.

**No root for app packages (found 2026-11-17).** `loveos-session` runs as `loveos` and runs the watcher before every menu start, and `/opt/native_apps`, `/opt/launchers` and `/opt/loveos/bin` belong to `loveos` (`build_rootfs.sh`); the cards are mounted for `loveos` too. So installing an app is something the login user already does, and **`r3i6nos-pkg` is a login-user tool: no sudo, no path unit, no request protocol, no root helper.** The menu's Settings > Packages (phase 4) will run it as `loveos` directly. A root helper exists only for system packages (phase 5), which touch root-owned files.

What changed in the watcher, now one engine for the session and for `r3i6nos-pkg` (also storage steps 2 and 4, `docs/STORAGE.md`):

1. **Both cards.** `native_deb/` on `/roms` and on `/roms2`, and the built-ins. A package found in more than one place is installed from **the highest Debian version** (the later folder when a version cannot be read, so a card still replaces a built-in of the same version; a stale card copy never shadows a newer built-in after a reflash). The comparison is Debian's own (`vercmp`, checked against `dpkg --compare-versions` on 484 pairs).
2. **Where it came from is written beside it** (`.r3i6nos-src`), and a package is removed only when **that** folder is there to say it is gone: a card that is out never uninstalls anything, whichever card it was.
3. **Refuses what does not fit, before extracting.** Installed-Size (or three times the file when unreadable) against the root filesystem's free space (counting the old copy it replaces), keeping back the larger of 200 MB or 10% (`R3I6NOS_PKG_RESERVE_MB`, `_PCT`). The reason names the numbers and the squashfs phase; the installed version stays as it was.
4. **Extracts into `<name>.new` and swaps by rename**; a leftover `.new` from a power-cut is cleared at the next pass. `dpkg-deb -x` runs as the login user, so what an archive can do is limited to what `loveos` can already do (a setuid bit means nothing for a file `loveos` owns). Not separately attacked in a test.
5. **App data is not in the tree** (`docs/APP-DATA.md`), so an update, a roll back or a removal never touches a person's saves.
6. **The menu sees a change within a second:** `r3i6nos-pkg` leaves `/run/loveos/pkg-changed`; `menu/pkgflag.lua` looks for it about once a second (one failed `open`) and reads the cards again.

Package metadata, one file in the `.deb` (like today's `usr/share/r3i6nos/tool` marker, which it replaces without breaking): `usr/share/r3i6nos/package.json`: `kind` (`app`, `tool`, `system`), `data_dir` (the `<App>` folder name, rule 16), `min_system` (the lowest `VERSION` of the base it runs on), `size_installed`. The Debian `Version` field is the version.

## Three ways in

All three are chosen by a person (rule 6: no telemetry, no network at boot, no background checks, no accounts).

### A. Push from a computer over SSH (first to build; no new menu)

`scripts/push-packages.sh UNIT [file.deb ...]` on the Mac: with no files it takes the newest build of every port (`scripts/ports.conf`, the same list `flash-macos.sh` uses) and of this repo's Commander and IO Checker (`build/native`), shows what it will send and asks once. It then clears `~/inbox` on the unit, `scp`s each file there and runs `ssh unit r3i6nos-pkg install --inbox --yes`. The SSH login is the one Settings > SSH sets up; **there is no `sudo`**. `--list`, `--remove NAME`, `--card data|system` and `-n` (say what would be sent) exist. It replaces `flash-macos.sh`'s copying of ports for a card already in service, and `scripts/build-ports.sh` plus this is the whole development loop.

`r3i6nos-pkg install` checks each file in a private copy it made (what is checked is what is installed): a Debian archive (`dpkg-deb` reads it), architecture arm64 or all, a package file name of letters, digits and `._+-`, **no `nttplay` inside** (rule 15), room on the card (with 5%), and room in the root filesystem for the extracted package. It puts the file in the data card's `native_deb/` (or the system card's when no data card is in; or the card the package is already on, so there is one copy), applies at once, and keeps the old `.deb` as `<name>.deb.prev` until the next update (for the later Roll back); if the new one does not install, or a newer version from elsewhere would stay in use, the old one is put back. Run by hand it asks first (name, version, size, sha256) and **refuses when it has no terminal to ask on unless `--yes`**. A file it refuses stays in the inbox and is named with the reason. A link in the inbox is never followed.

**The on-screen prompt of the first draft is dropped for this route:** the person who ran the push has already said yes at a terminal, and a program running as `loveos` can write the inbox but can also write `native_deb/` and `/opt/native_apps` directly, so a screen prompt defended nothing here. The prompt that matters is for root, in phase 5: a system package is never installed from the inbox at all.

### Built (2026-11-17)

`overlay/usr/local/bin/deb_watcher.py` (the engine), `overlay/usr/local/bin/r3i6nos-pkg` (`install`, `remove`, `list`, `apply`), `menu/pkgflag.lua`, `scripts/push-packages.sh`. Tests: `tests/test_deb_watcher.py` (24, the new ones cover both cards, versions, provenance, size and `.new`), `tests/test_r3i6nos_pkg.py` (29: fake cards and a **real `.deb` built with `dpkg-deb` and really extracted**, which found that two builds of one size in the same second looked identical to the watcher: `r3i6nos-pkg` now drops the old stamp itself), `tests/test_pkgflag.lua`, `tests/test_push_packages.sh` (fake `ssh` and `scp`, under bash 3.2). The real-`.deb` tests need `dpkg-deb` (CI and Debian have it; they were run in Debian 12 arm64 under Docker; a Mac skips them).

### B. Pull on the unit: Settings > Packages

New in the menu, the pad only, never scrolling (rule 5), whole pages:

- **Installed:** name, version, which card, size; **Update** (if the last check found one), **Roll back**, **Move to the other card** (`docs/STORAGE.md` step 5), **Remove** (confirmation starting on No; app data kept, and the dialog says where it is).
- **Check for updates:** downloads the signed catalog (below), compares versions, marks what's newer. The only network use in Settings > Packages, only on that press, sending nothing about the unit (User-Agent `r3i6nos-pkg`, as About's check).
- **Available:** the catalog's packages not installed, with size and a one-line description; **Install** asks for the card only when both are in (the data card is the default, rule 16's reason), with free space shown.
- **Inbox:** what method A left and refused, with the reason.

A download goes to `<card>/native_deb/<name>.deb.part` (resumable with a `Range` request), is checked against the catalog's `sha256` and size, then renamed to `<name>.deb` and applied. A power-cut leaves a `.part` that the next try resumes and a half-written `.deb` never exists under its real name. It refuses while the battery is under 20% and not charging (a system update: under 30%), and shows a progress bar with the step, bytes and time left.

### C. A page on the unit that takes an upload (later, only if A and B are not enough)

The unit starts a one-time upload page on its address (as the screen mirror starts its page: a person chooses it, a token in the address, stops on its own), for someone with a computer and no SSH. Not proposed first: A covers people with SSH, B covers people with a network and no computer, and C adds a listening server.

## The catalog

`index.json` and `index.json.sig`, attached to a GitHub release of this repository (tag `packages`, replaced in place; the repository is public, so the unit needs no token, as About's check). Each entry: `name`, `version`, `kind`, `size`, `sha256`, `url` (the `.deb` as a release asset of that port's own repo, or this one), `min_system`, `description`, `data_dir`.

`scripts/publish-packages.sh` on the maintainer's Mac builds the index from `ports.conf` (it fails if a port's `.deb` is missing or contains nttplay, rule 15), signs it, and uploads with `gh release upload` by hand, as the notarized Mac apps are (the maintainer's rule: no credential in CI, in the repo or in the environment). **The signing key is the maintainer's, offline, on the Mac; the public key is in the image** (`/etc/r3i6nos/packages.pub`). The catalog is not "the store": it only lists what the maintainer chose to publish. A person can still install any `.deb` by method A, with the on-screen prompt.

**The signature tool: `openssl` (decided 2026-11-17, checked in Debian 12 arm64).** It is **already in the image**: `ca-certificates` (in `build_rootfs.sh` for About's HTTPS check) depends on it, so it costs no package and no bytes (OpenSSL 3.0.22). Ed25519 works in it: `openssl pkeyutl -verify -pubin -inkey /etc/r3i6nos/packages.pub -rawin -in index.json -sigfile index.json.sig` passed on a good file and failed (exit 1) on a changed one. The alternatives were not chosen: `signify-openbsd` (221 KB installed, one more package, signs only its own format) and `minisign` (84 KB plus `libsodium23`, and not in the image). OpenSSL also covers future needs with the same binary: other key types, SHA-256/512 for downloads, X.509 if a signed update server is ever wanted, and the `ca-certificates` HTTPS the downloads already use. **Signing on the Mac** uses the same OpenSSL 3.0 in Debian 12 under Docker (the Mac's own LibreSSL has no usable Ed25519 `pkeyutl -rawin`), with the private key on the Mac and mounted read-only into the container, never in the repo or CI. `scripts/publish-packages.sh` signs, then **verifies the signature with the image's public key** before it uploads.

## System updates: the menu, the helpers and the default apps

A `kind: system` package is the part of the image that is **files**: `opt/loveos/menu`, `opt/loveos/theme`, `opt/loveos/defaults`, `usr/local/bin`, `etc/systemd/system`, `usr/share/r3i6nos`, `etc/asound.conf`, `etc/r3i6nos-release`. Everything else is refused. Because root applies it:

- **It must carry a valid signature**, from the catalog entry (`index.json.sig` covers its `sha256`), **and only from the catalog**: no inbox install, no push, no unsigned file, whatever the prompt says. This is the one place a program running as `loveos` could get root, so it is closed (rules 6, 7).
- It backs up every file it replaces to `/var/lib/r3i6nos/rollback/<from>-<to>/`, renames each new file into place, runs `systemctl daemon-reload`, restarts the menu (the session exits and starts again), and writes `etc/r3i6nos-release` last (About then shows the new version and commit).
- **Rollback by itself.** It leaves `/var/lib/r3i6nos/system-pending` with the old and new version. The menu, once it has drawn its first screen, removes it. If the file is still there at the next two boots (`r3i6nos-bootstate` counts them), the files come back from the rollback folder before the session starts. A bad menu cannot leave a unit that needs a card out.
- The default apps (IO Checker, Commander) need no system package: a `.deb` of the same name on a card already replaces the built-in (`deb_watcher.py`), so they update as apps.

A new Debian package the image lacks (the FAT32 format needs `mkfs.fat`, `dosfstools`) is a system package of its own files, if it is self-contained; if it is not, it stays a reflash item and the roadmap says so.

## What still needs a reflash or a card out

| What | Why | Then |
| --- | --- | --- |
| The kernel, its modules, the device tree, `boot.ini`, the bootloader, the boot logo | They live on BOOT and the card's own partitions, read before Linux runs; `libmali` and the modules come from the user's own card (rule 2) | Once at the first flash. A change to any of them is a reflash (`inject-boot.sh`), say so in the roadmap item that needs it |
| The partition layout and the filesystem formats | The setup service and Settings > Data card own them (rule 7) | Never, but a changed layout is a reflash |
| ROOTFS's size (about 2.3 GB) | A package that does not fit is refused, not squeezed in | **Refused** until the squashfs-on-card phase (decided 2026-11-17): `r3i6nos-pkg` checks the installed size (`size_installed`) against ROOTFS's free space minus a reserve (the larger of 200 MB or 10% of ROOTFS) **before it downloads or extracts**, and says "Too big for the system card's root; needs the squashfs phase" with the numbers. The catalog marks such a package `too_big` for the unit's free space; nothing is half-installed. Phase 6 then keeps them as squashfs images on the data card (`docs/STORAGE.md` step 4) |
| A Debian package with libraries the image lacks | Only self-contained packages (bundled libraries, as `deb_watcher.py`'s launcher sets `LD_LIBRARY_PATH`) install | Bundle the libraries into the app's `.deb` |

## What it costs

- **Rootfs:** nothing new for A and B except a signature tool (about 100 KB) and `r3i6nos-pkg` (Python stdlib); `squashfs-tools` (about 1 MB) only with the later phase.
- **Battery and boot** (rule 14): nothing runs at boot but the existing `apply`, which does nothing when nothing changed. A check and a download run only on a press. Settings > Packages gets a `== power ==` row in the system report (idle: no process, no timer; downloading: the CPU and the draw during a fetch of a known size).
- **The report** (rule 9): `r3i6nos-report` gets a `== packages ==` section: what's installed, from which card, version, the last `apply`'s result, the free space on ROOTFS, and the signature check against a known good and a known bad file.

## Build order (ROADMAP section 29)

1. **Done 2026-11-17:** `r3i6nos-pkg` with `apply`, `install`, `remove`, `list`, reading both cards, applying without a restart; tests with fakes and a real `.deb`.
2. **Done 2026-11-17:** method A, `scripts/push-packages.sh`.
3. The catalog: `scripts/publish-packages.sh`, the signature tool, the public key in the image.
4. Settings > Packages (Installed, Check, Available, Inbox) and its report section.
5. System packages with rollback.
6. Roll back and Move for apps; squashfs for big packages; method C, only if asked.

Untested on the unit: all of it. The questions only a unit answers: whether `r3i6nos-pkg` finds `python3`, `dpkg-deb` and the cards file on the unit's PATH over an SSH login, that the menu really lists a pushed app within a second, how long the extraction of a large package takes on the SD card, and whether a download over the Realtek dongle holds for a 30 MB package.
