@tool
extends Node3D

const NATIVE_PREVIEW_BUS := &"NativeSpatial"
const QUALITY_PRIVATE_BUS := &"QPlayerBus_Editor"
const EDITOR_3D_VIEWPORT_COUNT := 4

var _editor_viewport_audio_states: Array[bool] = []
var _scene_viewport_audio_was_enabled := false
var _audio_viewport_override_active := false

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
@export_node_path("Node3D") var quality_player_path: NodePath = ^"SoundSource/AudioStreamQPlayer3D"
@export_group("Source Audition")
@export var mute_native_at_source := false:
	set(value):
		mute_native_at_source = value
		_apply_source_mutes()
@export var mute_quality_at_source := false:
	set(value):
		mute_quality_at_source = value
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
	var quality_player: Node = _get_quality_player()
	_maintain_preview_routes(native_player, quality_player)
	if native_player != null and not native_player.playing:
		native_player.play()
	if quality_player != null and not quality_player.playing:
		quality_player.play()

func _maintain_preview_routes(native_player: AudioStreamPlayer3D, quality_player: Node) -> void:
	var native_bus_index := AudioServer.get_bus_index(NATIVE_PREVIEW_BUS)
	if native_player != null and native_bus_index >= 0:
		if native_player.bus != NATIVE_PREVIEW_BUS:
			native_player.bus = NATIVE_PREVIEW_BUS
		if AudioServer.get_bus_send(native_bus_index) != &"Master":
			AudioServer.set_bus_send(native_bus_index, &"Master")
	var quality_bus_index := AudioServer.get_bus_index(QUALITY_PRIVATE_BUS)
	if quality_player != null and quality_bus_index >= 0:
		if AudioServer.get_bus_send(quality_bus_index) != &"Master":
			AudioServer.set_bus_send(quality_bus_index, &"Master")
		quality_player.begin_editor_preview(QUALITY_PRIVATE_BUS)
		if native_player != null and quality_player.stream != native_player.stream:
			quality_player.stream = native_player.stream

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
	var quality_player: Node = _get_quality_player()
	if native_player == null or quality_player == null:
		push_warning("The A/B preview needs both native and quality players")
		return

	_ensure_preview_buses()
	# Both implementations receive exactly the same source material. Their outputs
	# remain separate for direct L/R meter comparison. Bus mute is never used:
	# playback is started and stopped at the source nodes themselves.
	# Stop first because playback_type only selects the path for a newly-created
	# playback; it cannot migrate a sample that the editor already started.
	native_player.stop()
	quality_player.stop()
	native_player.bus = NATIVE_PREVIEW_BUS
	native_player.playback_type = AudioServer.PLAYBACK_TYPE_STREAM
	quality_player.playback_type = AudioServer.PLAYBACK_TYPE_STREAM
	quality_player.begin_editor_preview(QUALITY_PRIVATE_BUS)
	quality_player.stream = native_player.stream
	_apply_source_mutes()
	_begin_scene_audio_listener_override()
	native_player.play()
	quality_player.play()

func _stop_preview() -> void:
	var native_player := get_node_or_null(native_player_path) as AudioStreamPlayer3D
	var quality_player: Node = _get_quality_player()
	if native_player != null:
		native_player.stop()
		native_player.bus = &"Master"
	if quality_player != null:
		quality_player.stop()
		quality_player.end_editor_preview()
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

func _get_quality_player() -> Node:
	return get_node_or_null(quality_player_path)

func _apply_source_mutes() -> void:
	if not is_inside_tree():
		return
	var native_player := get_node_or_null(native_player_path) as AudioStreamPlayer3D
	var quality_player: Node = _get_quality_player()
	if native_player != null:
		native_player.volume_db = -80.0 if mute_native_at_source else 0.0
	if quality_player != null:
		quality_player.volume_db = -80.0 if mute_quality_at_source else 0.0
	if not Engine.is_editor_hint():
		return
	# These controls are used for live A/B audition. If one side is muted while
	# preview is stopped, merely changing its gain would expose a player that has
	# never been started. Enter preview and keep the newly audible side running.
	if not preview_in_editor and (mute_native_at_source or mute_quality_at_source):
		preview_in_editor = true
		return
	if preview_in_editor:
		if native_player != null and not mute_native_at_source and not native_player.playing:
			native_player.play()
		if quality_player != null and not mute_quality_at_source and not quality_player.playing:
			quality_player.play()

func _ensure_preview_buses() -> void:
	var private_bus_index := AudioServer.get_bus_index(QUALITY_PRIVATE_BUS)
	if private_bus_index < 0:
		push_warning("The saved QPlayerBus_Editor spatial effect bus is missing")
	if AudioServer.get_bus_index(NATIVE_PREVIEW_BUS) < 0:
		push_warning("The saved NativeSpatial preview bus is missing")

func _remove_preview_buses() -> void:
	# Both preview buses are authored in default_bus_layout.tres so the Audio
	# dock always exposes the complete mechanism. Preview owns no buses.
	pass
