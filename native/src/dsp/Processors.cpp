#include "ParameterIdsCompat.h"
#include "ProcessorInternals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <utility>

namespace auralis::dsp::detail {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kSilenceFloor = 1.0e-20f;
constexpr float kMaximumInput = 32.0f;

static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

[[nodiscard]] float clamp01(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

[[nodiscard]] float cleanSample(float value) noexcept {
    if (!std::isfinite(value) || std::abs(value) < kSilenceFloor)
        return 0.0f;
    return std::clamp(value, -kMaximumInput, kMaximumInput);
}

[[nodiscard]] double safeSampleRate(double sampleRate) noexcept {
    return std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 8000.0, 192000.0)
            : 48000.0;
}

[[nodiscard]] float logMap(
    float normalized,
    float minimum,
    float maximum) noexcept {
    const float value = clamp01(normalized);
    return minimum * std::pow(maximum / minimum, value);
}

[[nodiscard]] float millisCoefficient(float milliseconds, double sampleRate)
    noexcept {
    const double seconds = std::max(0.0001, milliseconds * 0.001);
    return static_cast<float>(std::exp(-1.0 / (seconds * sampleRate)));
}

[[nodiscard]] float dbToLinear(float decibels) noexcept {
    return std::pow(10.0f, decibels / 20.0f);
}

[[nodiscard]] float linearToDb(float value) noexcept {
    return 20.0f * std::log10(std::max(value, 1.0e-8f));
}

} // namespace

ProcessorBase::ProcessorBase(std::uint16_t parameterCount) noexcept
    : parameterCount_(std::min<std::uint16_t>(
          parameterCount,
          static_cast<std::uint16_t>(parameters_.size()))) {
    for (auto& value : parameters_)
        value.store(0.5f, std::memory_order_relaxed);
}

void ProcessorBase::reset() noexcept {}

void ProcessorBase::setEnabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_release);
}

bool ProcessorBase::isEnabled() const noexcept {
    return enabled_.load(std::memory_order_acquire);
}

bool ProcessorBase::setParameter(
    std::uint16_t id,
    float normalizedValue) noexcept {
    if (!supportsParameterId(id) || !std::isfinite(normalizedValue))
        return false;
    parameters_[id].store(clamp01(normalizedValue), std::memory_order_release);
    return true;
}

float ProcessorBase::getParameter(std::uint16_t id) const noexcept {
    if (!supportsParameterId(id))
        return 0.0f;
    return parameters_[id].load(std::memory_order_acquire);
}

std::uint32_t ProcessorBase::knownLatencySamples(double) const noexcept {
    return 0;
}

void ProcessorBase::setDefault(
    std::uint16_t id,
    float normalizedValue) noexcept {
    if (id < parameters_.size())
        parameters_[id].store(clamp01(normalizedValue), std::memory_order_relaxed);
}

float ProcessorBase::parameter(std::uint16_t id) const noexcept {
    return id < parameters_.size()
            ? parameters_[id].load(std::memory_order_relaxed)
            : 0.0f;
}

bool ProcessorBase::supportsParameterId(std::uint16_t id) const noexcept {
    return id < parameterCount_;
}

void ProcessorBase::prepareChannels(const audio::ProcessSpec& spec) {
    channelCount_ = std::clamp<std::uint32_t>(spec.channelCount, 1, 64);
    sampleRate_ = safeSampleRate(spec.sampleRate);
}

std::uint32_t ProcessorBase::preparedChannels() const noexcept {
    return channelCount_;
}

double ProcessorBase::preparedSampleRate() const noexcept {
    return sampleRate_;
}

EqualizerProcessor::EqualizerProcessor() noexcept : ProcessorBase(6) {
    using Id = parameters::Equalizer;
    setDefault(static_cast<std::uint16_t>(Id::Frequency), 0.526f);
    setDefault(static_cast<std::uint16_t>(Id::Gain), 0.5f);
    setDefault(static_cast<std::uint16_t>(Id::Q), 0.22f);
    setDefault(static_cast<std::uint16_t>(Id::FilterType), 0.0f);
    setDefault(static_cast<std::uint16_t>(Id::HighPassFrequency), 0.0f);
    setDefault(static_cast<std::uint16_t>(Id::LowPassFrequency), 0.0f);
}

void EqualizerProcessor::prepare(const audio::ProcessSpec& spec) {
    prepareChannels(spec);
    states_.assign(preparedChannels(), {});
    smoothing_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.01 * preparedSampleRate())));
    for (auto& coefficients : current_)
        coefficients = {};
    target_ = current_;
    reset();
}

void EqualizerProcessor::reset() noexcept {
    for (auto& channel : states_)
        channel = {};
}

EqualizerProcessor::BiquadCoefficients EqualizerProcessor::makePeak(
    float frequency,
    float gainDb,
    float q) const noexcept {
    const double rate = preparedSampleRate();
    const double f = std::clamp(
        static_cast<double>(frequency),
        10.0,
        std::max(11.0, rate * 0.45));
    const double quality = std::clamp(static_cast<double>(q), 0.25, 12.0);
    const double amplitude = std::pow(10.0, static_cast<double>(gainDb) / 40.0);
    const double omega = 2.0 * std::numbers::pi * f / rate;
    const double cosine = std::cos(omega);
    const double alpha = std::sin(omega) / (2.0 * quality);
    double b0 = 1.0 + alpha * amplitude;
    double b1 = -2.0 * cosine;
    double b2 = 1.0 - alpha * amplitude;
    double a0 = 1.0 + alpha / amplitude;
    double a1 = -2.0 * cosine;
    double a2 = 1.0 - alpha / amplitude;

    if (currentFilterType_ == 1 || currentFilterType_ == 2) {
        const double rootA = std::sqrt(amplitude);
        const double twiceRootAlpha = 2.0 * rootA * alpha;
        if (currentFilterType_ == 1) {
            b0 = amplitude * ((amplitude + 1.0)
                - (amplitude - 1.0) * cosine + twiceRootAlpha);
            b1 = 2.0 * amplitude
                * ((amplitude - 1.0) - (amplitude + 1.0) * cosine);
            b2 = amplitude * ((amplitude + 1.0)
                - (amplitude - 1.0) * cosine - twiceRootAlpha);
            a0 = (amplitude + 1.0)
                + (amplitude - 1.0) * cosine + twiceRootAlpha;
            a1 = -2.0 * ((amplitude - 1.0)
                + (amplitude + 1.0) * cosine);
            a2 = (amplitude + 1.0)
                + (amplitude - 1.0) * cosine - twiceRootAlpha;
        } else {
            b0 = amplitude * ((amplitude + 1.0)
                + (amplitude - 1.0) * cosine + twiceRootAlpha);
            b1 = -2.0 * amplitude
                * ((amplitude - 1.0) + (amplitude + 1.0) * cosine);
            b2 = amplitude * ((amplitude + 1.0)
                + (amplitude - 1.0) * cosine - twiceRootAlpha);
            a0 = (amplitude + 1.0)
                - (amplitude - 1.0) * cosine + twiceRootAlpha;
            a1 = 2.0 * ((amplitude - 1.0)
                - (amplitude + 1.0) * cosine);
            a2 = (amplitude + 1.0)
                - (amplitude - 1.0) * cosine - twiceRootAlpha;
        }
    } else if (currentFilterType_ == 3) {
        b0 = 1.0;
        b1 = -2.0 * cosine;
        b2 = 1.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosine;
        a2 = 1.0 - alpha;
    }
    return {
        static_cast<float>(b0 / a0),
        static_cast<float>(b1 / a0),
        static_cast<float>(b2 / a0),
        static_cast<float>(a1 / a0),
        static_cast<float>(a2 / a0),
    };
}

EqualizerProcessor::BiquadCoefficients EqualizerProcessor::makeFilter(
    float frequency,
    float q,
    std::uint16_t filterType) const noexcept {
    const double rate = preparedSampleRate();
    const double f = std::clamp(
        static_cast<double>(frequency),
        10.0,
        std::max(11.0, rate * 0.45));
    const double omega = 2.0 * std::numbers::pi * f / rate;
    const double cosine = std::cos(omega);
    const double alpha = std::sin(omega)
        / (2.0 * std::clamp(static_cast<double>(q), 0.25, 12.0));
    const double a0 = 1.0 + alpha;
    const bool highPass = filterType == 1;

    return {
        static_cast<float>((highPass ? (1.0 + cosine) : (1.0 - cosine))
            / (2.0 * a0)),
        static_cast<float>((highPass ? -(1.0 + cosine) : (1.0 - cosine))
            / a0),
        static_cast<float>((highPass ? (1.0 + cosine) : (1.0 - cosine))
            / (2.0 * a0)),
        static_cast<float>((-2.0 * cosine) / a0),
        static_cast<float>((1.0 - alpha) / a0),
    };
}

