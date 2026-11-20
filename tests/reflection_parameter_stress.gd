extends SceneTree

const SAMPLE_RATE := 48000
const RUN_SECONDS := 2.0

func _initialize() -> void:
	call_deferred("_run")

func _make_impulse_stream() -> AudioStreamWAV:
	var stream := AudioStreamWAV.new()
	stream.format = AudioStreamWAV.FORMAT_16_BITS
	stream.mix_rate = SAMPLE_RATE
	stream.stereo = false
	var sample_count := int(SAMPLE_RATE * RUN_SECONDS)
	var bytes := PackedByteArray()
	bytes.resize(sample_count * 2)
	# One deliberately modest impulse followed by a nearly-silent deterministic
	# signal. Steam's RT60 parameters are supplied to Apply, not GetTail, so this
	# keeps Apply active while abrupt edits exercise its live filter update path.
	bytes.encode_s16(0, 8192)
	for sample in range(1, sample_count):
		bytes.encode_s16(sample * 2, 2 if sample % 2 == 0 else -2)
	stream.data = bytes
	return stream

func _run() -> void:
	var player := AudioStreamQPlayer3D.new()
	player.stream = _make_impulse_stream()
	player.reflections_enabled = true
	player.reflection_send_db = -12.0
	player.reflection_wet_db = -6.0
	player.reflection_reverb_times = Vector3(5.0, 4.0, 3.0)
	root.add_child(player)
	await process_frame
	assert(player.get_active_backend() == AudioStreamQPlayer3D.SpatialBackend.STEAM_AUDIO)

	for cycle in 3:
		player.play()
		# Exercise deliberately abrupt Dattoverb-like edits while the tail is hot.
		for change in 24:
			player.reflection_reverb_times = (
				Vector3(0.15, 0.12, 0.10) if change % 2 == 0
				else Vector3(5.0, 4.0, 3.0)
			)
			await create_timer(0.025).timeout
		player.stop()
		# Let the bus sleep before the next wake/impulse cycle.
		await create_timer(2.2).timeout

	var reflection_return = player.steam_reflection_hrtf
	assert(reflection_return != null)
	assert(not reflection_return.is_diagnostic_capture_ready(),
		"Rapid RT60 edits or sleep/wake produced a suspicious reflection block")
	player.queue_free()
	await process_frame
	print("reflection parameter/sleep/wake stress passed")
	quit()
