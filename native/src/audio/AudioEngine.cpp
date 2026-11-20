#include "AudioEngine.h"

#include <auralis/bridge/auralis_c_api.h>
#include <auralis/mixer/SignalChainFactory.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <utility>

// The engine branch may be built before the additive mixer-state contract is
// merged. The opaque declaration is compatible with the completed public POD.
struct AuralisMixerState;

namespace {

using auralis::audio::AudioDeviceBackend;
using auralis::audio::AudioDeviceInfo;
using auralis::audio::detail::AudioEngine;
using auralis::audio::kMaximumAudioChannels;
using auralis::mixer::ChainCommand;
using auralis::mixer::ChainCommandType;
using auralis::mixer::EffectType;
using auralis::mixer::SignalChainSnapshot;

struct MixerStateLayout {
    float input_gain_db;
    float output_gain_db;
    float pan;
    std::uint32_t muted;
};

static_assert(sizeof(MixerStateLayout) == 16);

[[nodiscard]] bool isValidEffectType(AuralisEffectType type) noexcept {
    const auto value = static_cast<std::uint32_t>(type);
    return value <= static_cast<std::uint32_t>(AURALIS_EFFECT_LIMITER);
}

[[nodiscard]] std::uint32_t copyUtf8Name(
    const char* source,
    char* destination,
    std::size_t capacity) noexcept {
    if (source == nullptr || destination == nullptr || capacity == 0) {
        return 0;
    }

    const auto* terminator = static_cast<const char*>(
        std::memchr(source, '\0', capacity));
    const auto sourceLength = terminator == nullptr
        ? capacity
        : static_cast<std::size_t>(terminator - source);
    std::size_t read = 0;
    std::size_t written = 0;
    while (read < sourceLength) {
        const auto lead = static_cast<unsigned char>(source[read]);
        const std::size_t sequenceLength =
            lead < 0x80 ? 1
            : (lead & 0xe0) == 0xc0 ? 2
            : (lead & 0xf0) == 0xe0 ? 3
            : (lead & 0xf8) == 0xf0 ? 4
            : 0;
        if (sequenceLength == 0
            || read + sequenceLength > sourceLength
            || written + sequenceLength >= capacity) {
            break;
        }
        bool validSequence = true;
        for (std::size_t byte = 1; byte < sequenceLength; ++byte) {
            const auto continuation =
                static_cast<unsigned char>(source[read + byte]);
            if ((continuation & 0xc0) != 0x80) {
                validSequence = false;
                break;
            }
        }
        if (!validSequence) {
            break;
        }
        std::memcpy(destination + written, source + read, sequenceLength);
        written += sequenceLength;
        read += sequenceLength;
    }
    destination[written] = '\0';
    return static_cast<std::uint32_t>(written);
}

[[nodiscard]] AuralisEffectType toCType(EffectType type) noexcept {
    return static_cast<AuralisEffectType>(static_cast<std::uint32_t>(type));
}

} // namespace

struct AuralisEngine {
    AuralisEngine(
        std::unique_ptr<AudioDeviceBackend> backend,
        std::unique_ptr<auralis::mixer::SignalChain> signalChain)
        : implementation(std::move(backend), std::move(signalChain)) {}

    AudioEngine implementation;
};

namespace {

[[nodiscard]] AuralisStatus enqueueCommand(
    AuralisEngine* engine,
    const ChainCommand& command) noexcept {
    if (engine == nullptr) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    try {
        return engine->implementation.enqueue(command)
            ? AURALIS_STATUS_OK
            : AURALIS_STATUS_QUEUE_FULL;
    } catch (...) {
        return AURALIS_STATUS_INTERNAL_ERROR;
    }
}

} // namespace

