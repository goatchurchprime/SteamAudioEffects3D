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


# This could actually be done on a bus
var effecthighshelffilter : AudioEffectHighShelfFilter
var effectamplify : AudioEffectAmplify
var effectpanner : AudioEffectPanner
func _ready():
	effecthighshelffilter = $AudioStreamPlayer.stream.get_effect(0)
	effectamplify = $AudioStreamPlayer.stream.get_effect(1)
	effectpanner = $AudioStreamPlayer.stream.get_effect(2)
	effecthighshelffilter.resonance = 1.0

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

	_update_panning()
