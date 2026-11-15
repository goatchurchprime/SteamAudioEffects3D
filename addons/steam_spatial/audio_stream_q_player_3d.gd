@tool
@icon("res://addons/steam_spatial/audio_stream_player_3d.svg")
## Plays a higher-quality positional sound in 3D space using Steam Audio.
##
## This node is designed as a scene and scripting replacement for
## [AudioStreamPlayer3D]. Its playback, routing, attenuation, directionality,
## and Doppler properties intentionally use the same names and defaults where
## practical. Unlike [AudioStreamPlayer3D], it inherits [Node3D], so typed
## references and APIs that explicitly require [AudioStreamPlayer3D] are not
## interchangeable.
##
## The source is forced through Godot's stream mixer and a private bus containing
## Reflection Send, [AudioEffectSteamDirect], [AudioEffectSteamSpatial], and
## Reflection HRTF Return effects in that order. Sample playback is not supported
## because it can bypass Godot bus effects. Stereo streams are currently folded
## to one point source before HRTF rendering.
class_name AudioStreamQPlayer3D
extends Node3D

## Selects the spatial renderer while preserving this node's public API.
enum SpatialBackend {
	## Uses Steam Audio when its native effect classes are available, otherwise
	## falls back to Godot's platform-native [AudioStreamPlayer3D].
	AUTO,
	## Requires the Steam Audio GDExtension.
	STEAM_AUDIO,
	## Uses Godot's built-in 3D renderer. This is the current Web fallback.
	GODOT_NATIVE,
}

## Built-in diffuse reflection starting points. These presets drive the current
## order-0 parametric return; they do not contain directional wall reflections.
enum ReflectionPreset {
	CUSTOM,
	DRY,
	DEAD_BOOTH,
	FURNISHED_ROOM,
	OFFICE,
	LARGE_HALL,
	STONE_TUNNEL,
	EXAGGERATED_CAVERN,
}

## Emitted when the audio finishes playing without being interrupted by [method stop].
signal finished

const CMP_EPSILON := 0.00001
const SPEED_OF_SOUND := 343.0
const SPATIAL_BUS_CONSTRUCT_RUNTIME := &"Construct Runtime"
const SPATIAL_BUS_CONSTRUCT_NOW := &"Construct Now"

var _stream: AudioStream
var _playing_requested := false
var _editor_resume_pending := false
var _editor_resume_position := 0.0
var _stream_paused := false
var _max_polyphony := 1
var _playback_type: AudioServer.PlaybackType = AudioServer.PLAYBACK_TYPE_STREAM
var _pitch_scale := 1.0
var _active_backend: SpatialBackend = SpatialBackend.GODOT_NATIVE
var _spatial_effect_bus: StringName = SPATIAL_BUS_CONSTRUCT_RUNTIME
var _bus_hint_cache := ""
var _native_audio_bus_hint := -1

@export_category("Renderer")
## Requested spatial renderer. [constant SpatialBackend.AUTO] is recommended
## for scenes which are exported to more than one platform.
@export var spatial_backend: SpatialBackend = SpatialBackend.AUTO

@export_category("Playback")
## The [AudioStream] resource to play.
@export var stream: AudioStream:
	get:
		return _stream
	set(value):
		_stream = value
		for player in [_get_audio_player(), _get_godot_player()]:
			if player != null:
				player.stream = value
## If [code]true[/code], playback begins when this node enters the scene tree at runtime.
@export var autoplay := false
## If [code]true[/code], this node is currently playing or has requested playback.
@export var playing: bool:
	get:
		var player := _get_active_player()
		return player.playing if player != null else _playing_requested
	set(value):
		_playing_requested = value
		_editor_resume_pending = false
		var player := _get_active_player()
		if player == null or not player.is_inside_tree():
			return
		if value:
			if not player.playing:
				player.play()
		else:
			player.stop()
## If [code]true[/code], the active stream playback is paused.
@export var stream_paused: bool:
	get:
		var player := _get_active_player()
		return player.stream_paused if player != null else _stream_paused
	set(value):
		_stream_paused = value
		for player in [_get_audio_player(), _get_godot_player()]:
			if player != null:
				player.stream_paused = value
## Maximum simultaneous playbacks. Starting another cuts off the oldest playback.
@export_range(1, 64, 1) var max_polyphony: int:
	get:
		return _max_polyphony
	set(value):
		_max_polyphony = maxi(1, value)
		for player in [_get_audio_player(), _get_godot_player()]:
			if player != null:
				player.max_polyphony = _max_polyphony
## Playback pitch multiplier. Doppler is applied on top of this value.
@export var pitch_scale: float = 1.0:
	get:
		return _pitch_scale
	set(value):
		_pitch_scale = maxf(value, 0.0001)
		for player in [_get_audio_player(), _get_godot_player()]:
			if player != null:
				player.pitch_scale = _pitch_scale
