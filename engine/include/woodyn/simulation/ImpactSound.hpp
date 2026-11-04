#pragma once

#include "woodyn/audio/ImpactSynth.hpp"
#include "woodyn/simulation/Simulation.hpp"

namespace woodyn::simulation {

/// Loudness of an impact: grows with approach speed and (weakly) with mass.
float impactAmplitude(float speed, float mass);

/// Triggers the sounds of one impact: every block involved rings with its own
/// modal model, a static surface adds its own (mat thud, fence knock).
/// @param pan   stereo position -1..1 (computed by the caller from the listener)
/// @param gain  extra gain, e.g. distance attenuation
void playImpact(audio::ImpactSynth& synth, const Simulation& sim, const SoundImpact& impact,
                float pan = 0.0f, float gain = 1.0f);

} // namespace woodyn::simulation
