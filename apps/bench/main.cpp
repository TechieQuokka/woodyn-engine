// woodyn_bench: headless scenario runner for physics tuning and performance (D-10).
//
// Usage: woodyn_bench [--scene tower|house|pile|showcase] [--count N] [--seconds S]
//                     [--threads T] [--seed N] [--trace]
//                     [--set key=value ...]   (PhysicsTuning overrides, see below)

#include "woodyn/core/Log.hpp"
#include "woodyn/simulation/ImpactSound.hpp"
#include "woodyn/simulation/Scenes.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace woodyn;

namespace {

struct Options {
    std::string scene = "tower";
    int count = 10;
    float seconds = 10.0f;
    std::uint32_t seed = 1234;
    bool trace = false;
    std::string wav; // write the synthesized impact sounds to this file
    physics::PhysicsTuning tuning;
};

bool applyOverride(physics::PhysicsTuning& t, const std::string& kv) {
    const auto eq = kv.find('=');
    if (eq == std::string::npos) {
        return false;
    }
    const std::string key = kv.substr(0, eq);
    const float v = std::stof(kv.substr(eq + 1));
    const std::map<std::string, std::function<void(float)>> setters{
        {"stepHz", [&](float x) { t.stepHz = x; }},
        {"lengthScale", [&](float x) { t.lengthScale = x; }},
        {"convexRadius", [&](float x) { t.convexRadius = x; }},
        {"speculative", [&](float x) { t.speculativeContactDistance = x; }},
        {"slop", [&](float x) { t.penetrationSlop = x; }},
        {"maxPenetration", [&](float x) { t.maxPenetrationDistance = x; }},
        {"manifold", [&](float x) { t.manifoldTolerance = x; }},
        {"pairCache", [&](float x) { t.bodyPairCacheMaxDeltaPosition = x; }},
        {"pairCacheRot", [&](float x) { t.bodyPairCacheMaxDeltaRotationDegrees = x; }},
        {"rolling", [&](float x) { t.rollingResistance = x != 0.0f; }},
        {"preserveLambda", [&](float x) { t.contactPointPreserveLambdaMaxDist = x; }},
        {"minRestitutionVel", [&](float x) { t.minVelocityForRestitution = x; }},
        {"baumgarte", [&](float x) { t.baumgarte = x; }},
        {"velSteps", [&](float x) { t.velocitySteps = static_cast<std::uint32_t>(x); }},
        {"posSteps", [&](float x) { t.positionSteps = static_cast<std::uint32_t>(x); }},
        {"sleepVel", [&](float x) { t.pointVelocitySleepThreshold = x; }},
        {"angularDamping", [&](float x) { t.angularDamping = x; }},
        {"ccd", [&](float x) { t.continuousCollision = x != 0.0f; }},
        {"warmStart", [&](float x) { t.constraintWarmStart = x != 0.0f; }},
        {"pairCacheUse", [&](float x) { t.useBodyPairContactCache = x != 0.0f; }},
        {"manifoldReduction", [&](float x) { t.useManifoldReduction = x != 0.0f; }},
        {"islandSplitter", [&](float x) { t.useLargeIslandSplitter = x != 0.0f; }},
        {"collisionSteps", [&](float x) { t.collisionStepsPerUpdate = static_cast<int>(x); }},
    };
    const auto it = setters.find(key);
    if (it == setters.end()) {
        return false;
    }
    it->second(v);
    return true;
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--scene") o.scene = next();
        else if (a == "--count") o.count = std::stoi(next());
        else if (a == "--seconds") o.seconds = std::stof(next());
        else if (a == "--threads") o.tuning.workerThreads = std::stoi(next());
        else if (a == "--seed") o.seed = static_cast<std::uint32_t>(std::stoul(next()));
        else if (a == "--trace") o.trace = true;
        else if (a == "--wav") o.wav = next();
        else if (a == "--set") {
            const std::string kv = next();
            if (!applyOverride(o.tuning, kv)) {
                std::fprintf(stderr, "unknown override: %s\n", kv.c_str());
                std::exit(2);
            }
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            std::exit(2);
        }
    }
    return o;
}

void writeWav(const std::string& path, const std::vector<float>& stereo, int sampleRate) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const auto dataBytes = static_cast<std::uint32_t>(stereo.size() * 2);
    f.write("RIFF", 4);
    u32(36 + dataBytes);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1); // PCM
    u16(2);
    u32(static_cast<std::uint32_t>(sampleRate));
    u32(static_cast<std::uint32_t>(sampleRate) * 4);
    u16(4);
    u16(16);
    f.write("data", 4);
    u32(dataBytes);
    for (const float v : stereo) {
        const auto s16 = static_cast<std::int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
        f.write(reinterpret_cast<const char*>(&s16), 2);
    }
}

} // namespace

