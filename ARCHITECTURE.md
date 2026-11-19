# Auralis Studio Architecture

## Goals

- One touch-capable, responsive Flutter UI on Windows and Android.
- C++20 signal processing and a small, versioned C ABI at the UI boundary.
- Platform-native low-latency I/O: JUCE/WASAPI on Windows; Oboe/AAudio on Android.
- Bounded processing, fixed-capacity signal-chain state, and no callback allocation
  or blocking.

## Component map

```text
Flutter UI (desktop + mobile)
  ├── mixer, effect panels, meters, preset browser
  └── platform plugin / FFI (control + snapshots; never streams audio through Dart)
        ├── Windows host ── JUCE AudioDeviceManager / WASAPI ──┐
        └── Android host ── Oboe / AAudio ─────────────────────┤
                                                              v
                                  C++ audio engine + signal graph
                                  ├── DSP processors
                                  ├── mixer/routing
                                  └── preset/state model
```

The audio device owns the realtime callback. Dart/Kotlin/C# UI code only sends
bounded control updates and reads meter/state snapshots; audio samples never
cross the UI bridge.

## Dependency and build decision

- C++20 and CMake are the native core baseline.
- JUCE is pinned to 8.0.11 for Windows audio-device support and DSP utilities.
- The official JUCE 8.0.11 CMake API documentation states that Android targets
  are not currently supported. Android is therefore packaged as a Flutter app
  and hosted by an NDK/Oboe plugin. Reusing only framework-independent DSP/core
  code avoids making an unsupported JUCE Android app claim.
- The Android Oboe adapter and native library must be proven with the installed
  NDK before the Android MVP is marked buildable.
- JUCE's bundled ASIO SDK is GPLv3-licensed; ASIO is excluded from the default
  build. WASAPI is the Windows MVP backend.
- The user's Android SDK has API platforms 31, 33–36; Flutter's doctor reports
  that some Android SDK licenses are not accepted.

## Module boundaries and file ownership

| Module | Public boundary | Owned implementation paths |
|---|---|---|
| Shared contracts | `include/auralis/{audio,dsp,mixer,bridge}/` | Architect until contracts freeze |
| Audio engine | C ABI + platform audio adapter | `native/src/audio/`, `native/src/platform/`, `native/include/auralis/audio/` |
| DSP | `dsp::AudioProcessor` | `native/src/dsp/`, `native/include/auralis/dsp/`, DSP tests |
| Mixer/signal chain | `mixer::SignalChain` | `native/src/mixer/`, `native/include/auralis/mixer/`, mixer tests |
| Flutter UI | C ABI / platform plugin only | `app/lib/`, `app/test/`, `app/pubspec.yaml` |
| Windows | existing engine boundary | `platform/windows/`, Windows build docs |
| Android | C ABI + plugin channel | `app/android/`, `platform/android/`, Android build docs |
| QA | documented module APIs | `tests/`, `TEST_PLAN.md` |
| Integration | all modules after review | top-level CMake, shared docs, release integration |

Agents must not modify shared public headers without coordination. Module APIs
are frozen in `API_CONTRACTS.md`; propose changes there before implementation.

## Dependency graph

```text
Architecture/contracts
       ├── DSP ─────────┐
       ├── mixer ───────┼── native engine integration
       ├── Windows host ┘
       ├── Android host ─── Flutter Android build
       ├── Flutter UI ───── platform bridge
       └── QA tests ─────── final integration/build verification
```

The module branches can be developed from the contract commit in parallel.
Final application build and cross-module QA depend on integration and review.
