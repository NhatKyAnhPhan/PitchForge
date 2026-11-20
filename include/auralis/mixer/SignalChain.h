#pragma once

#include <auralis/audio/AudioTypes.h>
#include <auralis/dsp/PitchDiagnostics.h>

#include <array>
#include <cstdint>

namespace auralis::mixer {

inline constexpr std::uint32_t kMaximumEffectsPerChannel = 16;

enum class EffectType : std::uint8_t {
    NoiseGate,
    Equalizer,
    DeEsser,
    Compressor,
    PitchCorrection,
    Saturation,
    Reverb,
    Delay,
    Limiter,
};

struct EffectSlot {
    EffectType type = EffectType::Equalizer;
    std::uint32_t instanceId = 0;
    bool enabled = true;
    float wet = 1.0f;
    std::uint32_t parameterCount = 0;
    std::array<std::uint16_t, 16> parameterIds{};
    std::array<float, 16> parameterValues{};
};

struct SignalChainSnapshot {
    std::array<EffectSlot, kMaximumEffectsPerChannel> slots{};
    std::uint32_t size = 0;
};

enum class ChainCommandType : std::uint8_t {
    SetEnabled,
    SetParameter,
    SetWet,
    Move,
    Add,
    Remove,
};

// Fixed-size command suitable for lock-free transfer to the audio thread.
struct ChainCommand {
    ChainCommandType type = ChainCommandType::SetEnabled;
    std::uint32_t instanceId = 0;
    std::uint16_t parameterId = 0;
    std::uint16_t index = 0;
    std::uint16_t destinationIndex = 0;
    EffectType effectType = EffectType::Equalizer;
    float value = 0.0f;
};

class SignalChain {
public:
    virtual ~SignalChain() = default;

    // Prepare all processor storage off the realtime thread before audio starts.
    virtual void prepare(const audio::ProcessSpec& spec) = 0;

    // Called from the audio callback; must be allocation-free and non-blocking.
    virtual void process(audio::AudioBlockView block) noexcept = 0;

    // Called from the control/UI thread. Returns false when the bounded command
    // queue is full; callers should retry later instead of blocking.
    [[nodiscard]] virtual bool enqueue(const ChainCommand& command) noexcept = 0;

    // Copies the most recently published state into caller-owned storage.
    virtual void copySnapshot(SignalChainSnapshot& destination) const noexcept = 0;

    // Control-thread telemetry: copies the latest pitch-correction
    // diagnostics for the chain instance into caller-owned storage.
    // Returns false when the instance is absent or is not pitch correction.
    [[nodiscard]] virtual bool copyPitchDiagnostics(
        std::uint32_t instanceId,
        auralis::dsp::PitchCorrectionDiagnostics& destination) const noexcept = 0;
};

} // namespace auralis::mixer
