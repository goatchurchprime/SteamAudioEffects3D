This addon replicates the functioning of the hidden 
AudioEffects internal to the AudioServer that are managed by each AudioStreamPlayer3D.

Use the compound AudioPlayerByEffects3D scene instead of an AudioStreamPlayer3D (or put one as a child for comparison).
On startup this creates a new bus with `AudioEffectHighShelfFilter`, `AudioEffectAmplify` and `AudioEffectPanner` 
whose parameters are updated using exactly the same calculations as 
[AudioStreamPlayer3D::_update_panning()](https://github.com/godotengine/godot/blob/master/scene/3d/audio_stream_player_3d.cpp#L382C1-L382C6)

This is a complete implementation of all the features including attenutation filters, emission angles and doppler.
For doppler to respect the motion of the AudioListener3D or Camera3D, you will need to instantiate a
VelocityTracker3D as a child of those nodes to measure their velocities since the built-in VelocityTracker3Ds [were unexposed](https://github.com/godotengine/godot/pull/64592).

To check for errors in the conversion of these complex C++ functions, the measurement 
of the output spectrum from a simple sinewave tone is plotted.

Limitations:
* Surround sound can't be done since the AudioEffects don't know which pair of channels they are attached to.  
This PR needs to be applied: https://github.com/godotengine/godot/pull/92532
* The input stream is assumed to be mono (but duplicated to both streams).  If the input is stereo we might need to treat it as two sources with a spread, [as described in the UE documentation](https://dev.epicgames.com/documentation/unreal-engine/spatialization-overview-in-unreal-engine#computingpanvaluesformulti-channelsources).
* AudioStreamPlayer2D is not implemented.
* This implementation favours accuracy over performance.  Once we prove total replicability of the current implementation, we can extend and replace it.  

<img width="866" height="489" alt="image" src="https://github.com/user-attachments/assets/c690a633-c47e-49fe-a7b6-0927155ba948" />