## Source volume in decibels, applied before HRTF rendering.
@export var volume_db := 0.0
## Source volume as a linear value.
var volume_linear: float:
	get:
		return db_to_linear(volume_db)
	set(value):
		volume_db = linear_to_db(value)
## Compatibility property. This player always uses [constant AudioServer.PLAYBACK_TYPE_STREAM]
## because Sample playback does not pass through Godot audio buses.
@export var playback_type: AudioServer.PlaybackType:
	get:
		return _playback_type
	set(value):
		# Bus effects only exist in Godot's stream mixer. Sample playback may be
		# handled outside the engine and therefore cannot run Direct or HRTF.
		_playback_type = AudioServer.PLAYBACK_TYPE_STREAM
		if value == AudioServer.PLAYBACK_TYPE_SAMPLE:
			push_warning("AudioStreamQPlayer3D requires stream playback for its bus effects")
		var player := _get_audio_player()
		if player != null:
			player.playback_type = AudioServer.PLAYBACK_TYPE_STREAM
		var godot_player := _get_godot_player()
		if godot_player != null:
			godot_player.playback_type = value
## Destination bus for the output of this player's private Steam processing bus.
@export var bus: StringName = &"Master":
	set(value):
		bus = value
		if effect_bus_index >= 0:
			AudioServer.set_bus_send(effect_bus_index, value)
		else:
			var player := _get_audio_player()
			if player != null:
				player.bus = value
## Selects where this player's spatial effect chain lives. [code]Construct Runtime[/code]
## creates a private bus when the scene runs and removes it afterwards.
## [code]Construct Now[/code] creates and assigns that same uniquely named bus in
## the editor, allowing it to be inspected and deliberately saved in the audio
## bus layout. An existing bus can also be selected if it already contains the
## four Steam spatial effects in their required order.
@export_custom(PROPERTY_HINT_ENUM, "Construct Runtime,Construct Now") var spatial_effect_bus: StringName:
	get:
		return _spatial_effect_bus
	set(value):
		_spatial_effect_bus = value if value != &"" else SPATIAL_BUS_CONSTRUCT_RUNTIME
		if is_inside_tree() and _active_backend == SpatialBackend.STEAM_AUDIO:
			_apply_spatial_effect_bus_selection()

@export_category("Spatialization")
## Distance attenuation curve applied to the direct sound path.
@export var attenuation_model: AudioStreamPlayer3D.AttenuationModel = AudioStreamPlayer3D.ATTENUATION_INVERSE_DISTANCE
## Distance at which attenuation begins. This matches [AudioStreamPlayer3D.unit_size].
@export var unit_size := 10.0
## Distance beyond which output fades linearly to silence. A value of [code]0.0[/code] disables this limit.
@export var max_distance := 0.0
## Maximum source gain in decibels.
@export var max_db := 3.0
## Multiplies [code]audio/general/3d_panning_strength[/code] to control HRTF spatial blend.
@export_range(0.0, 3.0, 0.01) var panning_strength := 1.0
## Selects whether Doppler tracking is disabled or evaluated. Updates currently occur on physics ticks.
@export var doppler_tracking: AudioStreamPlayer3D.DopplerTracking = AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED

@export_group("Reflections")
## A convenient diffuse-reverb starting point. Selecting a preset updates the
## reflection fields below. Editing those fields afterwards remains possible;
## select [constant ReflectionPreset.CUSTOM] to describe that intent explicitly.
@export var reflection_preset: ReflectionPreset = ReflectionPreset.DRY:
	set(value):
		reflection_preset = value
		_apply_reflection_preset(value)
## Enables Steam Audio's initial parametric reflection send and Ambisonic return.
@export var reflections_enabled := false
## Gain applied before submitting the reflected signal to the Ambisonic return.
@export_range(-80.0, 12.0, 0.1, "suffix:dB") var reflection_send_db := -12.0
## Low-, mid-, and high-frequency RT60 decay times in seconds.
@export var reflection_reverb_times := Vector3(1.0, 0.8, 0.5)
## Gain applied to the decoded reflection signal before it is mixed with the dry path.
@export_range(-80.0, 12.0, 0.1, "suffix:dB") var reflection_wet_db := 0.0

## Applies one of the built-in diffuse reflection starting points.
##
## The values are intentionally conservative audition presets rather than room
## measurements. [constant ReflectionPreset.CUSTOM] leaves all fields unchanged.
func apply_reflection_preset(preset: ReflectionPreset) -> void:
	reflection_preset = preset

