# tools/ffmpeg-min: a minimal ffmpeg for the R36S (GPL-2.0-or-later)

Why: the firmware image has no ffmpeg, and Debian's own package costs **161.2 MiB** on the rootfs (108 packages; measured 2026-11-11). Commander's Play tab
(`apps/commander-native/player.h`) needs one only to decode video and the sound formats it cannot read itself. This is the same program built with
`--disable-everything` and only what the player, Rec demo's encode and Diskette's audio and video lists use: **one stripped binary, 5,579,800 bytes (5.32 MiB;
5,582,848 on a 4 KiB-block disk), linking only libc and libm** (x264 and zlib are linked in statically). Tier A alone was 4,465,016 bytes; Tier B and the
stills add 1,114,784. `scripts/configure_rootfs.sh` installs it in the image as `/usr/bin/ffmpeg` with the GPL text, NOTICE and this build script (see "In the image"); no image has been built with it yet (ROADMAP section 20).

## Build and check

`scripts/build-ffmpeg-min.sh` (no arguments, Docker only) runs `tools/ffmpeg-min/build.sh` as root in `debian:bookworm` arm64 and leaves
`build/ffmpeg-min/ffmpeg`. The script:

1. downloads `ffmpeg-5.1.6.tar.xz` and **fails unless its SHA-256 is `f4fa066278f7a47feab316fef905f4db0d5e9b589451949740f83972b30901bd`**, and x264
   (stable, commit `baee400fa9ced6f5481a728138fed6e867b0ff7f`, build 164, the one Debian 12 ships) the same way, SHA-256
   `ce6623b8b289765daee04a297c2fd1a293cb2565a1749c76d66c8d72c7ddc1ab`;
2. builds x264 (static, 8-bit 4:2:0 only, no command line), then ffmpeg, and strips;
3. fails if anything but libc and libm is linked, or if a decoder asked for is missing from the binary (configure leaves out a misspelt name silently);
4. makes a file with Debian's **full** ffmpeg for each case below, decodes it with both the full and the minimal one **the way Commander's player does**
   (video: `-an -vf scale=640:400...,pad,fps=30 -f rawvideo -pix_fmt bgra pipe:1`; audio: `-vn -f s16le -ar 44100 -ac 2 pipe:1`) and **compares the bytes**.

**Licence and source: GPL-2.0-or-later** (`--enable-gpl --enable-libx264`; x264 is GPL-2.0-or-later, which makes the whole binary GPL; the maintainer accepted
this on 2026-11-11; no `--enable-nonfree`). Source: the two pinned tarballs above (ffmpeg.org and code.videolan.org), built with the flags in `build.sh`; the image
must ship that offer of source (NOTICE has the entry). Debian's own package is not used in the image; it is only the reference the checks compare with.

## In the image

- **`scripts/configure_rootfs.sh`** copies `build/ffmpeg-min/ffmpeg` (`R3I6NOS_FFMPEG_BUILD` names another folder) to `/usr/bin/ffmpeg`, mode 755. Without the binary it
  warns (the image then has no ffmpeg: Commander shows only Info for video, WebP, TIFF and PSD, and Rec demo cannot make its `.mp4`) and does not fail.
- **The GPL's terms ship with it** in `/usr/share/ffmpeg-min/`: `GPL-2` (from the rootfs's own `base-files`, else the build machine's), `NOTICE` (both sources' URLs and
  hashes), `build.sh` and `README.md` (the configure flags). `/usr/share/doc/ffmpeg-min/copyright` points at them; it is the only file `configure_rootfs.sh`'s trim of
  `/usr/share/doc` leaves, which is why the rest is not under `doc/`. `tests/test_configure_rootfs.sh` checks all of it, and that a missing binary leaves no stray licence files.
- **CI:** the `ffmpeg` job of `.github/workflows/build.yml` runs `tools/ffmpeg-min/build.sh` in a Debian 12 arm64 container on an arm64 runner (`ubuntu-24.04-arm`, or the
  label in the repository variable `R3I6NOS_ARM_RUNNER`; on an x86 runner it runs under qemu, which takes much longer), cached on everything that goes into it, and uploads
  `ffmpeg-min-arm64`; the `rootfs` job unpacks it before `build_rootfs.sh` and smoke-tests it under qemu against the rootfs's own libc. **That job has never run on
  GitHub**; whether the arm64 runner is available to this repository is not known.
- Locally: `scripts/build-ffmpeg-min.sh`, then `sudo scripts/build_rootfs.sh` (Linux).

## What is in it (Tier A, Tier B and the stills, 2026-11-11)

