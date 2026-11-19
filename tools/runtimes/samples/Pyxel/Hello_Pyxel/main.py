# Hello_Pyxel: the r3i6nos sample for the Pyxel runtime (docs/RUNTIMES.md). Original, public domain (CC0).
# D-pad (or the arrow keys) moves the square, A and B change its colour and play a note, Start (or Esc) quits. 160x120 at 4x fills the R36S's 640x480 screen.
# R3I6NOS_SAMPLE_FRAMES=n quits after n frames (a self-test hook; unset on the unit).
import os
import pyxel

pyxel.init(160, 120, title="Hello Pyxel", fps=30, display_scale=4)
pyxel.sounds[0].set("c3e3g3c4", "p", "7", "n", 15)
x, y, color, frames = 76, 56, 8, 0
AUTO = int(os.environ.get("R3I6NOS_SAMPLE_FRAMES", "0"))


def down(key, pad):
    return pyxel.btn(key) or pyxel.btn(pad)


def update():
    global x, y, color, frames
    frames += 1
    if AUTO and frames >= AUTO:
        pyxel.quit()
    if pyxel.btnp(pyxel.GAMEPAD1_BUTTON_START):
        pyxel.quit()
    if down(pyxel.KEY_LEFT, pyxel.GAMEPAD1_BUTTON_DPAD_LEFT):
        x = max(0, x - 2)
    if down(pyxel.KEY_RIGHT, pyxel.GAMEPAD1_BUTTON_DPAD_RIGHT):
        x = min(152, x + 2)
    if down(pyxel.KEY_UP, pyxel.GAMEPAD1_BUTTON_DPAD_UP):
        y = max(16, y - 2)
    if down(pyxel.KEY_DOWN, pyxel.GAMEPAD1_BUTTON_DPAD_DOWN):
        y = min(112, y + 2)
    if pyxel.btnp(pyxel.GAMEPAD1_BUTTON_A) or pyxel.btnp(pyxel.KEY_Z):
        color = 1 + color % 15
        pyxel.play(0, 0)
    if pyxel.btnp(pyxel.GAMEPAD1_BUTTON_B) or pyxel.btnp(pyxel.KEY_X):
        color = 1 + (color + 13) % 15
        pyxel.play(0, 0)


def draw():
    pyxel.cls(1)
    pyxel.text(4, 4, "HELLO PYXEL ON R3I6NOS", 7)
    pyxel.text(4, 10, "D-PAD MOVE  A/B COLOUR  START QUIT", 13)
    pyxel.rect(x, y, 8, 8, color)
    pyxel.text(4, 112, "FRAME %d" % frames, 6)


pyxel.run(update, draw)