void EqualizerProcessor::process(audio::AudioBlockView block) noexcept {
    if (!isEnabled() || !block.isValid() || states_.empty())
        return;
    using Id = parameters::Equalizer;

    const float frequency = logMap(
        parameter(static_cast<std::uint16_t>(Id::Frequency)),
        20.0f,
        20000.0f);
    const float gainDb = (parameter(static_cast<std::uint16_t>(Id::Gain)) - 0.5f)
        * 36.0f;
    const float q = logMap(
        parameter(static_cast<std::uint16_t>(Id::Q)), 0.25f, 12.0f);
    const auto filterType = static_cast<std::uint16_t>(std::lround(
        parameter(static_cast<std::uint16_t>(Id::FilterType)) * 3.0f));
    const float highPass = parameter(
        static_cast<std::uint16_t>(Id::HighPassFrequency));
    const float lowPass = parameter(
        static_cast<std::uint16_t>(Id::LowPassFrequency));
    const float maximumFrequency = static_cast<float>(
        std::min(20000.0, preparedSampleRate() * 0.45));
    const float highPassHz = logMap(highPass, 20.0f, maximumFrequency);
    const float lowPassHz = logMap(lowPass, 20.0f, maximumFrequency);

    currentFilterType_ = filterType;
    target_[0] = makePeak(frequency, gainDb, q);
    if (highPass > 0.001f)
        target_[1] = makeFilter(highPassHz, 0.7071f, 1);
    else
        target_[1] = {};
    if (lowPass > 0.001f)
        target_[2] = makeFilter(lowPassHz, 0.7071f, 0);
    else
        target_[2] = {};

    const std::uint32_t channels = std::min<std::uint32_t>(
        block.channelCount,
        preparedChannels());
    for (std::uint32_t sample = 0; sample < block.sampleCount; ++sample) {
        for (std::size_t band = 0; band < current_.size(); ++band) {
            auto& current = current_[band];
            const auto& target = target_[band];
            current.b0 += smoothing_ * (target.b0 - current.b0);
            current.b1 += smoothing_ * (target.b1 - current.b1);
            current.b2 += smoothing_ * (target.b2 - current.b2);
            current.a1 += smoothing_ * (target.a1 - current.a1);
            current.a2 += smoothing_ * (target.a2 - current.a2);
        }

        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            if (block.channels[channel] == nullptr)
                continue;
            float value = cleanSample(block.channels[channel][sample]);
            auto& channelStates = states_[channel];
            for (std::size_t band = 0; band < current_.size(); ++band) {
                const auto& coefficients = current_[band];
                auto& state = channelStates[band];
                const float output = coefficients.b0 * value + state.z1;
                state.z1 = coefficients.b1 * value - coefficients.a1 * output
                    + state.z2;
                state.z2 = coefficients.b2 * value - coefficients.a2 * output;
                value = std::isfinite(output) ? output : 0.0f;
            }
            block.channels[channel][sample] = cleanSample(value);
        }
    }
}

CompressorProcessor::CompressorProcessor() noexcept : ProcessorBase(6) {
    using Id = parameters::Compressor;
    setDefault(static_cast<std::uint16_t>(Id::Threshold), 0.7f);
    setDefault(static_cast<std::uint16_t>(Id::Ratio), 0.18f);
    setDefault(static_cast<std::uint16_t>(Id::Attack), 0.15f);
    setDefault(static_cast<std::uint16_t>(Id::Release), 0.25f);
    setDefault(static_cast<std::uint16_t>(Id::Knee), 0.25f);
    setDefault(static_cast<std::uint16_t>(Id::MakeupGain), 0.0f);
}

void CompressorProcessor::prepare(const audio::ProcessSpec& spec) {
    prepareChannels(spec);
    reset();
}

void CompressorProcessor::reset() noexcept {
    envelope_ = 0.0f;
    gain_ = 1.0f;
}

void CompressorProcessor::process(audio::AudioBlockView block) noexcept {
    if (!isEnabled() || !block.isValid())
        return;
    using Id = parameters::Compressor;
    const double rate = preparedSampleRate();
    const float thresholdDb = -60.0f
        + parameter(static_cast<std::uint16_t>(Id::Threshold)) * 60.0f;
    const float ratio = 1.0f
        + parameter(static_cast<std::uint16_t>(Id::Ratio)) * 19.0f;
    const float attackMs = logMap(
        parameter(static_cast<std::uint16_t>(Id::Attack)), 0.1f, 100.0f);
    const float releaseMs = logMap(
        parameter(static_cast<std::uint16_t>(Id::Release)), 10.0f, 1000.0f);
    const float kneeDb = parameter(static_cast<std::uint16_t>(Id::Knee)) * 24.0f;
    const float makeupDb = parameter(
        static_cast<std::uint16_t>(Id::MakeupGain)) * 24.0f;
    attackCoefficient_ = millisCoefficient(attackMs, rate);
    releaseCoefficient_ = millisCoefficient(releaseMs, rate);
    gainAttackCoefficient_ = millisCoefficient(attackMs, rate);
    gainReleaseCoefficient_ = millisCoefficient(releaseMs, rate);

    const std::uint32_t channels = std::min(block.channelCount, preparedChannels());
    for (std::uint32_t sample = 0; sample < block.sampleCount; ++sample) {
        float peak = 0.0f;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            if (block.channels[channel] != nullptr)
                peak = std::max(
                    peak,
                    std::abs(cleanSample(block.channels[channel][sample])));
        }

        const float detectorCoefficient = peak > envelope_
            ? attackCoefficient_
            : releaseCoefficient_;
        envelope_ = peak + detectorCoefficient * (envelope_ - peak);
        const float levelDb = linearToDb(envelope_);
        const float overDb = levelDb - thresholdDb;
        float reductionDb = 0.0f;
        const float halfKnee = kneeDb * 0.5f;
        if (kneeDb > 0.0f && overDb > -halfKnee && overDb < halfKnee) {
            const float distance = overDb + halfKnee;
            reductionDb = (1.0f / ratio - 1.0f)
                * distance * distance / (2.0f * kneeDb);
        } else if (overDb >= halfKnee) {
            reductionDb = (1.0f / ratio - 1.0f) * overDb;
        }
        const float desiredGain = dbToLinear(reductionDb + makeupDb);
        const float gainCoefficient = desiredGain < gain_
            ? gainAttackCoefficient_
            : gainReleaseCoefficient_;
        gain_ = desiredGain + gainCoefficient * (gain_ - desiredGain);

        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            if (block.channels[channel] != nullptr)
                block.channels[channel][sample] = cleanSample(
                    cleanSample(block.channels[channel][sample]) * gain_);
        }
    }
}

NoiseGateProcessor::NoiseGateProcessor() noexcept : ProcessorBase(5) {
    using Id = parameters::NoiseGate;
    setDefault(static_cast<std::uint16_t>(Id::Threshold), 0.3f);
    setDefault(static_cast<std::uint16_t>(Id::Attack), 0.1f);
    setDefault(static_cast<std::uint16_t>(Id::Release), 0.2f);
    setDefault(static_cast<std::uint16_t>(Id::Hold), 0.12f);
    setDefault(static_cast<std::uint16_t>(Id::Range), 0.1f);
}

void NoiseGateProcessor::prepare(const audio::ProcessSpec& spec) {
    prepareChannels(spec);
    reset();
}

void NoiseGateProcessor::reset() noexcept {
    envelope_ = 0.0f;
    gain_ = 1.0f;
    open_ = true;
    holdRemaining_ = 0;
}

