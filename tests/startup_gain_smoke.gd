extends SceneTree

const AUTHORED_VOLUME_DB := -48.0
const CAPTURE_FRAMES := 4096

func _initialize() -> void:
	call_deferred("_run")

func _make_loud_stream() -> AudioStreamWAV:
	var stream := AudioStreamWAV.new()
	stream.format = AudioStreamWAV.FORMAT_16_BITS
	stream.mix_rate = 8000
	stream.stereo = false
	stream.loop_mode = AudioStreamWAV.LOOP_DISABLED
	var samples := PackedByteArray()
	samples.resize(16000) # One second of mono 16-bit frames.
	for byte_index in range(0, samples.size(), 2):
		samples[byte_index] = 0xff
		samples[byte_index + 1] = 0x7f
	stream.data = samples
	return stream

func _capture_startup(capture: AudioEffectCapture, with_listener: bool) -> float:
	var listener: AudioListener3D
	if with_listener:
		listener = AudioListener3D.new()
		root.add_child(listener)
		listener.make_current()
		await process_frame

	var packed := load("res://addons/steam_spatial/audio_stream_q_player_3d.tscn") as PackedScene
	var player := packed.instantiate() as AudioStreamQPlayer3D
	player.stream = _make_loud_stream()
	player.volume_db = AUTHORED_VOLUME_DB
	player.unit_size = 1.0
	player.position = Vector3(0.0, 0.0, -20.0)
	root.add_child(player)
	await process_frame

	capture.clear_buffer()
	player.play()
	var authored_gain := db_to_linear(AUTHORED_VOLUME_DB)
	assert(is_equal_approx(player.steam_hrtf.gain, authored_gain),
			"Steam HRTF gain must be authored synchronously before playback, got %s" % player.steam_hrtf.gain)
	if with_listener:
		assert(is_equal_approx(player.steam_direct.distance_attenuation, 0.05),
				"Listener-present startup must prime distance attenuation")
	else:
		assert(is_equal_approx(player.steam_direct.distance_attenuation, 1.0),
				"Listener-free startup must use a safe neutral distance while retaining authored gain")

	for attempt in 100:
		if capture.get_frames_available() >= CAPTURE_FRAMES:
			break
		await create_timer(0.01).timeout
	assert(capture.get_frames_available() >= CAPTURE_FRAMES, "Timed out waiting for startup audio capture")
	var frames := capture.get_buffer(CAPTURE_FRAMES)
	var peak := 0.0
	for frame in frames:
		peak = maxf(peak, maxf(absf(frame.x), absf(frame.y)))

	player.stop()
	player.queue_free()
	if listener != null:
		listener.queue_free()
	await process_frame
	await create_timer(0.1).timeout
	return peak

func _run() -> void:
	var capture := AudioEffectCapture.new()
	capture.buffer_length = 2.0
	var capture_index := AudioServer.get_bus_effect_count(0)
	AudioServer.add_bus_effect(0, capture, capture_index)
	await process_frame

	var no_listener_peak := await _capture_startup(capture, false)
	var listener_peak := await _capture_startup(capture, true)
	# -48 dB is roughly 0.004 linear. Leave generous HRTF headroom while making
	# the former near-unity first block (observed around 0.96) unambiguously fail.
	assert(no_listener_peak < 0.05,
			"Listener-free first blocks contain an unprimed gain burst: peak=%s" % no_listener_peak)
	assert(listener_peak < 0.05,
			"Listener-present first blocks contain an unprimed gain burst: peak=%s" % listener_peak)
	assert(no_listener_peak > 0.00001, "Listener-free capture unexpectedly contains no processed signal")
	assert(listener_peak > 0.000001, "Listener-present capture unexpectedly contains no processed signal")

	AudioServer.remove_bus_effect(0, capture_index)
	print("startup gain smoke passed: no_listener=", no_listener_peak, " listener=", listener_peak)
	quit()
