@tool
extends Node3D

const NATIVE_PREVIEW_BUS := &"Native3DPreview"
const EFFECTS_PRIVATE_BUS := &"PlayerEffectBus_Editor"
const EDITOR_3D_VIEWPORT_COUNT := 4

var _editor_viewport_audio_states: Array[bool] = []
var _scene_viewport_audio_was_enabled := false
var _audio_viewport_override_active := false
var _owned_native_preview_bus: StringName = &""

@export_category("Editor A/B Spatial Preview")
@export var preview_in_editor := false:
	set(value):
		preview_in_editor = value
		if is_inside_tree():
			_sync_preview_state()

@export_tool_button("Toggle A/B Preview", "AudioStreamPlayer3D")
var toggle_preview: Callable = _toggle_preview

@export_node_path("AudioListener3D") var listener_path: NodePath = ^"PreviewListener"
@export_node_path("AudioStreamPlayer3D") var native_player_path: NodePath = ^"SoundSource/NativeAudioStreamPlayer3D"
@export_node_path("Node3D") var effects_player_path: NodePath = ^"SoundSource/AudioPlayerByEffects3D"
@export_group("Source Audition")
@export var mute_native_at_source := false:
	set(value):
		mute_native_at_source = value
		_apply_source_mutes()
@export var mute_effects_at_source := false:
	set(value):
		mute_effects_at_source = value
		_apply_source_mutes()

func _ready() -> void:
	if Engine.is_editor_hint():
		_sync_preview_state()

func _exit_tree() -> void:
	if Engine.is_editor_hint():
		_stop_preview()

func _process(_delta: float) -> void:
	if not Engine.is_editor_hint() or not preview_in_editor:
		return
	_enforce_scene_audio_listener()
	var native_player := get_node_or_null(native_player_path) as AudioStreamPlayer3D
	var effects_stream := _get_effects_stream_player()
	if native_player != null and not native_player.playing:
		native_player.play()
	if effects_stream != null and not effects_stream.playing:
		effects_stream.play()

func _toggle_preview() -> void:
	preview_in_editor = not preview_in_editor
	notify_property_list_changed()

func _sync_preview_state() -> void:
	if not Engine.is_editor_hint():
		return
	if preview_in_editor:
		_start_preview()
	else:
		_stop_preview()

func _start_preview() -> void:
	var native_player := get_node_or_null(native_player_path) as AudioStreamPlayer3D
	var effects_player := get_node_or_null(effects_player_path)
	var effects_stream := _get_effects_stream_player()
	if native_player == null or effects_player == null or effects_stream == null:
		push_warning("The A/B preview needs both the native and effects-based players")
		return

	_ensure_preview_buses()
	# Both implementations receive exactly the same source material. Their outputs
	# remain separate for direct L/R meter comparison. Bus mute is never used:
	# playback is started and stopped at the source nodes themselves.
	native_player.bus = _owned_native_preview_bus
	effects_player.call("begin_editor_preview", EFFECTS_PRIVATE_BUS)
	effects_stream.stream = native_player.stream
	_apply_source_mutes()
	_begin_scene_audio_listener_override()
	native_player.play()
	effects_stream.play()

func _stop_preview() -> void:
	var native_player := get_node_or_null(native_player_path) as AudioStreamPlayer3D
	var effects_player := get_node_or_null(effects_player_path)
	var effects_stream := _get_effects_stream_player()
	if native_player != null:
		native_player.stop()
		native_player.bus = &"Master"
	if effects_stream != null:
		effects_stream.stop()
	if effects_player != null and effects_player.has_method("end_editor_preview"):
		effects_player.call("end_editor_preview")
	_remove_preview_buses()
	_end_scene_audio_listener_override()

func _begin_scene_audio_listener_override() -> void:
	if _audio_viewport_override_active:
		_enforce_scene_audio_listener()
		return
	var scene_viewport := get_viewport()
	_scene_viewport_audio_was_enabled = scene_viewport.is_audio_listener_3d()
	_editor_viewport_audio_states.clear()
	for viewport_index in EDITOR_3D_VIEWPORT_COUNT:
		var editor_viewport := EditorInterface.get_editor_viewport_3d(viewport_index)
		_editor_viewport_audio_states.append(editor_viewport.is_audio_listener_3d())
	_audio_viewport_override_active = true
	_enforce_scene_audio_listener()

func _enforce_scene_audio_listener() -> void:
	if not _audio_viewport_override_active:
		return
	# AudioStreamPlayer3D considers cameras from every audio-enabled viewport in
	# the shared World3D. Exclude the editor cameras so this scene has one listener.
	get_viewport().set_as_audio_listener_3d(true)
	for viewport_index in EDITOR_3D_VIEWPORT_COUNT:
		EditorInterface.get_editor_viewport_3d(viewport_index).set_as_audio_listener_3d(false)
	var listener := get_node_or_null(listener_path) as AudioListener3D
	if listener != null and not listener.is_current():
		listener.make_current()

func _end_scene_audio_listener_override() -> void:
	if not _audio_viewport_override_active:
		return
	get_viewport().set_as_audio_listener_3d(_scene_viewport_audio_was_enabled)
	for viewport_index in mini(EDITOR_3D_VIEWPORT_COUNT, _editor_viewport_audio_states.size()):
		EditorInterface.get_editor_viewport_3d(viewport_index).set_as_audio_listener_3d(_editor_viewport_audio_states[viewport_index])
	_editor_viewport_audio_states.clear()
	_audio_viewport_override_active = false

func _get_effects_stream_player() -> AudioStreamPlayer:
	var effects_player := get_node_or_null(effects_player_path)
	if effects_player == null:
		return null
	return effects_player.get_node_or_null("AudioStreamPlayer") as AudioStreamPlayer

func _apply_source_mutes() -> void:
	if not is_inside_tree():
		return
	var native_player := get_node_or_null(native_player_path) as AudioStreamPlayer3D
	var effects_stream := _get_effects_stream_player()
	if native_player != null:
		native_player.volume_db = -80.0 if mute_native_at_source else 0.0
	if effects_stream != null:
		effects_stream.volume_db = -80.0 if mute_effects_at_source else 0.0

func _ensure_preview_buses() -> void:
	_remove_preview_buses()

	_owned_native_preview_bus = NATIVE_PREVIEW_BUS
	if AudioServer.get_bus_index(_owned_native_preview_bus) >= 0:
		_owned_native_preview_bus = StringName("%s_%s" % [NATIVE_PREVIEW_BUS, get_instance_id()])
	AudioServer.add_bus()
	var native_bus_index := AudioServer.bus_count - 1
	AudioServer.set_bus_name(native_bus_index, _owned_native_preview_bus)
	AudioServer.set_bus_send(native_bus_index, &"Master")
	AudioServer.add_bus_effect(native_bus_index, AudioEffectSpectrumAnalyzer.new())

	var private_bus_index := AudioServer.get_bus_index(EFFECTS_PRIVATE_BUS)
	if private_bus_index < 0:
		push_warning("The saved PlayerEffectBus_Editor inspection bus is missing")

func _remove_preview_buses() -> void:
	if _owned_native_preview_bus == &"":
		return
	var bus_index := AudioServer.get_bus_index(_owned_native_preview_bus)
	if bus_index >= 0:
		AudioServer.remove_bus(bus_index)
	_owned_native_preview_bus = &""
