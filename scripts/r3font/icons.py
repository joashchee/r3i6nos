"""System-status cells, coding glyphs (Powerline and friends) and the two-cell (20x20) icons: apps, system, files.

The icons are plain pictograms drawn for this font, not the apps' own artwork: the r3i6nos butterfly logo is the maintainer's hand-drawn art
and is never redrawn (docs/BRAND.md), so `app.r3i6nos` is a handheld, not the logo. Private use, names app.*, sys.*, file.*, code.*.
"""
import math

from .canvas import Bmp, CW, CH, micro, icon
from .pad import rrect, outline, dotted


def I():
    return icon()


def box(b, x, y, w, h, r=2, fill=False):
    body = rrect(w, h, r, x, y, b.w, b.h)
    b.blit(body if fill else outline(body))
    return b


def cell_art(rows, y=3, x=0):
    return Bmp().art(x, y, rows)


# ---------------------------------------------------------------------------------------------- system cells (one cell)
def sys_cells():
    s = {}
    s["headphones"] = Bmp().ellipse(5, 10, 4.6, 6.5, inner=(3.2, 5.0), quad={"tl", "tr"}).rect(0, 10, 3, 6).rect(7, 10, 3, 6)
    s["usb"] = Bmp().art(0, 2, ["....##....", "...####...", "....##....", ".#..##....", "##..##.##.", ".#..##.##.", ".#..##.#..", "..#.##.#..", "...####...",
                               "....##....", "....##....", "...####...", "...####...", "....##...."])
    s["dc"] = Bmp().rect(2, 3, 2, 4).rect(6, 3, 2, 4).rect(1, 7, 8, 5).rect(3, 12, 4, 2).rect(4, 14, 2, 5)
    s["card"] = outline(Bmp().poly([(1, 3), (8, 3), (9, 5), (9, 17), (1, 17)])).rect(2, 14, 1, 3).rect(4, 14, 1, 3).rect(6, 14, 1, 3)
    s["pad"] = outline(rrect(9, 9, 4, 0.5, 6, CW, CH)).blit(Bmp().rect(2, 9, 3, 1).rect(3, 8, 1, 3).rect(6, 9, 1, 1).rect(7, 10, 1, 1))
    s["lock"] = Bmp().frame(1, 9, 8, 8).rect(4, 12, 2, 2).ellipse(5, 8, 3, 4, inner=(1.6, 3), quad={"tl", "tr"}).rect(2, 7, 1, 3).rect(7, 7, 1, 3)
    s["unlock"] = Bmp().frame(1, 9, 8, 8).rect(4, 12, 2, 2).ellipse(5, 8, 3, 4, inner=(1.6, 3), quad={"tl", "tr"}).rect(7, 4, 1, 5)
    s["clock"] = Bmp().ring(5, 9.5, 4.8, 1).rect(5, 5, 1, 5).rect(5, 9, 3, 1)
    s["bluetooth"] = Bmp().art(1, 3, ["...#....", "...##...", "#..#.#..", ".#.#.#..", "..###...", "..###...", ".#.#.#..", "#..#.#..", "...##...", "...#...."]).shift(1, 3)
    for lv in range(5):
        b = Bmp().frame(1, 7, 8, 9).rect(3, 5, 4, 2)
        fill_h = int(round(7 * lv / 4.0))
        b.rect(2, 15 - fill_h, 6, fill_h)
        s[f"bat.{lv}"] = b
    s["bat.charge"] = Bmp().frame(1, 7, 8, 9).rect(3, 5, 4, 2).poly([(6, 8), (3, 12), (5, 12), (4, 15), (7, 10.5), (5, 10.5)])
    for lv in range(4):
        b = Bmp().rect(4, 15, 2, 2)
        for k in range(1, 4):
            if lv >= k:
                b.ellipse(5, 16, 1.6 + 1.5 * k, 2.0 + 2.2 * k, inner=(0.9 + 1.5 * k, 1.1 + 2.2 * k), quad={"tl", "tr"})
        s[f"wifi.{lv}"] = b
    for lv in range(4):
        b = Bmp().poly([(0, 7), (3, 7), (6, 4), (6, 15), (3, 12), (0, 12)])
        if lv >= 1:
            b.line(7, 8, 7, 11)
        if lv >= 2:
            b.line(8, 6, 8, 13)
        if lv >= 3:
            b.line(9, 4, 9, 15)
        s[f"vol.{lv}"] = b
    s["mute"] = Bmp().poly([(0, 7), (3, 7), (6, 4), (6, 15), (3, 12), (0, 12)]).line(7, 6, 9, 13).line(9, 6, 7, 13)
    s["bright"] = Bmp().disc(5, 9.5, 2.6).rect(5, 3, 1, 3).rect(5, 14, 1, 3).rect(1, 9, 3, 1).rect(7, 9, 3, 1)
    s["download"] = Bmp().rect(4, 3, 2, 8).poly([(1, 10), (9, 10), (5, 15)]).rect(1, 16, 8, 2)
    s["upload"] = Bmp().rect(4, 8, 2, 8).poly([(1, 9), (9, 9), (5, 4)]).rect(1, 17, 8, 2)
    s["sd.out"] = s["card"].copy().blit(Bmp().line(0, 2, 9, 17).line(1, 2, 9, 16))
    return s


