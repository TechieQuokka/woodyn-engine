#include "woodyn/audio/ModalModels.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace woodyn::audio {
namespace {

ModalModel buildBlockModel(blocks::BlockType type, materials::WoodSpecies species) {
    const auto& def = blocks::definition(type);
    const glm::vec3 size = def.halfExtents * 2.0f;
    const float longest = std::max({size.x, size.y, size.z});
    const float shortest = std::min({size.x, size.y, size.z});
    const float aspect = longest / shortest;

    // Fundamental: larger blocks sound lower. Exponent < 1 keeps tiny cubes
    // audible (their true bending modes would be ultrasonic; what we hear from
    // them is mostly the contact transient and low-order rigid "clack").
    float f0 = 1150.0f * std::pow(0.06f / longest, 0.7f);
    // Solid volume makes a block sound fuller/lower (arch vs. beam).
    f0 *= std::pow(def.analyticVolume / (longest * shortest * shortest), -0.15f);

    // Species: oak is slightly less stiff for its density and more damped.
    float pitch = 1.0f;
    float q = 38.0f; // wood quality factor (internal friction ~0.025)
    if (species == materials::WoodSpecies::Oak) {
        pitch = 0.94f;
        q = 32.0f;
    }
    f0 *= pitch;

    // Bar-like blocks ring with beam bending ratios; compact ones are denser.
    const bool barLike = aspect >= 1.9f;
    const std::array<float, 4> ratios = barLike ? std::array<float, 4>{1.0f, 2.76f, 5.40f, 8.93f}
                                                : std::array<float, 4>{1.0f, 1.59f, 2.14f, 2.83f};
    const std::array<float, 4> gains{1.0f, 0.55f, 0.32f, 0.16f};

    ModalModel model;
    for (std::size_t i = 0; i < ratios.size(); ++i) {
        const float f = f0 * ratios[i];
        if (f > 16000.0f) break;
        const float decay = std::clamp(q / (glm::pi<float>() * f), 0.004f, 0.08f);
        model.modes.push_back({f, decay, gains[i]});
    }
    model.clickGain = 0.35f;
    model.clickCutoffHz = 7000.0f;
    model.clickDecay = 0.0015f;
    return model;
}

ModalModel buildSurfaceModel(SurfaceKind kind) {
    ModalModel m;
    switch (kind) {
    case SurfaceKind::PlayMat:
        m.modes = {{110.0f, 0.045f, 0.55f}, {240.0f, 0.025f, 0.28f}, {520.0f, 0.012f, 0.12f}};
        m.clickGain = 0.18f;
        m.clickCutoffHz = 1400.0f;
        m.clickDecay = 0.003f;
        break;
    case SurfaceKind::Wood:
        m.modes = {{380.0f, 0.035f, 0.5f}, {910.0f, 0.020f, 0.3f}, {1730.0f, 0.010f, 0.15f}};
        m.clickGain = 0.3f;
        m.clickCutoffHz = 5000.0f;
        m.clickDecay = 0.0015f;
        break;
    case SurfaceKind::Silent:
        m.clickGain = 0.0f;
        break;
    }
    return m;
}

} // namespace

const ModalModel& blockModel(blocks::BlockType type, materials::WoodSpecies species) {
    static std::map<std::pair<blocks::BlockType, materials::WoodSpecies>, ModalModel> cache = [] {
        std::map<std::pair<blocks::BlockType, materials::WoodSpecies>, ModalModel> all;
        for (const auto t : blocks::kAllBlockTypes) {
            for (const auto s : {materials::WoodSpecies::Beech, materials::WoodSpecies::Oak}) {
                all.emplace(std::make_pair(t, s), buildBlockModel(t, s));
            }
        }
        return all;
    }();
    return cache.at({type, species});
}

const ModalModel& surfaceModel(SurfaceKind kind) {
    static const ModalModel playMat = buildSurfaceModel(SurfaceKind::PlayMat);
    static const ModalModel wood = buildSurfaceModel(SurfaceKind::Wood);
    static const ModalModel silent = buildSurfaceModel(SurfaceKind::Silent);
    switch (kind) {
    case SurfaceKind::PlayMat: return playMat;
    case SurfaceKind::Wood:    return wood;
    case SurfaceKind::Silent:  return silent;
    }
    return silent;
}

} // namespace woodyn::audio
