@tool
extends Node3D

const CMP_EPSILON = 0.00001
var cached_global_panning_strength = ProjectSettings.get_setting("audio/general/3d_panning_strength")

# parameters for emulating AudioStreamPlayer3D
@export var max_distance : float = 0.0
@export var volume_db : float = 0.0
@export var max_db : float = 3.0
@export var unit_size : float = 10.0
@export var attenuation_model : AudioStreamPlayer3D.AttenuationModel = AudioStreamPlayer3D.ATTENUATION_INVERSE_DISTANCE
@export var attenuation_filter_db : float = -24.0
@export var attenuation_filter_cutoff_hz : float = 5000
@export var emission_angle_enabled : bool = false
@export var emission_angle_degrees = 45.0
@export var emission_angle_filter_attenuation_db = -12.0
@export var pitch_scale : float = 1.0
@export var bus : String = ""
@export var doppler_tracking : AudioStreamPlayer3D.DopplerTracking = AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED
@export_group("Bus Debugging")
@export var existing_effect_bus: StringName = &""

var playereffectbus : String
var disable_doppler : bool = false

var ap : AudioStreamPlayer3D = null

# This could actually be done on a bus
var effecthighshelffilter : AudioEffectHighShelfFilter
var effectamplify : AudioEffectAmplify
var effectpanner : AudioEffectPanner
var effect_bus_index := -1
func _ready():
	if get_parent().is_class("AudioStreamPlayer3D"):
		ap = get_parent()
		# bus = ap.bus # Don't override unless upper AudioStreamPlayer is muted
		$AudioStreamPlayer.stream = ap.stream
		$AudioStreamPlayer.max_polyphony = ap.max_polyphony
		$AudioStreamPlayer.playing = ap.playing

	if Engine.is_editor_hint():
		# The editor preview controller creates its temporary buses explicitly.
		# Merely opening a scene must not mutate the editor's AudioBusLayout.
		return
	if bus == "":
		bus = $AudioStreamPlayer.bus
	if existing_effect_bus != &"":
		bus = String(existing_effect_bus)
		_attach_existing_effect_bus()
		return
	_create_runtime_effect_bus()

func begin_editor_preview(preview_bus: StringName) -> void:
	if not Engine.is_editor_hint():
		return
	bus = String(existing_effect_bus if existing_effect_bus != &"" else preview_bus)
	_attach_existing_effect_bus()

func end_editor_preview() -> void:
	if not Engine.is_editor_hint():
		return
	$AudioStreamPlayer.stop()
	$AudioStreamPlayer.bus = &"Master"
	effect_bus_index = -1
	effecthighshelffilter = null
	effectamplify = null
	effectpanner = null

func _attach_existing_effect_bus():
	# AudioServer bus mutations made by @tool scripts belong to the editor's live
	# AudioBusLayout and can be saved into default_bus_layout.tres. For previewing,
	# reuse the deliberately configured output bus instead of creating local buses.
	effect_bus_index = AudioServer.get_bus_index(bus)
	if effect_bus_index < 0:
		push_warning("Existing effect bus '%s' does not exist" % bus)
		return
	$AudioStreamPlayer.bus = bus
	for effect_index in AudioServer.get_bus_effect_count(effect_bus_index):
		var candidate := AudioServer.get_bus_effect(effect_bus_index, effect_index)
		if candidate is AudioEffectHighShelfFilter:
			effecthighshelffilter = candidate
		elif candidate is AudioEffectAmplify:
			effectamplify = candidate
		elif candidate is AudioEffectPanner:
			effectpanner = candidate
	if effecthighshelffilter == null or effectamplify == null or effectpanner == null:
		push_warning("Existing effect bus '%s' needs HighShelf, Amplify, and Panner effects" % bus)

