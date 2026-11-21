extends Node3D

## Headphone listening experiment for comparing native Godot spatialization with
## AudioStreamQPlayer3D at exactly the same source transform.

enum Backend { QUALITY_HRTF, NATIVE_GODOT }
enum TestSignal { VOICE_LIKE, FOOTSTEPS, PINK_NOISE, SIREN, PURE_TONE }

const SIGNAL_NAMES := ["Voice-like", "Footsteps", "Pink noise", "Siren", "Pure tone"]
const SOURCE_AZIMUTHS := [-150.0, -120.0, -90.0, -60.0, -30.0, 0.0, 30.0, 60.0, 90.0, 120.0, 150.0, 180.0]
const SOURCE_ELEVATIONS := [-25.0, 0.0, 25.0]
const LISTENER_YAWS := [-150.0, -120.0, -90.0, -60.0, -30.0, 0.0, 30.0, 60.0, 90.0, 120.0, 150.0, 180.0]

@export_group("Replaceable recordings")
## Optional real speech recording. A redistribution-safe synthetic vowel-like
## signal is generated when this is empty.
@export var voice_stream: AudioStream
## Optional real footstep recording. A synthetic impact is generated when this
## is empty.
@export var footstep_stream: AudioStream
## Optional XR controller or other Node3D used for pointing-score measurement.
## Its -Z axis is treated as the pointing direction. The listener is used when
## this path is empty.
@export_node_path("Node3D") var pointing_node_path: NodePath

@onready var listener: AudioListener3D = $Listener
@onready var source_rig: Node3D = $SourceRig
@onready var source_marker: Node3D = $SourceRig/Marker
@onready var native_player: AudioStreamPlayer3D = $SourceRig/NativeAudioStreamPlayer3D
@onready var quality_player: AudioStreamQPlayer3D = $SourceRig/AudioStreamQPlayer3D
@onready var overview_camera: Camera3D = $OverviewCamera
@onready var footstep_timer: Timer = $FootstepTimer
@onready var status_label: Label = $UI/Margin/VBox/Status
@onready var help_label: Label = $UI/Margin/VBox/Help
@onready var backend_button: Button = $UI/Margin/VBox/Controls/Backend
@onready var signal_button: Button = $UI/Margin/VBox/Controls/Signal
@onready var play_button: Button = $UI/Margin/VBox/Actions/Play
@onready var random_button: Button = $UI/Margin/VBox/Actions/Randomize
@onready var reveal_button: Button = $UI/Margin/VBox/Actions/Reveal

var backend := Backend.QUALITY_HRTF
var test_signal := TestSignal.VOICE_LIKE
var source_azimuth_degrees := 60.0
var source_elevation_degrees := 0.0
var source_distance_metres := 4.0
var audition_playing := false
var source_hidden := false
var last_pointing_error_degrees := -1.0
var rng := RandomNumberGenerator.new()

func _ready() -> void:
	rng.randomize()
	listener.make_current()
	overview_camera.look_at(Vector3(0.0, 0.5, 0.0))
	native_player.playback_type = AudioServer.PLAYBACK_TYPE_STREAM
	native_player.max_polyphony = 6
	quality_player.max_polyphony = 6
	if voice_stream == null:
		voice_stream = _make_voice_like_stream()
	if footstep_stream == null:
		footstep_stream = _make_footstep_stream()
	backend_button.pressed.connect(_cycle_backend)
	signal_button.pressed.connect(_cycle_signal)
	play_button.pressed.connect(_toggle_playback)
	random_button.pressed.connect(_randomize_hidden_source)
	reveal_button.pressed.connect(_reveal_and_score)
	footstep_timer.timeout.connect(_play_footstep)
	_set_source_angles(source_azimuth_degrees, source_elevation_degrees)
	_apply_signal()
	_update_interface()

func _unhandled_key_input(event: InputEvent) -> void:
	if not event.pressed or event.echo:
		return
	match event.physical_keycode:
		KEY_SPACE:
			_toggle_playback()
		KEY_B:
			_cycle_backend()
		KEY_S:
			_cycle_signal()
		KEY_R:
			if event.shift_pressed:
				_randomize_listener_yaw()
			else:
				_randomize_hidden_source()
		KEY_H, KEY_ENTER:
			_reveal_and_score()
		KEY_LEFT:
			_rotate_listener(10.0)
		KEY_RIGHT:
			_rotate_listener(-10.0)
		KEY_UP:
			_set_source_angles(source_azimuth_degrees, minf(source_elevation_degrees + 5.0, 80.0))
		KEY_DOWN:
			_set_source_angles(source_azimuth_degrees, maxf(source_elevation_degrees - 5.0, -80.0))

