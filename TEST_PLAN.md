# Test Plan

## Native unit tests

- Audio block shape, null and zero-size inputs.
- Every processor: impulse/frequency/step response, finite output, bypass, reset,
  minimum/maximum parameter values, mono/stereo, supported rates and block sizes.
- Pitch detector/corrector: generated reference tones, note transitions,
  vibrato, low confidence, noise, octave errors, scale/key mapping and correction
  amount.
- Signal chain: ordering, enable/bypass, bounded capacity, queue-full handling,
  reorder/remove/add operations, preset round-trip and invalid-state rejection.
- C ABI: null pointers, invalid indices, API version, status translation, no
  exceptions escaping exported functions.

## Realtime and performance

- Instrument callback allocations in debug/test builds.
- Stress concurrent parameter changes, preset operations, UI meter reads, and
  device reconnects.
- Check lock-free callback behavior, CPU time, clipping, NaNs, and denormals.
- Measure callback duration and underruns at 44.1/48/96 kHz and multiple block
  sizes; record physical round-trip latency separately.

## Platform and integration

- The mixer ABI declaration/layout check is read-only and can run without an
  engine, using the updated header:

  ```powershell
  .\scripts\qa\run_c_api_integration.ps1 -HeaderOnly `
    -Header C:\opencode\include\auralis\bridge\auralis_c_api.h
  ```

  This checks `AuralisMixerState` field order/types/ctypes offsets and the
  getter/setter declarations. It does not establish that symbols are exported
  or that calls behave correctly.
- After engine integration, run the real-library C ABI suite (never a mock):

  ```powershell
  .\scripts\qa\run_c_api_integration.ps1 -Library path\to\auralis_engine.dll
  ```

  This binds and validates the mixer exports and checks ABI layout,
  API version, null pointers/indices, and device enumeration. It exits `2` and
  labels live behavior **BLOCKED** unless audio is explicitly requested; it does
  not call a contract-only build a full integration pass. With a device, it
  exercises mixer get/set, finite/out-of-range gain and pan policies, mute
  validation, chain/parameter operations, and concurrent command stress. Gain
  bounds are input -60..+24 dB, output -60..+12 dB; pan is -1..+1; mute is 0/1.
  Out-of-range setters may reject without changing state or clamp to the bound;
  any accepted non-finite value must leave a finite, in-range state. Chain
  parameter stress uses `Equalizer::Gain`, ID 1 from
  `include/auralis/dsp/ParameterIds.h`:

  ```powershell
  .\scripts\qa\run_c_api_integration.ps1 -Library path\to\auralis_engine.dll `
    -Audio -JsonReport reports\c-api-integration.json
  ```

  `-Audio` opens a real device stream; select a safe output level and prevent
  microphone/speaker feedback before running it.
  Override `-ParameterEffect` and `-ParameterId` only with a supported pair
  from that catalog; pass device indices where the default devices are unsuitable.
  The stress latency numbers are control-call timings, not callback duration or
  physical audio latency. Mixer declaration/layout checks can run now, but
  export binding, null-pointer behavior, and live mixer semantics remain pending
  until the engine implementation is integrated. Preset round-trip remains
  blocked until a preset API is exposed.
- Windows: WASAPI shared/exclusive where supported, input/output selection,
  device unplug/replug, sample-rate/buffer changes, startup/shutdown and mute.
- Android: permission denial/grant, foreground/background, audio focus,
  rotation, wired/USB/Bluetooth routing, reconnect and device-specific latency.
- UI: preset save/load, signal order changes with visible alternatives to drag,
  accessible labels, touch targets, and narrow/landscape layouts.
- Release gate: Windows build + tests, Android APK build + physical-device audio
  smoke test. An Android APK compile alone does not satisfy audio acceptance.

## Reports

For each run record OS/tool versions, target/device, command, result, test
coverage, known limitations, and whether the audio path was audible/real DSP
or only an unavailable backend.
