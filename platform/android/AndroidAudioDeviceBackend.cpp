#include <auralis/audio/AudioDeviceBackend.h>

#include "AndroidAudioHost.h"

#include <oboe/Oboe.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace auralis::audio {
namespace {

constexpr std::uint32_t kMaximumChannels = 2;
constexpr std::uint32_t kMinimumRingCapacityFrames = 4096;
constexpr std::uint32_t kMaximumCallbackFrames = 4096;
constexpr auto kReconnectInterval = std::chrono::milliseconds(500);

std::atomic<bool> gHostForeground{true};
std::atomic<bool> gHostHasAudioFocus{true};
std::mutex gBackendRegistryMutex;
std::vector<class AndroidAudioBackend*> gBackendRegistry;

[[nodiscard]] std::uint32_t nextPowerOfTwo(std::uint32_t value) noexcept {
    if (value <= 1) return 1;
    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return value + 1;
}

class StereoFrameRing final {
public:
    void prepare(std::uint32_t requestedCapacity) {
        const auto capacity = nextPowerOfTwo(
            std::max(requestedCapacity, kMinimumRingCapacityFrames));
        samples_.assign(static_cast<std::size_t>(capacity) * 2, 0.0f);
        mask_ = capacity - 1;
        readFrame_.store(0, std::memory_order_relaxed);
        writtenFrame_.store(0, std::memory_order_relaxed);
    }

    void reset() noexcept {
        readFrame_.store(0, std::memory_order_relaxed);
        writtenFrame_.store(0, std::memory_order_relaxed);
    }

    void write(const float* left, const float* right, std::uint32_t frames) noexcept {
        if (samples_.empty() || left == nullptr || right == nullptr) return;
        const auto writeFrame = writtenFrame_.load(std::memory_order_relaxed);
        const auto readFrame = readFrame_.load(std::memory_order_acquire);
        const auto capacity = mask_ + 1;
        const auto queued = static_cast<std::uint32_t>(writeFrame - readFrame);
        const auto writable = capacity - std::min(capacity, queued);
        const auto count = std::min(frames, writable);
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            const auto index =
                (static_cast<std::uint32_t>(writeFrame + frame) & mask_) * 2;
            samples_[index] = left[frame];
            samples_[index + 1] = right[frame];
        }
        writtenFrame_.store(writeFrame + count, std::memory_order_release);
    }

    void read(
        float* left,
        float* right,
        std::uint32_t frames) noexcept {
        if (left == nullptr || right == nullptr) return;
        const auto readFrame = readFrame_.load(std::memory_order_relaxed);
        const auto writeFrame = writtenFrame_.load(std::memory_order_acquire);
        const auto available = std::min(writeFrame - readFrame, frames);
        for (std::uint32_t frame = 0; frame < available; ++frame) {
            const auto index =
                (static_cast<std::uint32_t>(readFrame + frame) & mask_) * 2;
            left[frame] = samples_[index];
            right[frame] = samples_[index + 1];
        }
        std::fill(left + available, left + frames, 0.0f);
        std::fill(right + available, right + frames, 0.0f);
        readFrame_.store(readFrame + available, std::memory_order_release);
    }

    [[nodiscard]] std::uint32_t queuedFrames() const noexcept {
        const auto readFrame = readFrame_.load(std::memory_order_acquire);
        const auto writeFrame = writtenFrame_.load(std::memory_order_acquire);
        return std::min(writeFrame - readFrame, mask_ + 1);
    }

private:
    std::vector<float> samples_;
    std::uint32_t mask_ = 0;
    alignas(64) std::atomic<std::uint32_t> readFrame_{0};
    alignas(64) std::atomic<std::uint32_t> writtenFrame_{0};
};

