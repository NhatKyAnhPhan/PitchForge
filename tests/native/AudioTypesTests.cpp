#include <auralis/audio/AudioTypes.h>
#include <auralis/audio/AudioDeviceBackend.h>
#include <auralis/dsp/ProcessorFactory.h>
#include <auralis/dsp/ParameterIds.h>
#include <auralis/bridge/auralis_c_api.h>
#include <auralis/mixer/SignalChain.h>

#include <array>
#include <cassert>
#include <type_traits>

int main() {
    std::array<float, 4> left{};
    std::array<float, 4> right{};
    std::array<float*, 2> channels{left.data(), right.data()};

    const auralis::audio::AudioBlockView valid{channels.data(), 2, 4};
    assert(valid.isValid());

    const auralis::audio::AudioBlockView empty{};
    assert(!empty.isValid());

    const auralis::audio::AudioBlockView noSamples{channels.data(), 2, 0};
    assert(!noSamples.isValid());

    static_assert(std::is_trivially_copyable_v<auralis::mixer::ChainCommand>);
    static_assert(AURALIS_MAX_EFFECTS_PER_CHANNEL == 16);
    static_assert(auralis::audio::kDefaultAudioDevice
        == static_cast<std::uint32_t>(-1));
    static_assert(static_cast<std::uint16_t>(
        auralis::dsp::parameters::PitchCorrection::CustomScaleLastNote) == 43);
    assert(auralis::mixer::kMaximumEffectsPerChannel
        == AURALIS_MAX_EFFECTS_PER_CHANNEL);

    return 0;
}
