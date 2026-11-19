# MusiCue: Cue on the R36S (2026-11-19)

**Status: a first slice is built (`apps/musicue-native/`), tested in Debian 12 arm64 under Docker and on a Mac, and never run on a real R36S.** What is and is not done is in "What is built" and in
ROADMAP section 36. MusiCue is the R36S port of the maintainer's web app Cue (`~/Coding/cue`, https://cue.chordica.com/: jump markers and synced captions on a YouTube video, A/B loops, Set Lists), rebuilt
around **an audio file on the card** and working **offline** once the device has fetched what it needs.

## Decisions (the maintainer, 2026-11-19)

| Decision | |
|---|---|
| **Local files, plus a yt-dlp add-on** | The sound is a file the person has. A separate, optional sync fetches Cue's server cue files and, with the yt-dlp package installed, audio |
| **Rebranded MusiCue** | The port has its own name. It is not a YouTube API client, so Cue's terms and privacy pages do not describe it (a text of its own is still to write) |
| **No video**, only **an audio waveform at the top showing the whole song (no scrolling) and a moving playhead** | Rows 1-6 are pixels; the rest of the screen is cells |
| **Everything else is the r3i6nos-ansiapps text mode, in true colour**, with gradients and shades | Palette slots 32-255 hold gradients made from the chosen theme; every text pair still passes 4.5:1 |

Cue's own rules had to be weighed (its `CLAUDE.md` rule 1, strictly a web app, and rule 3, no downloading or caching video, kept from the YouTube API review): a native port is a different thing, and the
yt-dlp add-on is what leaves rule 3. **One default was chosen to keep the add-on inside the line the maintainer has drawn so far: it fetches Creative Commons videos only** (yt-dlp's `license` field,
`--match-filter "license*=Creative Commons"`; the field's exact text should be checked against a real video), and fetching any video is a setting, off, in Start > Fetch from YouTube. That default is
a recommendation to confirm or change; it is not legal advice, and what a person fetches is theirs to answer for.

## What is built

| Piece | Where |
|---|---|
| Cue's files: a JSON reader (nesting and size capped), markers, captions, the five fixed metadata fields, the server pair (`.json` + `.srt` with the hidden metadata block), version data, `.cuebundle`, atomic writes | `apps/musicue-native/cuedata.h` |
| Sound: a WAV read in place (8/16 bit, mono/stereo, any rate resampled), everything else through the image's `ffmpeg` on a pipe, into RAM; an engine with play, seek, a gapless A/B loop and a no-device clock | `audio.h` |
| The waveform: peaks over the whole song, a gradient shape with played and to-come colours, marker flags, a tinted loop span, the playhead | `wave.h` |
| The app: library, player, menus, themes, the sync job screen, import and export, `--selftest`, `--screenshot`, `--make-demo` | `musicue.c` |
| The sync: Cue's server cue files and, with yt-dlp, audio; progress lines the app reads | `musicue-sync` (Python, standard library only), packed beside the program |
| Tests | `--selftest` (cue data, audio and the engine, the waveform, every screen in every theme for contrast, the song flow, the inbox), `tests/test_musicue_sync.py`; both run in `scripts/check.sh` |
| Build | `scripts/build-native-local.sh musicue-native` (Docker); `.github/workflows/app-musicue.yml`; a user-installed package (`DEFAULT=0`), not in the image. `scripts/build_native_app.sh` gained `EXTRA_FILES` for the helper |

### How it works, and why

- **The song lives in RAM.** 44.1 kHz stereo 16-bit is 176 KB/s: **4 minutes is 42 MB, 12 minutes (the cap) 121 MiB**, on a unit with about 848 MB free with an app running. A jump is a pointer move and the loop
  wraps inside the buffer the audio callback is filling, so it has no gap (a test checks the callback's samples across the wrap, sample for sample). Opening costs one decode: a WAV is read here, anything else is
  one `ffmpeg` run on a pipe (AAC decoded 19 s of audio in 0.01 s on a Mac; the unit's time is untested).
- **The waveform is the only pixel drawing.** 640 columns, one per pixel, each the lowest, highest and RMS of its slice of the song; long songs are sampled inside a column (a 12-minute track took 13 ms on a Mac, and one
  redraw 1 ms, neither measured on the unit). It redraws only when the playhead moves a pixel (a 4-minute song: under three times a second), a marker changes or the palette does. The pixel columns line up with the
  64 text columns (ten to one), so the section strip and the marker names under it are in step with the picture.
- **True colour in a cell grid.** A cell holds a palette slot, so gradients are palette ramps: a bar ramp (the top bar), an accent ramp (the selected row, the cue playing now, the progress bar), 32 marker hues
  (bright for played, dim for to come), and a text-to-page fade whose every step is chosen to keep 4.6:1 against the page (the cues before and after the current one fade along it). A cell on a gradient takes black
  or white by contrast, which is always at least 4.58:1. The marker colour comes from the marker's name with its number removed, so every "Chorus" has one colour and a song's structure reads at a glance.
- **Cue's rules are in the code.** The metadata fields cannot be added to by any file; a value is cut to its limit on a character boundary; `created` is dropped unless it looks like an ISO time and is only ever set by an
  export; the 33 marker key slots (1-9, 0, d-z) are assigned in time order for the files Cue reads. An import that disagrees with a file already there goes to `cues/.merge-conflicts/`, never over it.
- **The sync never overwrites a person's work, sends nothing but its requests, and is the only online step** (rule 6). It reads `inbox/urls.txt`, fetches each video's server cue files if the server has any (most videos
  have none), and, if yt-dlp is installed and the video is Creative Commons, its AAC audio (about 1 MB a minute) into `media/` as `Title [id].m4a`, which is how the app matches a file to Cue's data.

## Not done, and what each needs

| | |
|---|---|
| **Anything on a real R36S** | decode time on the Cortex-A35, memory, the sound device, the pad, the sync over Wi-Fi: **blocked on the maintainer's unit** |
| **The rules every R36S port has** | a device report with a `== power ==` section (rules 9, 14), Rec demo with a demo file (the synthetic song is already the demo file, `--make-demo`) and a hero list (in the README), the hot-plug toast wired into the sync (the toast is drawn in the main loop already), the app's own `docs/` pointer rule (the app never names a doc: checked) |
| **Naming a marker freely** | an on-screen keyboard (the theme has none yet); ten preset names and a USB keyboard's `M` for now |
| **A text of its own** | MusiCue's terms and privacy text (Cue's describe a YouTube API client with a play counter), and the name's clearance (`~/Coding/cue/docs/cue-store-name.md` is Cue's research) |
| **Cue's Set List and versions** | a bundle is imported as one version of each video (the favourite, else "mine", else the first); the Set List's order and the other versions are not kept |
| **Practice speed** | the web app has none; `ffmpeg-min` has no `atempo`, so it would need a time-stretch in the app |
| **Checking the Creative Commons filter** | against a real video's `license` field (the packaged yt-dlp, `docs/STREAM-TOOLS.md`); the filter is tested against a fake yt-dlp |

## What the earlier work gave this

- `docs/STREAM-TOOLS.md`: the yt-dlp package the add-on runs, its cost (a cold YouTube extraction about 4.5 s of CPU and 211 MiB on an Apple core, **estimated at tens of seconds on the unit**) which is paid once at sync, not at play.
- `docs/VIDEO-DECODE.md`: the VPU question does not arise: MusiCue plays no video, and AAC decoding is trivial on the CPU.
- The theme's true-colour palette (`theme/r3i6nos_themes.h`) and Commander's player pipe (`apps/commander-native/player.h`).
