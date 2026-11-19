#pragma once

#include <cstddef>
#include <cstdint>

namespace auralis::audio {

struct ProcessSpec {
    double sampleRate = 48000.0;
    std::uint32_t maximumBlockSize = 512;
    std::uint32_t channelCount = 2;
};

// Non-owning, planar float audio block. The view is valid only for the duration
// of the current process callback.
struct AudioBlockView {
    float* const* channels = nullptr;
    std::uint32_t channelCount = 0;
    std::uint32_t sampleCount = 0;

    [[nodiscard]] bool isValid() const noexcept {
        return channels != nullptr && channelCount > 0 && sampleCount > 0;
    }
};

struct StereoMeter {
    float leftPeak = 0.0f;
    float rightPeak = 0.0f;
    float leftRms = 0.0f;
    float rightRms = 0.0f;
    bool clipping = false;
};

} // namespace auralis::audio
