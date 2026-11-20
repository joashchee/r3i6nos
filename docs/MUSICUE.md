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

## The pad, the panels and Tempo (2026-11-19, maintainer's map)

**Buttons (the map of 2026-11-20, replacing the first one of 2026-11-19; the full table is in `apps/musicue-native/README.md`, "The pad"):** MusiCue follows `docs/r3i6nos-mapping.md` through `theme/r3i6nos_input.h` and `theme/r3i6nos_mainmenu.h`:
**L1+R1 the main menu** (About, Colour theme, Device report, Rec demo, Quit, then Songs and sync or Song options; Benchmark hidden), **L2+R2 or Y the context menu**, **Fn the next panel, L1+Fn the first, R1+Fn the one before**, the right stick through the reader's dead zone and rate,
R1 held for pages and Home/End. **Start plays (Play mode); L1+Start plays in Rec mode; playing, Start stops and ends Rec mode.** **Rec mode:** D-pad Down adds a cue, Up takes the last cue of the take back, Left and Right go to the cue before and after.
**Play mode:** Up and Down go to the first and last cue, Left and Right to the cue before and after. **R1+Up/Down set A and B, R1+Start switches the A-B loop (A and B are kept), R1+Select goes to A.** Select alone is 0:00. L1, R1, L2 and R2 alone do nothing, and the old
L1/R1 marker steps, L2/R2 10 s and Y loop cycle are gone (R1+Left/Right is 10 s). Quit is in the main menu and Select+Start no longer leaves. Markers have no button: a marker is a bookmarked cue (the context menu).
**Panels (rows 9-20, two at a time, Fn turns the carousel one place, and a seventh place is the Jump panel on the whole width, the one with the stick has the double frame):** Markers, Cues (time, a tempo dot, a marker flag, the words), Metadata (key, tempo, sig, scripter, created; A edits, Sig toggles 4/4 and 6/8,
a number or a letter picker for the rest, a USB keyboard types), Set List, **Last 10 cued**, **Top 10 cued**.
**Set List** is Cue's: an ordered list of songs (add this song, move up and down, remove, A opens), in `setlist.tsv`. **Last and Top 10 cued are local**: Cue's are server lists across all users (its `VIDEO_LOG_ENDPOINT`, not deployed), which MusiCue
cannot have under rule 6 (no telemetry), so these are the songs *opened on this unit* (`history.tsv`: newest first; most opened first). Cue's versions of a song are not in MusiCue yet, so a Set List entry is a song and not a song-and-version.
**Tempo is Cue's logic, read from its source** (`computeGapTempo`, `classifyTempoDot`, `computeSuggestedTempoCluster`, `maybeSuggestTempo`, `renderCaptions`): a cue's length (its start to the next cue's start) is 16 beats in 4/4, 8 in 6/8 (any other Sig counts as 4/4, and MusiCue says so),
so it has its own tempo, `60 / (length / beats)`; against the Tempo it is **green within 2 BPM, yellow within 5, red beyond** (the last cue has no dot). A Tempo that is blank or `?` is **suggested**: the largest group of cues within 2 BPM of one of them (the first found on a tie), averaged to two
decimals, those cues green. It runs when a cue is added or deleted, when a song with cues opens and when the Sig changes, and **never overwrites a Tempo that was typed**; Metadata > Tempo with the number erased, or the menu's Recalculate, asks for it again. As in Cue, once suggested it
is a number like any other, so later cues do not move it until it is erased. The Assisted Cue entry's bars use the green cues only.

## The Jump panel, the Piano and numbered marker names (2026-11-20, maintainer's request)

**Jump panel** (rows 9-20, the whole width; the carousel's seventh place): every marker, a page at a time, with **Cue's playback-progress fill** (`updateMarkerProgressFills` in Cue's source): the marker's own section, from its time to the next
marker's, fills left to right in the marker's colour while the playhead is in it, one section at a time, and **the last marker has no section** (Cue's rule: no next marker to bound it), so it never fills. A triangle marks the marker last passed, an `L` the sections inside a running A-B
loop. The bottom row is **Cue's A-B progress** (`updateTempLoopCProgress`): A, a bar, B, filling only while the loop is on (empty when it is off or B is not set yet). The **eight buttons** (D-pad up, down, left, right, then A, B, X, Y) are **one-touch**:
the right stick picks a marker, **stopped** a press binds it (again on the same marker frees it), **playing** a press moves the playhead there; each row shows the tags of its buttons. Bindings are kept by the marker's time in `cues/<id>.jump`. R1+Up/Down/Start/Select
keep their A-B meaning there. The Jump panel overrides Rec mode's D-pad (record on another panel). **Fn while Play mode plays goes straight to the Jump panel, wherever the carousel is; when the music stops, the panels that were showing come back** (a manual Fn or L1/R1+Fn in between cancels the restore; Rec mode does not do this). **Not decided by the request, so chosen here:** the Jump panel is the last place of the carousel (after Top 10 cued); B does not leave the song on it (a face button is a jump button).

**Piano.** Editing the **Key** in Metadata opens a floating Piano panel (the field's box shrinks to the upper half): one octave, **C3 to B3** (C3 is 130.81 Hz, scientific naming), played through the same audio callback over the song (a sine with two overtones and a decay, up to 12 voices, a released note fades in 50 ms), so a person finds the
key by ear while the song plays. D-pad Up C3, Left D3, Down E3, Right F3; Y G3, B A3, A B3, X A#3; L1 C#3, L2 D#3, R2 F#3, R1 G#3 (shoulders are notes there, not modifiers, so L1+R1 together is two notes and not the menu); **Select Oct-, Start Oct+** (octaves 1-5); **Fn closes it**
(and opens it again). **A note writes the key's root** (sharps: C#, not Db) and keeps what follows it, so Am7 played G is Gm7; A keeps the key once the piano is closed (A is B3 while it is open), B cancels. Tested for the notes, the voices (no clipping) and every theme's contrast; **never heard on a unit.**

**Numbered names.** In "Name the selected marker", **Right on a preset** (Intro, Verse, Pre-Chorus, Chorus ...) asks for a **number or letter after it**: Up and Down walk 1-9, A-Z, a-z, 0; Right moves to a next place (so Verse A, Chorus 1a); A keeps "Verse 1a", B cancels; A on the preset alone keeps the plain name.

**What this changed:** Device report and Rec demo in the main menu say they are not built yet (Rec demo is the video of rule 11, nothing to do with Rec mode); the old Theme menu item is the main menu's Colour theme; the old menu's items are now Songs and sync (Add, Sync, Import, Demo song, Fetch from YouTube)
and Song options (Offset, Save, Export, Add to the Set List, Recalculate tempo, Back to the songs); the context menu has Cue here, Mark here and what the focused panel can do. **Untested on a unit:** every press (the pad, the shoulders as notes, the chord feel), the piano's sound and latency.

## The cue panel (2026-11-19)

Rows 7 and 8, under the waveform, are Cue's cue panel (the section strip and the playhead triangle are gone; the loop and the offset moved to the command line's status). **Row 7: the cue playing now** (its start has passed and its end has not; a cue with no end runs to the next cue, the last one 6 s), with the **beat dot** at the right, a green dot that pulses once a beat (from the Tempo, anchored to the start of the cue playing, only while the song plays). **Row 8: the next cue, muted**, only while a cue is playing. **At the last cue, row 8 is Cue's Assisted Cue entry instead**: two bars, full at the last cue's start, draining toward the middle over the average length of the cues whose own tempo is green (within 2 BPM of the Tempo; 16 beats a cue in 4/4, 8 in 6/8), shown only when the Tempo is set, the song plays and there is room for one more cue, as Cue's `updateAssist()` decides. MusiCue had no way to make a cue, so **D-pad Down** (and the menu's Cue here) stamps one at the playhead, reading "- - - -" as in Cue; the words are still typed in Cue. Tested in `--selftest` (`test_cue_panel`); **not looked at on a unit**.

## Add: YouTube ID (2026-11-19)

The menu's (L1+R1) **Add: YouTube ID (11 chars)** opens an 11-slot entry (no on-screen keyboard exists yet: Up/Down cycle A-Z a-z 0-9 - _, L1/R1 jump 8, Left/Right move, Y erases, a USB keyboard types it; case matters).
A on the last filled slot, or Start, goes. **Online** (a default route in `/proc/net/route`; nothing is sent to find out) it runs `musicue-sync --id <id>`, which does what Sync does for that one video, and when it ends
the song opens. **Offline** the id is appended to `inbox/urls.txt`, so the next Sync gets it. **The Creative Commons default still applies:** a video that is not CC gets its cues (if the server has any) but no audio, and the job
screen says so; Start > Fetch from YouTube lifts it. Tested against a fake helper (`--selftest`) and `tests/test_musicue_sync.py`; **untested on a unit and with real yt-dlp over Wi-Fi.**

## What the earlier work gave this

- `docs/STREAM-TOOLS.md`: the yt-dlp package the add-on runs, its cost (a cold YouTube extraction about 4.5 s of CPU and 211 MiB on an Apple core, **estimated at tens of seconds on the unit**) which is paid once at sync, not at play.
- `docs/VIDEO-DECODE.md`: the VPU question does not arise: MusiCue plays no video, and AAC decoding is trivial on the CPU.
- The theme's true-colour palette (`theme/r3i6nos_themes.h`) and Commander's player pipe (`apps/commander-native/player.h`).