void NoiseGateProcessor::process(audio::AudioBlockView block) noexcept {
    if (!isEnabled() || !block.isValid())
        return;
    using Id = parameters::NoiseGate;
    const double rate = preparedSampleRate();
    const float threshold = dbToLinear(
        -80.0f + parameter(static_cast<std::uint16_t>(Id::Threshold)) * 70.0f);
    const float attackCoefficient = millisCoefficient(
        logMap(parameter(static_cast<std::uint16_t>(Id::Attack)), 0.1f, 100.0f),
        rate);
    const float releaseCoefficient = millisCoefficient(
        logMap(parameter(static_cast<std::uint16_t>(Id::Release)), 10.0f, 1000.0f),
        rate);
    const auto holdSamples = static_cast<std::uint64_t>(
        parameter(static_cast<std::uint16_t>(Id::Hold)) * rate * 0.5);
    const float closedGain = dbToLinear(
        -80.0f * (1.0f - parameter(static_cast<std::uint16_t>(Id::Range))));
    const std::uint32_t channels = std::min(block.channelCount, preparedChannels());

    for (std::uint32_t sample = 0; sample < block.sampleCount; ++sample) {
        float peak = 0.0f;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            if (block.channels[channel] != nullptr)
                peak = std::max(
                    peak,
                    std::abs(cleanSample(block.channels[channel][sample])));
        }
        const float coefficient = peak > envelope_
            ? attackCoefficient
            : releaseCoefficient;
        envelope_ = peak + coefficient * (envelope_ - peak);

        const float closeThreshold = threshold * 0.7079458f;
        if (envelope_ >= threshold) {
            open_ = true;
            holdRemaining_ = holdSamples;
        } else if (holdRemaining_ > 0) {
            --holdRemaining_;
        } else if (envelope_ < closeThreshold) {
            open_ = false;
        }
        const float desiredGain = open_ ? 1.0f : closedGain;
        const float gainCoefficient = desiredGain > gain_
            ? attackCoefficient
            : releaseCoefficient;
        gain_ = desiredGain + gainCoefficient * (gain_ - desiredGain);

        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            if (block.channels[channel] != nullptr)
                block.channels[channel][sample] = cleanSample(
                    cleanSample(block.channels[channel][sample]) * gain_);
        }
    }
}

LimiterProcessor::LimiterProcessor() noexcept : ProcessorBase(3) {
    using Id = parameters::Limiter;
    setDefault(static_cast<std::uint16_t>(Id::Threshold), 0.8f);
    setDefault(static_cast<std::uint16_t>(Id::Release), 0.25f);
    setDefault(static_cast<std::uint16_t>(Id::Ceiling), 1.0f);
}

void LimiterProcessor::prepare(const audio::ProcessSpec& spec) {
    prepareChannels(spec);
    sampleRate_ = preparedSampleRate();
    lookAheadSamples_ = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::llround(sampleRate_ * 0.005)));
    const std::size_t ringSize = lookAheadSamples_ + 1;
    delay_.assign(preparedChannels(), std::vector<float>(ringSize, 0.0f));
    dequeIndices_.assign(lookAheadSamples_ + 2, 0);
    dequeValues_.assign(lookAheadSamples_ + 2, 0.0f);
    releaseCoefficient_ = millisCoefficient(
        logMap(parameter(static_cast<std::uint16_t>(parameters::Limiter::Release)),
            10.0f,
            1000.0f),
        sampleRate_);
    reset();
}

void LimiterProcessor::reset() noexcept {
    for (auto& channel : delay_)
        std::fill(channel.begin(), channel.end(), 0.0f);
    std::fill(dequeIndices_.begin(), dequeIndices_.end(), 0);
    std::fill(dequeValues_.begin(), dequeValues_.end(), 0.0f);
    sampleIndex_ = 0;
    delayWrite_ = 0;
    dequeHead_ = 0;
    dequeSize_ = 0;
    gain_ = 1.0f;
}

std::uint32_t LimiterProcessor::knownLatencySamples(
    double sampleRate) const noexcept {
    return static_cast<std::uint32_t>(
        std::llround(safeSampleRate(sampleRate) * 0.005));
}

void LimiterProcessor::process(audio::AudioBlockView block) noexcept {
    if (!isEnabled() || !block.isValid() || delay_.empty())
        return;
    using Id = parameters::Limiter;
    thresholdDb_ = -36.0f
        + parameter(static_cast<std::uint16_t>(Id::Threshold)) * 36.0f;
    ceilingLinear_ = dbToLinear(
        -12.0f + parameter(static_cast<std::uint16_t>(Id::Ceiling)) * 12.0f);
    releaseCoefficient_ = millisCoefficient(
        logMap(parameter(static_cast<std::uint16_t>(Id::Release)), 10.0f, 1000.0f),
        sampleRate_);
    const float thresholdLinear = dbToLinear(thresholdDb_);
    const std::uint32_t channels = std::min<std::uint32_t>(
        block.channelCount,
        preparedChannels());
    const std::size_t ringSize = lookAheadSamples_ + 1;
    const std::size_t dequeCapacity = dequeIndices_.size();

    for (std::uint32_t sample = 0; sample < block.sampleCount; ++sample) {
        float inputPeak = 0.0f;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            if (block.channels[channel] == nullptr)
                continue;
            const float input = cleanSample(block.channels[channel][sample]);
            delay_[channel][delayWrite_] = input;
            inputPeak = std::max(inputPeak, std::abs(input));
        }

        while (dequeSize_ > 0) {
            const std::size_t back = (dequeHead_ + dequeSize_ - 1)
                % dequeCapacity;
            if (dequeValues_[back] > inputPeak)
                break;
            --dequeSize_;
        }
        const std::size_t tail = (dequeHead_ + dequeSize_) % dequeCapacity;
        dequeIndices_[tail] = sampleIndex_;
        dequeValues_[tail] = inputPeak;
        ++dequeSize_;

        const std::uint64_t oldest = sampleIndex_ > lookAheadSamples_
            ? sampleIndex_ - lookAheadSamples_
            : 0;
        while (dequeSize_ > 0 && dequeIndices_[dequeHead_] < oldest) {
            dequeHead_ = (dequeHead_ + 1) % dequeCapacity;
            --dequeSize_;
        }
        const float windowPeak = dequeSize_ > 0
            ? dequeValues_[dequeHead_]
            : 0.0f;

        float desiredGain = 1.0f;
        if (windowPeak > thresholdLinear) {
            const float over = windowPeak / thresholdLinear;
            const float compressed = thresholdLinear
                * std::pow(over, 0.1f);
            desiredGain = std::min(desiredGain, compressed / windowPeak);
        }
        if (windowPeak > ceilingLinear_)
            desiredGain = std::min(desiredGain, ceilingLinear_ / windowPeak);
        gain_ = desiredGain < gain_
            ? desiredGain
            : desiredGain + releaseCoefficient_ * (gain_ - desiredGain);

        if (sampleIndex_ >= lookAheadSamples_) {
            const std::size_t readIndex = (delayWrite_ + ringSize
                - lookAheadSamples_) % ringSize;
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                if (block.channels[channel] != nullptr)
                    block.channels[channel][sample] = cleanSample(
                        delay_[channel][readIndex] * gain_);
            }
        } else {
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                if (block.channels[channel] != nullptr)
                    block.channels[channel][sample] = 0.0f;
            }
        }

        delayWrite_ = (delayWrite_ + 1) % ringSize;
        ++sampleIndex_;
    }
}

DelayProcessor::DelayProcessor() noexcept : ProcessorBase(4) {
    using Id = parameters::Delay;
    setDefault(static_cast<std::uint16_t>(Id::Time), 0.43f);
    setDefault(static_cast<std::uint16_t>(Id::Feedback), 0.28f);
    setDefault(static_cast<std::uint16_t>(Id::StereoOffset), 0.0f);
    setDefault(static_cast<std::uint16_t>(Id::Tone), 0.8f);
}

void DelayProcessor::prepare(const audio::ProcessSpec& spec) {
    prepareChannels(spec);
    const std::size_t maximumDelay = static_cast<std::size_t>(
        std::ceil(preparedSampleRate() * 2.05)) + 2;
    buffers_.assign(
        preparedChannels(),
        std::vector<float>(maximumDelay, 0.0f));
    delayedScratch_.assign(preparedChannels(), 0.0f);
    toneState_.assign(preparedChannels(), 0.0f);
    currentDelaySamples_ = logMap(
        parameter(static_cast<std::uint16_t>(parameters::Delay::Time)),
        0.01f,
        2.0f) * preparedSampleRate();
    currentFeedback_ = parameter(
        static_cast<std::uint16_t>(parameters::Delay::Feedback)) * 0.92f;
    currentCrossfeed_ = parameter(
        static_cast<std::uint16_t>(parameters::Delay::StereoOffset));
    currentDamping_ = (1.0f - parameter(
        static_cast<std::uint16_t>(parameters::Delay::Tone))) * 0.95f;
    delaySmoothing_ = 1.0 - std::exp(
        -1.0 / (0.02 * preparedSampleRate()));
    parameterSmoothing_ = static_cast<float>(delaySmoothing_);
    reset();
}

void DelayProcessor::reset() noexcept {
    for (auto& buffer : buffers_)
        std::fill(buffer.begin(), buffer.end(), 0.0f);
    std::fill(delayedScratch_.begin(), delayedScratch_.end(), 0.0f);
    std::fill(toneState_.begin(), toneState_.end(), 0.0f);
    writeIndex_ = 0;
}

