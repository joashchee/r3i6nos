// The mirror page's script, run in node against a stub DOM: a full frame, a delta and an extended-cell datagram paint the cells they should, from
// R3i6nos Mono, pixel for pixel (the glyph rows in theme/font/r3i6nos-mono/r3i6nos-mono.json). Run: node tests/test_mirror_page.js
"use strict";
const fs = require("fs"), path = require("path");
const root = path.resolve(__dirname, "..");
const html = fs.readFileSync(path.join(root, "overlay/usr/share/r3i6nos/mirror/page.html"), "utf8");
const font = JSON.parse(fs.readFileSync(path.join(root, "theme/font/r3i6nos-mono/r3i6nos-mono.json"), "utf8"));
const script = html.split("<script>")[1].split("</script>")[0];

const painted = {};   // "x,y" -> Uint8ClampedArray of the last 10 x 20 block put there
const listeners = {};
const ctx = { createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4), width: w, height: h }),
              putImageData: (img, x, y) => { painted[x + "," + y] = Uint8ClampedArray.from(img.data); } };
const el = () => ({ getContext: () => ctx, textContent: "", hidden: false });
global.document = { getElementById: () => el() };
global.EventSource = function () { return { addEventListener: (n, f) => { listeners[n] = f; } }; };
global.window = {};
new Function(script)();    // the page's own code

let failed = 0;
function check(c, msg) { if (!c) { console.log("FAIL: " + msg); failed++; } }
const b64 = (bytes) => Buffer.from(bytes).toString("base64");
function expected(cp, fg, bg) {   // the font's 20 rows of 10 bits as RGB bytes
  const g = font.glyphs[font.cmap[String(cp)]], out = [];
  const RGB = [[0,0,0],[0,0,170],[0,170,0],[0,170,170],[170,0,0],[170,0,170],[170,85,0],[170,170,170],
               [85,85,85],[85,85,255],[85,255,85],[85,255,255],[255,85,85],[255,85,255],[255,255,85],[255,255,255]];
  for (let y = 0; y < 20; y++) {
    const row = parseInt(g.substr(y * 3, 3), 16);
    for (let x = 0; x < 10; x++) { const c = RGB[(row >> (9 - x)) & 1 ? fg : bg]; out.push(c[0], c[1], c[2], 255); }
  }
  return Uint8ClampedArray.from(out);
}
const same = (a, b) => a && b && a.length === b.length && a.every((v, i) => v === b[i]);
const at = (cell) => painted[((cell % 64) * 10) + "," + (Math.floor(cell / 64) * 20)];

const frame = new Uint8Array(64 * 24 * 2);
for (let i = 0; i < 64 * 24; i++) { frame[2 * i] = 32; frame[2 * i + 1] = 7; }
frame[2 * 70] = 65; frame[2 * 70 + 1] = 0x1E;          // an 'A', yellow on blue, in cell 70
frame[2 * 71] = 0xC9; frame[2 * 71 + 1] = 0x0B;        // a double-line corner
listeners.full({ data: b64(frame) });
check(painted["0,0"] !== undefined && Object.keys(painted).length === 1536, "a full frame paints every cell");
check(same(at(70), expected(65, 14, 1)), "'A' is the font's A, yellow on blue");
check(same(at(71), expected(0x2554, 11, 0)), "byte 0xC9 is U+2554 of the font");
check(same(at(0), expected(32, 7, 0)), "a space is blank");

// an extended cell: a pad button and the left half of an icon, after the stand-in 254 of a delta
const padA = require(path.join(root, "theme/font/r3i6nos-mono/r3i6nos-mono.json")).names["pad.btn.A.on"][0];
const d = [0, 80, 1, 254, 0x1F];
listeners.delta({ data: b64(d) });
check(same(at(80), expected(0x25A0 /* stand-in 254 is the CP437 black square */, 15, 1)), "an old unit's stand-in draws the CP437 ■");
const e = [0, 80, 1, (padA >> 16) & 255, (padA >> 8) & 255, padA & 255, 0x1F];
listeners.ext({ data: b64(e) });
check(same(at(80), expected(padA, 15, 1)), "an E cell draws the code point's glyph in its attribute");
listeners.delta({ data: b64([0, 80, 1, 65, 0x07]) });
check(same(at(80), expected(65, 7, 0)), "a delta makes the cell ordinary again");
listeners.ext({ data: b64(e) });
listeners.full({ data: b64(frame) });
check(same(at(80), expected(32, 7, 0)), "a full frame clears every extended cell");
listeners.ext({ data: b64([0, 5, 1, 0x00, 0x11, 0x11, 7]) });   // U+1111, not in the font
check(same(at(5), expected(32, 7, 0)), "a code point the font lacks is blank, not an error");

if (failed) { console.log("test_mirror_page: " + failed + " failure(s)"); process.exit(1); }
console.log("ok: test_mirror_page.js");
