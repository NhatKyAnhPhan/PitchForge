# Changelog

## Unreleased

- Initialize the Auralis Studio repository, architecture, native API contracts,
  platform build notes, DSP plan, and QA plan.
- Pin JUCE 8.0.11 and establish Flutter + C++20/NDK as the cross-platform
  implementation direction.
- Verify the native contract skeleton with Visual Studio 2026 and CTest (1/1).
- Generate Android/Windows Flutter host scaffolding; pass starter widget test
  and build the Windows Debug shell (native audio not integrated).
- Build the Android Debug APK from the starter Flutter project and pass 2/2
  native contract tests, including C and C++ C-ABI header compilation.
- Configure/fetch JUCE 8.0.11 successfully with Visual Studio CMake and build
  the `juceaide` helper during configuration.
- Harden the Windows Flutter runner and add tested release ZIP packaging with
  explicit shell-only labeling when the native DLL is absent.
- Add a real-library C ABI integration harness, mixer ABI declaration/layout
  checks, and a baseline QA report; runtime behavior remains pending.
- Implement the native DSP processors, pitch detector/correction, fixed-capacity
  signal chain, presets codec, C ABI audio engine, and Windows WASAPI/Android
  Oboe backends.
- Integrate the native modules; pass 5/5 CTest cases in Debug and Release, build
  a JUCE-enabled Windows DLL, and build Android APK libraries for three ABIs.
- Fix release-mode mixer tests, Android CMake include paths and lifecycle/device
  reconfiguration handling, plus Windows JUCE duplicate-module linking.
- Keep physical-device audio validation and app-facing atomic preset replacement
  explicitly pending.