void DelayProcessor::process(audio::AudioBlockView block) noexcept {
    if (!isEnabled() || !block.isValid() || buffers_.empty())
        return;
    using Id = parameters::Delay;
    const double rate = preparedSampleRate();
    const double targetDelay = logMap(
        parameter(static_cast<std::uint16_t>(Id::Time)), 0.01f, 2.0f) * rate;
    const float targetFeedback = parameter(
        static_cast<std::uint16_t>(Id::Feedback)) * 0.92f;
    const float targetCrossfeed = parameter(
        static_cast<std::uint16_t>(Id::StereoOffset));
    const float targetDamping = (1.0f - parameter(
        static_cast<std::uint16_t>(Id::Tone))) * 0.95f;
    const std::uint32_t channels = std::min<std::uint32_t>(
        block.channelCount,
        preparedChannels());
    const std::size_t bufferSize = buffers_[0].size();
    const float filterCoefficient = static_cast<float>(
        1.0 - std::exp(-2.0 * std::numbers::pi * 9000.0 / rate));

    for (std::uint32_t sample = 0; sample < block.sampleCount; ++sample) {
        currentDelaySamples_ += delaySmoothing_
            * (targetDelay - currentDelaySamples_);
        currentFeedback_ += parameterSmoothing_
            * (targetFeedback - currentFeedback_);
        currentCrossfeed_ += parameterSmoothing_
            * (targetCrossfeed - currentCrossfeed_);
        currentDamping_ += parameterSmoothing_
            * (targetDamping - currentDamping_);

        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            if (block.channels[channel] == nullptr) {
                delayedScratch_[channel] = 0.0f;
                continue;
            }
            const double offset = (channel % 2 == 1)
                ? currentDelaySamples_ * currentCrossfeed_ * 0.5
                : 0.0;
            const double delaySamples = std::clamp(
                currentDelaySamples_ + offset,
                1.0,
                static_cast<double>(bufferSize - 2));
            double readPosition = static_cast<double>(writeIndex_) - delaySamples;
            while (readPosition < 0.0)
                readPosition += static_cast<double>(bufferSize);
            const auto first = static_cast<std::size_t>(readPosition);
            const auto second = (first + 1) % bufferSize;
            const float fraction = static_cast<float>(
                readPosition - static_cast<double>(first));
            const float delayed = buffers_[channel][first] * (1.0f - fraction)
                + buffers_[channel][second] * fraction;
            toneState_[channel] += filterCoefficient
                * (delayed - toneState_[channel]);
            const float filtered = delayed * (1.0f - currentDamping_)
                + toneState_[channel] * currentDamping_;
            delayedScratch_[channel] = cleanSample(filtered);
        }

        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            if (block.channels[channel] == nullptr)
                continue;
            const float input = cleanSample(block.channels[channel][sample]);
            buffers_[channel][writeIndex_] = cleanSample(
                input + currentFeedback_ * delayedScratch_[channel]);
            block.channels[channel][sample] = cleanSample(
                input + delayedScratch_[channel]);
        }
        writeIndex_ = (writeIndex_ + 1) % bufferSize;
    }
}

ReverbProcessor::ReverbProcessor() noexcept : ProcessorBase(6) {
    using Id = parameters::Reverb;
    setDefault(static_cast<std::uint16_t>(Id::Type), 0.0f);
    setDefault(static_cast<std::uint16_t>(Id::Decay), 0.29f);
    setDefault(static_cast<std::uint16_t>(Id::PreDelay), 0.0f);
    setDefault(static_cast<std::uint16_t>(Id::Size), 0.5f);
    setDefault(static_cast<std::uint16_t>(Id::Damping), 0.4f);
    setDefault(static_cast<std::uint16_t>(Id::Width), 0.75f);
}

void ReverbProcessor::prepare(const audio::ProcessSpec& spec) {
    prepareChannels(spec);
    sampleRate_ = preparedSampleRate();
    channels_.assign(preparedChannels(), {});
    wetScratch_.assign(preparedChannels(), 0.0f);
    currentDecaySeconds_ = logMap(
        parameter(static_cast<std::uint16_t>(parameters::Reverb::Decay)),
        0.2f,
        8.0f);
    currentPreDelaySamples_ = parameter(
        static_cast<std::uint16_t>(parameters::Reverb::PreDelay))
        * sampleRate_ * 0.2;
    currentSize_ = 0.5f
        + parameter(static_cast<std::uint16_t>(parameters::Reverb::Size));
    currentDamping_ = parameter(
        static_cast<std::uint16_t>(parameters::Reverb::Damping));
    currentWidth_ = parameter(
        static_cast<std::uint16_t>(parameters::Reverb::Width)) * 1.6f;
    currentType_ = static_cast<std::uint16_t>(std::lround(
        parameter(static_cast<std::uint16_t>(parameters::Reverb::Type)) * 3.0f));
    parameterSmoothing_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.03 * sampleRate_)));
    preDelaySmoothing_ = parameterSmoothing_;

    constexpr std::array<std::array<float, 4>, 4> combSeconds{{
        {{0.0297f, 0.0371f, 0.0411f, 0.0437f}},
        {{0.0113f, 0.0139f, 0.0173f, 0.0197f}},
        {{0.0231f, 0.0311f, 0.0373f, 0.0417f}},
        {{0.0173f, 0.0261f, 0.0331f, 0.0397f}},
    }};
    constexpr std::array<std::size_t, 2> allpassSamples{347, 113};
    const std::size_t maximumCombSamples = static_cast<std::size_t>(
        std::ceil(sampleRate_ * 0.09)) + 4;
    const std::size_t maximumPreDelay = static_cast<std::size_t>(
        std::ceil(sampleRate_ * 0.2)) + 2;

    for (std::size_t channel = 0; channel < channels_.size(); ++channel) {
        auto& state = channels_[channel];
        state.preDelay.assign(maximumPreDelay, 0.0f);
        for (std::size_t comb = 0; comb < state.combs.size(); ++comb) {
            auto& line = state.combs[comb];
            const float channelOffset = static_cast<float>(channel) * 0.00031f;
            const float seconds = std::min(
                0.075f,
                combSeconds[0][comb] + channelOffset) * 1.5f;
            const std::size_t requested = static_cast<std::size_t>(
                std::round(sampleRate_ * seconds));
            line.buffer.assign(
                std::min(maximumCombSamples, std::max<std::size_t>(2, requested + 1)),
                0.0f);
        }
        for (std::size_t stage = 0; stage < state.allpasses.size(); ++stage) {
            auto& line = state.allpasses[stage];
            const std::size_t length = static_cast<std::size_t>(
                std::round(sampleRate_ * allpassSamples[stage] / 48000.0));
            line.buffer.assign(std::max<std::size_t>(2, length), 0.0f);
        }
    }
    reset();
}

void ReverbProcessor::reset() noexcept {
    for (auto& channel : channels_) {
        for (auto& comb : channel.combs) {
            std::fill(comb.buffer.begin(), comb.buffer.end(), 0.0f);
            comb.index = 0;
            comb.dampingState = 0.0f;
        }
        for (auto& allpass : channel.allpasses) {
            std::fill(allpass.buffer.begin(), allpass.buffer.end(), 0.0f);
            allpass.index = 0;
        }
        std::fill(channel.preDelay.begin(), channel.preDelay.end(), 0.0f);
        channel.preDelayIndex = 0;
    }
}