func _apply_reflection_preset(preset: ReflectionPreset) -> void:
	match preset:
		ReflectionPreset.CUSTOM:
			return
		ReflectionPreset.DRY:
			reflections_enabled = false
			reflection_send_db = -80.0
			reflection_reverb_times = Vector3(0.15, 0.12, 0.10)
			reflection_wet_db = 0.0
		ReflectionPreset.DEAD_BOOTH:
			reflections_enabled = true
			reflection_send_db = -20.0
			reflection_reverb_times = Vector3(0.15, 0.12, 0.10)
			reflection_wet_db = -6.0
		ReflectionPreset.FURNISHED_ROOM:
			reflections_enabled = true
			reflection_send_db = -16.0
			reflection_reverb_times = Vector3(0.35, 0.30, 0.22)
			reflection_wet_db = -4.0
		ReflectionPreset.OFFICE:
			reflections_enabled = true
			reflection_send_db = -14.0
			reflection_reverb_times = Vector3(0.55, 0.45, 0.30)
			reflection_wet_db = -4.0
		ReflectionPreset.LARGE_HALL:
			reflections_enabled = true
			reflection_send_db = -12.0
			reflection_reverb_times = Vector3(2.20, 1.80, 1.30)
			reflection_wet_db = -4.0
		ReflectionPreset.STONE_TUNNEL:
			reflections_enabled = true
			reflection_send_db = -10.0
			reflection_reverb_times = Vector3(3.20, 2.70, 2.00)
			reflection_wet_db = -3.0
		ReflectionPreset.EXAGGERATED_CAVERN:
			reflections_enabled = true
			reflection_send_db = -8.0
			reflection_reverb_times = Vector3(5.00, 4.00, 3.00)
			reflection_wet_db = -3.0
	_push_reflection_parameters()

func _push_reflection_parameters() -> void:
	if steam_reflection_send != null:
		steam_reflection_send.set("enabled", reflections_enabled)
		steam_reflection_send.set("send_gain", db_to_linear(volume_db) * db_to_linear(reflection_send_db))
		steam_reflection_send.set("reverb_times", reflection_reverb_times)
	if steam_reflection_hrtf != null:
		steam_reflection_hrtf.set("wet_gain", db_to_linear(reflection_wet_db))

@export_group("Emission Angle")
## Enables attenuation when the listener is outside the source's forward emission cone.
@export var emission_angle_enabled := false
## Half-angle of the unattenuated forward emission cone, in degrees.
@export_range(0.1, 90.0, 0.1, "degrees") var emission_angle_degrees := 45.0
## Gain applied outside the emission cone, in decibels.
@export_range(-80.0, 0.0, 0.1, "suffix:dB") var emission_angle_filter_attenuation_db := -12.0

# Kept for AudioStreamPlayer3D scene/API compatibility. Steam's physical
# three-band air absorption replaces this single high-shelf model in v1.
@export_group("Attenuation Filter")
## Compatibility placeholder. Steam's three-band air absorption replaces Godot's cutoff filter.
@export_range(1.0, 20500.0, 1.0, "suffix:Hz") var attenuation_filter_cutoff_hz := 5000.0
## Compatibility placeholder. Steam's three-band air absorption replaces Godot's attenuation filter.
@export_range(-80.0, 0.0, 0.1, "suffix:dB") var attenuation_filter_db := -24.0

var effect_bus_name: StringName = &""
var effect_bus_index := -1
var owns_effect_bus := false
var steam_direct: AudioEffect
var steam_hrtf: AudioEffect
var steam_reflection_send: AudioEffect
var steam_reflection_hrtf: AudioEffect
var disable_doppler := false

func _enter_tree() -> void:
	# A class_name script added from Create Node does not instantiate the companion
	# .tscn. Keep these implementation nodes internal so AudioStreamQPlayer3D is a
	# real one-node drop-in rather than a scene which only works when instanced.
	_ensure_internal_nodes()

func _ensure_internal_nodes() -> void:
	if get_node_or_null("AudioStreamPlayer") == null:
		var steam_player := AudioStreamPlayer.new()
		steam_player.name = "AudioStreamPlayer"
		steam_player.playback_type = AudioServer.PLAYBACK_TYPE_STREAM
		add_child(steam_player, false, Node.INTERNAL_MODE_BACK)
	if get_node_or_null("GodotAudioStreamPlayer3D") == null:
		var godot_player := AudioStreamPlayer3D.new()
		godot_player.name = "GodotAudioStreamPlayer3D"
		add_child(godot_player, false, Node.INTERNAL_MODE_BACK)
	if get_node_or_null("VelocityTracker3D") == null:
		var tracker := Node3D.new()
		tracker.name = "VelocityTracker3D"
		tracker.set_script(preload("res://addons/steam_spatial/velocity_tracker_3d.gd"))
		add_child(tracker, false, Node.INTERNAL_MODE_BACK)

