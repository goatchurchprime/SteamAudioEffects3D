extends Node3D

# Needs to be a child of Camera3D, AudioListener3D, or AudioStreamEffect3D

class PositionHistory:
	var frame : int = 0;
	var position : Vector3;

var physics_step : bool = false;
var position_history : Array[PositionHistory] = [ ]
var position_history_len : int;
var doppler_tracking : AudioStreamPlayer3D.DopplerTracking = AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED

func _init():
	position_history.resize(4)

func _ready():
	print(get_parent().get_name())
	_update_dopper_tracking()

func _update_dopper_tracking():
	doppler_tracking = get_parent().doppler_tracking
	if doppler_tracking != AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED:
		reset(global_transform.origin);
		physics_step = (doppler_tracking == AudioStreamPlayer3D.DOPPLER_TRACKING_PHYSICS_STEP)
		set_notify_transform(true)
	else:
		set_notify_transform(false)


func _notification(p_what : int):
	if p_what == NOTIFICATION_TRANSFORM_CHANGED:
		if doppler_tracking != AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED:
			update_position(global_transform.origin)

func reset(p_new_pos : Vector3):
	var ph = PositionHistory.new()
	ph.position = p_new_pos;
	if physics_step:
		ph.frame = Engine.get_physics_frames()
	else:
		ph.frame = Time.get_ticks_usec()

	position_history[0] = ph;
	position_history_len = 1;

func update_position(p_position : Vector3):
	var ph = PositionHistory.new()
	ph.position = p_position;
	if physics_step:
		ph.frame = Engine.get_physics_frames()
	else: 
		ph.frame = Time.get_ticks_usec()

	if (position_history_len == 0 or position_history[0].frame != ph.frame):
		position_history_len = min(position_history.size(), position_history_len + 1);
		for i in range(position_history_len - 1, 0, -1):
			position_history[i] = position_history[i - 1];

	position_history[0] = ph;

func get_user_physics_ticks_per_second():
	return max(1.0, Engine.time_scale * Engine.physics_ticks_per_second)

func get_tracked_linear_velocity() -> Vector3:
	var linear_velocity : Vector3
	var max_time : float = 1 / 5.0; # maximum time to interpolate a velocity

	var distance_accum : Vector3 = Vector3.ZERO;
	var time_accum : float  = 0.0;
	var base_time : float = 0.0;

	if position_history_len:
		if physics_step:
			var base : = Engine.get_physics_frames();
			base_time = float(base - position_history[0].frame) / get_user_physics_ticks_per_second();
		else:
			var base : = Time.get_ticks_usec()
			base_time = float(base - position_history[0].frame) / 1000000.0;

	for i in range(position_history_len - 1):
		var delta : float = 0.0;
		var diff : int = position_history[i].frame - position_history[i + 1].frame;
		var distance : Vector3 = position_history[i].position - position_history[i + 1].position;


		if physics_step:
			delta = float(diff) / get_user_physics_ticks_per_second();
		else:
			delta = float(diff) / 1000000.0;

		if base_time + time_accum + delta > max_time:
			break;

		distance_accum += distance;
		time_accum += delta;

	if time_accum:
		linear_velocity = distance_accum / time_accum;

	return linear_velocity;
