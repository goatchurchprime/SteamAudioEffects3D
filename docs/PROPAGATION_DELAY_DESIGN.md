# Propagation delay, spatial history, and crater VR demo

Status: design record; not implemented. Last reviewed 2026-11-20.

This document records a deferred extension to `AudioStreamQPlayer3D`. Its motivating test is a VR crater scene containing distant explosions, incoming missiles, moving fragments, directional echoes, and continuous head movement. It is intended to preserve the reasoning needed to resume the work without implying that these features already exist.

## Existing behaviour and constraints

- Steam Audio's public Direct Effect does not add the source-to-listener propagation delay. Valve leaves that to the integration because varying the delay also produces Doppler shift.
- Steam Audio reflection simulation expresses reflection arrival times relative to the direct path. Adding one base propagation delay before both direct and reflected processing therefore preserves the intended direct-versus-echo timing.
- The current player evaluates Godot's emission cone in GDScript and passes the resulting scalar gain to `AudioEffectSteamDirect`. The Reflection Send does not currently receive the source orientation or emission model.
- The current reflection path supports parametric reverb and loadable first-order directional energy fields. Automatic geometry simulation and probe selection are separate future systems.

## Central rule

Transport the audio and its emission-time source state through world space, then render the arriving state into listener space using the listener transform at arrival time.

State that travels with the sound includes:

- source world position and orientation;
- emission/directivity parameters;
- reflection-field selection and blend;
- occlusion, transmission, and reflection-send parameters;
- the audio samples themselves.

State that remains live at playback includes:

- listener world position and orientation;
- final binaural rendering and output configuration.

For a delayed source position:

```text
world_direction = normalize(delayed_source_position - current_listener_position)
listener_direction = inverse(current_listener_orientation) * world_direction
```

This is important in VR: a listener may turn their head while a distant sound is travelling. Baking listener orientation into the delayed record would make the arriving sound follow an obsolete head pose.

## Proposed effect order

1. Propagation Delay
2. Reflection Send
3. Direct Path
4. Direct HRTF
5. Reflection HRTF Return

The Propagation Delay owns the audio delay line and the matching history of source-side parameters. When a delayed block emerges, it publishes the corresponding state to the downstream effects. One base delay feeds both paths; Steam Audio's reflection data then supplies the additional path delay for echoes.

## Recorded state

Use an absolute sample counter, not Godot callback number or callback size. A first implementation can record one state per input block and interpolate it at the same fractional read position as the audio.

```cpp
struct PropagationRecord {
    uint64_t input_sample;
    Vector3 source_world_position;
    Quaternion source_world_orientation;
    float emission_cone_angle;
    float emission_cone_outside_gain;
    uint16_t reflection_field_a;
    uint16_t reflection_field_b;
    float reflection_field_blend;
    Vector3 reflection_reverb_times;
    float reflection_send_gain;
    bool reflections_enabled;
    float occlusion;
    Vector3 transmission;
};
```

The audio and state rings must be fixed-capacity and preallocated. Callback-size changes must not invalidate their timeline.

## Variable delay and Doppler

A fractional delay line implements:

```text
y(t) = x(t - delay(t))
```

Changing `delay(t)` naturally produces Doppler shift. This should eventually replace the player's separate velocity-accumulator Doppler path, but the old path should remain available until listening and numerical tests agree.

Candidate controls are:

- enabled;
- speed of sound;
- maximum delay in seconds;
- latency compensation in seconds;
- source and listener world positions;
- interpolation quality;
- teleport/discontinuity policy.

For VoIP, a useful policy is:

```text
audible_delay = max(0, virtual_distance / speed_of_sound - measured_transport_latency)
```

This aligns virtual propagation with delay already incurred by the network without attempting impossible negative delay. Supersonic shock cones and sonic booms are separate source/geometry effects, not consequences of this delay line alone.

## Communicating with downstream effects

The delay effect should own the history rings, but target stable effect resources rather than fragile effect instances. Godot may recreate effect instances when a bus changes.

At audio rate, publish a plain-data arrival snapshot using atomics or a sequence-lock-style double buffer. Do not call Godot setters, emit signals, allocate memory, or take an ordinary mutex from the audio callback. Each player must own unique effect resources; sharing them would cross-wire source histories.

A later refactor may replace explicit targets with a small common `ArrivalState` interface. The first implementation should favour observability: direct, reflected, dry, and delayed paths must remain independently mutable and metered.

