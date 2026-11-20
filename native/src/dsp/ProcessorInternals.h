#pragma once

#include <auralis/dsp/PitchDiagnostics.h>

#include <auralis/dsp/AudioProcessor.h>

#include <array>
#include <atomic>
#include <complex>
#include <cstdint>
#include <vector>

namespace auralis::dsp::detail {

class ProcessorBase : public AudioProcessor {
public:
    explicit ProcessorBase(std::uint16_t parameterCount) noexcept;

    void reset() noexcept override;
    void setEnabled(bool enabled) noexcept override;
    [[nodiscard]] bool isEnabled() const noexcept override;
    [[nodiscard]] bool setParameter(
        std::uint16_t parameterId,
        float normalizedValue) noexcept override;
    [[nodiscard]] float getParameter(
        std::uint16_t parameterId) const noexcept override;

    [[nodiscard]] virtual std::uint32_t knownLatencySamples(
        double sampleRate) const noexcept;

protected:
    void setDefault(std::uint16_t parameterId, float normalizedValue) noexcept;
    [[nodiscard]] float parameter(std::uint16_t parameterId) const noexcept;
    [[nodiscard]] virtual bool supportsParameterId(
        std::uint16_t parameterId) const noexcept;
    void prepareChannels(const audio::ProcessSpec& spec);
    [[nodiscard]] std::uint32_t preparedChannels() const noexcept;
    [[nodiscard]] double preparedSampleRate() const noexcept;

private:
    std::array<std::atomic<float>, 48> parameters_{};
    std::atomic<bool> enabled_{true};
    std::uint16_t parameterCount_ = 0;
    std::uint32_t channelCount_ = 0;
    double sampleRate_ = 48000.0;
};

class EqualizerProcessor final : public ProcessorBase {
public:
    EqualizerProcessor() noexcept;
    void prepare(const audio::ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(audio::AudioBlockView block) noexcept override;

private:
    struct BiquadCoefficients {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
    };
    struct BiquadState {
        float z1 = 0.0f;
        float z2 = 0.0f;
    };

    [[nodiscard]] BiquadCoefficients makePeak(
        float frequency,
        float gainDb,
        float q) const noexcept;
    [[nodiscard]] BiquadCoefficients makeFilter(
        float frequency,
        float q,
        std::uint16_t filterType) const noexcept;

    std::array<BiquadCoefficients, 3> current_{};
    std::array<BiquadCoefficients, 3> target_{};
    std::vector<std::array<BiquadState, 3>> states_;
    float smoothing_ = 1.0f;
    std::uint16_t currentFilterType_ = 0;
};

class CompressorProcessor final : public ProcessorBase {
public:
    CompressorProcessor() noexcept;
    void prepare(const audio::ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(audio::AudioBlockView block) noexcept override;

private:
    float envelope_ = 0.0f;
    float gain_ = 1.0f;
    float attackCoefficient_ = 0.0f;
    float releaseCoefficient_ = 0.0f;
    float gainAttackCoefficient_ = 0.0f;
    float gainReleaseCoefficient_ = 0.0f;
};

class NoiseGateProcessor final : public ProcessorBase {
public:
    NoiseGateProcessor() noexcept;
    void prepare(const audio::ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(audio::AudioBlockView block) noexcept override;

private:
    float envelope_ = 0.0f;
    float gain_ = 1.0f;
    bool open_ = true;
    std::uint64_t holdRemaining_ = 0;
};

class LimiterProcessor final : public ProcessorBase {
public:
    LimiterProcessor() noexcept;
    void prepare(const audio::ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(audio::AudioBlockView block) noexcept override;
    [[nodiscard]] std::uint32_t knownLatencySamples(
        double sampleRate) const noexcept override;

private:
    std::vector<std::vector<float>> delay_;
    std::vector<std::uint64_t> dequeIndices_;
    std::vector<float> dequeValues_;
    std::uint64_t sampleIndex_ = 0;
    std::size_t delayWrite_ = 0;
    std::size_t dequeHead_ = 0;
    std::size_t dequeSize_ = 0;
    std::size_t lookAheadSamples_ = 0;
    float gain_ = 1.0f;
    float releaseCoefficient_ = 0.0f;
    float thresholdDb_ = -12.0f;
    float ceilingLinear_ = 1.0f;
    double sampleRate_ = 48000.0;
};

class DelayProcessor final : public ProcessorBase {
public:
    DelayProcessor() noexcept;
    void prepare(const audio::ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(audio::AudioBlockView block) noexcept override;

private:
    std::vector<std::vector<float>> buffers_;
    std::vector<float> delayedScratch_;
    std::vector<float> toneState_;
    std::size_t writeIndex_ = 0;
    double currentDelaySamples_ = 1.0;
    float currentFeedback_ = 0.0f;
    float currentCrossfeed_ = 0.0f;
    float currentDamping_ = 0.0f;
    double delaySmoothing_ = 1.0;
    float parameterSmoothing_ = 1.0f;
};

class ReverbProcessor final : public ProcessorBase {
public:
    ReverbProcessor() noexcept;
    void prepare(const audio::ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(audio::AudioBlockView block) noexcept override;

private:
    struct Comb {
        std::vector<float> buffer;
        std::size_t index = 0;
        float dampingState = 0.0f;
    };
    struct Allpass {
        std::vector<float> buffer;
        std::size_t index = 0;
    };
    struct ChannelState {
        std::array<Comb, 4> combs;
        std::array<Allpass, 2> allpasses;
        std::vector<float> preDelay;
        std::size_t preDelayIndex = 0;
    };

    std::vector<ChannelState> channels_;
    std::vector<float> wetScratch_;
    double sampleRate_ = 48000.0;
    double currentPreDelaySamples_ = 0.0;
    double preDelaySmoothing_ = 1.0;
    float currentDecaySeconds_ = 2.4f;
    float currentDamping_ = 0.4f;
    float currentSize_ = 0.7f;
    float currentWidth_ = 1.0f;
    std::uint16_t currentType_ = 0;
    float parameterSmoothing_ = 1.0f;
};

class SaturationProcessor final : public ProcessorBase {
public:
    SaturationProcessor() noexcept;
    void prepare(const audio::ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(audio::AudioBlockView block) noexcept override;

private:
    std::vector<float> previousInput_;
    std::vector<float> toneState_;
    float currentDrive_ = 1.0f;
    float driveSmoothing_ = 1.0f;
    float currentTone_ = 1.0f;
    float currentOutputGain_ = 1.0f;
    float toneSmoothing_ = 1.0f;
};

class DeEsserProcessor final : public ProcessorBase {
public:
    DeEsserProcessor() noexcept;
    void prepare(const audio::ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(audio::AudioBlockView block) noexcept override;

private:
    std::vector<float> lowState_;
    std::vector<float> highScratch_;
    float detectorEnvelope_ = 0.0f;
    float attackCoefficient_ = 0.0f;
    float releaseCoefficient_ = 0.0f;
    float currentCutoff_ = 6500.0f;
    float cutoffSmoothing_ = 1.0f;
    float ratio_ = 4.0f;
};

class PitchCorrectionProcessor final : public ProcessorBase {
public:
    explicit PitchCorrectionProcessor(std::uint32_t instanceId) noexcept;
    void prepare(const audio::ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(audio::AudioBlockView block) noexcept override;
    [[nodiscard]] std::uint32_t knownLatencySamples(
        double sampleRate) const noexcept override;
    void copyDiagnostics(
        PitchCorrectionDiagnostics& destination) const noexcept;

private:
    static constexpr std::size_t kMaximumFftSize = 4096;
    static constexpr std::size_t kDownsampledWindowSize = 512;
    static constexpr std::size_t kMaximumBinCount =
        kMaximumFftSize / 2 + 1;
    struct VocoderChannel {
        std::array<std::complex<float>, kMaximumFftSize> spectrum{};
        std::array<float, kMaximumBinCount> previousPhase{};
        std::array<float, kMaximumBinCount> synthesisPhase{};
        std::array<float, kMaximumFftSize> input{};
        std::array<float, kMaximumFftSize * 2> output{};
        bool hasPreviousFrame = false;
    };

    [[nodiscard]] bool supportsParameterId(
        std::uint16_t parameterId) const noexcept override;
    void analyzePitch() noexcept;
    void processVocoderFrame(
        std::uint32_t channel,
        double pitchRatio) noexcept;
    void transform(VocoderChannel& channel, bool inverse) noexcept;
    [[nodiscard]] float nearestScaleNote(float frequency) const noexcept;
    [[nodiscard]] float randomUnit() noexcept;

    std::array<float, kMaximumFftSize> pitchWindow_{};
    std::array<float, kDownsampledWindowSize> yinWindow_{};
    std::array<float, kDownsampledWindowSize / 2> differences_{};
    std::array<float, kMaximumFftSize> analysisWindow_{};
    std::array<std::complex<float>, kMaximumFftSize / 2> twiddles_{};
    std::array<std::uint16_t, kMaximumFftSize> bitReversed_{};
    std::array<float, kMaximumBinCount> shiftedMagnitude_{};
    std::array<float, kMaximumBinCount> shiftedFrequency_{};
    std::array<float, kMaximumBinCount> shiftedWeight_{};
    std::array<float, kMaximumBinCount> shiftedInitialPhase_{};
    std::vector<VocoderChannel> vocoderChannels_;
    std::vector<float> formantLowState_;
    std::size_t fftSize_ = 2048;
    std::size_t hopSize_ = 512;
    std::size_t downsampleFactor_ = 4;
    std::size_t inputWriteIndex_ = 0;
    std::size_t outputWriteIndex_ = 0;
    std::size_t samplesFilled_ = 0;
    std::size_t samplesSinceFrame_ = 0;
    std::size_t samplesUntilAnalysis_ = 2048;
    double sampleRate_ = 48000.0;
    double detectedFrequency_ = 0.0;
    double targetFrequency_ = 0.0;
    double currentSemitones_ = 0.0;
    double vibratoPhase_ = 0.0;
    double humanizeCounter_ = 0.0;
    float confidence_ = 0.0f;
    float humanizeOffset_ = 0.0f;
    float currentCorrectionAmount_ = 0.0f;
    float retuneCoefficient_ = 1.0f;
    std::atomic<float> detectedFrequencyTelemetry_{0.0f};
    std::atomic<float> targetFrequencyTelemetry_{0.0f};
    std::atomic<float> confidenceTelemetry_{0.0f};
    std::atomic<float> shiftSemitonesTelemetry_{0.0f};
    std::uint32_t randomState_ = 1;
};

} // namespace auralis::dsp::detail