namespace auralis::audio::detail {
namespace {

struct MeterPair {
    float peak = 0.0f;
    float rms = 0.0f;
};

[[nodiscard]] MeterPair measureChannel(
    const float* samples,
    std::uint32_t sampleCount,
    bool& clipping) noexcept {
    if (samples == nullptr || sampleCount == 0) {
        return {};
    }

    float peak = 0.0f;
    double squareSum = 0.0;
    for (std::uint32_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        const auto value = samples[sampleIndex];
        if (!std::isfinite(value)) {
            clipping = true;
            continue;
        }
        const auto magnitude = std::abs(value);
        peak = std::max(peak, magnitude);
        squareSum += static_cast<double>(value) * value;
        clipping = clipping || magnitude >= 1.0f;
    }
    return {peak, static_cast<float>(std::sqrt(squareSum / sampleCount))};
}

} // namespace

AudioEngine::AudioEngine(
    std::unique_ptr<AudioDeviceBackend> backend,
    std::unique_ptr<mixer::SignalChain> signalChain)
    : backend_(std::move(backend)),
      signalChain_(std::move(signalChain)) {
    if (backend_ == nullptr || signalChain_ == nullptr) {
        throw std::invalid_argument("Audio engine dependencies are unavailable");
    }
    for (auto& value : meterValues_) {
        value.store(std::bit_cast<std::uint32_t>(0.0f), std::memory_order_relaxed);
    }
    for (auto& value : waveformValues_) {
        value.store(std::bit_cast<std::uint32_t>(0.0f), std::memory_order_relaxed);
    }
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::start(
    const AudioDeviceConfig& config,
    std::uint32_t inputChannelCount) {
    constexpr double kMinimumSampleRate = 8000.0;
    constexpr double kMaximumSampleRate = 384000.0;
    constexpr std::uint32_t kMaximumBlockSize = 32768;

    const auto& spec = config.processSpec;
    if (!std::isfinite(spec.sampleRate)
        || spec.sampleRate < kMinimumSampleRate
        || spec.sampleRate > kMaximumSampleRate
        || spec.maximumBlockSize == 0
        || spec.maximumBlockSize > kMaximumBlockSize
        || spec.channelCount == 0
        || spec.channelCount > kMaximumAudioChannels
        || inputChannelCount > kMaximumAudioChannels) {
        return false;
    }

    std::lock_guard lock(controlMutex_);
    backend_->stop();
    sampleRateHz_.store(spec.sampleRate, std::memory_order_relaxed);
    configuredInputChannels_.store(inputChannelCount, std::memory_order_relaxed);
    {
        std::lock_guard commandLock(commandMutex_);
        signalChain_->prepare(spec);
    }

    auto selectedConfig = config;
    if (selectedConfig.inputDeviceIndex == kDefaultAudioDevice) {
        selectedConfig.inputDeviceIndex = inputDeviceIndex_;
    } else {
        inputDeviceIndex_ = selectedConfig.inputDeviceIndex;
    }
    if (selectedConfig.outputDeviceIndex == kDefaultAudioDevice) {
        selectedConfig.outputDeviceIndex = outputDeviceIndex_;
    } else {
        outputDeviceIndex_ = selectedConfig.outputDeviceIndex;
    }

    if (!backend_->start(selectedConfig, *this)) {
        return false;
    }
    return true;
}

void AudioEngine::stop() noexcept {
    testToneEnabled_.store(0, std::memory_order_release);
    try {
        std::lock_guard lock(controlMutex_);
        backend_->stop();
    } catch (...) {
        // Destruction and stop are C ABI-safe even if a platform adapter fails.
    }
}

bool AudioEngine::isRunning() const noexcept {
    return backend_->isRunning();
}

bool AudioEngine::isValidDevice(bool forInput, std::uint32_t index) const {
    std::lock_guard lock(controlMutex_);
    return index < backend_->deviceCount(forInput);
}

bool AudioEngine::selectDevice(bool forInput, std::uint32_t index) {
    std::lock_guard lock(controlMutex_);
    if (index >= backend_->deviceCount(forInput)) {
        return false;
    }
    if (!backend_->selectDevice(forInput, index)) {
        return false;
    }
    (forInput ? inputDeviceIndex_ : outputDeviceIndex_) = index;
    return true;
}

std::uint32_t AudioEngine::requestedInputChannelCount() const noexcept {
    return configuredInputChannels_.load(std::memory_order_relaxed);
}

bool AudioEngine::setInputGainDb(float gainDb) noexcept {
    if (!std::isfinite(gainDb) || gainDb < -60.0f || gainDb > 24.0f) {
        return false;
    }
    inputGainLinear_.store(std::pow(10.0f, gainDb / 20.0f),
                           std::memory_order_relaxed);
    inputGainDb_.store(gainDb, std::memory_order_relaxed);
    return true;
}

bool AudioEngine::setOutputGainDb(float gainDb) noexcept {
    if (!std::isfinite(gainDb) || gainDb < -60.0f || gainDb > 12.0f) {
        return false;
    }
    outputGainLinear_.store(std::pow(10.0f, gainDb / 20.0f),
                            std::memory_order_relaxed);
    outputGainDb_.store(gainDb, std::memory_order_relaxed);
    return true;
}

bool AudioEngine::setPan(float pan) noexcept {
    if (!std::isfinite(pan) || pan < -1.0f || pan > 1.0f) {
        return false;
    }
    pan_.store(pan, std::memory_order_relaxed);
    return true;
}

void AudioEngine::setMuted(bool muted) noexcept {
    muted_.store(muted ? 1u : 0u, std::memory_order_relaxed);
}

bool AudioEngine::copyMixerState(
    float& inputGainDb,
    float& outputGainDb,
    float& pan,
    bool& muted) const noexcept {
    inputGainDb = inputGainDb_.load(std::memory_order_relaxed);
    outputGainDb = outputGainDb_.load(std::memory_order_relaxed);
    pan = pan_.load(std::memory_order_relaxed);
    muted = muted_.load(std::memory_order_relaxed) != 0;
    return true;
}

bool AudioEngine::copyMeters(
    std::array<float, 8>& values,
    bool& clipping,
    double& estimatedLatencyMs) const noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] = std::bit_cast<float>(
            meterValues_[index].load(std::memory_order_relaxed));
    }
    clipping = clipping_.load(std::memory_order_relaxed) != 0;
    estimatedLatencyMs = backend_->isRunning()
        ? backend_->estimatedLatencyMs()
        : 0.0;
    return true;
}

