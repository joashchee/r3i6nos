# Hello_Godot: the r3i6nos sample for the Godot runtime (docs/RUNTIMES.md). Original, public domain (CC0).
# D-pad (or the arrow keys) moves the square, A and B change its colour and play a note, Start (or Esc) quits. No asset files: the picture is drawn
# and the note is made here, so the game is three small text files in the pack.
# R3I6NOS_SAMPLE_FRAMES=n quits after n frames (a self-test hook; unset on the unit).
extends Node2D

var pos = Vector2(320, 240)
var hue = 0.0
var frames = 0
var auto = 0
var player = AudioStreamPlayer.new()

func _ready():
	auto = int(OS.get_environment("R3I6NOS_SAMPLE_FRAMES"))
	var wave = PoolByteArray()
	for i in range(11025):
		var s = int(sin(i * TAU * 440.0 / 22050.0) * 9000.0 * (1.0 - i / 11025.0))
		wave.append(s & 255)
		wave.append((s >> 8) & 255)
	var stream = AudioStreamSample.new()
	stream.format = AudioStreamSample.FORMAT_16_BITS
	stream.mix_rate = 22050
	stream.data = wave
	player.stream = stream
	add_child(player)
	print("HELLO_GODOT ready renderer=", OS.get_current_video_driver(), " size=", OS.window_size)

func _process(_delta):
	frames += 1
	var step = Vector2(Input.get_action_strength("ui_right") - Input.get_action_strength("ui_left"), Input.get_action_strength("ui_down") - Input.get_action_strength("ui_up"))
	pos = Vector2(clamp(pos.x + step.x * 6.0, 16, 624), clamp(pos.y + step.y * 6.0, 48, 464))
	if Input.is_action_just_pressed("ui_accept") or Input.is_action_just_pressed("ui_focus_next"):
		hue = fmod(hue + 0.12, 1.0)
		player.play()
	if Input.is_action_just_pressed("ui_cancel") or Input.is_joy_button_pressed(0, JOY_START):
		quit_now("start")
	if auto > 0 and frames >= auto:
		quit_now("frames")
	update()

func quit_now(why):
	print("HELLO_GODOT quit ", why, " frames=", frames)
	get_tree().quit()

func _draw():
	draw_rect(Rect2(0, 0, 640, 480), Color(0.06, 0.07, 0.16))
	draw_rect(Rect2(0, 0, 640, 32), Color(0.2, 0.2, 0.35))
	draw_rect(Rect2(pos.x - 16, pos.y - 16, 32, 32), Color.from_hsv(hue, 0.7, 1.0))
	draw_rect(Rect2(pos.x - 16, pos.y - 16, 32, 32), Color.white, false, 2.0)
	var bar = int(frames % 640)
	draw_rect(Rect2(bar, 32, 4, 8), Color(0.9, 0.9, 0.3))
