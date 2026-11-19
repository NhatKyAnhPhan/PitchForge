#include "PresetCodec.h"

#include <auralis/mixer/SignalChainFactory.h>

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

using namespace auralis;

namespace auralis::dsp {
extern std::atomic<std::uint32_t> gTestSetParameterCount;
}

namespace {

mixer::ChainCommand addCommand(
    std::uint32_t id,
    std::uint16_t index,
    mixer::EffectType type = mixer::EffectType::Equalizer) {
    mixer::ChainCommand command;
    command.type = mixer::ChainCommandType::Add;
    command.instanceId = id;
    command.index = index;
    command.effectType = type;
    return command;
}

mixer::ChainCommand instanceCommand(
    mixer::ChainCommandType type,
    std::uint32_t id,
    float value = 0.0f) {
    mixer::ChainCommand command;
    command.type = type;
    command.instanceId = id;
    command.value = value;
    return command;
}

void processSilence(mixer::SignalChain& chain) {
    std::array<float, 4> left{};
    std::array<float, 4> right{};
    std::array<float*, 2> channels{left.data(), right.data()};
    chain.process({channels.data(), 2, 4});
}

void testQueueFullAndRecovery() {
    auto chain = mixer::createSignalChain();
    for (std::uint32_t i = 0; i < 64; ++i) {
        assert(chain->enqueue(instanceCommand(
            mixer::ChainCommandType::SetWet, 1, 0.25f)));
    }
    assert(!chain->enqueue(instanceCommand(
        mixer::ChainCommandType::SetWet, 1, 0.5f)));

    chain->prepare({48000.0, 64, 2});
    processSilence(*chain);
    assert(chain->enqueue(addCommand(1, 0)));
    processSilence(*chain);
    mixer::SignalChainSnapshot snapshot;
    chain->copySnapshot(snapshot);
    assert(snapshot.size == 1);
    assert(snapshot.slots[0].instanceId == 1);
}

void testCommandsOrderWetAndParameters() {
    auto chain = mixer::createSignalChain();
    chain->prepare({48000.0, 64, 2});
    assert(chain->enqueue(addCommand(11, 0)));
    assert(chain->enqueue(addCommand(12, 1, mixer::EffectType::Compressor)));
    assert(chain->enqueue(addCommand(13, 2, mixer::EffectType::Reverb)));
    processSilence(*chain);

    mixer::ChainCommand move;
    move.type = mixer::ChainCommandType::Move;
    move.instanceId = 11;
    move.destinationIndex = 2;
    assert(chain->enqueue(move));
    assert(chain->enqueue(instanceCommand(
        mixer::ChainCommandType::SetWet, 11, 0.5f)));
    auto parameter = instanceCommand(
        mixer::ChainCommandType::SetParameter, 11, 1.0f);
    parameter.parameterId = 1;
    assert(chain->enqueue(parameter));
    auto bypass = instanceCommand(
        mixer::ChainCommandType::SetEnabled, 12, 0.0f);
    assert(chain->enqueue(bypass));
    processSilence(*chain);

    mixer::SignalChainSnapshot snapshot;
    chain->copySnapshot(snapshot);
    assert(snapshot.size == 3);
    assert(snapshot.slots[0].instanceId == 12);
    assert(!snapshot.slots[0].enabled);
    assert(snapshot.slots[1].instanceId == 13);
    assert(snapshot.slots[2].instanceId == 11);
    assert(snapshot.slots[2].wet == 0.5f);
    assert(snapshot.slots[2].parameterCount == 1);
    assert(snapshot.slots[2].parameterIds[0] == 1);
    // The test DSP accepts parameter ID 1; its normalized value is retained.
    assert(snapshot.slots[2].parameterValues[0] == 1.0f);

    std::array<float, 4> left{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> right{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float*, 2> channels{left.data(), right.data()};
    chain->process({channels.data(), 2, 4});
    // At the final slot, the test processor doubles the signal and 50% wet
    // blends it with dry input.
    for (std::size_t i = 0; i < left.size(); ++i) {
        assert(std::abs(left[i] - 1.5f) < 0.0001f);
        assert(std::abs(right[i] - 1.5f) < 0.0001f);
    }

    assert(chain->enqueue(instanceCommand(
        mixer::ChainCommandType::Remove, 13)));
    processSilence(*chain);
    chain->copySnapshot(snapshot);
    assert(snapshot.size == 2);
    assert(snapshot.slots[0].instanceId == 12);
    assert(snapshot.slots[1].instanceId == 11);
}

void testEffectCapacityAndDuplicateIds() {
    auto chain = mixer::createSignalChain();
    chain->prepare({48000.0, 64, 2});
    for (std::uint32_t i = 0; i < mixer::kMaximumEffectsPerChannel; ++i) {
        assert(chain->enqueue(addCommand(i + 1, static_cast<std::uint16_t>(i))));
    }
    assert(chain->enqueue(addCommand(100, 16)));
    assert(chain->enqueue(addCommand(1, 0)));
    processSilence(*chain);

    mixer::SignalChainSnapshot snapshot;
    chain->copySnapshot(snapshot);
    assert(snapshot.size == mixer::kMaximumEffectsPerChannel);
    assert(snapshot.slots[0].instanceId == 1);
    assert(snapshot.slots[15].instanceId == 16);

    assert(chain->enqueue(instanceCommand(
        mixer::ChainCommandType::Remove, 8)));
    assert(chain->enqueue(addCommand(100, 15)));
    assert(chain->enqueue(instanceCommand(
        mixer::ChainCommandType::SetWet, 100, 0.25f)));
    processSilence(*chain);
    for (std::uint32_t attempt = 0; attempt < 100; ++attempt) {
        chain->copySnapshot(snapshot);
        if (snapshot.size == mixer::kMaximumEffectsPerChannel) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        processSilence(*chain);
    }
    chain->copySnapshot(snapshot);
    assert(snapshot.size == mixer::kMaximumEffectsPerChannel);
    assert(snapshot.slots[15].instanceId == 100);
    assert(snapshot.slots[15].wet == 0.25f);
}

void testSnapshotConcurrentRead() {
    auto chain = mixer::createSignalChain();
    chain->prepare({48000.0, 64, 2});
    assert(chain->enqueue(addCommand(7, 0)));
    processSilence(*chain);

    std::atomic<bool> done{false};
    std::thread reader([&] {
        mixer::SignalChainSnapshot snapshot;
        while (!done.load(std::memory_order_acquire)) {
            chain->copySnapshot(snapshot);
            assert(snapshot.size <= mixer::kMaximumEffectsPerChannel);
            for (std::uint32_t i = 0; i < snapshot.size; ++i) {
                assert(snapshot.slots[i].instanceId != 0);
                assert(snapshot.slots[i].wet >= 0.0f);
                assert(snapshot.slots[i].wet <= 1.0f);
            }
        }
    });

    for (std::uint32_t i = 0; i < 1000; ++i) {
        assert(chain->enqueue(instanceCommand(
            mixer::ChainCommandType::SetWet, 7,
            static_cast<float>(i & 1u))));
        processSilence(*chain);
    }
    done.store(true, std::memory_order_release);
    reader.join();
}

void testMultipleCommandProducers() {
    auto chain = mixer::createSignalChain();
    chain->prepare({48000.0, 64, 2});
    assert(chain->enqueue(addCommand(9, 0)));
    processSilence(*chain);
    dsp::gTestSetParameterCount.store(0, std::memory_order_relaxed);

    constexpr std::uint32_t producerCount = 4;
    constexpr std::uint32_t commandsPerProducer = 500;
    std::atomic<std::uint32_t> completedProducers{0};
    std::array<std::thread, producerCount> producers;
    for (std::uint32_t producer = 0;
         producer < producerCount;
         ++producer) {
        producers[producer] = std::thread([&, producer] {
            for (std::uint32_t i = 0; i < commandsPerProducer; ++i) {
                const float value = static_cast<float>((i + producer) & 1u);
                auto command = instanceCommand(
                    mixer::ChainCommandType::SetParameter, 9, value);
                command.parameterId = 1;
                while (!chain->enqueue(command)) {
                    std::this_thread::yield();
                }
            }
            completedProducers.fetch_add(1, std::memory_order_release);
        });
    }

    while (completedProducers.load(std::memory_order_acquire) < producerCount) {
        processSilence(*chain);
    }
    for (auto& producer : producers) {
        producer.join();
    }
    processSilence(*chain);

    mixer::SignalChainSnapshot snapshot;
    chain->copySnapshot(snapshot);
    assert(snapshot.size == 1);
    assert(snapshot.slots[0].instanceId == 9);
    assert(snapshot.slots[0].parameterCount == 1);
    assert(snapshot.slots[0].parameterValues[0] == 0.0f
        || snapshot.slots[0].parameterValues[0] == 1.0f);
    assert(dsp::gTestSetParameterCount.load(std::memory_order_relaxed)
        == producerCount * commandsPerProducer);
}

mixer::SignalChainSnapshot samplePreset() {
    mixer::SignalChainSnapshot snapshot;
    snapshot.size = 2;
    snapshot.slots[0].type = mixer::EffectType::Equalizer;
    snapshot.slots[0].instanceId = 40;
    snapshot.slots[0].enabled = false;
    snapshot.slots[0].wet = 0.25f;
    snapshot.slots[0].parameterCount = 2;
    snapshot.slots[0].parameterIds[0] = 4;
    snapshot.slots[0].parameterValues[0] = 0.125f;
    snapshot.slots[0].parameterIds[1] = 9;
    snapshot.slots[0].parameterValues[1] = 0.875f;
    snapshot.slots[1].type = mixer::EffectType::Equalizer;
    snapshot.slots[1].instanceId = 41;
    snapshot.slots[1].enabled = true;
    snapshot.slots[1].wet = 1.0f;
    return snapshot;
}

void testPresetRoundTripAndValidation() {
    auto expected = samplePreset();
    std::vector<std::uint8_t> encoded;
    assert(mixer::detail::encodePreset(expected, encoded)
        == mixer::detail::PresetCodecStatus::Ok);
    assert(encoded.size() > 7);

    mixer::SignalChainSnapshot decoded;
    assert(mixer::detail::decodePreset(encoded, decoded)
        == mixer::detail::PresetCodecStatus::Ok);
    assert(decoded.size == expected.size);
    for (std::uint32_t i = 0; i < expected.size; ++i) {
        assert(decoded.slots[i].type == expected.slots[i].type);
        assert(decoded.slots[i].instanceId == expected.slots[i].instanceId);
        assert(decoded.slots[i].enabled == expected.slots[i].enabled);
        assert(decoded.slots[i].wet == expected.slots[i].wet);
        assert(decoded.slots[i].parameterCount
            == expected.slots[i].parameterCount);
        for (std::uint32_t p = 0;
             p < expected.slots[i].parameterCount;
             ++p) {
            assert(decoded.slots[i].parameterIds[p]
                == expected.slots[i].parameterIds[p]);
            assert(decoded.slots[i].parameterValues[p]
                == expected.slots[i].parameterValues[p]);
        }
    }

    const auto originalSize = decoded.size;
    auto truncated = encoded;
    truncated.pop_back();
    assert(mixer::detail::decodePreset(truncated, decoded)
        == mixer::detail::PresetCodecStatus::Truncated);
    assert(decoded.size == originalSize);

    auto unsupportedVersion = encoded;
    unsupportedVersion[4] = 2;
    assert(mixer::detail::decodePreset(unsupportedVersion, decoded)
        == mixer::detail::PresetCodecStatus::UnsupportedVersion);
    assert(decoded.size == originalSize);

    auto invalidMagic = encoded;
    invalidMagic[0] = 'X';
    assert(mixer::detail::decodePreset(invalidMagic, decoded)
        == mixer::detail::PresetCodecStatus::InvalidData);

    auto duplicateInstanceId = encoded;
    constexpr std::size_t firstSlotSize = 11 + 2 * 6;
    constexpr std::size_t secondSlotInstanceIdOffset = 7 + firstSlotSize + 1;
    for (std::size_t byte = 0; byte < 4; ++byte) {
        duplicateInstanceId[secondSlotInstanceIdOffset + byte] =
            duplicateInstanceId[8 + byte];
    }
    assert(mixer::detail::decodePreset(duplicateInstanceId, decoded)
        == mixer::detail::PresetCodecStatus::InvalidData);
    assert(decoded.size == originalSize);

    auto trailingData = encoded;
    trailingData.push_back(0);
    assert(mixer::detail::decodePreset(trailingData, decoded)
        == mixer::detail::PresetCodecStatus::InvalidData);

    auto invalidSnapshot = expected;
    invalidSnapshot.slots[1].instanceId = invalidSnapshot.slots[0].instanceId;
    assert(mixer::detail::encodePreset(invalidSnapshot, encoded)
        == mixer::detail::PresetCodecStatus::InvalidState);
    invalidSnapshot = expected;
    invalidSnapshot.slots[0].wet = std::numeric_limits<float>::quiet_NaN();
    assert(mixer::detail::encodePreset(invalidSnapshot, encoded)
        == mixer::detail::PresetCodecStatus::InvalidState);
}

} // namespace

int main() {
    testQueueFullAndRecovery();
    testCommandsOrderWetAndParameters();
    testEffectCapacityAndDuplicateIds();
    testSnapshotConcurrentRead();
    testMultipleCommandProducers();
    testPresetRoundTripAndValidation();
    return 0;
}