void ReverbProcessor::process(audio::AudioBlockView block) noexcept {
    if (!isEnabled() || !block.isValid() || channels_.empty())
        return;
    using Id = parameters::Reverb;
    const float targetDecay = logMap(
        parameter(static_cast<std::uint16_t>(Id::Decay)), 0.2f, 8.0f);
    const double targetPreDelay = parameter(
        static_cast<std::uint16_t>(Id::PreDelay)) * sampleRate_ * 0.2;
    const float targetSize = 0.5f
        + parameter(static_cast<std::uint16_t>(Id::Size));
    const float targetDamping = parameter(static_cast<std::uint16_t>(Id::Damping));
    const float targetWidth = parameter(static_cast<std::uint16_t>(Id::Width)) * 1.6f;
    const std::uint16_t targetType = static_cast<std::uint16_t>(std::lround(
        parameter(static_cast<std::uint16_t>(Id::Type)) * 3.0f));
    constexpr std::array<std::array<float, 4>, 4> combSeconds{{
        {{0.0297f, 0.0371f, 0.0411f, 0.0437f}},
        {{0.0113f, 0.0139f, 0.0173f, 0.0197f}},
        {{0.0231f, 0.0311f, 0.0373f, 0.0417f}},
        {{0.0173f, 0.0261f, 0.0331f, 0.0397f}},
    }};
    const std::uint32_t channelCount = std::min<std::uint32_t>(
        block.channelCount,
        preparedChannels());

    for (std::uint32_t sample = 0; sample < block.sampleCount; ++sample) {
        currentDecaySeconds_ += parameterSmoothing_
            * (targetDecay - currentDecaySeconds_);
        currentPreDelaySamples_ += preDelaySmoothing_
            * (targetPreDelay - currentPreDelaySamples_);
        currentSize_ += parameterSmoothing_ * (targetSize - currentSize_);
        currentDamping_ += parameterSmoothing_ * (targetDamping - currentDamping_);
        currentWidth_ += parameterSmoothing_ * (targetWidth - currentWidth_);
        currentType_ = targetType;

        for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
            if (block.channels[channel] == nullptr) {
                wetScratch_[channel] = 0.0f;
                continue;
            }
            auto& state = channels_[channel];
            const float input = cleanSample(block.channels[channel][sample]);
            const std::size_t preSize = state.preDelay.size();
            const auto preDelay = static_cast<std::size_t>(std::clamp(
                currentPreDelaySamples_,
                0.0,
                static_cast<double>(preSize - 1)));
            const std::size_t preRead = (state.preDelayIndex + preSize
                - preDelay) % preSize;
            const float predelayed = preDelay == 0
                ? input
                : state.preDelay[preRead];
            state.preDelay[state.preDelayIndex] = input;
            state.preDelayIndex = (state.preDelayIndex + 1) % preSize;

            float combSum = 0.0f;
            for (std::size_t comb = 0; comb < state.combs.size(); ++comb) {
                auto& line = state.combs[comb];
                const float seconds = combSeconds[currentType_][comb]
                    * currentSize_;
                const auto delaySamples = static_cast<std::size_t>(std::clamp(
                    std::round(seconds * sampleRate_),
                    1.0,
                    static_cast<double>(line.buffer.size() - 1)));
                const std::size_t read = (line.index + line.buffer.size()
                    - delaySamples) % line.buffer.size();
                const float delayed = line.buffer[read];
                line.dampingState += (1.0f - currentDamping_)
                    * (delayed - line.dampingState);
                const float feedback = std::min(
                    0.98f,
                    std::pow(10.0f, -3.0f * seconds
                        / std::max(0.2f, currentDecaySeconds_)));
                line.buffer[line.index] = cleanSample(
                    predelayed + line.dampingState * feedback);
                line.index = (line.index + 1) % line.buffer.size();
                combSum += delayed;
            }

            float reverberated = combSum * 0.25f;
            for (auto& line : state.allpasses) {
                const float delayed = line.buffer[line.index];
                const float output = delayed - 0.5f * reverberated;
                line.buffer[line.index] = cleanSample(
                    reverberated + 0.5f * output);
                line.index = (line.index + 1) % line.buffer.size();
                reverberated = output;
            }
            wetScratch_[channel] = cleanSample(reverberated);
        }

        if (channelCount >= 2) {
            const float mid = (wetScratch_[0] + wetScratch_[1]) * 0.5f;
            const float side = (wetScratch_[0] - wetScratch_[1]) * 0.5f
                * currentWidth_;
            wetScratch_[0] = cleanSample(mid + side);
            wetScratch_[1] = cleanSample(mid - side);
        }
        for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
            if (block.channels[channel] != nullptr)
                block.channels[channel][sample] = cleanSample(
                    cleanSample(block.channels[channel][sample])
                    + wetScratch_[channel] * 0.6f);
        }
    }
}

SaturationProcessor::SaturationProcessor() noexcept : ProcessorBase(3) {
    using Id = parameters::Saturation;
    setDefault(static_cast<std::uint16_t>(Id::Drive), 0.0f);
    setDefault(static_cast<std::uint16_t>(Id::Tone), 0.8f);
    setDefault(static_cast<std::uint16_t>(Id::Output), 0.5f);
}

void SaturationProcessor::prepare(const audio::ProcessSpec& spec) {
    prepareChannels(spec);
    previousInput_.assign(preparedChannels(), 0.0f);
    toneState_.assign(preparedChannels(), 0.0f);
    currentDrive_ = 1.0f
        + parameter(static_cast<std::uint16_t>(parameters::Saturation::Drive))
            * 19.0f;
    driveSmoothing_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.01 * preparedSampleRate())));
    toneSmoothing_ = driveSmoothing_;
    reset();
}

void SaturationProcessor::reset() noexcept {
    std::fill(previousInput_.begin(), previousInput_.end(), 0.0f);
    std::fill(toneState_.begin(), toneState_.end(), 0.0f);
}

void SaturationProcessor::process(audio::AudioBlockView block) noexcept {
    if (!isEnabled() || !block.isValid() || previousInput_.empty())
        return;
    using Id = parameters::Saturation;
    const float drive = 1.0f
        + parameter(static_cast<std::uint16_t>(Id::Drive)) * 19.0f;
    const float tone = parameter(static_cast<std::uint16_t>(Id::Tone));
    const float toneFrequency = logMap(tone, 1000.0f, 18000.0f);
    const float toneCoefficient = static_cast<float>(
        1.0 - std::exp(
            -2.0 * std::numbers::pi * toneFrequency / preparedSampleRate()));
    const float outputGain = dbToLinear(
        (parameter(static_cast<std::uint16_t>(Id::Output)) - 0.5f) * 24.0f);
    currentOutputGain_ = outputGain;

    const std::uint32_t channels = std::min(block.channelCount, preparedChannels());
    for (std::uint32_t sample = 0; sample < block.sampleCount; ++sample) {
        currentDrive_ += driveSmoothing_ * (drive - currentDrive_);
        currentTone_ += toneSmoothing_ * (toneCoefficient - currentTone_);
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            if (block.channels[channel] == nullptr)
                continue;
            const float input = cleanSample(block.channels[channel][sample]);
            const float driven = input * currentDrive_;
            const float previous = previousInput_[channel];
            const float delta = driven - previous;
            float saturated = 0.0f;
            if (std::abs(delta) < 1.0e-4f) {
                saturated = std::tanh((driven + previous) * 0.5f);
            } else {
                const auto logCosh = [](float value) noexcept {
                    const float absolute = std::abs(value);
                    return absolute + std::log1p(std::exp(-2.0f * absolute))
                        - 0.69314718056f;
                };
                saturated = (logCosh(driven) - logCosh(previous)) / delta;
            }
            previousInput_[channel] = driven;
            toneState_[channel] += currentTone_
                * (saturated - toneState_[channel]);
            block.channels[channel][sample] = cleanSample(
                toneState_[channel] * currentOutputGain_);
        }
    }
}

DeEsserProcessor::DeEsserProcessor() noexcept : ProcessorBase(5) {
    using Id = parameters::DeEsser;
    setDefault(static_cast<std::uint16_t>(Id::Frequency), 0.65f);
    setDefault(static_cast<std::uint16_t>(Id::Threshold), 0.6f);
    setDefault(static_cast<std::uint16_t>(Id::Ratio), 0.25f);
    setDefault(static_cast<std::uint16_t>(Id::Attack), 0.15f);
    setDefault(static_cast<std::uint16_t>(Id::Release), 0.25f);
}

void DeEsserProcessor::prepare(const audio::ProcessSpec& spec) {
    prepareChannels(spec);
    lowState_.assign(preparedChannels(), 0.0f);
    highScratch_.assign(preparedChannels(), 0.0f);
    cutoffSmoothing_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.02 * preparedSampleRate())));
    reset();
}

void DeEsserProcessor::reset() noexcept {
    std::fill(lowState_.begin(), lowState_.end(), 0.0f);
    std::fill(highScratch_.begin(), highScratch_.end(), 0.0f);
    detectorEnvelope_ = 0.0f;
}

