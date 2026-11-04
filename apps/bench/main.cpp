// woodyn_bench: headless scenario runner for physics tuning and performance (D-10).
//
// Usage: woodyn_bench [--scene tower|house|pile|showcase|drops|field|collapse|knockall]
//                     [--count N] [--seconds S] [--threads T] [--seed N] [--trace]
//                     [--timeline] [--csv]
//                     [--set key=value ...]   (PhysicsTuning overrides, see below)
//
// Performance scenes (Phase 5, docs/PERFORMANCE.md), all on the play mat:
//   field     N cubes as a grid of 10-cube towers, standing (mostly asleep)
//   collapse  the field; at t = 1 s, 30 % of the towers are pushed over
//   knockall  the field; at t = 1 s, every tower is pushed over (worst case)
// Frame statistics group the fixed steps into 60 fps frames (stepHz / 60
// steps each), i.e. the physics cost the viewer pays per displayed frame.

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
    bool timeline = false; // per-0.25 s active count and frame cost
    bool csv = false;      // one machine-readable summary line
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
        else if (a == "--timeline") o.timeline = true;
        else if (a == "--csv") o.csv = true;
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
    std::vector<std::vector<std::size_t>> towers;
    const float knockFraction = opt.scene == "collapse" ? 0.3f : opt.scene == "knockall" ? 1.0f : 0.0f;
    const int knockStep = static_cast<int>(1.0f * opt.tuning.stepHz);
    const bool field = opt.scene == "field" || opt.scene == "collapse" || opt.scene == "knockall";
    if (opt.scene == "drops" || field || !opt.wav.empty()) sim.addPlayArea();
    else sim.addFloor(4.0f);
    namespace sc = simulation::scenes;
    if (opt.scene == "tower") sc::buildTower(sim, opt.count);
    else if (opt.scene == "house") sc::buildHouse(sim);
    else if (opt.scene == "showcase") sc::buildShowcase(sim, {-0.3f, 0.0f, 0.0f});
    else if (opt.scene == "pile") sc::dropPile(sim, opt.count, opt.seed);
    else if (opt.scene == "drops") { /* spawned over time below */ }
    else if (field) towers = sc::buildTowerField(sim, opt.count);
    else {
        std::fprintf(stderr, "unknown scene: %s\n", opt.scene.c_str());
        return 2;
    }

    std::vector<physics::Transform> initial;
    for (const auto& b : sim.blocks()) initial.push_back(b.current);

    const int steps = static_cast<int>(opt.seconds * opt.tuning.stepHz + 0.5f);
    std::vector<double> stepMs;
    stepMs.reserve(static_cast<std::size_t>(steps));
    std::vector<std::uint32_t> activeAt;
    activeAt.reserve(static_cast<std::size_t>(steps));
    double joltMs = 0.0;
    double bookkeepingMs = 0.0;
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
        if (knockFraction > 0.0f && i == knockStep) sc::knockTowers(sim, towers, knockFraction);
        const auto t0 = std::chrono::steady_clock::now();
        sim.stepOnce();
        joltMs += sim.lastStepProfile().physicsMilliseconds;
        bookkeepingMs += sim.lastStepProfile().bookkeepingMilliseconds;
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
        activeAt.push_back(sim.world().activeBodyCount());
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

    // Physics cost per displayed 60 fps frame.
    const int perFrame = std::max(1, static_cast<int>(std::lround(opt.tuning.stepHz / 60.0f)));
    std::vector<double> frameMs;
    std::vector<std::uint32_t> frameActive;
    for (std::size_t i = 0; i + static_cast<std::size_t>(perFrame) <= stepMs.size(); i += static_cast<std::size_t>(perFrame)) {
        double f = 0.0;
        std::uint32_t act = 0;
        for (int k = 0; k < perFrame; ++k) {
            f += stepMs[i + static_cast<std::size_t>(k)];
            act = std::max(act, activeAt[i + static_cast<std::size_t>(k)]);
        }
        frameMs.push_back(f);
        frameActive.push_back(act);
    }
    std::vector<double> frameSorted = frameMs;
    std::sort(frameSorted.begin(), frameSorted.end());
    double frameSum = 0.0;
    for (double v : frameMs) frameSum += v;
    const double frameAvg = frameMs.empty() ? 0.0 : frameSum / static_cast<double>(frameMs.size());
    const double frameP99 = frameSorted.empty() ? 0.0
        : frameSorted[static_cast<std::size_t>(0.99 * static_cast<double>(frameSorted.size() - 1))];
    const double frameMax = frameSorted.empty() ? 0.0 : frameSorted.back();
    const std::uint32_t peakActive = activeAt.empty() ? 0u : *std::max_element(activeAt.begin(), activeAt.end());
    constexpr double kBudgetMs = 1000.0 / 60.0;
    const auto overBudget = std::count_if(frameMs.begin(), frameMs.end(), [](double f) { return f > kBudgetMs; });

    if (opt.timeline) {
        // 0.25 s windows: 15 frames each.
        constexpr std::size_t kWindow = 15;
        std::printf("    t[s]  active(max)  frame avg [ms]  frame max [ms]\n");
        for (std::size_t w = 0; w < frameMs.size(); w += kWindow) {
            double fs = 0.0;
            double fm = 0.0;
            std::uint32_t am = 0;
            const std::size_t end = std::min(frameMs.size(), w + kWindow);
            for (std::size_t k = w; k < end; ++k) {
                fs += frameMs[k];
                fm = std::max(fm, frameMs[k]);
                am = std::max(am, frameActive[k]);
            }
            std::printf("  %6.2f  %11u  %14.2f  %14.2f\n", static_cast<double>(w) / 60.0, am,
                        fs / static_cast<double>(end - w), fm);
        }
    }

    if (!opt.wav.empty()) {
        writeWav(opt.wav, audioOut, kSampleRate);
        std::printf("wrote %s (%.1f s)\n", opt.wav.c_str(),
                    static_cast<double>(audioOut.size()) / 2.0 / kSampleRate);
    }
    const double totalProfiled = std::max(joltMs + bookkeepingMs, 1e-9);
    if (opt.csv) {
        // scene,blocks,threads,stepHz,stepAvg,stepP99,frameAvg,frameP99,frameMax,overBudget,frames,peakActive,finalActive,joltShare
        std::printf("%s,%zu,%d,%.0f,%.4f,%.4f,%.3f,%.3f,%.3f,%ld,%zu,%u,%u,%.3f\n", opt.scene.c_str(),
                    sim.blocks().size(), opt.tuning.workerThreads, static_cast<double>(opt.tuning.stepHz), avg, p99,
                    frameAvg, frameP99, frameMax, static_cast<long>(overBudget), frameMs.size(), peakActive,
                    sim.world().activeBodyCount(), joltMs / totalProfiled);
        return 0;
    }
    std::printf("scene=%s blocks=%zu steps=%d | step avg=%.3f ms p99=%.3f ms max=%.3f ms | "
                "active=%u maxDisp=%.5f m minY=%.4f m\n",
                opt.scene.c_str(), sim.blocks().size(), steps, avg, p99, sorted.back(),
                sim.world().activeBodyCount(), static_cast<double>(maxDisp), static_cast<double>(minY));
    std::printf("per 60 fps frame (%d steps): avg=%.2f ms p99=%.2f ms max=%.2f ms | over %.1f ms budget: %ld of %zu"
                " | peak active=%u | Jolt %.0f %% / bookkeeping %.0f %%\n",
                perFrame, frameAvg, frameP99, frameMax, kBudgetMs, static_cast<long>(overBudget), frameMs.size(),
                peakActive, 100.0 * joltMs / totalProfiled, 100.0 * bookkeepingMs / totalProfiled);
    return 0;
}
