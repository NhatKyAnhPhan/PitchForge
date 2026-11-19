# Windows Build

## Support boundary

The Flutter Windows runner and packaging path are buildable independently from
the audio backend. The native target in this checkout is currently an
interface-only `auralis_core`; there is no `auralis_audio_engine.dll` or
implemented WASAPI stream yet. A successful Flutter build/package is therefore
the UI shell only, not a realtime audio MVP.

Windows audio is intended to use JUCE's WASAPI backend. Do not infer support for
ASIO, exclusive-mode operation, a particular device format, or hot-plug
recovery until the Windows engine adapter is implemented and tested on real
devices.

## Requirements

- Windows 10/11 x64
- Visual Studio 2022 or newer with **Desktop development with C++** and a
  Windows SDK
- CMake 3.22+
- Git
- Flutter stable with Windows desktop enabled

The initial machine has Visual Studio Community 2026, Flutter 3.47.3, and
Visual Studio's bundled CMake 4.3.1. Android SDK CMake packages do not list a
Visual Studio 2026 generator; use the bundled CMake or another recent version.
On that machine, the bundled CMake is:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
```

If `cmake` is not on `PATH`, use the bundled path above. Check available
generators with `& $cmake --help`; the installed toolchain may expose a
different Visual Studio generator.

## Native core

The local toolchain supports `Visual Studio 18 2026`. The scaffold/contracts
build below does not fetch JUCE and does not produce an audio engine DLL.
From the repository root:

```powershell
cmake -S . -B build/native-scaffold -G 'Visual Studio 18 2026' -A x64 `
  -DAURALIS_FETCH_JUCE=OFF -DAURALIS_BUILD_TESTS=ON
cmake --build build/native-scaffold --config Release
ctest --test-dir build/native-scaffold -C Release --output-on-failure
```

If CMake is not on `PATH`, prefix each command with `& $cmake`.

### JUCE-enabled Windows engine

For the audio-enabled configuration, fetch the pinned JUCE dependency. JUCE
ASIO remains excluded/off by default; WASAPI is the intended Windows backend.

```powershell
cmake -S . -B build/native-juce -G 'Visual Studio 18 2026' -A x64 `
  -DAURALIS_FETCH_JUCE=ON -DAURALIS_BUILD_TESTS=OFF
cmake --build build/native-juce --config Release
```

This project state still defines only the `auralis_core` interface target: the
JUCE-enabled command verifies/builds the dependency (including `juceaide`) but
does not yet produce `auralis_audio_engine.dll`. Once native integration adds
that target, build it with
`cmake --build build/native-juce --config Release --target auralis_audio_engine`
and pass the resulting DLL to the Flutter build/package steps below. Do not
describe the scaffold or JUCE dependency build alone as audio-enabled.

## Flutter desktop

From `app`:

```powershell
flutter config --enable-windows-desktop
flutter pub get
flutter test
flutter build windows --release
```

The runner checks COM initialization, balances COM teardown, handles message
loop failures, and reports window-creation failures through the debugger output.
The Flutter Windows build stages the ICU data, Flutter runtime, plugins, and
assets beside/in `build\windows\x64\runner\Release`.

## Staging/loading the native engine DLL

`app/windows/CMakeLists.txt` accepts an optional absolute path in either the
`AURALIS_AUDIO_ENGINE_DLL` CMake cache variable or environment variable. It
validates the filename and existence, copies the DLL beside
`auralis_studio.exe` after builds, and includes it in CMake's runtime install
bundle. Flutter caches its CMake configuration, so run `flutter clean` after
changing the DLL path. The release script below does this automatically. For a
manual build from the repository root after the engine target exists:

```powershell
$env:AURALIS_AUDIO_ENGINE_DLL = (Resolve-Path '.\build\native\Release\auralis_audio_engine.dll').Path
Push-Location app
flutter clean
flutter build windows --release
Pop-Location
Remove-Item Env:AURALIS_AUDIO_ENGINE_DLL
```

The Flutter output should contain both `auralis_studio.exe` and
`auralis_audio_engine.dll` in the same directory. Windows resolves the engine
and its dependent DLLs according to normal DLL search rules. The Dart FFI
consumer should open the engine by an absolute path derived from
`Platform.resolvedExecutable`'s parent directory, rather than relying on the
current working directory. No Dart FFI loader exists in this checkout yet.
Ensure any non-system dependencies of the engine are staged alongside it too.

## Release ZIP

`packaging/windows/package-release.ps1` performs a clean Flutter Release build,
validates the runtime bundle, and writes
`app\build\windows\packages\auralis_studio-windows-x64.zip`. It uses only
Flutter and PowerShell's built-in `Compress-Archive`; no installer toolchain is
present, so this branch does not claim to produce an installer.
The script discovers Flutter on `PATH`, through `FLUTTER_ROOT`, or at
`C:\flutter\bin\flutter.bat`; pass `-FlutterPath` when it is installed elsewhere.

Package the shell:

```powershell
.\packaging\windows\package-release.ps1
```

Package with the engine once it is available:

```powershell
.\packaging\windows\package-release.ps1 `
  -EngineDll '.\build\native\Release\auralis_audio_engine.dll'
```

