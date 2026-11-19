#include "ProcessorInternals.h"
#include "ProcessorLatency.h"
#include "PitchDiagnostics.h"

namespace auralis::dsp {

std::uint32_t getKnownLatencySamples(
    const AudioProcessor& processor,
    double sampleRate) noexcept {
    const auto* implementation =
        dynamic_cast<const detail::ProcessorBase*>(&processor);
    return implementation == nullptr
        ? 0
        : implementation->knownLatencySamples(sampleRate);
}

bool getPitchCorrectionDiagnostics(
    const AudioProcessor& processor,
    PitchCorrectionDiagnostics& destination) noexcept {
    const auto* pitch =
        dynamic_cast<const detail::PitchCorrectionProcessor*>(&processor);
    if (pitch == nullptr)
        return false;
    pitch->copyDiagnostics(destination);
    return true;
}

} // namespace auralis::dsp
