# Offline acoustic reference tools

`bake_reference_energy_fields.cpp` constructs controlled room meshes using the
public Steam Audio API. It asks Steam Audio to ray trace and bake both the
first-order Ambisonic energy field and the three-band parametric RT60 estimate
at one listener probe for a static source.

Run a quick, low-ray-count bake while developing:

```sh
scripts/bake_reference_energy_fields.sh addons/steam_spatial/reference_energy_fields quick
```

Run the slower reference bake with the Steam documentation's offline-quality
ray, diffuse-sample, and bounce counts:

```sh
scripts/bake_reference_energy_fields.sh
```

Each JSON file contains the exact flattened layout accepted by
`AudioEffectSteamReflectionSend.energy_field`: Ambisonic channel, frequency
band, then 10 ms time bin. The files also include the RT60 values Steam derives
from the same simulation. They are diagnostic fixtures, not runtime assets or
an API commitment. The checked-in copies under the addon are used by its
audition presets; rerunning this tool replaces them with newly simulated data.

All checked-in fields remain in the fixed world-space orientation produced by
Steam Audio. Listener orientation is applied later by Steam's Ambisonic HRTF
decoder; the addon does not animate or artistically rotate the field.
