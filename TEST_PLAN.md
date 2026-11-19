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
