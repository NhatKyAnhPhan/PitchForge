# API Contracts

This document freezes module boundaries for parallel implementation. Propose
public changes here first; do not silently change headers another module owns.

## Shared C++ contracts

- `auralis::audio::ProcessSpec` — sample rate, maximum callback block, channel
  count.
- `auralis::audio::AudioBlockView` — non-owning planar float block, valid only
  during the current callback.
- `auralis::dsp::AudioProcessor` — off-thread `prepare`, `reset`, callback
  `process`, enabled state, and normalized parameter get/set methods.
- `auralis::dsp::createProcessor` — control-thread factory keyed by
  `mixer::EffectType`; returns null for unsupported effects.
- `auralis::mixer::SignalChain` — callback processing, bounded non-blocking
  command enqueue, and caller-owned snapshot copy.
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

API version starts at `1`; increment it only for incompatible ABI changes.

## Ownership

- Audio Engine owns platform streams, callback lifetime, input/output meters,
  and lifecycle.
- DSP owns effect algorithms and per-effect parameter interpretation.
- Mixer owns effect identity, ordering, bypass, routing, and chain snapshots.
- Flutter owns presentation, input validation, preset browsing, and persistence
  requests; presets are not hardcoded display-only data.
- Windows/Android adapters own platform permissions, device changes, and their
  native stream implementation.

## Presets

Preset state is versioned, validated off-thread, and describes effect type,
stable instance IDs, order, bypass, wet/dry, and DSP parameter IDs/values. Loading
must be transactional: reject malformed/unsupported data without leaving a
partially applied realtime chain.
