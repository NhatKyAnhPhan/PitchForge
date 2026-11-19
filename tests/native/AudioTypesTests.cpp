#include <auralis/audio/AudioTypes.h>

#include <array>
#include <cassert>

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

    return 0;
}
