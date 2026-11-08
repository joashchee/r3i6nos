# Lean CLI ports of Composer, Painter and Animator (QuickJS)

Status (2026-11-08): **the shared toolchain is built and tested; the three tools run, on this Mac and in Debian 12 arm64 under Docker, with the same output as Node; each app's repo
has its `r36s/` folder (manifest, command list, licence note, check and package scripts, workflow); nothing has run on a real R36S, and the GitHub workflows have never run.** The apps: Composer (`~/Coding/composer`, seeded music: MIDI, WAV, NTT),
Painter (`~/Coding/painter`, sprites and ANSI art: PNG, ANS, TXT) and Animator (`~/Coding/animator`, motion clips: JSON). All three are dependency-free JavaScript libraries
with a Node `cli.js` (`compose`, `sprites`, `animate`). The point of a port is that **other apps can use them**: NoCoGaMa's sprites, jingles and animations, KlamANSI's ANSI
art, anything on the device, from a command line, with no Node on it.

## Shape: one shared builder here, a thin manifest in each app's repo

KlamANSI's host lives in KlamANSI's repo because it is proprietary. A Node-CLI-to-QuickJS builder has nothing proprietary in it, so it lives here (MIT, `tools/qjs-node/`),
takes an app folder as input, and **copies none of it into this repo** (rule 2). Each app's repo will carry only what is its own: a manifest, its grant or licence, its list of
commands to compare, and a workflow (`r36s/`, as KlamANSI has). Composer is proprietary (its `.deb` needs a confirmed grant, as KlamANSI's); Painter and Animator are Apache-2.0
(their `.deb` ships the licence and `NOTICE`).

| Part | What |
|---|---|
| `tools/qjs-node/shim/` | `node:util` (`parseArgs`), `node:fs`, `node:path`, `node:zlib` (inflate, deflate), `node:crypto` (`createHash('sha256')`), `Buffer`, `TextEncoder`/`TextDecoder`, `structuredClone`, `process`, `console` for QuickJS |
| `tools/qjs-node/stage.mjs` | copies a CLI's import closure and points every `node:` import at a shim; stops the build if one has no shim, so nothing fails on the device |
| `tools/qjs-node/build.sh` | stage, compile to **QuickJS bytecode** (`qjsc`), link: ONE executable per tool, no JavaScript source shipped; needs only libc and libm |
| `tools/qjs-node/main_tail.c` | the `main()`: an uncaught error or rejection prints on stderr and exits 1, with a 4 MB stack (qjsc's own `main` exits **0, silently**, and has 256 KB) |
| `tools/qjs-node/make-deb.sh` | a **tool package**: `usr/bin/<tool>`, the marker `usr/share/r3i6nos/tool`, licences in `usr/share/doc/<package>` |
| `deb_watcher.py` | a package with the marker is a tool: no menu launcher; wrappers in `/opt/loveos/bin`; removed with the deb (never by an absent card) |
| `run_app.sh`, `/etc/profile.d/r3i6nos-tools.sh` | `/opt/loveos/bin` is on `PATH` for every app and for SSH |

## What was checked

- **The shims against Node's own** (`tests/test_qjs_node.mjs`, 6 groups): `parseArgs` (values, positionals and error codes), `path`, UTF-8 encode and decode on good and bad input,
  `Buffer`, `zlib` both ways (our inflate reads Node's deflate at every level; Node's inflate reads ours), `sha256` for every length around the block edges.
- **The toolchain end to end** (`scripts/test_qjs_node.sh`, a sample CLI that uses every shim): stdout, stderr, exit status and files equal Node's for 11 command lines, including an uncaught
  error and a rejection (status 1 and the message on stderr; the stack format differs). "NOT RUN" without the pinned QuickJS tarball.
- **The three real tools**, the same commands under Node and QuickJS (stdout, stderr, exit status, every file): **Composer, 14 commands, all identical, including the `.wav` byte for byte**;
  **Animator, 10, all identical**; **Painter, every command identical except the PNG files, which have the same pixels** (below).
- **In Debian 12 arm64 under Docker**: the three binaries (1.0 to 1.8 MB, needing `libm` and `libc`), the three tool packages (0.37 to 0.65 MB), installed by the real `deb_watcher.py` (no launchers; `compose`,
  `sprites`, `animate` on the tools path), and their output compared with Node's from the Mac: `.mid`, `.wav`, `.ans` and `.json` **byte-identical**, the `.png` the same pixels.
  `compose make song ...` takes 14 ms there (a VM on an Apple chip: **not** a Cortex-A35).

## Limits, said plainly

- **PNG files are not the same bytes as Node's, and bigger** (a 42 KB scene is 10 KB from zlib; a sheet 1.4 MB against 278 KB): the shim's deflate is fixed-Huffman with greedy matching, not zlib's. The pixels, the
  checksums and the format are right (Python's zlib and Node read them). Painter says as much about its own PNGs ("hashes are of the pixels, not of the PNG bytes"). A better deflate (hash chains, dynamic
  Huffman) is a contained improvement if size matters.
- **`sprites kits` takes 14 s under Node** (it draws every kit subject) and minutes under QuickJS: a maintainer's coverage report, not a device command. Single draws, lints and exports are fast
  (QuickJS starts in about 60 ms where Node takes 110 ms).
- **`console.log` of non-strings is a compact `util.inspect`**, not Node's (short arrays and objects, BigInt `n`, `-0`; not column grouping of long number arrays, class instances or getters). The tools print strings.
- **Files a tool reads at run time, not imports, are not bundled**: Painter's `--render` of an ANSI file to PNG reads `docs/fonts/ibm-vga-8x16.png` (CC BY-SA, via `import.meta.url`). That path is not ported
  yet (it would embed the atlas in the binary, with its credit). `node:` modules other than the six above stop the build.
- **Numbers that are floats** (Composer's WAV preview) came out identical in QuickJS (libm) and V8 here. Composer's rule 4 says the preview is never byte-compared; a different libm on the device could differ
  in a last bit: **untested on the R36S**.
- **Not tried on a real R36S**: any of it, including that a LÖVE app's `io.popen` and `os.execute` work there. They are LuaJIT's and LÖVE does not remove them, but nobody has run it.

## Using a tool from a LÖVE app (NoCoGaMa)

```lua
local h = io.popen('compose make jingle kind=win --out /roms/NoCoGaMa/assets/win.wav 2>&1')
local said, ok = h:read('*a'), h:close()   -- `said` is the tool's one line, or its error; `ok` is true on exit 0
```

The tool writes only where `--out` or `--dir` say, so a game points it into its own folder under `/roms/NoCoGaMa/` (rule 7). `PATH` has `/opt/loveos/bin` for apps started by `run_app.sh`. Arguments built from
user input must be quoted (a name typed on the on-screen keyboard is a shell injection otherwise): NoCoGaMa's device spike already refuses shell-injection names in its Card page. A small `tools.lua`
that quotes and checks the exit status is the natural next piece, in NoCoGaMa's repo. Games exported from NoCoGaMa should carry the **generated files**, not call the tools at run time: an exported game runs
on any LÖVE, with or without these packages.

## The three apps' `r36s/` folders (2026-11-08)

Each repo (`~/Coding/composer`, `painter`, `animator`) has `r36s/` with `tool.conf` (the command, the package, the licence that ships), `commands.txt` (compared with Node), `package/copyright`, `check.sh`
(a native build compared with Node: needs Node 22+), `build-deb.sh` (an arm64 Debian 12 container: builds, packs, installs with r3i6nos's real `deb_watcher.py`, runs the first command) and a README, plus
`.github/workflows/r36s.yml` (not yet run on GitHub) and a changelog entry. The scripts are r3i6nos's (`compare.py`, `check-tool.sh`, `package-tool.sh`); the repos hold no copy of them. No recipe or output changed, and no version was bumped.

| Repo | Command | Compared with Node | Package | Licence that ships |
|---|---|---|---|---|
| Composer | `compose` | **15 of 15 identical** (the `.wav` byte for byte) | `compose.deb`, 452 KiB | KlamANSI's free R36S grant, **a draft** (`COMPOSER_RELEASE=1` refuses a build while the `DRAFT GRANT` marker is there; checked) |
| Painter | `sprites` | 11 of 11 identical, `.png` by pixels (`kits` is not run: minutes) | `sprites.deb`, 644 KiB | the same grant (`PAINTER_RELEASE`); `NOTICE` with the CoffeeMUD attribution and the Apache-2.0 text that must go with that data |
| Animator | `animate` | **10 of 10 identical** | `animate.deb`, 388 KiB | the same grant (`ANIMATOR_RELEASE`) |

**The licences (maintainer's decisions, 2026-11-08):** the three apps travel together, so all three follow Composer's: proprietary, all rights reserved (`LICENSE`), `package.json` `"license": "UNLICENSED"`, `private`.
Composer's R36S package ships **KlamANSI's grant** (free to use on the R36S and pass on, complete and unmodified; source stays proprietary; holder ansiapps; a `DRAFT GRANT` marker blocks a release until the maintainer confirms it), and so do
Painter's and Animator's, with their own names. Painter and Animator were **Apache-2.0** until then and are now proprietary like Composer: copies already distributed under Apache-2.0, such as the one Coupler ships, stay under it.
Painter's CoffeeMUD data is Apache-2.0 third-party material (Bo Zimmerman), so its licence text is kept (`LICENSES/Apache-2.0.txt`) and appended to the package's `NOTICE`.
**Open: Composer's `LICENSE` and `package.json` name Joash Chee as holder, while the grant (like KlamANSI's) and the other two apps name ansiapps.**

## What is left

1. **The maintainer confirms the grant** (KlamANSI's wording, now on four packages) and deletes the markers, and settles whose name Composer's `LICENSE` carries.
2. A try on a real R36S, then `qjsc` startup and memory numbers on the Cortex-A35, and a LÖVE app's `io.popen` there.
3. Run the three GitHub workflows once (they need this repo to be readable by theirs, and an arm64 build under qemu, which is slow).
4. Optional: a better deflate; the VGA atlas for Painter's PNG render; a `tools.lua` helper for LÖVE apps; a `--help` that fits the 64-column screen.