func _ready() -> void:
	var steam_player := _get_audio_player()
	var godot_player := _get_godot_player()
	if steam_player == null or godot_player == null:
		push_error("AudioStreamQPlayer3D requires both private player children")
		return
	_active_backend = _choose_backend()
	for player in [steam_player, godot_player]:
		player.finished.connect(_on_stream_finished)
		player.stream = _stream
		player.stream_paused = _stream_paused
		player.max_polyphony = _max_polyphony
		player.pitch_scale = _pitch_scale
	steam_player.playback_type = AudioServer.PLAYBACK_TYPE_STREAM
	godot_player.playback_type = _playback_type
	_configure_godot_player()

	if _active_backend == SpatialBackend.STEAM_AUDIO:
		_apply_spatial_effect_bus_selection()

	# Bus and playback type are selected when play_basic() creates the playback;
	# changing either afterwards does not migrate an already-playing stream.
	if _playing_requested or (autoplay and not Engine.is_editor_hint()):
		play()

func _get_audio_player() -> AudioStreamPlayer:
	return get_node_or_null("AudioStreamPlayer") as AudioStreamPlayer

func _get_godot_player() -> AudioStreamPlayer3D:
	return get_node_or_null("GodotAudioStreamPlayer3D") as AudioStreamPlayer3D

func _get_active_player() -> Node:
	return _get_audio_player() if _active_backend == SpatialBackend.STEAM_AUDIO else _get_godot_player()

func _steam_effects_available() -> bool:
	return (
		ClassDB.class_exists("AudioEffectSteamReflectionSend")
		and ClassDB.class_exists("AudioEffectSteamDirect")
		and ClassDB.class_exists("AudioEffectSteamSpatial")
		and ClassDB.class_exists("AudioEffectSteamReflectionHRTF")
	)

func _choose_backend() -> SpatialBackend:
	if spatial_backend == SpatialBackend.GODOT_NATIVE:
		return SpatialBackend.GODOT_NATIVE
	if _steam_effects_available():
		return SpatialBackend.STEAM_AUDIO
	if spatial_backend == SpatialBackend.STEAM_AUDIO:
		push_warning("Steam Audio is unavailable on this platform; using Godot native 3D audio")
	return SpatialBackend.GODOT_NATIVE

## Returns the renderer actually used by this instance.
func get_active_backend() -> SpatialBackend:
	return _active_backend

## Reports optional renderer features without changing the common player API.
func get_spatial_capabilities() -> Dictionary:
	return {
		"backend": "steam_audio" if _active_backend == SpatialBackend.STEAM_AUDIO else "godot_native",
		"hrtf": _active_backend == SpatialBackend.STEAM_AUDIO,
		"reflections": _active_backend == SpatialBackend.STEAM_AUDIO,
		"ambisonics": _active_backend == SpatialBackend.STEAM_AUDIO,
	}

## Starts playback at [param from_position] seconds.
func play(from_position := 0.0) -> void:
	_playing_requested = true
	_editor_resume_pending = false
	var player := _get_active_player()
	if player != null:
		# Set this on the actual player immediately before every new playback.
		# Playback type is sampled by Godot in play_basic() and changing it later
		# cannot move an already-running sample into the software bus mixer.
		if _active_backend == SpatialBackend.STEAM_AUDIO:
			player.playback_type = AudioServer.PLAYBACK_TYPE_STREAM
		player.play(from_position)

## Moves the playback position to [param to_position] seconds.
func seek(to_position: float) -> void:
	var player := _get_active_player()
	if player != null:
		player.seek(to_position)

## Stops playback without emitting [signal finished].
func stop() -> void:
	_playing_requested = false
	_editor_resume_pending = false
	var player := _get_active_player()
	if player != null:
		player.stop()

## Returns [code]true[/code] while audio is playing.
func is_playing() -> bool:
	var player := _get_active_player()
	return player.is_playing() if player != null else false

## Returns the approximate playback position in seconds.
func get_playback_position() -> float:
	var player := _get_active_player()
	return player.get_playback_position() if player != null else 0.0

## Returns the active [AudioStreamPlayback], or [code]null[/code].
func get_stream_playback() -> AudioStreamPlayback:
	var player := _get_active_player()
	return player.get_stream_playback() if player != null else null

## Returns whether an [AudioStreamPlayback] currently exists.
func has_stream_playback() -> bool:
	var player := _get_active_player()
	return player.has_stream_playback() if player != null else false

func _on_stream_finished() -> void:
	_playing_requested = false
	_editor_resume_pending = false
	finished.emit()