bool AudioEngine::copyWaveform(std::array<float, 128>& values) const noexcept {
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = std::bit_cast<float>(
            waveformValues_[i].load(std::memory_order_relaxed));
    }
    return true;
}

void AudioEngine::setTestTone(bool enabled, float frequencyHz) noexcept {
    if (!std::isfinite(frequencyHz)) frequencyHz = 440.0f;
    testToneFrequencyHz_.store(std::clamp(frequencyHz, 20.0f, 20000.0f),
                               std::memory_order_relaxed);
    testToneEnabled_.store(enabled ? 1u : 0u, std::memory_order_release);
}

bool AudioEngine::copyChainSnapshot(
    mixer::SignalChainSnapshot& destination) const noexcept {
    try {
        std::lock_guard lock(controlMutex_);
        signalChain_->copySnapshot(destination);
        return true;
    } catch (...) {
        return false;
    }
}

bool AudioEngine::copyPitchDiagnostics(
    std::uint32_t instanceId,
    auralis::dsp::PitchCorrectionDiagnostics& destination) const noexcept {
    try {
        std::lock_guard lock(controlMutex_);
        return signalChain_ != nullptr
            && signalChain_->copyPitchDiagnostics(instanceId, destination);
    } catch (...) {
        return false;
    }
}

bool AudioEngine::enqueue(const mixer::ChainCommand& command) noexcept {
    try {
        std::lock_guard lock(commandMutex_);
        return signalChain_->enqueue(command);
    } catch (...) {
        return false;
    }
}