class AndroidAudioBackend final
    : public AudioDeviceBackend
    , public oboe::AudioStreamDataCallback
    , public oboe::AudioStreamErrorCallback {
public:
    AndroidAudioBackend() {
        {
            std::lock_guard lock(gBackendRegistryMutex);
            gBackendRegistry.push_back(this);
        }
        reconnectThread_ = std::thread([this] { reconnectLoop(); });
    }

    ~AndroidAudioBackend() override {
        stop();
        {
            std::lock_guard lock(gBackendRegistryMutex);
            std::erase(gBackendRegistry, this);
        }
        {
            std::lock_guard lock(workerMutex_);
            workerExit_ = true;
            workerPending_ = true;
        }
        workerCondition_.notify_one();
        if (reconnectThread_.joinable()) reconnectThread_.join();
    }

    [[nodiscard]] std::uint32_t deviceCount(bool forInput) const override {
        const auto devices = android::deviceSnapshot(forInput);
        return static_cast<std::uint32_t>(std::min<std::size_t>(
            devices.size(),
            std::numeric_limits<std::uint32_t>::max()));
    }

    [[nodiscard]] bool getDeviceInfo(
        bool forInput,
        std::uint32_t index,
        AudioDeviceInfo& destination) const override {
        const auto devices = android::deviceSnapshot(forInput);
        if (index >= devices.size()) return false;

        const auto& source = devices[index];
        destination = {};
        destination.index = index;
        destination.inputChannels = forInput ? source.channelCount : 0;
        destination.outputChannels = forInput ? 0 : source.channelCount;
        destination.isDefault = source.isDefault;
        const auto length = std::min(source.name.size(), sizeof(destination.name) - 1);
        std::copy_n(source.name.data(), length, destination.name);
        destination.name[length] = '\0';
        return true;
    }

    [[nodiscard]] bool start(
        const AudioDeviceConfig& config,
        AudioDeviceCallback& callback) override {
        if (!std::isfinite(config.processSpec.sampleRate)
            || config.processSpec.sampleRate < 8000.0
            || config.processSpec.sampleRate > 384000.0
            || config.processSpec.maximumBlockSize == 0
            || config.processSpec.channelCount == 0
            || config.processSpec.channelCount > kMaximumChannels) {
            return false;
        }
        if (!android::hasRecordPermission()
            || !android::requestAudioFocus()) {
            return false;
        }

        const auto inputDevice = config.inputDeviceIndex == kDefaultAudioDevice
            ? android::DeviceDescriptor{}
            : selectedDevice(true, config.inputDeviceIndex);
        const auto outputDevice = config.outputDeviceIndex == kDefaultAudioDevice
            ? android::DeviceDescriptor{}
            : selectedDevice(false, config.outputDeviceIndex);
        if ((config.inputDeviceIndex != kDefaultAudioDevice && inputDevice.id < 0)
            || (config.outputDeviceIndex != kDefaultAudioDevice
                && outputDevice.id < 0)) {
            android::abandonAudioFocus();
            return false;
        }

        std::lock_guard lock(lifecycleMutex_);
        closeStreams();
        reconfigurePending_ = false;
        requestedConfig_ = config;
        requestedCallback_ = &callback;
        if (config.inputDeviceIndex != kDefaultAudioDevice) {
            selectedInputDevice_ = config.inputDeviceIndex;
            selectedInputDeviceId_ = inputDevice.id;
            selectedInputDeviceChannels_ = inputDevice.channelCount;
            selectedInputDeviceName_ = inputDevice.name;
        }
        if (config.outputDeviceIndex != kDefaultAudioDevice) {
            selectedOutputDevice_ = config.outputDeviceIndex;
            selectedOutputDeviceId_ = outputDevice.id;
            selectedOutputDeviceChannels_ = outputDevice.channelCount;
            selectedOutputDeviceName_ = outputDevice.name;
        }
        requestedConfig_.inputDeviceIndex = selectedInputDevice_;
        requestedConfig_.outputDeviceIndex = selectedOutputDevice_;
        requestedRunning_.store(true, std::memory_order_release);
        if (!hostAllowsAudio()) {
            requestedRunning_.store(false, std::memory_order_release);
            requestedCallback_ = nullptr;
            android::abandonAudioFocus();
            return false;
        }
        if (openAndStartStreams()) {
            return true;
        }

        requestedRunning_.store(false, std::memory_order_release);
        closeStreams();
        android::abandonAudioFocus();
        return false;
    }

    void stop() noexcept override {
        requestedRunning_.store(false, std::memory_order_release);
        {
            std::lock_guard lock(lifecycleMutex_);
            closeStreams();
            reconfigurePending_ = false;
            requestedCallback_ = nullptr;
        }
        android::abandonAudioFocus();
        scheduleWorker();
    }

    [[nodiscard]] bool selectDevice(
        bool forInput,
        std::uint32_t index) override {
        android::DeviceDescriptor descriptor;
        if (index != kDefaultAudioDevice) {
            const auto devices = android::deviceSnapshot(forInput);
            if (index >= devices.size()) return false;
            descriptor = devices[index];
            if (descriptor.id < 0) return false;
        }

        const bool wasRunning =
            requestedRunning_.load(std::memory_order_acquire);
        {
            std::lock_guard lock(lifecycleMutex_);
            if (forInput) {
                selectedInputDevice_ = index;
                requestedConfig_.inputDeviceIndex = index;
                selectedInputDeviceId_ = descriptor.id;
                selectedInputDeviceChannels_ = descriptor.channelCount;
                selectedInputDeviceName_ = descriptor.name;
            } else {
                selectedOutputDevice_ = index;
                requestedConfig_.outputDeviceIndex = index;
                selectedOutputDeviceId_ = descriptor.id;
                selectedOutputDeviceChannels_ = descriptor.channelCount;
                selectedOutputDeviceName_ = descriptor.name;
            }
            if (wasRunning) {
                reconfigurePending_ = true;
            }
        }
        if (wasRunning) {
            scheduleWorker();
        }
        return true;
    }

    [[nodiscard]] bool isRunning() const noexcept override {
        return running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] double estimatedLatencyMs() const noexcept override {
        const double sampleRate = sampleRateHz_.load(std::memory_order_relaxed);
        const double deviceEstimate =
            estimatedLatencyMs_.load(std::memory_order_relaxed);
        if (sampleRate <= 0.0) return deviceEstimate;
        const double queuedFrames =
            static_cast<double>(inputRing_.queuedFrames());
        return deviceEstimate + queuedFrames * 1000.0 / sampleRate;
    }

    oboe::DataCallbackResult onAudioReady(
        oboe::AudioStream* stream,
        void* audioData,
        std::int32_t frameCount) override {
        if (audioData == nullptr || frameCount <= 0) {
            return oboe::DataCallbackResult::Stop;
        }
        if (stream->getDirection() == oboe::Direction::Input) {
            captureInput(stream, audioData, static_cast<std::uint32_t>(frameCount));
        } else {
            renderOutput(stream, audioData, static_cast<std::uint32_t>(frameCount));
        }
        return oboe::DataCallbackResult::Continue;
    }

    void onErrorAfterClose(
        oboe::AudioStream*,
        oboe::Result) override {
        if (requestedRunning_.load(std::memory_order_acquire)) {
            running_.store(false, std::memory_order_release);
            scheduleWorker();
        }
    }

    void notifyHostStateChanged() noexcept {
        if (!hostAllowsAudio()) {
            scheduleWorker();
        } else if (requestedRunning_.load(std::memory_order_acquire)) {
            scheduleWorker();
        }
    }

private:
    [[nodiscard]] bool hostAllowsAudio() const noexcept {
        return gHostForeground.load(std::memory_order_acquire)
            && gHostHasAudioFocus.load(std::memory_order_acquire);
    }

    [[nodiscard]] android::DeviceDescriptor selectedDevice(
        bool forInput,
        std::uint32_t index) const {
        if (index == kDefaultAudioDevice) return {};
        const auto devices = android::deviceSnapshot(forInput);
        if (index >= devices.size()) return {};
        return devices[index];
    }

    [[nodiscard]] std::int32_t refreshSelectedDeviceId(bool forInput) {
        auto& selectedId =
            forInput ? selectedInputDeviceId_ : selectedOutputDeviceId_;
        auto& selectedName =
            forInput ? selectedInputDeviceName_ : selectedOutputDeviceName_;
        auto& selectedChannels =
            forInput ? selectedInputDeviceChannels_ : selectedOutputDeviceChannels_;
        if (selectedId < 0) return selectedId;

        const auto devices = android::deviceSnapshot(forInput);
        const auto byId = std::find_if(
            devices.begin(),
            devices.end(),
            [&selectedId](const auto& device) {
                return device.id == selectedId;
            });
        const auto found = byId != devices.end()
            ? byId
            : std::find_if(
                devices.begin(),
                devices.end(),
                [&selectedName](const auto& device) {
                    return !selectedName.empty()
                        && device.name == selectedName;
                });
        if (found != devices.end()) {
            selectedId = found->id;
            selectedName = found->name;
            selectedChannels = found->channelCount;
        }
        return selectedId;
    }

    [[nodiscard]] std::uint32_t streamChannelCount(
        bool forInput,
        std::int32_t deviceId) const {
        const auto requested = std::clamp(
            requestedConfig_.processSpec.channelCount,
            1u,
            kMaximumChannels);
        const auto deviceChannels =
            forInput ? selectedInputDeviceChannels_ : selectedOutputDeviceChannels_;
        if (deviceId < 0 || deviceChannels == 0) return requested;
        return std::clamp(
            std::min(requested, deviceChannels),
            1u,
            kMaximumChannels);
    }

    [[nodiscard]] bool openStream(
        oboe::Direction direction,
        std::uint32_t channels,
        std::int32_t deviceId,
        oboe::AudioFormat format,
        std::shared_ptr<oboe::AudioStream>& destination) {
        oboe::AudioStreamBuilder builder;
        builder.setDirection(direction);
        builder.setSharingMode(oboe::SharingMode::Shared);
        builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
        builder.setSampleRate(
            static_cast<std::int32_t>(
                std::lround(requestedConfig_.processSpec.sampleRate)));
        builder.setChannelCount(static_cast<std::int32_t>(channels));
        builder.setFormat(format);
        builder.setFramesPerDataCallback(static_cast<std::int32_t>(
            std::min(
                requestedConfig_.processSpec.maximumBlockSize,
                kMaximumCallbackFrames)));
        builder.setDataCallback(this);
        builder.setErrorCallback(this);
        if (deviceId >= 0) builder.setDeviceId(deviceId);
        if (direction == oboe::Direction::Input) {
            builder.setInputPreset(oboe::InputPreset::VoiceCommunication);
        } else {
            builder.setUsage(oboe::Usage::VoiceCommunication);
            builder.setContentType(oboe::ContentType::Speech);
        }

        if (builder.openStream(destination) != oboe::Result::OK
            || destination == nullptr) {
            destination.reset();
            return false;
        }
        return true;
    }

    [[nodiscard]] bool openStreamWithFormatFallback(
        oboe::Direction direction,
        std::uint32_t channels,
        std::int32_t deviceId,
        std::shared_ptr<oboe::AudioStream>& destination) {
        if (openStream(
                direction,
                channels,
                deviceId,
                oboe::AudioFormat::Float,
                destination)) {
            return true;
        }
        return openStream(
            direction,
            channels,
            deviceId,
            oboe::AudioFormat::I16,
            destination);
    }

    [[nodiscard]] bool openAndStartStreams() {
        const auto maxFrames = std::clamp(
            requestedConfig_.processSpec.maximumBlockSize,
            1u,
            kMaximumCallbackFrames);
        inputLeft_.assign(maxFrames, 0.0f);
        inputRight_.assign(maxFrames, 0.0f);
        outputLeft_.assign(maxFrames, 0.0f);
        outputRight_.assign(maxFrames, 0.0f);
        outputPointers_[0] = outputLeft_.data();
        outputPointers_[1] = outputRight_.data();
        constInputPointers_[0] = inputLeft_.data();
        constInputPointers_[1] = inputRight_.data();

        const auto ringCapacity = nextPowerOfTwo(std::max(
            kMinimumRingCapacityFrames,
            maxFrames * 8u));
        inputRing_.prepare(ringCapacity);
        auto* callback = requestedCallback_;
        if (callback == nullptr) return false;

        const auto inputDeviceId = refreshSelectedDeviceId(true);
        const auto outputDeviceId = refreshSelectedDeviceId(false);
        const auto inputChannels =
            streamChannelCount(true, inputDeviceId);
        const auto outputChannels =
            streamChannelCount(false, outputDeviceId);
        if (!openStreamWithFormatFallback(
                oboe::Direction::Input,
                inputChannels,
                inputDeviceId,
                inputStream_)) {
            return false;
        }
        if (!openStreamWithFormatFallback(
                oboe::Direction::Output,
                outputChannels,
                outputDeviceId,
                outputStream_)) {
            closeStreams();
            return false;
        }

        inputChannelCount_ = static_cast<std::uint32_t>(
            inputStream_->getChannelCount());
        outputChannelCount_ = static_cast<std::uint32_t>(
            outputStream_->getChannelCount());
        inputRing_.reset();
        if (inputStream_->requestStart() != oboe::Result::OK) {
            closeStreams();
            return false;
        }
        if (outputStream_->requestStart() != oboe::Result::OK) {
            closeStreams();
            return false;
        }

        const double rate = static_cast<double>(outputStream_->getSampleRate());
        const double bufferedFrames =
            static_cast<double>(inputStream_->getBufferSizeInFrames())
            + static_cast<double>(outputStream_->getBufferSizeInFrames());
        estimatedLatencyMs_.store(
            rate > 0.0 ? bufferedFrames * 1000.0 / rate : 0.0,
            std::memory_order_relaxed);
        sampleRateHz_.store(rate, std::memory_order_relaxed);
        running_.store(true, std::memory_order_release);
        return true;
    }

    void closeStreams() noexcept {
        running_.store(false, std::memory_order_release);
        if (outputStream_ != nullptr) {
            outputStream_->requestStop();
            outputStream_->close();
            outputStream_.reset();
        }
        if (inputStream_ != nullptr) {
            inputStream_->requestStop();
            inputStream_->close();
            inputStream_.reset();
        }
        estimatedLatencyMs_.store(0.0, std::memory_order_relaxed);
        sampleRateHz_.store(0.0, std::memory_order_relaxed);
    }

    void captureInput(
        const oboe::AudioStream* stream,
        const void* data,
        std::uint32_t frameCount) noexcept {
        const auto channels = static_cast<std::uint32_t>(stream->getChannelCount());
        std::array<float, kMaximumCallbackFrames> leftScratch;
        std::array<float, kMaximumCallbackFrames> rightScratch;
        const float* source = nullptr;
        const float* sourceRight = nullptr;

        // Oboe may deliver a larger block than the requested callback size.
        // Split it into fixed stack chunks so the realtime thread never allocates.
        std::uint32_t offset = 0;
        while (offset < frameCount) {
            const auto count = std::min(
                frameCount - offset,
                kMaximumCallbackFrames);
            if (stream->getFormat() == oboe::AudioFormat::Float) {
                const auto* samples = static_cast<const float*>(data)
                    + static_cast<std::size_t>(offset) * channels;
                for (std::uint32_t frame = 0; frame < count; ++frame) {
                    leftScratch[frame] = samples[
                        static_cast<std::size_t>(frame) * channels];
                }
                source = leftScratch.data();
                if (channels > 1) {
                    for (std::uint32_t frame = 0; frame < count; ++frame) {
                        rightScratch[frame] = samples[
                            static_cast<std::size_t>(frame) * channels + 1];
                    }
                    sourceRight = rightScratch.data();
                } else {
                    sourceRight = source;
                }
            } else if (stream->getFormat() == oboe::AudioFormat::I16) {
                const auto* samples = static_cast<const std::int16_t*>(data)
                    + static_cast<std::size_t>(offset) * channels;
                for (std::uint32_t frame = 0; frame < count; ++frame) {
                    leftScratch[frame] = static_cast<float>(
                        samples[static_cast<std::size_t>(frame) * channels])
                        / 32768.0f;
                }
                source = leftScratch.data();
                if (channels > 1) {
                    for (std::uint32_t frame = 0; frame < count; ++frame) {
                        rightScratch[frame] = static_cast<float>(
                            samples[static_cast<std::size_t>(frame) * channels + 1])
                            / 32768.0f;
                    }
                    sourceRight = rightScratch.data();
                } else {
                    sourceRight = source;
                }
            } else {
                break;
            }

            inputRing_.write(source, sourceRight, count);
            offset += count;
        }
    }

    void renderOutput(
        oboe::AudioStream* stream,
        void* data,
        std::uint32_t frameCount) noexcept {
        const auto streamChannels =
            static_cast<std::uint32_t>(stream->getChannelCount());
        const auto processChannels = std::min(
            outputChannelCount_,
            kMaximumChannels);
        if (requestedCallback_ == nullptr
            || !hostAllowsAudio()
            || !requestedRunning_.load(std::memory_order_acquire)) {
            silence(
                data,
                frameCount,
                streamChannels,
                stream->getFormat(),
                static_cast<std::size_t>(stream->getBytesPerFrame()));
            return;
        }

        std::uint32_t offset = 0;
        while (offset < frameCount) {
            const auto count = std::min(
                frameCount - offset,
                static_cast<std::uint32_t>(outputLeft_.size()));
            inputRing_.read(inputLeft_.data(), inputRight_.data(), count);
            const ConstAudioBlockView input{
                constInputPointers_.data(),
                inputChannelCount_,
                count,
            };
            const AudioBlockView output{
                outputPointers_.data(),
                processChannels,
                count,
            };
            requestedCallback_->process(input, output);
            writeOutput(
                stream,
                data,
                offset,
                count,
                streamChannels,
                processChannels);
            offset += count;
        }
    }

    void writeOutput(
        oboe::AudioStream* stream,
        void* data,
        std::uint32_t offset,
        std::uint32_t frameCount,
        std::uint32_t streamChannels,
        std::uint32_t processChannels) noexcept {
        if (stream->getFormat() == oboe::AudioFormat::Float) {
            auto* samples = static_cast<float*>(data)
                + static_cast<std::size_t>(offset) * streamChannels;
            for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
                const float left = outputLeft_[frame];
                const float right = processChannels > 1
                    ? outputRight_[frame]
                    : left;
                samples[static_cast<std::size_t>(frame) * streamChannels] = left;
                if (streamChannels > 1) {
                    samples[static_cast<std::size_t>(frame) * streamChannels + 1] =
                        right;
                }
                for (std::uint32_t channel = 2; channel < streamChannels; ++channel) {
                    samples[static_cast<std::size_t>(frame) * streamChannels + channel] =
                        0.0f;
                }
            }
        } else if (stream->getFormat() == oboe::AudioFormat::I16) {
            auto* samples = static_cast<std::int16_t*>(data)
                + static_cast<std::size_t>(offset) * streamChannels;
            for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
                const auto toInt16 = [](float value) {
                    const auto bounded = std::clamp(value, -1.0f, 1.0f);
                    return static_cast<std::int16_t>(
                        std::lrintf(bounded * 32767.0f));
                };
                const auto left = toInt16(outputLeft_[frame]);
                const auto right = toInt16(processChannels > 1
                    ? outputRight_[frame]
                    : outputLeft_[frame]);
                samples[static_cast<std::size_t>(frame) * streamChannels] = left;
                if (streamChannels > 1) {
                    samples[static_cast<std::size_t>(frame) * streamChannels + 1] =
                        right;
                }
                for (std::uint32_t channel = 2; channel < streamChannels; ++channel) {
                    samples[static_cast<std::size_t>(frame) * streamChannels + channel] =
                        0;
                }
            }
        } else {
            silence(
                static_cast<std::byte*>(data)
                    + static_cast<std::size_t>(offset) * streamChannels
                        * static_cast<std::size_t>(stream->getBytesPerFrame()),
                frameCount,
                streamChannels,
                stream->getFormat(),
                static_cast<std::size_t>(stream->getBytesPerFrame()));
        }
    }

    static void silence(
        void* data,
        std::uint32_t frames,
        std::uint32_t channels,
        oboe::AudioFormat format,
        std::size_t bytesPerFrame) noexcept {
        if (format == oboe::AudioFormat::Float) {
            std::fill_n(
                static_cast<float*>(data),
                static_cast<std::size_t>(frames) * channels,
                0.0f);
        } else if (format == oboe::AudioFormat::I16) {
            std::fill_n(
                static_cast<std::int16_t*>(data),
                static_cast<std::size_t>(frames) * channels,
                0);
        } else {
            std::fill_n(
                static_cast<std::byte*>(data),
                static_cast<std::size_t>(frames) * bytesPerFrame,
                std::byte{0});
        }
    }

    void scheduleWorker() noexcept {
        {
            std::lock_guard lock(workerMutex_);
            workerPending_ = true;
        }
        workerCondition_.notify_one();
    }

    void reconnectLoop() noexcept {
        std::unique_lock workerLock(workerMutex_);
        while (!workerExit_) {
            workerCondition_.wait(workerLock, [this] {
                return workerExit_ || workerPending_;
            });
            if (workerExit_) break;
            workerPending_ = false;
            workerLock.unlock();

            {
                std::lock_guard lifecycleLock(lifecycleMutex_);
                if (!requestedRunning_.load(std::memory_order_acquire)) {
                    closeStreams();
                } else if (!hostAllowsAudio()) {
                    closeStreams();
                } else if (reconfigurePending_
                    || !running_.load(std::memory_order_acquire)) {
                    reconfigurePending_ = false;
                    closeStreams();
                    if (!openAndStartStreams()) {
                        closeStreams();
                    }
                }
            }

            workerLock.lock();
            if (!workerExit_
                && requestedRunning_.load(std::memory_order_acquire)
                && hostAllowsAudio()
                && !running_.load(std::memory_order_acquire)) {
                workerCondition_.wait_for(
                    workerLock,
                    kReconnectInterval,
                    [this] { return workerExit_ || workerPending_; });
                if (!workerExit_) workerPending_ = true;
            }
        }
    }

    mutable std::mutex lifecycleMutex_;
    std::shared_ptr<oboe::AudioStream> inputStream_;
    std::shared_ptr<oboe::AudioStream> outputStream_;
    AudioDeviceConfig requestedConfig_{};
    AudioDeviceCallback* requestedCallback_ = nullptr;
    std::uint32_t selectedInputDevice_ = kDefaultAudioDevice;
    std::uint32_t selectedOutputDevice_ = kDefaultAudioDevice;
    std::int32_t selectedInputDeviceId_ = -1;
    std::int32_t selectedOutputDeviceId_ = -1;
    std::uint32_t selectedInputDeviceChannels_ = 0;
    std::uint32_t selectedOutputDeviceChannels_ = 0;
    std::string selectedInputDeviceName_;
    std::string selectedOutputDeviceName_;
    bool reconfigurePending_ = false;
    std::uint32_t inputChannelCount_ = 1;
    std::uint32_t outputChannelCount_ = 2;
    StereoFrameRing inputRing_;
    std::vector<float> inputLeft_;
    std::vector<float> inputRight_;
    std::vector<float> outputLeft_;
    std::vector<float> outputRight_;
    std::array<float*, kMaximumChannels> outputPointers_{};
    std::array<const float*, kMaximumChannels> constInputPointers_{
        nullptr, nullptr};
    std::atomic<bool> requestedRunning_{false};
    std::atomic<bool> running_{false};
    std::atomic<double> estimatedLatencyMs_{0.0};
    std::atomic<double> sampleRateHz_{0.0};

    std::mutex workerMutex_;
    std::condition_variable workerCondition_;
    std::thread reconnectThread_;
    bool workerPending_ = false;
    bool workerExit_ = false;
};

} // namespace

void android::setHostState(
    bool isForeground,
    bool hasAudioFocus) noexcept {
    gHostForeground.store(isForeground, std::memory_order_release);
    gHostHasAudioFocus.store(hasAudioFocus, std::memory_order_release);
    std::lock_guard lock(gBackendRegistryMutex);
    for (auto* backend : gBackendRegistry) {
        backend->notifyHostStateChanged();
    }
}

std::unique_ptr<AudioDeviceBackend> createPlatformAudioBackend() {
    return std::make_unique<AndroidAudioBackend>();
}

} // namespace auralis::audio
