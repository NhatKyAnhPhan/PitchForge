# API Contracts

This document freezes module boundaries for parallel implementation. Propose
public changes here first; do not silently change headers another module owns.

## Shared C++ contracts

- `auralis::audio::ProcessSpec` — sample rate, maximum callback block, channel
  count.
- `auralis::audio::AudioBlockView` — non-owning planar float block, valid only
  during the current callback.
- `auralis::audio::AudioDeviceBackend` — platform-owned device enumeration,
  selection, lifecycle, latency estimate, and planar float callback contract.
- `auralis::audio::AudioDeviceCallback` — non-blocking realtime input/output
  callback used by the engine and platform adapters.
- `auralis::dsp::AudioProcessor` — off-thread `prepare`, `reset`, callback
  `process`, enabled state, and normalized parameter get/set methods.
- `auralis::dsp::createProcessor` — control-thread factory keyed by
  `mixer::EffectType`; returns null for unsupported effects.
- `auralis::dsp::parameters` — stable per-effect parameter IDs; C ABI values are
  normalized to [0, 1], with parameter-specific conversion to display units.
- `auralis::mixer::SignalChain` — callback processing, bounded non-blocking
  command enqueue, off-thread preparation, and caller-owned snapshot copy.
- `auralis::mixer::createSignalChain` — control-thread factory for the production
  fixed-capacity signal-chain implementation.
- `auralis::mixer::ChainCommand` — fixed-size trivially-copyable control
  operation. Queue full must be reported, not waited through.
- `kMaximumEffectsPerChannel` — 16 for the MVP; effects beyond this are rejected.

All C++ public headers are framework-independent. Platform backends adapt
WASAPI/JUCE or Oboe buffers to the planar float view.

## C ABI for Flutter

`include/auralis/bridge/auralis_c_api.h` defines a versioned, opaque engine
handle, fixed-layout POD configuration/device/meter structs, and status codes.
Strings are fixed-size UTF-8 buffers. API functions must validate pointers and
indices and must not throw across the C boundary.

The UI bridge may call device enumeration, engine start/stop, device selection,
and snapshot APIs from a control thread. It must not call allocating APIs from
an audio callback. Real-time parameter updates use bounded queues in the native
implementation.

The C ABI also defines stable effect-type numeric values, fixed-capacity chain
state snapshots, and add/remove/move/enable/parameter operations. A successful
control call means the command was accepted into the bounded queue, not that it
has already been applied by the audio callback. Queue saturation returns
`AURALIS_STATUS_QUEUE_FULL`.
The effect wet/dry control is a separate normalized [0, 1] command. Parameter
IDs are effect-specific and normalized values must be clamped/validated by DSP.
Input gain, output gain, pan, and mute are separate engine controls and meter
state is read-only. Solo requires multiple independently routable channels and
is not exposed by the single-vocal-channel MVP contract. Gain values are in
dB (input -60..+24, output -60..+12), pan is -1.0..+1.0, and mute is 0/1;
non-finite/out-of-range values are rejected or safely clamped consistently.
`SignalChain::prepare` is called on the control thread before a device stream is
started. The MVP may preallocate one processor instance per effect type; effect
insertion/removal/order changes are applied from the bounded command queue and
must not allocate on the audio thread.

API version starts at `1`; increment it only for incompatible ABI changes.

## Ownership

- Audio Engine owns platform streams, callback lifetime, input/output meters,
  and lifecycle.
- DSP owns effect algorithms and per-effect parameter interpretation.
- Mixer owns effect identity, ordering, bypass, routing, and chain snapshots.
- The UI layer (the Flutter and Rust UIs) owns presentation, input
  validation, preset browsing, and persistence requests; presets are not
  hardcoded display-only data.
- Windows/Android adapters own platform permissions, device changes, and their
  native stream implementation.

## Proposed additions (additive; C API version stays `1`)

Added to support the pitch-display UI milestone:

- `AuralisPitchDiagnostics` (`detected_frequency_hz`, `target_frequency_hz`,
  `confidence`, `applied_shift_semitones`) and
  `auralis_engine_get_pitch_diagnostics(engine, instance_id, destination)` —
  control-thread pitch-display telemetry. `instance_id` is the pitch-correction
  slot ID from `auralis_engine_get_chain_state`. Returns
  `AURALIS_STATUS_NOT_READY` when the slot is absent or the processor is not
  pitch correction. Implemented by `FixedSignalChain::copyPitchDiagnostics`
  (slot scan plus `dsp::getPitchCorrectionDiagnostics`).

## Presets

Preset state is versioned, validated off-thread, and describes effect type,
stable instance IDs, order, bypass, wet/dry, and DSP parameter IDs/values. Loading
must be transactional: reject malformed/unsupported data without leaving a
partially applied realtime chain.