void AudioEngine::process(
    ConstAudioBlockView input,
    AudioBlockView output) noexcept {
    if (output.channels == nullptr || output.channelCount == 0
        || output.sampleCount == 0) {
        return;
    }
    if (output.channelCount > kMaximumAudioChannels) {
        for (std::uint32_t channel = 0; channel < output.channelCount; ++channel) {
            if (output.channels[channel] != nullptr) {
                std::fill_n(output.channels[channel], output.sampleCount, 0.0f);
            }
        }
        return;
    }

    const auto configuredInputCount =
        configuredInputChannels_.load(std::memory_order_relaxed);
    const auto activeInputCount = std::min(
        std::min(input.channelCount, configuredInputCount),
        kMaximumAudioChannels);
    const ConstAudioBlockView activeInput{
        input.channels,
        activeInputCount,
        input.sampleCount,
    };
    bool hasMissingOutput = false;
    for (std::uint32_t channel = 0; channel < output.channelCount; ++channel) {
        hasMissingOutput = hasMissingOutput || output.channels[channel] == nullptr;
    }
    if (hasMissingOutput) {
        for (std::uint32_t channel = 0; channel < output.channelCount; ++channel) {
            if (output.channels[channel] != nullptr) {
                std::fill_n(
                    output.channels[channel], output.sampleCount, 0.0f);
            }
        }
        publishMeters(activeInput, output);
        return;
    }

    const auto inputGain = inputGainLinear_.load(std::memory_order_relaxed);
    const bool testTone = testToneEnabled_.load(std::memory_order_acquire) != 0;
    if (testTone) {
        const auto frequency = testToneFrequencyHz_.load(std::memory_order_relaxed);
        const auto rate = sampleRateHz_.load(std::memory_order_relaxed);
        const auto phaseStep = 2.0 * std::numbers::pi * frequency / rate;
        for (std::uint32_t sample = 0; sample < output.sampleCount; ++sample) {
            const auto value = static_cast<float>(0.16 * std::sin(testTonePhase_));
            for (std::uint32_t channel = 0; channel < output.channelCount; ++channel)
                output.channels[channel][sample] = value;
            testTonePhase_ += phaseStep;
            if (testTonePhase_ >= 2.0 * std::numbers::pi)
                testTonePhase_ -= 2.0 * std::numbers::pi;
        }
    } else for (std::uint32_t channel = 0; channel < output.channelCount; ++channel) {
        auto* outputSamples = output.channels[channel];
        if (outputSamples == nullptr) {
            continue;
        }
        const auto sourceChannel = activeInputCount == 1 ? 0 : channel;
        const auto* inputSamples = activeInput.channels != nullptr
            && sourceChannel < activeInputCount
            ? activeInput.channels[sourceChannel]
            : nullptr;
        if (inputSamples == nullptr) {
            std::fill_n(outputSamples, output.sampleCount, 0.0f);
        } else {
            for (std::uint32_t sample = 0; sample < output.sampleCount; ++sample) {
                outputSamples[sample] = inputSamples[sample] * inputGain;
            }
        }
    }

    signalChain_->process(output);

    const auto muted = muted_.load(std::memory_order_relaxed) != 0;
    const auto outputGain = outputGainLinear_.load(std::memory_order_relaxed);
    const auto pan = pan_.load(std::memory_order_relaxed);
    const auto leftBalance = pan > 0.0f ? 1.0f - pan : 1.0f;
    const auto rightBalance = pan < 0.0f ? 1.0f + pan : 1.0f;
    for (std::uint32_t channel = 0; channel < output.channelCount; ++channel) {
        auto* samples = output.channels[channel];
        if (samples == nullptr) {
            continue;
        }
        const auto balance = output.channelCount >= 2 && channel == 0
            ? leftBalance
            : output.channelCount >= 2 && channel == 1
                ? rightBalance
                : 1.0f;
        const auto gain = outputGain * balance;
        if (muted) {
            std::fill_n(samples, output.sampleCount, 0.0f);
        } else if (gain != 1.0f) {
            for (std::uint32_t sample = 0; sample < output.sampleCount; ++sample) {
                samples[sample] *= gain;
            }
        }
    }

    publishMeters(activeInput, output);
    const auto* waveformSamples = testTone
        ? output.channels[0]
        : activeInput.channels != nullptr && activeInput.channelCount > 0
            ? activeInput.channels[0]
            : nullptr;
    publishWaveform(waveformSamples, testTone ? output.sampleCount : activeInput.sampleCount);
}

