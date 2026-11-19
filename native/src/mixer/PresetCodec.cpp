#include "PresetCodec.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace auralis::mixer::detail {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'A', 'U', 'R', 'P'};
constexpr std::size_t kHeaderSize = 7;
constexpr std::size_t kFixedSlotSize = 11;
constexpr std::size_t kParameterSize = 6;

[[nodiscard]] bool validType(EffectType type) noexcept {
    return static_cast<std::uint8_t>(type) <=
        static_cast<std::uint8_t>(EffectType::Limiter);
}

[[nodiscard]] bool validNormalized(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(
            static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void appendFloat(std::vector<std::uint8_t>& bytes, float value) {
    appendU32(bytes, std::bit_cast<std::uint32_t>(value));
}

class Reader final {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool readU8(std::uint8_t& value) noexcept {
        if (offset_ >= bytes_.size()) {
            return false;
        }
        value = bytes_[offset_++];
        return true;
    }

    [[nodiscard]] bool readU16(std::uint16_t& value) noexcept {
        std::uint8_t low = 0;
        std::uint8_t high = 0;
        if (!readU8(low) || !readU8(high)) {
            return false;
        }
        value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(low)
            | (static_cast<std::uint16_t>(high) << 8));
        return true;
    }

    [[nodiscard]] bool readU32(std::uint32_t& value) noexcept {
        value = 0;
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            std::uint8_t byte = 0;
            if (!readU8(byte)) {
                return false;
            }
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }

    [[nodiscard]] bool readFloat(float& value) noexcept {
        std::uint32_t bits = 0;
        if (!readU32(bits)) {
            return false;
        }
        value = std::bit_cast<float>(bits);
        return true;
    }

    [[nodiscard]] bool exhausted() const noexcept {
        return offset_ == bytes_.size();
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

[[nodiscard]] PresetCodecStatus validate(
    const SignalChainSnapshot& snapshot) noexcept {
    if (snapshot.size > kMaximumEffectsPerChannel) {
        return PresetCodecStatus::InvalidState;
    }

    for (std::uint32_t i = 0; i < snapshot.size; ++i) {
        const auto& slot = snapshot.slots[i];
        if (!validType(slot.type) || slot.instanceId == 0
            || !validNormalized(slot.wet)
            || slot.parameterCount > slot.parameterIds.size()) {
            return PresetCodecStatus::InvalidState;
        }
        for (std::uint32_t prior = 0; prior < i; ++prior) {
            if (snapshot.slots[prior].instanceId == slot.instanceId) {
                return PresetCodecStatus::InvalidState;
            }
        }
        for (std::uint32_t parameter = 0;
             parameter < slot.parameterCount;
             ++parameter) {
            if (!validNormalized(slot.parameterValues[parameter])) {
                return PresetCodecStatus::InvalidState;
            }
            for (std::uint32_t prior = 0; prior < parameter; ++prior) {
                if (slot.parameterIds[prior] == slot.parameterIds[parameter]) {
                    return PresetCodecStatus::InvalidState;
                }
            }
        }
    }
    return PresetCodecStatus::Ok;
}

} // namespace

PresetCodecStatus encodePreset(
    const SignalChainSnapshot& snapshot,
    std::vector<std::uint8_t>& destination) {
    const auto validation = validate(snapshot);
    if (validation != PresetCodecStatus::Ok) {
        return validation;
    }

    std::vector<std::uint8_t> encoded;
    std::size_t requiredSize = kHeaderSize;
    for (std::uint32_t i = 0; i < snapshot.size; ++i) {
        const std::size_t parameterCount =
            snapshot.slots[i].parameterCount;
        if (parameterCount
            > (std::numeric_limits<std::size_t>::max() - requiredSize
                - kFixedSlotSize) / kParameterSize) {
            return PresetCodecStatus::InvalidState;
        }
        requiredSize += kFixedSlotSize + parameterCount * kParameterSize;
    }
    encoded.reserve(requiredSize);
    encoded.insert(encoded.end(), kMagic.begin(), kMagic.end());
    appendU16(encoded, kPresetFormatVersion);
    encoded.push_back(static_cast<std::uint8_t>(snapshot.size));

    for (std::uint32_t i = 0; i < snapshot.size; ++i) {
        const auto& slot = snapshot.slots[i];
        encoded.push_back(static_cast<std::uint8_t>(slot.type));
        appendU32(encoded, slot.instanceId);
        encoded.push_back(slot.enabled ? 1u : 0u);
        appendFloat(encoded, slot.wet);
        encoded.push_back(static_cast<std::uint8_t>(slot.parameterCount));
        for (std::uint32_t parameter = 0;
             parameter < slot.parameterCount;
             ++parameter) {
            appendU16(encoded, slot.parameterIds[parameter]);
            appendFloat(encoded, slot.parameterValues[parameter]);
        }
    }

    destination = std::move(encoded);
    return PresetCodecStatus::Ok;
}

PresetCodecStatus decodePreset(
    std::span<const std::uint8_t> source,
    SignalChainSnapshot& destination) {
    if (source.size() < kHeaderSize) {
        return PresetCodecStatus::Truncated;
    }
    for (std::size_t i = 0; i < kMagic.size(); ++i) {
        if (source[i] != kMagic[i]) {
            return PresetCodecStatus::InvalidData;
        }
    }

    Reader reader(source.subspan(kMagic.size()));
    std::uint16_t version = 0;
    std::uint8_t count = 0;
    if (!reader.readU16(version) || !reader.readU8(count)) {
        return PresetCodecStatus::Truncated;
    }
    if (version != kPresetFormatVersion) {
        return PresetCodecStatus::UnsupportedVersion;
    }
    if (count > kMaximumEffectsPerChannel) {
        return PresetCodecStatus::InvalidData;
    }

    SignalChainSnapshot decoded{};
    decoded.size = count;
    for (std::uint32_t i = 0; i < decoded.size; ++i) {
        auto& slot = decoded.slots[i];
        std::uint8_t type = 0;
        std::uint8_t enabled = 0;
        std::uint8_t parameterCount = 0;
        if (!reader.readU8(type)
            || !reader.readU32(slot.instanceId)
            || !reader.readU8(enabled)
            || !reader.readFloat(slot.wet)
            || !reader.readU8(parameterCount)) {
            return PresetCodecStatus::Truncated;
        }
        slot.type = static_cast<EffectType>(type);
        if (enabled > 1 || parameterCount > slot.parameterIds.size()) {
            return PresetCodecStatus::InvalidData;
        }
        slot.enabled = enabled != 0;
        slot.parameterCount = parameterCount;
        for (std::uint32_t parameter = 0;
             parameter < slot.parameterCount;
             ++parameter) {
            if (!reader.readU16(slot.parameterIds[parameter])
                || !reader.readFloat(slot.parameterValues[parameter])) {
                return PresetCodecStatus::Truncated;
            }
        }
    }
    if (!reader.exhausted()) {
        return PresetCodecStatus::InvalidData;
    }

    const auto validation = validate(decoded);
    if (validation != PresetCodecStatus::Ok) {
        return PresetCodecStatus::InvalidData;
    }
    destination = decoded;
    return PresetCodecStatus::Ok;
}

} // namespace auralis::mixer::detail
