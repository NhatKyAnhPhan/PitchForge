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
- JUCE is not installed globally. CMake will fetch the pinned JUCE 8.0.11
  dependency on first configure.
- No MCP resources are currently available.
- Native contract smoke build passes with Visual Studio 2026; 1/1 CTest passed.
- Flutter Android/Windows project skeleton generated; starter Flutter widget
  test passes and a Windows Debug executable builds. The generated counter UI is
  scaffolding only and is not a product feature.
- Android Debug APK builds successfully from the Flutter starter project
  (native audio plug-in/DSP not integrated).
- Updated native contract tests pass 2/2, including C ABI header compilation
  as both C and C++.

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
| Windows/Android platform hosts | In parallel implementation |
| Integration and cross-platform QA | Pending module review |

## Project task list

- [x] Survey repository, skills, toolchains, and platform constraints.
- [x] Establish architecture, module ownership, dependency graph, and C/C++
  contracts.
- [x] Create CMake + Flutter Windows/Android skeletons and baseline test plan.
- [x] Verify native contract configure/build/test and Flutter Windows/Android
  shell builds.
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
| Windows | `feature/windows` | `C:\Users\admin\.local\share\opencode\worktree\global\windows` | Windows runner/package/docs | In progress |
| Android | `feature/android` | `C:\Users\admin\.local\share\opencode\worktree\global\android` | Oboe/NDK host + Gradle/docs | In progress |
| QA | `feature/qa` | `C:\Users\admin\.local\share\opencode\worktree\global\qa` | Integration tests/reports | In progress |

Shared public headers and CMake integration remain orchestrator-owned. Agents
must commit their scoped changes before code review/merge.

## Blockers / limitations

- Contract smoke test is not the full JUCE audio engine build; the pinned JUCE
  dependency has not yet been fetched or compiled.
- Android SDK licenses need acceptance before claiming Android build readiness.
- The Android APK test used the unmodified Flutter starter app; it proves
  packaging/toolchain only, not microphone access, audio I/O, or DSP.
- Android Oboe plugin has not been compiled or tested on a physical device.
- No real-time DSP, audio passthrough, Auto-Tune, or preset processing exists yet.
- JUCE GPLv3/commercial terms must be reviewed before distribution.