void AudioEngine::publishWaveform(
    const float* samples, std::uint32_t sampleCount) noexcept {
    for (std::size_t i = 0; i < waveformValues_.size(); ++i) {
        float value = 0.0f;
        if (samples != nullptr && sampleCount > 0) {
            const auto index = static_cast<std::uint32_t>(
                (i * static_cast<std::size_t>(sampleCount)) / waveformValues_.size());
            value = samples[std::min(index, sampleCount - 1)];
        }
        waveformValues_[i].store(std::bit_cast<std::uint32_t>(value),
                                 std::memory_order_relaxed);
    }
}

void AudioEngine::publishMeters(
    ConstAudioBlockView input,
    AudioBlockView output) noexcept {
    bool clipping = false;
    const auto inputLeft = measureChannel(
        input.channels != nullptr && input.channelCount > 0
            ? input.channels[0]
            : nullptr,
        input.sampleCount, clipping);
    const auto inputRight = measureChannel(
        input.channels != nullptr && input.channelCount > 1
            ? input.channels[1]
            : nullptr,
        input.sampleCount, clipping);
    const auto outputLeft = measureChannel(
        output.channels != nullptr && output.channelCount > 0
            ? output.channels[0]
            : nullptr,
        output.sampleCount, clipping);
    const auto outputRight = measureChannel(
        output.channels != nullptr && output.channelCount > 1
            ? output.channels[1]
            : nullptr,
        output.sampleCount, clipping);
    if (input.channels != nullptr) {
        for (std::uint32_t channel = 2; channel < input.channelCount; ++channel) {
            const auto* samples = input.channels[channel];
            if (samples == nullptr) {
                continue;
            }
            for (std::uint32_t sample = 0; sample < input.sampleCount; ++sample) {
                clipping = clipping || !std::isfinite(samples[sample])
                    || std::abs(samples[sample]) >= 1.0f;
            }
        }
    }
    if (output.channels != nullptr) {
        for (std::uint32_t channel = 2; channel < output.channelCount; ++channel) {
            const auto* samples = output.channels[channel];
            if (samples == nullptr) {
                continue;
            }
            for (std::uint32_t sample = 0; sample < output.sampleCount; ++sample) {
                clipping = clipping || !std::isfinite(samples[sample])
                    || std::abs(samples[sample]) >= 1.0f;
            }
        }
    }

    const std::array values{
        inputLeft.peak,
        inputRight.peak,
        inputLeft.rms,
        inputRight.rms,
        outputLeft.peak,
        outputRight.peak,
        outputLeft.rms,
        outputRight.rms,
    };
    for (std::size_t index = 0; index < values.size(); ++index) {
        meterValues_[index].store(
            std::bit_cast<std::uint32_t>(values[index]),
            std::memory_order_relaxed);
    }
    clipping_.store(clipping ? 1u : 0u, std::memory_order_relaxed);
}

} // namespace auralis::audio::detail

extern "C" {

AURALIS_API std::uint32_t auralis_api_version(void) {
    return 1;
}

AURALIS_API AuralisEngine* auralis_engine_create(void) {
    try {
        auto backend = auralis::audio::createPlatformAudioBackend();
        auto chain = auralis::mixer::createSignalChain();
        return new AuralisEngine(std::move(backend), std::move(chain));
    } catch (...) {
        return nullptr;
    }
}

AURALIS_API void auralis_engine_destroy(AuralisEngine* engine) {
    delete engine;
}

AURALIS_API AuralisStatus auralis_engine_start(
    AuralisEngine* engine,
    const AuralisAudioConfig* config) {
    if (engine == nullptr || config == nullptr) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    if (!std::isfinite(config->sample_rate)
        || config->sample_rate < 8000.0
        || config->sample_rate > 384000.0
        || config->buffer_size == 0
        || config->buffer_size > 32768
        || config->input_channels > kMaximumAudioChannels
        || config->output_channels == 0
        || config->output_channels > kMaximumAudioChannels) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }

    try {
        auralis::audio::AudioDeviceConfig deviceConfig{};
        deviceConfig.processSpec.sampleRate = config->sample_rate;
        deviceConfig.processSpec.maximumBlockSize = config->buffer_size;
        deviceConfig.processSpec.channelCount = config->output_channels;
        deviceConfig.inputDeviceIndex = auralis::audio::kDefaultAudioDevice;
        deviceConfig.outputDeviceIndex = auralis::audio::kDefaultAudioDevice;
        return engine->implementation.start(
                   deviceConfig, config->input_channels)
            ? AURALIS_STATUS_OK
            : AURALIS_STATUS_DEVICE_ERROR;
    } catch (...) {
        return AURALIS_STATUS_INTERNAL_ERROR;
    }
}