def code_cells():
    c = {}
    c["bp.on"] = Bmp().disc(5, 9.5, 4.5)
    c["bp.off"] = Bmp().ring(5, 9.5, 4.5, 1)
    c["bp.cond"] = Bmp().poly([(5, 4), (9.5, 9.5), (5, 15), (0.5, 9.5)])
    c["bp.hit"] = Bmp().disc(5, 9.5, 4.5).poly([(3, 6), (3, 13), (8, 9.5)]).rect(0, 0, 0, 0)
    c["step.over"] = Bmp().ellipse(5, 11, 4.4, 5, inner=(2.8, 3.4), quad={"tl", "tr"}).poly([(7, 8), (10, 11.5), (5.5, 11.5)]).rect(4, 14, 2, 2)
    c["step.into"] = Bmp().rect(4, 3, 2, 8).poly([(1, 10), (9, 10), (5, 15)]).rect(4, 16, 2, 2)
    c["step.out"] = Bmp().rect(4, 8, 2, 8).poly([(1, 9), (9, 9), (5, 4)]).rect(4, 2, 2, 2)
    c["restart"] = Bmp().ellipse(5, 10, 4.2, 6, inner=(2.6, 4.4)).rect(5, 2, 5, 5, 0).poly([(2.5, 1), (2.5, 8), (8, 4.5)])
    c["error"] = Bmp().disc(5, 9.5, 5).blit(Bmp().line(3, 7, 7, 12).line(7, 7, 3, 12), op="clear")
    c["warn"] = Bmp().poly([(5, 3), (10, 15), (0, 15)]).rect(4, 7, 2, 4, 0).rect(4, 12, 2, 2, 0)
    c["info"] = Bmp().disc(5, 9.5, 5).rect(4, 5, 2, 2, 0).rect(4, 8, 2, 6, 0)
    c["hint"] = Bmp().ellipse(5, 8, 3.8, 4.6).rect(3, 13, 4, 3).rect(4, 17, 2, 1)
    c["ok"] = Bmp().disc(5, 9.5, 5).blit(Bmp().line(2, 9, 4, 12).line(4, 12, 8, 6), op="clear")
    c["todo"] = Bmp().frame(1, 5, 8, 10).rect(3, 8, 4, 1).rect(3, 11, 4, 1)
    c["commit"] = Bmp().ring(5, 9.5, 3.2, 1).rect(0, 9, 2, 1).rect(8, 9, 2, 1)
    c["merge"] = Bmp().disc(2, 4, 1.8).disc(2, 15, 1.8).disc(8, 11, 1.8).rect(1, 6, 2, 8).line(2, 9, 8, 9, 1)
    c["tag"] = Bmp().poly([(0, 5), (6, 5), (10, 9.5), (6, 14), (0, 14)]).rect(2, 8, 2, 2, 0)
    c["add"] = Bmp().rect(4, 5, 2, 10).rect(1, 9, 8, 2)
    c["del"] = Bmp().rect(1, 9, 8, 2)
    c["mod"] = Bmp().disc(5, 9.5, 3.2)
    c["ren"] = Bmp().rect(1, 9, 7, 2).poly([(6, 5), (10, 10), (6, 15)])
    c["conflict"] = Bmp().poly([(0, 4), (10, 4), (5, 10)]).poly([(0, 16), (10, 16), (5, 10)])
    c["folder"] = Bmp().frame(0, 6, 10, 10).rect(0, 4, 4, 2)
    c["folder.open"] = Bmp().rect(0, 4, 4, 2).hline(0, 9, 6).vline(0, 6, 15).hline(0, 6, 15).line(6, 15, 9, 9)
    c["file"] = Bmp().frame(1, 3, 8, 14).rect(6, 3, 3, 3, 0).hline(6, 8, 6).vline(6, 3, 6)
    c["link"] = Bmp().frame(0, 8, 5, 4).frame(5, 8, 5, 4).rect(4, 9, 2, 2)
    c["fold.open"] = Bmp().poly([(1, 7), (9, 7), (5, 13)])
    c["fold.closed"] = Bmp().poly([(3, 4), (3, 15), (9, 9.5)])
    c["branch"] = Bmp().rect(2, 3, 1, 13).disc(2.5, 16, 1.6).disc(2.5, 3.5, 1.6).disc(7.5, 6, 1.6).line(7, 7, 3, 12, 1)
    c["linenr"] = Bmp().blit(micro("LN"), 1, 7)
    c["padlock"] = Bmp().frame(1, 9, 8, 8).rect(4, 12, 2, 2).ellipse(5, 8, 3, 4, inner=(1.6, 3), quad={"tl", "tr"})
    c["cursor"] = Bmp().poly([(1, 3), (1, 16), (4, 13), (6, 18), (8, 17), (6, 12), (10, 12)])
    c["bulb"] = c["hint"].copy()
    return c


