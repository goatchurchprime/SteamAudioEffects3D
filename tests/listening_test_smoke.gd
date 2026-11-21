extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var packed := load("res://listening_test.tscn") as PackedScene
	assert(packed != null, "Listening test scene must load")
	var demo := packed.instantiate()
	root.add_child(demo)
	await process_frame
	await physics_frame

	var listener := demo.get_node("Listener") as AudioListener3D
	var source_rig := demo.get_node("SourceRig") as Node3D
	var native_player := demo.get_node("SourceRig/NativeAudioStreamPlayer3D") as AudioStreamPlayer3D
	var quality_player := demo.get_node("SourceRig/AudioStreamQPlayer3D") as AudioStreamQPlayer3D
	assert(listener.is_current(), "Listening test must make its explicit listener current")
	assert(native_player.stream != null and native_player.stream == quality_player.stream,
			"Both renderers must receive the identical audition stream")

	var source_position_before_listener_turn := source_rig.position
	var listener_yaw_before := listener.rotation_degrees.y
	demo.call("_randomize_listener_yaw")
	await physics_frame
	assert(not is_equal_approx(listener.rotation_degrees.y, listener_yaw_before),
			"Shift+R listener test must select a different yaw")
	assert(source_rig.position == source_position_before_listener_turn,
			"Listener randomization must not move the sound source")

	demo.call("_randomize_hidden_source")
	assert(not demo.get_node("SourceRig/Marker").visible,
			"A blind trial must hide its source marker")
	demo.call("_toggle_playback")
	await process_frame
	assert(quality_player.is_playing(), "QPlayer must be the default audition backend")
	demo.call("_cycle_backend")
	await process_frame
	assert(native_player.is_playing() and not quality_player.is_playing(),
			"Backend switching must remain exclusive")

	demo.call("_stop_players")
	native_player.stream = null
	quality_player.stream = null
	await create_timer(0.15).timeout
	demo.queue_free()
	await process_frame
	listener = null
	source_rig = null
	native_player = null
	quality_player = null
	demo = null
	packed = null
	await process_frame
	print("listening test smoke passed")
	quit()
