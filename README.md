This project is designed to replicate the functioning of the hidden 
AudioEffects internal to the AudioServer that are set in a physics_process of the corresponding AudioStreamPlayer3D.

Currently it depends on the AudioStreamWithEffects feature in this PR:
  https://github.com/godotengine/godot/pull/107523

but I could strip out this dependency and make it put its effects on an AudioBus instead.

To check for errors in the conversion of these complex C++ functions, the measurement 
of the output spectrum from a simple sinewave tone is plotted.

