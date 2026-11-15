@tool
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var one_shot := AudioStreamWAV.new()
	one_shot.format = AudioStreamWAV.FORMAT_16_BITS
	one_shot.mix_rate = 8000
	one_shot.stereo = false
	one_shot.loop_mode = AudioStreamWAV.LOOP_DISABLED
	# 800 mono 16-bit frames: a deliberately short 100 ms one-shot. Silence is
	# sufficient here because this test is about playback lifetime, not meters.
	one_shot.data = PackedByteArray()
	one_shot.data.resize(1600)

	var packed := load("res://addons/steam_spatial/audio_stream_q_player_3d.tscn") as PackedScene
	var player := packed.instantiate() as AudioStreamQPlayer3D
	player.stream = one_shot
	root.add_child(player)
	await process_frame
	await physics_frame

	var finished_count := [0]
	player.finished.connect(func(): finished_count[0] += 1)
	player.playing = true
	assert(player.is_playing(), "Editor one-shot must begin playback")

	# Wait several times longer than the stream, then wait again to prove that a
	# late editor physics tick cannot resurrect it after normal finished handling.
	await create_timer(0.45).timeout
	await physics_frame
	assert(not player.is_playing(), "Editor one-shot must stop at natural EOF")
	assert(not player.playing, "The exported playing property must clear at natural EOF")
	assert(finished_count[0] == 1, "Editor one-shot must emit finished exactly once")
	await create_timer(0.35).timeout
	await physics_frame
	assert(not player.is_playing(), "Editor one-shot must not restart after EOF")
	assert(not player.playing, "The playing property must remain false after EOF")
	assert(finished_count[0] == 1, "Editor watchdog must not create repeated finished emissions")

	player.queue_free()
	await process_frame
	print("editor one-shot smoke passed")
	quit()
