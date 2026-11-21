extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var packed := load("res://addons/steam_spatial/audio_stream_q_player_3d.tscn") as PackedScene
	var player := packed.instantiate()
	var generated_stream := AudioStreamGenerator.new()
	generated_stream.mix_rate = 48000.0
	player.stream = generated_stream
	player.max_polyphony = 3
	player.playback_type = AudioServer.PLAYBACK_TYPE_STREAM
	player.volume_linear = 0.5
	root.add_child(player)
	await process_frame
	var available_properties := {}
	for property in player.get_property_list():
		available_properties[property.name] = true
	for property_name in [
		"stream", "attenuation_model", "volume_db", "volume_linear", "unit_size", "max_db",
		"pitch_scale", "playing", "autoplay", "stream_paused", "max_distance", "max_polyphony",
		"panning_strength", "bus", "playback_type", "emission_angle_enabled",
		"emission_angle_degrees", "emission_angle_filter_attenuation_db",
		"attenuation_filter_cutoff_hz", "attenuation_filter_db", "doppler_tracking",
	]:
		assert(available_properties.has(property_name), "Missing AudioStreamPlayer3D-compatible property: %s" % property_name)

	var internal_player := player.get_node("AudioStreamPlayer") as AudioStreamPlayer
	assert(internal_player.stream == generated_stream, "The wrapper stream must reach the private player")
	assert(internal_player.max_polyphony == 3, "The wrapper polyphony must reach the private player")
	assert(internal_player.playback_type == AudioServer.PLAYBACK_TYPE_STREAM, "Playback type must reach the private player")
	player.playback_type = AudioServer.PLAYBACK_TYPE_SAMPLE
	assert(player.playback_type == AudioServer.PLAYBACK_TYPE_SAMPLE, "The public playback choice must remain available to the native/Web fallback")
	assert(internal_player.playback_type == AudioServer.PLAYBACK_TYPE_STREAM, "The private player must remain in Godot's stream mixer")
	assert(player.get_node("GodotAudioStreamPlayer3D").playback_type == AudioServer.PLAYBACK_TYPE_SAMPLE)
	assert(is_equal_approx(player.volume_db, linear_to_db(0.5)), "volume_linear must alias volume_db")
	var private_bus_index := AudioServer.get_bus_index(internal_player.bus)
	assert(private_bus_index >= 0, "The player must create its private bus")
	assert(AudioServer.get_bus_effect_count(private_bus_index) == 2)
	assert(AudioServer.get_bus_effect(private_bus_index, 0).get_class() == "AudioEffectSteamDirect")
	assert(AudioServer.get_bus_effect(private_bus_index, 1).get_class() == "AudioEffectSteamSpatial")
	player._update_spatialization()
	var spatial_effect := AudioServer.get_bus_effect(private_bus_index, 1)
	assert(spatial_effect.source_position == player.global_position)
	for effect_index in 2:
		assert(AudioServer.is_bus_effect_enabled(private_bus_index, effect_index))

	player.play()
	await process_frame
	assert(player.playing, "playing must report the private player's live state")
	assert(player.is_playing(), "is_playing() must be forwarded")
	assert(player.has_stream_playback(), "The stream playback must be available through the wrapper")
	assert(player.get_stream_playback() == internal_player.get_stream_playback())

	player.stream_paused = true
	assert(internal_player.stream_paused, "stream_paused must reach the private player")
	player.stream_paused = false
	player.stop()
	assert(not player.playing, "stop() must update the public playing state")

	player.queue_free()
	await process_frame

	var fallback := packed.instantiate()
	fallback.spatial_backend = AudioStreamQPlayer3D.SpatialBackend.GODOT_NATIVE
	fallback.stream = generated_stream
	root.add_child(fallback)
	await process_frame
	assert(fallback.get_active_backend() == AudioStreamQPlayer3D.SpatialBackend.GODOT_NATIVE)
	assert(fallback.get_spatial_capabilities().backend == "godot_native")
	assert(fallback.effect_bus_index == -1, "The platform fallback must not create a Steam effect bus")
	fallback.play()
	await process_frame
	assert(fallback.get_node("GodotAudioStreamPlayer3D").is_playing())
	fallback.stop()
	fallback.queue_free()
	await process_frame
	print("AudioStreamQPlayer3D compatibility smoke passed")
	quit()