def powerline():
    p = {}
    p[0xE0B0] = Bmp().poly([(0, 0), (10, 10), (0, 20)])
    p[0xE0B1] = Bmp().line(0, 0, 9, 10).line(9, 10, 0, 19).line(0, 1, 8, 10).line(8, 10, 0, 18)
    p[0xE0B2] = p[0xE0B0].flip_h(); p[0xE0B3] = p[0xE0B1].flip_h()
    p[0xE0B4] = Bmp().ellipse(0, 10, 10, 10, quad={"tr", "br"})
    p[0xE0B5] = Bmp().ellipse(0, 10, 10, 10, inner=(8.6, 8.6), quad={"tr", "br"})
    p[0xE0B6] = p[0xE0B4].flip_h(); p[0xE0B7] = p[0xE0B5].flip_h()
    p[0xE0B8] = Bmp().poly([(0, 0), (10, 20), (0, 20)]); p[0xE0B9] = Bmp().line(0, 0, 9, 19).line(1, 0, 9, 18)
    p[0xE0BA] = Bmp().poly([(10, 0), (10, 20), (0, 20)]); p[0xE0BB] = Bmp().line(9, 0, 0, 19).line(8, 0, 0, 18)
    p[0xE0BC] = Bmp().poly([(0, 0), (10, 0), (0, 20)]); p[0xE0BD] = Bmp().line(9, 0, 0, 19).line(8, 0, 0, 18)
    p[0xE0BE] = Bmp().poly([(0, 0), (10, 0), (10, 20)]); p[0xE0BF] = Bmp().line(0, 0, 9, 19).line(1, 0, 9, 18)
    return p


# ---------------------------------------------------------------------------------------------- two-cell icons
def page(glyph=None, fold=True):
    b = I()
    b.frame(4, 1, 12, 18)
    if fold:
        b.rect(11, 1, 5, 5, 0).hline(11, 15, 6).vline(11, 1, 6).rect(12, 2, 3, 0)
    if glyph:
        glyph(b)
    return b


def f_text(b):
    for y in (9, 11, 13, 15):
        b.rect(6, y, 8, 1)


def f_code(b):
    b.line(8, 9, 6, 12).line(6, 12, 8, 15).line(12, 9, 14, 12).line(14, 12, 12, 15).line(11, 8, 9, 16)


def f_image(b):
    b.line(6, 15, 9, 10).line(9, 10, 12, 14).line(12, 14, 14, 12).line(14, 12, 15, 15).disc(13, 9, 1.4)


def f_audio(b):
    b.ellipse(8.5, 15, 2.4, 1.8).rect(10, 8, 1, 7).rect(10, 8, 4, 1).rect(13, 8, 1, 3)