## Delayed source orientation and emission angle

The directivity scalar should be evaluated from historical source orientation but the current listener position:

```text
historical_forward = delayed_source_orientation * local_forward
to_listener = current_listener_position - delayed_source_position
directivity = evaluate_emission_model(historical_forward, to_listener, cone_settings)
```

Direct Path receives this delayed result. Reflection illumination is more complicated: a single source-to-listener scalar cannot describe how a directional source illuminates surrounding walls. Faithful reflection baking may require distinct energy fields for source position and orientation, or a basis representation that can be blended at runtime.

## Reflection energy-field rack

Reflection Send should eventually expose an indexed rack of immutable, prepared field resources. A field resource contains:

- dimensions and duration;
- serialized energy-field values;
- reconstructed ambisonic impulse response;
- prepared FFT partitions;
- optional authored source position/orientation metadata.

Preparation happens off the audio thread. Propagation records carry only field indexes and blend weights, never large arrays or FFT kernels.

Changing a convolution kernel cannot simply reset the active convolver: doing so truncates the room tail and can produce overloads. Keep old and new convolver slots alive and crossfade their outputs. Probe interpolation may require several simultaneously active fields with normalized weights. Rack slots should remain stable during playback; replacement is an explicit non-realtime operation.

## Crater missile VR demonstration

The demo should make the engineering audible and visible rather than merely show settings.

Scenarios:

1. Distant explosions at known radii, with visible flashes before their sound arrives.
2. Impacts above the crater rim and inside the crater, using different baked reflection fields.
3. A head turn while a sound is in flight, proving that arrival direction uses the current head pose.
4. A moving missile shriek, demonstrating Doppler generated by changing propagation delay.
5. A tumbling fragment with a directional jet, demonstrating delayed source orientation and emission angle.
6. Wet-only and dry-only controls for inspecting reflections independently.
7. Microphone/VoIP monitoring with measured latency compensation.
8. A/B modes for no propagation, legacy Doppler, physical propagation, and emergency mute.

Useful visuals include missile trails, impact flashes, an expanding acoustic wavefront, source-at-emission and source-at-arrival markers, selected reflection-field identifiers, delay read/write positions, and per-stage peak meters.

Acceptance checks:

- direct arrival delay agrees with distance divided by speed of sound;
- echo timing retains its correct additional delay after base propagation is added;
- turning the head during flight changes final HRTF orientation without moving the historical source;
- constant radial velocity produces the expected Doppler ratio;
- teleports do not create unbounded pitch or buffer reads;
- reflection-field transitions preserve tails and do not overload;
- the callback allocates no memory and tolerates changing callback sizes;
- VoIP compensation never requests negative delay.

## Suggested implementation stages

1. Fixed, sample-accurate delay with impulse tests.
2. Fractional variable delay with Doppler and teleport tests.
3. Position-history ring feeding Direct HRTF.
4. Delayed Direct Path and source-orientation/emission calculation.
5. Reflection Send after the delay, with an impulse timing test for direct and reflected paths.
6. Immutable prepared energy-field resources and an indexed rack.
7. Two-field convolution transition with tail-preserving crossfade.
8. Desktop crater test scene and instrumentation.
9. VR version with continuous head movement.
10. Timestamped VoIP latency compensation.

Keep dry bypass and wet-only monitoring available at every stage. Do not remove the existing Doppler implementation until the replacement is proven.

## Questions deliberately left open

- Should downstream effects be explicit targets or consume a common arrival-state resource?
- Should the caller provide the desired delay, or should the effect solve retarded source/listener time from trajectories?
- How many simultaneous convolution fields are affordable on desktop, mobile, and VR hardware?
- Should directional reflection fields be baked per orientation, procedurally generated, or represented as a directional basis?
- Which Steam Audio hybrid reflection modes should be exposed without hiding their cutoff and transition behaviour?
- Which clock and timestamp contract should align VoIP transport latency with audio-device latency?

These questions should be answered with small listening tests and instrumentation before the public API is frozen.

## Relevant upstream references

- [Steam Audio issue discussing speed-of-sound delay and Doppler](https://github.com/ValveSoftware/steam-audio/issues/79)
- [Steam Audio reflection simulator](https://github.com/ValveSoftware/steam-audio/blob/master/core/src/core/reflection_simulator.cpp)
- [Steam Audio delay effect implementation](https://github.com/ValveSoftware/steam-audio/blob/master/core/src/core/delay_effect.cpp)

