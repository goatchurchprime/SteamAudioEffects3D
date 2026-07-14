extends Node3D

var t = 0.0
func _physics_process(delta):
	t += delta
	var slowermotion = $Control/HBox/SlowerMotion.button_pressed
	if $Control/HBox/SpinSource.button_pressed:
		$AudioNode/AudioPosition.rotation_degrees.y = t*(20 if slowermotion else 90)
	if $Control/HBox/RotateSource.button_pressed:
		$AudioNode.rotation_degrees.y = t*(10 if slowermotion else 60)
	if $Control/HBox/SwingSource.button_pressed:
		$AudioNode/AudioPosition.position.x = 4*sin(deg_to_rad(t*60)) + 5
	$AudioNode/AudioPosition/AudioStreamPlayer3D.attenuation_filter_db = -60 if $Control/HBox/AttenuationSource.button_pressed else 0

func _on_play_button_pressed():
	$Display/SoundProbe3D.setstreamplayer($AudioNode/AudioPosition/AudioStreamPlayer3D)
	$Display/SoundProbeEffect.setstreamplayer($AudioNode/AudioPosition/AudioStreamPlayer3D/AudioStreamEffect3D/AudioStreamPlayer)


var s = 0
func _on_polyphony_test_pressed():
	var d : AudioEffectDelay = AudioServer.get_bus_effect(AudioServer.get_bus_index($AudioStreamPlayer.bus), 0)
	$AudioStreamPlayer.play()
	s += 1
	d.tap1_delay_ms = 5 if ((s%4) < 2) else 0
	d.tap2_delay_ms = 5 if (((s+1)%4) < 2) else 0
	prints(d.tap1_delay_ms, d.tap2_delay_ms)
