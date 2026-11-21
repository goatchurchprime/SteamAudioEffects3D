extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if not ClassDB.class_exists("AudioEffectSteamDirect") or not ClassDB.class_exists("AudioEffectSteamSpatial"):
		push_error("Steam Audio effects were not registered")
		quit(1)
		return

	var direct_effect = ClassDB.instantiate("AudioEffectSteamDirect")
	direct_effect.distance_attenuation_enabled = true
	direct_effect.distance_attenuation = 0.5
	direct_effect.air_absorption_enabled = true
	direct_effect.air_absorption = Vector3(0.9, 0.8, 0.7)
	direct_effect.directivity_enabled = true
	direct_effect.occlusion_enabled = true
	direct_effect.occlusion = 0.6
	direct_effect.transmission_enabled = true
	direct_effect.transmission_type = direct_effect.TRANSMISSION_FREQUENCY_DEPENDENT
	direct_effect.transmission = Vector3(0.5, 0.4, 0.3)
	direct_effect.directivity = 0.75
	assert(direct_effect.distance_attenuation == 0.5)
	assert(direct_effect.directivity == 0.75)
	assert(direct_effect.air_absorption == Vector3(0.9, 0.8, 0.7))
	assert(direct_effect.transmission == Vector3(0.5, 0.4, 0.3))
	var spatial_effect = ClassDB.instantiate("AudioEffectSteamSpatial")
	spatial_effect.set_spatial_parameters(
			Vector3(11.0, 2.0, 3.0),
			Vector3(10.0, 2.0, 3.0),
			Quaternion(Vector3.UP, PI * 0.5),
			0.5,
			1.0)
	assert(spatial_effect.source_position == Vector3(11.0, 2.0, 3.0))
	assert(spatial_effect.receiver_position == Vector3(10.0, 2.0, 3.0))
	assert(is_equal_approx(spatial_effect.gain, 0.5))

	AudioServer.add_bus()
	var bus_index := AudioServer.bus_count - 1
	AudioServer.set_bus_name(bus_index, "SteamSpatialSmoke")
	AudioServer.add_bus_effect(bus_index, direct_effect)
	AudioServer.add_bus_effect(bus_index, spatial_effect)
	await process_frame

	for effect_index in 2:
		var instance := AudioServer.get_bus_effect_instance(bus_index, effect_index)
		if instance == null or not instance.call("is_ready"):
			push_error("Steam Audio effect instance %d did not initialize" % effect_index)
			AudioServer.remove_bus(bus_index)
			quit(1)
			return

	AudioServer.remove_bus(bus_index)
	print("Steam direct + global-transform HRTF smoke test passed")
	quit(0)
