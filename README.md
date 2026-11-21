# SteamAudioEffects3D

`AudioStreamQPlayer3D` now has one cross-platform scene and scripting API. In
`AUTO` mode it uses Steam Audio when the native effect classes are present and
falls back to Godot's platform-native `AudioStreamPlayer3D` elsewhere. See
[`docs/CROSS_PLATFORM_BACKENDS.md`](docs/CROSS_PLATFORM_BACKENDS.md) for the
capability contract and the remaining Web HRTF integration boundary.

SteamAudioEffects3D is the Steam Audio implementation companion to
[AudioStreamEffects3D](https://github.com/goatchurchprime/AudioStreamEffects3D). The reference repository demonstrates
the per-player effect-bus architecture without native dependencies; this repository uses the same player-shaped API
to provide Steam Audio HRTF spatialization now.

The addons are alternatives, not dependencies and not intended to be installed together. This repository packages
the complete Godot integration under `addons/steam_spatial/`: the `AudioStreamQPlayer3D` scene and script, velocity
helper, GDExtension descriptor, extension library, and Steam Audio runtime library. The public node name describes
what it offers—a higher-quality spatial audio player—without exposing its implementation choice to game code.

## Install a release

Download `steam-audio-effects-3d-vX.Y.Z.zip` from the GitHub release and
extract it at the root of a Godot project. This installs
`addons/steam_spatial/`. Do not use GitHub's automatic source archive as an
addon dependency: generated native libraries are attached to the release, not
committed to the repository.

Leave `spatial_backend` on **Auto** and `spatial_effect_bus` on **Construct
Runtime** for a portable scene. The same scene then uses Steam Audio where its
native binary is present and transparently uses a normal
`AudioStreamPlayer3D` elsewhere.

| Target | Backend |
| --- | --- |
| Linux x86-64 | Steam Audio |
| Linux ARM64 / Steam Frame | Steam Audio |
| Windows x86-64 | Steam Audio |
| macOS x86-64 and Apple Silicon | Steam Audio (universal binary) |
| Android ARM64 | Steam Audio |
| Web, iOS, and other architectures | Godot native `AudioStreamPlayer3D` fallback |

Godot 4.6 is the GDExtension compatibility minimum. Release builds are tested
with Godot 4.7, and the 4.x GDExtension compatibility contract allows the same
binary interface to load in later compatible 4.x releases.

## Player and effect paths

`AudioStreamQPlayer3D` is intended as the practical replacement for `AudioStreamPlayer3D`. It exposes `stream`,
`autoplay`, `playing`, `stream_paused`, `max_polyphony`, `playback_type`, routing, volume and spatial properties,
`play()`, `seek()`, `stop()`, playback queries, and the `finished` signal. It remains a `Node3D`, so strict
`AudioStreamPlayer3D` type annotations are not interchangeable.

Each player uses a spatial effect bus with two visible effects:

1. Steam Audio Direct Path: distance gain, physical three-band air absorption, and source directivity.
2. Steam Audio HRTF Spatializer: point-source binaural rendering derived from global source/receiver transforms.

The effects deliberately remain separate because the native library models direct-path gain/filtering before binaural
rendering. The player updates the resources together, and the complete mechanism remains visible in Godot's Audio
dock. The legacy Godot high-shelf, gain, and panner effects are not duplicated here; their exact bus-based recreation
belongs to the dependency-free reference repository.

`AudioEffectSteamDirect` exposes every `IPLDirectEffectParams` path: distance attenuation, three-band air absorption,
directivity, occlusion, transmission, and transmission mode. It can calculate Steam's distance attenuation and air
absorption from source distance, or accept manual coefficients. `AudioStreamQPlayer3D` v1 deliberately drives only
distance, minimum distance, distance enable/disable, source-cone directivity, gain, global source/receiver transforms, and HRTF blend. The
remaining controls are ready on the effect for later mappings after their player-level interface has been designed.

Detailed references:

- [AudioStreamQPlayer3D compatibility and substitution](docs/audio_stream_q_player_3d.md)
- [Steam effect parameters and environmental-analysis boundary](docs/steam_effects.md)
- [Deferred propagation-delay, spatial-history, and crater VR design](docs/PROPAGATION_DELAY_DESIGN.md)

## Runtime localization audition

Run `listening_test.tscn` with headphones for an exclusive A/B comparison at
identical source transforms. It provides QPlayer/native backend switching,
voice-like, footstep, pink-noise, siren and tone signals, random hidden source
positions, and a revealable pointing-error score.
The synthetic voice-like and footstep signals are deliberately redistribution
safe; assign real recordings to the scene's exported `voice_stream` and
`footstep_stream` fields for production listening.

Keyboard controls are shown in the scene: **Space** plays, **B** switches the
backend, **S** changes signal, **R** starts a random hidden-source trial, and
**Shift+R** randomizes listener yaw without moving the source.
The arrow keys turn the listener/change source elevation, and **H** reveals and
scores.

For VR, retain the same source rig and let the XR viewport's active listener
drive both players. Set `pointing_node_path` to an `XRController3D` to score its
`-Z` pointing direction, or leave it empty to score head direction. This scene
does not initialize a project-specific OpenXR rig; it is intended to be copied
under the game's existing `XROrigin3D`.

## Build and run on NixOS

```sh
git submodule update --init --recursive
nix develop --command scripts/build_linux.sh
nix develop --command godot4 --headless --path . --import
nix develop --command godot4 --headless --audio-driver Dummy --path . --script res://tests/steam_spatial_smoke.gd
nix develop --command godot4 --headless --audio-driver Dummy --path . --script res://tests/audio_player_compatibility_smoke.gd
nix develop --command godot4 --headless --audio-driver Dummy --path . --script res://tests/listening_test_smoke.gd
nix develop --command godot4 --editor --path .
```

The native dependencies are separately owned and pinned: `godot-cpp` uses the Godot 4.6 API bindings for backward
API compatibility, Steam Audio is at v4.8.1, and the build host is Godot 4.7.2. The host and extension use one pinned
Nix environment and `use_static_cpp=false`, avoiding mixed process-wide C++ runtimes; the extension dynamically loads
the colocated `libphonon.so` through `$ORIGIN`.

## Release automation

`.github/workflows/build-release.yml` builds both Godot debug and release
libraries for every native target above, assembles one addon-only archive, and
publishes it when a `v*` tag is pushed. It can also be run manually without
creating a release, which is the validation path for changes to compilers or
the platform matrix. Steam Audio v4.8.1 and the GDExtension are built from the
pinned source on every native target. This keeps the native API under our
control for the planned energy-field/impulse-response bridge. Web and iOS do
not load this C++ extension and therefore exercise the deliberate Godot-native
fallback.

## In-editor comparison

Open `ineditordemo.tscn`, select its `Main` root, and click **Toggle A/B Preview**. The scene forces its
`PreviewListener` for both implementations and compares native `AudioStreamPlayer3D` output with the saved
QPlayer bus. Inspect the two spatial effects and L/R meters in the Audio dock while moving the source
or listener. This same two-player arrangement is the recommended basis for a VR A/B test: give both players the same
stream and transform, and play only the selected implementation. Runtime replacement of existing scene nodes is
intentionally avoided because it would invalidate typed references and can change signal and ownership semantics.

For an ordinary player, `bus` selects the chain's output destination and
`spatial_effect_bus` selects how the intermediate chain is supplied. Choose
**Construct Runtime** for an automatically owned temporary bus, **Construct
Now** to create and assign an inspectable bus in the current editor layout, or
select an already-authored bus. Attached chains also appear as two expandable,
read-only live effect resources in the player Inspector.

The Steam effects size their processing buffers from the `frame_count` supplied by Godot and rebuild their Steam
Audio processors if that block size changes. The HRTF path folds stereo input to one mono point source. It covers
global source/receiver position and receiver orientation, Steam inverse-distance attenuation, physical air absorption, scalar source
directivity, and Doppler pitch. The compatibility properties `attenuation_filter_db` and
`attenuation_filter_cutoff_hz` remain available to ease scene migration, but Steam's physical three-band air model
replaces their DSP semantics. The earlier reflection experiment remains in source history but is excluded from this
direct-only release build and public player API. Automatic geometry/probe selection, propagation delay,
multi-source stereo spread, and surround output remain future work.

## Platform backends

The portable contract should be functional rather than sample-identical: stream and playback controls plus source,
listener, distance, directionality, and Doppler parameters go in; the best local stereo spatialization comes out.
Steam Audio is the native backend implemented here. Godot's web Sample playback already delegates individual sounds
to the Web Audio API, so a future web backend can map the same contract to `PannerNode` HRTF, distance, and cone
properties. A plain Godot backend can remain the final fallback. This should be capability selection inside the
player implementation, not runtime replacement of scene nodes.

Web Sample playback does not run Godot bus effects, so the current Steam effect stack is not itself the web backend.
Godot 4.6 creates an individual `AudioBufferSourceNode` for each sample, then applies engine-computed channel gains in
its JavaScript `SampleNodeBus`; it does not currently insert a `PannerNode` or pass source/listener transforms across
that boundary. The architecture spike should add an optional spatial-parameter call there, insert an HRTF panner, and
run the same VR comparison scene before the backend interface is frozen. It should document the reduced feature set:
browser-dependent HRTF, no physical Steam air model or propagation, and no Godot bus effects, Doppler, or reverb in
Sample playback. Live VoIP is a separate web case because procedural audio cannot use Sample playback; it will need a
stream or `AudioWorklet` source routed through the same browser panner contract.
