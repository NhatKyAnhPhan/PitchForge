#include <auralis/audio/AudioDeviceBackend.h>
#include <auralis/bridge/auralis_c_api.h>
#include <auralis/mixer/SignalChainFactory.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

struct AuralisMixerState;
extern "C" AuralisStatus auralis_engine_get_mixer_state(
    const AuralisEngine*,
    AuralisMixerState*);
extern "C" AuralisStatus auralis_engine_set_input_gain(
    AuralisEngine*,
    float);
extern "C" AuralisStatus auralis_engine_set_output_gain(
    AuralisEngine*,
    float);
extern "C" AuralisStatus auralis_engine_set_pan(AuralisEngine*, float);
extern "C" AuralisStatus auralis_engine_set_mute(
    AuralisEngine*,
    std::uint32_t);

namespace {

struct MixerStateFields {
    float input_gain_db;
    float output_gain_db;
    float pan;
    std::uint32_t muted;
};

static_assert(sizeof(MixerStateFields) == 16);

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "engine test failed: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

bool near(float actual, float expected, float tolerance = 0.0001f) {
    return std::abs(actual - expected) <= tolerance;
}

class FakeSignalChain final : public auralis::mixer::SignalChain {
public:
    void prepare(const auralis::audio::ProcessSpec& spec) override {
        preparedSpec = spec;
        wasPrepared = true;
    }

    void process(auralis::audio::AudioBlockView) noexcept override {
        ++processCount;
        while (queuedCommandCount > 0) {
            const auto command = queuedCommands[0];
            for (std::size_t i = 1; i < queuedCommandCount; ++i) {
                queuedCommands[i - 1] = queuedCommands[i];
            }
            --queuedCommandCount;
            apply(command);
        }
    }

    [[nodiscard]] bool enqueue(
        const auralis::mixer::ChainCommand& command) noexcept override {
        if (queuedCommandCount == queuedCommands.size()) {
            return false;
        }
        queuedCommands[queuedCommandCount++] = command;
        return true;
    }

    void copySnapshot(
        auralis::mixer::SignalChainSnapshot& destination) const noexcept override {
        destination = snapshot;
    }

    auralis::audio::ProcessSpec preparedSpec{};
    bool wasPrepared = false;
    std::uint32_t processCount = 0;

private:
    void apply(const auralis::mixer::ChainCommand& command) noexcept {
        using auralis::mixer::ChainCommandType;
        using auralis::mixer::EffectSlot;

        auto findSlot = [this, &command]() -> std::uint32_t {
            for (std::uint32_t i = 0; i < snapshot.size; ++i) {
                if (snapshot.slots[i].instanceId == command.instanceId) {
                    return i;
                }
            }
            return snapshot.size;
        };

        switch (command.type) {
        case ChainCommandType::Add: {
            if (snapshot.size == snapshot.slots.size()
                || command.index > snapshot.size) {
                return;
            }
            for (auto i = snapshot.size; i > command.index; --i) {
                snapshot.slots[i] = snapshot.slots[i - 1];
            }
            auto& slot = snapshot.slots[command.index];
            slot = EffectSlot{};
            slot.instanceId = command.instanceId;
            slot.type = command.effectType;
            ++snapshot.size;
            break;
        }
        case ChainCommandType::Remove: {
            const auto index = findSlot();
            if (index == snapshot.size) {
                return;
            }
            for (auto i = index + 1; i < snapshot.size; ++i) {
                snapshot.slots[i - 1] = snapshot.slots[i];
            }
            --snapshot.size;
            break;
        }
        case ChainCommandType::Move: {
            const auto source = findSlot();
            if (source == snapshot.size || command.destinationIndex >= snapshot.size) {
                return;
            }
            const auto slot = snapshot.slots[source];
            if (source < command.destinationIndex) {
                for (auto i = source; i < command.destinationIndex; ++i) {
                    snapshot.slots[i] = snapshot.slots[i + 1];
                }
            } else {
                for (auto i = source; i > command.destinationIndex; --i) {
                    snapshot.slots[i] = snapshot.slots[i - 1];
                }
            }
            snapshot.slots[command.destinationIndex] = slot;
            break;
        }
        case ChainCommandType::SetEnabled:
        case ChainCommandType::SetWet:
        case ChainCommandType::SetParameter: {
            const auto index = findSlot();
            if (index == snapshot.size) {
                return;
            }
            auto& slot = snapshot.slots[index];
            if (command.type == ChainCommandType::SetEnabled) {
                slot.enabled = command.value != 0.0f;
            } else if (command.type == ChainCommandType::SetWet) {
                slot.wet = command.value;
            } else {
                for (std::uint32_t i = 0; i < slot.parameterCount; ++i) {
                    if (slot.parameterIds[i] == command.parameterId) {
                        slot.parameterValues[i] = command.value;
                        return;
                    }
                }
                if (slot.parameterCount < slot.parameterIds.size()) {
                    const auto parameter = slot.parameterCount++;
                    slot.parameterIds[parameter] = command.parameterId;
                    slot.parameterValues[parameter] = command.value;
                }
            }
            break;
        }
        }
    }

