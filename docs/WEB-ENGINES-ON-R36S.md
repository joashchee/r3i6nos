# Web-based game engines on the R36S: what can run offline (validated 2026-11-23)

Source list: `docs/web-based-game-engines.md` (Perplexity research, unedited). It lists engines a developer can *publish to a browser* with. This
document asks a different question: **which of those engines' games can run locally and offline on the R36S, and how hard is each?**
Checked against this repo (`docs/RUNTIMES.md`, `docs/hardware-findings.md`, ROADMAP section 33) and five web searches. **Nothing here was run on a unit
(rule 1); "plausible" means "reasoned from the stack, not tried".**

## The device, as it limits the choice

- No browser in the image; no X or Wayland; no desktop `libGL` (SDL 2.26.5 on KMSDRM; OpenGL ES 3.2 from the user's own Mali-G31 blob, rule 2).
- Kernel 4.4.189, Debian 12 arm64 userland (glibc 2.36), 1 GB of RAM, 640x480.
- What the image does have: SDL2, OpenAL, QuickJS (`tools/qjs-node`, KlamANSI), LÖVE, and the engine runtimes of ROADMAP section 33
  (Pyxel, Ren'Py, Solarus, AGS, Godot 3.6 FRT) packed as tool packages that need no root.

So a game runs only if (a) its engine has a **native arm64 runtime** that draws with SDL2 or GLES and needs no desktop GL or window server, or
(b) we build a **web runtime** (a browser, or a JS host with canvas, audio and input).

## What the research doc gets right and wrong

- **Right:** the "free to use" and licence column is broadly correct.
- **Wrong frame:** it never says whether a game *runs* offline on a handheld; browser-only engines (Phaser, Three.js, ...) need a browser this image lacks.
- **Stale or unchecked:** Turbulenz is believed discontinued (not checked). Unity's and GameMaker's free-tier terms change and were not verified.
  The "third-party software" column (Tiled, Spine, ...) has no bearing on running. "Best for small web builds: Defold" is about web size, not handheld fit.
  Solar2D's "HTML5 builds" is irrelevant here: it has no official Linux arm64 build (below).

## Ranking: easiest to hardest

Effort uses the roadmap's scale (S an hour or less, M a few hours to a day, L several days, XL a week or more), for the hands-on work once nothing blocks it.

| # | Engine | Route to the R36S | Effort | Validation |
|---|---|---|---|---|
| 1 | **Godot 3.6** | FRT runtime already built (`r3i6nos-runtime-godot`). Games are exported as a `.pck` (not the web export). | S | In this repo: sample `Hello_Godot.pck` runs under Mesa in Docker. **Untested on the Mali.** |
| 2 | **Godot 4.x** | Same recipe; PortMaster has a 4.3 build (23.5 MB). | M | Needs GLES3; the report shows the blob gives OpenGL ES 3.2, so plausible. Untested. |
| 3 | **Defold** | Defold 1.10 can bundle a Linux arm64 application. | M–L | [Forum](https://forum.defold.com/t/defold-retro-handhelds/79016): bundling confirmed; an earlier thread says the engine needs desktop OpenGL and X11, and a libdecor workaround is needed. The image has no `libGL`, so likely a custom engine build or gl4es. |
| 4 | **HaxeFlixel** | Haxe to native C++ through Lime, which uses SDL2. | M–L | From knowledge, **not searched**: matches the image's SDL2 + KMSDRM stack; Linux arm64 support unverified. |
| 5 | **Ebitengine** (Go) | Go cross-compiles to linux/arm64. | L | [Discussion](https://www.github.com/hajimehoshi/ebiten/discussions/2942): OpenGL ES on Linux "not well tested". Nothing found on KMS/DRM; probably needs X or Wayland. |
| 6 | **Castle Game Engine** | Free Pascal builds for aarch64. | L | Not verified; the window layer (GTK, X11 or SDL) is the open question. |
| 7 | **Solar2D** | Lua engine. | L–XL | No official arm64 build; only a [community cross-compile](https://forums.solar2d.com/t/building-a-docker-container/356439). We would build and maintain the engine. |
| 8 | **GameMaker, Unity** | Native Linux arm64 players. | XL | PortMaster's wiki says [GameMaker is not compatible by conventional methods](https://jantrueno.github.io/PortMaster-Wiki/contribute/porting/engines/gamemaker-studio). Unity's Linux arm64 SDK was not validated for a 4.4 kernel and 1 GB. |
| 9 | **GDevelop, Construct 3, Stencyl, microStudio, Cocos Creator, LayaAir** | Their desktop exports are Electron or webview wrappers. | XL | GDevelop's stock export does not target arm64 ([forum](https://forum.gdevelop.io/t/arm64-appimage-export-option/74357)); Electron on 1 GB, a 4.4 kernel and no X is unrealistic. |
| 10 | **Phaser, PixiJS, Kaplay, melonJS, LittleJS** (JS 2D) | An embedded browser (XL), or a new "mini web runtime": QuickJS + SDL2 with canvas, audio and input shims. | XL | QuickJS is in the image, but there is no DOM, Canvas2D or WebAudio. LittleJS and Kaplay are the smallest to shim. A new product. |
| 11 | **Three.js, Babylon.js, PlayCanvas, A-Frame, Wonderland** | As 10, plus WebGL and WebAudio shims. | XL+ | A Mali-G31 with 1 GB is a poor fit; WebXR has no headset to use. Not recommended. |
| — | **Turbulenz** | — | not worth it | Believed discontinued; not checked. |

## Takeaways

- Use the engines' **native arm64 exports**, not their web exports: Godot first, then Defold or HaxeFlixel.
- Running JS web engines offline is a separate runtime project (rank 10), and would need a decision on how it sits with the "no emulators" rule.
- Third-party games from these engines are exempt from rules 8-14 and 16; makers built on them are not.
- Nothing above is "works on the R36S" until a unit report says so.

## Sources

- [Defold, retro handhelds](https://forum.defold.com/t/defold-retro-handhelds/79016)
- [Solar2D arm64 Docker cross-compile](https://forums.solar2d.com/t/building-a-docker-container/356439)
- [Ebitengine OpenGL ES discussion](https://www.github.com/hajimehoshi/ebiten/discussions/2942)
- [GDevelop arm64 export request](https://forum.gdevelop.io/t/arm64-appimage-export-option/74357)
- [PortMaster wiki, GameMaker Studio](https://jantrueno.github.io/PortMaster-Wiki/contribute/porting/engines/gamemaker-studio)
