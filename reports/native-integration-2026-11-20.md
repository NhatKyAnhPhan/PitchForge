# Native Integration Verification — 2026-11-20

## Results

| Check | Result | Scope |
|---|---|---|
| Integrated native CMake build, Debug | PASS | DSP, mixer, common engine, contract tests |
| Integrated native CMake build, Release | PASS | Same targets; Release-safe mixer test checks |
| CTest | PASS, 5/5 Debug and 5/5 Release | Contract, C ABI header, DSP, mixer, and fake-backend engine tests |
| JUCE-enabled Windows Release target | PASS | Built `auralis_audio_engine.dll` with JUCE 8.0.11 and shared-mode WASAPI |
| Windows DLL exports | PASS | API version, engine lifecycle, mixer state, and effect/chain exports present |
| Real-library QA harness without `--audio` | PARTIAL | ABI/layout, invalid arguments, and device enumeration pass; callback/mixer live checks are skipped |
| Flutter Android Debug APK | PASS | Oboe + common engine/DSP/mixer library packaged for arm64-v8a, armeabi-v7a, x86_64 |
| Android native C ABI exports | PASS | Engine, mixer, and effect symbols verified in all three ABI libraries |
| Android device / audible stream | NOT RUN | No device is attached; APK build is not an audio behavior test |
| Windows device / audible stream | NOT RUN | No WASAPI stream was opened during this verification |

## Important boundaries

- The Windows engine unit test uses a fake backend. It does not verify WASAPI
  endpoint startup, device format negotiation, callback timing, or passthrough.
- The Android build verifies NDK compilation, packaging, and exported symbols;
  microphone permission, audio focus, route switching, reconnect, latency, and
  audible output still need physical-device testing.
- The integrated Flutter application UI is still being completed separately.
- Preset codecs and file persistence exist, but the C ABI does not yet provide
  an atomic live-chain replacement operation. Preset loading is not yet claimed
  transactional.
- Pitch correction passes deterministic signal tests, but live vocal artifacts
  and true formant preservation are not verified; true formant preservation is
  not implemented.

## Reproduction

Use Visual Studio CMake 4.3.1 / generator `Visual Studio 18 2026`:

```powershell
cmake -S . -B build/integrated-core-vs18 `
  -G 'Visual Studio 18 2026' -A x64 `
  -DAURALIS_BUILD_TESTS=ON -DAURALIS_FETCH_JUCE=OFF
cmake --build build/integrated-core-vs18 --config Debug
ctest --test-dir build/integrated-core-vs18 -C Debug --output-on-failure
cmake --build build/integrated-core-vs18 --config Release
ctest --test-dir build/integrated-core-vs18 -C Release --output-on-failure
```

The Windows audio DLL was built from a JUCE-enabled configuration using the
locally fetched JUCE 8.0.11 source. The Android APK was built with
`cd app; flutter build apk --debug`.