func _create_runtime_effect_bus():
	playereffectbus = "PlayerEffectBus_%d" % $AudioStreamPlayer.get_instance_id()
	effect_bus_index = AudioServer.get_bus_index(playereffectbus)
	if effect_bus_index < 0:
		AudioServer.add_bus()
		effect_bus_index = AudioServer.bus_count - 1
	else:
		while AudioServer.get_bus_effect_count(effect_bus_index) > 0:
			AudioServer.remove_bus_effect(effect_bus_index, 0)
	assert (AudioServer.get_bus_effect_count(effect_bus_index) == 0)
	AudioServer.set_bus_name(effect_bus_index, playereffectbus)
	AudioServer.set_bus_send(effect_bus_index, bus)
	print("Made bus ", playereffectbus, " sending to ", bus)
	$AudioStreamPlayer.bus = playereffectbus
	effecthighshelffilter = AudioEffectHighShelfFilter.new()
	effecthighshelffilter.resonance = 1.0
	AudioServer.add_bus_effect(effect_bus_index, effecthighshelffilter)
	effectamplify = AudioEffectAmplify.new()
	AudioServer.add_bus_effect(effect_bus_index, effectamplify)
	effectpanner = AudioEffectPanner.new()
	AudioServer.add_bus_effect(effect_bus_index, effectpanner)

func _exit_tree():
	if Engine.is_editor_hint():
		return
	var current_index := AudioServer.get_bus_index(playereffectbus)
	if current_index >= 0:
		AudioServer.remove_bus(current_index)

func _get_attenuation_db(p_distance):
	var att = 0
	match attenuation_model:
		AudioStreamPlayer3D.ATTENUATION_INVERSE_DISTANCE:
			att = linear_to_db(1.0 / ((p_distance / unit_size) + CMP_EPSILON));
		AudioStreamPlayer3D.ATTENUATION_INVERSE_SQUARE_DISTANCE:
			var d = (p_distance / unit_size)
			d *= d
			att = linear_to_db(1.0 / (d + CMP_EPSILON));
		AudioStreamPlayer3D.ATTENUATION_LOGARITHMIC:
			att = -20 * log(p_distance / unit_size + CMP_EPSILON);
		AudioStreamPlayer3D.ATTENUATION_DISABLED:
			pass;

	att += volume_db;
	if att > max_db:
		att = max_db;

	return att;

func _calc_output_vol_stereo(source_dir : Vector3, panning_strength : float):
	var flatrad = sqrt(source_dir.x * source_dir.x + source_dir.z * source_dir.z);
	var g = clampf((1.0 - panning_strength) * (1.0 - panning_strength), 0.0, 1.0);
	var f = (1.0 - g) / (1.0 + g);
	var cosx = clampf(source_dir.x / (1.0 if flatrad == 0.0 else flatrad), -1.0, 1.0);
	var fcosx = cosx * f;
	return Vector2(sqrt((-fcosx + 1.0) / 2.0), sqrt((fcosx + 1.0) / 2.0));

func _apply_max_volume_from_vector(output_volume_vector : Vector2, listener_volume_vector : Vector2) -> Vector2:
	return Vector2(maxf(output_volume_vector.x, listener_volume_vector.x), maxf(output_volume_vector.y, listener_volume_vector.y))