| | |
|---|---|
| Containers | mp4/mov/m4a/3gp, Matroska/WebM, AVI, ASF (wmv/wma), FLV, MPEG program and transport streams (`mpegvideo` is there because a program stream identifies its video by probing it), WAV, MP3, Ogg, FLAC, AIFF, ADTS AAC, WavPack, raw AC-3, E-AC-3 and DTS |
| Video decoders | H.264, HEVC, MPEG-1/2/4, VP8, VP9, WMV1/2/3, VC-1, MJPEG, MS-MPEG4 v1/2/3, H.263, FLV1, VP6, Cinepak, MS Video 1, QuickTime RLE, SVQ1, SVQ3, ProRes |
| Audio decoders | AAC (and LATM), MP1, MP2, MP3, AC-3, E-AC-3, DTS, FLAC, Vorbis, Opus, WavPack, ALAC, WMA v1/v2/Pro, PCM (8, 16, 24, 32 bit and float), MS and IMA ADPCM |
| Stills | WebP (lossy, lossless, alpha), TIFF (uncompressed, LZW, deflate, PackBits) and PSD (raw and RLE) decoders; zlib is linked in for TIFF's deflate. **JPEG too, recognised by its content** (`image_jpeg_pipe`, so a file with no or a wrong extension decodes): Commander draws a **progressive** JPEG through it, which its own decoder does not. Arithmetic-coded JPEG decodes in neither (only libjpeg-turbo reads it) |
| Encoding (Tier B) | **libx264** (`-preset veryfast -crf 18 -tune animation -pix_fmt yuv420p`) and the native **AAC** encoder into an **MP4** (`-movflags +faststart`), from raw BGRA frames on a pipe (`rawvideo` demuxer and decoder) and a WAV: exactly what `r3_demo_encode_p` in `theme/r3i6nos_demo.h` runs |
| Other output | rawvideo and 16-bit PCM, through the filters `scale fps pad format aresample aformat null anull setpts` |

**Checked (36 cases, byte-identical to the full ffmpeg on 2026-11-11, in Debian 12 arm64 under Docker):** H.264 (mp4), MPEG-4 (avi), MS-MPEG4 v2 and v3 (avi), MJPEG
(avi, mov), H.263 (mov), FLV1 (flv), Cinepak, MS Video 1 (avi), QuickTime RLE, SVQ1, ProRes (mov), WMV1 and WMV2 (asf), MPEG-1 (mpg), MPEG-2 (ts), VP8, VP9
(webm), HEVC (mkv); MP2 (mp2, ts), AC-3 (ac3, mkv), E-AC-3, DTS (mkv), WMA v1 and v2 (asf), AAC (m4a), MP3, Opus, Vorbis (ogg), FLAC, WAV, AIFF, WavPack.

**Stills (byte-identical to the full ffmpeg):** JPEG baseline, progressive (also with no extension), and a progressive one decodes to the same pixels as the baseline file it was made from; WebP lossy, lossless and with alpha; TIFF uncompressed, LZW, deflate and PackBits; a hand-made raw-RGB PSD (no ffmpeg
encoder exists for PSD; RLE-compressed PSDs have not been tried). Run end to end: Commander (built with GCC in Debian 12) draws a real WebP, TIFF and PSD
through this binary (`--view FILE --screenshot`, 221,184 pixels of the picture's colour each).

**Rec demo's encode, checked:** raw 640x480 BGRA frames plus a WAV in, through the real command line, give an MP4 that ffprobe reads as H.264 yuv420p 640x480
30 fps with 90 frames, AAC stereo 44.1 kHz; the full and the minimal ffmpeg decode it to the same bytes; PSNR against the source frames 39.3 dB (crf 18); and
`tests/test_demo.c` passes with this binary as the encoder.

**In the binary but never decoded a file** (the reference ffmpeg has no encoder to make one, so there was no sample): WMV3, VC-1, SVQ3, VP6, WMA Pro.
Bring a real file for each before relying on them.

**Not checked on the unit:** that the unit decodes 640x400 at 30 fps in time (H.264 and HEVC in software on four Cortex-A35 cores), **how long the encode takes
there (a 30-90 s Rec demo video: unknown; the checks above ran on an Apple-silicon VM, which says nothing about it)**, memory while decoding and encoding, and
that the binary runs on the image's own libc. The build is for arm64 Debian 12, the same as the image.

## What is not in it (ROADMAP section 20)

Any encoder but libx264, AAC, raw video and PCM; AV1; HEIC and AVIF stills; JPEG 2000 and other still formats; AMR; subtitles; metadata (`ffprobe`); and every
network protocol.
