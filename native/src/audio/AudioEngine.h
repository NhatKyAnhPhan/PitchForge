#pragma once

#include <auralis/audio/AudioDeviceBackend.h>
#include <auralis/mixer/SignalChain.h>

#include "EngineAudioCallbackConfig.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace auralis::audio::detail {

static_assert(std::atomic<float>::is_always_lock_free);

class AudioEngine final
    : private AudioDeviceCallback,
      public InputChannelCountProvider {
public:
    AudioEngine(
        std::unique_ptr<AudioDeviceBackend> backend,
        std::unique_ptr<mixer::SignalChain> signalChain);
    ~AudioEngine() override;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    [[nodiscard]] bool start(
        const AudioDeviceConfig& config,
        std::uint32_t inputChannelCount);
    void stop() noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool selectDevice(bool forInput, std::uint32_t index);
    [[nodiscard]] bool isValidDevice(bool forInput, std::uint32_t index) const;
    [[nodiscard]] std::uint32_t requestedInputChannelCount()
        const noexcept override;
    [[nodiscard]] bool setInputGainDb(float gainDb) noexcept;
    [[nodiscard]] bool setOutputGainDb(float gainDb) noexcept;
    [[nodiscard]] bool setPan(float pan) noexcept;
    void setMuted(bool muted) noexcept;
    [[nodiscard]] bool copyMixerState(
        float& inputGainDb,
        float& outputGainDb,
        float& pan,
        bool& muted) const noexcept;
    [[nodiscard]] bool copyMeters(
        std::array<float, 8>& values,
        bool& clipping,
        double& estimatedLatencyMs) const noexcept;
    [[nodiscard]] bool copyWaveform(std::array<float, 128>& values) const noexcept;
    void setTestTone(bool enabled, float frequencyHz) noexcept;
    [[nodiscard]] bool copyChainSnapshot(
        mixer::SignalChainSnapshot& destination) const noexcept;
    [[nodiscard]] bool copyPitchDiagnostics(
        std::uint32_t instanceId,
        auralis::dsp::PitchCorrectionDiagnostics& destination) const noexcept;
    [[nodiscard]] bool enqueue(const mixer::ChainCommand& command) noexcept;

private:
    void process(ConstAudioBlockView input, AudioBlockView output) noexcept override;
    void publishMeters(
        ConstAudioBlockView input,
        AudioBlockView output) noexcept;
    void publishWaveform(const float* samples, std::uint32_t sampleCount) noexcept;

    std::unique_ptr<AudioDeviceBackend> backend_;
    std::unique_ptr<mixer::SignalChain> signalChain_;
    mutable std::mutex controlMutex_;
    std::mutex commandMutex_;
    std::array<std::atomic<std::uint32_t>, 8> meterValues_{};
    std::array<std::atomic<std::uint32_t>, 128> waveformValues_{};
    std::atomic<std::uint32_t> testToneEnabled_{0};
    std::atomic<float> testToneFrequencyHz_{440.0f};
    std::atomic<double> sampleRateHz_{48000.0};
    double testTonePhase_ = 0.0;
    std::atomic<std::uint32_t> clipping_{0};
    std::atomic<std::uint32_t> configuredInputChannels_{0};
    std::atomic<float> inputGainDb_{0.0f};
    std::atomic<float> outputGainDb_{0.0f};
    std::atomic<float> inputGainLinear_{1.0f};
    std::atomic<float> outputGainLinear_{1.0f};
    std::atomic<float> pan_{0.0f};
    std::atomic<std::uint32_t> muted_{0};
    std::uint32_t inputDeviceIndex_ = kDefaultAudioDevice;
    std::uint32_t outputDeviceIndex_ = kDefaultAudioDevice;
};

} // namespace auralis::audio::detail