AURALIS_API void auralis_engine_stop(AuralisEngine* engine) {
    if (engine != nullptr) {
        engine->implementation.stop();
    }
}

AURALIS_API std::uint32_t auralis_engine_is_running(
    const AuralisEngine* engine) {
    return engine != nullptr && engine->implementation.isRunning() ? 1u : 0u;
}

AURALIS_API AuralisStatus auralis_engine_get_meters(
    const AuralisEngine* engine,
    AuralisMeters* destination) {
    if (engine == nullptr || destination == nullptr) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }

    std::array<float, 8> values{};
    bool clipping = false;
    double latency = 0.0;
    if (!engine->implementation.copyMeters(values, clipping, latency)) {
        return AURALIS_STATUS_INTERNAL_ERROR;
    }
    *destination = {
        values[0],
        values[1],
        values[2],
        values[3],
        values[4],
        values[5],
        values[6],
        values[7],
        clipping ? 1u : 0u,
        latency,
    };
    return AURALIS_STATUS_OK;
}

AURALIS_API AuralisStatus auralis_engine_get_waveform(
    const AuralisEngine* engine, float* destination, std::uint32_t sampleCount) {
    if (engine == nullptr || destination == nullptr || sampleCount == 0)
        return AURALIS_STATUS_INVALID_ARGUMENT;
    std::array<float, 128> values{};
    if (!engine->implementation.copyWaveform(values))
        return AURALIS_STATUS_INTERNAL_ERROR;
    const auto count = std::min<std::size_t>(sampleCount, values.size());
    std::copy_n(values.begin(), count, destination);
    return AURALIS_STATUS_OK;
}

AURALIS_API AuralisStatus auralis_engine_set_test_tone(
    AuralisEngine* engine, std::uint32_t enabled, float frequencyHz) {
    if (engine == nullptr || !std::isfinite(frequencyHz)
        || frequencyHz < 20.0f || frequencyHz > 20000.0f)
        return AURALIS_STATUS_INVALID_ARGUMENT;
    engine->implementation.setTestTone(enabled != 0, frequencyHz);
    return AURALIS_STATUS_OK;
}

AURALIS_API AuralisStatus auralis_engine_get_mixer_state(
    const AuralisEngine* engine,
    AuralisMixerState* destination) {
    if (engine == nullptr || destination == nullptr) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    MixerStateLayout state{};
    bool muted = false;
    if (!engine->implementation.copyMixerState(
            state.input_gain_db,
            state.output_gain_db,
            state.pan,
            muted)) {
        return AURALIS_STATUS_INTERNAL_ERROR;
    }
    state.muted = muted ? 1u : 0u;
    std::memcpy(destination, &state, sizeof(state));
    return AURALIS_STATUS_OK;
}

AURALIS_API AuralisStatus auralis_engine_set_input_gain(
    AuralisEngine* engine,
    float gain_db) {
    if (engine == nullptr) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    return engine->implementation.setInputGainDb(gain_db)
        ? AURALIS_STATUS_OK
        : AURALIS_STATUS_INVALID_ARGUMENT;
}

AURALIS_API AuralisStatus auralis_engine_set_output_gain(
    AuralisEngine* engine,
    float gain_db) {
    if (engine == nullptr) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    return engine->implementation.setOutputGainDb(gain_db)
        ? AURALIS_STATUS_OK
        : AURALIS_STATUS_INVALID_ARGUMENT;
}