def f_video(b):
    b.poly([(8, 9), (14, 12.5), (8, 16)])


def f_archive(b):
    for y in range(5, 16, 2):
        b.rect(10, y, 1, 1)
    b.rect(9, 9, 3, 3, 0).frame(9, 9, 3, 4)


def f_disk(b):
    b.frame(6, 9, 8, 8).rect(8, 9, 4, 3).rect(8, 14, 4, 3)


def f_doc(b):
    b.rect(6, 8, 8, 2)
    for y in (12, 14, 16):
        b.rect(6, y, 8 if y != 16 else 5, 1)


def f_font(b):
    b.blit(Bmp(20, CH).line(7, 16, 10, 8).line(10, 8, 13, 16).rect(8, 13, 5, 1))


def f_exec(b):
    b.line(6, 9, 9, 12).line(9, 12, 6, 15).rect(11, 15, 4, 1)


def f_music(b):
    for y in (9, 11, 13, 15):
        b.rect(6, y, 8, 1)
    b.ellipse(8.5, 14, 2, 1.5).rect(10, 8, 1, 6)


def f_tracker(b):
    for x in (6, 9, 12):
        b.rect(x, 8, 2, 8)
    for y in (9, 12, 15):
        b.rect(6, y, 8, 1, 0)


def f_midi(b):
    b.rect(6, 9, 8, 7, 1).rect(8, 9, 1, 7, 0).rect(11, 9, 1, 7, 0)


def f_data(b):
    b.hline(6, 14, 9).hline(6, 14, 12).hline(6, 14, 15).vline(10, 9, 15)


def f_link(b):
    b.line(6, 15, 13, 8).poly([(10, 7), (15, 7), (15, 12)])


def folder(open_=False):
    b = I()
    if not open_:
        b.frame(1, 5, 18, 13).rect(1, 3, 8, 3).rect(2, 4, 6, 0)
    else:
        b.rect(1, 3, 8, 2).hline(1, 12, 5).vline(1, 3, 17).hline(1, 14, 17)
        b.line(14, 17, 19, 8).hline(5, 19, 8).line(5, 8, 1, 17)
    return b


def gear(d=9.0, teeth=8):
    b = I()
    b.disc(10, 10, d - 1.8)
    for k in range(teeth):
        a = 2 * math.pi * k / teeth
        for r in range(int(d - 3), int(d + 1)):
            x, y = 10 + r * math.sin(a), 10 - r * math.cos(a)
            b.rect(int(x - 1), int(y - 1), 3, 3)
    b.disc(10, 10, 3.2, 0)
    return b


def terminal():
    b = I()
    box(b, 1, 3, 18, 14, 2)
    b.rect(2, 6, 16, 1)
    b.line(4, 9, 7, 11).line(7, 11, 4, 13).rect(9, 13, 5, 1)
    return b


def wifi():
    b = I()
    b.rect(9, 15, 3, 3)
    for k in (1, 2, 3):
        b.ellipse(10, 17, 3 + 3 * k, 3 + 3 * k, inner=(1.5 + 3 * k, 1.5 + 3 * k), quad={"tl", "tr"})
    return b


def speaker():
    b = I()
    b.poly([(2, 7), (6, 7), (11, 3), (11, 17), (6, 13), (2, 13)])
    for k in (1, 2):
        b.ellipse(11, 10, 2 + 3 * k, 3 + 3 * k, inner=(1 + 3 * k, 2 + 3 * k), quad={"tr", "br"})
    return b


def sdcard():
    b = I()
    b.blit(outline(Bmp(20, CH).poly([(4, 1), (13, 1), (16, 4), (16, 19), (4, 19)])))
    for x in (6, 9, 12):
        b.rect(x, 3, 2, 5)
    return b


def clipboard():
    b = I()
    box(b, 3, 3, 14, 16, 2)
    b.rect(7, 1, 6, 4)
    b.line(6, 11, 8, 13).line(8, 13, 13, 8)
    b.rect(6, 15, 8, 1)
    return b


def about():
    b = I()
    b.ring(10, 10, 9, 2)
    b.rect(9, 5, 3, 3).rect(9, 9, 3, 7)
    return b


