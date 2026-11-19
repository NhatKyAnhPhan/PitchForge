# Development Guide

## Prerequisites (verified on the initial development machine)

- Git 2.52.0
- Flutter 3.47.3 / Dart 3.13.3 (`C:\flutter`)
- Android Studio, Android SDK API 31, 33–36, NDK 28.2 and 30.0
- Android SDK CMake 3.22.1 and 4.1.2
- Visual Studio-bundled CMake 4.3.1 and Ninja 1.13.2
- Visual Studio Community 2026 with C++ components (detected by `flutter doctor`)
- JDK 21 (bundled with Android Studio)
- Python 3 (used by the UI/UX skill scripts)

`flutter doctor -v` currently reports unaccepted Android SDK licenses. Do not
silently mark Android builds successful until licenses are accepted and a target
build completes.

The contract-only native CMake smoke build has been configured with the
`Visual Studio 18 2026` generator and passed its initial CTest test.

## Realtime rules

1. `process()` is called by the platform audio thread.
2. Never allocate/free memory, lock a contended mutex, log, access files/network,
   or call UI code from the audio callback.
3. Prepare/reset processors off the audio thread before publishing them.
4. Transfer commands through bounded queues; expose queue saturation to callers.
5. Use atomics or immutable snapshots for meters and parameter visibility.
6. Clamp/validate external values before they reach DSP.
7. Avoid destroying processors on the audio thread. Reclamation must happen on
   a non-realtime thread.
8. Measure actual device latency and report estimates honestly; device-reported
   buffer sizes are not end-to-end round-trip measurements.

## Workflow

- Keep platform work in `platform/` and Flutter-only work in `app/`.
- Add tests for DSP and state changes. Do not use mock signal paths to claim an
  effect is implemented.
- Use branches/worktrees for parallel work and commit scoped changes.
- Before integration, review public API changes, run formatting/tests, configure
  both native and Flutter builds, and report platform limitations.
- Update `PROJECT_STATUS.md` when a milestone changes.

## JUCE licensing

JUCE 8 is offered under GPLv3 or a commercial license. The default build omits
ASIO; enabling the bundled ASIO SDK introduces additional GPLv3 obligations.
Review licensing before distributing a proprietary product.
