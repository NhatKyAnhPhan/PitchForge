#include <auralis/audio/AudioDeviceBackend.h>

#include <juce_audio_devices/juce_audio_devices.h>

#include "../../audio/EngineAudioCallbackConfig.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

namespace auralis::audio {
namespace {

class JuceWasapiBackend final : public AudioDeviceBackend {
public:
    JuceWasapiBackend()
        : deviceType_(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(
              juce::WASAPIDeviceMode::shared)),
          callbackAdapter_(*this) {
        if (deviceType_ != nullptr) {
            deviceType_->scanForDevices();
        }
    }

    ~JuceWasapiBackend() override {
        stop();
    }

    [[nodiscard]] std::uint32_t deviceCount(bool forInput) const override {
        if (deviceType_ == nullptr) {
            return 0;
        }
        deviceType_->scanForDevices();
        return static_cast<std::uint32_t>(
            deviceType_->getDeviceNames(forInput).size());
    }

    [[nodiscard]] bool getDeviceInfo(
        bool forInput,
        std::uint32_t index,
        AudioDeviceInfo& destination) const override {
        if (deviceType_ == nullptr) {
            return false;
        }
        deviceType_->scanForDevices();
        const auto names = deviceType_->getDeviceNames(forInput);
        if (index >= static_cast<std::uint32_t>(names.size())) {
            return false;
        }

        const auto name = names[static_cast<int>(index)];
        const auto outputName = forInput ? juce::String{} : name;
        const auto inputName = forInput ? name : juce::String{};
        std::unique_ptr<juce::AudioIODevice> device(
            deviceType_->createDevice(outputName, inputName));

        AudioDeviceInfo result{};
        result.index = index;
        copyUtf8Name(name, result.name, sizeof(result.name));
        if (device != nullptr) {
            result.inputChannels = static_cast<std::uint32_t>(
                device->getInputChannelNames().size());
            result.outputChannels = static_cast<std::uint32_t>(
                device->getOutputChannelNames().size());
        }
        result.isDefault =
            deviceType_->getDefaultDeviceIndex(forInput)
            == static_cast<int>(index);
        destination = result;
        return true;
    }

    [[nodiscard]] bool start(
        const AudioDeviceConfig& config,
        AudioDeviceCallback& callback) override {
        stop();
        if (deviceType_ == nullptr
            || config.processSpec.channelCount == 0
            || config.processSpec.channelCount > kMaximumAudioChannels
            || config.processSpec.maximumBlockSize == 0) {
            return false;
        }

        auto requestedInputChannels = config.processSpec.channelCount;
        if (const auto* inputConfig =
                dynamic_cast<const detail::InputChannelCountProvider*>(&callback)) {
            requestedInputChannels =
                inputConfig->requestedInputChannelCount();
        }
        if (requestedInputChannels > kMaximumAudioChannels) {
            return false;
        }

        deviceType_->scanForDevices();
        const auto inputIndex = requestedInputChannels == 0
            ? std::optional<std::uint32_t>{}
            : resolveIndex(true, config.inputDeviceIndex, selectedInputIndex_);
        const auto outputIndex = resolveIndex(
            false, config.outputDeviceIndex, selectedOutputIndex_);
        if ((requestedInputChannels > 0 && !inputIndex.has_value())
            || !outputIndex.has_value()) {
            return false;
        }

        const auto outputNames = deviceType_->getDeviceNames(false);
        const auto inputNames = requestedInputChannels == 0
            ? juce::StringArray{}
            : deviceType_->getDeviceNames(true);
        const auto inputName = requestedInputChannels == 0
            ? juce::String{}
            : inputNames[static_cast<int>(*inputIndex)];
        const auto outputName = outputNames[static_cast<int>(*outputIndex)];
        std::unique_ptr<juce::AudioIODevice> candidate(
            deviceType_->createDevice(outputName, inputName));
        if (candidate == nullptr) {
            return false;
        }

        const auto inputChannelNames = candidate->getInputChannelNames();
        const auto outputChannelNames = candidate->getOutputChannelNames();
        const auto requestedChannels = static_cast<int>(
            config.processSpec.channelCount);
        const auto activeInputChannels = static_cast<int>(
            requestedInputChannels);
        const auto activeOutputChannels = std::min(
            requestedChannels, outputChannelNames.size());
        if (activeOutputChannels != requestedChannels
            || activeInputChannels > inputChannelNames.size()) {
            return false;
        }

        juce::BigInteger inputMask;
        juce::BigInteger outputMask;
        inputMask.setRange(0, activeInputChannels, true);
        outputMask.setRange(0, activeOutputChannels, true);

        const auto openError = candidate->open(
            inputMask,
            outputMask,
            config.processSpec.sampleRate,
            static_cast<int>(config.processSpec.maximumBlockSize));
        if (openError.isNotEmpty()) {
            return false;
        }

        callback_ = &callback;
        activeConfig_ = config;
        if (inputIndex.has_value()) {
            selectedInputIndex_ = *inputIndex;
        }
        selectedOutputIndex_ = *outputIndex;
        device_ = std::move(candidate);
        device_->start(&callbackAdapter_);
        if (!device_->isPlaying()) {
            stop();
            return false;
        }
        running_.store(true, std::memory_order_release);
        updateLatency(device_.get());
        return true;
    }