func begin_editor_preview(preview_bus: StringName) -> void:
	if not Engine.is_editor_hint():
		return
	var target_bus := _spatial_effect_bus
	if target_bus == SPATIAL_BUS_CONSTRUCT_RUNTIME or target_bus == SPATIAL_BUS_CONSTRUCT_NOW:
		target_bus = preview_bus
	var target_index := AudioServer.get_bus_index(target_bus)
	var player := _get_audio_player()
	if target_index < 0:
		push_warning("Steam spatial effect bus '%s' does not exist" % target_bus)
		return
	if effect_bus_index != target_index or player == null or player.bus != target_bus or steam_reflection_send == null or steam_direct == null or steam_hrtf == null or steam_reflection_hrtf == null:
		_attach_effect_bus(target_bus)

func end_editor_preview() -> void:
	if not Engine.is_editor_hint():
		return
	stop()
	var player := _get_audio_player()
	if player != null:
		player.bus = &"Master"
	effect_bus_index = -1
	steam_direct = null
	steam_hrtf = null
	steam_reflection_send = null
	steam_reflection_hrtf = null
	notify_property_list_changed()

func _apply_spatial_effect_bus_selection() -> void:
	if not _steam_effects_available():
		return
	_ensure_internal_nodes()
	var player := _get_audio_player()
	if player == null:
		return
	var was_playing := player.playing
	var playback_position := player.get_playback_position() if was_playing else 0.0
	if was_playing:
		player.stop()

	# A generated bus belongs to this player when its instance-numbered name is
	# the name this player would generate now. Existing named buses are external.
	var remove_previous_bus := owns_effect_bus
	if (
			Engine.is_editor_hint()
			and _effect_bus_belongs_to_player(effect_bus_name)
			and _spatial_effect_bus != effect_bus_name
	):
		remove_previous_bus = true
	_clear_effect_bus(remove_previous_bus)
	match _spatial_effect_bus:
		SPATIAL_BUS_CONSTRUCT_RUNTIME:
			if not Engine.is_editor_hint():
				_create_runtime_effect_bus()
		SPATIAL_BUS_CONSTRUCT_NOW:
			if Engine.is_editor_hint():
				var generated_name := _generated_effect_bus_name()
				if AudioServer.get_bus_index(generated_name) >= 0:
					_attach_effect_bus(generated_name)
				else:
					_create_effect_bus(generated_name)
				# This is an authored bus: keep it in the layout when this node exits.
				owns_effect_bus = false
				_spatial_effect_bus = generated_name
				notify_property_list_changed()
				if EditorInterface.get_edited_scene_root() != null:
					EditorInterface.mark_scene_as_unsaved()
			else:
				_create_runtime_effect_bus()
		_:
			_attach_effect_bus(_spatial_effect_bus)

	if was_playing:
		player.play(playback_position)

func _clear_effect_bus(remove_owned: bool) -> void:
	var old_name := effect_bus_name
	var player := _get_audio_player()
	if player != null:
		player.bus = bus
	effect_bus_name = &""
	effect_bus_index = -1
	steam_reflection_send = null
	steam_direct = null
	steam_hrtf = null
	steam_reflection_hrtf = null
	owns_effect_bus = false
	if remove_owned and old_name != &"":
		var old_index := AudioServer.get_bus_index(old_name)
		if old_index >= 0:
			AudioServer.remove_bus(old_index)
	notify_property_list_changed()

func _attach_effect_bus(bus_name: StringName) -> void:
	owns_effect_bus = false
	effect_bus_name = bus_name
	effect_bus_index = AudioServer.get_bus_index(bus_name)
	steam_reflection_send = null
	steam_direct = null
	steam_hrtf = null
	steam_reflection_hrtf = null
	if effect_bus_index < 0:
		push_warning("Steam spatial effect bus '%s' does not exist" % bus_name)
		notify_property_list_changed()
		return
	var player := _get_audio_player()
	if player != null:
		player.bus = bus_name
	for effect_index in AudioServer.get_bus_effect_count(effect_bus_index):
		var candidate := AudioServer.get_bus_effect(effect_bus_index, effect_index)
		if candidate.get_class() == "AudioEffectSteamReflectionSend":
			steam_reflection_send = candidate
		elif candidate.get_class() == "AudioEffectSteamDirect":
			steam_direct = candidate
		elif candidate.get_class() == "AudioEffectSteamSpatial":
			steam_hrtf = candidate
		elif candidate.get_class() == "AudioEffectSteamReflectionHRTF":
			steam_reflection_hrtf = candidate
	if steam_reflection_send == null or steam_direct == null or steam_hrtf == null or steam_reflection_hrtf == null:
		push_warning("Bus '%s' needs Reflection Send, Steam Direct, Steam HRTF, then Reflection HRTF Return" % bus_name)
	else:
		steam_reflection_send.set("target", steam_reflection_hrtf)
		_push_reflection_parameters()
	notify_property_list_changed()

