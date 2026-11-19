#pragma once

#include <auralis/dsp/AudioProcessor.h>

#include <cstdint>

namespace auralis::dsp {

// Returns the processor's known worst-case algorithmic delay, or zero for
// processors with no intentional look-ahead/buffering latency.
[[nodiscard]] std::uint32_t getKnownLatencySamples(
    const AudioProcessor& processor,
    double sampleRate) noexcept;

} // namespace auralis::dsp
