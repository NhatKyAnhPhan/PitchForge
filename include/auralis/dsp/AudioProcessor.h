#pragma once

#include <auralis/audio/AudioTypes.h>

namespace auralis::dsp {

// Implementations must not allocate, block, perform file/network I/O, or log
// from process(). prepare() is called on a non-realtime thread before use.
class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;

    virtual void prepare(const audio::ProcessSpec& spec) = 0;
    virtual void reset() noexcept = 0;
    virtual void process(audio::AudioBlockView block) noexcept = 0;
    virtual void setEnabled(bool enabled) noexcept = 0;
    virtual bool setParameter(
        std::uint16_t parameterId,
        float normalizedValue) noexcept = 0;

    [[nodiscard]] virtual bool isEnabled() const noexcept = 0;
    [[nodiscard]] virtual float getParameter(
        std::uint16_t parameterId) const noexcept = 0;
};

} // namespace auralis::dsp