void DeEsserProcessor::process(audio::AudioBlockView block) noexcept {
    if (!isEnabled() || !block.isValid() || lowState_.empty())
        return;
    using Id = parameters::DeEsser;
    const double rate = preparedSampleRate();
    const float frequency = logMap(
        parameter(static_cast<std::uint16_t>(Id::Frequency)), 2000.0f, 12000.0f);
    const float targetCutoff = static_cast<float>(
        1.0 - std::exp(-2.0 * std::numbers::pi * frequency / rate));
    const float thresholdDb = -60.0f
        + parameter(static_cast<std::uint16_t>(Id::Threshold)) * 54.0f;
    ratio_ = 1.0f
        + parameter(static_cast<std::uint16_t>(Id::Ratio)) * 11.0f;
    attackCoefficient_ = millisCoefficient(
        logMap(parameter(static_cast<std::uint16_t>(Id::Attack)), 0.1f, 20.0f),
        rate);
    releaseCoefficient_ = millisCoefficient(
        logMap(parameter(static_cast<std::uint16_t>(Id::Release)), 20.0f, 500.0f),
        rate);
    const std::uint32_t channels = std::min(block.channelCount, preparedChannels());

    for (std::uint32_t sample = 0; sample < block.sampleCount; ++sample) {
        currentCutoff_ += cutoffSmoothing_ * (targetCutoff - currentCutoff_);
        float detectorPeak = 0.0f;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            if (block.channels[channel] == nullptr) {
                highScratch_[channel] = 0.0f;
                continue;
            }
            const float input = cleanSample(block.channels[channel][sample]);
            lowState_[channel] += currentCutoff_ * (input - lowState_[channel]);
            const float high = input - lowState_[channel];
            highScratch_[channel] = high;
            detectorPeak = std::max(detectorPeak, std::abs(high));
        }
        const float coefficient = detectorPeak > detectorEnvelope_
            ? attackCoefficient_
            : releaseCoefficient_;
        detectorEnvelope_ = detectorPeak
            + coefficient * (detectorEnvelope_ - detectorPeak);
        const float overDb = linearToDb(detectorEnvelope_) - thresholdDb;
        const float gainDb = overDb > 0.0f
            ? -(overDb * (1.0f - 1.0f / ratio_))
            : 0.0f;
        const float highGain = std::clamp(dbToLinear(gainDb), 0.125f, 1.0f);
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            if (block.channels[channel] != nullptr) {
                const float input = cleanSample(block.channels[channel][sample]);
                block.channels[channel][sample] = cleanSample(
                    input - highScratch_[channel] * (1.0f - highGain));
            }
        }
    }
}

PitchCorrectionProcessor::PitchCorrectionProcessor(
    std::uint32_t instanceId) noexcept
    : ProcessorBase(44),
      randomState_(instanceId ^ 0x9e3779b9u) {
    using Id = parameters::PitchCorrection;
    setDefault(static_cast<std::uint16_t>(Id::Key), 0.0f);
    setDefault(static_cast<std::uint16_t>(Id::Scale), 1.0f / 3.0f);
    setDefault(static_cast<std::uint16_t>(Id::RetuneSpeed), 0.38f);
    setDefault(static_cast<std::uint16_t>(Id::CorrectionAmount), 1.0f);
    setDefault(static_cast<std::uint16_t>(Id::Humanize), 0.0f);
    setDefault(static_cast<std::uint16_t>(Id::Vibrato), 0.0f);
    setDefault(static_cast<std::uint16_t>(Id::Formant), 0.5f);
    setDefault(static_cast<std::uint16_t>(Id::LowNote), 0.16f);
    setDefault(static_cast<std::uint16_t>(Id::HighNote), 0.83f);

    constexpr std::array<bool, 12> cMajor{
        true, false, true, false, true, true,
        false, true, false, true, false, true,
    };
    for (std::uint16_t note = 0; note < cMajor.size(); ++note) {
        setDefault(
            static_cast<std::uint16_t>(Id::CustomScaleFirstNote) + note,
            cMajor[note] ? 1.0f : 0.0f);
    }
    if (randomState_ == 0)
        randomState_ = 1;
}

bool PitchCorrectionProcessor::supportsParameterId(
    std::uint16_t id) const noexcept {
    using Id = parameters::PitchCorrection;
    return id <= static_cast<std::uint16_t>(Id::HighNote)
        || (id >= static_cast<std::uint16_t>(Id::CustomScaleFirstNote)
            && id <= static_cast<std::uint16_t>(Id::CustomScaleLastNote));
}

void PitchCorrectionProcessor::prepare(const audio::ProcessSpec& spec) {
    prepareChannels(spec);
    sampleRate_ = preparedSampleRate();
    fftSize_ = sampleRate_ > 50000.0 ? kMaximumFftSize : 2048;
    hopSize_ = fftSize_ / 4;
    downsampleFactor_ = fftSize_ / kDownsampledWindowSize;
    vocoderChannels_.assign(preparedChannels(), {});
    formantLowState_.assign(preparedChannels(), 0.0f);

    for (std::size_t i = 0; i < fftSize_; ++i) {
        analysisWindow_[i] = 0.5f * (1.0f - std::cos(
            2.0f * kPi * static_cast<float>(i)
            / static_cast<float>(fftSize_ - 1)));
    }
    for (std::size_t i = 0; i < twiddles_.size(); ++i) {
        const float angle = -2.0f * kPi * static_cast<float>(i)
            / static_cast<float>(kMaximumFftSize);
        twiddles_[i] = {std::cos(angle), std::sin(angle)};
    }
    std::size_t reversed = 0;
    for (std::size_t i = 0; i < fftSize_; ++i) {
        bitReversed_[i] = static_cast<std::uint16_t>(reversed);
        std::size_t bit = fftSize_ >> 1;
        while (bit != 0 && (reversed & bit) != 0) {
            reversed ^= bit;
            bit >>= 1;
        }
        reversed ^= bit;
    }
    reset();
}

void PitchCorrectionProcessor::reset() noexcept {
    pitchWindow_.fill(0.0f);
    yinWindow_.fill(0.0f);
    differences_.fill(0.0f);
    for (auto& channel : vocoderChannels_)
        channel = {};
    std::fill(formantLowState_.begin(), formantLowState_.end(), 0.0f);
    inputWriteIndex_ = 0;
    outputWriteIndex_ = 0;
    samplesFilled_ = 0;
    samplesSinceFrame_ = 0;
    samplesUntilAnalysis_ = fftSize_;
    detectedFrequency_ = 0.0;
    targetFrequency_ = 0.0;
    currentSemitones_ = 0.0;
    vibratoPhase_ = 0.0;
    humanizeCounter_ = 0.0;
    confidence_ = 0.0f;
    humanizeOffset_ = 0.0f;
    currentCorrectionAmount_ = parameter(
        static_cast<std::uint16_t>(parameters::PitchCorrection::CorrectionAmount));
    detectedFrequencyTelemetry_.store(0.0f, std::memory_order_relaxed);
    targetFrequencyTelemetry_.store(0.0f, std::memory_order_relaxed);
    confidenceTelemetry_.store(0.0f, std::memory_order_relaxed);
    shiftSemitonesTelemetry_.store(0.0f, std::memory_order_relaxed);
}

void PitchCorrectionProcessor::copyDiagnostics(
    PitchCorrectionDiagnostics& destination) const noexcept {
    destination.detectedFrequencyHz =
        detectedFrequencyTelemetry_.load(std::memory_order_acquire);
    destination.targetFrequencyHz =
        targetFrequencyTelemetry_.load(std::memory_order_acquire);
    destination.confidence =
        confidenceTelemetry_.load(std::memory_order_acquire);
    destination.appliedShiftSemitones =
        shiftSemitonesTelemetry_.load(std::memory_order_acquire);
}

std::uint32_t PitchCorrectionProcessor::knownLatencySamples(
    double sampleRate) const noexcept {
    const double rate = safeSampleRate(sampleRate);
    return static_cast<std::uint32_t>(rate > 50000.0
        ? kMaximumFftSize
        : 2048);
}

float PitchCorrectionProcessor::randomUnit() noexcept {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return static_cast<float>(randomState_ / 4294967295.0 * 2.0 - 1.0);
}

float PitchCorrectionProcessor::nearestScaleNote(float frequency) const noexcept {
    using Id = parameters::PitchCorrection;
    if (!(frequency > 0.0f) || !std::isfinite(frequency))
        return 0.0f;
    const float midi = 69.0f + 12.0f * std::log2(frequency / 440.0f);
    const int roundedMidi = static_cast<int>(std::lround(midi));
    const int key = static_cast<int>(std::lround(
        parameter(static_cast<std::uint16_t>(Id::Key)) * 11.0f));
    const auto scale = static_cast<parameters::PitchScale>(std::clamp(
        static_cast<int>(std::lround(
            parameter(static_cast<std::uint16_t>(Id::Scale)) * 3.0f)),
        0,
        3));
    constexpr std::array<int, 12> major{
        0, -1, 2, -1, 4, 5, -1, 7, -1, 9, -1, 11,
    };
    constexpr std::array<int, 12> minor{
        0, -1, 2, 3, -1, 5, -1, 7, 8, -1, 10, -1,
    };

    int bestMidi = roundedMidi;
    float bestDistance = std::numeric_limits<float>::max();
    for (int candidate = roundedMidi - 12; candidate <= roundedMidi + 12;
         ++candidate) {
        const int pitchClass = (candidate % 12 + 12) % 12;
        const int relative = (pitchClass - key + 12) % 12;
        bool enabled = false;
        switch (scale) {
        case parameters::PitchScale::Chromatic:
            enabled = true;
            break;
        case parameters::PitchScale::Major:
            enabled = major[relative] >= 0;
            break;
        case parameters::PitchScale::Minor:
            enabled = minor[relative] >= 0;
            break;
        case parameters::PitchScale::Custom:
            enabled = parameter(
                static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(Id::CustomScaleFirstNote)
                    + static_cast<std::uint16_t>(relative)))
                >= 0.5f;
            break;
        }
        const float distance = std::abs(static_cast<float>(candidate) - midi);
        if (enabled && distance < bestDistance) {
            bestDistance = distance;
            bestMidi = candidate;
        }
    }
    return static_cast<float>(bestMidi) - midi;
}

