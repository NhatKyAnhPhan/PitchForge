#include <auralis/dsp/ProcessorFactory.h>
#include <auralis/mixer/SignalChainFactory.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace auralis::mixer {
namespace {

constexpr std::size_t kEffectTypeCount = 9;
constexpr std::size_t kProcessorsPerType = kMaximumEffectsPerChannel;
constexpr std::size_t kCommandQueueCapacity = 64;
constexpr std::size_t kParameterCapacity = 16;

static_assert((kCommandQueueCapacity & (kCommandQueueCapacity - 1)) == 0);
static_assert(std::is_trivially_copyable_v<ChainCommand>);
static_assert(std::atomic<std::uint8_t>::is_always_lock_free);
static_assert(std::atomic<std::uint16_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::size_t>::is_always_lock_free);

[[nodiscard]] constexpr bool isValidEffectType(EffectType type) noexcept {
    return static_cast<std::uint8_t>(type)
        < static_cast<std::uint8_t>(kEffectTypeCount);
}

[[nodiscard]] constexpr std::size_t effectIndex(EffectType type) noexcept {
    return static_cast<std::size_t>(type);
}

[[nodiscard]] constexpr EffectType effectTypeAt(std::size_t index) noexcept {
    return static_cast<EffectType>(index);
}

template <typename T>
class BoundedQueue final {
    static_assert(std::is_trivially_copyable_v<T>);

