extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if not ClassDB.class_exists("AudioEffectSteamSpatial"):
		push_error("AudioEffectSteamSpatial was not registered")
		quit(1)
		return

	var effect = ClassDB.instantiate("AudioEffectSteamSpatial")
	effect.direction = Vector3.RIGHT
	effect.gain = 0.5
	effect.spatial_blend = 1.0

	AudioServer.add_bus()
	var bus_index := AudioServer.bus_count - 1
	AudioServer.set_bus_name(bus_index, "SteamSpatialSmoke")
	AudioServer.add_bus_effect(bus_index, effect)
	await process_frame

	var instance := AudioServer.get_bus_effect_instance(bus_index, 0)
	if instance == null or not instance.call("is_ready"):
		push_error("Steam Audio effect instance did not initialize")
		AudioServer.remove_bus(bus_index)
		quit(1)
		return

	AudioServer.remove_bus(bus_index)
	print("Steam spatial smoke test passed")
	quit(0)
