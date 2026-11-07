@tool
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var packed := load("res://ineditordemo.tscn") as PackedScene
	var demo := packed.instantiate()
	root.add_child(demo)
	await process_frame

	assert(AudioServer.bus_count == 2, "The editor demo should begin with Master and its saved inspection bus")
	var private_bus_index := AudioServer.get_bus_index(&"PlayerEffectBus_Editor")
	assert(private_bus_index >= 0)
	assert(AudioServer.get_bus_effect_count(private_bus_index) == 3)
	demo.preview_in_editor = true
	await process_frame
	var native_bus_index := AudioServer.get_bus_index(&"Native3DPreview")
	assert(native_bus_index >= 0)
	assert(AudioServer.get_bus_send(private_bus_index) == &"Master")

	# The source begins directly right of the listener and should reach hard right.
	await physics_frame
	await physics_frame
	var panner := AudioServer.get_bus_effect(private_bus_index, 2) as AudioEffectPanner
	assert(panner != null)
	assert(panner.pan > 0.99, "Effects panner should reach hard right, got %s" % panner.pan)
	await create_timer(0.1).timeout
	var effects_left_peak := AudioServer.get_bus_peak_volume_left_db(private_bus_index, 0)
	var effects_right_peak := AudioServer.get_bus_peak_volume_right_db(private_bus_index, 0)
	var native_left_peak := AudioServer.get_bus_peak_volume_left_db(native_bus_index, 0)
	var native_right_peak := AudioServer.get_bus_peak_volume_right_db(native_bus_index, 0)
	assert(effects_left_peak <= -70.0, "Hard-right effects output should bottom the left meter, got %s dB" % effects_left_peak)
	assert(effects_right_peak > -70.0, "Hard-right effects output should remain audible on the right, got %s dB" % effects_right_peak)
	assert(native_left_peak <= -70.0, "Forced scene listener should bottom the native left meter, got %s dB" % native_left_peak)
	assert(native_right_peak > -70.0, "Forced scene listener should keep native right audible, got %s dB" % native_right_peak)
	var source := demo.get_node("SoundSource") as Node3D
	source.position.x = -absf(source.position.x)
	await physics_frame
	await physics_frame
	assert(panner.pan < -0.99, "Effects panner should reach hard left, got %s" % panner.pan)

	demo.preview_in_editor = false
	await process_frame
	assert(AudioServer.bus_count == 2, "Stopping preview must remove its temporary native meter bus")
	assert(AudioServer.get_bus_name(0) == &"Master")

	# Closing an edited scene while preview is active must perform the same cleanup.
	demo.preview_in_editor = true
	await process_frame
	await process_frame
	var reopened_bus_names: Array[StringName] = []
	for bus_index in AudioServer.bus_count:
		reopened_bus_names.append(AudioServer.get_bus_name(bus_index))
	assert(AudioServer.bus_count == 3, "Restarting preview should restore its native bus, got %s" % [reopened_bus_names])
	demo.queue_free()
	await process_frame
	assert(AudioServer.bus_count == 2, "Closing the preview scene must remove temporary buses")
	assert(AudioServer.get_bus_name(0) == &"Master")
	print("in-editor bus lifecycle smoke passed")
	quit()