void PitchCorrectionProcessor::analyzePitch() noexcept {
    using Id = parameters::PitchCorrection;
    constexpr std::size_t windowSize = kDownsampledWindowSize;
    const std::size_t lowLagBound = static_cast<std::size_t>(
        (sampleRate_ / downsampleFactor_) / 1200.0);
    float mean = 0.0f;
    for (std::size_t i = 0; i < windowSize; ++i) {
        const std::size_t index = (inputWriteIndex_
            + i * downsampleFactor_) % fftSize_;
        yinWindow_[i] = pitchWindow_[index];
        mean += yinWindow_[i];
    }
    mean /= static_cast<float>(windowSize);
    float energy = 0.0f;
    for (float& value : yinWindow_) {
        value -= mean;
        energy += value * value;
    }
    const float rms = std::sqrt(energy / static_cast<float>(windowSize));
    const float minimumFrequency = logMap(
        parameter(static_cast<std::uint16_t>(Id::LowNote)), 45.0f, 250.0f);
    const float maximumFrequency = logMap(
        parameter(static_cast<std::uint16_t>(Id::HighNote)), 200.0f, 1200.0f);
    const float lowFrequency = std::min(
        minimumFrequency,
        std::max(45.0f, maximumFrequency * 0.75f));
    const float highFrequency = std::max(
        maximumFrequency,
        minimumFrequency * 1.25f);
    const double analysisRate = sampleRate_ / downsampleFactor_;
    const std::size_t minLag = std::clamp<std::size_t>(
        static_cast<std::size_t>(analysisRate / highFrequency),
        std::max<std::size_t>(2, lowLagBound),
        windowSize / 4);
    const std::size_t maxLag = std::clamp<std::size_t>(
        static_cast<std::size_t>(analysisRate / lowFrequency),
        minLag + 1,
        std::min(differences_.size() - 1, windowSize / 2 - 1));

    if (rms < 0.003f || maxLag <= minLag) {
        confidence_ = 0.0f;
        detectedFrequency_ = 0.0;
        targetFrequency_ = 0.0;
        detectedFrequencyTelemetry_.store(0.0f, std::memory_order_release);
        targetFrequencyTelemetry_.store(0.0f, std::memory_order_release);
        confidenceTelemetry_.store(0.0f, std::memory_order_release);
        return;
    }

    double cumulativeDifference = 0.0;
    differences_.fill(1.0f);
    for (std::size_t lag = 1; lag <= maxLag; ++lag) {
        double difference = 0.0;
        const std::size_t count = windowSize - lag;
        for (std::size_t i = 0; i < count; ++i) {
            const double delta = static_cast<double>(yinWindow_[i])
                - yinWindow_[i + lag];
            difference += delta * delta;
        }
        cumulativeDifference += difference;
        if (lag >= minLag) {
            differences_[lag] = static_cast<float>(
                difference * lag / std::max(cumulativeDifference, 1.0e-12));
        }
    }

    std::size_t bestLag = minLag;
    bool foundThresholdCrossing = false;
    constexpr float kYinThreshold = 0.16f;
    for (std::size_t lag = minLag; lag <= maxLag; ++lag) {
        if (differences_[lag] < kYinThreshold) {
            bestLag = lag;
            while (bestLag < maxLag
                && differences_[bestLag + 1] < differences_[bestLag]) {
                ++bestLag;
            }
            foundThresholdCrossing = true;
            break;
        }
    }
    if (!foundThresholdCrossing) {
        for (std::size_t lag = minLag + 1; lag <= maxLag; ++lag) {
            if (differences_[lag] < differences_[bestLag])
                bestLag = lag;
        }
    }

    float refinedLag = static_cast<float>(bestLag);
    if (bestLag > minLag && bestLag < maxLag) {
        const float left = differences_[bestLag - 1];
        const float center = differences_[bestLag];
        const float right = differences_[bestLag + 1];
        const float denominator = left - 2.0f * center + right;
        if (std::abs(denominator) > 1.0e-8f) {
            const float offset = 0.5f * (left - right) / denominator;
            refinedLag += std::clamp(offset, -0.5f, 0.5f);
        }
    }
    const float cmnd = differences_[bestLag];
    const float confidence = std::clamp(1.0f - cmnd, 0.0f, 1.0f);
    const double candidate = analysisRate / std::max(1.0f, refinedLag);
    if (confidence < 0.55f || candidate < lowFrequency * 0.85
        || candidate > highFrequency * 1.15) {
        confidence_ = 0.0f;
        detectedFrequency_ = 0.0;
        targetFrequency_ = 0.0;
        detectedFrequencyTelemetry_.store(0.0f, std::memory_order_release);
        targetFrequencyTelemetry_.store(0.0f, std::memory_order_release);
        confidenceTelemetry_.store(0.0f, std::memory_order_release);
        return;
    }

    if (detectedFrequency_ > 0.0) {
        const double ratio = candidate / detectedFrequency_;
        if ((ratio > 1.6 || ratio < 0.625)
            && confidence < confidence_ + 0.12f) {
            confidence_ *= 0.98f;
            confidenceTelemetry_.store(confidence_, std::memory_order_release);
            return;
        }
    }
    detectedFrequency_ = candidate;
    confidence_ = confidence;
    const float semitoneOffset = nearestScaleNote(
        static_cast<float>(detectedFrequency_));
    targetFrequency_ = detectedFrequency_
        * std::pow(2.0, static_cast<double>(semitoneOffset) / 12.0);
    detectedFrequencyTelemetry_.store(
        static_cast<float>(detectedFrequency_),
        std::memory_order_release);
    targetFrequencyTelemetry_.store(
        static_cast<float>(targetFrequency_),
        std::memory_order_release);
    confidenceTelemetry_.store(confidence_, std::memory_order_release);
}

void PitchCorrectionProcessor::transform(
    VocoderChannel& channel,
    bool inverse) noexcept {
    for (std::size_t i = 0; i < fftSize_; ++i) {
        const std::size_t reversed = bitReversed_[i];
        if (i < reversed)
            std::swap(channel.spectrum[i], channel.spectrum[reversed]);
    }

    for (std::size_t length = 2; length <= fftSize_; length <<= 1) {
        const std::size_t half = length / 2;
        const std::size_t twiddleStride = kMaximumFftSize / length;
        for (std::size_t start = 0; start < fftSize_; start += length) {
            for (std::size_t offset = 0; offset < half; ++offset) {
                auto twiddle = twiddles_[offset * twiddleStride];
                if (inverse)
                    twiddle = std::conj(twiddle);
                const auto even = channel.spectrum[start + offset];
                const auto odd = channel.spectrum[start + offset + half]
                    * twiddle;
                channel.spectrum[start + offset] = even + odd;
                channel.spectrum[start + offset + half] = even - odd;
            }
        }
    }
    if (inverse) {
        const float scale = 1.0f / static_cast<float>(fftSize_);
        for (std::size_t i = 0; i < fftSize_; ++i)
            channel.spectrum[i] *= scale;
    }
}