func _generated_effect_bus_name() -> StringName:
	if Engine.is_editor_hint() and is_inside_tree():
		var scene_root: Node = self
		while scene_root.get_parent() != null and not (scene_root.get_parent() is Viewport):
			scene_root = scene_root.get_parent()
		var scene_path := scene_root.scene_file_path
		if scene_path != "":
			var stable_identity := "%s::%s" % [scene_path, scene_root.get_path_to(self)]
			return StringName("QPlayerBus_%s" % stable_identity.sha256_text().substr(0, 12))
	var player := _get_audio_player()
	return StringName("QPlayerBus_%d" % player.get_instance_id()) if player != null else &"QPlayerBus"

func _effect_bus_belongs_to_player(bus_name: StringName) -> bool:
	if bus_name == _generated_effect_bus_name():
		return true
	# Recognize a bus made by the earlier instance-number naming during this
	# editor session, so switching to the stable scheme does not orphan it.
	var player := _get_audio_player()
	return player != null and bus_name == StringName("QPlayerBus_%d" % player.get_instance_id())

func _create_runtime_effect_bus() -> void:
	_create_effect_bus(_generated_effect_bus_name())

func _create_effect_bus(bus_name: StringName) -> void:
	var player := _get_audio_player()
	if player == null:
		return
	owns_effect_bus = true
	effect_bus_name = bus_name
	AudioServer.add_bus()
	effect_bus_index = AudioServer.bus_count - 1
	AudioServer.set_bus_name(effect_bus_index, effect_bus_name)
	AudioServer.set_bus_send(effect_bus_index, bus)
	player.bus = effect_bus_name

	steam_reflection_hrtf = ClassDB.instantiate("AudioEffectSteamReflectionHRTF") as AudioEffect
	steam_reflection_send = ClassDB.instantiate("AudioEffectSteamReflectionSend") as AudioEffect
	steam_direct = ClassDB.instantiate("AudioEffectSteamDirect") as AudioEffect
	steam_hrtf = ClassDB.instantiate("AudioEffectSteamSpatial") as AudioEffect
	if steam_reflection_send == null or steam_direct == null or steam_hrtf == null or steam_reflection_hrtf == null:
		push_error("Steam spatial effects are unavailable; build and load the GDExtension first")
		return
	steam_reflection_send.resource_name = "Reflection Send"
	steam_direct.resource_name = "Direct Path"
	steam_hrtf.resource_name = "HRTF Spatializer"
	steam_reflection_hrtf.resource_name = "Reflection HRTF Return"
	steam_reflection_send.set("target", steam_reflection_hrtf)
	AudioServer.add_bus_effect(effect_bus_index, steam_reflection_send)
	AudioServer.add_bus_effect(effect_bus_index, steam_direct)
	AudioServer.add_bus_effect(effect_bus_index, steam_hrtf)
	AudioServer.add_bus_effect(effect_bus_index, steam_reflection_hrtf)
	_push_reflection_parameters()
	notify_property_list_changed()
	if Engine.is_editor_hint():
		# add_bus() refreshes the Audio dock immediately, before this function has
		# populated the new bus. add_bus_effect() has no public change signal;
		# Godot's own dock calls its private _update_bus() after adding an effect.
		# Re-emit the public layout signal on the next idle step so the dock rebuilds
		# once, after the complete four-effect chain is present.
		AudioServer.call_deferred("emit_signal", &"bus_layout_changed")

func _bus_names_hint(include_construction_choices: bool) -> String:
	var names := PackedStringArray()
	if include_construction_choices:
		names.append(String(SPATIAL_BUS_CONSTRUCT_RUNTIME))
		names.append(String(SPATIAL_BUS_CONSTRUCT_NOW))
	for bus_index in AudioServer.bus_count:
		names.append(String(AudioServer.get_bus_name(bus_index)))
	return ",".join(names)

func _validate_property(property: Dictionary) -> void:
	if property.name == "bus":
		property.hint = _get_native_audio_bus_hint()
		property.hint_string = ""
	elif property.name == "spatial_effect_bus":
		property.hint = PROPERTY_HINT_ENUM
		property.hint_string = _bus_names_hint(true)

func _get_native_audio_bus_hint() -> int:
	if _native_audio_bus_hint >= 0:
		return _native_audio_bus_hint
	var native_player := AudioStreamPlayer3D.new()
	for property in native_player.get_property_list():
		if property.name == &"bus":
			_native_audio_bus_hint = property.hint
			break
	native_player.free()
	return _native_audio_bus_hint

