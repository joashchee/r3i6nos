# Port template

Not an app: this folder has no `app.conf`, so no script or workflow picks it up. To add a port:

1. Copy it to `apps/<name>-port/`; rename `app.conf.template` to `app.conf` and `build.sh.template` to `build.sh`; fill in the manifest.
2. `scripts/build_port.sh <name>-port --validate` (anywhere), then `--prepare-only some/dir` to check the pin and patches.
3. Copy `app-port.yml.template` to `.github/workflows/app-<name>.yml` and rename the port in it.
4. Record the source, licence and what game data it needs (`docs/` or the port's README). Never commit the source, ROMs, BIOS or proprietary data.
5. Give it a **device report** (CLAUDE.md rule 9, `docs/DEVICE-REPORTS.md`): a Device report item in its menu and a `--report` flag that write
   `/roms/<App>/r36s-report.txt` for Claude, with `theme/r3i6nos_report.h`; a self-test that checks the file. The port is not done without it.

A port is a user-installed `.deb` (card `native_deb/`), never part of the firmware image, and links only libraries the rootfs has.
