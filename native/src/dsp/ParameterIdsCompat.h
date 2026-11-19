#pragma once

#if __has_include(<auralis/dsp/ParameterIds.h>)
#include <auralis/dsp/ParameterIds.h>
#else

#include <cstdint>

// Keep this private fallback in sync with the additive public contract. It is
// used only in worktrees where that not-yet-merged public header is unavailable.
namespace auralis::dsp::parameters {

enum class Equalizer : std::uint16_t {
    Frequency = 0,
    Gain = 1,
    Q = 2,
    FilterType = 3,
    HighPassFrequency = 4,
    LowPassFrequency = 5,
};

enum class Compressor : std::uint16_t {
    Threshold = 0,
    Ratio = 1,
    Attack = 2,
    Release = 3,
    Knee = 4,
    MakeupGain = 5,
};

enum class NoiseGate : std::uint16_t {
    Threshold = 0,
    Attack = 1,
    Release = 2,
    Hold = 3,
    Range = 4,
};

enum class Limiter : std::uint16_t {
    Threshold = 0,
    Release = 1,
    Ceiling = 2,
};

enum class DeEsser : std::uint16_t {
    Frequency = 0,
    Threshold = 1,
    Ratio = 2,
    Attack = 3,
    Release = 4,
};

enum class Saturation : std::uint16_t {
    Drive = 0,
    Tone = 1,
    Output = 2,
};

enum class Reverb : std::uint16_t {
    Type = 0,
    Decay = 1,
    PreDelay = 2,
    Size = 3,
    Damping = 4,
    Width = 5,
};

enum class Delay : std::uint16_t {
    Time = 0,
    Feedback = 1,
    StereoOffset = 2,
    Tone = 3,
};

enum class PitchCorrection : std::uint16_t {
    Key = 0,
    Scale = 1,
    RetuneSpeed = 2,
    CorrectionAmount = 3,
    Humanize = 4,
    Vibrato = 5,
    Formant = 6,
    LowNote = 7,
    HighNote = 8,
    CustomScaleFirstNote = 32,
    CustomScaleLastNote = 43,
};

enum class PitchScale : std::uint8_t {
    Chromatic = 0,
    Major = 1,
    Minor = 2,
    Custom = 3,
};

} // namespace auralis::dsp::parameters

#endif