    void stop() noexcept override {
        running_.store(false, std::memory_order_release);
        if (device_ != nullptr) {
            device_->stop();
            device_->close();
            device_.reset();
        }
        callback_ = nullptr;
        estimatedLatencyMs_.store(0.0, std::memory_order_relaxed);
    }

    [[nodiscard]] bool selectDevice(
        bool forInput,
        std::uint32_t index) override {
        if (deviceType_ == nullptr || index >= deviceCount(forInput)) {
            return false;
        }

        const auto previousInput = selectedInputIndex_;
        const auto previousOutput = selectedOutputIndex_;
        const auto wasRunning = running_.load(std::memory_order_acquire);
        auto* const callback = callback_;
        auto restartConfig = activeConfig_;
        restartConfig.inputDeviceIndex = kDefaultAudioDevice;
        restartConfig.outputDeviceIndex = kDefaultAudioDevice;

        (forInput ? selectedInputIndex_ : selectedOutputIndex_) = index;
        if (!wasRunning) {
            return true;
        }
        if (callback != nullptr && start(restartConfig, *callback)) {
            return true;
        }

        selectedInputIndex_ = previousInput;
        selectedOutputIndex_ = previousOutput;
        if (callback != nullptr) {
            static_cast<void>(start(restartConfig, *callback));
        }
        return false;
    }

    [[nodiscard]] bool isRunning() const noexcept override {
        return running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] double estimatedLatencyMs() const noexcept override {
        return estimatedLatencyMs_.load(std::memory_order_relaxed);
    }

private:
    class CallbackAdapter final : public juce::AudioIODeviceCallback {
    public:
        explicit CallbackAdapter(JuceWasapiBackend& owner) noexcept
            : owner_(owner) {}

        void audioDeviceIOCallbackWithContext(
            const float* const* inputChannelData,
            int numInputChannels,
            float* const* outputChannelData,
            int numOutputChannels,
            int numSamples,
            const juce::AudioIODeviceCallbackContext&) override {
            if (numSamples <= 0 || numOutputChannels <= 0) {
                return;
            }
            if (numOutputChannels > static_cast<int>(kMaximumAudioChannels)) {
                clearOutputs(outputChannelData, numOutputChannels, numSamples);
                return;
            }

            std::array<const float*, kMaximumAudioChannels> inputs{};
            std::array<float*, kMaximumAudioChannels> outputs{};
            const auto inputCount = std::clamp(
                numInputChannels, 0, static_cast<int>(kMaximumAudioChannels));
            for (int channel = 0; channel < inputCount; ++channel) {
                inputs[static_cast<std::size_t>(channel)] =
                    inputChannelData == nullptr ? nullptr : inputChannelData[channel];
            }
            for (int channel = 0; channel < numOutputChannels; ++channel) {
                outputs[static_cast<std::size_t>(channel)] =
                    outputChannelData == nullptr ? nullptr : outputChannelData[channel];
            }

            auto* callback = owner_.callback_;
            if (callback == nullptr) {
                clearOutputs(outputChannelData, numOutputChannels, numSamples);
                return;
            }
            callback->process(
                {inputCount > 0 ? inputs.data() : nullptr,
                 static_cast<std::uint32_t>(inputCount),
                 static_cast<std::uint32_t>(numSamples)},
                {outputs.data(),
                 static_cast<std::uint32_t>(numOutputChannels),
                 static_cast<std::uint32_t>(numSamples)});
        }

