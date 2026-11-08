# SteamAudioEffects3D

This repository is a history-preserving sibling of
[AudioStreamEffects3D](https://github.com/goatchurchprime/AudioStreamEffects3D). It keeps the same
per-player bus interface and reference spatialization experiment, then uses that architecture to make Steam Audio
spatialization available without waiting for corresponding changes to Godot's `AudioServer`.

The reference repository remains focused on reproducing and examining Godot's hidden per-player spatial effects.
Reusable architectural improvements can therefore be developed here and mapped back cleanly without coupling that
experiment to Steam Audio.

## Steam Audio HRTF experiment

The project currently contains a minimal `AudioEffectSteamSpatial` GDExtension. It is deliberately not integrated
into `AudioPlayerByEffects3D`: that node remains the reference experiment which replicates Godot's spatialization.
The Steam effect resource exposes `direction`, `gain`, `spatial_blend`, and HRTF `interpolation`; its required
`AudioEffectSteamSpatialInstance` owns the Steam Audio context, built-in HRTF, binaural effect, and audio buffers.
There is no separate spatial-source processor or adapter layer.

Build and run on NixOS:

```sh
git submodule update --init --recursive
nix develop --command scripts/build_linux.sh
nix develop --command godot4 --headless --path . --import
nix develop --command godot4 --headless --audio-driver Dummy --path . --script res://tests/steam_spatial_smoke.gd
nix develop --command godot4 --editor --path .
```

The import step makes Godot discover the GDExtension and resource UIDs in a fresh checkout before the smoke test.

The native dependencies remain separately owned and pinned: `godot-cpp` is at 10.0.0-rc1 (Godot 4.6), and Valve
Steam Audio is at v4.8.1. `scripts/build_linux.sh` builds Valve's `libphonon.so`, then links the small Godot-specific
GDExtension against it. The same boundary is appropriate for Opus, RNNoise, ONNX Runtime, and similar libraries.

Current deliberate limits: the HRTF path assumes Godot's current 512-frame audio mix block, takes a mono source
(stereo is folded down), and implements direction plus distance gain only. It does not yet add air absorption,
occlusion, reflections, or distance-of-flight delay. These should be added one at a time while retaining the old
stack as an A/B reference.

## Native/addon in-editor comparison

Open `ineditordemo.tscn`, select its `Main` root, and click **Toggle A/B Preview** in the Inspector. The tool script
temporarily excludes the editor's four 3D viewports from native audio-listener selection, makes the scene's
`PreviewListener` current, then starts both implementations with the same stream and position. All viewport states
are restored when preview stops. Drag `SoundSource` or `PreviewListener` and compare `Native3DPreview` (Godot's
`AudioStreamPlayer3D`) with the saved `PlayerEffectBus_Editor` inspection bus. The saved bus exposes the addon's
HighShelf, Amplify, and Panner controls in the Audio dock; the native meter bus exists only while
preview is enabled. The two Source Audition properties mute the corresponding player node at source—no bus mute or
solo state is used. The preview deliberately uses the mono pink-noise fixture: the bus-effect reconstruction assumes
a mono VoIP-style source, whereas arbitrary stereo material is not mathematically equivalent under a stereo panner.

The waveform application shown in the original experiment now lives under `spatialization_verifier/`. Its root
installs `spatialization_verifier/bus_layout.tres` in `_enter_tree()`, before its audio children initialize, so its
analyzer, recording, and delay buses do not pollute the project default layout. Runtime `AudioPlayerByEffects3D`
instances continue to create and remove their own private processing buses.

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
