# Cross-platform renderer contract

`AudioStreamQPlayer3D` is the stable Godot-facing class. A scene should normally
leave `spatial_backend` set to `AUTO` and should not inspect Steam Audio classes.

`AUTO` currently selects:

| Runtime | Renderer | HRTF | Reflections / Ambisonics |
| --- | --- | --- | --- |
| A platform with the Steam GDExtension | Steam Audio | Yes | No (deferred) |
| Web or a platform without that binary | Godot `AudioStreamPlayer3D` | No | No |

Call `get_spatial_capabilities()` when UI needs to describe optional features.
The ordinary playback and positioning API does not change with the renderer.

Cross-platform scenes should leave `spatial_effect_bus` on **Construct
Runtime**. Do not save Steam-native effect resources into a bus layout intended
for Web or another platform without the extension: Godot must deserialize that
layout before the GDScript fallback can run, and an unavailable native effect
class makes the layout invalid. The authored buses in this repository belong
to the desktop editor demonstration and are not part of the addon-only release
package.

## Browser HRTF boundary

The desired Web backend is a browser `PannerNode` with
`panningModel = "HRTF"`. It can retain the same `AudioStreamQPlayer3D` UI and
omit the native Direct effect's three-band air absorption.

This cannot be implemented faithfully by evaluating a small JavaScript snippet
from the current addon. Godot's Web Sample backend does create one private
`AudioBufferSourceNode` per sample playback, but its JavaScript playback map is
not exposed to GDExtensions or `JavaScriptBridge`. It currently routes those
sources through engine-managed channel gains and does not create a
`PannerNode`. A final-output `PannerNode` would spatialize the entire game
together, while separately fetching an audio file would stop supporting
arbitrary Godot `AudioStream` resources and live streams.

The clean implementation point is therefore Godot's Web sample/playback
backend: add a per-playback spatial node and pass source/listener transforms to
it. The addon can then select that backend without changing its public scene or
script API. Until that engine path exists, the Web fallback deliberately
reports `hrtf = false` rather than claiming acoustic behavior it does not have.

Relevant upstream references:

- [Godot Web audio implementation](https://github.com/godotengine/godot/blob/master/platform/web/js/libs/library_godot_audio.js)
- [Godot issue #95991: bus effects are bypassed by Web Sample playback](https://github.com/godotengine/godot/issues/95991)
- [Godot audio-stream playback-type documentation](https://github.com/godotengine/godot-docs/blob/master/tutorials/audio/audio_streams.rst)

## Release packaging

The tag-driven workflow produces one addon-only archive. Native Steam Audio
artifacts are included for Linux x86-64, Linux ARM64, Windows x86-64, macOS
universal, and Android ARM64. Web, iOS, and unmatched architectures load the
same GDScript class but select Godot's native player because no matching
GDExtension library is advertised.

Linux x86-64 is compiled on the workflow's portable Ubuntu baseline. Linux
ARM64 is cross-compiled using the older Nixpkgs ABI selected for Steam Frame.
Steam Audio itself and this GDExtension are compiled from their pinned sources
on every native target, so later public API additions are reproducible and do
not depend on opaque SDK binaries. Developer binaries produced by the default
local Nix shell are not release inputs.