def cloud(down=True):
    b = I()
    b.disc(7, 11, 4).disc(12, 9, 5).disc(15, 12, 3.5).rect(5, 11, 11, 4)
    arrow = Bmp(20, CH).rect(9, 7, 2, 8).poly([(6, 12), (14, 12), (10, 17)]) if down else Bmp(20, CH).rect(9, 9, 2, 8).poly([(6, 10), (14, 10), (10, 5)])
    b.blit(arrow, op="xor")
    return b


def keyboard():
    b = I()
    box(b, 1, 4, 18, 12, 2)
    for y in (7, 10):
        for x in range(4, 16, 3):
            b.rect(x, y, 2, 1)
    b.rect(6, 13, 8, 1)
    return b


def monitor():
    b = I()
    box(b, 1, 2, 18, 12, 1)
    b.rect(8, 14, 4, 3).rect(5, 17, 10, 2)
    b.line(5, 9, 8, 6).line(15, 9, 12, 6)
    return b


def power():
    b = I()
    b.ellipse(10, 11, 7, 7.5, inner=(5, 5.5)).rect(7, 3, 6, 8, 0).rect(9, 2, 2, 9)
    return b


def clock2():
    b = I()
    b.ring(10, 10, 9, 2)
    b.rect(9, 4, 2, 7).rect(9, 9, 5, 2)
    return b


def package():
    b = I()
    box(b, 2, 5, 16, 13, 1)
    b.rect(2, 8, 16, 1).rect(9, 5, 2, 4)
    b.line(2, 5, 6, 2).line(18, 5, 14, 2).hline(6, 14, 2)
    return b


def floppy():
    b = I()
    box(b, 2, 2, 16, 16, 2)
    b.rect(5, 2, 10, 5).rect(12, 3, 2, 3, 0).rect(5, 11, 10, 7, 0).frame(5, 11, 10, 7)
    return b


def handheld(screen_label=None):
    b = I()
    box(b, 0, 3, 20, 14, 5)
    b.frame(5, 5, 10, 8)
    b.rect(2, 9, 3, 1).rect(3, 8, 1, 3)
    b.disc(16.5, 8.5, 1.2).disc(14.5, 11.5, 1.2)
    b.rect(8, 15, 4, 1)
    return b


