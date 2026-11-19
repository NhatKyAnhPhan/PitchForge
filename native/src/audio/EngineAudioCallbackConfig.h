#pragma once

#include <atomic>
#include <cstdint>

namespace auralis::audio {

inline constexpr std::uint32_t kMaximumAudioChannels = 32;
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

namespace detail {

// Private adapter metadata needed because the frozen public AudioDeviceConfig
// has one processed-channel count while the C ABI configures input/output
// channels independently.
class InputChannelCountProvider {
public:
    virtual ~InputChannelCountProvider() = default;
    [[nodiscard]] virtual std::uint32_t requestedInputChannelCount()
        const noexcept = 0;
};

} // namespace detail
} // namespace auralis::audio
