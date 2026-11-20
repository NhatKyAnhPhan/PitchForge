# Project Status

**Milestone:** Native audio/DSP/mixer integrated; Flutter product UI pending
**Date:** 2026-11-20
**Overall:** Native modules and both platform libraries build; automated native
tests pass. Audio has not been verified on physical hardware, so the MVP is not
complete.

## Repository and environment

- Workspace was empty apart from local `.opencode/skills`; a Git repository was
  initialized on `main`.
- Git 2.52.0, Flutter 3.47.3, Dart 3.13.3, Android Studio, JDK 21, Android SDK
  platforms 31/33–36, NDK 28.2 and 30.0, Android CMake 3.22.1 and 4.1.2, and
  Visual Studio CMake 4.3.1.
- `flutter doctor -v` detects Visual Studio Community 2026 and Windows desktop
  support; Android licenses are not all accepted.
- JUCE is not installed globally. CMake fetched/configured pinned JUCE 8.0.11
  successfully; its `juceaide` helper built during configure. The JUCE-enabled
  Windows Release build now links `auralis_audio_engine.dll`.
- No MCP resources are currently available.
- Integrated native configure/build and CTest pass **5/5 in Debug and Release**
  with Visual Studio 2026.
- The JUCE-enabled Windows Release DLL builds and exports the engine, mixer, and
  effect C ABI symbols. The real-library QA harness validates API/layout,
  pointer/index handling, and device enumeration; live callback/audio checks
  were not run.
- Flutter Debug APK builds with Oboe, engine, mixer, and DSP for `arm64-v8a`,
  `armeabi-v7a`, and `x86_64`; the shared engine symbols were verified in each
  ABI library.
- Flutter product UI work is still on `feature/ui` and has not yet been merged
  into `main`. The APK build therefore verifies native packaging, not the final
  product UI.
- `adb devices` finds no physical/emulated Android device for audio smoke tests.

## Skills

- Read and used: `ui-ux-pro-max`, `ui-styling`, `opencode`.
- Local skill directory also includes `banner-design`, `brand`, `design-system`,
  `design`, and `slides`; none provides C++/JUCE/DSP/platform/build QA guidance.
- UI skill recommends touch targets >=48dp on Android and non-gesture
  alternatives for core operations. Its Flutter stack query had no matching
  result; general native UI guidance is used instead.

## Architecture

- Flutter UI on both platforms; C++20 native engine/DSP with C ABI boundary.
- Windows uses JUCE/WASAPI; Android uses an NDK Oboe/AAudio plugin.
- JUCE 8.0.11 official CMake docs say Android app targets are unsupported.
- ASIO is disabled pending licensing review; WASAPI is the default Windows path.

## Milestones

| Milestone | Status |
|---|---|
| Repository/environment survey | Complete |
| Architecture and shared contracts | Complete; frozen for parallel module work |
| Project skeleton smoke build | Complete (Windows native 2/2 CTest; Flutter Windows and Android Debug builds) |
| Native audio + DSP + mixer | Integrated; 5/5 native tests pass in Debug and Release |
| Flutter UI | In progress on `feature/ui`; not yet integrated |
| Windows runner/package host | Native Release DLL builds; physical-device and packaged-app audio testing pending |
| Android native host | APK and 3 ABI libraries build; physical-device testing pending |
| QA harness/report | Real Windows DLL loads; audio/callback suite is explicitly pending |
| Integration and cross-platform QA | Cross-builds pass; UI and hardware validation pending |

## Project task list

- [x] Survey repository, skills, toolchains, and platform constraints.
- [x] Establish architecture, module ownership, dependency graph, and C/C++
  contracts.
- [x] Create CMake + Flutter Windows/Android skeletons and baseline test plan.
- [x] Verify native contract configure/build/test and Flutter Windows/Android
  shell builds.
- [x] Verify pinned JUCE fetch and CMake setup (`juceaide`).
- [x] Harden Windows Flutter runner and verify shell release ZIP packaging.
- [x] Add real-library C ABI QA harness and read-only mixer-header validation.
- [x] Implement native engine lifecycle, device enumeration/selection, callback,
  passthrough path, clipping-safe meters, and latency reporting (unit tests pass;
  physical-device behavior remains unverified).
- [x] Implement/test DSP: EQ, gate, compressor, limiter, reverb, delay, tone and
  pitch detector/correction with key/scale/retune controls (signal tests pass;
  audio quality/latency remain unverified on hardware).
- [ ] Complete app-facing preset load/save transaction across the native ABI.
  Native binary codec and Flutter file persistence exist, but atomic replacement
  of a live chain is not yet exposed or verified.
- [ ] Implement responsive desktop/mobile mixer UI, device panel, meters,
  effect controls, pitch display, accessible interactions, and preset browser.
- [x] Integrate Windows WASAPI host and produce a Windows Release audio DLL
  (the real device stream remains untested).
- [x] Integrate Android Oboe/AAudio plugin, permission/lifecycle/routing handling,
  and build a debug APK (the real device stream remains untested).
- [ ] Integrate all modules, review code, resolve API/build conflicts, and run
  cross-module tests.
- [ ] Validate audio on physical hardware, report latency/device limitations,
  package builds, and prepare release notes.

## Active worktrees

| Agent | Branch | Worktree | Ownership | State |
|---|---|---|---|---|
| Audio Engine | `feature/engine` | `C:\Users\admin\.local\share\opencode\worktree\global\engine` | C API, common engine, Windows WASAPI adapter | Merged (`29f39f1`); integrated DLL built |
| DSP | `feature/dsp` | `C:\Users\admin\.local\share\opencode\worktree\global\dsp` | DSP algorithms + tests | Merged (`3a41409`); integrated tests pass |
| Mixer | `feature/mixer` | `C:\Users\admin\.local\share\opencode\worktree\global\mixer` | Signal chain + presets + tests | Merged (`e08f938`); Release test and snapshot fixes integrated |
| UI/UX | `feature/ui` | `C:\Users\admin\.local\share\opencode\worktree\global\ui` | Flutter widgets/services/tests | In progress; awaiting final test/commit |
| Windows | `feature/windows` | `C:\Users\admin\.local\share\opencode\worktree\global\windows` | Windows runner/package/docs | Merged (`d5edd0f`); shell package tested |
| Android | `feature/android` | `C:\Users\admin\.local\share\opencode\worktree\global\android` | Oboe/NDK host + Gradle/docs | Merged (`5a45f0e`); fixes integrated and APK exports verified |
| QA | `feature/qa` | `C:\Users\admin\.local\share\opencode\worktree\global\qa` | Integration tests/reports | Merged (`3dba2c4`); runtime audio mode still pending |

Shared public headers and CMake integration remain orchestrator-owned. The native
feature branches were reviewed and merged; Flutter UI is the remaining feature
branch. Integration fixes are recorded on `main` after each build/review cycle.

## Blockers / limitations

- No WASAPI or Oboe audio stream has been opened for a real hardware test.
- Windows DLL packaging/staging alongside the final Flutter UI is not yet
  re-verified after native engine integration.
- Android SDK licenses need acceptance before claiming Android build readiness.
- Android APK and native libraries compile, but permission, audio focus, routing,
  reconnect, passthrough, latency, and audible DSP require physical-device tests.
- Pitch-shift artifact quality and reported latency are only covered by
  deterministic signal tests; live vocal quality is unverified. True formant
  preservation is not implemented.
- Atomic native preset replacement is not yet available through the C ABI.
- JUCE GPLv3/commercial terms must be reviewed before distribution.
