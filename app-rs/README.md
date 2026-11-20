# AURALIS STUDIO — PitchForge product UI (Rust/Slint)

A native Rust/Slint UI for the PitchForge audio studio, integrated over the
native engine C ABI (`include/auralis/bridge/auralis_c_api.h`).

## Run

```powershell
# Build (downloads slint + winit; builds the slint UI)
cargo build --release

# Run — the engine DLL is next to the exe (target\debug\)
Copy-Item "..\..\build-native\native\src\audio\Release\auralis_audio_engine.dll" "target\debug\"
.\target\debug\auralis-studio-rs.exe
```

The UI runs in **demo mode** when `auralis_audio_engine.dll` is missing:
meters read zero, controls are inert, but the interface is fully usable
(presets, device enumeration, and all displays work).

## Layout (design-system/auralis-studio/MASTER.md palette)

- **Top bar** — engine state badge + "Start Engine" button.
- **Devices panel** — input/output device combos (`auralis_device_*`).
- **Mixer panel** — input/output meters + latency, pan, mute.
- **Effects chain** — per-effect cards for the 9 effect types
  (Noise Gate, Equalizer, De-Esser, Compressor, Pitch Correction,
  Saturation, Reverb, Delay, Limiter) with wet/dry + parameter readouts;
  toggle with the "On/Off" button per card.
- **Pitch display** — detected/target frequency, confidence, shift from
  the PitchCorrection diagnostics (C ABI addition, see API_CONTRACTS.md).
- **Presets** — save/load JSON presets of the full chain.

## Architecture notes

- `src/ffi.rs` — runtime `LoadLibraryA`/`GetProcAddress` bindings to
  `auralis_c_api.h` (no link-time dependency on the engine DLL).
- `src/model.rs` — engine polling state (meters, chain, pitch, devices).
- `src/main.rs` — Slint two-way slider bindings, App callbacks, poll loop
  via `slint::invoke_from_event_loop`.
- `ui/main.slint` — the UI tree (generated bindings via `slint_build`).

## Environment

- Rust 1.100 stable (winget `Rustlang.Rustup`).
- Slint 1.19 with `renderer-winit-software` (pure Rust software renderer —
  no ANGLE/Skia build).
