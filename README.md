# Auralis Studio

Auralis Studio is a cross-platform live vocal and audio-processing application
for Windows and Android. The project is at an early MVP stage.

## Current direction

- **Application UI:** Flutter, shared between Windows desktop and Android.
- **Native audio/DSP:** C++20 with a stable C ABI for the Flutter boundary.
- **Windows audio:** JUCE device management with WASAPI as the supported default.
- **Android audio:** Android NDK with Oboe/AAudio through a Flutter platform plugin.
- **DSP:** reusable realtime-safe C++ processors and a bounded, ordered signal chain.
- **ASIO:** not enabled by default; JUCE's bundled ASIO SDK has separate licensing
  obligations. WASAPI is the initial Windows backend.

JUCE 8.0.11's official CMake API does not provide Android app targets. Android
integration will therefore use Flutter/Gradle for packaging and an Android
native audio backend, rather than claiming an unsupported JUCE Android CMake
application. This boundary and its validation status are tracked in
[`ARCHITECTURE.md`](ARCHITECTURE.md).

## Repository map

- `include/auralis/` — shared, framework-independent API contracts.
- `native/` — C++ implementation and CMake targets.
- `app/` — Flutter application (being established).
- `tests/` — native and integration tests.
- `docs/` — platform, DSP, and development notes.

## Build

See [`BUILD_WINDOWS.md`](BUILD_WINDOWS.md) and [`BUILD_ANDROID.md`](BUILD_ANDROID.md).
Do not treat a configured build as a successful build; run the documented build
and tests on the target platform.

## License

Project licensing is not yet selected. JUCE 8.0.11 is fetched with
`-DAURALIS_FETCH_JUCE=ON` and is available under GPLv3 or a commercial license.
Confirm product distribution requirements before shipping a closed-source binary.