def app_icons():
    a = {}
    a["r3i6nos"] = handheld()
    a["menu"] = I().rect(2, 2, 7, 7).rect(11, 2, 7, 7).rect(2, 11, 7, 7).rect(11, 11, 7, 7)
    k = I(); box(k, 1, 2, 18, 16, 2); k.rect(2, 5, 16, 1)
    for x in range(3, 17, 2):
        k.rect(x, 14, 1, 2)
    k.rect(3, 8, 2, 4).rect(6, 8, 2, 4).rect(10, 8, 2, 4).rect(14, 8, 3, 4)       # blocks, as the shade characters draw them
    a["klamansi"] = k
    sprite = ["..#.....#..", "...#...#...", "..#######..", ".##.###.##.", "###########", "#.#######.#", "#.#.....#.#", "...##.##..."]
    a["nocogama"] = I().art(4, 5, sprite).shift(-1, 0)
    c = I(); c.ellipse(6, 15, 3.2, 2.4).rect(8, 5, 1, 11).ellipse(14, 13, 3.2, 2.4).rect(16, 3, 1, 11).rect(8, 3, 9, 2)
    c.rect(1, 3, 1, 3).rect(0, 4, 3, 1)
    a["composer"] = c
    p = I(); p.ellipse(10, 11, 9, 7).ellipse(14, 14, 2, 1.6, 0).disc(6, 10, 1.4, 0).disc(9, 7, 1.4, 0).disc(13, 8, 1.4, 0)
    a["painter"] = p
    f = I(); box(f, 1, 4, 18, 12, 1); f.rect(3, 4, 2, 12, 0).rect(15, 4, 2, 12, 0)
    for y in (5, 8, 11, 14):
        f.rect(2, y, 1, 2, 0).rect(17, y, 1, 2, 0)
    f.poly([(8, 7), (13, 10), (8, 13)])
    a["animator"] = f
    d = I(); d.blit(outline(rrect(10, 14, 1, 1, 3, 20, 20))); d.blit(outline(rrect(10, 14, 1, 7, 3, 20, 20)))
    d.rect(9, 6, 8, 12, 0).blit(outline(rrect(8, 12, 1, 8, 6, 20, 20))); d.rect(10, 10, 6, 1)
    a["crunchy"] = d
    g = I(); g.rect(3, 2, 14, 1).rect(3, 17, 14, 1)
    for x in range(3, 17, 4):
        g.rect(x, 2, 1, 16)
    for y in range(2, 18, 5):
        g.rect(3, y, 14, 1)
    g.disc(8, 6.5, 2).disc(12, 11.5, 2).disc(4, 16, 1.5, 0)
    a["chordica"] = g
    t = I()
    for i in range(8):
        h = (2, 5, 3, 7, 4, 6, 2, 5)[i]
        t.rect(2 + i * 2, 12 - h, 1, h + 3)
    t.ellipse(10, 10, 9, 8, inner=(7.4, 6.4), quad={"bl", "br", "tr"}).poly([(14, 1), (19, 4), (14, 7)])
    a["tinyntt"] = t
    h = I(); h.ellipse(10, 11, 8, 9, inner=(5.8, 7), quad={"tl", "tr"}).rect(1, 11, 4, 7).rect(15, 11, 4, 7)
    a["hydrolik"] = h
    m = I()
    for i, hh in enumerate((4, 8, 12, 7, 10, 5, 8, 3)):
        m.rect(2 + i * 2, 12 - hh // 2 + 3, 1, hh)
    m.rect(11, 1, 1, 18).poly([(12, 1), (18, 3.5), (12, 6)])
    a["musicue"] = m
    n = I(); n.blit(wave_line()); n.rect(2, 14, 16, 4)
    for x in range(3, 17, 3):
        n.rect(x, 14, 1, 4, 0)
    a["neumetik"] = n
    cm = I(); box(cm, 0, 3, 9, 14, 1); box(cm, 11, 3, 9, 14, 1)
    for y in (6, 9, 12):
        cm.rect(2, y, 5, 1).rect(13, y, 5, 1)
    cm.rect(2, 14, 5, 1, 0)
    a["commander"] = cm
    io = I(); box(io, 1, 2, 18, 16, 2)
    for i, y in enumerate((6, 10, 14)):
        io.line(4, y, 5, y + 1).line(5, y + 1, 8, y - 2).rect(11, y, 6, 1)
    a["iochecker"] = io
    pm = I(); box(pm, 3, 2, 14, 16, 1); pm.rect(6, 2, 8, 4, 0).frame(6, 2, 8, 4).rect(6, 9, 8, 2).rect(6, 13, 5, 1)
    a["portmaster"] = pm
    pi = I(); box(pi, 1, 3, 18, 14, 2); pi.rect(2, 6, 16, 1)
    pi.line(7, 9, 4, 11).line(4, 11, 7, 13).line(13, 9, 16, 11).line(16, 11, 13, 13).line(11, 8, 9, 14)
    a["pocketide"] = pi
    fm = I(); fm.ring(6, 10, 5, 1).ring(14, 10, 5, 1).rect(9, 9, 2, 2).blit(wave_line().shift(0, -5).sub(0, 0, 20, 20))
    a["fm1ntt"] = fm
    a["firmware"] = I().blit(outline(rrect(12, 12, 1, 4, 4, 20, 20))).rect(7, 7, 6, 6)
    for x in (6, 9, 12):
        a["firmware"].rect(x, 1, 2, 3).rect(x, 16, 2, 3)
    for y in (6, 9, 12):
        a["firmware"].rect(1, y, 3, 2).rect(16, y, 3, 2)
    return a


def wave_line():
    b = Bmp(20, CH)
    for x in range(20):
        y = int(round(8 - 4.5 * math.sin(2 * math.pi * x / 20)))
        b.set(x, y).set(x, y + 1)
    return b


def system_icons():
    s = {}
    s["settings"] = gear()
    s["network"] = wifi()
    s["ssh"] = terminal()
    b = I(); box(b, 1, 6, 15, 9, 1); b.rect(16, 8, 3, 5); b.rect(3, 8, 8, 5)
    s["battery"] = b
    s["sound"] = speaker()
    s["datacard"] = sdcard()
    s["clock"] = clock2()
    s["report"] = clipboard()
    s["about"] = about()
    s["update"] = cloud(True)
    s["upload"] = cloud(False)
    s["keyboard"] = keyboard()
    s["mirror"] = monitor()
    s["power"] = power()
    s["packages"] = package()
    s["saved"] = floppy()
    g = I(); g.ring(10, 10, 9, 1).ellipse(10, 10, 4, 9, inner=(3, 8)).rect(1, 10, 18, 1).rect(10, 1, 1, 18)
    s["globe"] = g
    s["bug"] = I().ellipse(10, 11, 5, 6.5).rect(9, 3, 2, 3).line(3, 6, 6, 9).line(17, 6, 14, 9).line(2, 12, 5, 12).line(18, 12, 15, 12).line(3, 18, 6, 15).line(17, 18, 14, 15)
    s["trash"] = I().rect(3, 4, 14, 2).rect(8, 2, 4, 2).frame(5, 6, 10, 12).rect(8, 8, 1, 8).rect(11, 8, 1, 8)
    s["search"] = I().ring(8, 8, 6, 2).line(12, 12, 18, 18, 2)
    s["star"] = I().poly([(10, 1), (12.5, 7.5), (19, 8), (14, 12.5), (15.5, 19), (10, 15.5), (4.5, 19), (6, 12.5), (1, 8), (7.5, 7.5)])
    s["heart"] = I().disc(6, 7, 4.2).disc(14, 7, 4.2).poly([(2, 9), (18, 9), (10, 18)])
    s["home"] = I().poly([(10, 2), (19, 10), (1, 10)]).rect(4, 10, 12, 8).rect(8, 13, 4, 5, 0)
    s["user"] = I().disc(10, 6, 4).ellipse(10, 19, 8, 8, quad={"tl", "tr"})
    s["lock"] = I().frame(4, 9, 12, 10, 2).ellipse(10, 9, 5, 7, inner=(3, 5), quad={"tl", "tr"}).rect(9, 12, 2, 4)
    s["mail"] = I().frame(1, 4, 18, 13).line(1, 4, 9, 11).line(18, 4, 10, 11)
    s["image"] = I().frame(1, 3, 18, 14).line(3, 15, 8, 8).line(8, 8, 12, 13).line(12, 13, 15, 10).line(15, 10, 17, 15).disc(14, 7, 1.6)
    s["add"] = I().rect(9, 3, 2, 14).rect(3, 9, 14, 2)
    s["close"] = I().line(4, 4, 15, 15, 2).line(15, 4, 4, 15, 2)
    s["check"] = I().line(3, 10, 8, 15, 2).line(8, 15, 17, 4, 2)
    s["warning"] = I().poly([(10, 1), (19, 18), (1, 18)]).rect(9, 7, 2, 6, 0).rect(9, 14, 2, 2, 0)
    s["pad"] = handheld()
    return s


def file_icons():
    f = {"folder": folder(False), "folder.open": folder(True), "file": page(None)}
    for nm, fn in (("text", f_text), ("code", f_code), ("image", f_image), ("audio", f_audio), ("video", f_video), ("archive", f_archive),
                   ("disk", f_disk), ("doc", f_doc), ("font", f_font), ("exec", f_exec), ("music", f_music), ("tracker", f_tracker),
                   ("midi", f_midi), ("data", f_data), ("link", f_link)):
        f["file." + nm] = page(fn)
    return f


def build(R):
    R.section("system status")
    for nm, b in sys_cells().items():
        R.one("sys", f"sys.{nm}", b)
    R.section("coding")
    for nm, b in code_cells().items():
        R.one("code", f"code.{nm}", b)
    for cp, b in powerline().items():
        R.put(cp, b)
    R.put(0xE0A0, code_cells()["branch"]); R.put(0xE0A1, code_cells()["linenr"]); R.put(0xE0A2, code_cells()["padlock"]); R.put(0xE0A3, Bmp().blit(micro("CN"), 1, 7))
    R.section("icons")
    for nm, b in app_icons().items():
        R.two("app", f"icon.app.{nm}", b)
    for nm, b in system_icons().items():
        R.two("app", f"icon.sys.{nm}", b)
    for nm, b in file_icons().items():
        R.two("app", f"icon.{nm if nm.startswith('file') else 'file.' + nm}" if nm not in ("folder", "folder.open") else f"icon.file.{nm}", b)
