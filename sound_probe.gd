extends Node2D

var streamplayer = null
var sai1 : AudioEffectSpectrumAnalyzerInstance
var windowseconds = 12
@onready var width = $ColorRect.size.x
@onready var height = $ColorRect.size.y

func setstreamplayer(lstreamplayer):
	streamplayer = lstreamplayer
	streamplayer.play()
	$Label.text = streamplayer.get_name()
	var bi = AudioServer.get_bus_index(streamplayer.bus)
	sai1 = AudioServer.get_bus_effect_instance(bi, 0)

var t = 0

func _physics_process(delta):
	t += delta
	if t > windowseconds:
		t -= windowseconds
		$Line2D_left.clear_points()
		$Line2D_right.clear_points()

	if sai1:
		var m = sai1.get_magnitude_for_frequency_range(0, 50000)
		var x = width*t/windowseconds
		$Line2D_left.add_point(Vector2(x, height*(1-m.x)))
		$Line2D_right.add_point(Vector2(x, height*(1-m.y)))
