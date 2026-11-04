#include "woodyn/simulation/ImpactSound.hpp"

#include <algorithm>
#include <cmath>

namespace woodyn::simulation {

float impactAmplitude(float speed, float mass) {
    constexpr float kReferenceSpeed = 2.5f;    // ~30 cm drop
    constexpr float kReferenceMass = 0.0194f;  // beech cube
    const float s = std::pow(std::max(speed, 0.0f) / kReferenceSpeed, 1.1f);
    const float m = std::pow(std::max(mass, 1e-4f) / kReferenceMass, 0.25f);
    return std::clamp(s * m, 0.0f, 1.6f);
}

void playImpact(audio::ImpactSynth& synth, const Simulation& sim, const SoundImpact& impact,
                float pan, float gain) {
    const auto& blocks = sim.blocks();
    auto blockAmp = [&](std::size_t index) {
        return impactAmplitude(impact.speed, sim.world().mass(blocks[index].body));
    };
    const bool twoBlocks = impact.a.block && impact.b.block;
    for (const ImpactSide* side : {&impact.a, &impact.b}) {
        if (side->block) {
            const auto& b = blocks.at(*side->block);
            const float amp = blockAmp(*side->block) * (twoBlocks ? 0.75f : 1.0f) * gain;
            synth.trigger(audio::blockModel(b.type, b.species), amp, pan);
        } else {
            const ImpactSide& other = (side == &impact.a) ? impact.b : impact.a;
            const float amp = (other.block ? blockAmp(*other.block) : 0.0f) * 0.8f * gain;
            synth.trigger(audio::surfaceModel(side->surface), amp, pan);
        }
    }
}

} // namespace woodyn::simulation