# Implement AudioStreamPlayer3D::_update_panning()
# https://github.com/godotengine/godot/blob/master/scene/3d/audio_stream_player_3d.cpp#L382
func _update_panning():
	if effecthighshelffilter == null or effectamplify == null or effectpanner == null:
		return
	var output_volume_vector = Vector2(0,0)

	var global_pos = global_transform.origin

	var audio_listener = get_viewport().get_audio_listener_3d()
	var llistener_node = audio_listener if audio_listener else get_viewport().get_camera_3d()
	if llistener_node == null:
		return

	var linear_attenuation = 0.0
	
	var actual_pitch_scale = pitch_scale;
	var linear_velocity : Vector3 = Vector3.ZERO
	if doppler_tracking != AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED:
		linear_velocity = $VelocityTracker3D.get_tracked_linear_velocity()

	var pitch_scale_sum : float = 0.0
	var pitch_scale_weight : float = 0.0

	for listener_node in [ llistener_node ]:
		var local_pos = listener_node.global_transform.orthonormalized().affine_inverse() * global_pos
		var dist = local_pos.length()
		if max_distance > 0 and dist > max_distance:
			continue
		
		var multiplier = db_to_linear(_get_attenuation_db(dist));
		if (max_distance > 0):
			multiplier *= maxf(0, 1.0 - (dist / max_distance));
		var db_att = (1.0 - minf(1.0, multiplier)) * attenuation_filter_db;

		if emission_angle_enabled:
			var listenertopos = global_pos - listener_node.global_transform.origin
			var c = listenertopos.normalized().dot(global_transform.basis.z.normalized())
			var angle = rad_to_deg(acos(c))
			if angle > emission_angle_degrees:
				db_att -= -emission_angle_filter_attenuation_db

		linear_attenuation = maxf(linear_attenuation, db_to_linear(db_att))

		var source_panning_strength := ap.panning_strength if ap != null else 1.0
		var listener_volume_vector = _calc_output_vol_stereo(local_pos, cached_global_panning_strength * source_panning_strength);
		listener_volume_vector *= multiplier
		output_volume_vector = _apply_max_volume_from_vector(output_volume_vector, listener_volume_vector);

		if doppler_tracking != AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED:
			var listener_velocity : Vector3 = Vector3.ZERO
			var listener_velocity_tracker = listener_node.get_node_or_null("VelocityTracker3D")
			if listener_velocity_tracker != null:
				if listener_velocity_tracker.doppler_tracking != listener_node.doppler_tracking:
					listener_velocity_tracker._update_dopper_tracking()
				listener_velocity = listener_velocity_tracker.get_tracked_linear_velocity()

			var local_velocity : Vector3 = listener_node.global_transform.orthonormalized().basis.inverse() * (linear_velocity - listener_velocity)
			if local_velocity != Vector3.ZERO:
				var approaching : float = local_pos.normalized().dot(local_velocity.normalized());
				var velocity : float = local_velocity.length();
				const speed_of_sound = 343.0;
				var doppler_pitch_scale : float  = pitch_scale * speed_of_sound / (speed_of_sound + velocity * approaching);
				# limit the pitch scale, so we do not get execeedingly extreme pitch effects in edge cases
				doppler_pitch_scale = clampf(doppler_pitch_scale, (1.0 / 8.0), 8.0);

				# just use the maximum volume of the current volume vector as weight
				# so the pitch effect fades out with lower volumes
				var weight : float  = maxf(listener_volume_vector.x, listener_volume_vector.y)
				pitch_scale_sum += weight * doppler_pitch_scale
				pitch_scale_weight += weight

	if pitch_scale_weight > 0.0 and not disable_doppler:
		actual_pitch_scale = pitch_scale_sum / pitch_scale_weight
	$AudioStreamPlayer.pitch_scale = actual_pitch_scale

	# solve: output_volume_vector == (vol*(1-pan), vol*(1+pan))
	effectamplify.volume_linear = (output_volume_vector.x + output_volume_vector.y)/2
	effectpanner.pan = (output_volume_vector.y - output_volume_vector.x)/effectamplify.volume_linear/2 if effectamplify.volume_linear > CMP_EPSILON else 0.0
	#if linear_attenuation == 0 : bypass
	effecthighshelffilter.gain = linear_attenuation
	effecthighshelffilter.cutoff_hz = attenuation_filter_cutoff_hz

func _physics_process(delta):
	# Several edited scenes can be open together. Only the player explicitly
	# started by the preview controller may drive the shared editor scratch bus.
	if Engine.is_editor_hint() and not $AudioStreamPlayer.playing:
		return
	if ap != null:
		volume_db = ap.volume_db
		max_db = ap.max_db
		max_distance = ap.max_distance
		unit_size = ap.unit_size
		attenuation_model = ap.attenuation_model
		attenuation_filter_db = ap.attenuation_filter_db
		attenuation_filter_cutoff_hz = ap.attenuation_filter_cutoff_hz
		emission_angle_enabled = ap.emission_angle_enabled
		emission_angle_degrees = ap.emission_angle_degrees
		emission_angle_filter_attenuation_db = ap.emission_angle_filter_attenuation_db
		pitch_scale = ap.pitch_scale

		#$AudioStreamPlayer.playing = ap.playing

		if doppler_tracking != ap.doppler_tracking:
			doppler_tracking = ap.doppler_tracking

	# VelocityTracker3D is intentionally still a runtime-only helper. The editor
	# preview demonstrates stationary panning and attenuation, not Doppler.
	if not Engine.is_editor_hint() and $VelocityTracker3D.doppler_tracking != doppler_tracking:
		$VelocityTracker3D._update_dopper_tracking()

	_update_panning()
