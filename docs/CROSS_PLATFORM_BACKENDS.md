# Cross-platform renderer contract

`AudioStreamQPlayer3D` is the stable Godot-facing class. A scene should normally
leave `spatial_backend` set to `AUTO` and should not inspect Steam Audio classes.

`AUTO` currently selects:

| Runtime | Renderer | HRTF | Reflections / Ambisonics |
| --- | --- | --- | --- |
| A platform with the Steam GDExtension | Steam Audio | Yes | Yes |
| Web or a platform without that binary | Godot `AudioStreamPlayer3D` | No | No |

Call `get_spatial_capabilities()` when UI needs to describe optional features.
The ordinary playback and positioning API does not change with the renderer.

## Browser HRTF boundary

The desired Web backend is a browser `PannerNode` with
`panningModel = "HRTF"`. It can retain the same `AudioStreamQPlayer3D` UI and
omit Steam's reflection controls.

This cannot be implemented faithfully by evaluating a small JavaScript snippet
from the current addon. Godot's Web audio driver exposes the engine's mixed
output to Web Audio; it does not expose one browser node for each
`AudioStreamPlayer3D`. A final-output `PannerNode` would spatialize the entire
game together, while separately fetching an audio file would stop supporting
arbitrary Godot `AudioStream` resources and live streams.

The clean implementation point is therefore Godot's Web sample/playback
backend: add a per-playback spatial node and pass source/listener transforms to
it. The addon can then select that backend without changing its public scene or
script API. Until that engine path exists, the Web fallback deliberately
reports `hrtf = false` rather than claiming acoustic behavior it does not have.

## Packaging work still required

The source tree is backend-ready, but distributable native packages still need
release binaries for each supported architecture. Linux release artifacts must
be rebuilt against the repository's portable GLIBC baseline; local Nix-built
libraries are development artifacts and are not suitable for redistribution.