void PitchCorrectionProcessor::processVocoderFrame(
    std::uint32_t channelIndex,
    double pitchRatio) noexcept {
    auto& channel = vocoderChannels_[channelIndex];
    const std::size_t binCount = fftSize_ / 2 + 1;
    const std::size_t outputRingSize = kMaximumFftSize * 2;
    pitchRatio = std::clamp(pitchRatio, 0.5, 2.0);

    for (std::size_t i = 0; i < fftSize_; ++i) {
        const std::size_t inputIndex = (inputWriteIndex_ + i) % fftSize_;
        channel.spectrum[i] = {
            channel.input[inputIndex] * analysisWindow_[i],
            0.0f,
        };
    }
    transform(channel, false);

    std::fill_n(shiftedMagnitude_.begin(), binCount, 0.0f);
    std::fill_n(shiftedFrequency_.begin(), binCount, 0.0f);
    std::fill_n(shiftedWeight_.begin(), binCount, 0.0f);
    std::fill_n(shiftedInitialPhase_.begin(), binCount, 0.0f);
    for (std::size_t inputBin = 0; inputBin < binCount; ++inputBin) {
        const auto value = channel.spectrum[inputBin];
        const float magnitude = std::abs(value);
        const float phase = std::atan2(value.imag(), value.real());
        double instantaneousFrequency = 2.0 * std::numbers::pi
            * inputBin / fftSize_;
        if (channel.hasPreviousFrame) {
            const double expectedAdvance = 2.0 * std::numbers::pi
                * inputBin * hopSize_ / fftSize_;
            double phaseDeviation = phase - channel.previousPhase[inputBin]
                - expectedAdvance;
            phaseDeviation -= 2.0 * std::numbers::pi
                * std::round(phaseDeviation / (2.0 * std::numbers::pi));
            instantaneousFrequency += phaseDeviation / hopSize_;
        }
        channel.previousPhase[inputBin] = phase;

        const double shiftedBin = inputBin * pitchRatio;
        if (shiftedBin >= static_cast<double>(binCount - 1))
            continue;
        const std::size_t lower = static_cast<std::size_t>(shiftedBin);
        const float fraction = static_cast<float>(shiftedBin - lower);
        const float lowerWeight = magnitude * (1.0f - fraction);
        const float upperWeight = magnitude * fraction;
        shiftedMagnitude_[lower] += lowerWeight;
        shiftedFrequency_[lower] += static_cast<float>(
            instantaneousFrequency * pitchRatio) * lowerWeight;
        shiftedWeight_[lower] += lowerWeight;
        if (!channel.hasPreviousFrame && lowerWeight > 0.0f)
            shiftedInitialPhase_[lower] = phase;
        if (lower + 1 < binCount && upperWeight > 0.0f) {
            shiftedMagnitude_[lower + 1] += upperWeight;
            shiftedFrequency_[lower + 1] += static_cast<float>(
                instantaneousFrequency * pitchRatio) * upperWeight;
            shiftedWeight_[lower + 1] += upperWeight;
            if (!channel.hasPreviousFrame)
                shiftedInitialPhase_[lower + 1] = phase;
        }
    }

    for (std::size_t outputBin = 0; outputBin < binCount; ++outputBin) {
        const float weight = shiftedWeight_[outputBin];
        if (weight <= 1.0e-12f) {
            channel.spectrum[outputBin] = {};
            continue;
        }
        if (channel.hasPreviousFrame) {
            const float frequency = shiftedFrequency_[outputBin] / weight;
            channel.synthesisPhase[outputBin] += frequency
                * static_cast<float>(hopSize_);
        } else {
            channel.synthesisPhase[outputBin] =
                shiftedInitialPhase_[outputBin];
        }
        const float magnitude = shiftedMagnitude_[outputBin];
        channel.spectrum[outputBin] = {
            magnitude * std::cos(channel.synthesisPhase[outputBin]),
            magnitude * std::sin(channel.synthesisPhase[outputBin]),
        };
    }
    channel.spectrum[0] = {channel.spectrum[0].real(), 0.0f};
    channel.spectrum[fftSize_ / 2] = {
        channel.spectrum[fftSize_ / 2].real(),
        0.0f,
    };
    for (std::size_t bin = 1; bin + 1 < binCount; ++bin)
        channel.spectrum[fftSize_ - bin] = std::conj(channel.spectrum[bin]);
    transform(channel, true);

    const std::size_t outputStart = (outputWriteIndex_ + 1) % outputRingSize;
    constexpr float kOverlapNormalization = 1.5f;
    for (std::size_t i = 0; i < fftSize_; ++i) {
        const std::size_t outputIndex = (outputStart + i) % outputRingSize;
        channel.output[outputIndex] += channel.spectrum[i].real()
            * analysisWindow_[i] / kOverlapNormalization;
    }
    channel.hasPreviousFrame = true;
}

void PitchCorrectionProcessor::process(audio::AudioBlockView block) noexcept {
    if (!isEnabled() || !block.isValid() || vocoderChannels_.empty())
        return;
    using Id = parameters::PitchCorrection;
    const std::uint32_t channels = std::min<std::uint32_t>(
        block.channelCount,
        preparedChannels());
    const std::size_t outputRingSize = kMaximumFftSize * 2;
    const float currentLowFrequency = logMap(
        parameter(static_cast<std::uint16_t>(Id::LowNote)), 45.0f, 250.0f);
    const float formant = parameter(static_cast<std::uint16_t>(Id::Formant));
    const float formantGain = std::pow(2.0f, (formant - 0.5f) * 1.0f);
    const float formantCoefficient = static_cast<float>(
        1.0 - std::exp(
            -2.0 * std::numbers::pi * currentLowFrequency * 1.5
            / sampleRate_));
    const float amount = parameter(
        static_cast<std::uint16_t>(Id::CorrectionAmount));
    const float retuneMs = logMap(
        parameter(static_cast<std::uint16_t>(Id::RetuneSpeed)), 2.0f, 500.0f);
    retuneCoefficient_ = static_cast<float>(
        std::exp(-1.0 / (retuneMs * 0.001 * sampleRate_)));
    const float humanize = parameter(static_cast<std::uint16_t>(Id::Humanize));
    const float vibrato = parameter(static_cast<std::uint16_t>(Id::Vibrato));
    const double humanizeInterval = sampleRate_ * 0.1;

    for (std::uint32_t sample = 0; sample < block.sampleCount; ++sample) {
        float mono = 0.0f;
        std::uint32_t validInputs = 0;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            auto& state = vocoderChannels_[channel];
            if (block.channels[channel] == nullptr) {
                state.input[inputWriteIndex_] = 0.0f;
                state.output[outputWriteIndex_] = 0.0f;
                continue;
            }
            const float input = cleanSample(block.channels[channel][sample]);
            mono += input;
            ++validInputs;
            state.input[inputWriteIndex_] = input;

            const float shifted = cleanSample(state.output[outputWriteIndex_]);
            state.output[outputWriteIndex_] = 0.0f;
            formantLowState_[channel] += formantCoefficient
                * (shifted - formantLowState_[channel]);
            const float shaped = formantLowState_[channel]
                + (shifted - formantLowState_[channel]) * formantGain;
            block.channels[channel][sample] = cleanSample(shaped);
        }
        if (validInputs > 0)
            mono /= static_cast<float>(validInputs);
        pitchWindow_[inputWriteIndex_] = mono;

        inputWriteIndex_ = (inputWriteIndex_ + 1) % fftSize_;
        samplesFilled_ = std::min(fftSize_, samplesFilled_ + 1);
        ++samplesSinceFrame_;
        if (samplesUntilAnalysis_ > 0)
            --samplesUntilAnalysis_;
        if (samplesUntilAnalysis_ == 0) {
            analyzePitch();
            samplesUntilAnalysis_ = std::max<std::size_t>(1, fftSize_ / 8);
        }

        humanizeCounter_ += 1.0;
        if (humanizeCounter_ >= humanizeInterval) {
            humanizeCounter_ -= humanizeInterval;
            humanizeOffset_ = randomUnit() * humanize * 0.18f;
        }
        const float targetSemitones = detectedFrequency_ > 0.0
            && targetFrequency_ > 0.0
            ? static_cast<float>(12.0 * std::log2(
                targetFrequency_ / detectedFrequency_))
                + humanizeOffset_
            : 0.0f;
        currentCorrectionAmount_ += 0.001f
            * (amount - currentCorrectionAmount_);
        currentSemitones_ = targetSemitones
            + retuneCoefficient_ * (currentSemitones_ - targetSemitones);
        const double vibratoSemitones = std::sin(vibratoPhase_)
            * vibrato * 0.35;
        const double ratio = std::clamp(
            std::pow(
                2.0,
                (currentSemitones_ * currentCorrectionAmount_
                    + vibratoSemitones) / 12.0),
            0.5,
            2.0);
        if (samplesFilled_ == fftSize_ && samplesSinceFrame_ >= hopSize_) {
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                if (block.channels[channel] != nullptr)
                    processVocoderFrame(channel, ratio);
            }
            samplesSinceFrame_ = 0;
        }

        vibratoPhase_ += 2.0 * std::numbers::pi * 5.2 / sampleRate_;
        if (vibratoPhase_ >= 2.0 * std::numbers::pi)
            vibratoPhase_ -= 2.0 * std::numbers::pi;
        outputWriteIndex_ = (outputWriteIndex_ + 1) % outputRingSize;
    }
    shiftSemitonesTelemetry_.store(
        static_cast<float>(currentSemitones_ * currentCorrectionAmount_),
        std::memory_order_release);
}

} // namespace auralis::dsp::detail