    std::array<auralis::mixer::ChainCommand, 64> queuedCommands{};
    std::size_t queuedCommandCount = 0;
    auralis::mixer::SignalChainSnapshot snapshot{};
};

class FakeAudioDeviceBackend final : public auralis::audio::AudioDeviceBackend {
public:
    [[nodiscard]] std::uint32_t deviceCount(bool) const override {
        return 2;
    }

    [[nodiscard]] bool getDeviceInfo(
        bool forInput,
        std::uint32_t index,
        auralis::audio::AudioDeviceInfo& destination) const override {
        if (index >= deviceCount(forInput)) {
            return false;
        }
        destination = {};
        destination.index = index;
        const auto* name = forInput ? "Test Microphone" : "Test Speakers";
        std::memcpy(destination.name, name, std::strlen(name) + 1);
        destination.inputChannels = forInput ? 2 : 0;
        destination.outputChannels = forInput ? 0 : 2;
        destination.isDefault = index == 0;
        return true;
    }

    [[nodiscard]] bool start(
        const auralis::audio::AudioDeviceConfig& config,
        auralis::audio::AudioDeviceCallback& callback) override {
        lastConfig = config;
        callback_ = &callback;
        running_ = true;
        return true;
    }

    void stop() noexcept override {
        running_ = false;
        callback_ = nullptr;
    }

    [[nodiscard]] bool selectDevice(bool forInput, std::uint32_t index) override {
        if (index >= deviceCount(forInput)) {
            return false;
        }
        (forInput ? selectedInput : selectedOutput) = index;
        return true;
    }

    [[nodiscard]] bool isRunning() const noexcept override {
        return running_;
    }

    [[nodiscard]] double estimatedLatencyMs() const noexcept override {
        return 7.5;
    }

    void simulateDeviceStop() noexcept {
        running_ = false;
    }

    void render(
        const float* const* inputs,
        std::uint32_t inputChannels,
        float* const* outputs,
        std::uint32_t outputChannels,
        std::uint32_t sampleCount) {
        check(callback_ != nullptr, "fake device callback was installed");
        callback_->process(
            {inputs, inputChannels, sampleCount},
            {outputs, outputChannels, sampleCount});
    }

    auralis::audio::AudioDeviceConfig lastConfig{};
    std::uint32_t selectedInput = 0;
    std::uint32_t selectedOutput = 0;

private:
    auralis::audio::AudioDeviceCallback* callback_ = nullptr;
    bool running_ = false;
};

FakeAudioDeviceBackend* lastBackend = nullptr;
FakeSignalChain* lastSignalChain = nullptr;

} // namespace

namespace auralis::audio {

std::unique_ptr<AudioDeviceBackend> createPlatformAudioBackend() {
    auto backend = std::make_unique<FakeAudioDeviceBackend>();
    lastBackend = backend.get();
    return backend;
}

} // namespace auralis::audio

namespace auralis::mixer {

std::unique_ptr<SignalChain> createSignalChain() {
    auto chain = std::make_unique<FakeSignalChain>();
    lastSignalChain = chain.get();
    return chain;
}

} // namespace auralis::mixer

