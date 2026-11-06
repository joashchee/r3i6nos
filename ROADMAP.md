# Roadmap

What is planned and what is left, as of 2026-11-06. Each section names the
document that has the detail. Nothing here has run on a real R36S yet.

## 1. Scaffold (spec: `docs/r36s_love_only_firmware_specification.md`)

- [x] **Repository:** README, CLAUDE.md, CHANGELOG, MIT licence, CI, `scripts/check.sh`.
- [x] **Overlay scripts:** `deb_watcher.py` (tested), `run_app.sh`, `S99loveos`
  in `overlay/`, rewritten from the spec's drafts (see CHANGELOG for what changed).
- [x] **Menu:** LÖVE 11.5 launcher in `menu/`, paged list, scan tested.
- [ ] **First push** to GitHub (`joashchee/r3i6nos`).

## 2. Prove it on hardware

- [ ] **Confirm the board:** read the DTB on the original card (spec section 1) and record which units are supported.
- [ ] **Capture the gamepad mapping** from a real unit and replace the unverified
  `SDL_GAMECONTROLLERCONFIG` in `run_app.sh`.
- [ ] **Decide on gptokeyb:** needed at all? LÖVE reads gamepads itself; drop it
  and `etc/love_keys.cfg` if not (`/etc/love_keys.cfg` is referenced but not written yet).
- [ ] **Boot the overlay** on a stock dArkOS card by hand: menu shows, a `.love` runs, a `.deb` runs.

## 3. Image build

- [ ] **Base:** pick the base (dArkOS, spec section 5) and record its licence and source.
- [ ] **LÖVE runtimes:** ARM64 builds of 11.5 and 12.0 into `/opt/love/<ver>/`
  (12.0 is a nightly: pin a commit).
- [ ] **`scripts/build-image.sh`:** base + overlay + runtimes + menu into a flashable image, no ROMs or BIOS.
- [ ] **Strip** the emulation stack (EmulationStation, RetroArch) and measure free RAM against the spec's claim.

## 4. Native apps

- [ ] **Dependencies:** `dpkg-deb -x` pulls no dependencies; decide between
  bundled libs only (today) and a pre-baked set of common ones.
- [ ] **Multiple binaries:** let a package say which one launches (today: name match, else first).

## 5. Release

- [ ] **Releases:** image uploaded by hand with `gh release upload`; checksums in the notes.
- [ ] **Flashing guide** for macOS, Windows and Linux.