    // Bounded MPSC ring: producers reserve with a CAS on enqueuePosition_;
    // the single audio consumer owns dequeuePosition_ and never takes that CAS
    // or waits for a producer. A pending reservation is treated as empty until
    // its producer publishes the cell; a full ring returns false immediately.
    struct Cell {
        std::atomic<std::size_t> sequence{0};
        T value{};
    };

public:
    BoundedQueue() noexcept {
        for (std::size_t i = 0; i < kCommandQueueCapacity; ++i) {
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] bool tryPush(const T& value) noexcept {
        std::size_t position = enqueuePosition_.load(std::memory_order_relaxed);
        Cell* cell = nullptr;
        for (;;) {
            cell = &cells_[position & (kCommandQueueCapacity - 1)];
            const std::size_t sequence =
                cell->sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<std::intptr_t>(sequence)
                - static_cast<std::intptr_t>(position);
            if (difference == 0) {
                if (enqueuePosition_.compare_exchange_weak(
                        position,
                        position + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    break;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = enqueuePosition_.load(std::memory_order_relaxed);
            }
        }

        cell->value = value;
        cell->sequence.store(position + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool tryPop(T& value) noexcept {
        const std::size_t position = dequeuePosition_;
        Cell& cell = cells_[position & (kCommandQueueCapacity - 1)];
        const std::size_t sequence =
            cell.sequence.load(std::memory_order_acquire);
        const auto difference = static_cast<std::intptr_t>(sequence)
            - static_cast<std::intptr_t>(position + 1);
        if (difference != 0) {
            return false;
        }

        value = cell.value;
        dequeuePosition_ = position + 1;
        cell.sequence.store(
            position + kCommandQueueCapacity, std::memory_order_release);
        return true;
    }

private:
    std::array<Cell, kCommandQueueCapacity> cells_{};
    std::atomic<std::size_t> enqueuePosition_{0};
    std::size_t dequeuePosition_ = 0;
};

enum class ProcessorPoolState : std::uint8_t {
    Unavailable,
    Ready,
    InUse,
    Retired,
    Resetting,
};

static_assert(std::atomic<ProcessorPoolState>::is_always_lock_free);

struct ProcessorPoolEntry {
    std::unique_ptr<dsp::AudioProcessor> processor;
    std::atomic<ProcessorPoolState> state{ProcessorPoolState::Unavailable};
    std::uint32_t parameterCount = 0;
    std::array<std::uint16_t, kParameterCapacity> parameterIds{};
    std::array<float, kParameterCapacity> originalParameterValues{};
};

struct RuntimeSlot {
    EffectSlot state{};
    std::uint8_t processorIndex = 0;
};

struct AtomicEffectSlot {
    std::atomic<std::uint8_t> type{
        static_cast<std::uint8_t>(EffectType::Equalizer)};
    std::atomic<std::uint32_t> instanceId{0};
    std::atomic<bool> enabled{true};
    std::atomic<float> wet{1.0f};
    std::atomic<std::uint32_t> parameterCount{0};
    std::array<std::atomic<std::uint16_t>, kParameterCapacity> parameterIds{};
    std::array<std::atomic<float>, kParameterCapacity> parameterValues{};

    void store(const EffectSlot& source) noexcept {
        type.store(static_cast<std::uint8_t>(source.type),
            std::memory_order_seq_cst);
        instanceId.store(source.instanceId, std::memory_order_seq_cst);
        enabled.store(source.enabled, std::memory_order_seq_cst);
        wet.store(source.wet, std::memory_order_seq_cst);
        parameterCount.store(source.parameterCount, std::memory_order_seq_cst);
        for (std::size_t i = 0; i < kParameterCapacity; ++i) {
            parameterIds[i].store(
                source.parameterIds[i], std::memory_order_seq_cst);
            parameterValues[i].store(
                source.parameterValues[i], std::memory_order_seq_cst);
        }
    }

    [[nodiscard]] EffectSlot load() const noexcept {
        EffectSlot result;
        result.type = static_cast<EffectType>(
            type.load(std::memory_order_seq_cst));
        result.instanceId = instanceId.load(std::memory_order_seq_cst);
        result.enabled = enabled.load(std::memory_order_seq_cst);
        result.wet = wet.load(std::memory_order_seq_cst);
        result.parameterCount =
            parameterCount.load(std::memory_order_seq_cst);
        for (std::size_t i = 0; i < kParameterCapacity; ++i) {
            result.parameterIds[i] =
                parameterIds[i].load(std::memory_order_seq_cst);
            result.parameterValues[i] =
                parameterValues[i].load(std::memory_order_seq_cst);
        }
        return result;
    }
};

struct AtomicSnapshot {
    std::atomic<std::uint32_t> sequence{0};
    std::atomic<std::uint32_t> size{0};
    std::array<AtomicEffectSlot, kMaximumEffectsPerChannel> slots{};
};

class FixedSignalChain final : public SignalChain {
public:
    FixedSignalChain()
        : maintenanceThread_([this] {
            maintenanceLoop();
        }) {}

    ~FixedSignalChain() override {
        maintenanceExit_.store(true, std::memory_order_release);
        if (maintenanceThread_.joinable()) {
            maintenanceThread_.join();
        }
    }

    void prepare(const audio::ProcessSpec& spec) override {
        configured_ = false;
        if (!std::isfinite(spec.sampleRate) || spec.sampleRate <= 0.0
            || spec.maximumBlockSize == 0
            || spec.channelCount == 0
            || static_cast<std::size_t>(spec.channelCount)
                > std::numeric_limits<std::size_t>::max()
                    / static_cast<std::size_t>(spec.maximumBlockSize)) {
            scratch_.clear();
            return;
        }

        try {
            scratch_.resize(
                static_cast<std::size_t>(spec.channelCount)
                * static_cast<std::size_t>(spec.maximumBlockSize));

            for (std::size_t type = 0; type < kEffectTypeCount; ++type) {
                for (std::size_t poolIndex = 0;
                     poolIndex < kProcessorsPerType;
                     ++poolIndex) {
                    auto& entry = processors_[type][poolIndex];
                    if (!entry.processor) {
                        // DSP processors use a private pool identity. The public
                        // instance ID remains in the mixer slot and is never
                        // ambiguous, even when an effect type is repeated.
                        const auto privateId = static_cast<std::uint32_t>(
                            0x80000000u
                            | (static_cast<std::uint32_t>(type) << 8)
                            | static_cast<std::uint32_t>(poolIndex + 1));
                        try {
                            entry.processor = dsp::createProcessor(
                                effectTypeAt(type), privateId);
                        } catch (...) {
                            entry.processor.reset();
                        }
                    }
                    if (!entry.processor) {
                        break;
                    }

                    auto state = entry.state.load(std::memory_order_acquire);
                    bool ownsEntry = false;
                    for (;;) {
                        if (state == ProcessorPoolState::Resetting) {
                            std::this_thread::yield();
                            state = entry.state.load(std::memory_order_acquire);
                            continue;
                        }
                        if (state == ProcessorPoolState::InUse) {
                            break;
                        }
                        if (entry.state.compare_exchange_weak(
                                state,
                                ProcessorPoolState::Resetting,
                                std::memory_order_acq_rel,
                                std::memory_order_acquire)) {
                            ownsEntry = true;
                            break;
                        }
                    }

                    try {
                        entry.processor->prepare(spec);
                    } catch (...) {
                        if (ownsEntry) {
                            entry.state.store(
                                ProcessorPoolState::Unavailable,
                                std::memory_order_release);
                        }
                        throw;
                    }
                    if (ownsEntry) {
                        restoreAndReset(entry);
                        entry.state.store(
                            ProcessorPoolState::Ready,
                            std::memory_order_release);
                    }
                }
            }
        } catch (...) {
            scratch_.clear();
            return;
        }

        maximumBlockSize_ = spec.maximumBlockSize;
        channelCount_ = spec.channelCount;
        configured_ = true;
    }

    void process(audio::AudioBlockView block) noexcept override {
        if (!configured_ || !block.isValid()
            || block.channelCount > channelCount_
            || block.sampleCount > maximumBlockSize_) {
            return;
        }
        for (std::uint32_t channel = 0; channel < block.channelCount; ++channel) {
            if (block.channels[channel] == nullptr) {
                return;
            }
        }

        bool changed = false;
        ChainCommand command;
        while (deferredCommandCount_ > 0) {
            const ApplyResult result = apply(deferredCommands_[0]);
            if (result == ApplyResult::Deferred) {
                break;
            }
            changed = result == ApplyResult::Changed || changed;
            for (std::uint32_t i = 1; i < deferredCommandCount_; ++i) {
                deferredCommands_[i - 1] = deferredCommands_[i];
            }
            --deferredCommandCount_;
        }

        if (deferredCommandCount_ == 0) {
            while (commands_.tryPop(command)) {
                const ApplyResult result = apply(command);
                if (result == ApplyResult::Deferred) {
                    deferredCommands_[deferredCommandCount_++] = command;
                    break;
                }
                changed = result == ApplyResult::Changed || changed;
            }
        }
        while (deferredCommandCount_ < deferredCommands_.size()
            && commands_.tryPop(command)) {
            deferredCommands_[deferredCommandCount_++] = command;
        }
        if (changed) {
            publishSnapshot();
        }

        for (std::uint32_t slotIndex = 0; slotIndex < size_; ++slotIndex) {
            const auto& slot = slots_[slotIndex];
            if (!slot.state.enabled || slot.state.wet <= 0.0f) {
                continue;
            }

            auto& entry = processors_[effectIndex(slot.state.type)]
                [slot.processorIndex];
            if (!entry.processor) {
                continue;
            }

            if (slot.state.wet < 1.0f) {
                for (std::uint32_t channel = 0;
                     channel < block.channelCount;
                     ++channel) {
                    const auto scratchOffset =
                        static_cast<std::size_t>(channel) * maximumBlockSize_;
                    std::copy_n(
                        block.channels[channel],
                        block.sampleCount,
                        scratch_.data() + scratchOffset);
                }
            }

            entry.processor->process(block);

            if (slot.state.wet < 1.0f) {
                const float wet = slot.state.wet;
                const float dry = 1.0f - wet;
                for (std::uint32_t channel = 0;
                     channel < block.channelCount;
                     ++channel) {
                    const auto scratchOffset =
                        static_cast<std::size_t>(channel) * maximumBlockSize_;
                    const float* drySamples = scratch_.data() + scratchOffset;
                    float* output = block.channels[channel];
                    for (std::uint32_t sample = 0;
                         sample < block.sampleCount;
                         ++sample) {
                        output[sample] =
                            drySamples[sample] * dry + output[sample] * wet;
                    }
                }
            }
        }
    }

    [[nodiscard]] bool enqueue(const ChainCommand& command) noexcept override {
        reclaimRetiredProcessors();
        return commands_.tryPush(command);
    }

    void copySnapshot(SignalChainSnapshot& destination) const noexcept override {
        for (;;) {
            const auto before =
                snapshot_.sequence.load(std::memory_order_seq_cst);
            if ((before & 1u) != 0) {
                continue;
            }
            destination.size = snapshot_.size.load(std::memory_order_seq_cst);
            for (std::size_t i = 0; i < kMaximumEffectsPerChannel; ++i) {
                destination.slots[i] = snapshot_.slots[i].load();
            }
            const auto after =
                snapshot_.sequence.load(std::memory_order_seq_cst);
            if (before == after && (after & 1u) == 0) {
                return;
            }
        }
    }

private:
    enum class ApplyResult : std::uint8_t {
        NoChange,
        Changed,
        Deferred,
    };

    [[nodiscard]] std::uint32_t findSlot(
        std::uint32_t instanceId) const noexcept {
        for (std::uint32_t i = 0; i < size_; ++i) {
            if (slots_[i].state.instanceId == instanceId) {
                return i;
            }
        }
        return size_;
    }

    [[nodiscard]] ApplyResult apply(const ChainCommand& command) noexcept {
        switch (command.type) {
        case ChainCommandType::Add:
            return add(command);
        case ChainCommandType::Remove:
            return remove(command.instanceId)
                ? ApplyResult::Changed : ApplyResult::NoChange;
        case ChainCommandType::Move:
            return move(command)
                ? ApplyResult::Changed : ApplyResult::NoChange;
        case ChainCommandType::SetEnabled:
            return setEnabled(command)
                ? ApplyResult::Changed : ApplyResult::NoChange;
        case ChainCommandType::SetWet:
            return setWet(command)
                ? ApplyResult::Changed : ApplyResult::NoChange;
        case ChainCommandType::SetParameter:
            return setParameter(command)
                ? ApplyResult::Changed : ApplyResult::NoChange;
        }
        return ApplyResult::NoChange;
    }

    [[nodiscard]] ApplyResult add(const ChainCommand& command) noexcept {
        if (!configured_ || command.instanceId == 0
            || !isValidEffectType(command.effectType)
            || size_ >= kMaximumEffectsPerChannel
            || command.index > size_ || findSlot(command.instanceId) != size_) {
            return ApplyResult::NoChange;
        }

        const std::size_t type = effectIndex(command.effectType);
        std::size_t freeIndex = kProcessorsPerType;
        bool waitingForReset = false;
        for (std::size_t i = 0; i < kProcessorsPerType; ++i) {
            auto& candidate = processors_[type][i];
            auto state = candidate.state.load(std::memory_order_acquire);
            if (candidate.processor && state == ProcessorPoolState::Ready
                && candidate.state.compare_exchange_strong(
                    state,
                    ProcessorPoolState::InUse,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                freeIndex = i;
                break;
            }
            if (candidate.processor
                && (state == ProcessorPoolState::Retired
                    || state == ProcessorPoolState::Resetting
                    || state == ProcessorPoolState::Ready)) {
                waitingForReset = true;
            }
        }
        if (freeIndex == kProcessorsPerType) {
            return waitingForReset
                ? ApplyResult::Deferred : ApplyResult::NoChange;
        }

        auto& entry = processors_[type][freeIndex];
        entry.processor->setEnabled(true);

        for (std::uint32_t i = size_; i > command.index; --i) {
            slots_[i] = slots_[i - 1];
        }
        RuntimeSlot& inserted = slots_[command.index];
        inserted.state = EffectSlot{};
        inserted.state.type = command.effectType;
        inserted.state.instanceId = command.instanceId;
        inserted.state.enabled = true;
        inserted.state.wet = 1.0f;
        inserted.processorIndex = static_cast<std::uint8_t>(freeIndex);
        ++size_;
        return ApplyResult::Changed;
    }

    [[nodiscard]] bool remove(std::uint32_t instanceId) noexcept {
        const std::uint32_t index = findSlot(instanceId);
        if (index == size_) {
            return false;
        }
        const RuntimeSlot removed = slots_[index];
        auto& entry = processors_[effectIndex(removed.state.type)]
            [removed.processorIndex];

        for (std::uint32_t i = index + 1; i < size_; ++i) {
            slots_[i - 1] = slots_[i];
        }
        --size_;
        slots_[size_] = RuntimeSlot{};
        // The control thread reclaims and resets this processor before making
        // it available again; the callback never resets or destroys it.
        entry.state.store(
            ProcessorPoolState::Retired, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool move(const ChainCommand& command) noexcept {
        const std::uint32_t source = findSlot(command.instanceId);
        if (source == size_ || command.destinationIndex >= size_) {
            return false;
        }
        const std::uint32_t destination = command.destinationIndex;
        if (source == destination) {
            return false;
        }
        const RuntimeSlot moving = slots_[source];
        if (source < destination) {
            for (std::uint32_t i = source; i < destination; ++i) {
                slots_[i] = slots_[i + 1];
            }
        } else {
            for (std::uint32_t i = source; i > destination; --i) {
                slots_[i] = slots_[i - 1];
            }
        }
        slots_[destination] = moving;
        return true;
    }

    [[nodiscard]] bool setEnabled(const ChainCommand& command) noexcept {
        if (!std::isfinite(command.value)) {
            return false;
        }
        const std::uint32_t index = findSlot(command.instanceId);
        if (index == size_) {
            return false;
        }
        auto& slot = slots_[index];
        const bool enabled = command.value >= 0.5f;
        if (slot.state.enabled == enabled) {
            return false;
        }
        slot.state.enabled = enabled;
        auto& entry = processors_[effectIndex(slot.state.type)]
            [slot.processorIndex];
        if (entry.processor) {
            entry.processor->setEnabled(enabled);
        }
        return true;
    }

    [[nodiscard]] bool setWet(const ChainCommand& command) noexcept {
        if (!std::isfinite(command.value)) {
            return false;
        }
        const std::uint32_t index = findSlot(command.instanceId);
        if (index == size_) {
            return false;
        }
        const float wet = std::clamp(command.value, 0.0f, 1.0f);
        if (slots_[index].state.wet == wet) {
            return false;
        }
        slots_[index].state.wet = wet;
        return true;
    }

    static void restoreAndReset(ProcessorPoolEntry& entry) noexcept {
        if (!entry.processor) {
            entry.parameterCount = 0;
            return;
        }
        for (std::uint32_t i = 0; i < entry.parameterCount; ++i) {
            (void)entry.processor->setParameter(
                entry.parameterIds[i], entry.originalParameterValues[i]);
        }
        entry.parameterCount = 0;
        entry.processor->setEnabled(false);
        entry.processor->reset();
    }

    void reclaimRetiredProcessors() noexcept {
        for (auto& typePool : processors_) {
            for (auto& entry : typePool) {
                auto expected = ProcessorPoolState::Retired;
                if (!entry.state.compare_exchange_strong(
                        expected,
                        ProcessorPoolState::Resetting,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed)) {
                    continue;
                }
                restoreAndReset(entry);
                entry.state.store(
                    ProcessorPoolState::Ready, std::memory_order_release);
            }
        }
    }

    void maintenanceLoop() noexcept {
        while (!maintenanceExit_.load(std::memory_order_acquire)) {
            reclaimRetiredProcessors();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    [[nodiscard]] bool setParameter(const ChainCommand& command) noexcept {
        if (!std::isfinite(command.value)) {
            return false;
        }
        const std::uint32_t index = findSlot(command.instanceId);
        if (index == size_) {
            return false;
        }

        auto& slot = slots_[index];
        auto& entry = processors_[effectIndex(slot.state.type)]
            [slot.processorIndex];
        if (!entry.processor) {
            return false;
        }

        std::uint32_t slotParameter = slot.state.parameterCount;
        for (std::uint32_t i = 0; i < slot.state.parameterCount; ++i) {
            if (slot.state.parameterIds[i] == command.parameterId) {
                slotParameter = i;
                break;
            }
        }
        if (slotParameter == slot.state.parameterCount
            && slot.state.parameterCount >= kParameterCapacity) {
            return false;
        }

        std::uint32_t baselineParameter = entry.parameterCount;
        for (std::uint32_t i = 0; i < entry.parameterCount; ++i) {
            if (entry.parameterIds[i] == command.parameterId) {
                baselineParameter = i;
                break;
            }
        }
        if (baselineParameter == entry.parameterCount
            && entry.parameterCount >= kParameterCapacity) {
            return false;
        }

        float originalValue = 0.0f;
        if (baselineParameter == entry.parameterCount) {
            originalValue =
                entry.processor->getParameter(command.parameterId);
            if (!std::isfinite(originalValue)) {
                return false;
            }
        }

        const float normalized =
            std::clamp(command.value, 0.0f, 1.0f);
        if (!entry.processor->setParameter(
                command.parameterId, normalized)) {
            return false;
        }

        float actualValue =
            entry.processor->getParameter(command.parameterId);
        if (!std::isfinite(actualValue)) {
            actualValue = normalized;
        }
        actualValue = std::clamp(actualValue, 0.0f, 1.0f);

        if (baselineParameter == entry.parameterCount) {
            entry.parameterIds[baselineParameter] = command.parameterId;
            entry.originalParameterValues[baselineParameter] = originalValue;
            ++entry.parameterCount;
        }
        if (slotParameter == slot.state.parameterCount) {
            slot.state.parameterIds[slotParameter] = command.parameterId;
            ++slot.state.parameterCount;
        }
        slot.state.parameterValues[slotParameter] = actualValue;
        return true;
    }

    void publishSnapshot() noexcept {
        snapshot_.sequence.fetch_add(1, std::memory_order_seq_cst);
        snapshot_.size.store(size_, std::memory_order_seq_cst);
        for (std::size_t i = 0; i < kMaximumEffectsPerChannel; ++i) {
            snapshot_.slots[i].store(
                i < size_ ? slots_[i].state : EffectSlot{});
        }
        snapshot_.sequence.fetch_add(1, std::memory_order_seq_cst);
    }

    BoundedQueue<ChainCommand> commands_;
    std::array<RuntimeSlot, kMaximumEffectsPerChannel> slots_{};
    std::uint32_t size_ = 0;
    std::array<
        std::array<ProcessorPoolEntry, kProcessorsPerType>,
        kEffectTypeCount> processors_{};
    std::vector<float> scratch_;
    std::uint32_t maximumBlockSize_ = 0;
    std::uint32_t channelCount_ = 0;
    bool configured_ = false;
    AtomicSnapshot snapshot_;
    std::array<ChainCommand, kCommandQueueCapacity> deferredCommands_{};
    std::size_t deferredCommandCount_ = 0;
    std::atomic<bool> maintenanceExit_{false};
    std::thread maintenanceThread_;
};

} // namespace

std::unique_ptr<SignalChain> createSignalChain() {
    return std::make_unique<FixedSignalChain>();
}

} // namespace auralis::mixer
