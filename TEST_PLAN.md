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

- After engine integration, run the real-library C ABI suite (never a mock):

  ```powershell
  .\scripts\qa\run_c_api_integration.ps1 -Library path\to\auralis_engine.dll
  ```

  This performs ABI layout, API version, null-pointer/index, and real device
  enumeration checks. It exits `2` and labels the run **BLOCKED** unless live
  audio is explicitly requested; it does not call a contract-only build a full
  integration pass. With a device, exercise chain/parameter operations and
  concurrent command stress using the equalizer gain parameter
  (`Equalizer::Gain`, ID 1) from `include/auralis/dsp/ParameterIds.h`:

  ```powershell
  .\scripts\qa\run_c_api_integration.ps1 -Library path\to\auralis_engine.dll `
    -Audio -JsonReport reports\c-api-integration.json
  ```

  `-Audio` opens a real device stream; select a safe output level and prevent
  microphone/speaker feedback before running it.
  Override `-ParameterEffect` and `-ParameterId` only with a supported pair
  from that catalog; pass device indices where the default devices are unsuitable.
  The stress latency numbers are control-call timings, not callback duration or
  physical audio latency. Preset round-trip remains blocked until a preset API is
  exposed. Mixer-state/gain/pan/mute integration assertions are pending until
  their implementation is integrated into this worktree; do not infer behavior
  from header declarations alone.
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