AURALIS_API AuralisStatus auralis_engine_set_pan(
    AuralisEngine* engine,
    float pan) {
    if (engine == nullptr) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    return engine->implementation.setPan(pan)
        ? AURALIS_STATUS_OK
        : AURALIS_STATUS_INVALID_ARGUMENT;
}

AURALIS_API AuralisStatus auralis_engine_set_mute(
    AuralisEngine* engine,
    std::uint32_t muted) {
    if (engine == nullptr || muted > 1) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    engine->implementation.setMuted(muted != 0);
    return AURALIS_STATUS_OK;
}

AURALIS_API std::uint32_t auralis_device_count(std::uint32_t for_input) {
    if (for_input > 1) {
        return 0;
    }
    try {
        auto backend = auralis::audio::createPlatformAudioBackend();
        return backend == nullptr
            ? 0
            : backend->deviceCount(for_input != 0);
    } catch (...) {
        return 0;
    }
}

AURALIS_API AuralisStatus auralis_device_get_info(
    std::uint32_t for_input,
    std::uint32_t index,
    AuralisDeviceInfo* destination) {
    if (for_input > 1 || destination == nullptr) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }

    try {
        auto backend = auralis::audio::createPlatformAudioBackend();
        if (backend == nullptr) {
            return AURALIS_STATUS_DEVICE_ERROR;
        }
        const auto input = for_input != 0;
        if (index >= backend->deviceCount(input)) {
            return AURALIS_STATUS_INVALID_ARGUMENT;
        }

        AudioDeviceInfo info{};
        if (!backend->getDeviceInfo(input, index, info)) {
            return AURALIS_STATUS_DEVICE_ERROR;
        }

        AuralisDeviceInfo result{};
        result.index = info.index;
        static_cast<void>(
            copyUtf8Name(info.name, result.name, sizeof(result.name)));
        result.input_channels = info.inputChannels;
        result.output_channels = info.outputChannels;
        result.is_default = info.isDefault ? 1u : 0u;
        *destination = result;
        return AURALIS_STATUS_OK;
    } catch (...) {
        return AURALIS_STATUS_INTERNAL_ERROR;
    }
}

AURALIS_API AuralisStatus auralis_engine_select_device(
    AuralisEngine* engine,
    std::uint32_t for_input,
    std::uint32_t index) {
    if (engine == nullptr || for_input > 1) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    try {
        if (!engine->implementation.isValidDevice(for_input != 0, index)) {
            return AURALIS_STATUS_INVALID_ARGUMENT;
        }
        return engine->implementation.selectDevice(for_input != 0, index)
            ? AURALIS_STATUS_OK
            : AURALIS_STATUS_DEVICE_ERROR;
    } catch (...) {
        return AURALIS_STATUS_INTERNAL_ERROR;
    }
}

AURALIS_API AuralisStatus auralis_engine_effect_add(
    AuralisEngine* engine,
    AuralisEffectType type,
    std::uint32_t instance_id,
    std::uint32_t index) {
    if (engine == nullptr
        || !isValidEffectType(type)
        || instance_id == 0
        || index > AURALIS_MAX_EFFECTS_PER_CHANNEL) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    ChainCommand command{};
    command.type = ChainCommandType::Add;
    command.instanceId = instance_id;
    command.effectType = static_cast<EffectType>(
        static_cast<std::uint8_t>(type));
    command.index = static_cast<std::uint16_t>(index);
    return enqueueCommand(engine, command);
}

AURALIS_API AuralisStatus auralis_engine_effect_remove(
    AuralisEngine* engine,
    std::uint32_t instance_id) {
    if (engine == nullptr || instance_id == 0) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    ChainCommand command{};
    command.type = ChainCommandType::Remove;
    command.instanceId = instance_id;
    return enqueueCommand(engine, command);
}

AURALIS_API AuralisStatus auralis_engine_effect_move(
    AuralisEngine* engine,
    std::uint32_t instance_id,
    std::uint32_t destination_index) {
    if (engine == nullptr
        || instance_id == 0
        || destination_index >= AURALIS_MAX_EFFECTS_PER_CHANNEL) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    ChainCommand command{};
    command.type = ChainCommandType::Move;
    command.instanceId = instance_id;
    command.destinationIndex = static_cast<std::uint16_t>(destination_index);
    return enqueueCommand(engine, command);
}

