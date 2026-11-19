# R36S Video Streaming & VPU Optimization Guide (`yt-dlp` & Rockchip RK3326)

## Core Findings

* **GPU vs. VPU vs. RGA:** On the R36S’s **Rockchip RK3326** SoC, the **ARM Mali-G31 MP2** GPU handles 3D rendering and compositing, while the dedicated **VPU (Video Processing Unit)** handles video decoding and the **RGA (Raster Graphic Acceleration)** 2D engine handles scaling and color-space conversion (YUV to RGB). Hardware video decoding relies on the VPU, not the GPU.
* **Codec Mismatch with YouTube:** The RK3326 VPU accelerates **H.264 (AVC)**, **H.265 (HEVC)**, **VP8**, and **MPEG-1/2/4** (up to 1080p @ 60fps), but **does not support VP9 or AV1**. Because YouTube defaults to VP9 or AV1 for most modern streams, `yt-dlp` must be constrained to H.264 (`avc1`) at 480p or 720p to match the R36S’s 640×480 screen and prevent high CPU usage from software decoding.
* **Kernel & Userspace Driver Stack:** Hardware acceleration depends on the underlying Linux kernel and media player build:
  * **BSP kernels** (e.g., Rockchip 4.4 / 5.10 used by ArkOS) rely on Rockchip’s **`mpp` (Media Process Platform)** library (`/dev/mpp_service`) and require `ffmpeg` / `mpv` compiled with `--enable-rkmpp`.
  * **Mainline Linux kernels** expose the open-source **`hantro` V4L2 stateless decoder** (`/dev/video*`), accessed via `v4l2m2m` / V4L2-request in `ffmpeg` and `mpv`.
* **Stream Pipeline & Audio-Only Playback:** Use `yt-dlp` to select or extract H.264 stream URLs (`-g` or via `mpv --ytdl-format=...`), keep `yt-dlp` updated as YouTube updates its extraction logic, and pass `--no-video` when audio-only playback is sufficient (which runs easily on the quad-core Cortex-A35 CPU with minimal load).

---

## Recommended Command & Verification Steps

### Recommended `mpv` + `yt-dlp` Command (Force H.264 $\le$ 720p)

```bash
mpv --ytdl-format="bestvideo[vcodec^=avc1][height<=720]+bestaudio[acodec^=mp4a]/best[vcodec^=avc1][height<=720]" "https://www.youtube.com/watch?v=VIDEO_ID"
```

### Verification Checklist

1. **Check Available FFmpeg Hardware Accelerators:**
   ```bash
   ffmpeg -hwaccels
   ```
   *Expected result:* Lists `rkmpp`, `drm`, or `v4l2m2m` if compiled with hardware acceleration support.

2. **Inspect Kernel VPU / Media Device Nodes:**
   ```bash
   ls -l /dev/mpp_service /dev/video* /dev/rga 2>/dev/null
   ```
   *Expected result:* Relevant device nodes exist and have read/write permissions for your user.

3. **Verify H.264 Stream Selection in `yt-dlp`:**
   ```bash
   yt-dlp -f "bestvideo[vcodec^=avc1][height<=720]+bestaudio[acodec^=mp4a]/best[vcodec^=avc1][height<=720]" -g "https://www.youtube.com/watch?v=VIDEO_ID"
   ```
   *Expected result:* Returns direct stream URLs for H.264/AAC without falling back to VP9 (`vp9`) or AV1 (`av01`).

4. **Confirm Hardware Decode During Playback:**
   Run `mpv` or `ffmpeg` with verbose/debug logging enabled and verify that logs initialize hardware decoding (`rkmpp` or V4L2) rather than software decoding, with noticeably lower CPU usage than VP9/AV1 playback.

---

## Sources to Check

* **FFmpeg Hardware Acceleration Wiki (`HWAccelIntro`):** [https://trac.ffmpeg.org/wiki/HWAccelIntro](https://trac.ffmpeg.org/wiki/HWAccelIntro) — Reference for `ffmpeg -hwaccels` and hardware acceleration flags.
* **Jellyfin Rockchip VPU Documentation:** [https://jellyfin.org/docs/general/post-install/transcoding/hardware-acceleration/rockchip/](https://jellyfin.org/docs/general/post-install/transcoding/hardware-acceleration/rockchip/) — Notes on Rockchip MPP, RGA, device nodes, and codec support across Rockchip SoCs.
* **Mainline Kernel vs. BSP VPU Status (`flipperone-linux-build-scripts` Issue #13):** [https://github.com/flipperdevices/flipperone-linux-build-scripts/issues/13](https://github.com/flipperdevices/flipperone-linux-build-scripts/issues/13) — Context on BSP MPP vs. mainline V4L2/Hantro video decoding and VP9/AV1 limitations.
* **Armbian Forum Thread (`ffmpeg with hardware accelerated encoding`):** [https://forum.armbian.com/topic/56058-ffmpeg-with-hardware-accelerated-encoding/](https://forum.armbian.com/topic/56058-ffmpeg-with-hardware-accelerated-encoding/) — Practical notes on Rockchip `ffmpeg` / MPP vs. `v4l2m2m` across kernel versions.
* **`yt-dlp` & `mpv` Integration Issue (`yt-dlp/yt-dlp` #6496):** [https://github.com/yt-dlp/yt-dlp/issues/6496](https://github.com/yt-dlp/yt-dlp/issues/6496) — Troubleshooting `yt-dlp` format flags (`-f` / `--ytdl-format`) with `mpv`.