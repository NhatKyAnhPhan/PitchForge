#pragma once

#include <auralis/mixer/SignalChain.h>

#include <cstdint>
#include <span>
#include <vector>

namespace auralis::mixer::detail {

inline constexpr std::uint16_t kPresetFormatVersion = 1;

enum class PresetCodecStatus : std::uint8_t {
    Ok,
    InvalidState,
    Truncated,
    InvalidData,
    UnsupportedVersion,
};

// Presets are encoded as a compact, little-endian binary record. These helpers
// are control-thread-only; decode never changes destination on failure.
[[nodiscard]] PresetCodecStatus encodePreset(
    const SignalChainSnapshot& snapshot,
    std::vector<std::uint8_t>& destination);

[[nodiscard]] PresetCodecStatus decodePreset(
    std::span<const std::uint8_t> source,
    SignalChainSnapshot& destination);

} // namespace auralis::mixer::detail
