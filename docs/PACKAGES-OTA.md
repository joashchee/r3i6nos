# Packages over the network: adding and upgrading native `.deb` bundles without taking a card out

Status: **proposal, 2026-11-17** (maintainer's request). Nothing here is built. It rests on what works on the unit as of today (the maintainer, 2026-11-17: Wi-Fi and wired network, the data-card format, the text-mode screen mirror and SSH all work). Whatever behaviour on the unit this document describes is untested until a report says otherwise (CLAUDE.md rule 1). Roadmap: section 29.

## The goal

After the first flash, **nothing a person does day to day needs a reflash or a card out of its slot**: a new app, a new version of an app, a fix to the menu or a helper, a new corpus. The card comes out only for what the card holds that the running system cannot change (the table at the end).

## The one idea: a package is a `.deb` in `native_deb/`

Today the card's `native_deb/` folder **is** the list of installed packages: `deb_watcher.py` extracts each `.deb` into `/opt/native_apps/<name>/`, writes a launcher or tool wrappers, and removes what's gone. It runs only at session start and reads only `/roms/native_deb`. That model stays. **Every channel below does the same two things: get a checked `.deb` into a card's `native_deb/`, then ask the root helper to apply.** A computer that copies a file onto the card (the old way) and the unit downloading one end in the same state, so they never disagree.

Changes the watcher needs (they are also storage steps 2 and 4, `docs/STORAGE.md`):

1. **Becomes a root helper with a request protocol**, `r3i6nos-pkg` (Python stdlib, as `r3i6nos-net` and `r3i6nos-format`: the menu writes `/run/loveos/pkg/request`, `r3i6nos-pkg.path` starts a oneshot root service, the answer is a root-owned `/run/r3i6nos-pkg/result`). Verbs from an allowlist: `list`, `apply`, `install FILE`, `remove NAME`, `rollback NAME`, `move NAME card`, `fetch ID`. `deb_watcher.py` stays as the boot-time `apply`, so a boot with nothing new costs what it does now.
2. **Reads both cards.** `native_deb/` on `/roms` and on `/roms2`; the data card's wins on the same name when its version is newer, else the higher version wins. The card's identity comes from `/run/r3i6nos/cards`, so "card out" is never mistaken for "package removed" (the existing rule: a package is removed only when its card's folder is there to say so; that now means the card's mount **and** its `.r3i6nos-card` marker, `docs/APP-DATA.md`).
3. **Applies at once, no restart.** It writes the launcher; the menu's list already re-reads when the launcher folder changes (it does for the card's games). A tool's wrappers are live as soon as they're written.
4. **Hardened extraction.** `dpkg-deb -x` into `<name>.new`, then: reject absolute paths and `..`, drop setuid and setgid bits, chown everything to `loveos`, refuse a package that would not fit (free space on ROOTFS, minus a reserve). Then swap by rename and keep the previous tree as `<name>.old` until the next update (one **Roll back**). The extraction runs as root and the file came from anywhere, so the folder is treated as hostile, as `docs/NETWORK.md` treats the net helper's.
5. **App data is not in the tree** (`docs/APP-DATA.md`), so an update, a roll back or a removal never touches a person's saves.

Package metadata, one file in the `.deb` (like today's `usr/share/r3i6nos/tool` marker, which it replaces without breaking): `usr/share/r3i6nos/package.json`: `kind` (`app`, `tool`, `system`), `data_dir` (the `<App>` folder name, rule 16), `min_system` (the lowest `VERSION` of the base it runs on), `size_installed`. The Debian `Version` field is the version.

## Three ways in

All three are chosen by a person (rule 6: no telemetry, no network at boot, no background checks, no accounts).

### A. Push from a computer over SSH (first to build; no new menu)

`scripts/push-packages.sh [unit] [file.deb ...]` on the Mac: with no files it takes the newest build of every port (`scripts/ports.conf`, the same list `flash-macos.sh` uses), `scp`s each into the unit's inbox (`~loveos/inbox/`), then runs `ssh -t unit sudo r3i6nos-pkg install --inbox` (the password prompt is the existing sudo one, `docs/NETWORK.md`). It replaces `flash-macos.sh`'s copying of ports for a card already in service, and `build-ports.sh` plus this is the whole development loop. **Everything an app needs to be developed needs no card out.**

Because any program on the card runs as `loveos` and can write the inbox, the unit **asks on its screen** before it installs anything from the inbox that is not from the catalog: "Install Commander 0.9.3 (4.2 MB, sha256 3f9a…) from the inbox? Not in the r3i6nos catalog." (A installs, B discards; it starts on B.) That prompt is for the person, not a defence against a malicious app, which already runs as `loveos` (the same stated limit as `docs/NETWORK.md`); what the helper does defend is root: see "Trust" below.

### B. Pull on the unit: Settings > Packages

New in the menu, the pad only, never scrolling (rule 5), whole pages:

- **Installed:** name, version, which card, size; **Update** (if the last check found one), **Roll back**, **Move to the other card** (`docs/STORAGE.md` step 5), **Remove** (confirmation starting on No; app data kept, and the dialog says where it is).
- **Check for updates:** downloads the signed catalog (below), compares versions, marks what's newer. The only network use in Settings > Packages, only on that press, sending nothing about the unit (User-Agent `r3i6nos-pkg`, as About's check).
- **Available:** the catalog's packages not installed, with size and a one-line description; **Install** asks for the card only when both are in (the data card is the default, rule 16's reason), with free space shown.
- **Inbox:** what method A or a person left, with the same prompt.

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

1. `r3i6nos-pkg` with `apply`, `install`, `remove` and the hardened extraction, reading both cards, applying without a restart; tests with fakes.
2. Method A: `scripts/push-packages.sh` and the inbox prompt.
3. The catalog: `scripts/publish-packages.sh`, the signature tool, the public key in the image.
4. Settings > Packages (Installed, Check, Available, Inbox) and its report section.
5. System packages with rollback.
6. Roll back and Move for apps; squashfs for big packages; method C, only if asked.

Untested on the unit: all of it. The questions only a unit answers: whether `r3i6nos-pkg.path` starts under systemd 252 on kernel 4.4 as the net helper's does, how long the extraction of a large package takes on the SD card, and whether a download over the Realtek dongle holds for a 30 MB package.
