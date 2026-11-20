# Video decode on the R36S: software, or the Rockchip VPU? (review of `r36s_streaming_vpu_optimization_guide.md`, 2026-11-19)

The guide (written by Gemini, saved in `docs/r36s_streaming_vpu_optimization_guide.md`) says: the RK3326's VPU, not the GPU, decodes video; it cannot do VP9 or AV1, so constrain
`yt-dlp` to H.264; use `mpv` with `rkmpp` (vendor kernel) or V4L2 (mainline); play audio-only when that is enough. This is the check of each claim against the datasheet, the stock card's own
files, Debian 12's package list and this repo's measurements. **Nothing here has run on a real R36S.**

## Verdict, claim by claim

| Claim | Result |
|---|---|
| VPU decodes, not the GPU; the RGA does scaling and colour conversion | **Right**, and present on the unit: the stock DTB has `vpu_service@ff442000`, `vpu_combo` and `rk_rga@ff480000` (`rockchip,rga2`) |
| The VPU decodes H.264, H.265, VP8, MPEG-1/2/4 up to 1080p60, **no VP9, no AV1** | **Mostly right, now from the primary source** (RK3326 datasheet V1.4, January 2019, `docs/r36s-chips.md` links it): decode **MPEG-4 (ISO/IEC 14496-2), H.264 BP/MP/HP to level 4.2, H.265 Main10 to level 4.2, VP8, VC-1**, all to 1080p60, plus MVC and JPEG; **no VP9, no AV1, and no MPEG-1/2 decode** (the guide's "MPEG-1/2/4" is wrong for the hardware). **Encode is H.264 only** (BP/MP/HP level 4.1, 1080p30) and JPEG. The guide omits VC-1 and encode |
| So pin `yt-dlp` to H.264 (`avc1`) | **Right, and already done** (`docs/STREAM-TOOLS.md`: the wrapper's default is at most 480p H.264 + AAC) |
| ...at **480p or 720p** | **Wrong for this screen.** The panel is 640x480; 720p decodes three times the pixels (`1280x720` is 921,600, `640x480` is 307,200) only to throw them away when scaling down. The default stays 480p; 360p (format 18) is the battery option |
| BSP kernels use Rockchip's `mpp` (`/dev/mpp_service`); mainline uses Hantro V4L2 | **Right in outline.** The stock 4.4.189 kernel ships `mpp_service.ko`, `rk-vcodec.ko`, `mpp_dev_vepu.ko`, `mpp_dev_rkvenc.ko`, `mpp_dev_h265e.ko` and `rga2.ko`. Mainline Hantro VDPU2 H.264 for `rockchip,px30-vpu` was posted in 2021 (Ezequiel Garcia's series); **which release merged it is not confirmed**, and VDPU2 is H.264, MPEG-2 and VP8, so H.265 is probably BSP-only (unverified) |
| Use `mpv --ytdl-format=...` | **Does not fit.** `mpv` is not in the image: a minimal install is **211 packages** (`apt-get -s install --no-install-recommends mpv`, Debian 12). The player here is `ffmpeg-min`, which has **no network** (`-g` URLs cannot be opened), so yt-dlp feeds it through a pipe, and a pipe takes one progressive stream (no separate video and audio) |
| `ffmpeg -hwaccels` / `rkmpp` | **Not available.** Debian 12 has no `librockchip-mpp`, `librga` or rkmpp ffmpeg (`apt-cache policy` empty); `ffmpeg-min` is built with `--disable-everything`, so it lists none. rkmpp lives only in forks of ffmpeg and needs Rockchip's MPP library, whose version must suit the kernel's `mpp_service` (a known pain; not checked here) |
| Verify the device nodes | **Good idea, not yet possible**: nothing in the repo loads these modules (`modules.alias` has no entry for them, no `modprobe`), and no report has listed `/dev`. See step 1 |
| `--no-video` / audio-only is easy on the Cortex-A35 | **Right, and measured**: see below |

## Measured here (Debian 12 arm64 under Docker on an Apple core, **not a Cortex-A35**, 2026-11-19)

One short public YouTube video (19 s), downloaded by the packaged yt-dlp, decoded by `ffmpeg-min`:

| | wall | CPU | memory |
|---|---|---|---|
| Audio, format 140 (AAC), 19 s to 16-bit stereo PCM | 0.01 s | 0.01 s | 9 MiB |
| Video, format 18 (320x240 H.264), 19 s scaled to 640x400 BGRA at 30 fps, as Commander's player does | 0.22 s | 0.50 s | 31 MiB |

Audio is effectively free. The 240p video costs about 2.6% of real time in CPU here. **For 480p, about four times the pixels, a straight extrapolation is ~10% of a core on this machine; the unit's
cores are several times slower, so software decode of 480p H.264 may or may not keep up: that is the number to measure** (ROADMAP section 20's open item and section 35 step 1). The clip is 240p, so this is
not a 480p measurement.

## The probe (written 2026-11-19; **built and tested against fakes and in Debian 12 arm64, never run on the unit**)

`r3i6nos-report` (Settings > Device reports > r3i6nos) has two new sections, read-only and with no root:

- **`== video hardware ==`**: the device-tree nodes named vpu, vcodec, rkvdec, hevc, vepu, rga or mpp with their `compatible` and `status`; the `/dev` nodes (`mpp_service`, `vpu_service`, `rga`, `ion`, `video*`,
  `media*`, `dma_heap`, `dri`) and whether this user can open them; `/proc/devices`; every VPU or RGA module as loaded, built in, or **a file on disk that nothing loads** (and whether `modules.alias` gives it a
  modalias, so udev could load it by itself); the kernel config options when one is readable; `librockchip_mpp`, `librga` and V4L2 libraries from `ldconfig -p`; `ffmpeg -hwaccels`; and the **DRAM**: the device tree's
  size, the devfreq `dmc` frequencies and the kernel-log lines that mention ddr, dram, dmc, vpu, rga or mpp. It ends in a **verdict** line (usable node: PASS; modules on disk but not loaded; hardware in the device tree
  but no node; nothing found). It never loads a module: that needs root, and nothing here should change the unit's state.
- **`== video decode ==`**: the software-decode benchmark. It makes a synthetic H.264 clip with `ffmpeg-min` (90 frames at 30 fps, a scrolling ramp under a noise screen, at a target bit rate of about what YouTube
  serves: 400, 700, 1,000 and 2,500 kbit/s for 240p, 360p, 480p and 720p), decodes each the way Commander's player does (scaled to 640x400, BGRA, 30 fps, to a pipe), best of two, and reports **times real time, wall, CPU,
  the share of all four cores and the child's own peak memory**. **480p has a goal of at least 1.5x real time** (FAIL when missed; a goal to adjust from the first report); the other sizes are INFO. It runs on **run 1 of 3
  only** (it loads every core). Synthetic content is not footage: the clip's real bit rate is printed beside each result, and a real 480p file is still the better test.

On an Apple core in Debian 12 arm64 the section ran 21x to 63x real time (480p: 37x at 907 kbit/s); **the unit's number is the one that matters**, and nothing says yet what it will be.

Commander's report now measures this on the unit (`== playback ==`, `docs/PLAYBACK.md`, 2026-11-20): the player's command, one change at a time, the cores busy, the YUV-against-BGRA path and the whole Player on a real clock.

## What is worth pursuing, in order

1. **Measure software decode on the unit first** (already open: ROADMAP section 20, "does H.264 at 640x400 and 30 fps keep up"). If 480p keeps up with CPU to spare, there is nothing to optimise, and
   the whole hardware path below is unnecessary. Use a 480p H.264 file of the same kind YouTube serves.
2. **The `== video hardware ==` section of `r3i6nos-report`** (rule 9): written (above). It does not run `modprobe` (root), so "would loading them work" is the step after it: `sudo modprobe mpp_service rk-vcodec rga2`, then run the report again.
3. **Audio-only mode in the first frontend**: `yt-dlp -f 'ba[acodec^=mp4a]'` (format 140) then `ffmpeg`/nttplay. Cheapest in CPU, data and battery; the JavaScript challenge solve (~4.5 s CPU on an Apple core) still happens.
4. **Keep 480p as the cap**, 360p as the saver. Do not adopt the guide's 720p.

## What is not worth pursuing now

- **`mpv`**: 211 packages, needs network-capable ffmpeg and TLS; replaces nothing that works.
- **An rkmpp ffmpeg or a `v4l2m2m` path**: new libraries to build (MPP is Apache-2.0, the ffmpeg fork GPL), a dynamic dependency where `ffmpeg-min` links only libc and libm, a kernel-and-userspace version match, and
  no zero-copy route into the Mali/SDL output (frames would come back to the CPU for upload, losing part of the gain). It also depends on the card's vendor modules (rule 2), and **the mainline-kernel decision
  (`docs/MAINLINE-KERNEL.md`) would replace this stack entirely** (Hantro V4L2, H.264 only). Revisit only if step 1 shows software decode is not enough **and** step 2 shows the nodes work.
- **Hardware encode for Rec demo** (the VEPU/RKVENC modules exist; H.264 only): Rec demo's libx264 encode took 64.7 s for a 53 s clip (TinyNTT's report, 2026-11-11), which is acceptable. Same prerequisites as above.

## Sources checked

- Rockchip RK3326 datasheet **V1.4** (53 pages, 2019-01-10), read directly: [rockchip.fr mirror](https://rockchip.fr/RK3326%20datasheet%20V1.4.pdf), section 1.2.6 "Video Decoder / Video Encoder" (confirmed earlier by [CNX Software's summary](https://www.cnx-software.com/2018/07/23/rockchip-rk3308-rk3326-datasheet/)).
- Mainline Hantro VDPU2 H.264 for PX30/RK3326: [Ezequiel Garcia, "hantro: Enable H.264 VDPU2", linux-rockchip, 2021-07](https://lists.infradead.org/pipermail/linux-rockchip/2021-July/026864.html) (tested on an Odroid Advance Go); merge release unconfirmed.
- Local: `local_hardware_assets/rootfs/lib/modules/4.4.189/modules.builtin` and `kernel/drivers/video/rockchip/{vpu,vcodec,rga2}`; the stock `rk3326-r35s-linux.dtb` strings; Debian 12 `apt-cache` in a container.
