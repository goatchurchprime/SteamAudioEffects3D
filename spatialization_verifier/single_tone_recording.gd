extends AudioStreamPlayer

const VERIFIER_BUS_LAYOUT := preload("res://spatialization_verifier/bus_layout.tres")

# Use this script to generate a fixed sinewave tone

var hz = 330
var vol = 0.4
var t = 0
var duration = 10

@onready var g : AudioStreamGeneratorPlayback = get_stream_playback()

func _enter_tree() -> void:
	AudioServer.set_bus_layout(VERIFIER_BUS_LAYOUT)

func _ready():
	var recorder : AudioEffectRecord = AudioServer.get_bus_effect(AudioServer.get_bus_index("ToneRecording"), 0)
	await get_tree().create_timer(0.5).timeout
	recorder.set_recording_active(true)
	await get_tree().create_timer(duration).timeout
	recorder.set_recording_active(false)
	var recording : AudioStreamWAV = recorder.get_recording()
	var fname = "tone%d.wav" % hz
	print("Saving ", fname)
	recording.save_to_wav(fname)
	stop()

var nframe = 0
func _process(_delta):
	if g != null:
		while g.can_push_buffer(1):
			var x = sin(nframe/stream.mix_rate*hz*TAU)*vol
			g.push_frame(Vector2.ONE*x)
			nframe += 1