func _get_property_list() -> Array[Dictionary]:
	if steam_reflection_send == null or steam_direct == null or steam_hrtf == null or steam_reflection_hrtf == null:
		return []
	var properties: Array[Dictionary] = [{
		"name": "Spatial Effect Bus Effects",
		"type": TYPE_NIL,
		"usage": PROPERTY_USAGE_GROUP,
	}]
	for property_name in [&"reflection_send_effect", &"direct_path_effect", &"hrtf_spatializer_effect", &"reflection_hrtf_effect"]:
		properties.append({
			"name": property_name,
			"type": TYPE_OBJECT,
			"hint": PROPERTY_HINT_RESOURCE_TYPE,
			"hint_string": "AudioEffect",
			"usage": PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY,
		})
	return properties

func _get(property: StringName) -> Variant:
	match property:
		&"reflection_send_effect":
			return steam_reflection_send
		&"direct_path_effect":
			return steam_direct
		&"hrtf_spatializer_effect":
			return steam_hrtf
		&"reflection_hrtf_effect":
			return steam_reflection_hrtf
	return null

func _exit_tree() -> void:
	if Engine.is_editor_hint() or not owns_effect_bus or effect_bus_name == &"":
		return
	var current_index := AudioServer.get_bus_index(effect_bus_name)
	if current_index >= 0:
		AudioServer.remove_bus(current_index)

func _update_spatialization() -> void:
	if steam_reflection_send == null or steam_direct == null or steam_hrtf == null or steam_reflection_hrtf == null:
		return
	# Reflection controls do not depend on a listener. Keep the live effect in
	# sync even when an editor viewport has not produced a camera yet.
	_push_reflection_parameters()
	var listener := _get_spatial_listener()
	if listener == null:
		return

	var local_position: Vector3 = listener.global_transform.orthonormalized().affine_inverse() * global_position
	var distance := local_position.length()
	var direction := local_position.normalized() if distance > CMP_EPSILON else Vector3(0.0, 0.0, -1.0)
	var source_directivity := 1.0
	if emission_angle_enabled and distance > CMP_EPSILON:
		var source_to_listener := listener.global_position - global_position
		var source_forward := -global_basis.orthonormalized().z
		var angle := rad_to_deg(acos(clampf(source_to_listener.normalized().dot(source_forward), -1.0, 1.0)))
		if angle > emission_angle_degrees:
			source_directivity = db_to_linear(emission_angle_filter_attenuation_db)

	# SteamDirect consumes ordinary direct-path properties, just as Godot's
	# Panner consumes `pan`. Its gain and EQ stages smooth changes internally.
	var normalized_distance := distance / maxf(unit_size, CMP_EPSILON)
	var distance_attenuation := 1.0
	match attenuation_model:
		AudioStreamPlayer3D.ATTENUATION_INVERSE_DISTANCE:
			distance_attenuation = 1.0 / maxf(normalized_distance, 1.0)
		AudioStreamPlayer3D.ATTENUATION_INVERSE_SQUARE_DISTANCE:
			distance_attenuation = 1.0 / maxf(normalized_distance * normalized_distance, 1.0)
		AudioStreamPlayer3D.ATTENUATION_LOGARITHMIC:
			distance_attenuation = exp(-maxf(log(maxf(normalized_distance, 1.0)), 0.0))
		AudioStreamPlayer3D.ATTENUATION_DISABLED:
			distance_attenuation = 1.0
	steam_direct.set("distance_attenuation_enabled", attenuation_model != AudioStreamPlayer3D.ATTENUATION_DISABLED)
	steam_direct.set("distance_attenuation", distance_attenuation)
	steam_direct.set("air_absorption", Vector3(
			exp(-0.0002 * distance),
			exp(-0.0017 * distance),
			exp(-0.0182 * distance)))
	steam_direct.set("directivity", source_directivity)

	var source_gain := db_to_linear(minf(volume_db, max_db))
	if max_distance > 0.0:
		source_gain *= maxf(0.0, 1.0 - distance / max_distance)
	steam_reflection_send.set("enabled", reflections_enabled)
	# These presets are parametric reverbs, not geometry-baked impulse responses,
	# so they contain no propagation loss of their own. Feed them with the same
	# source/listener distance law as the direct path; reflection_send_db remains
	# an independent artistic wet-send control.
	steam_reflection_send.set(
			"send_gain",
			source_gain * distance_attenuation * db_to_linear(reflection_send_db))
	steam_reflection_send.set("reverb_times", reflection_reverb_times)
	steam_reflection_hrtf.call(
			"set_listener_parameters",
			listener.global_basis.orthonormalized(),
			db_to_linear(reflection_wet_db))
	var spatial_blend := clampf(
			float(ProjectSettings.get_setting("audio/general/3d_panning_strength")) * panning_strength,
			0.0,
			1.0)
	steam_hrtf.call("set_source_parameters", direction, source_gain, spatial_blend)
	_update_doppler(listener, local_position, source_gain)

