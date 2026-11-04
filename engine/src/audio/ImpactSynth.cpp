#include "woodyn/audio/ImpactSynth.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace woodyn::audio {

ImpactSynth::ImpactSynth(float sampleRate, int maxVoices)
    : m_sampleRate(sampleRate), m_maxVoices(maxVoices) {}

void ImpactSynth::trigger(const ModalModel& model, float amplitude, float pan) {
    if (amplitude <= 0.0f) return;
    Voice v;
    const float nyquist = 0.45f * m_sampleRate;
    for (const auto& mode : model.modes) {
        if (mode.frequency <= 0.0f || mode.frequency >= nyquist) continue;
        const float w = glm::two_pi<float>() * mode.frequency / m_sampleRate;
        const float r = std::exp(-1.0f / (mode.decay * m_sampleRate));
        Oscillator o;
        // Start at phase 90 deg (sine onset) so the attack is click-free.
        o.re = 0.0f;
        o.im = amplitude * mode.gain;
        o.cr = r * std::cos(w);
        o.ci = r * std::sin(w);
        v.modes.push_back(o);
    }
    if (model.clickGain > 0.0f && model.clickDecay > 0.0f) {
        v.click = amplitude * model.clickGain;
        v.clickDecay = std::exp(-1.0f / (model.clickDecay * m_sampleRate));
        const float x = glm::two_pi<float>() * std::min(model.clickCutoffHz, nyquist) / m_sampleRate;
        v.clickAlpha = x / (1.0f + x);
    }
    if (v.modes.empty() && v.click <= 0.0f) return;
    // Equal-power pan.
    const float p = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * glm::pi<float>();
    v.gainL = std::cos(p);
    v.gainR = std::sin(p);

    std::lock_guard lock(m_mutex);
    if (m_pending.size() < 64) m_pending.push_back(std::move(v));
}

float ImpactSynth::nextNoise() {
    // xorshift32 -> [-1, 1)
    m_noise ^= m_noise << 13;
    m_noise ^= m_noise >> 17;
    m_noise ^= m_noise << 5;
    return static_cast<float>(m_noise) * (2.0f / 4294967296.0f) - 1.0f;
}

void ImpactSynth::render(float* out, int frames) {
    {
        std::lock_guard lock(m_mutex);
        for (auto& v : m_pending) m_voices.push_back(std::move(v));
        m_pending.clear();
    }
    // Voice stealing: drop the oldest voices beyond the limit.
    if (static_cast<int>(m_voices.size()) > m_maxVoices) {
        std::sort(m_voices.begin(), m_voices.end(),
                  [](const Voice& a, const Voice& b) { return a.age < b.age; });
        m_voices.resize(static_cast<std::size_t>(m_maxVoices));
    }

    std::fill(out, out + 2 * frames, 0.0f);
    for (auto& v : m_voices) {
        for (int i = 0; i < frames; ++i) {
            float s = 0.0f;
            for (auto& o : v.modes) {
                const float re = o.re * o.cr - o.im * o.ci;
                const float im = o.re * o.ci + o.im * o.cr;
                o.re = re;
                o.im = im;
                s += im;
            }
            if (v.click > 1e-5f) {
                v.clickState += v.clickAlpha * (nextNoise() - v.clickState);
                s += v.clickState * v.click;
                v.click *= v.clickDecay;
            }
            out[2 * i] += s * v.gainL;
            out[2 * i + 1] += s * v.gainR;
        }
        v.age += frames;
    }

    // Remove voices that have decayed below audibility.
    std::erase_if(m_voices, [](const Voice& v) {
        if (v.click > 1e-5f) return false;
        for (const auto& o : v.modes) {
            if (std::abs(o.re) + std::abs(o.im) > 1e-4f) return false;
        }
        return true;
    });

    // Master gain and gentle soft clipping.
    for (int i = 0; i < 2 * frames; ++i) {
        out[i] = std::tanh(out[i] * m_master);
    }
}

int ImpactSynth::activeVoices() const { return static_cast<int>(m_voices.size()); }

} // namespace woodyn::audio