int main(int argc, char** argv) {
    const Options opt = parse(argc, argv);
    log::init(log::Level::Warn);

    simulation::Simulation sim(opt.tuning);
    if (opt.scene == "drops" || !opt.wav.empty()) sim.addPlayArea();
    else sim.addFloor(4.0f);
    namespace sc = simulation::scenes;
    if (opt.scene == "tower") sc::buildTower(sim, opt.count);
    else if (opt.scene == "house") sc::buildHouse(sim);
    else if (opt.scene == "showcase") sc::buildShowcase(sim, {-0.3f, 0.0f, 0.0f});
    else if (opt.scene == "pile") sc::dropPile(sim, opt.count, opt.seed);
    else if (opt.scene == "drops") { /* spawned over time below */ }
    else {
        std::fprintf(stderr, "unknown scene: %s\n", opt.scene.c_str());
        return 2;
    }

    std::vector<physics::Transform> initial;
    for (const auto& b : sim.blocks()) initial.push_back(b.current);

    const int steps = static_cast<int>(opt.seconds * opt.tuning.stepHz + 0.5f);
    std::vector<double> stepMs;
    stepMs.reserve(static_cast<std::size_t>(steps));
    constexpr int kSampleRate = 48000;
    audio::ImpactSynth synth(static_cast<float>(kSampleRate));
    std::vector<float> audioOut;
    const int samplesPerStep = static_cast<int>(static_cast<float>(kSampleRate) / opt.tuning.stepHz);
    for (int i = 0; i < steps; ++i) {
        if (opt.scene == "drops") {
            // One block of every type, dropped from 30 cm, 0.8 s apart.
            const int every = static_cast<int>(0.8f * opt.tuning.stepHz);
            const auto k = static_cast<std::size_t>(i / every);
            if (i % every == 0 && k < blocks::kAllBlockTypes.size()) {
                const auto type = blocks::kAllBlockTypes[k];
                sim.addBlock(type, materials::WoodSpecies::Beech,
                             {{-0.4f + 0.09f * static_cast<float>(k), 0.3f, 0.0f}, {}});
                initial.push_back(sim.blocks().back().current);
            }
        }
        const auto t0 = std::chrono::steady_clock::now();
        sim.stepOnce();
        if (!opt.wav.empty()) {
            for (const auto& e : sim.takeImpacts()) {
                simulation::playImpact(synth, sim, e, std::clamp(e.point.x / 0.5f, -1.0f, 1.0f));
            }
            const std::size_t at = audioOut.size();
            audioOut.resize(at + 2 * static_cast<std::size_t>(samplesPerStep));
            synth.render(audioOut.data() + at, samplesPerStep);
        }
        const auto t1 = std::chrono::steady_clock::now();
        stepMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        if (opt.trace && i % static_cast<int>(opt.tuning.stepHz / 10.0f) == 0) {
            float maxSpeed = 0.0f;
            float maxDisp = 0.0f;
            for (std::size_t k = 0; k < sim.blocks().size(); ++k) {
                const auto& b = sim.blocks()[k];
                maxSpeed = std::max(maxSpeed, glm::length(sim.world().linearVelocity(b.body)));
                maxDisp = std::max(maxDisp, glm::length(b.current.position - initial[k].position));
            }
            std::printf("t=%6.2fs active=%4u maxSpeed=%.5f m/s maxDisp=%.5f m\n",
                        sim.simulatedSeconds(), sim.world().activeBodyCount(), static_cast<double>(maxSpeed),
                        static_cast<double>(maxDisp));
        }
    }

    float maxDisp = 0.0f;
    float minY = 1e9f;
    for (std::size_t k = 0; k < sim.blocks().size(); ++k) {
        const auto& b = sim.blocks()[k];
        maxDisp = std::max(maxDisp, glm::length(b.current.position - initial[k].position));
        minY = std::min(minY, b.current.position.y);
    }
    std::vector<double> sorted = stepMs;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (double v : stepMs) sum += v;
    const double avg = sum / static_cast<double>(stepMs.size());
    const double p99 = sorted[static_cast<std::size_t>(0.99 * static_cast<double>(sorted.size() - 1))];

    if (!opt.wav.empty()) {
        writeWav(opt.wav, audioOut, kSampleRate);
        std::printf("wrote %s (%.1f s)\n", opt.wav.c_str(),
                    static_cast<double>(audioOut.size()) / 2.0 / kSampleRate);
    }
    std::printf("scene=%s blocks=%zu steps=%d | step avg=%.3f ms p99=%.3f ms max=%.3f ms | "
                "active=%u maxDisp=%.5f m minY=%.4f m\n",
                opt.scene.c_str(), sim.blocks().size(), steps, avg, p99, sorted.back(),
                sim.world().activeBodyCount(), static_cast<double>(maxDisp), static_cast<double>(minY));
    return 0;
}
