# QA Baseline — 2026-11-20

## Machine and toolchain

- Windows 11 Home Single Language, build 26200 (25H2), x64.
- Git 2.52.0; Visual Studio Community 2026 18.10.2 / MSVC 19.51.36260.0.
- CMake and CTest 4.3.1; Visual Studio 18 2026 x64 generator.
- Flutter 3.47.3, Dart 3.13.3; Python 3.14.5.
- Android SDK 36.1.0 and JDK 21. `adb` is installed, but `adb devices -l`
  returned no attached Android devices. `flutter doctor -v` reports Android
  SDK licenses not all accepted.
- Windows audio endpoints are installed (Realtek, Intel SST, NVIDIA, and virtual
  endpoints); no Auralis stream was opened.

## Checks run

| Check | Result | Scope |
|---|---|---|
| Native CMake configure + Release build | PASS | CMake 4.3.1, VS 2026 x64 |
| CTest | PASS, 2/2 | Contract/audio-types and C ABI header smoke tests only |
| `flutter test` | PASS, 1/1 | Generated starter counter widget |
| `flutter analyze` | PASS | No issues found |
| Integration runner Python parse/help | PASS | Runner validation only; no engine loaded |
| Integration runner with nonexistent DLL | Expected rejection, exit 1 | Confirms it requires a real library; not an integration pass |

The native target is still `auralis_core INTERFACE`; this baseline does not
exercise an engine implementation, live DSP, or device callback.

## Mixer ABI follow-up

- Read-only header check against main commit `6ec1cc5`'s
  `include/auralis/bridge/auralis_c_api.h`: PASS. The mixer state has four
  fields at ctypes offsets 0/4/8/12 and size 16 bytes; all five mixer getter /
  setter declarations match the expected signatures.
- The check used the supplied `C:\opencode\include` copy; public headers in this
  QA worktree were not changed.
- The real-library harness now requires those exports and includes null-pointer
  checks plus opt-in live set/get and range-policy checks for gain, pan, and
  mute. These runtime checks are **PENDING**: no implementation/shared library
  is present in this worktree, so no behavior is claimed.

## Integration gaps / blockers

- No Auralis engine shared library or implementation is present in this worktree.
  Consequently C ABI runtime calls, mixer/chain/parameter queue behavior,
  callback stress and callback timing/allocation/underrun measurements were not
  run.
- Physical Windows audio operation was not exercised. A Windows endpoint being
  installed is not evidence that Auralis audio works.
- No Android device is attached and Android licenses remain outstanding; Android
  permission, routing, lifecycle, and audible-output checks are unavailable.
- Read-only review of main commit `6ec1cc5`'s headers identified mixer-state,
  input/output gain, pan, and mute additions. Their integration assertions are
  now present but remain pending until the implementation is integrated into
  this worktree; declarations alone are not treated as behavior.
- The current C ABI exposes no preset save/load entry points, so transactional
  preset round-trip coverage remains blocked.
- `tests/integration/c_api_integration.py` reports control-call stress latency,
  not callback duration or physical round-trip latency. Use an integrated
  implementation and real device before interpreting those measures.

**Acceptance:** This report records baseline checks only; user MVP acceptance is
not complete.
