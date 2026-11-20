# stretch-proto

The prototype of MusiCue's offline Key and Tempo render (ROADMAP section 40, step 1; design in `docs/MUSICUE.md`, "Key and Tempo: render a copy"). It is not in the app or the image.

- `stretch_render.hpp`: the renderer (header-only, MIT, this repository's licence).
- `proto.cpp`: `render IN.wav OUT.wav [--ratio R] [--semitones S] [--workers N] [--chunks N] [--equal-power] [--no-quiet-seams]` (16-bit mono or stereo WAV), `selftest`, `bench [SECS]`.
- `build-proto.sh [--bench] [--sanitize]`: fetches the two sources below into `build/stretchproto/` (gitignored), builds in Debian 12 arm64 under Docker and runs the self-test.

## Vendored sources (rule 2: licence and source of everything vendored)

Fetched at build time, never committed. Each is pinned by commit and checked after the clone.

| Source | Licence | Commit |
|---|---|---|
| [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) 1.3.2 (Geraint Luff / Signalsmith Audio Ltd.) | MIT | `88061fd4eda56a2353c5d65c74aa3f4ccf76f9da` |
| [Signalsmith Linear](https://github.com/Signalsmith-Audio/linear) (Signalsmith Audio, FFT and SIMD helpers) | MIT | `7bb1c8f36794d6980080ce817c91f8da8fe56a66` |

Whatever ships must carry both licence texts.

## What it does not show

Timings here are an Apple core under Docker, not a Cortex-A35. The tests use synthetic tones and a synthetic song; nobody has listened to real music through it.
