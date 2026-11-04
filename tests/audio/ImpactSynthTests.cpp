#include "woodyn/audio/ImpactSynth.hpp"

#include <glm/gtc/constants.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace woodyn;

namespace {

std::vector<float> renderMono(audio::ImpactSynth& synth, int frames) {
    std::vector<float> stereo(static_cast<std::size_t>(frames) * 2);
    synth.render(stereo.data(), frames);
    std::vector<float> mono(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        mono[static_cast<std::size_t>(i)] = 0.5f * (stereo[2 * static_cast<std::size_t>(i)] +
                                                    stereo[2 * static_cast<std::size_t>(i) + 1]);
    }
    return mono;
}

float rms(const std::vector<float>& x, std::size_t begin, std::size_t end) {
    double s = 0.0;
    for (std::size_t i = begin; i < end; ++i) s += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return static_cast<float>(std::sqrt(s / static_cast<double>(end - begin)));
}

/// Goertzel power at frequency f.
float power(const std::vector<float>& x, float f, float sampleRate) {
    const float k = 2.0f * std::cos(glm::two_pi<float>() * f / sampleRate);
    float s1 = 0.0f, s2 = 0.0f;
    for (const float v : x) {
        const float s = v + k * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - k * s1 * s2;
}

} // namespace

TEST(ImpactSynth, SilentWithoutTriggers) {
    audio::ImpactSynth synth;
    const auto out = renderMono(synth, 4800);
    EXPECT_EQ(rms(out, 0, out.size()), 0.0f);
}

TEST(ImpactSynth, ImpactRingsAndDecays) {
    audio::ImpactSynth synth;
    const auto& model = audio::blockModel(blocks::BlockType::Beam, materials::WoodSpecies::Beech);
    synth.trigger(model, 1.0f);
    const auto out = renderMono(synth, 48000 / 2);
    const float early = rms(out, 0, 480);          // first 10 ms
    const float late = rms(out, 9600, 14400);      // 200-300 ms
    EXPECT_GT(early, 0.01f);
    EXPECT_LT(late, early * 0.01f) << "wood is highly damped: the knock must die out quickly";
    EXPECT_EQ(synth.activeVoices(), 0) << "decayed voices are released";
}

TEST(ImpactSynth, SpectrumPeaksAtModelFundamental) {
    audio::ImpactSynth synth;
    const auto& model = audio::blockModel(blocks::BlockType::Beam, materials::WoodSpecies::Beech);
    synth.trigger(model, 0.5f);
    const auto out = renderMono(synth, 4800);
    const float f0 = model.modes.front().frequency;
    EXPECT_GT(power(out, f0, 48000.0f), 20.0f * power(out, f0 * 1.37f, 48000.0f));
}

TEST(ImpactSynth, LargerBlocksSoundLower) {
    const float beam = audio::blockModel(blocks::BlockType::Beam, materials::WoodSpecies::Beech).modes.front().frequency;
    const float brick = audio::blockModel(blocks::BlockType::Brick, materials::WoodSpecies::Beech).modes.front().frequency;
    const float cube = audio::blockModel(blocks::BlockType::Cube, materials::WoodSpecies::Beech).modes.front().frequency;
    EXPECT_LT(beam, brick);
    EXPECT_LT(brick, cube);
    for (const float f : {beam, brick, cube}) {
        EXPECT_GT(f, 300.0f);
        EXPECT_LT(f, 5000.0f);
    }
}

TEST(ImpactSynth, HarderImpactsAreLouder) {
    const auto& model = audio::blockModel(blocks::BlockType::Cube, materials::WoodSpecies::Beech);
    audio::ImpactSynth soft;
    audio::ImpactSynth hard;
    soft.trigger(model, 0.1f);
    hard.trigger(model, 0.8f);
    const auto a = renderMono(soft, 2400);
    const auto b = renderMono(hard, 2400);
    EXPECT_GT(rms(b, 0, b.size()), 3.0f * rms(a, 0, a.size()));
}

TEST(ImpactSynth, PanMovesEnergyBetweenChannels) {
    audio::ImpactSynth synth;
    synth.trigger(audio::blockModel(blocks::BlockType::Cube, materials::WoodSpecies::Beech), 0.5f, -1.0f);
    std::vector<float> stereo(2 * 2400);
    synth.render(stereo.data(), 2400);
    double l = 0.0, r = 0.0;
    for (int i = 0; i < 2400; ++i) {
        l += static_cast<double>(std::abs(stereo[2 * static_cast<std::size_t>(i)]));
        r += static_cast<double>(std::abs(stereo[2 * static_cast<std::size_t>(i) + 1]));
    }
    EXPECT_GT(l, 20.0 * r);
}

TEST(ImpactSynth, VoiceLimitIsRespected) {
    audio::ImpactSynth synth(48000.0f, 8);
    const auto& model = audio::blockModel(blocks::BlockType::Cube, materials::WoodSpecies::Beech);
    for (int i = 0; i < 40; ++i) synth.trigger(model, 0.5f);
    std::vector<float> stereo(2 * 64);
    synth.render(stereo.data(), 64);
    EXPECT_LE(synth.activeVoices(), 8);
}

TEST(ImpactSynth, SilentSurfaceProducesNothing) {
    audio::ImpactSynth synth;
    synth.trigger(audio::surfaceModel(audio::SurfaceKind::Silent), 1.0f);
    const auto out = renderMono(synth, 2400);
    EXPECT_EQ(rms(out, 0, out.size()), 0.0f);
}
