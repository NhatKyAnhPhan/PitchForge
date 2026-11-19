#include "ProcessorInternals.h"

#include <auralis/dsp/ProcessorFactory.h>

namespace auralis::dsp {

std::unique_ptr<AudioProcessor> createProcessor(
    mixer::EffectType type,
    std::uint32_t instanceId) {
    using namespace detail;
    switch (type) {
    case mixer::EffectType::NoiseGate:
        return std::make_unique<NoiseGateProcessor>();
    case mixer::EffectType::Equalizer:
        return std::make_unique<EqualizerProcessor>();
    case mixer::EffectType::DeEsser:
        return std::make_unique<DeEsserProcessor>();
    case mixer::EffectType::Compressor:
        return std::make_unique<CompressorProcessor>();
    case mixer::EffectType::PitchCorrection:
        return std::make_unique<PitchCorrectionProcessor>(instanceId);
    case mixer::EffectType::Saturation:
        return std::make_unique<SaturationProcessor>();
    case mixer::EffectType::Reverb:
        return std::make_unique<ReverbProcessor>();
    case mixer::EffectType::Delay:
        return std::make_unique<DelayProcessor>();
    case mixer::EffectType::Limiter:
        return std::make_unique<LimiterProcessor>();
    }
    return nullptr;
}

} // namespace auralis::dsp
