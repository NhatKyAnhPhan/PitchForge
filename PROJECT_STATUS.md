# Project Status

**Milestone:** Repository and shared-contract bootstrap
**Date:** 2026-11-20
**Overall:** Architecture/contracts complete; no feature should yet be
described as complete.

## Repository and environment

- Workspace was empty apart from local `.opencode/skills`; a Git repository was
  initialized on `main`.
- Git 2.52.0, Flutter 3.47.3, Dart 3.13.3, Android Studio, JDK 21, Android SDK
  platforms 31/33–36, NDK 28.2 and 30.0, Android CMake 3.22.1 and 4.1.2, and
  Visual Studio CMake 4.3.1.
- `flutter doctor -v` detects Visual Studio Community 2026 and Windows desktop
  support; Android licenses are not all accepted.
- JUCE is not installed globally. CMake fetched/configured pinned JUCE 8.0.11
  successfully with `AURALIS_FETCH_JUCE=ON`; its `juceaide` helper built during
  configure. The application audio modules have not yet been compiled.
- No MCP resources are currently available.
- Native contract smoke build passes with Visual Studio 2026; 1/1 CTest passed.
- JUCE dependency configure passed with Visual Studio CMake 4.3.1; `juceaide`
  configured, built, and passed its setup check.
- Flutter Android/Windows project skeleton generated; starter Flutter widget
  test passes and a Windows Debug executable builds. The generated counter UI is
  scaffolding only and is not a product feature.
- Android Debug APK builds successfully from the Flutter starter project
  (native audio plug-in/DSP not integrated).
- Updated native contract tests pass 2/2, including C ABI header compilation
  as both C and C++.
- `flutter analyze` reports no issues in the starter app. `adb devices` finds
  no physical/emulated Android device for audio smoke tests.

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
| Native audio + DSP + mixer | In parallel implementation |
| Flutter UI | In parallel implementation |
| Windows runner/package host | Complete for Flutter shell; native DLL staging awaits engine |
| Android native host | In parallel implementation |
| QA harness/report | Complete for baseline/contracts; runtime integration awaits engine |
| Integration and cross-platform QA | Pending native module review |

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
- [ ] Implement native engine lifecycle, device enumeration/selection, callback,
  passthrough, clipping-safe meters, and latency reporting.
- [ ] Implement/test DSP: EQ, gate, compressor, limiter, reverb, delay, tone and
  realtime pitch detector/correction with key/scale/retune controls.
- [ ] Implement/test editable signal chain, bounded command queue, routing,
  bypass/wet-dry, and versioned preset save/load.
- [ ] Implement responsive desktop/mobile mixer UI, device panel, meters,
  effect controls, pitch display, accessible interactions, and preset browser.
- [ ] Integrate Windows WASAPI host and produce a reproducible Windows build.
- [ ] Integrate Android Oboe/AAudio plugin, permission/lifecycle/routing handling,
  and build a debug APK.
- [ ] Integrate all modules, review code, resolve API/build conflicts, and run
  cross-module tests.
- [ ] Validate audio on physical hardware, report latency/device limitations,
  package builds, and prepare release notes.

## Active worktrees

| Agent | Branch | Worktree | Ownership | State |
|---|---|---|---|---|
| Audio Engine | `feature/engine` | `C:\Users\admin\.local\share\opencode\worktree\global\engine` | C API, common engine, Windows WASAPI adapter | In progress |
| DSP | `feature/dsp` | `C:\Users\admin\.local\share\opencode\worktree\global\dsp` | DSP algorithms + tests | In progress |
| Mixer | `feature/mixer` | `C:\Users\admin\.local\share\opencode\worktree\global\mixer` | Signal chain + presets + tests | In progress |
| UI/UX | `feature/ui` | `C:\Users\admin\.local\share\opencode\worktree\global\ui` | Flutter widgets/services/tests | In progress |
| Windows | `feature/windows` | `C:\Users\admin\.local\share\opencode\worktree\global\windows` | Windows runner/package/docs | Merged (`d5edd0f`); shell package tested |
| Android | `feature/android` | `C:\Users\admin\.local\share\opencode\worktree\global\android` | Oboe/NDK host + Gradle/docs | In progress |
| QA | `feature/qa` | `C:\Users\admin\.local\share\opencode\worktree\global\qa` | Integration tests/reports | Merged (`3dba2c4`); runtime blocked on engine |

Shared public headers and CMake integration remain orchestrator-owned. Agents
must commit their scoped changes before code review/merge. The remaining feature
branches started from the frozen `e69b2aa` contract commit; additive C ABI and
root CMake updates on `main` must be incorporated during final integration.

## Blockers / limitations

- Contract smoke test is not the full JUCE audio engine build; the JUCE modules
  and application audio target have not yet been compiled.
- Windows release ZIP was tested without an engine DLL and correctly warned that
  it contains only the Flutter shell. DLL staging has not yet been exercised.
- Android SDK licenses need acceptance before claiming Android build readiness.
- The Android APK test used the unmodified Flutter starter app; it proves
  packaging/toolchain only, not microphone access, audio I/O, or DSP.
- Android Oboe plugin has not been compiled or tested on a physical device.
- No real-time DSP, audio passthrough, Auto-Tune, or preset processing exists yet.
- JUCE GPLv3/commercial terms must be reviewed before distribution.
