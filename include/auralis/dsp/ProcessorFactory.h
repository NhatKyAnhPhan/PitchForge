#pragma once

#include <auralis/dsp/AudioProcessor.h>
#include <auralis/mixer/SignalChain.h>

#include <cstdint>
#include <memory>

namespace auralis::dsp {

// Called only on a control/setup thread. Implementations may allocate here;
// process() must never allocate. Returns null when the effect is unsupported.
[[nodiscard]] std::unique_ptr<AudioProcessor> createProcessor(
    mixer::EffectType type,
    std::uint32_t instanceId);

} // namespace auralis::dsp
