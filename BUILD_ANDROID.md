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

Android uses the pinned Oboe 1.9.3 Prefab dependency and the NDK CMake target
`auralis_audio_engine` (`libauralis_audio_engine.so`). Its CMake links
`auralis_core`; the common engine remains the sole owner of the versioned C ABI
used by Dart FFI. Do not add a second Android-specific C ABI or pass audio
samples through a Flutter channel.

Before starting the C ABI engine, Dart should invoke the lifecycle channel:

```dart
const audioLifecycle = MethodChannel('com.auralis.audio/lifecycle');
final permission = await audioLifecycle
    .invokeMapMethod<String, Object?>('requestMicrophonePermission');
if (permission?['status'] != 'granted') {
  // Keep the engine stopped and explain how to grant microphone access.
  return;
}
final focus = await audioLifecycle
    .invokeMapMethod<String, Object?>('requestAudioFocus');
if (focus?['status'] != 'granted') {
  // Keep the engine stopped until audio focus can be obtained.
  return;
}
// Call the engine start/stop/device/meter/effect functions through dart:ffi.
// After stopping the engine, invoke 'abandonAudioFocus'.
```

The channel methods are `requestMicrophonePermission`, `requestAudioFocus`, and
`abandonAudioFocus`. Each returns a map with a `status` string. Permission
statuses are `granted`, `denied`, `request_in_progress`, `activity_destroyed`,
and `unavailable`; focus statuses are `granted` and `denied`; focus release
returns `abandoned`. Native engine start independently checks
the permission and acquires audio focus, so it fails closed if Dart skipped
this UI boundary.

The Oboe backend opens separate low-latency input/output streams and adapts
interleaved device buffers to the shared planar-float callback. It enumerates
Android routes through `AudioManager`, uses the default route sentinel when
requested, and retries after stream disconnects (including USB route changes).
Pausing the Activity or losing audio focus suspends the streams; returning to
the foreground/focus resumes a requested session. The input/output stream
buffer sizes and current software FIFO occupancy provide only a rough
round-trip latency estimate; this excludes processing, scheduling and other
system delays.

## Device smoke test (after common engine/C ABI integration)

1. Install/run the debug APK on a physical Android device and verify that
   microphone use remains off until the Android permission prompt is granted.
2. Start the engine through the C ABI. Check that input/output device
   enumeration, meters, and audible processing work; do not use a loudspeaker
   at high gain because live microphone monitoring can feed back.
3. Connect and remove wired headsets and USB audio devices while running;
   confirm the route list refreshes and the stream recovers. Background and
   resume the app, and test audio-focus loss/reacquisition.
4. Record device model/API level, selected route, sample rate, buffer size,
   reported latency and audible glitches. Repeat after cold launch.

Bluetooth audio is explicitly not a low-latency target: Android may route it
through high-latency codecs/profiles, and supported formats, channel counts,
sample rates, buffer sizes, focus behavior and reconnect timing vary by device,
OS build and accessory. A successful CMake/APK build proves packaging only; it
does not establish low latency or correct behavior on hardware.

The current contract baseline has a headers-only `auralis_core` target. The
Android target is wired to that shared target and intentionally does not
implement duplicate C ABI symbols; until the common engine/C ABI sources are
integrated into `auralis_core`, the packaged backend alone is not FFI-ready.
