extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var player := AudioStreamQPlayer3D.new()
	player.stream = load("res://audiocheck.net_pinknoise.wav")
	player.volume_db = 0.0
	player.direct_path_enabled = false
	root.add_child(player)
	await process_frame
	var baked_presets := [
		AudioStreamQPlayer3D.ReflectionPreset.ABSORBENT_WALL,
		AudioStreamQPlayer3D.ReflectionPreset.SMALL_SHOEBOX,
		AudioStreamQPlayer3D.ReflectionPreset.FAR_WALL_ECHO,
		AudioStreamQPlayer3D.ReflectionPreset.BIG_AUDITORIUM,
	]
	for preset in baked_presets:
		player.apply_reflection_preset(preset)
		assert(not player.reflection_energy_field.is_empty(), "Baked preset %s must load its checked-in energy field" % preset)
		player.play()
		var wet_peak := -200.0
		for attempt in 40:
			await create_timer(0.05).timeout
			wet_peak = maxf(wet_peak, AudioServer.get_bus_peak_volume_left_db(player.effect_bus_index, 0))
			wet_peak = maxf(wet_peak, AudioServer.get_bus_peak_volume_right_db(player.effect_bus_index, 0))
		player.stop()
		assert(wet_peak > -70.0, "Baked preset %s must remain audible; peak was %s dB" % [preset, wet_peak])
		assert(wet_peak < 3.0, "Baked preset %s must leave peak headroom; peak was %s dB" % [preset, wet_peak])
	player.apply_reflection_preset(AudioStreamQPlayer3D.ReflectionPreset.SMALL_SHOEBOX)
	assert(player.reflection_reverb_times.y < 2.0, "The shoebox bake must remain a compact room response")
	player.apply_reflection_preset(AudioStreamQPlayer3D.ReflectionPreset.FAR_WALL_ECHO)
	var echo_bins := int(player.reflection_energy_field.size() / 12)
	var strongest_mid_bin := 0
	for bin in echo_bins:
		if player.reflection_energy_field[echo_bins + bin] > player.reflection_energy_field[echo_bins + strongest_mid_bin]:
			strongest_mid_bin = bin
	assert(strongest_mid_bin in range(26, 30),
			"The far-wall bake must retain its distinct approximately 275 ms arrival; strongest bin was %s" % strongest_mid_bin)
	player.apply_reflection_preset(AudioStreamQPlayer3D.ReflectionPreset.BIG_AUDITORIUM)
	assert(player.reflection_reverb_times.y > 5.0, "The auditorium bake must retain a long simulated decay")
	var reflection_return := AudioServer.get_bus_effect(player.effect_bus_index, 3)
	assert(reflection_return.dry_gain == 0.0)
	player.apply_reflection_preset(AudioStreamQPlayer3D.ReflectionPreset.OFF)
	await create_timer(0.05).timeout
	player.stop()
	player.stream = null
	player.queue_free()
	await process_frame
	await process_frame
	await create_timer(0.05).timeout
	print("All checked-in Steam-baked energy-field audio presets passed")
	quit(0)
