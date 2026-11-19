# Hello_Renpy: the r3i6nos sample for the Ren'Py runtime (docs/RUNTIMES.md). Original, public domain (CC0).
# A short scene with one choice; it tests the screen, the pad (Ren'Py's own mapping: A advances, the d-pad picks) and the time to the first frame.
# R3I6NOS_SAMPLE_AUTOQUIT=1 quits at the first line (a self-test hook; unset on the unit).
init -1 python:
    config.name = "Hello Renpy"
    config.screen_width = 640
    config.screen_height = 480
    config.window_title = "Hello Renpy"

define n = Character("Narrator")

label start:
    if renpy.os.environ.get("R3I6NOS_SAMPLE_AUTOQUIT"):
        $ renpy.quit()
    scene black
    n "Hello from Ren'Py on the R36S."
    n "This is a tiny scene made for r3i6nos."
    menu:
        n "Which button do you like best?"
        "A":
            n "A it is."
        "B":
            n "B, then."
    n "That is all. Choose Quit from the menu or hold Select and Start."
    return
