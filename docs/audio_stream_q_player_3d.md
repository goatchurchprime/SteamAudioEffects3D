# AudioStreamQPlayer3D

`AudioStreamQPlayer3D` is a higher-quality, HRTF-based alternative to Godot's
`AudioStreamPlayer3D`. It deliberately keeps the familiar player-shaped API so
ordinary scene and gameplay code needs as little change as possible.

It is not a subclass of `AudioStreamPlayer3D`. It is a `Node3D` scene containing
an `AudioStreamPlayer`, a velocity tracker, and a private processing bus. Code
typed specifically as `AudioStreamPlayer3D`, engine APIs that require that exact
class, and `is AudioStreamPlayer3D` checks must be changed.

## Compatibility

| Surface | Status | Difference from `AudioStreamPlayer3D` |
| --- | --- | --- |
| `stream`, `autoplay`, `playing`, `stream_paused` | Compatible | Forwarded to the internal stream player. |
| `play()`, `seek()`, `stop()` and playback queries | Compatible | Same public signatures. |
| `finished` | Compatible | Forwarded from the internal stream player. |
| `volume_db`, `volume_linear`, `pitch_scale` | Compatible | Volume is applied before HRTF rendering. |
| `max_polyphony` | Compatible | All voices share the node's position and one effect chain. |
| `bus` | Functionally compatible | Selects the destination of the private processing bus, not the internal player's immediate source bus. |
| `playback_type` | Name-compatible only | Always forced to Stream because Sample playback bypasses Godot bus effects. |
| `attenuation_model`, `unit_size` | Mostly compatible | Evaluated by the addon and supplied to Steam Direct. The current logarithmic curve needs closer parity testing. |
| `max_distance` | Audible behavior compatible | Fades to silence, but currently does not stop source mixing to save CPU. |
| `panning_strength` | Functionally compatible | Controls HRTF spatial blend rather than Godot's speaker panner; it is intentionally not acoustically identical. |
| emission-angle properties | Functionally compatible | Applied as broadband directivity gain rather than Godot's attenuation filter. |
| `doppler_tracking` | Partially compatible | Disabled versus enabled is respected, but Idle and Physics modes currently both update on physics ticks. Listener motion needs the included `VelocityTracker3D`. |
| attenuation-filter properties | Stored compatibility placeholders | Steam's three-band physical air absorption replaces Godot's single cutoff/attenuation filter. These two values currently do not change DSP. |
| `area_mask` | Stored compatibility placeholder | `Area3D` bus redirection and area reverb are not implemented yet. |

## Diffuse reflection presets

`reflection_preset` and `apply_reflection_preset()` provide conservative
audition starting points: Dry, Dead Booth, Furnished Room, Office, Large Hall,
Stone Tunnel, and Exaggerated Cavern. Each preset sets
`reflections_enabled`, `reflection_send_db`, the low/mid/high
`reflection_reverb_times`, and `reflection_wet_db` together.

These are artistic RT60 presets for the current order-0 parametric return, not
geometry measurements. They cannot place an echo at a wall direction. Keep the
player on Dry when comparing direct-source localization, then select a room to
judge how a diffuse tail affects speech or transient localization.

The current renderer also differs in these important ways:

- Output is stereo binaural audio for headphones. It is not a 5.1/7.1 speaker
  renderer.
- Stereo input is folded to one mono point source. A spatial stereo source would
  need two source positions or an explicit spread model.
- The native effects size their Steam Audio processors and buffers from Godot's
  current processing block and rebuild them if its frame count changes.
- Distance-of-flight delay, geometry occlusion, material transmission,
  convolution reflections, and pathing are not automatically calculated by the
  player. The initial reflection path is a manually parameterized diffuse
  parametric reverb and is disabled by default.
- The listener and source parameters are updated once per physics tick. Effect
  parameters are sampled atomically by the audio thread once per audio block.

## Substitution strategy

For an existing scene, replace an `AudioStreamPlayer3D` node with an instance of
`audio_stream_q_player_3d.tscn`, retain its node name, and copy the compatible
properties. Existing `$Path/To/Player` lookups and duck-typed calls can remain
unchanged. Replace strict annotations such as:

```gdscript
@onready var voice: AudioStreamPlayer3D = $Voice
```

with `AudioStreamQPlayer3D`, `Node3D`, or an untyped reference as appropriate.
The two classes cannot be transparently exchanged where a method argument,
exported property, or engine subsystem explicitly requires `AudioStreamPlayer3D`.

Avoid creating and destroying large numbers of these nodes every frame. Each
runtime node creates a private Godot bus and four native effect processors. For
long-lived world sounds and VoIP talkers this makes ownership and parameter
isolation simple; highly transient one-shot sounds will eventually benefit from
pooling.

## Performance work still available

- Cache listener transforms and velocities once per update rather than once per
  source.
- Skip parameter writes for stationary sources and unchanged listeners.
- Pool or share immutable Steam Audio HRTF data while retaining per-source
  binaural filter state.
- Cull processing outside `max_distance` instead of merely supplying zero gain.
- Avoid repeated bus creation for pooled or short-lived sources.
- Measure nearest versus bilinear HRTF interpolation on the target VR hardware.

## References

- [Godot `AudioStreamPlayer3D` class reference](https://docs.godotengine.org/en/4.7/classes/class_audiostreamplayer3d.html)
- [Godot GDExtension documentation system](https://docs.godotengine.org/en/4.7/tutorials/scripting/cpp/gdextension_docs_system.html)
