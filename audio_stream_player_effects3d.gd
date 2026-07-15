extends Node3D

@onready var ap : AudioStreamPlayer3D = get_parent()
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

var playereffectbus : String
var disable_doppler : bool = false

# Doppler implementation made complicated due to removal of VelocityTracker3D: https://github.com/godotengine/godot/pull/64592

# This could actually be done on a bus
var effecthighshelffilter : AudioEffectHighShelfFilter
var effectamplify : AudioEffectAmplify
var effectpanner : AudioEffectPanner
func _ready():
	if bus == "":
		bus = $AudioStreamPlayer.bus
	playereffectbus = "PlayerEffectBus_%d" % $AudioStreamPlayer.get_instance_id()
	AudioServer.add_bus()
	var bus_idx = AudioServer.bus_count - 1
	assert (AudioServer.get_bus_effect_count(bus_idx) == 0)
	AudioServer.set_bus_name(bus_idx, playereffectbus)
	AudioServer.set_bus_send(bus_idx, bus)
	$AudioStreamPlayer.bus = playereffectbus
	effecthighshelffilter = AudioEffectHighShelfFilter.new()
	effecthighshelffilter.resonance = 1.0
	AudioServer.add_bus_effect(bus_idx, effecthighshelffilter)
	effectamplify = AudioEffectAmplify.new()
	AudioServer.add_bus_effect(bus_idx, effectamplify)
	effectpanner = AudioEffectPanner.new()
	AudioServer.add_bus_effect(bus_idx, effectpanner)

func _get_attenuation_db(p_distance):
	var att = 0
	match attenuation_model:
		AudioStreamPlayer3D.ATTENUATION_INVERSE_DISTANCE:
			att = linear_to_db(1.0 / ((p_distance / unit_size) + CMP_EPSILON));
		AudioStreamPlayer3D.ATTENUATION_INVERSE_SQUARE_DISTANCE:
			var d = (p_distance / ap.unit_size)
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
	var output_volume_vector = Vector2(0,0)

	var global_pos = global_transform.origin

	var audio_listener = get_viewport().get_audio_listener_3d()
	var llistener_node = audio_listener if audio_listener else get_viewport().get_camera_3d()
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

		if ap.emission_angle_enabled:
			var listenertopos = global_pos - listener_node.global_transform.origin
			var c = listenertopos.normalized().dot(global_transform.basis.z.normalized())
			var angle = rad_to_deg(acos(c))
			if angle > emission_angle_degrees:
				db_att -= -emission_angle_filter_attenuation_db

		linear_attenuation = maxf(linear_attenuation, db_to_linear(db_att))

		var listener_volume_vector = _calc_output_vol_stereo(local_pos, cached_global_panning_strength * ap.panning_strength);
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
	effectpanner.pan = (output_volume_vector.y - output_volume_vector.x)/effectamplify.volume_linear/2
	#if linear_attenuation == 0 : bypass
	effecthighshelffilter.gain = linear_attenuation
	effecthighshelffilter.cutoff_hz = attenuation_filter_cutoff_hz

func _physics_process(delta):
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
	
	if doppler_tracking != ap.doppler_tracking:
		doppler_tracking = ap.doppler_tracking
		$VelocityTracker3D._update_dopper_tracking()

	_update_panning()