AURALIS_API AuralisStatus auralis_engine_effect_set_enabled(
    AuralisEngine* engine,
    std::uint32_t instance_id,
    std::uint32_t enabled) {
    if (engine == nullptr || instance_id == 0 || enabled > 1) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    ChainCommand command{};
    command.type = ChainCommandType::SetEnabled;
    command.instanceId = instance_id;
    command.value = enabled != 0 ? 1.0f : 0.0f;
    return enqueueCommand(engine, command);
}

AURALIS_API AuralisStatus auralis_engine_effect_set_wet(
    AuralisEngine* engine,
    std::uint32_t instance_id,
    float wet) {
    if (engine == nullptr
        || instance_id == 0
        || !std::isfinite(wet)
        || wet < 0.0f
        || wet > 1.0f) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    ChainCommand command{};
    command.type = ChainCommandType::SetWet;
    command.instanceId = instance_id;
    command.value = wet;
    return enqueueCommand(engine, command);
}

AURALIS_API AuralisStatus auralis_engine_effect_set_parameter(
    AuralisEngine* engine,
    std::uint32_t instance_id,
    std::uint16_t parameter_id,
    float normalized_value) {
    if (engine == nullptr
        || instance_id == 0
        || !std::isfinite(normalized_value)) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    ChainCommand command{};
    command.type = ChainCommandType::SetParameter;
    command.instanceId = instance_id;
    command.parameterId = parameter_id;
    command.value = std::clamp(normalized_value, 0.0f, 1.0f);
    return enqueueCommand(engine, command);
}

AURALIS_API AuralisStatus auralis_engine_get_chain_state(
    const AuralisEngine* engine,
    AuralisChainState* destination) {
    if (engine == nullptr || destination == nullptr) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }

    SignalChainSnapshot snapshot{};
    if (!engine->implementation.copyChainSnapshot(snapshot)) {
        return AURALIS_STATUS_INTERNAL_ERROR;
    }
    if (snapshot.size > AURALIS_MAX_EFFECTS_PER_CHANNEL) {
        return AURALIS_STATUS_INTERNAL_ERROR;
    }

    AuralisChainState result{};
    result.effect_count = snapshot.size;
    for (std::uint32_t index = 0; index < snapshot.size; ++index) {
        const auto& source = snapshot.slots[index];
        if (source.parameterCount > AURALIS_MAX_PARAMETERS_PER_EFFECT) {
            return AURALIS_STATUS_INTERNAL_ERROR;
        }
        auto& effect = result.effects[index];
        effect.instance_id = source.instanceId;
        effect.type = toCType(source.type);
        effect.enabled = source.enabled ? 1u : 0u;
        effect.wet = source.wet;
        effect.parameter_count = source.parameterCount;
        for (std::uint32_t parameter = 0;
             parameter < source.parameterCount;
             ++parameter) {
            effect.parameter_ids[parameter] = source.parameterIds[parameter];
            effect.parameter_values[parameter] =
                source.parameterValues[parameter];
        }
    }
    *destination = result;
    return AURALIS_STATUS_OK;
}

AURALIS_API AuralisStatus auralis_engine_get_pitch_diagnostics(
    const AuralisEngine* engine,
    uint32_t instance_id,
    AuralisPitchDiagnostics* destination) {
    if (engine == nullptr || destination == nullptr) {
        return AURALIS_STATUS_INVALID_ARGUMENT;
    }
    auralis::dsp::PitchCorrectionDiagnostics diagnostics{};
    if (!engine->implementation.copyPitchDiagnostics(
            instance_id, diagnostics)) {
        return AURALIS_STATUS_NOT_READY;
    }
    destination->detected_frequency_hz = diagnostics.detectedFrequencyHz;
    destination->target_frequency_hz = diagnostics.targetFrequencyHz;
    destination->confidence = diagnostics.confidence;
    destination->applied_shift_semitones = diagnostics.appliedShiftSemitones;
    return AURALIS_STATUS_OK;
}

} // extern "C"
