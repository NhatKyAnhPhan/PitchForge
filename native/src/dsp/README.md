# DSP module

This directory builds the `auralis_dsp` target and intentionally has its own
test entry point at `tests/dsp`. The module does not modify the root build or
shared API headers. `ParameterIdsCompat.h` is only a local fallback for worktrees
that do not yet contain the shared `auralis/dsp/ParameterIds.h`; its values match
that contract. When the shared header is available it is included directly.

Processors accept and return normalized `[0, 1]` values by the stable
effect-specific IDs. `getParameter(id)` returns the normalized value that was
accepted (after clamping), so mixer state must preserve each ID/value pair
unchanged. Physical-unit mappings are as follows:

| Effect | Stable IDs and mapping |
| --- | --- |
| Equalizer | `Frequency` 20 Hz–20 kHz (log), `Gain` -18–+18 dB, `Q` 0.25–12 (log), `FilterType` Peak/low shelf/high shelf/notch, optional `HighPassFrequency` and `LowPassFrequency` (zero disables) |
| Compressor | `Threshold` -60–0 dB, `Ratio` 1:1–20:1, `Attack` 0.1–100 ms (log), `Release` 10–1000 ms (log), `Knee` 0–24 dB, `MakeupGain` 0–24 dB |
| Noise gate | `Threshold` -80–-10 dB, `Attack` 0.1–100 ms, `Release` 10–1000 ms, `Hold` 0–500 ms, `Range` 80–0 dB attenuation |
| Limiter | `Threshold` -36–0 dB, `Release` 10–1000 ms, `Ceiling` -12–0 dB |
| De-esser | `Frequency` 2–12 kHz, `Threshold` -60–-6 dB, `Ratio` 1:1–12:1, `Attack` 0.1–20 ms, `Release` 20–500 ms |
| Saturation | `Drive` 1–20x, `Tone` 1–18 kHz, `Output` -12–+12 dB |
| Reverb | `Type` Hall/room/plate/chamber, `Decay` 0.2–8 s, `PreDelay` 0–200 ms, `Size` 0.5–1.5x, `Damping` 0–1, `Width` 0–1.6x |
| Delay | `Time` 10–2000 ms (log), `Feedback` 0–0.92, `StereoOffset` 0–50% of delay time, `Tone` 0–1 |
| Pitch correction | `Key` 12 chromatic keys, `Scale` Chromatic/Major/Minor/Custom, `RetuneSpeed` 2–500 ms (log), `CorrectionAmount` 0–100%, `Humanize` 0–18 cents, `Vibrato` 0–35 cents, `Formant` neutral at 0.5, `LowNote` 45–250 Hz, `HighNote` 200–1200 Hz; custom scale note toggles use IDs 32–43 |

The separate mixer wet/dry control remains outside these processors; every
processor returns its fully processed signal. Bypass leaves the input samples
unchanged. `prepare()` sizes and clears all state; `process()` uses preallocated
storage, bounded loops, and no locks or I/O.

## Latency and pitch-shifter notes

`getKnownLatencySamples()` reports a 5 ms look-ahead for the limiter. Pitch
correction uses a fixed-frame phase vocoder: 2048 samples through 50 kHz and
4096 samples above 50 kHz. This is 42.7 ms at 48 kHz and 96 kHz (46.4 ms at
44.1 kHz). Deterministic impulse tests check the reported frame delay. The pitch
detector is YIN with cumulative-mean normalization, confidence rejection, and
octave-jump rejection. Correction maps voiced estimates to the closest enabled
scale note; humanization uses a deterministic per-instance noise source.

The phase vocoder alters samples and is functional in real time, but its
short-window spectral remapping can smear transients and is not a substitute for
device-level artifact, CPU, and latency measurements. The `Formant` control is
only spectral-tilt compensation around a neutral midpoint; true vocal formant
preservation is not implemented. Vibrato rate is fixed at 5.2 Hz because the
stable parameter contract has no vibrato-rate ID.

The shared chain snapshot has room for 16 parameter pairs per effect, while the
pitch effect exposes 21 IDs when all twelve custom-scale toggles are included.
The DSP preserves every supplied ID/value, but the current snapshot capacity
cannot serialize all pitch parameters simultaneously. That requires a shared
contract change before a complete custom-scale state can be persisted.