func _cycle_backend() -> void:
	var was_playing := audition_playing
	_stop_players()
	backend = Backend.NATIVE_GODOT if backend == Backend.QUALITY_HRTF else Backend.QUALITY_HRTF
	_prepare_backend()
	if was_playing:
		_start_players()
	_update_interface()

func _cycle_signal() -> void:
	var was_playing := audition_playing
	_stop_players()
	test_signal = (test_signal + 1) % TestSignal.size()
	_apply_signal()
	if was_playing:
		_start_players()
	_update_interface()

func _toggle_playback() -> void:
	if audition_playing:
		_stop_players()
	else:
		_start_players()
	_update_interface()

func _start_players() -> void:
	audition_playing = true
	_prepare_backend()
	if test_signal == TestSignal.FOOTSTEPS:
		_play_footstep()
		footstep_timer.start()
	else:
		_active_player().play()

func _stop_players() -> void:
	audition_playing = false
	footstep_timer.stop()
	native_player.stop()
	quality_player.stop()

func _play_footstep() -> void:
	if audition_playing:
		_active_player().play()

func _active_player() -> Node:
	return quality_player if backend == Backend.QUALITY_HRTF else native_player

func _apply_signal() -> void:
	var selected_stream: AudioStream
	match test_signal:
		TestSignal.VOICE_LIKE:
			selected_stream = voice_stream
		TestSignal.FOOTSTEPS:
			selected_stream = footstep_stream
		TestSignal.PINK_NOISE:
			selected_stream = preload("res://audiocheck.net_pinknoise.wav")
		TestSignal.SIREN:
			selected_stream = preload("res://cop_siren.wav")
		TestSignal.PURE_TONE:
			selected_stream = preload("res://tone330.wav")
	native_player.stream = selected_stream
	quality_player.stream = selected_stream

func _prepare_backend() -> void:
	quality_player._update_spatialization()

func _randomize_hidden_source() -> void:
	var old_azimuth := source_azimuth_degrees
	for attempt in 8:
		source_azimuth_degrees = SOURCE_AZIMUTHS[rng.randi_range(0, SOURCE_AZIMUTHS.size() - 1)]
		if not is_equal_approx(source_azimuth_degrees, old_azimuth):
			break
	source_elevation_degrees = SOURCE_ELEVATIONS[rng.randi_range(0, SOURCE_ELEVATIONS.size() - 1)]
	source_hidden = true
	last_pointing_error_degrees = -1.0
	_set_source_angles(source_azimuth_degrees, source_elevation_degrees)
	_update_interface()

func _randomize_listener_yaw() -> void:
	var old_yaw := wrapf(listener.rotation_degrees.y, -180.0, 180.0)
	var new_yaw := old_yaw
	for attempt in 8:
		new_yaw = LISTENER_YAWS[rng.randi_range(0, LISTENER_YAWS.size() - 1)]
		if not is_equal_approx(new_yaw, old_yaw):
			break
	listener.rotation_degrees.y = new_yaw
	_update_interface()

func _reveal_and_score() -> void:
	if source_hidden:
		last_pointing_error_degrees = _calculate_pointing_error()
		source_hidden = false
	else:
		source_hidden = true
		last_pointing_error_degrees = -1.0
	source_marker.visible = not source_hidden
	_update_interface()

func _calculate_pointing_error() -> float:
	var pointer := get_node_or_null(pointing_node_path) as Node3D
	if pointer == null:
		pointer = listener
	var pointing_direction := -pointer.global_basis.orthonormalized().z
	var source_direction := (source_rig.global_position - pointer.global_position).normalized()
	return rad_to_deg(acos(clampf(pointing_direction.dot(source_direction), -1.0, 1.0)))

func _rotate_listener(yaw_degrees: float) -> void:
	listener.rotate_y(deg_to_rad(yaw_degrees))
	_update_interface()

