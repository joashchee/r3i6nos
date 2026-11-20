# Commander's playback report (`== playback ==`, 2026-11-20)

Rule 9's device report for Commander has a section that times **every way Commander plays something**, to find what to optimise and where the CPU cores, the GPU and the VPU
could be used. It runs from the main menu (Device report, asked first) or `commander --report`; `playback_report.h` (the ffmpeg half) and `commander.c` (`report_playback`, the SDL half)
write it into `r36s-report.txt`. Skip it with `R3I6NOS_PLAYBACK_BENCH=0`; `R3I6NOS_PLAYBACK_SECS` sets the clip's length (4 s), `R3I6NOS_PLAYBACK_CLIP` names a video, `R3I6NOS_PLAYBACK_PLAY_S` how long the whole-path test plays (6 s; set it and `R3I6NOS_PLAYBACK_SECS` to 600 for the drift check), `R3I6NOS_FM_PIPE_KB` the video pipe's size (the report's experiments; 1024 by default). **Built and tested in Debian 12 arm64 under Docker
(a dummy video and audio driver, ffmpeg-min); never run on a unit.**

## What it measures

| Path | Lines | What they answer |
|---|---|---|
| the machine | `cpu`, `hardware nodes`, `ffmpeg hwaccels`, `ffmpeg hardware decoders`, `h264 decoder threading` | cores online, governor and clock (a low governor slows every figure), whether `/dev/mpp_service`, `vpu_service`, `rga`, `dri` exist, whether this ffmpeg could reach the VPU at all |
| **video through ffmpeg** (the Play tab's `PL_VIDEO`) | `clip`, `app path`, `decoder threads 1/2/4`, `scale fast_bilinear`, `scale bilinear`, `skip loop filter`, `yuv420p, no scale, no BGRA`, `480p app path` (PASS at 1.5x real time) | the player's own command, then one change at a time, each as times real time, CPU, cores busy and **which cores**. The clip is a real MusiCue video (`<card>/MusiCue/video/*.mp4`) when there is one, else a synthetic 854x480 H.264 clip made by libx264 |
| the advice | `advice threads`, `advice pixel format`, `advice scaler`, `advice loop filter` | what the numbers say to change: whether the decode spreads over the cores, whether to ship YUV and let the GPU scale, whether bilinear is affordable |
| sound through ffmpeg | `ffmpeg audio decode` | AAC to PCM, and the cost of the second ffmpeg the player starts for the sound |
| sound through nttplay (`PL_NTT`) | `nttplay decode` | up to three MIDI or tracker files of the corpus, times real time |
| the sound device (`PL_WAV`, everything queued) | `sound device` | driver, the format and buffer the device itself takes, latency, whether SDL resamples |
| the picture to the screen | `renderer`, `screen frame`, `texture upload` | accelerated or software, vsync, the cost of a frame of the app's screen, and a BGRA frame against a YUV frame scaled by the renderer |
| sync over time | `sync drift` | the picture's lag behind the sound every 10 s of the whole-path play; FAIL when it rises by more than 100 ms (a rate mismatch, not a one-off lag) |
| the whole path | `whole path`, and the `playing` power window (rule 14) | the real Player on the clip for up to 6 s of the real clock: pictures drawn, how far the picture ended behind the sound (goal 250 ms), CPU, wake-ups, the draw in mW |

## What the first runs found (Debian 12 arm64, Docker, dummy drivers: **not the unit**)

- **A real bug, fixed in `player.h`: a video with no sound froze.** The clock was the sound's; with no sound it never advanced, so the picture stayed on frame 0. Once the sound is over (or never was) the wall clock goes on.
- **A real throughput limit, fixed in `player.h`: the video pipe held 64 KiB and a 640x400 BGRA frame is 1 MiB.** The player refilled it 16 times for every frame and could only move as fast as the UI loop came round: **26 frames in 4 s and the picture 3.1 s behind**. `pl_spawn` now asks for a 1 MiB pipe (`F_SETPIPE_SZ`; the unprivileged limit): **120 frames in 4 s, in step.** On the unit the cores are slower, so the gain there is the one to measure.
- **The sync drift the maintainer saw on the unit (video fine for the first minutes, then slipping, very noticeable past 10 minutes; reported 2026-11-20 on the build before this session) is reproduced by the same pipe limit, and gone with the fix.** A 60 s clip with AAC sound played with the old 64 KiB pipe: the picture was 7.4 s behind after 10 s and 44 s behind after 60 s (a steady rise: the supply of frames was slower than 30 a second, and the player never skips, so it never caught up). With the 1 MiB pipe over 120 s the lag stayed between -33 and -4 ms with no trend. **The unit's refill is faster than this machine's, so its deficit was smaller and took minutes to show, which fits; that this is what the unit did is not confirmed until its report's `sync drift` line is read.** Dropping late frames was tried and rejected: when the supply is the limit every frame is late, so it showed nothing.
- **Leaving the picture as YUV at its own size was 1.9x faster than scaling and converting to BGRA in ffmpeg** (49x against 26x real time) and the pipe carried 18 MB/s instead of 31. That is a change to try, not made: the player would take `yuv420p`, upload it with an SDL IYUV texture and let the renderer scale. **On a software renderer (this run) the YUV upload cost more (0.72 ms against 0.13 ms), so it only pays where the GPU takes the scaling: the unit's `texture upload` line decides.**
- Decoder threads: 4 threads were 1.7x the speed of 1 on this machine; ffmpeg's default (every core) is right. Bilinear costs 20% over neighbor here.

## What to do with a unit's report

1. `480p app path` FAIL or under ~3x: the picture will not keep up with the menu and the sound. Try the advice lines in order (YUV, then `skip loop filter` as a fallback, then cap at 360p in MusiCue's `VIDEO_FORMAT`).
2. `hardware nodes` present and `ffmpeg hardware decoders` none: the VPU is reachable from the kernel but not from this ffmpeg; the next step is `docs/VIDEO-DECODE.md`'s (a root modprobe, then an rkmpp-capable decoder), only if software decode is not enough.
3. `renderer` accelerated and `texture upload` YUV no dearer: move the scaling to the GPU (one change in `player.h` and the viewer's draw).
4. `sound device` not 44100 Hz: ask for the device's own rate, so SDL stops resampling.
5. The `playing` power window: the CPU and the draw while playing; the busiest other process should be ffmpeg and nothing else.
