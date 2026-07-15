This addon replicates the functioning of the hidden 
AudioEffects internal to the AudioServer that are managed by each AudioStreamPlayer3D.

Use the compound AudioPlayerByEffects3D scene instead of an AudioStreamPlayer3D (or put one as a child for comparison).
On startup this creates a new bus with `AudioEffectHighShelfFilter`, `AudioEffectAmplify` and `AudioEffectPanner` 
whose parameters are updated using exactly the same calculations as 
[AudioStreamPlayer3D::_update_panning()](https://github.com/godotengine/godot/blob/master/scene/3d/audio_stream_player_3d.cpp#L382C1-L382C6)

This is a complete implementation of all the features including attenutation filters, emission angles and doppler.
For doppler to respect the motion of the AudioListener3D or Camera3D, you will need to instantiate a
VelocityTracker3D as a child of those nodes to measure its velocity.

To check for errors in the conversion of these complex C++ functions, the measurement 
of the output spectrum from a simple sinewave tone is plotted.

<img width="891" height="493" alt="image" src="https://github.com/user-attachments/assets/ead05add-fa9a-4a41-9eda-9927577cbeef" />
