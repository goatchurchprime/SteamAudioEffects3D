@tool
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var packed := load("res://ineditordemo.tscn") as PackedScene
	var demo := packed.instantiate()
	root.add_child(demo)
	await process_frame

	assert(AudioServer.bus_count == 3, "The editor demo should begin with Master and its two saved preview buses")
	var private_bus_index := AudioServer.get_bus_index(&"QPlayerBus_Editor")
	var native_bus_index := AudioServer.get_bus_index(&"NativeSpatial")
	assert(private_bus_index >= 0)
	assert(native_bus_index >= 0)
	assert(AudioServer.get_bus_effect_count(private_bus_index) == 4)
	# Match interactive editor use: the saved buses normally sit silent long
	# enough for automatic bus disabling before the user starts the preview.
	await create_timer(3.0).timeout
	# Keep the direction assertions independent of the position last saved while
	# interactively dragging the demo source.
	var source := demo.get_node("SoundSource") as Node3D
	var quality_player := demo.get_node("SoundSource/AudioStreamQPlayer3D")
	var native_player := demo.get_node("SoundSource/NativeAudioStreamPlayer3D") as AudioStreamPlayer3D
	# A/B mute controls must start preview when the other renderer has not yet
	# been started; this is the common manual-editor workflow.
	demo.mute_quality_at_source = true
	await process_frame
	assert(demo.preview_in_editor)
	assert(native_player.playing, "Muting quality must expose a running native comparison")
	demo.mute_quality_at_source = false
	demo.mute_native_at_source = false
	quality_player.apply_reflection_preset(AudioStreamQPlayer3D.ReflectionPreset.STONE_TUNNEL)
	source.position = Vector3(5.0, 0.0, 0.0)
	demo.preview_in_editor = true
	await process_frame
	assert(AudioServer.get_bus_send(private_bus_index) == &"Master")

	# Steam receives the same listener-local hard-right direction that the native
	# player uses, but HRTF crossfeed means its left channel should not be silent.
	await physics_frame
	await physics_frame
	for effect_index in 4:
		assert(AudioServer.is_bus_effect_enabled(private_bus_index, effect_index))
	var reflection_send := AudioServer.get_bus_effect(private_bus_index, 0)
	var direct_effect := AudioServer.get_bus_effect(private_bus_index, 1)
	assert(direct_effect != null and direct_effect.get_class() == "AudioEffectSteamDirect")
	assert(is_equal_approx(direct_effect.distance_attenuation, 1.0), "Steam Direct should receive the near-field gain, got %s" % direct_effect.distance_attenuation)
	assert(direct_effect.air_absorption.z < 1.0, "Steam Direct should receive distance-derived air absorption")
	var steam_effect := AudioServer.get_bus_effect(private_bus_index, 2)
	assert(steam_effect != null and steam_effect.get_class() == "AudioEffectSteamSpatial")
	assert(steam_effect.direction.x > 0.99, "Steam direction should point hard right, got %s" % steam_effect.direction)
	var reflection_return := AudioServer.get_bus_effect(private_bus_index, 3)
	assert(reflection_send.get_class() == "AudioEffectSteamReflectionSend")
	assert(reflection_return.get_class() == "AudioEffectSteamReflectionHRTF")
	assert(reflection_send.target == reflection_return, "Reflection send must target the stable return resource")
	assert(reflection_send.enabled, "QPlayer should update the reflection send parameters")
	assert(reflection_send.reverb_times == Vector3(3.2, 2.7, 2.0), "The selected reflection preset must reach the live editor effect")
	# A real desktop driver may need several callbacks to wake after prolonged
	# editor silence. Wait for actual playback progress instead of one fixed tick.
	var quality_stream_player := demo.get_node("SoundSource/AudioStreamQPlayer3D/AudioStreamPlayer") as AudioStreamPlayer
	for attempt in 30:
		if quality_stream_player.get_playback_position() > 0.05:
			break
		await create_timer(0.1).timeout
	var effects_left_peak := AudioServer.get_bus_peak_volume_left_db(private_bus_index, 0)
	var effects_right_peak := AudioServer.get_bus_peak_volume_right_db(private_bus_index, 0)
	var native_left_peak := AudioServer.get_bus_peak_volume_left_db(native_bus_index, 0)
	var native_right_peak := AudioServer.get_bus_peak_volume_right_db(native_bus_index, 0)
	var master_left_peak := AudioServer.get_bus_peak_volume_left_db(0, 0)
	var master_right_peak := AudioServer.get_bus_peak_volume_right_db(0, 0)
	print("editor routes: native=", demo.get_node("SoundSource/NativeAudioStreamPlayer3D").bus, " quality=", quality_stream_player.bus, " playing=", quality_stream_player.playing, " position=", quality_stream_player.get_playback_position(), " sends=", AudioServer.get_bus_send(native_bus_index), "/", AudioServer.get_bus_send(private_bus_index))
	print("editor peaks: effects=", Vector2(effects_left_peak, effects_right_peak), " native=", Vector2(native_left_peak, native_right_peak), " master=", Vector2(master_left_peak, master_right_peak))
	assert(effects_left_peak > -70.0, "HRTF crossfeed should keep the effects left channel audible, got %s dB" % effects_left_peak)
	assert(effects_right_peak > effects_left_peak, "A right-side HRTF source should be louder on the right: %s vs %s dB" % [effects_right_peak, effects_left_peak])
	# Native editor preview may still include an editor-camera listener depending
	# on how this headless editor session was created. Its dedicated meter must be
	# live; the Steam direction and panning assertions above use the explicit one.
	assert(maxf(native_left_peak, native_right_peak) > -70.0, "The authored native preview bus should be live")
	assert(maxf(master_left_peak, master_right_peak) > -70.0, "The preview buses should reach Master, got %s / %s dB" % [master_left_peak, master_right_peak])
	source.position.x = -absf(source.position.x)
	await physics_frame
	await physics_frame
	assert(steam_effect.direction.x < -0.99, "Steam direction should point hard left, got %s" % steam_effect.direction)
	quality_player.stop()
	await create_timer(0.1).timeout
	var reflection_tail_peak := maxf(
			AudioServer.get_bus_peak_volume_left_db(private_bus_index, 0),
			AudioServer.get_bus_peak_volume_right_db(private_bus_index, 0))
	assert(reflection_tail_peak > -70.0, "Reflection effects should render their tail after the source stops, got %s dB" % reflection_tail_peak)

	demo.preview_in_editor = false
	await process_frame
	assert(AudioServer.bus_count == 3, "Stopping preview must retain the authored preview buses")
	assert(AudioServer.get_bus_name(0) == &"Master")

	# Closing an edited scene while preview is active must perform the same cleanup.
	demo.preview_in_editor = true
	await process_frame
	await process_frame
	var reopened_bus_names: Array[StringName] = []
	for bus_index in AudioServer.bus_count:
		reopened_bus_names.append(AudioServer.get_bus_name(bus_index))
	assert(AudioServer.bus_count == 3, "Restarting preview must reuse the authored buses, got %s" % [reopened_bus_names])
	demo.queue_free()
	await process_frame
	assert(AudioServer.bus_count == 3, "Closing the preview scene must leave the authored buses")
	assert(AudioServer.get_bus_name(0) == &"Master")
	print("in-editor bus lifecycle smoke passed")
	quit()
