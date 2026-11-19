# DSP Design

## Runtime contract

DSP processors implement `auralis::dsp::AudioProcessor`. Audio data is
non-owning planar `float` audio through `auralis::audio::AudioBlockView`.
`prepare()` runs off the realtime thread; `process()` must be bounded,
allocation-free, non-blocking, and `noexcept`.

Effect instances and state must be prepared before publication to the signal
chain. UI updates are validated and transferred as fixed-size commands. Preset
serialization and file I/O happen only on a non-realtime thread.

## Initial processing order

```text
Input -> Noise Gate -> EQ -> De-esser -> Compressor -> Pitch Correction
      -> Saturation -> Reverb/Delay -> Limiter -> Output
```

The order is user-editable within a fixed maximum chain length. Each module
must have an explicit bypass and wet/dry policy; effects that are not yet
implemented must not be represented as active DSP.

## Planned algorithms

- **Pitch detection:** YIN difference function with cumulative-mean normalized
  difference and confidence/voicing threshold; track period over successive
  blocks and reject octave jumps. Validate against synthetic tones, vibrato,
  noise, and unvoiced consonants.
- **Pitch correction:** map voiced pitch to the nearest enabled note in the
  chosen scale; smooth target changes according to retune time and correction
  amount. The pitch-shifting implementation must be measured for algorithmic
  latency and artifacts before being advertised as realtime Auto-Tune.
- **EQ:** cascaded second-order biquads (RBJ cookbook), with coefficient updates
  smoothed rather than stepped.
- **Compressor/limiter/gate:** envelope followers with explicit attack/release,
  ratio/threshold/hysteresis, bounded gain reduction, and denormal protection.
- **Reverb/delay:** preallocated delay lines and feedback buffers sized in
  `prepare()`, with parameter interpolation and bounded feedback.

## Validation

Measure magnitude response, gain reduction, decay time, pitch error, latency,
clipping, and stability at multiple sample rates and block sizes. Include NaN,
infinity, silence, impulse, overload, and parameter-extreme cases.