        void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
            owner_.updateLatency(device);
            owner_.running_.store(true, std::memory_order_release);
        }

        void audioDeviceStopped() override {
            owner_.running_.store(false, std::memory_order_release);
        }

        void audioDeviceError(const juce::String&) override {
            owner_.running_.store(false, std::memory_order_release);
        }

    private:
        static void clearOutputs(
            float* const* outputs,
            int channelCount,
            int sampleCount) noexcept {
            if (outputs == nullptr) {
                return;
            }
            for (int channel = 0; channel < channelCount; ++channel) {
                if (outputs[channel] != nullptr) {
                    std::fill_n(outputs[channel], sampleCount, 0.0f);
                }
            }
        }

        JuceWasapiBackend& owner_;
    };

    [[nodiscard]] std::optional<std::uint32_t> resolveIndex(
        bool forInput,
        std::uint32_t requestedIndex,
        std::uint32_t selectedIndex) const {
        const auto names = deviceType_->getDeviceNames(forInput);
        auto index = requestedIndex == kDefaultAudioDevice
            ? selectedIndex
            : requestedIndex;
        if (index == kDefaultAudioDevice) {
            const auto defaultIndex = deviceType_->getDefaultDeviceIndex(forInput);
            if (defaultIndex < 0) {
                return {};
            }
            index = static_cast<std::uint32_t>(defaultIndex);
        }
        if (index >= static_cast<std::uint32_t>(names.size())) {
            return {};
        }
        return index;
    }

    static void copyUtf8Name(
        const juce::String& source,
        char* destination,
        std::size_t capacity) noexcept {
        if (capacity == 0) {
            return;
        }
        destination[0] = '\0';
        const auto* utf8 = source.toRawUTF8();
        if (utf8 == nullptr) {
            return;
        }

        std::size_t written = 0;
        while (utf8[0] != '\0') {
            const auto lead = static_cast<unsigned char>(utf8[0]);
            const std::size_t sequenceLength =
                lead < 0x80 ? 1
                : (lead & 0xe0) == 0xc0 ? 2
                : (lead & 0xf0) == 0xe0 ? 3
                : (lead & 0xf8) == 0xf0 ? 4
                : 1;
            if (written + sequenceLength >= capacity) {
                break;
            }
            bool complete = true;
            for (std::size_t byte = 1; byte < sequenceLength; ++byte) {
                if (utf8[byte] == '\0') {
                    complete = false;
                    break;
                }
            }
            if (!complete) {
                break;
            }
            std::memcpy(destination + written, utf8, sequenceLength);
            written += sequenceLength;
            utf8 += sequenceLength;
        }
        destination[written] = '\0';
    }

    void updateLatency(juce::AudioIODevice* device) noexcept {
        if (device == nullptr) {
            estimatedLatencyMs_.store(0.0, std::memory_order_relaxed);
            return;
        }
        const auto sampleRate = device->getCurrentSampleRate();
        if (sampleRate <= 0.0) {
            estimatedLatencyMs_.store(0.0, std::memory_order_relaxed);
            return;
        }
        const auto latencySamples = std::max(
            0,
            device->getInputLatencyInSamples()
                + device->getOutputLatencyInSamples());
        estimatedLatencyMs_.store(
            1000.0 * static_cast<double>(latencySamples) / sampleRate,
            std::memory_order_relaxed);
    }

    std::unique_ptr<juce::AudioIODeviceType> deviceType_;
    std::unique_ptr<juce::AudioIODevice> device_;
    CallbackAdapter callbackAdapter_;
    AudioDeviceCallback* callback_ = nullptr;
    AudioDeviceConfig activeConfig_{};
    std::uint32_t selectedInputIndex_ = kDefaultAudioDevice;
    std::uint32_t selectedOutputIndex_ = kDefaultAudioDevice;
    std::atomic<bool> running_{false};
    std::atomic<double> estimatedLatencyMs_{0.0};
};

} // namespace

std::unique_ptr<AudioDeviceBackend> createPlatformAudioBackend() {
    return std::make_unique<JuceWasapiBackend>();
}

} // namespace auralis::audio
