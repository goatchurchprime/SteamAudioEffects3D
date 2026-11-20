# Steam Audio effects and environmental analysis

The addon currently exposes four independent render effects. They consume acoustic
parameters; they do not own a world representation or probe Godot geometry.

## AudioEffectSteamDirect

`AudioEffectSteamDirect` is the endpoint for the direct path from one source to
the listener. Its fields correspond to Steam Audio's `IPLDirectEffectParams`:

| Property | Meaning | Typical producer |
| --- | --- | --- |
| `distance_attenuation` | Broadband gain from distance, `0..1` | Source/listener distance curve |
| `air_absorption` | Low/mid/high band gains, each `0..1` | Distance and atmospheric model |
| `directivity` | Already-evaluated source directivity gain, `0..1` | Source orientation and radiation pattern |
| `occlusion` | Visible direct-path fraction: `0` fully occluded, `1` clear | Godot ray or shape casts |
| `transmission` | Low/mid/high material transmission gains | Intersected acoustic materials |
| `transmission_type` | Broadband or frequency-dependent transmission | Rendering policy |

Each stage has an `*_enabled` property. The effect combines enabled stages in one
Steam Audio direct-effect pass. It does not need Steam's simulator if Godot or
game code supplies these values.

This creates a useful Godot-native architecture:

```text
Godot transforms/physics/materials
        -> direct-path parameter calculation
        -> AudioEffectSteamDirect
        -> AudioEffectSteamSpatial
        -> ordinary Godot bus routing
```

For example, a Godot integration can ray-cast from listener to source, determine
which `CollisionObject3D` materials were crossed, calculate the unobstructed
fraction and three transmission bands, and update the effect. Steam Audio then
renders those results without owning a duplicate scene or physics lifecycle.

Steam's own simulator produces `IPLSimulationOutputs.direct`, which can likewise
be mapped to this interface. This makes the renderer independent of who performs
the analysis.

## AudioEffectSteamSpatial

`AudioEffectSteamSpatial` turns a point source into binaural stereo using Steam
Audio's built-in HRTF:

| Property | Meaning |
| --- | --- |
| `direction` | Listener-local unit vector from listener to source |
| `gain` | Nonnegative pre-HRTF linear gain supplied by this integration |
| `spatial_blend` | Blend from unspatialized mono (`0`) to full HRTF (`1`) |
| `interpolation` | Nearest measurement or smoother, costlier bilinear HRTF lookup |

`set_source_parameters()` updates direction, gain, and blend together. The
effect folds stereo input to mono before rendering; `spatial_blend = 0` therefore
does not recover the original stereo image.

## AudioEffectSteamReflectionSend

`AudioEffectSteamReflectionSend` is a normal pass-through bus effect with one
additional relationship: its `target` is an
`AudioEffectSteamReflectionHRTF` resource. It folds its input to mono, applies
one of two reflection renderers, and submits a first-order Ambisonic block to
that target. Its ordinary Godot output remains the unchanged dry signal.

With an empty `energy_field`, the renderer is Steam Audio's inexpensive
parametric reverb using the three `reverb_times`. A populated field contains
`4 channels * 3 bands * N time bins`; every bin represents 10 ms. The flattened
layout is channel-major, then low/mid/high band, then time. Steam Audio's public
`IPLEnergyField`, `IPLReconstructor`, and `IPLImpulseResponse` APIs reconstruct
the four-channel impulse response when `set_energy_field()` is called. The addon
then prepares fixed-size FFT partitions outside `_process()` and streams the
convolution on the audio thread. No Valve source is modified.

The internal 2048-sample convolution partition is independent of Godot's
callback size. At 48 kHz it adds about 42.7 ms to the reflected path, keeping
multi-second four-channel fields practical in an unoptimized build.

The target is deliberately the effect resource rather than its effect instance.
The receiver resource directly owns four float arrays, one per first-order
Ambisonic channel. A send calls its native `mix_in()` function, which resizes
and clears those arrays when the processing block size changes and then
copy-adds the submitted block. There is no sidecar transport object. Godot can
recreate playback instances without invalidating the resource link, and
multiple sends can target one return resource.

The field producer is intentionally separate. Presets, procedural room models,
baked assets, or a future geometry simulation can all load the same stable
render interface without changing the Godot bus arrangement.

## AudioEffectSteamReflectionHRTF

`AudioEffectSteamReflectionHRTF` belongs after the point-source HRTF. It accepts
the dry stereo signal through the ordinary effect chain, retrieves accumulated
Ambisonic blocks, rotates them using the listener's absolute world orientation,
decodes them with an Ambisonic HRTF decoder, and adds the wet result to the dry
input. The four dynamically sized channel arrays are owned directly by this
effect resource; there is no additional state object, Godot object, or public
Ambisonic bus.

`dry_gain` and `wet_gain` make the return observable as a conventional parallel
effect. Set `dry_gain` to zero to hear only reflections, or `wet_gain` to zero
to verify that the direct chain passes through unchanged.

The QPlayer stack is therefore:

```text
Reflection Send -> Direct -> Point HRTF -> Reflection HRTF Return
       |                                      ^
       +-------- native Ambisonic submit -----+
```

## What these parameters do not cover

The direct parameters are not a universal interface for every Steam Audio
feature. Steam divides simulation results into three paths:

- `direct`: distance, air absorption, directivity, occlusion, and transmission;
- `reflections`: early reflections and reverberant impulse-response data;
- `pathing`: indirect paths, often represented as an Ambisonic sound field.

The Direct effect handles only the first category, and the point HRTF effect
spatializes the resulting direct source. The reflection pair accepts either a
manual parametric approximation or a full directional energy field.
Geometry-derived reflections still require separate machinery to calculate or
retrieve that field, while pathing requires its own path data; the five
direct-path scalars and three RT60 values are not sufficient.

This separation matches Steam Audio's integration guidance: simulation results
are transferred from a game/simulation thread to render effects on the audio
thread, and direct and indirect signals may be separate effects in the host audio
graph. It therefore does not require adopting Steam Audio as a second all-owning
physics server.

## Update and thread contract

Properties may be set from Godot's main or simulation thread. The extension
stores them atomically and the audio processor reads one value set per processing
block. Buffer sizes come from `_process()`'s `frame_count`; if that size changes,
the instance rebuilds its size-dependent Steam Audio processor and buffers. The
initial allocation is silent, while later size changes identify the effect and
old/new frame counts in Godot's verbose output (`--verbose`). The
addon does not currently add cross-block smoothing of every scalar;
Steam Audio retains the internal filter state of its effects, while moving-source
parameter cadence is controlled by `AudioStreamQPlayer3D`.

The Direct and point-HRTF instances return `false` from `_process_silence()`.
Reflection Send returns `true` only while Steam reports a reverb tail, and the
Reflection HRTF Return does so only while a submission or decoder tail remains.
This lets the wet signal finish after its stream stops without keeping idle
source buses permanently active.

## References

- [Steam Audio Direct Effect](https://valvesoftware.github.io/steam-audio/doc/capi/direct-effect.html)
- [Steam Audio Binaural Effect](https://valvesoftware.github.io/steam-audio/doc/capi/binaural-effect.html)
- [Steam Audio Simulation](https://valvesoftware.github.io/steam-audio/doc/capi/simulation.html)
- [Steam Audio Integration](https://valvesoftware.github.io/steam-audio/doc/capi/integration.html)
