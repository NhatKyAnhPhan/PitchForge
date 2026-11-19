#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace auralis::audio::android {

struct DeviceDescriptor {
    std::int32_t id = -1;
    std::uint32_t channelCount = 0;
    bool isDefault = false;
    std::string name;
};

[[nodiscard]] bool hasRecordPermission() noexcept;
[[nodiscard]] bool requestAudioFocus() noexcept;
void abandonAudioFocus() noexcept;
[[nodiscard]] std::vector<DeviceDescriptor> deviceSnapshot(bool forInput);

void setHostState(bool isForeground, bool hasAudioFocus) noexcept;

} // namespace auralis::audio::android
