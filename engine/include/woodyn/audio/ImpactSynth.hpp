#pragma once

#include "woodyn/audio/ModalModels.hpp"

#include <cstdint>
#include <mutex>
#include <vector>

namespace woodyn::audio {

/// Real-time modal synthesizer for impact sounds (D-27). Backend-agnostic: it
/// renders interleaved stereo float samples; the app feeds them to SDL audio.
/// `trigger` may be called from the game thread while `render` runs on the
/// audio thread.
class ImpactSynth {
public:
    explicit ImpactSynth(float sampleRate = 48000.0f, int maxVoices = 48);

    /// Starts a sound. amplitude ~ 0..1.5 (impact strength), pan -1 (left) .. +1 (right).
    void trigger(const ModalModel& model, float amplitude, float pan = 0.0f);

    /// Renders `frames` stereo frames (2 floats each), replacing the buffer content.
    void render(float* interleavedStereo, int frames);

    [[nodiscard]] int activeVoices() const;
    void setMasterGain(float gain) { m_master = gain; }

private:
    struct Oscillator {
        float re = 0.0f, im = 0.0f; // current phasor (amplitude-scaled)
        float cr = 1.0f, ci = 0.0f; // per-sample rotation * decay
    };
    struct Voice {
        std::vector<Oscillator> modes;
        float click = 0.0f;       // noise burst envelope
        float clickDecay = 0.0f;  // per-sample multiplier
        float clickAlpha = 0.0f;  // one-pole low-pass coefficient
        float clickState = 0.0f;
        float gainL = 0.0f, gainR = 0.0f;
        int age = 0;
    };

    float nextNoise();

    float m_sampleRate;
    int m_maxVoices;
    float m_master = 0.3f;
    std::uint32_t m_noise = 0x12345678u;
    mutable std::mutex m_mutex; // guards m_pending
    std::vector<Voice> m_pending;
    std::vector<Voice> m_voices; // audio thread only
};

} // namespace woodyn::audio
