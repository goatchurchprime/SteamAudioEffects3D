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
	var exported_properties := quality.get_property_list()
	var exported_bus_property := exported_properties.filter(func(property): return property.name == &"bus").front() as Dictionary
	var exported_spatial_bus_property := exported_properties.filter(func(property): return property.name == &"spatial_effect_bus").front() as Dictionary
	var native_player := AudioStreamPlayer3D.new()
	var native_bus_property := native_player.get_property_list().filter(func(property): return property.name == &"bus").front() as Dictionary
	native_player.free()
	assert(exported_bus_property.hint == native_bus_property.hint, "Output bus must use the running Godot version's native audio-bus editor: QPlayer=%s native=%s" % [exported_bus_property, native_bus_property])
	assert(exported_spatial_bus_property.hint == PROPERTY_HINT_ENUM, "Spatial bus declaration must request a dropdown instead of a StringName text field: %s" % [exported_spatial_bus_property])
	assert(String(exported_spatial_bus_property.hint_string).begins_with("Construct Runtime,Construct Now,Master"), "Godot's actual property list must contain construction modes followed by live buses: %s" % [exported_spatial_bus_property])
	assert(is_equal_approx(quality.pitch_scale, 1.0), "QPlayer pitch_scale must default to 1.0")
	assert(quality.spatial_effect_bus == AudioStreamQPlayer3D.SPATIAL_BUS_CONSTRUCT_RUNTIME, "The minimal editor player must defer its spatial bus to runtime")
	assert(private_player.bus == &"Master", "The minimal editor player must not create or attach an effects bus automatically")
	var layout_change_count := [0]
	var count_layout_change := func(): layout_change_count[0] += 1
	AudioServer.bus_layout_changed.connect(count_layout_change)
	quality.spatial_effect_bus = AudioStreamQPlayer3D.SPATIAL_BUS_CONSTRUCT_NOW
	await process_frame
	AudioServer.bus_layout_changed.disconnect(count_layout_change)
	assert(layout_change_count[0] >= 2, "Construct Now must notify once for bus creation and again after its effects are populated")
	assert(String(quality.spatial_effect_bus).begins_with("QPlayerBus_"), "Construct Now must assign a uniquely named spatial bus")
	assert(private_player.bus == quality.spatial_effect_bus, "Construct Now must route the private player")
	assert(quality.spatial_effect_bus == quality._generated_effect_bus_name(), "A constructed editor bus must carry this player's stable scene/node identity")
	var constructed_properties := quality.get_property_list()
	for effect_property in [&"direct_path_effect", &"hrtf_spatializer_effect"]:
		var matches := constructed_properties.filter(func(property): return property.name == effect_property)
		assert(matches.size() == 1, "Constructed spatial bus must expose %s in the Inspector" % effect_property)
		assert(matches[0].usage & PROPERTY_USAGE_READ_ONLY, "%s must be a read-only live reference" % effect_property)
		assert(quality.get(effect_property) is AudioEffect)
	quality.playing = true
	assert(private_player.playing, "The minimal editor QPlayer playing property must start its private player")
	for attempt in 30:
		if private_player.get_playback_position() > 0.05:
			break
		await create_timer(0.1).timeout
	assert(private_player.get_playback_position() > 0.05, "The minimal editor QPlayer must advance playback")
	# A minimal scene has no AudioListener3D. Its tool preview must therefore use
	# the 3D editor camera, not the otherwise unrelated edited-scene viewport.
	var editor_camera := EditorInterface.get_editor_viewport_3d(0).get_camera_3d()
	assert(editor_camera != null)
	var saved_camera_transform := editor_camera.global_transform
	editor_camera.global_transform = Transform3D(Basis.IDENTITY, quality.global_position - Vector3(5.0, 0.0, 0.0))
	quality._update_spatialization()
	assert(quality.steam_hrtf.source_position == quality.global_position)
	assert(quality.steam_hrtf.receiver_position == editor_camera.global_position)
	editor_camera.global_transform = Transform3D(Basis.IDENTITY, quality.global_position + Vector3(20.0, 0.0, 0.0))
	quality._update_spatialization()
	assert(quality.steam_hrtf.receiver_position == editor_camera.global_position)
	editor_camera.global_transform = saved_camera_transform
	var bus_index := AudioServer.get_bus_index(quality.spatial_effect_bus)
	var effect_peak := -200.0
	for attempt in 30:
		effect_peak = maxf(
				AudioServer.get_bus_peak_volume_left_db(bus_index, 0),
				AudioServer.get_bus_peak_volume_right_db(bus_index, 0))
		if effect_peak > -70.0:
			break
		await create_timer(0.1).timeout
	assert(effect_peak > -70.0, "The minimal editor QPlayer must produce audible effect-bus output")
	quality.stop()
	var owned_bus := quality.spatial_effect_bus
	quality.spatial_effect_bus = AudioStreamQPlayer3D.SPATIAL_BUS_CONSTRUCT_RUNTIME
	assert(AudioServer.get_bus_index(owned_bus) < 0, "Selecting away must remove a spatial bus constructed by this player")
	assert(private_player.bus == quality.bus, "Construct Runtime in the editor must leave the source on its output bus until runtime")
	scene.queue_free()
	await process_frame
	print("minimal editor QPlayer smoke passed")
	quit()
