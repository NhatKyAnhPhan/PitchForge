#include "PresetCodec.h"

#include <auralis/mixer/SignalChainFactory.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            std::cerr << "CHECK failed: " << #condition << " at "          \
                      << __FILE__ << ':' << __LINE__ << '\n';              \
            std::abort();                                                  \
        }                                                                  \
    } while (false)

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
        CHECK(chain->enqueue(instanceCommand(
            mixer::ChainCommandType::SetWet, 1, 0.25f)));
    }
    CHECK(!chain->enqueue(instanceCommand(
        mixer::ChainCommandType::SetWet, 1, 0.5f)));

    chain->prepare({48000.0, 64, 2});
    processSilence(*chain);
    CHECK(chain->enqueue(addCommand(1, 0)));
    processSilence(*chain);
    mixer::SignalChainSnapshot snapshot;
    chain->copySnapshot(snapshot);
    CHECK(snapshot.size == 1);
    CHECK(snapshot.slots[0].instanceId == 1);
}

void testCommandsOrderWetAndParameters() {
    auto chain = mixer::createSignalChain();
    chain->prepare({48000.0, 64, 2});
    CHECK(chain->enqueue(addCommand(11, 0)));
    CHECK(chain->enqueue(addCommand(12, 1, mixer::EffectType::Compressor)));
    CHECK(chain->enqueue(addCommand(13, 2, mixer::EffectType::Reverb)));
    processSilence(*chain);

    mixer::ChainCommand move;
    move.type = mixer::ChainCommandType::Move;
    move.instanceId = 11;
    move.destinationIndex = 2;
    CHECK(chain->enqueue(move));
    CHECK(chain->enqueue(instanceCommand(
        mixer::ChainCommandType::SetWet, 11, 0.5f)));
    auto parameter = instanceCommand(
        mixer::ChainCommandType::SetParameter, 11, 1.0f);
    parameter.parameterId = 1;
    CHECK(chain->enqueue(parameter));
    auto bypass = instanceCommand(
        mixer::ChainCommandType::SetEnabled, 12, 0.0f);
    CHECK(chain->enqueue(bypass));
    processSilence(*chain);

    mixer::SignalChainSnapshot snapshot;
    chain->copySnapshot(snapshot);
    CHECK(snapshot.size == 3);
    CHECK(snapshot.slots[0].instanceId == 12);
    CHECK(!snapshot.slots[0].enabled);
    CHECK(snapshot.slots[1].instanceId == 13);
    CHECK(snapshot.slots[2].instanceId == 11);
    CHECK(snapshot.slots[2].wet == 0.5f);
    CHECK(snapshot.slots[2].parameterCount == 1);
    CHECK(snapshot.slots[2].parameterIds[0] == 1);
    // The test DSP accepts parameter ID 1; its normalized value is retained.
    CHECK(snapshot.slots[2].parameterValues[0] == 1.0f);

    std::array<float, 4> left{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> right{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float*, 2> channels{left.data(), right.data()};
    chain->process({channels.data(), 2, 4});
    // At the final slot, the test processor doubles the signal and 50% wet
    // blends it with dry input.
    for (std::size_t i = 0; i < left.size(); ++i) {
        CHECK(std::abs(left[i] - 1.5f) < 0.0001f);
        CHECK(std::abs(right[i] - 1.5f) < 0.0001f);
    }

    CHECK(chain->enqueue(instanceCommand(
        mixer::ChainCommandType::Remove, 13)));
    processSilence(*chain);
    chain->copySnapshot(snapshot);
    CHECK(snapshot.size == 2);
    CHECK(snapshot.slots[0].instanceId == 12);
    CHECK(snapshot.slots[1].instanceId == 11);
}

void testEffectCapacityAndDuplicateIds() {
    auto chain = mixer::createSignalChain();
    chain->prepare({48000.0, 64, 2});
    for (std::uint32_t i = 0; i < mixer::kMaximumEffectsPerChannel; ++i) {
        CHECK(chain->enqueue(addCommand(i + 1, static_cast<std::uint16_t>(i))));
    }
    CHECK(chain->enqueue(addCommand(100, 16)));
    CHECK(chain->enqueue(addCommand(1, 0)));
    processSilence(*chain);

    mixer::SignalChainSnapshot snapshot;
    chain->copySnapshot(snapshot);
    CHECK(snapshot.size == mixer::kMaximumEffectsPerChannel);
    CHECK(snapshot.slots[0].instanceId == 1);
    CHECK(snapshot.slots[15].instanceId == 16);

    CHECK(chain->enqueue(instanceCommand(
        mixer::ChainCommandType::Remove, 8)));
    CHECK(chain->enqueue(addCommand(100, 15)));
    CHECK(chain->enqueue(instanceCommand(
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
    CHECK(snapshot.size == mixer::kMaximumEffectsPerChannel);
    CHECK(snapshot.slots[15].instanceId == 100);
    CHECK(snapshot.slots[15].wet == 0.25f);
}

void testSnapshotConcurrentRead() {
    auto chain = mixer::createSignalChain();
    chain->prepare({48000.0, 64, 2});
    CHECK(chain->enqueue(addCommand(7, 0)));
    processSilence(*chain);

    std::atomic<bool> done{false};
    std::thread reader([&] {
        mixer::SignalChainSnapshot snapshot;
        while (!done.load(std::memory_order_acquire)) {
            chain->copySnapshot(snapshot);
            CHECK(snapshot.size <= mixer::kMaximumEffectsPerChannel);
            for (std::uint32_t i = 0; i < snapshot.size; ++i) {
                CHECK(snapshot.slots[i].instanceId != 0);
                CHECK(snapshot.slots[i].wet >= 0.0f);
                CHECK(snapshot.slots[i].wet <= 1.0f);
            }
        }
    });

    for (std::uint32_t i = 0; i < 1000; ++i) {
        CHECK(chain->enqueue(instanceCommand(
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
    CHECK(chain->enqueue(addCommand(9, 0)));
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
    CHECK(snapshot.size == 1);
    CHECK(snapshot.slots[0].instanceId == 9);
    CHECK(snapshot.slots[0].parameterCount == 1);
    CHECK(snapshot.slots[0].parameterValues[0] == 0.0f
        || snapshot.slots[0].parameterValues[0] == 1.0f);
    CHECK(dsp::gTestSetParameterCount.load(std::memory_order_relaxed)
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
    CHECK(mixer::detail::encodePreset(expected, encoded)
        == mixer::detail::PresetCodecStatus::Ok);
    CHECK(encoded.size() > 7);

    mixer::SignalChainSnapshot decoded;
    CHECK(mixer::detail::decodePreset(encoded, decoded)
        == mixer::detail::PresetCodecStatus::Ok);
    CHECK(decoded.size == expected.size);
    for (std::uint32_t i = 0; i < expected.size; ++i) {
        CHECK(decoded.slots[i].type == expected.slots[i].type);
        CHECK(decoded.slots[i].instanceId == expected.slots[i].instanceId);
        CHECK(decoded.slots[i].enabled == expected.slots[i].enabled);
        CHECK(decoded.slots[i].wet == expected.slots[i].wet);
        CHECK(decoded.slots[i].parameterCount
            == expected.slots[i].parameterCount);
        for (std::uint32_t p = 0;
             p < expected.slots[i].parameterCount;
             ++p) {
            CHECK(decoded.slots[i].parameterIds[p]
                == expected.slots[i].parameterIds[p]);
            CHECK(decoded.slots[i].parameterValues[p]
                == expected.slots[i].parameterValues[p]);
        }
    }

    const auto originalSize = decoded.size;
    auto truncated = encoded;
    truncated.pop_back();
    CHECK(mixer::detail::decodePreset(truncated, decoded)
        == mixer::detail::PresetCodecStatus::Truncated);
    CHECK(decoded.size == originalSize);

    auto unsupportedVersion = encoded;
    unsupportedVersion[4] = 2;
    CHECK(mixer::detail::decodePreset(unsupportedVersion, decoded)
        == mixer::detail::PresetCodecStatus::UnsupportedVersion);
    CHECK(decoded.size == originalSize);

    auto invalidMagic = encoded;
    invalidMagic[0] = 'X';
    CHECK(mixer::detail::decodePreset(invalidMagic, decoded)
        == mixer::detail::PresetCodecStatus::InvalidData);

    auto duplicateInstanceId = encoded;
    constexpr std::size_t firstSlotSize = 11 + 2 * 6;
    constexpr std::size_t secondSlotInstanceIdOffset = 7 + firstSlotSize + 1;
    for (std::size_t byte = 0; byte < 4; ++byte) {
        duplicateInstanceId[secondSlotInstanceIdOffset + byte] =
            duplicateInstanceId[8 + byte];
    }
    CHECK(mixer::detail::decodePreset(duplicateInstanceId, decoded)
        == mixer::detail::PresetCodecStatus::InvalidData);
    CHECK(decoded.size == originalSize);

    auto trailingData = encoded;
    trailingData.push_back(0);
    CHECK(mixer::detail::decodePreset(trailingData, decoded)
        == mixer::detail::PresetCodecStatus::InvalidData);

    auto invalidSnapshot = expected;
    invalidSnapshot.slots[1].instanceId = invalidSnapshot.slots[0].instanceId;
    CHECK(mixer::detail::encodePreset(invalidSnapshot, encoded)
        == mixer::detail::PresetCodecStatus::InvalidState);
    invalidSnapshot = expected;
    invalidSnapshot.slots[0].wet = std::numeric_limits<float>::quiet_NaN();
    CHECK(mixer::detail::encodePreset(invalidSnapshot, encoded)
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
