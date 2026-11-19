# Stream tools: yt-dlp, gallery-dl and streamlink as backends for native apps

Why: an app that fetches video or pictures from the web (a player, a downloader, a gallery) should not carry a site-by-site scraper, and no one can keep one working: sites change every few
weeks. **yt-dlp** (video and audio, about 1,700 extractors), **gallery-dl** (image galleries, about 3,900 extractors) and **streamlink** (live streams and HLS/DASH, 136 plugins) are the maintained
ones. They are packaged here as ordinary **tool packages**, so a native or LÖVE app runs them as commands (`io.popen`, `popen`) and reads what they print or write: the same shape as
`compose`, `sprites` and `animate` (`docs/QJS-CLI-PORTS.md`) and as nttplay (rule 15).

**Status (2026-11-19): built, packed reproducibly and tested in Debian 12 arm64 under Docker, plus one real YouTube request; not run on a real R36S.** Everything about speed on the unit below is an
estimate from a faster CPU and is marked so.

## What is here

| Piece | Where |
|---|---|
| Pinned sources and versions (every download is checked against a sha256) | `tools/streamtools/pins.conf`, `tools/streamtools/lock/<tool>.txt` (every Python package with its hash) |
| The command each package installs | `tools/streamtools/{ytdlp,gallery-dl,streamlink}/run` (`yt-dlp`, `gallery-dl`, `streamlink`) |
| The build, in Debian 12 arm64 under Docker | `scripts/build-streamtools.sh` (`--latest`, `--relock`, or tool names) runs `tools/streamtools/build.sh`; output `build/streamtools/r3i6nos-{ytdlp,gallery-dl,streamlink}.deb` |
| Tests | `tests/test_streamtools.sh` (no Docker: pins, locks, the wrappers' arguments), `tests/test_streamtools_docker.sh` (Docker: the packages installed by the real `deb_watcher.py` in a clean Debian 12 and run) |
| One real request, by hand | `R3I6NOS_LIVE=1 tests/test_streamtools_docker.sh` |
| CI, and where to download the packages | `.github/workflows/app-streamtools.yml`: builds the three on an arm64 runner (sharing `ffmpeg-min`'s cache with `build.yml`), installs them in a clean Debian 12 through the real `deb_watcher.py`, runs each tool against a local web server, and uploads the `.deb` files as the artifact `streamtools-arm64`; a `streamtools-v*` tag also publishes them (with `SHA256SUMS`) as a GitHub release. Checked with `actionlint`; **never run on GitHub** |

The packages are **not in the image** (like the runtimes, `docs/RUNTIMES.md`): put the `.deb` in a card's `native_deb/`, or `scripts/push-packages.sh UNIT build/streamtools/*.deb`, or publish them in the
signed catalog (`scripts/publish-packages.sh build build/streamtools/*.deb`; the catalog reads them as `kind: tool`, checked).

## What each costs

Measured on 2026-11-19 (`.deb` is xz; installed is the sum of the files; both include byte code, see "Start-up").

| Package | `.deb` | Installed | Native code | Licence | Notes |
|---|---|---|---|---|---|
| `r3i6nos-ytdlp` 2026.10.26 | 10.0 MiB (10,491,512 B) | 17.4 MiB | QuickJS-ng (1.2 MiB, libc and libm only), pycryptodomex (AES) | Unlicense; QuickJS-ng MIT | 8.4 MiB is yt-dlp itself (3.0 MiB as released, plus its byte code), 7.0 MiB pycryptodomex, 1.2 MiB QuickJS-ng, 0.15 MiB the challenge solver |
| `r3i6nos-gallery-dl` 1.32.20 | 2.2 MiB (2,276,964 B) | 12.3 MiB | none | **GPL-2.0-only** | `requests` and its four pure-Python dependencies; its `.py` files are its source |
| `r3i6nos-streamlink` 8.6.3 | 6.5 MiB (6,790,820 B) | 38.8 MiB | lxml, pycryptodome (aarch64 wheels) | BSD-2-Clause; pycountry is LGPL-2.1 | 20 packages; **21 MiB of pycountry translations are removed** (the code falls back to English; checked) |

**All three: 18.7 MiB packed, 68.5 MiB installed.** gallery-dl is the cheapest thing to add (2 MiB, no native code). streamlink is the most expensive (lxml, trio and pycryptodome); it earns its place
only for live streams (Twitch and similar) that yt-dlp does not handle as well. Nothing else is worth shipping: `youtube-dl` is stale, `you-get` and `lux` cover less, and Invidious or Piped
need a server someone runs (no accounts, no network unless chosen: rule 6). Python 3.11 and `ssl` come from the image's own `python3-minimal` and `libpython3-stdlib` (the test installs only those).

## The design

- **One installed copy of each, and apps are frontends.** The same rule as nttplay (rule 15): an app runs `yt-dlp` from the tools `PATH` (`/opt/loveos/bin`) and **never bundles its own copy** or a site
  scraper. A frontend that cannot find it says so on screen.
- **The tools run on the image's Python 3.11**, and yt-dlp's zipapp is **unpacked, byte-compiled and packed again** at build time (the original is checked against its sha256 first). Python cannot cache
  byte code inside a zip, and the install folder is read-only for the user who runs it, so without this every run would recompile yt-dlp's ~1,000 files (the test checks that nothing is compiled at run time).
- **yt-dlp is the network layer for video.** `ffmpeg-min` has no network (`--disable-network`, protocols `file` and `pipe` only), so a frontend cannot hand it a URL. Either yt-dlp **downloads to the app's
  data folder** and the app plays the file, or yt-dlp **writes to a pipe** that `ffmpeg` (or nttplay) reads.
- **JavaScript for YouTube: QuickJS-ng.** Since late 2025 YouTube's signature and "n" challenges need an external JavaScript runtime. The package carries `qjs` (QuickJS-ng 0.18.0, built here from a pinned
  source; yt-dlp wants quickjs-ng 0.12 or newer, or QuickJS 2025-04-26 or newer, and the 2024-01-13 QuickJS that `tools/qjs-node` uses is too old, so this is a **second, private copy** inside the
  yt-dlp package, not on the `PATH`) and **`yt-dlp-ejs`**, the solver scripts yt-dlp loads (a 53 KB wheel; the zipapp does not include it). The wrapper passes `--js-runtimes quickjs:<that qjs>`.
- **Defaults come first, so any option wins.** The wrapper sets the cache directory, the JS runtime, `--ffmpeg-location /usr/bin/ffmpeg`, `-P $R3I6NOS_APP_DATA` when a frontend gave one (rule 16), and a
  format that suits the unit: **at most 480p, H.264 + AAC, joined into an MP4 without re-encoding** (`bv*[height<=480][vcodec^=avc1]+ba[acodec^=mp4a]/b[height<=480][ext=mp4]/...`, last alternatives take
  whatever the site has). The screen is 640x480 and the CPU has no video decoder. A frontend's own `-f`, `-S` or `-o` replaces them.
- **No self-update.** `yt-dlp -U` and `--update-to` stop with exit 2 and say `r3i6nos-pkg update` (the folder is part of a signed package and read-only). See "Updating".
- **Data and rule 2.** The tools write only where told: `R3I6NOS_APP_DATA` (a frontend sets it, `docs/APP-DATA.md`), `R3I6NOS_YTDLP_CACHE` for yt-dlp's cache, else the user's cache. Nothing downloaded is in the
  repo, CI or a release. What a person downloads is theirs to answer for (a site's terms apply); the packages do nothing until a person or an app they chose runs them (rule 6).
- **Certificates:** yt-dlp uses Python's `ssl` and the image's CA store; the wrappers point `requests` (gallery-dl, streamlink) at `/etc/ssl/certs/ca-certificates.crt`.

## Recipes for a frontend

```sh
# download into the app's data folder (rule 16), then play the file
R3I6NOS_APP_DATA="$DATA" yt-dlp -o '%(id)s.%(ext)s' --print after_move:filepath "$URL"

# stream: yt-dlp is the network, ffmpeg reads the pipe (a progressive, streamable MP4: format 18 on YouTube)
yt-dlp -q -f 'b[height<=480][ext=mp4]/18' -o - "$URL" | ffmpeg -i pipe:0 ...

# what is on offer (JSON on stdout), without downloading
yt-dlp -J --no-playlist "$URL"

# a gallery into the app's folder; a live or HLS stream saved
R3I6NOS_APP_DATA="$DATA" gallery-dl "$URL"
streamlink --quiet "$URL" best -o "$DATA/live.ts"
```

Three limits a frontend must design for (all measured):

1. **Streaming needs a streamable file.** A pipe is not seekable: an MP4 with its index at the end does **not** play from a pipe (the test shows it failing), and **yt-dlp cannot merge a separate video
   and audio stream to stdout**. Stream only progressive formats (`-f b`, or `18` on YouTube), or download first and play the file, which is the only way to get better than 360p.
2. **A cold YouTube extraction costs about 4.5 s of CPU and 211 MiB of memory** on an Apple-silicon core, almost all of it QuickJS solving the challenge, and the solved result is **not cached between runs**
   (6 s wall for one video). **On the unit this is an estimate, not a measurement**: the R36S's Cortex-A35 cores are several times slower, so expect tens of seconds, and 211 MiB is a fifth of the RAM. So: one
   `yt-dlp` process per video (extract and download together, not `-J` and then a second run), show a "Getting the video address" state with a progress bar (the fast-start rule), and never run it at
   boot or in the background.
3. **Sites break.** yt-dlp fixes arrive within days of a change; a frozen package stops working. See "Updating".

## Updating

yt-dlp is the part that goes stale, so the build is arranged to make a new release cheap, and a **weekly CI job tells you when one is out**: `.github/workflows/streamtools-fresh.yml` runs `scripts/check-streamtools-fresh.sh` every Monday and **fails when the pin in `pins.conf` is behind yt-dlp's latest release** (which is how GitHub emails the maintainer); the log has the lines to paste and the rebuild commands. It changes nothing. Tested against a fake GitHub (`tests/test_streamtools.sh`: current, newer, older, a bad tag, an unreachable API); run once against the real API on 2026-11-19, it said `2026.10.26` is the latest. **The workflow itself has never run on GitHub.**

1. `scripts/build-streamtools.sh --latest` prints the newest yt-dlp release, its zipapp's sha256 (from the release's own `SHA2-256SUMS`) and the newest versions of the Python packages; nothing is changed.
2. Edit `tools/streamtools/pins.conf` (the hash is checked, so read what changed first), run `scripts/build-streamtools.sh --relock` to rewrite `lock/*.txt`, then `scripts/build-streamtools.sh` and
   `tests/test_streamtools_docker.sh` (and `R3I6NOS_LIVE=1` for one real request).
3. Publish the new `r3i6nos-ytdlp.deb` in the signed catalog; a unit gets it with `r3i6nos-pkg update` (`docs/PACKAGES-OTA.md`), or send it with `scripts/push-packages.sh`.

The build is **reproducible**: two builds of the same pins give byte-identical `.deb` files (a fixed `SOURCE_DATE_EPOCH`; checked 2026-11-19).

## ffmpeg-min: what yt-dlp needed from it

yt-dlp downloads the better qualities as separate video and audio streams and asks ffmpeg to join them with `-c copy`. **`ffmpeg-min` had only the MP4 muxer, so it could join H.264 + AAC but not
VP9 + Opus (WebM) or anything into Matroska** (the older build fails with `Invalid argument`, shown by the test with `R3I6NOS_BASELINE_FFMPEG`). Added on 2026-11-19: the **`matroska`, `webm` and `ipod`
(M4A) muxers**. The cost was **zero bytes** at the file level (5,645,336 bytes before and after: the code fits in the binary's alignment padding), every earlier decode check still passes, and `tools/ffmpeg-min/build.sh`
now checks the merges yt-dlp makes (H.264 + AAC into MP4 and MKV, VP9 + Opus into WebM and MKV, ADTS AAC into M4A through `aac_adtstoasc`, which was already built in). The image's ffmpeg gets it at its next rebuild
(`scripts/build-ffmpeg-min.sh`, then an image; CI's `ffmpeg` job does it).

## What the tests show (Debian 12 arm64 under Docker, a clean container with only the image's Python packages)

| | |
|---|---|
| Install | the three `.deb` files through the real `deb_watcher.py`; the commands appear on the tools path; nothing is compiled at run time |
| yt-dlp | version, 1,755 extractor lines, `-U` refused, QuickJS-ng 0.18.0 and `yt_dlp_ejs` 0.8.0 and pycryptodomex found; a direct MP4; **DASH with separate video and audio joined into an MP4 and, with VP9 + Opus, into a WebM, by ffmpeg-min**; plain and **AES-128 HLS** (decrypted natively by pycryptodomex, not the "extremely slow" pure-Python path); a streamable MP4 played from a pipe; the file and the cache under `R3I6NOS_APP_DATA` |
| gallery-dl | version, 3,894 extractors, a picture downloaded, into `R3I6NOS_APP_DATA/gallery-dl` |
| streamlink | version, 136 plugins, an HLS playlist saved and decoded by ffmpeg-min, AES-128 HLS, language lookups without the removed translations, lxml and pycryptodome import |
| One real request | **YouTube, one public video, metadata only**: QuickJS-ng ran the challenge solver, 25 formats listed, the default selection is format 18 (320x240 H.264 + AAC); format 18 streamed through a pipe into `ffmpeg-min` and decoded |

Speed and memory in the same container, **on an Apple-silicon core, not a Cortex-A35**: yt-dlp `--version` 0.13 s and 33 MiB; a direct download 0.45 s and 46 MiB; a DASH join 0.50 s; gallery-dl
`--version` 0.04 s and 20 MiB, a download 0.24 s and 39 MiB; streamlink `--version` 0.12 s and 34 MiB, an HLS save 0.22 s and 48 MiB. **None of this is measured on the unit.**

A note for anyone repeating the live test in Docker (Colima): the VM's resolver never answers IPv6 lookups, so glibc waits 4 s on every new host and a YouTube extraction took 26 s. The test sets glibc's
`options no-aaaa` in its own container (`/etc/resolv.conf`) for the timings; it says nothing about yt-dlp or the unit.

Decoding what yt-dlp fetched (same container, a 19 s clip): AAC audio 0.01 s, 240p H.264 scaled to 640x400 BGRA 0.5 s of CPU. Whether the unit decodes **480p** in real time, and whether the VPU is worth using, is `docs/VIDEO-DECODE.md` and ROADMAP section 35.

## Not done, not tried

- **Anything on a real R36S:** start-up and extraction time on the Cortex-A35, memory with 1 GB of RAM, the Wi-Fi path, the install through `r3i6nos-pkg`. A device report section (rule 9) belongs to the first frontend, since
  these are tools, not apps; `== runtimes ==` in `r3i6nos-report` is the model if the maintainer wants the unit to check them.
- **Other sites than YouTube**, and what yt-dlp does when YouTube asks for a PO token (this video needed none). Sites that need a login or cookies were not tried.
- **Reusing a solved address:** `yt-dlp -J` gives direct media URLs that a second process could download without solving the challenge again; not tried (they expire, and some need headers).
- **Not packaged:** a frontend (no app uses these yet), a Settings screen for them, a deno or node runtime (QuickJS is the only one that fits), `curl_cffi` (browser impersonation), `websockets`, `brotli`, `mutagen`.
- **A faster QuickJS or a trimmed pycryptodomex:** the 7 MiB of pycryptodomex could go (yt-dlp then decrypts AES-128 HLS in pure Python, "extremely slow") and `.py` files could be dropped next to the byte code
  (about 3 MiB of the zipapp); neither is done, both are small and reversible.
