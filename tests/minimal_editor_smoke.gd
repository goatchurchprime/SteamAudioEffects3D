@tool
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var packed := load("res://minimaltest.tscn") as PackedScene
	var scene := packed.instantiate()
	root.add_child(scene)
	await process_frame
	await physics_frame
	var quality := scene.get_node("AudioStreamQPlayer3D") as AudioStreamQPlayer3D
	var private_player := quality.get_node("AudioStreamPlayer") as AudioStreamPlayer
	assert(is_equal_approx(quality.pitch_scale, 1.0), "QPlayer pitch_scale must default to 1.0")
	assert(private_player.bus == &"QPlayerBus_Editor", "The minimal editor player must start on the effect bus")
	quality.playing = true
	assert(private_player.playing, "The minimal editor QPlayer playing property must start its private player")
	for attempt in 30:
		if private_player.get_playback_position() > 0.05:
			break
		await create_timer(0.1).timeout
	assert(private_player.get_playback_position() > 0.05, "The minimal editor QPlayer must advance playback")
	quality.apply_reflection_preset(AudioStreamQPlayer3D.ReflectionPreset.EXAGGERATED_CAVERN)
	await physics_frame
	await physics_frame
	assert(quality.steam_reflection_send.enabled)
	assert(quality.steam_reflection_send.reverb_times == Vector3(5.0, 4.0, 3.0))
	# A minimal scene has no AudioListener3D. Its tool preview must therefore use
	# the 3D editor camera, not the otherwise unrelated edited-scene viewport.
	var editor_camera := EditorInterface.get_editor_viewport_3d(0).get_camera_3d()
	assert(editor_camera != null)
	var saved_camera_transform := editor_camera.global_transform
	editor_camera.global_transform = Transform3D(Basis.IDENTITY, quality.global_position - Vector3(5.0, 0.0, 0.0))
	quality._update_spatialization()
	assert(quality.steam_hrtf.direction.x > 0.99, "Editor camera left of source must render right, got %s" % quality.steam_hrtf.direction)
	var near_reflection_send: float = quality.steam_reflection_send.send_gain
	editor_camera.global_transform = Transform3D(Basis.IDENTITY, quality.global_position + Vector3(20.0, 0.0, 0.0))
	quality._update_spatialization()
	assert(quality.steam_hrtf.direction.x < -0.99, "Editor camera right of source must render left, got %s" % quality.steam_hrtf.direction)
	assert(is_equal_approx(quality.steam_reflection_send.send_gain, near_reflection_send * 0.5),
			"Reflection preset send must follow inverse-distance attenuation")
	editor_camera.global_transform = saved_camera_transform
	var bus_index := AudioServer.get_bus_index(&"QPlayerBus_Editor")
	assert(maxf(
		AudioServer.get_bus_peak_volume_left_db(bus_index, 0),
		AudioServer.get_bus_peak_volume_right_db(bus_index, 0)) > -70.0,
		"The minimal editor QPlayer must produce audible effect-bus output")
	quality.stop()
	scene.queue_free()
	await process_frame
	print("minimal editor QPlayer smoke passed")
	quit()
