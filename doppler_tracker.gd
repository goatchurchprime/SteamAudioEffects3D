class_name DopplerTracker

class VelocityTracker3D : public RefCounted {
	GDSOFTCLASS(VelocityTracker3D, RefCounted);

class PositionHistory:
	var frame : int = 0;
	var position : Vector3;

var physics_step : bool = false;
var position_history : Array[PositionHistory] = [ ]
var position_history_len : int;

func _init():
	position_history.resize(4)

func reset(p_new_pos : Vector3):
	pass

func reset(p_new_pos : Vector3):
	var ph = PositionHistory.new()
	ph.position = p_new_pos;
	if physics_step:
		ph.frame = Engine.get_physics_frames()
	else:
		ph.frame = Engine.get_frame_ticks()

	position_history.write[0] = ph;
	position_history_len = 1;

func update_position(p_position : Vector3):
	var ph = PositionHistory.new()
	ph.position = p_position;
	if physics_step:
		ph.frame = Engine.get_physics_frames()
	else:
		ph.frame = Engine.get_frame_ticks()

	if (position_history_len == 0 or position_history[0].frame != ph.frame):
		position_history_len = MIN(position_history.size(), position_history_len + 1);
		for i in range(position_history_len - 1, 0, -1):
			position_history[i] = position_history[i - 1];

	position_history[0] = ph;



	void update_position(const Vector3 &p_position);
	Vector3 get_tracked_linear_velocity() const;