Without `-EngineDll`, the script warns that the archive is a UI shell without
audio. It cleans the Flutter build first to avoid stale engine DLLs or cached
paths contaminating an archive.

## WASAPI, devices, privacy, and errors

- WASAPI is the intended Windows backend through JUCE, but is not implemented in
  this checkout. The future backend must enumerate input/output endpoints,
  report the selected device and supported format, and own stream lifecycle and
  device-change recovery; do not add a second audio path in the Flutter runner.
- Device enumeration and engine lifecycle are control-thread operations.
  Audio samples stay in the native callback; do not route them through Dart.
- Respect Windows **Settings → Privacy & security → Microphone** and the
  **Microphone access** / **Let desktop apps access your microphone** controls.
  If microphone access is disabled, explain the setting and let the user retry
  after changing it. Do not persist or transmit device/audio data as a side
  effect of enumeration; request/start input only when the user chooses it.
- Surface native status codes rather than silently retrying:
  `AURALIS_STATUS_INVALID_ARGUMENT` means invalid selection/configuration;
  `AURALIS_STATUS_DEVICE_ERROR` means the endpoint/stream could not be opened
  or failed; `AURALIS_STATUS_NOT_READY` means the engine is not initialized;
  `AURALIS_STATUS_QUEUE_FULL` means a bounded control update was not accepted;
  `AURALIS_STATUS_INTERNAL_ERROR` is an unexpected native failure. These are
  contract meanings; detailed OS error strings and reconnect policy remain to
  be implemented by the Windows backend.
- For endpoint open failures, ask the user to check Windows Sound input/output
  selection, app microphone privacy, device connectivity, and whether another
  application is holding a device in an incompatible mode. Then retry
  enumeration/open; do not claim automatic fallback until implemented.

## ASIO licensing

ASIO support is off by default and is not claimed. JUCE's bundled ASIO SDK is
GPLv3-licensed according to the project architecture decision. Do not enable or
ship ASIO in a proprietary build without an approved compatible licensing
review. WASAPI is the Windows MVP target.

## Troubleshooting and validation

- `flutter doctor -v`: verify the Windows toolchain is green. Android SDK
  license warnings do not block a Windows build.
- CMake generator not found: run `& $cmake --help` and select a generator
  available in the installed Visual Studio/CMake.
- Engine DLL validation fails: verify the path exists, is the x64 Release DLL,
  and is named exactly `auralis_audio_engine.dll`; pass an absolute path.
- DLL load failure: verify the engine and all its non-system dependencies are
  beside the executable and match the process architecture. Check the debugger
  output/Windows Event Viewer for loader details.
- No input device or access denied: check Windows Sound settings and the
  microphone privacy controls above. The runner cannot grant microphone access
  or recover an audio stream.
- Verify a release build with `flutter build windows --release`, run
  `flutter test` from `app`, and test input/output on physical Windows devices.
  A successful shell build is not evidence of WASAPI or realtime audio support.