func _set_source_angles(azimuth_degrees: float, elevation_degrees: float) -> void:
	source_azimuth_degrees = azimuth_degrees
	source_elevation_degrees = elevation_degrees
	var azimuth := deg_to_rad(azimuth_degrees)
	var elevation := deg_to_rad(elevation_degrees)
	var horizontal := cos(elevation) * source_distance_metres
	source_rig.position = Vector3(
		-horizontal * sin(azimuth),
		sin(elevation) * source_distance_metres + 1.6,
		-horizontal * cos(azimuth))
	source_marker.visible = not source_hidden
	_update_interface()

func _update_interface() -> void:
	if not is_node_ready():
		return
	backend_button.text = "Backend: %s" % ("QPlayer HRTF" if backend == Backend.QUALITY_HRTF else "Godot native")
	signal_button.text = "Signal: %s" % SIGNAL_NAMES[test_signal]
	play_button.text = "Stop" if audition_playing else "Play"
	reveal_button.text = "Reveal + score" if source_hidden else "Hide source"
	var location := "hidden" if source_hidden else "azimuth %.0f°, elevation %.0f°" % [source_azimuth_degrees, source_elevation_degrees]
	var listener_yaw := wrapf(listener.rotation_degrees.y, -180.0, 180.0)
	var score := ""
	if last_pointing_error_degrees >= 0.0:
		score = " | pointing error %.1f°" % last_pointing_error_degrees
	status_label.text = "%s | %s | %s | listener yaw %.0f°%s" % [
		"playing" if audition_playing else "stopped",
		"QPlayer HRTF" if backend == Backend.QUALITY_HRTF else "Godot native",
		location,
		listener_yaw,
		score,
	]
	help_label.text = "Headphones recommended. Space play · B backend · S signal · R random source · Shift+R random listener yaw · Left/Right turn listener · Up/Down source elevation · H reveal/score"

func _make_voice_like_stream() -> AudioStreamWAV:
	const SAMPLE_RATE := 48000
	const DURATION_SECONDS := 2.0
	var sample_count := int(SAMPLE_RATE * DURATION_SECONDS)
	var bytes := PackedByteArray()
	bytes.resize(sample_count * 2)
	for sample_index in sample_count:
		var time := float(sample_index) / SAMPLE_RATE
		var syllable := 0.18 + 0.82 * pow(0.5 - 0.5 * cos(TAU * 2.0 * time), 0.7)
		var value := syllable * (
			0.34 * sin(TAU * 110.0 * time)
			+ 0.22 * sin(TAU * 220.0 * time)
			+ 0.12 * sin(TAU * 330.0 * time)
			+ 0.17 * sin(TAU * 660.0 * time)
			+ 0.09 * sin(TAU * 990.0 * time)
			+ 0.06 * sin(TAU * 2420.0 * time))
		bytes.encode_s16(sample_index * 2, int(clampf(value, -1.0, 1.0) * 24500.0))
	var stream := AudioStreamWAV.new()
	stream.format = AudioStreamWAV.FORMAT_16_BITS
	stream.mix_rate = SAMPLE_RATE
	stream.stereo = false
	stream.loop_mode = AudioStreamWAV.LOOP_FORWARD
	stream.loop_begin = 0
	stream.loop_end = sample_count
	stream.data = bytes
	return stream

func _make_footstep_stream() -> AudioStreamWAV:
	const SAMPLE_RATE := 48000
	const DURATION_SECONDS := 0.28
	var sample_count := int(SAMPLE_RATE * DURATION_SECONDS)
	var bytes := PackedByteArray()
	bytes.resize(sample_count * 2)
	var noise_rng := RandomNumberGenerator.new()
	noise_rng.seed = 0x51544550
	var low_noise := 0.0
	for sample_index in sample_count:
		var time := float(sample_index) / SAMPLE_RATE
		low_noise = lerpf(low_noise, noise_rng.randf_range(-1.0, 1.0), 0.055)
		var first_impact := exp(-time * 24.0) * (0.55 * low_noise + 0.45 * sin(TAU * 88.0 * time))
		var heel_time := maxf(time - 0.085, 0.0)
		var second_impact := 0.0
		if time >= 0.085:
			second_impact = exp(-heel_time * 38.0) * (0.35 * low_noise + 0.20 * sin(TAU * 145.0 * heel_time))
		var value := first_impact + second_impact
		bytes.encode_s16(sample_index * 2, int(clampf(value, -1.0, 1.0) * 25000.0))
	var stream := AudioStreamWAV.new()
	stream.format = AudioStreamWAV.FORMAT_16_BITS
	stream.mix_rate = SAMPLE_RATE
	stream.stereo = false
	stream.data = bytes
	return stream