func _get_spatial_listener() -> Node3D:
	# A deliberately-current listener (as used by ineditordemo) takes precedence
	# over the editor camera. This keeps the same node useful for both kinds of
	# preview without the editor camera silently stealing the full demo's role.
	var listener: Node3D = get_viewport().get_audio_listener_3d()
	if listener != null:
		return listener
	var scene_root: Node = self
	while scene_root.get_parent() != null and not (scene_root.get_parent() is Viewport):
		scene_root = scene_root.get_parent()
	for candidate in scene_root.find_children("*", "AudioListener3D", true, false):
		if candidate is AudioListener3D and candidate.is_current():
			return candidate
	if Engine.is_editor_hint():
		# The edited scene's runtime viewport is not the 3D editor viewport. Read
		# its camera explicitly so orbiting, rotating and zooming update HRTF.
		for viewport_index in 4:
			var editor_viewport := EditorInterface.get_editor_viewport_3d(viewport_index)
			if editor_viewport == null:
				continue
			var editor_camera := editor_viewport.get_camera_3d()
			if editor_camera != null:
				return editor_camera
	return get_viewport().get_camera_3d()

func _is_in_active_editor_scene() -> bool:
	if not Engine.is_editor_hint():
		return true
	# Tool scripts created by tests and editor plugins can live in the editor's
	# SceneTree without belonging to an open scene tab. They must not be mistaken
	# for a background tab and stopped.
	if not is_part_of_edited_scene():
		return true
	var edited_root := EditorInterface.get_edited_scene_root()
	return edited_root == null or edited_root == self or edited_root.is_ancestor_of(self)

func _configure_godot_player() -> void:
	var player := _get_godot_player()
	if player == null:
		return
	player.attenuation_model = attenuation_model
	player.unit_size = unit_size
	player.max_distance = max_distance
	player.max_db = max_db
	player.volume_db = volume_db
	player.panning_strength = panning_strength
	player.pitch_scale = _pitch_scale
	player.doppler_tracking = doppler_tracking
	player.emission_angle_enabled = emission_angle_enabled
	player.emission_angle_degrees = emission_angle_degrees
	player.emission_angle_filter_attenuation_db = emission_angle_filter_attenuation_db
	player.attenuation_filter_cutoff_hz = attenuation_filter_cutoff_hz
	player.attenuation_filter_db = attenuation_filter_db
	player.bus = bus

func _update_doppler(listener: Node3D, local_position: Vector3, weight: float) -> void:
	var actual_pitch := _pitch_scale
	if doppler_tracking != AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED and not disable_doppler:
		var source_velocity: Vector3 = $VelocityTracker3D.get_tracked_linear_velocity()
		var listener_velocity := Vector3.ZERO
		var listener_tracker := listener.get_node_or_null("VelocityTracker3D")
		if listener_tracker != null:
			listener_velocity = listener_tracker.get_tracked_linear_velocity()
		var relative_velocity: Vector3 = listener.global_basis.inverse() * (source_velocity - listener_velocity)
		if relative_velocity.length_squared() > CMP_EPSILON and local_position.length_squared() > CMP_EPSILON and weight > CMP_EPSILON:
			var approaching := local_position.normalized().dot(relative_velocity.normalized())
			actual_pitch = clampf(
					_pitch_scale * SPEED_OF_SOUND / (SPEED_OF_SOUND + relative_velocity.length() * approaching),
					1.0 / 8.0,
					8.0)
	var player := _get_audio_player()
	if player != null:
		player.pitch_scale = actual_pitch

func _physics_process(_delta: float) -> void:
	if Engine.is_editor_hint():
		var current_bus_hint := _bus_names_hint(true)
		if current_bus_hint != _bus_hint_cache:
			_bus_hint_cache = current_bus_hint
			notify_property_list_changed()
	var player := _get_active_player()
	if Engine.is_editor_hint() and not _is_in_active_editor_scene():
		if player != null and player.playing:
			# This is an intentional editor interruption, unlike natural EOF. Record
			# exactly this stop so returning to the scene tab can resume it without
			# treating every stopped one-shot as something that should be restarted.
			_editor_resume_position = player.get_playback_position()
			_editor_resume_pending = _playing_requested
			player.stop()
		return
	if Engine.is_editor_hint() and _editor_resume_pending:
		var resume_position := _editor_resume_position
		_editor_resume_pending = false
		if _playing_requested and player != null and not player.playing:
			play(resume_position)
	if player == null or (Engine.is_editor_hint() and not player.playing):
		return
	if _active_backend == SpatialBackend.GODOT_NATIVE:
		_configure_godot_player()
		return
	if not Engine.is_editor_hint() and $VelocityTracker3D.doppler_tracking != doppler_tracking:
		$VelocityTracker3D._update_dopper_tracking()
	_update_spatialization()
