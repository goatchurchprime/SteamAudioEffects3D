extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if not ClassDB.class_exists("AudioEffectSteamDirect") or not ClassDB.class_exists("AudioEffectSteamSpatial") or not ClassDB.class_exists("AudioEffectSteamReflectionSend") or not ClassDB.class_exists("AudioEffectSteamReflectionHRTF"):
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
	spatial_effect.direction = Vector3.RIGHT
	spatial_effect.gain = 0.5
	spatial_effect.spatial_blend = 1.0
	var reflection_return = ClassDB.instantiate("AudioEffectSteamReflectionHRTF")
	reflection_return.listener_orientation = Basis.IDENTITY
	reflection_return.dry_gain = 0.0
	reflection_return.wet_gain = 0.5
	assert(reflection_return.dry_gain == 0.0)
	var reflection_send = ClassDB.instantiate("AudioEffectSteamReflectionSend")
	reflection_send.enabled = true
	reflection_send.send_gain = 0.25
	reflection_send.reverb_times = Vector3(1.0, 0.8, 0.5)
	reflection_send.target = reflection_return
	assert(reflection_send.target == reflection_return)
	reflection_send.energy_field_duration = 0.02
	var field := PackedFloat32Array()
	field.resize(4 * 3 * 2)
	for band in 3:
		field[(0 * 3 + band) * 2] = 0.01
		field[(0 * 3 + band) * 2 + 1] = 0.005
		field[(3 * 3 + band) * 2] = 0.006
	reflection_send.energy_field = field
	assert(reflection_send.energy_field.size() == 24)
	assert(reflection_send.get_energy_field_bin_count() == 2)

	AudioServer.add_bus()
	var bus_index := AudioServer.bus_count - 1
	AudioServer.set_bus_name(bus_index, "SteamSpatialSmoke")
	AudioServer.add_bus_effect(bus_index, reflection_send)
	AudioServer.add_bus_effect(bus_index, direct_effect)
	AudioServer.add_bus_effect(bus_index, spatial_effect)
	AudioServer.add_bus_effect(bus_index, reflection_return)
	await process_frame

	for effect_index in 4:
		var instance := AudioServer.get_bus_effect_instance(bus_index, effect_index)
		if instance == null or not instance.call("is_ready"):
			push_error("Steam Audio effect instance %d did not initialize" % effect_index)
			AudioServer.remove_bus(bus_index)
			quit(1)
			return

	# Recreate the receiver playback instance while retaining its resource. The
	# send must remain linked directly to the stable receiver resource.
	AudioServer.remove_bus_effect(bus_index, 3)
	AudioServer.add_bus_effect(bus_index, reflection_return, 3)
	await process_frame
	assert(reflection_send.target == reflection_return)
	var rebuilt_return_instance := AudioServer.get_bus_effect_instance(bus_index, 3)
	assert(rebuilt_return_instance != null and rebuilt_return_instance.call("is_ready"))

	AudioServer.remove_bus(bus_index)
	print("Steam direct + spatial + loadable energy-field reflection smoke test passed")
	quit(0)
