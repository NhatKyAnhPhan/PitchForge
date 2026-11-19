#include <auralis/dsp/ProcessorFactory.h>

#include <array>
#include <atomic>
#include <memory>

namespace auralis::dsp {

std::atomic<std::uint32_t> gTestSetParameterCount{0};

namespace {

class TestProcessor final : public AudioProcessor {
public:
    void prepare(const audio::ProcessSpec&) override {}

    void reset() noexcept override {}

    void process(audio::AudioBlockView block) noexcept override {
        if (!enabled_) {
            return;
        }
        const float gain = parameters_[1] * 2.0f;
        for (std::uint32_t channel = 0; channel < block.channelCount; ++channel) {
            for (std::uint32_t sample = 0; sample < block.sampleCount; ++sample) {
                block.channels[channel][sample] *= gain;
            }
        }
    }

    void setEnabled(bool enabled) noexcept override {
        enabled_ = enabled;
    }

    bool setParameter(
        std::uint16_t parameterId,
        float normalizedValue) noexcept override {
        if (parameterId != 1) {
            return false;
        }
        parameters_[parameterId] = normalizedValue;
        gTestSetParameterCount.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    [[nodiscard]] bool isEnabled() const noexcept override {
        return enabled_;
    }

    [[nodiscard]] float getParameter(
        std::uint16_t parameterId) const noexcept override {
        if (parameterId >= parameters_.size()) {
            return 0.0f;
        }
        return parameters_[parameterId];
    }

private:
    std::array<float, 2> parameters_{0.0f, 0.5f};
    bool enabled_ = false;
};

} // namespace

std::unique_ptr<AudioProcessor> createProcessor(
    mixer::EffectType,
    std::uint32_t) {
    return std::make_unique<TestProcessor>();
}

} // namespace auralis::dsp
