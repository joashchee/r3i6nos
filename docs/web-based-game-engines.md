# Web-Based Game Engines

A practical list of free-to-use engines and frameworks for making games that run in a web browser, plus third-party tools that support them. “Free to use” includes open-source projects and proprietary tools with no royalty or license fee for typical web publishing.

## Browser-first 2D

| Engine / framework | Type | Free-to-use status | Best for | Third-party software that supports it |
|---|---|---|---|---|
| **Phaser** | 2D JavaScript/TypeScript framework | MIT open source; free | Canvas/WebGL 2D browser games | Tiled, TexturePacker, Spine, Aseprite, Matter.js, Poki SDK |
| **PixiJS** | 2D rendering library | Free, open source | High-performance 2D rendering and custom engines | Tiled, Spine, Adobe Animate exports, TexturePacker |
| **Kaplay** | 2D JavaScript game library | MIT open source; free | Small, fast 2D games with a friendly API | Tiled, Aseprite, Krita, Vite |
| **melonJS** | 2D HTML5 game engine | Free, open source | Tile-based 2D platformers and arcade games | Tiled, standard JS asset/audio tooling |
| **LittleJS** | Tiny 2D JavaScript engine | Free, open source | Minimal, lightweight browser games | Tiled, Aseprite, Blender, standard JS build tools |
| **microStudio** | Browser-based game creation environment | Free to use | Beginner-friendly 2D games made entirely in the browser | Built-in code, sprite, and map editors; web export |
| **Construct 3** | Visual, no-code/low-code browser editor | Free tier; paid subscriptions for larger projects | 2D games without heavy programming | Tiled, Spine, Adobe Animate, Audacity, Poki |
| **Stencyl** | Visual game creator | Free tier with limitations | 2D games with block-based logic | External art/audio assets; HTML5 export |

## 3D web engines

| Engine / framework | Type | Free-to-use status | Best for | Third-party software that supports it |
|---|---|---|---|---|
| **Three.js** | 3D rendering library | MIT open source; free | Custom 3D web experiences and games | Blender, glTF exporters, Substance 3D, Mixamo, Rapier, Cannon-es |
| **Babylon.js** | Full 3D web engine | Apache-2.0 open source; free | Feature-rich 3D browser games | Blender, glTF/GLB, Rapier, Ammo.js, Havok, Poki |
| **PlayCanvas** | 3D engine and browser editor | MIT open-source engine; cloud editor has free and paid tiers | Collaborative browser-based 3D games | Blender, glTF, Spine, Tiled, Poki |
| **A-Frame** | WebXR / 3D framework | MIT open source; free | VR and AR experiences in the browser | Three.js ecosystem, Blender glTF assets, WebXR headsets |
| **Wonderland Engine** | WebXR-focused 3D engine | Free tier available | Performance-focused browser VR/AR | Blender, glTF assets, WebXR hardware |
| **Cocos Creator** | 2D/3D engine and editor | Free to use; no royalty or license fee for released games | Cross-platform games including web/mini-game targets | Blender, Spine, Tiled, TexturePacker, Cocos Creator editor |
| **LayaAir** | 2D/3D web engine | Free engine with commercial licensing considerations | WebGL games, especially in Chinese markets | TypeScript/JavaScript workflows, glTF assets, web deployment |
| **Turbulenz** | HTML5 2D/3D framework | Free, open source | Older but still relevant browser-game projects | Standard JavaScript tooling and HTML5 asset workflows |

## General engines with web export

| Engine | Type | Free-to-use status | Web support | Third-party software that supports it |
|---|---|---|---|---|
| **Godot** | 2D/3D engine and editor | MIT open source; free | Exports HTML5/Web builds | Blender, Krita, Aseprite, Tiled, Spine, Audacity, FMOD, Poki |
| **Defold** | 2D-focused engine with editor | Completely free to use | Exports HTML5 | Tiled, Spine, Aseprite, Blender, Poki |
| **GDevelop** | No-code/low-code 2D/3D engine | MIT open source; free | Publishes HTML5 web games | Tiled, Piskel, Aseprite, Krita, Blender, browser publishing |
| **Unity** | 2D/3D engine and editor | Free personal tier subject to Unity’s eligibility terms | WebGL builds | Blender, Maya, 3ds Max, Substance 3D, Spine, Tiled, FMOD/Wwise, Photon, Poki |
| **GameMaker** | 2D-focused engine and editor | Free tier; paid tiers for some export targets | HTML5 export availability depends on licence tier | Aseprite, Spine, Tiled, GameMaker IDE, Poki |
| **Solar2D** | 2D Lua engine | Free, open source | HTML5 builds | Tiled, Spine, Aseprite, Lua plugins |
| **Ebitengine** | 2D Go game library | Apache-2.0 open source; free | WebAssembly/browser deployment | Aseprite, Tiled, Go tooling, asset loaders |
| **HaxeFlixel** | 2D Haxe game framework | MIT open source; free | Compiles to HTML5 | Tiled, LDtk, Aseprite, Spine, Haxe tooling |
| **Castle Game Engine** | 2D/3D Pascal engine | Free, open source | Browser/WebGL deployment | Blender, glTF, Pascal/Lazarus tooling |

## Useful third-party tools

- **Blender** — free 3D modelling, animation, and glTF export; broadly compatible with Three.js, Babylon.js, PlayCanvas, Godot, Unity, and Cocos Creator.
- **Tiled** — free/open-source 2D tilemap editor; especially strong with Phaser, melonJS, Godot, Defold, GDevelop, and PixiJS.
- **Aseprite** — paid pixel-art editor with a free source-available build option; widely used for sprite assets.
- **Krita** — free open-source painting and 2D art tool.
- **Spine** — paid 2D skeletal-animation tool with runtime support across many engines, including Phaser, PixiJS, Godot, Defold, Unity, and Cocos Creator.
- **TexturePacker** — paid sprite-atlas and optimization tool; useful for Phaser, PixiJS, Cocos Creator, and Unity.
- **Audacity** — free open-source audio editor for sound effects and music.
- **Poki SDK** — free developer SDK for publishing and monetizing HTML5 games on Poki; supports many major web-game engines.

## Quick picks

- **Easiest browser-native 2D:** Phaser, GDevelop, or Construct 3.
- **Best free 3D web engine:** Babylon.js or Three.js; Babylon.js for more built-in engine features, Three.js for maximum control.
- **Best free editor-based 2D/3D option:** Godot or Defold.
- **Best for VR/AR in the browser:** A-Frame or Wonderland Engine.
- **Best for small web builds:** Defold.