int main() {
    check(auralis_api_version() == 1, "C ABI version is one");
    check(auralis_device_count(0) == 2, "output devices enumerate");
    check(auralis_device_count(1) == 2, "input devices enumerate");

    AuralisDeviceInfo deviceInfo{};
    check(auralis_device_get_info(1, 0, &deviceInfo) == AURALIS_STATUS_OK,
          "device info succeeds");
    check(std::strcmp(deviceInfo.name, "Test Microphone") == 0,
          "device name is copied");
    check(deviceInfo.input_channels == 2 && deviceInfo.is_default == 1,
          "device metadata is copied");

    auto* engine = auralis_engine_create();
    check(engine != nullptr, "engine is created");
    check(auralis_engine_get_mixer_state(engine, nullptr)
              == AURALIS_STATUS_INVALID_ARGUMENT,
          "mixer state validates destination");
    AuralisMeters meters{};
    check(auralis_engine_get_meters(engine, &meters) == AURALIS_STATUS_OK,
          "initial meter snapshot succeeds");
    check(meters.estimated_latency_ms == 0.0,
          "latency is zero before a stream starts");

    AuralisAudioConfig invalidConfig{0.0, 128, 2, 2};
    check(auralis_engine_start(engine, &invalidConfig)
              == AURALIS_STATUS_INVALID_ARGUMENT,
          "invalid stream configuration is rejected");
    AuralisAudioConfig config{48000.0, 128, 2, 2};
    check(auralis_engine_start(engine, &config) == AURALIS_STATUS_OK,
          "engine starts with a valid configuration");
    check(auralis_engine_is_running(engine) == 1,
          "running state follows the backend");
    check(lastSignalChain != nullptr && lastSignalChain->wasPrepared,
          "signal chain is prepared before the stream starts");
    check(lastSignalChain->preparedSpec.sampleRate == 48000.0,
          "signal chain receives configured sample rate");
    check(lastBackend != nullptr
              && lastBackend->lastConfig.processSpec.maximumBlockSize == 128,
          "backend receives configured block size");
    check(auralis_engine_select_device(engine, 1, 1) == AURALIS_STATUS_OK,
          "input selection succeeds");
    check(lastBackend->selectedInput == 1,
          "backend receives input selection");
    check(auralis_engine_select_device(engine, 0, 7)
              == AURALIS_STATUS_INVALID_ARGUMENT,
          "out-of-range device selection is rejected");

    const std::array<float, 4> inLeft{0.25f, -0.25f, 0.25f, -0.25f};
    const std::array<float, 4> inRight{0.5f, -0.5f, 0.5f, -0.5f};
    const float* inputChannels[]{inLeft.data(), inRight.data()};
    std::array<float, 4> outLeft{};
    std::array<float, 4> outRight{};
    float* outputChannels[]{outLeft.data(), outRight.data()};

    check(auralis_engine_set_input_gain(engine, 6.0206f) == AURALIS_STATUS_OK,
          "input gain command succeeds");
    check(auralis_engine_set_pan(engine, -1.0f) == AURALIS_STATUS_OK,
          "pan command succeeds");
    lastBackend->render(inputChannels, 2, outputChannels, 2, 4);
    check(near(outLeft[0], 0.5f) && near(outRight[0], 0.0f),
          "input gain and hard-left pan affect callback samples");
    check(auralis_engine_get_meters(engine, &meters) == AURALIS_STATUS_OK,
          "meter snapshot succeeds after callback");
    check(near(meters.input_peak_left, 0.25f)
              && near(meters.output_peak_left, 0.5f),
          "meters cover input and post-mixer output");
    check(!meters.clipping && near(
              static_cast<float>(meters.estimated_latency_ms), 7.5f),
          "clipping and estimated latency are reported");

    check(auralis_engine_set_output_gain(engine, -6.0206f) == AURALIS_STATUS_OK,
          "output gain command succeeds");
    check(auralis_engine_set_pan(engine, 0.0f) == AURALIS_STATUS_OK,
          "center pan command succeeds");
    lastBackend->render(inputChannels, 2, outputChannels, 2, 4);
    check(near(outLeft[0], 0.25f) && near(outRight[0], 0.5f),
          "output gain affects callback samples");

    const std::array<float, 4> clippedSamples{1.2f, 0.0f, 0.0f, 0.0f};
    const float* clippedInputChannels[]{
        clippedSamples.data(), clippedSamples.data()};
    lastBackend->render(clippedInputChannels, 2, outputChannels, 2, 4);
    check(auralis_engine_get_meters(engine, &meters) == AURALIS_STATUS_OK
              && meters.clipping == 1,
          "input/output clipping is reflected in meters");

    check(auralis_engine_set_mute(engine, 1) == AURALIS_STATUS_OK,
          "mute command succeeds");
    lastBackend->render(inputChannels, 2, outputChannels, 2, 4);
    check(near(outLeft[0], 0.0f) && near(outRight[0], 0.0f),
          "mute silences callback output");
    check(auralis_engine_set_mute(engine, 0) == AURALIS_STATUS_OK,
          "unmute command succeeds");
    check(auralis_engine_set_pan(engine, 2.0f)
              == AURALIS_STATUS_INVALID_ARGUMENT,
          "pan outside normalized range is rejected");
    check(auralis_engine_set_input_gain(engine, 30.0f)
              == AURALIS_STATUS_INVALID_ARGUMENT,
          "gain outside supported dB range is rejected");

    MixerStateFields mixerState{};
    check(auralis_engine_get_mixer_state(
              engine,
              reinterpret_cast<AuralisMixerState*>(&mixerState))
              == AURALIS_STATUS_OK,
          "mixer state snapshot succeeds");
    check(near(mixerState.input_gain_db, 6.0206f)
              && near(mixerState.output_gain_db, -6.0206f)
              && near(mixerState.pan, 0.0f)
              && mixerState.muted == 0,
          "mixer state reflects accepted controls");

    check(auralis_engine_effect_add(
              engine, AURALIS_EFFECT_COMPRESSOR, 91, 0)
              == AURALIS_STATUS_OK,
          "effect add is accepted");
    check(auralis_engine_effect_set_wet(engine, 91, 0.4f)
              == AURALIS_STATUS_OK,
          "effect wet is accepted");
    check(auralis_engine_effect_set_enabled(engine, 91, 0)
              == AURALIS_STATUS_OK,
          "effect bypass is accepted");
    check(auralis_engine_effect_set_parameter(engine, 91, 2, 1.2f)
              == AURALIS_STATUS_OK,
          "normalized effect parameter is accepted and clamped");
    AuralisChainState chainState{};
    check(auralis_engine_get_chain_state(engine, &chainState)
              == AURALIS_STATUS_OK
              && chainState.effect_count == 0,
          "chain snapshot does not report queued commands as applied");
    lastBackend->render(inputChannels, 2, outputChannels, 2, 4);
    check(auralis_engine_get_chain_state(engine, &chainState)
              == AURALIS_STATUS_OK
              && chainState.effect_count == 1,
          "chain snapshot reports commands after callback consumption");
    check(chainState.effects[0].instance_id == 91
              && chainState.effects[0].enabled == 0
              && near(chainState.effects[0].wet, 0.4f)
              && chainState.effects[0].parameter_count == 1
              && near(chainState.effects[0].parameter_values[0], 1.0f),
          "chain command values reach the chain snapshot");
    check(auralis_engine_effect_add(
              engine, AURALIS_EFFECT_DELAY, 92, 1)
              == AURALIS_STATUS_OK
              && auralis_engine_effect_move(engine, 92, 0)
                  == AURALIS_STATUS_OK,
          "effect insertion and move are accepted");
    lastBackend->render(inputChannels, 2, outputChannels, 2, 4);
    check(auralis_engine_get_chain_state(engine, &chainState)
              == AURALIS_STATUS_OK
              && chainState.effect_count == 2
              && chainState.effects[0].instance_id == 92,
          "effect ordering is published after the callback");
    check(auralis_engine_effect_remove(engine, 92) == AURALIS_STATUS_OK,
          "effect removal is accepted");
    lastBackend->render(inputChannels, 2, outputChannels, 2, 4);
    check(auralis_engine_get_chain_state(engine, &chainState)
              == AURALIS_STATUS_OK
              && chainState.effect_count == 1
              && chainState.effects[0].instance_id == 91,
          "effect removal is published after the callback");

    for (std::uint32_t i = 0; i < 64; ++i) {
        check(auralis_engine_effect_set_wet(engine, 91, 0.5f)
                  == AURALIS_STATUS_OK,
              "bounded command queue accepts available entries");
    }
    check(auralis_engine_effect_set_wet(engine, 91, 0.5f)
              == AURALIS_STATUS_QUEUE_FULL,
          "queue saturation is returned to the caller");
    lastBackend->render(inputChannels, 2, outputChannels, 2, 4);

    std::atomic<std::uint32_t> acceptedCommands{0};
    std::atomic<std::uint32_t> fullCommands{0};
    std::atomic<std::uint32_t> unexpectedStatuses{0};
    std::vector<std::thread> producers;
    for (std::uint32_t producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&, producer] {
            for (std::uint32_t update = 0; update < 100; ++update) {
                const auto value = static_cast<float>(
                    (producer + update) % 10) / 10.0f;
                const auto status = (update % 2) == 0
                    ? auralis_engine_effect_set_wet(engine, 91, value)
                    : auralis_engine_effect_set_parameter(
                          engine, 91, 2, value);
                if (status == AURALIS_STATUS_OK) {
                    acceptedCommands.fetch_add(1, std::memory_order_relaxed);
                } else if (status == AURALIS_STATUS_QUEUE_FULL) {
                    fullCommands.fetch_add(1, std::memory_order_relaxed);
                } else {
                    unexpectedStatuses.fetch_add(
                        1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    check(unexpectedStatuses.load(std::memory_order_relaxed) == 0,
          "concurrent control producers receive bounded queue statuses");
    check(acceptedCommands.load(std::memory_order_relaxed) > 0
              && fullCommands.load(std::memory_order_relaxed) > 0,
          "concurrent producers observe accepted and saturated updates");
    lastBackend->render(inputChannels, 2, outputChannels, 2, 4);

    lastBackend->simulateDeviceStop();
    check(auralis_engine_get_meters(engine, &meters) == AURALIS_STATUS_OK
              && meters.estimated_latency_ms == 0.0,
          "asynchronous device stop clears the reported latency");
    auralis_engine_stop(engine);
    check(auralis_engine_is_running(engine) == 0,
          "stop clears running state");
    check(auralis_engine_get_meters(engine, &meters) == AURALIS_STATUS_OK
              && meters.estimated_latency_ms == 0.0,
          "stop clears device latency estimate");
    auralis_engine_destroy(engine);
    return EXIT_SUCCESS;
}
