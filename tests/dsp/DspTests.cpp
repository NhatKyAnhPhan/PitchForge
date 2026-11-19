#include "ParameterIdsCompat.h"
#include "ProcessorLatency.h"
#include "PitchDiagnostics.h"

#include <auralis/audio/AudioTypes.h>
#include <auralis/dsp/ProcessorFactory.h>
#include <auralis/mixer/SignalChain.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace {

using auralis::audio::AudioBlockView;
using auralis::audio::ProcessSpec;
using auralis::dsp::AudioProcessor;
using auralis::dsp::createProcessor;
using auralis::mixer::EffectType;
using auralis::dsp::parameters::Compressor;
using auralis::dsp::parameters::DeEsser;
using auralis::dsp::parameters::Delay;
using auralis::dsp::parameters::Equalizer;
using auralis::dsp::parameters::Limiter;
using auralis::dsp::parameters::PitchCorrection;
using auralis::dsp::parameters::Reverb;

constexpr double kPi = 3.14159265358979323846;

[[noreturn]] void fail(const char* message) {
    std::cerr << "DSP test failure: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition)
        fail(message);
}

std::uint16_t id(std::uint16_t value) {
    return value;
}

void set(
    AudioProcessor& processor,
    std::uint16_t parameterId,
    float value) {
    require(processor.setParameter(parameterId, value), "valid parameter rejected");
}

std::vector<float> makeTone(double sampleRate, double frequency, std::size_t count) {
    std::vector<float> samples(count);
    for (std::size_t i = 0; i < count; ++i)
        samples[i] = 0.5f * static_cast<float>(
            std::sin(2.0 * kPi * frequency * i / sampleRate));
    return samples;
}

void process(AudioProcessor& processor, std::vector<float>& mono) {
    float* channel = mono.data();
    processor.process({&channel, 1, static_cast<std::uint32_t>(mono.size())});
}

void processInBlocks(
    AudioProcessor& processor,
    std::vector<float>& mono,
    std::size_t blockSize) {
    for (std::size_t offset = 0; offset < mono.size(); offset += blockSize) {
        const auto count = static_cast<std::uint32_t>(
            std::min(blockSize, mono.size() - offset));
        float* channel = mono.data() + offset;
        processor.process({&channel, 1, count});
    }
}

