#pragma once

#include <auralis/audio/AudioTypes.h>

#include <cstdint>
#include <memory>

namespace auralis::audio {

inline constexpr std::uint32_t kDefaultAudioDevice =
    static_cast<std::uint32_t>(-1);

struct AudioDeviceInfo {
    std::uint32_t index = 0;
    char name[192]{};
    std::uint32_t inputChannels = 0;
    std::uint32_t outputChannels = 0;
    bool isDefault = false;
};

struct AudioDeviceConfig {
    ProcessSpec processSpec{};
    std::uint32_t inputDeviceIndex = kDefaultAudioDevice;
    std::uint32_t outputDeviceIndex = kDefaultAudioDevice;
};

class AudioDeviceCallback {
public:
    virtual ~AudioDeviceCallback() = default;

    // Invoked on the realtime device thread. Views are valid for this call only.
    virtual void process(
        ConstAudioBlockView input,
        AudioBlockView output) noexcept = 0;
};

class AudioDeviceBackend {
public:
    virtual ~AudioDeviceBackend() = default;

    [[nodiscard]] virtual std::uint32_t deviceCount(bool forInput) const = 0;
    [[nodiscard]] virtual bool getDeviceInfo(
        bool forInput,
        std::uint32_t index,
        AudioDeviceInfo& destination) const = 0;
    [[nodiscard]] virtual bool start(
        const AudioDeviceConfig& config,
        AudioDeviceCallback& callback) = 0;
    virtual void stop() noexcept = 0;
    [[nodiscard]] virtual bool selectDevice(
        bool forInput,
        std::uint32_t index) = 0;
    [[nodiscard]] virtual bool isRunning() const noexcept = 0;
    [[nodiscard]] virtual double estimatedLatencyMs() const noexcept = 0;
};

// Exactly one platform implementation is linked into a given application.
[[nodiscard]] std::unique_ptr<AudioDeviceBackend> createPlatformAudioBackend();

} // namespace auralis::audio
