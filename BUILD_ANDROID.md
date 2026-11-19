# Android Build

## Requirements

- Flutter stable and Dart
- Android Studio / Android SDK, platform tools, NDK, and CMake
- JDK 17 or newer
- An accepted Android SDK license set

The initial machine has Flutter 3.47.3, JDK 21, SDK API 31/33/34/35/36,
NDK 28.2.13676358 and 30.0.14904198, and CMake 3.22.1 / 4.1.2.
`flutter doctor -v` reports that some Android licenses are not accepted.

## Build

```powershell
cd app
flutter pub get
flutter test
flutter build apk --debug
```

Run the APK on a physical device to validate microphone permission prompts,
audio focus, headset/USB reconnects, lifecycle handling, buffer/latency
reporting, and audible output. Bluetooth audio is not a low-latency target.

The Android native audio backend is planned around Oboe/AAudio through the NDK;
JUCE's official CMake API does not currently support Android app targets.
An APK build without a working native audio plugin is not evidence of audio
feature support.
