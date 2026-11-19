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
| Project skeleton smoke build | Complete (Windows configure/build + 1/1 CTest) |
| Native audio + DSP + mixer | Not started |
| Flutter UI | Not started |
| Windows/Android platform hosts | Not started |
| Integration and cross-platform QA | Not started |

## Blockers / limitations

- Contract smoke test is not the full JUCE audio engine build; the pinned JUCE
  dependency has not yet been fetched or compiled.
- Android SDK licenses need acceptance before claiming Android build readiness.
- Android Oboe plugin has not been compiled or tested on a physical device.
- No real-time DSP, audio passthrough, Auto-Tune, or preset processing exists yet.
- JUCE GPLv3/commercial terms must be reviewed before distribution.