double tonePower(
    const std::vector<float>& samples,
    std::size_t start,
    double sampleRate,
    double frequency) {
    const double omega = 2.0 * kPi * frequency / sampleRate;
    const double coefficient = 2.0 * std::cos(omega);
    double s1 = 0.0;
    double s2 = 0.0;
    for (std::size_t i = start; i < samples.size(); ++i) {
        const double s0 = samples[i] + coefficient * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coefficient * s1 * s2;
}

float normalizedLog(float value, float minimum, float maximum) {
    return std::log(value / minimum) / std::log(maximum / minimum);
}

void testFactoryAndIds() {
    constexpr std::array<EffectType, 9> allEffects{
        EffectType::NoiseGate,
        EffectType::Equalizer,
        EffectType::DeEsser,
        EffectType::Compressor,
        EffectType::PitchCorrection,
        EffectType::Saturation,
        EffectType::Reverb,
        EffectType::Delay,
        EffectType::Limiter,
    };
    for (const auto type : allEffects) {
        auto processor = createProcessor(type, 17);
        require(processor != nullptr, "factory did not create a supported effect");
        require(processor->isEnabled(), "processor should start enabled");
        const ProcessSpec spec{48000.0, 256, 1};
        processor->prepare(spec);
    }
    require(
        createProcessor(static_cast<EffectType>(255), 1) == nullptr,
        "unknown effect type must return nullptr");

    auto equalizer = createProcessor(EffectType::Equalizer, 0);
    require(!equalizer->setParameter(6, 0.5f), "EQ accepted an unknown ID");
    require(!equalizer->setParameter(
        id(static_cast<std::uint16_t>(Equalizer::Frequency)),
        std::numeric_limits<float>::quiet_NaN()),
        "processor accepted a non-finite normalized value");
    set(*equalizer, id(static_cast<std::uint16_t>(Equalizer::Gain)), 2.0f);
    require(
        std::abs(equalizer->getParameter(
            id(static_cast<std::uint16_t>(Equalizer::Gain))) - 1.0f) < 1.0e-6f,
        "normalized parameter was not clamped");

    auto pitch = createProcessor(EffectType::PitchCorrection, 123);
    require(pitch->setParameter(
        static_cast<std::uint16_t>(PitchCorrection::CustomScaleFirstNote), 1.0f),
        "custom pitch-scale IDs should be supported");
    require(pitch->setParameter(
        static_cast<std::uint16_t>(PitchCorrection::CustomScaleLastNote), 0.0f),
        "last custom pitch-scale ID should be supported");
    require(!pitch->setParameter(20, 0.5f), "pitch accepted an unassigned ID");
}

void testEqualizerResponse() {
    constexpr double sampleRate = 48000.0;
    auto equalizer = createProcessor(EffectType::Equalizer, 0);
    set(*equalizer,
        static_cast<std::uint16_t>(Equalizer::Frequency),
        normalizedLog(1000.0f, 20.0f, 20000.0f));
    set(*equalizer, static_cast<std::uint16_t>(Equalizer::Gain), 0.8333333f);
    set(*equalizer,
        static_cast<std::uint16_t>(Equalizer::Q),
        normalizedLog(1.0f, 0.25f, 12.0f));
    equalizer->prepare({sampleRate, 512, 1});

    auto tone = makeTone(sampleRate, 1000.0, 48000);
    processInBlocks(*equalizer, tone, 257);
    const double boosted = tonePower(tone, tone.size() / 2, sampleRate, 1000.0);
    require(boosted > 1.7e7, "EQ boost at its center frequency was too small");

    auto highPass = createProcessor(EffectType::Equalizer, 0);
    set(*highPass,
        static_cast<std::uint16_t>(Equalizer::HighPassFrequency),
        normalizedLog(1000.0f, 20.0f, 20000.0f));
    highPass->prepare({sampleRate, 512, 1});
    auto bass = makeTone(sampleRate, 100.0, 48000);
    processInBlocks(*highPass, bass, 128);
    const double attenuated = tonePower(bass, bass.size() / 2, sampleRate, 100.0);
    require(attenuated < 2.0e6, "EQ high-pass did not attenuate bass");
}

void testDynamics() {
    constexpr double sampleRate = 48000.0;
    auto compressor = createProcessor(EffectType::Compressor, 0);
    set(*compressor,
        static_cast<std::uint16_t>(Compressor::Threshold),
        2.0f / 3.0f);
    set(*compressor,
        static_cast<std::uint16_t>(Compressor::Ratio),
        3.0f / 19.0f);
    set(*compressor, static_cast<std::uint16_t>(Compressor::Attack), 0.0f);
    set(*compressor, static_cast<std::uint16_t>(Compressor::Knee), 0.0f);
    compressor->prepare({sampleRate, 256, 1});
    auto loud = makeTone(sampleRate, 1000.0, 48000);
    processInBlocks(*compressor, loud, 256);
    require(
        tonePower(loud, loud.size() / 2, sampleRate, 1000.0) < 5.0e6,
        "compressor did not reduce loud tone level");

    auto gate = createProcessor(EffectType::NoiseGate, 0);
    set(*gate, 0, 0.6f);
    set(*gate, 1, 0.0f);
    set(*gate, 2, 0.0f);
    set(*gate, 3, 0.0f);
    set(*gate, 4, 0.0f);
    gate->prepare({sampleRate, 256, 1});
    std::vector<float> quiet(24000, 0.0001f);
    processInBlocks(*gate, quiet, 256);
    require(std::abs(quiet.back()) < 1.0e-5f, "noise gate did not close");
    std::vector<float> signal(2048, 0.5f);
    processInBlocks(*gate, signal, 256);
    require(signal.back() > 0.2f, "noise gate did not reopen");
}

void testDeEsserAndSaturation() {
    constexpr double sampleRate = 48000.0;
    auto deEsser = createProcessor(EffectType::DeEsser, 0);
    set(*deEsser,
        static_cast<std::uint16_t>(DeEsser::Frequency),
        normalizedLog(8000.0f, 2000.0f, 12000.0f));
    set(*deEsser,
        static_cast<std::uint16_t>(DeEsser::Threshold),
        0.3f);
    set(*deEsser,
        static_cast<std::uint16_t>(DeEsser::Ratio),
        1.0f);
    set(*deEsser,
        static_cast<std::uint16_t>(DeEsser::Attack),
        0.0f);
    set(*deEsser,
        static_cast<std::uint16_t>(DeEsser::Release),
        0.0f);
    deEsser->prepare({sampleRate, 256, 1});
    auto sibilance = makeTone(sampleRate, 8000.0, 48000);
    processInBlocks(*deEsser, sibilance, 256);
    require(
        tonePower(sibilance, sibilance.size() / 2, sampleRate, 8000.0) < 5.0e6,
        "de-esser did not reduce high-frequency energy");

    auto saturation = createProcessor(EffectType::Saturation, 0);
    set(*saturation, 0, 0.75f);
    set(*saturation, 1, 1.0f);
    set(*saturation, 2, 0.5f);
    saturation->prepare({sampleRate, 256, 1});
    auto fundamental = makeTone(sampleRate, 1000.0, 48000);
    processInBlocks(*saturation, fundamental, 256);
    const double fundamentalPower = tonePower(
        fundamental, fundamental.size() / 2, sampleRate, 1000.0);
    const double thirdHarmonicPower = tonePower(
        fundamental, fundamental.size() / 2, sampleRate, 3000.0);
    require(
        thirdHarmonicPower > fundamentalPower * 0.005,
        "saturation did not create measurable harmonics");
}

void testDelayAndReverb() {
    constexpr double sampleRate = 48000.0;
    auto delay = createProcessor(EffectType::Delay, 0);
    set(*delay,
        static_cast<std::uint16_t>(Delay::Time),
        normalizedLog(0.05f, 0.01f, 2.0f));
    set(*delay, static_cast<std::uint16_t>(Delay::Feedback), 0.0f);
    set(*delay, static_cast<std::uint16_t>(Delay::StereoOffset), 0.0f);
    set(*delay, static_cast<std::uint16_t>(Delay::Tone), 1.0f);
    delay->prepare({sampleRate, 512, 1});
    std::vector<float> impulse(12000, 0.0f);
    impulse[0] = 1.0f;
    processInBlocks(*delay, impulse, 512);
    require(std::abs(impulse[2400]) > 0.5f, "delay echo missed its configured time");

    auto reverb = createProcessor(EffectType::Reverb, 0);
    set(*reverb, static_cast<std::uint16_t>(Reverb::PreDelay), 0.0f);
    set(*reverb, static_cast<std::uint16_t>(Reverb::Decay), 1.0f);
    set(*reverb, static_cast<std::uint16_t>(Reverb::Size), 0.5f);
    reverb->prepare({sampleRate, 512, 1});
    std::vector<float> reverbImpulse(24000, 0.0f);
    reverbImpulse[0] = 1.0f;
    processInBlocks(*reverb, reverbImpulse, 512);
    double tailEnergy = 0.0;
    for (std::size_t i = 3000; i < reverbImpulse.size(); ++i)
        tailEnergy += reverbImpulse[i] * reverbImpulse[i];
    require(tailEnergy > 0.02, "reverb did not produce an impulse tail");
}

void testLimiterAndLatency() {
    constexpr double sampleRate = 48000.0;
    auto limiter = createProcessor(EffectType::Limiter, 0);
    set(*limiter, static_cast<std::uint16_t>(Limiter::Threshold), 1.0f);
    set(*limiter, static_cast<std::uint16_t>(Limiter::Release), 0.0f);
    set(*limiter, static_cast<std::uint16_t>(Limiter::Ceiling), 1.0f);
    limiter->prepare({sampleRate, 256, 1});
    std::vector<float> impulse(2048, 0.0f);
    impulse[0] = 8.0f;
    processInBlocks(*limiter, impulse, 128);
    const float peak = *std::max_element(
        impulse.begin(), impulse.end(), [](float left, float right) {
            return std::abs(left) < std::abs(right);
        });
    require(std::abs(peak) <= 1.001f, "look-ahead limiter exceeded its ceiling");
    require(
        auralis::dsp::getKnownLatencySamples(*limiter, sampleRate) == 240,
        "limiter latency report did not match its 5 ms look-ahead");

    auto pitch = createProcessor(EffectType::PitchCorrection, 1);
    set(*pitch,
        static_cast<std::uint16_t>(PitchCorrection::CorrectionAmount),
        0.0f);
    pitch->prepare({sampleRate, 512, 1});
    std::vector<float> pitchImpulse(5000, 0.0f);
    pitchImpulse[512] = 1.0f;
    process(*pitch, pitchImpulse);
    const auto firstNonzero = std::find_if(
        pitchImpulse.begin(), pitchImpulse.end(),
        [](float value) { return std::abs(value) > 1.0e-4f; });
    require(firstNonzero != pitchImpulse.end(), "pitch shifter lost its impulse");
    require(
        std::distance(pitchImpulse.begin(), firstNonzero) == 2560,
        "pitch shifter impulse did not show its 2048-sample frame delay");
    require(
        auralis::dsp::getKnownLatencySamples(*pitch, sampleRate) == 2048,
        "pitch shifter did not report its FFT-frame latency");
}

void testPitchCorrection() {
    for (const double sampleRate : {44100.0, 48000.0, 96000.0}) {
        auto pitch = createProcessor(
            EffectType::PitchCorrection,
            static_cast<std::uint32_t>(sampleRate));
        set(*pitch, static_cast<std::uint16_t>(PitchCorrection::Key), 0.0f);
        set(*pitch,
            static_cast<std::uint16_t>(PitchCorrection::Scale),
            1.0f / 3.0f);
        set(*pitch,
            static_cast<std::uint16_t>(PitchCorrection::RetuneSpeed),
            0.0f);
        set(*pitch,
            static_cast<std::uint16_t>(PitchCorrection::CorrectionAmount),
            1.0f);
        set(*pitch,
            static_cast<std::uint16_t>(PitchCorrection::Humanize),
            0.0f);
        set(*pitch,
            static_cast<std::uint16_t>(PitchCorrection::Vibrato),
            0.0f);
        pitch->prepare({sampleRate, 256, 1});
        auto tone = makeTone(
            sampleRate,
            466.1638,
            static_cast<std::size_t>(sampleRate * 1.5));
        processInBlocks(*pitch, tone, 256);

        const std::size_t windowSize = static_cast<std::size_t>(
            sampleRate * 0.25);
        const std::size_t start = tone.size() - windowSize;
        const double uncorrectedPower = tonePower(
            tone, start, sampleRate, 466.1638);
        auralis::dsp::PitchCorrectionDiagnostics diagnostics;
        require(
            auralis::dsp::getPitchCorrectionDiagnostics(*pitch, diagnostics),
            "pitch diagnostics unavailable");
        const double correctedPower = tonePower(
            tone,
            start,
            sampleRate,
            diagnostics.targetFrequencyHz);
        require(
            diagnostics.confidence > 0.8f
                && std::abs(diagnostics.detectedFrequencyHz - 466.1638f) < 3.0f,
            "YIN pitch detector did not track the input tone");
        require(
            correctedPower > uncorrectedPower * 1.5,
            "pitch correction did not shift an out-of-scale note to its scale target");
    }
}

void testBypassAndRobustness() {
    constexpr std::array<EffectType, 9> allEffects{
        EffectType::NoiseGate,
        EffectType::Equalizer,
        EffectType::DeEsser,
        EffectType::Compressor,
        EffectType::PitchCorrection,
        EffectType::Saturation,
        EffectType::Reverb,
        EffectType::Delay,
        EffectType::Limiter,
    };
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const auto type : allEffects) {
            auto processor = createProcessor(type, 4);
            processor->prepare({rate, 127, 2});

            std::array<float, 128> left{};
            std::array<float, 128> right{};
            std::array<float*, 2> channels{left.data(), right.data()};
            processor->process({channels.data(), 2, 128});
            require(
                std::all_of(left.begin(), left.end(), [](float value) {
                    return value == 0.0f;
                })
                    && std::all_of(right.begin(), right.end(), [](float value) {
                        return value == 0.0f;
                    }),
                "effect generated output from silence");

            for (std::uint16_t parameterId = 0; parameterId < 48; ++parameterId) {
                if (processor->setParameter(parameterId, 0.0f)) {
                    require(
                        processor->setParameter(parameterId, 1.0f),
                        "parameter rejected an extreme normalized value");
                }
            }
            left[0] = std::numeric_limits<float>::quiet_NaN();
            right[0] = std::numeric_limits<float>::infinity();
            left[1] = -std::numeric_limits<float>::infinity();
            right[1] = 1.0e30f;
            for (std::size_t i = 2; i < left.size(); ++i) {
                left[i] = (i % 2 == 0) ? 64.0f : -64.0f;
                right[i] = static_cast<float>(i % 17) * 0.1f;
            }
            processor->process({channels.data(), 2, 128});
            for (const float value : left)
                require(std::isfinite(value), "effect emitted NaN/Inf on left channel");
            for (const float value : right)
                require(std::isfinite(value), "effect emitted NaN/Inf on right channel");

            processor->setEnabled(false);
            std::array<float, 32> bypassLeft{};
            std::array<float, 32> bypassRight{};
            for (std::size_t i = 0; i < bypassLeft.size(); ++i) {
                bypassLeft[i] = static_cast<float>(i) * 0.03125f - 0.25f;
                bypassRight[i] = -bypassLeft[i];
            }
            const auto originalLeft = bypassLeft;
            const auto originalRight = bypassRight;
            std::array<float*, 2> bypassChannels{
                bypassLeft.data(),
                bypassRight.data(),
            };
            processor->process({bypassChannels.data(), 2, 32});
            require(bypassLeft == originalLeft, "disabled effect changed left audio");
            require(bypassRight == originalRight, "disabled effect changed right audio");
        }
    }
}

} // namespace

int main() {
    testFactoryAndIds();
    testEqualizerResponse();
    testDynamics();
    testDeEsserAndSaturation();
    testDelayAndReverb();
    testLimiterAndLatency();
    testPitchCorrection();
    testBypassAndRobustness();
    std::cout << "Auralis DSP tests passed\n";
    return 0;
}
