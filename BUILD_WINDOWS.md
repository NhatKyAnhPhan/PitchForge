# Windows Build

## Requirements

- Windows 10/11
- Visual Studio 2022 or newer with Desktop development with C++
- CMake 3.22+
- Git
- Flutter stable with Windows desktop enabled

The initial machine has Visual Studio Community 2026, Flutter 3.47.3, and
Visual Studio's bundled CMake 4.3.1. Android SDK CMake packages do not list a
Visual Studio 2026 generator; use the bundled CMake or another recent version.

## Native core

From the repository root:

```powershell
cmake -S . -B build/native -G "Visual Studio 18 2026" -A x64
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure
```

If the Visual Studio 2026 generator is unavailable, check `cmake --help` and
select a generator supported by the installed toolchain.

## Flutter desktop

```powershell
cd app
flutter config --enable-windows-desktop
flutter pub get
flutter test
flutter build windows --release
```

The Flutter shell is not considered an audio MVP until its native Windows
backend is linked and input/output devices plus realtime passthrough are tested.
Packaging/installer work is tracked separately from a successful local build.
