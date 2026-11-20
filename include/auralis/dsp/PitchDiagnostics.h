#pragma once

#include <auralis/dsp/AudioProcessor.h>

namespace auralis::dsp {

struct PitchCorrectionDiagnostics {
    float detectedFrequencyHz = 0.0f;
    float targetFrequencyHz = 0.0f;
    float confidence = 0.0f;
    float appliedShiftSemitones = 0.0f;
};

// Lock-free telemetry snapshot for tests and control-thread diagnostics. Returns
// false for processors other than the DSP pitch-correction implementation.
[[nodiscard]] bool getPitchCorrectionDiagnostics(
    const AudioProcessor& processor,
    PitchCorrectionDiagnostics& destination) noexcept;

} // namespace auralis::dsp
