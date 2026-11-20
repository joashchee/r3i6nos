# stretch-proto

The prototype of MusiCue's offline Key and Tempo render (ROADMAP section 40, step 1; design in `docs/MUSICUE.md`, "Key and Tempo: render a copy"). It is not in the app or the image.

- `stretch_render.hpp`: the renderer (header-only, MIT, this repository's licence).
- `proto.cpp`: `render IN.wav OUT.wav [--ratio R] [--semitones S] [--workers N] [--chunks N] [--equal-power] [--no-quiet-seams]` (16-bit mono or stereo WAV), `selftest`, `bench [SECS]`.
- `build-proto.sh [--bench] [--sanitize] [--package]`: fetches the three sources below into `build/stretchproto/` (gitignored), builds two programs in Debian 12 arm64 under Docker (`stretch-proto` with Signalsmith Linear's own FFT, `stretch-proto-pffft` with PFFFT, which has NEON code; both `-O3 -mcpu=cortex-a35`, libstdc++ linked statically) and runs the self-test on both. `--package` makes `build/stretchproto/r3i6nos-stretch-proto.deb` and runs its report from an empty folder.
- `stretch-proto-report`: the device report (rule 9) for step 2 on the unit.
- **`musicue_render.cpp`, `stream_render.hpp`** (ROADMAP section 40 step 3): the renderer as MusiCue runs it, a child program that streams the copy to a file with a journal and resumes (`docs/MUSICUE.md`, "Key and Tempo: render a copy"); `test-render.sh` checks it (run by `build-proto.sh`); it is built into MusiCue's package by `scripts/build_native_app.sh` (`RENDER_HELPER=1`), from the sources pinned in `pins.conf`.

## Step 2 on the unit (ROADMAP section 40), with no SSH

1. `tools/stretch-proto/build-proto.sh --package` makes `build/stretchproto/r3i6nos-stretch-proto.deb`.
2. Copy it into **`native_deb/` on the data card** (`EASYROMS`-style: the card in the second slot; the folder is there once the card has been formatted by Settings > Data card; else use the system card's `native_deb/`). The boot watcher installs it at the next start: restart the unit.
3. **Settings > Device reports > StretchProto**: a screen asks for the charger to be out; A starts it (about 3 minutes, a progress bar). Leave the unit alone while it runs.
4. Copy **`StretchProto/r36s-report.txt`** from the data card (the system card when no data card is in) to the computer and give it to Claude.

Over SSH instead: `scripts/push-packages.sh UNIT build/stretchproto/r3i6nos-stretch-proto.deb`, then `ssh loveos@UNIT stretch-proto-report`.

The report runs both programs' self-tests on the unit, then times 1 to 4 workers for each FFT and each preset on 60 s of audio, with the unit's temperature and clock before and after, the peak memory and, on battery, the draw in mW.

## Vendored sources (rule 2: licence and source of everything vendored)

Fetched at build time, never committed. Each is pinned by commit and checked after the clone.

| Source | Licence | Commit |
|---|---|---|
| [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) 1.3.2 (Geraint Luff / Signalsmith Audio Ltd.) | MIT | `88061fd4eda56a2353c5d65c74aa3f4ccf76f9da` |
| [Signalsmith Linear](https://github.com/Signalsmith-Audio/linear) (Signalsmith Audio, FFT and SIMD helpers) | MIT | `7bb1c8f36794d6980080ce817c91f8da8fe56a66` |
| [PFFFT](https://github.com/marton78/pffft) (Julien Pommier's, with FFTPACK; only in `stretch-proto-pffft`) | BSD-like (the text is at the top of `pffft.h`; the package carries it) | `efc1dd2de6c3562fc464f905d37d9281f08a20dd` |

Whatever ships must carry the licence texts (the package has them in `/usr/share/doc/r3i6nos-stretch-proto/`).

## What it does not show

Timings here are an Apple core under Docker, not a Cortex-A35. The tests use synthetic tones and a synthetic song; nobody has listened to real music through it.
