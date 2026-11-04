// woodyn_viewer: interactive toy viewer for the physics prototype.
//
// Controls (D-31)
//   Camera     W A S D move · right-drag orbit · middle-drag or Shift+right-drag pan · wheel zoom
//   Blocks     left-drag a block: carry it (natural grip keeps its pose)
//              + Shift + mouse up/down: raise/lower    + wheel: raise/lower
//              + Ctrl + mouse drag: turn it in the hand
//              + hold Space: gently align to the nearest square orientation
//              + Q / E: turn 90 deg   R: tip 90 deg     release: drop or throw
//   Create     1-9 drop a block at the cursor (Cube, Brick, Beam, Roof, Cylinder, HalfCyl,
//              Arch, Cone, Pyramid) · C recolor held/hovered block · O beech/oak
//   Scene      F2 tower  F3 house  F4 showcase  F5 pile  F6 grain demo  Delete clear
//   Edit       Ctrl+Z undo · Ctrl+Y / Ctrl+Shift+Z redo
//   Other      P pause · M mute · F1 help · Esc quit
//
// Headless verification:
//   woodyn_viewer --scene house --frames 300 --screenshot out.bmp [--hidden] [--demo-grab]

#include "DebugRenderer.hpp"
#include "OrbitCamera.hpp"
#include "Playroom.hpp"

#include "woodyn/core/Log.hpp"
#include "woodyn/simulation/ImpactSound.hpp"
#include "woodyn/simulation/Scenes.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace woodyn;
namespace scenes = simulation::scenes;

namespace {

struct Options {
    std::string scene = "house";
    int frames = 0;           // > 0: run a fixed number of frames then exit
    std::string screenshot;   // optional BMP written after the last frame
    bool hidden = false;
    bool demoGrab = false;    // scripted pick-and-carry for headless checks
    bool mute = false;
    float zoom = 0.0f;        // camera distance override (screenshots)
    float yaw = -100.0f;      // camera yaw override [rad], -100 = keep default
};

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--scene") o.scene = next();
        else if (a == "--frames") o.frames = std::stoi(next());
        else if (a == "--screenshot") o.screenshot = next();
        else if (a == "--hidden") o.hidden = true;
        else if (a == "--demo-grab") o.demoGrab = true;
        else if (a == "--mute") o.mute = true;
        else if (a == "--zoom") o.zoom = std::stof(next());
        else if (a == "--yaw") o.yaw = std::stof(next());
        else std::fprintf(stderr, "ignoring unknown argument: %s\n", a.c_str());
    }
    return o;
}

/// The 24 axis-aligned orientations (rotation group of the cube).
const std::vector<glm::quat>& axisAlignedRotations() {
    static const std::vector<glm::quat> all = [] {
        std::vector<glm::quat> out;
        const std::array<glm::vec3, 6> axes{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
        for (const auto& x : axes) {
            for (const auto& y : axes) {
                if (std::abs(glm::dot(x, y)) > 0.5f) continue;
                out.push_back(glm::normalize(glm::quat_cast(glm::mat3(x, y, glm::cross(x, y)))));
            }
        }
        return out;
    }();
    return all;
}

glm::quat snapRotation(const glm::quat& q) {
    glm::quat best = q;
    float bestDot = -1.0f;
    for (const auto& c : axisAlignedRotations()) {
        const float d = std::abs(glm::dot(q, c));
        if (d > bestDot) {
            bestDot = d;
            best = c;
        }
    }
    return glm::dot(q, best) < 0.0f ? -best : best;
}

/// SDL audio output for the impact synthesizer (runs on SDL's audio thread).
class AudioOutput {
public:
    explicit AudioOutput(audio::ImpactSynth& synth) : m_synth(synth) {
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
        m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioOutput::callback, this);
        if (!m_stream) {
            log::warn("audio unavailable: {}", SDL_GetError());
            return;
        }
        SDL_ResumeAudioStreamDevice(m_stream);
        log::info("audio: 48 kHz stereo, modal impact synthesis");
    }
    ~AudioOutput() {
        if (m_stream) SDL_DestroyAudioStream(m_stream);
    }
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    [[nodiscard]] bool ok() const { return m_stream != nullptr; }

private:
    static void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
        auto* self = static_cast<AudioOutput*>(user);
        const int frames = additional / static_cast<int>(2 * sizeof(float));
        if (frames <= 0) return;
        self->m_buffer.resize(static_cast<std::size_t>(frames) * 2);
        self->m_synth.render(self->m_buffer.data(), frames);
        SDL_PutAudioStreamData(stream, self->m_buffer.data(), frames * static_cast<int>(2 * sizeof(float)));
    }

    audio::ImpactSynth& m_synth;
    SDL_AudioStream* m_stream = nullptr;
    std::vector<float> m_buffer;
};

class App {
public:
    App(SDL_Renderer* renderer, bool mute) : m_draw(renderer), m_renderer(renderer), m_muted(mute) {
        m_sim.addPlayArea();
        m_draw.setRoom(viewer::buildPlayroom(viewer::Skin{}, &*m_sim.playArea()));
        m_audio = std::make_unique<AudioOutput>(m_synth);
    }

    viewer::OrbitCamera& camera() { return m_camera; }

    void loadScene(const std::string& name, bool undoable = true) {
        if (undoable) pushUndo();
        release();
        m_sim.clearBlocks();
        if (name == "house") scenes::buildHouse(m_sim, {0.0f, 0.0f, 0.0f}, m_species);
        else if (name == "tower") scenes::buildTower(m_sim, 15, 0.0f, 0.0f, m_species);
        else if (name == "showcase") scenes::buildShowcase(m_sim, {-0.4f, 0.0f, 0.0f}, m_species);
        else if (name == "pile") scenes::dropPile(m_sim, 100, m_seed++, {}, m_species);
        else if (name == "grain") scenes::buildGrainDemo(m_sim, m_species);
        else if (name != "empty") log::warn("unknown scene '{}'", name);
    }

    /// Returns false when the user asked to quit. Coordinates are render pixels.
    bool handle(const SDL_Event& e) {
        const SDL_Keymod mod = SDL_GetModState();
        const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
        const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
        auto& world = m_sim.world();
        switch (e.type) {
        case SDL_EVENT_QUIT: return false;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            m_mouse = {e.button.x, e.button.y};
            if (e.button.button == SDL_BUTTON_LEFT) tryGrab(e.button.x, e.button.y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) release();
            break;
        case SDL_EVENT_MOUSE_MOTION:
            m_mouse = {e.motion.x, e.motion.y};
            if (world.grabbedBody()) {
                if (ctrl) {
                    turnInHand(e.motion.xrel, e.motion.yrel);
                } else if (shift) {
                    m_handHeight = std::clamp(m_handHeight - e.motion.yrel * 0.0012f * m_camera.distance, 0.005f, 0.9f);
                    m_handTarget.y = m_handHeight;
                    world.moveGrab(m_handTarget);
                } else {
                    moveHand(e.motion.x, e.motion.y);
                }
            } else if ((e.motion.state & SDL_BUTTON_MMASK) != 0 ||
                       ((e.motion.state & SDL_BUTTON_RMASK) != 0 && shift)) {
                m_camera.pan(e.motion.xrel, e.motion.yrel);
            } else if ((e.motion.state & (SDL_BUTTON_RMASK | SDL_BUTTON_LMASK)) != 0) {
                m_camera.orbit(e.motion.xrel, e.motion.yrel);
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (world.grabbedBody()) {
                m_handHeight = std::clamp(m_handHeight + 0.006f * e.wheel.y, 0.005f, 0.9f);
                m_handTarget.y = m_handHeight;
                world.moveGrab(m_handTarget);
            } else {
                m_camera.zoom(e.wheel.y);
            }
            break;
        case SDL_EVENT_KEY_DOWN: {
            if (e.key.repeat) break;
            const SDL_Keycode k = e.key.key;
            if (k == SDLK_ESCAPE) return false;
            if (ctrl && k == SDLK_Z) { shift ? redo() : undo(); break; }
            if (ctrl && k == SDLK_Y) { redo(); break; }
            if (ctrl) break; // Ctrl is the "turn in hand" modifier: no other shortcuts
            if (k >= SDLK_1 && k <= SDLK_9) spawnAtCursor(static_cast<std::size_t>(k - SDLK_1));
            else if (k == SDLK_F2) loadScene("tower");
            else if (k == SDLK_F3) loadScene("house");
            else if (k == SDLK_F4) loadScene("showcase");
            else if (k == SDLK_F5) loadScene("pile");
            else if (k == SDLK_F6) loadScene("grain");
            else if (k == SDLK_DELETE) { pushUndo(); release(); m_sim.clearBlocks(); }
            else if (k == SDLK_P) m_sim.paused = !m_sim.paused;
            else if (k == SDLK_M) m_muted = !m_muted;
            else if (k == SDLK_F1) m_showHelp = !m_showHelp;
            else if (k == SDLK_Q) turnStep(glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0)));
            else if (k == SDLK_E) turnStep(glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0, 1, 0)));
            else if (k == SDLK_R) tip();
            else if (k == SDLK_C) recolor();
            else if (k == SDLK_O) {
                m_species = m_species == materials::WoodSpecies::Beech ? materials::WoodSpecies::Oak
                                                                        : materials::WoodSpecies::Beech;
            }
            break;
        }
        default: break;
        }
        return true;
    }

    void frame(float dt) {
        moveCameraWithKeys(dt);
        alignWhileSpaceHeld(dt);

        const auto stats = m_sim.advance(dt);
        m_physicsMs = 0.9 * m_physicsMs + 0.1 * stats.physicsMilliseconds;
        m_fps = 0.95 * m_fps + 0.05 * (dt > 0.0f ? 1.0 / static_cast<double>(dt) : 0.0);
        playSounds();

        m_draw.draw(m_sim, m_camera, buildOverlay());
        drawHud();
    }

    /// Scripted pick-and-carry used for headless verification (--demo-grab).
    void scriptedGrab(int frame) {
        if (frame == 60) {
            if (m_sim.blocks().empty()) return;
            const glm::vec3 c = m_sim.interpolated(0).position;
            const auto& def = blocks::definition(m_sim.blocks()[0].type);
            const glm::vec3 top = c + glm::vec3(0.0f, def.halfExtents.y * 0.9f, 0.0f);
            if (const auto hit = m_sim.world().castRay(top + glm::vec3(0, 0.5f, 0), {0, -1, 0}, 1.0f)) {
                beginGrabAt(*hit);
            }
        } else if (frame > 60 && m_sim.world().grabbedBody()) {
            const float t = std::min(static_cast<float>(frame - 60) / 120.0f, 1.0f);
            m_handTarget = m_grabStart + glm::vec3(0.12f * t, 0.06f * t + 0.02f, 0.04f * t);
            m_handHeight = m_handTarget.y;
            m_sim.world().moveGrab(m_handTarget);
        }
    }

private:
    // --- Undo / redo (D-31) ------------------------------------------------------
    void pushUndo() {
        m_undo.push_back(m_sim.snapshot());
        if (m_undo.size() > 64) m_undo.erase(m_undo.begin());
        m_redo.clear();
    }
    void undo() {
        if (m_undo.empty()) return;
        release();
        m_redo.push_back(m_sim.snapshot());
        m_sim.restore(m_undo.back());
        m_undo.pop_back();
    }
    void redo() {
        if (m_redo.empty()) return;
        release();
        m_undo.push_back(m_sim.snapshot());
        m_sim.restore(m_redo.back());
        m_redo.pop_back();
    }

    // --- Camera ------------------------------------------------------------------
    void moveCameraWithKeys(float dt) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if ((SDL_GetModState() & SDL_KMOD_CTRL) != 0) return;
        glm::vec3 forward = m_camera.target - m_camera.eye();
        forward.y = 0.0f;
        if (glm::length(forward) < 1e-5f) return;
        forward = glm::normalize(forward);
        const glm::vec3 right = glm::cross(forward, glm::vec3(0, 1, 0));
        glm::vec3 move{0.0f};
        if (keys[SDL_SCANCODE_W]) move += forward;
        if (keys[SDL_SCANCODE_S]) move -= forward;
        if (keys[SDL_SCANCODE_D]) move += right;
        if (keys[SDL_SCANCODE_A]) move -= right;
        if (glm::length(move) > 0.0f) {
            m_camera.target += glm::normalize(move) * (0.5f * m_camera.distance * dt);
            if (m_sim.world().grabbedBody()) moveHand(m_mouse.x, m_mouse.y);
        }
        constrainCamera();
    }

    void constrainCamera() {
        // Stay over the play area and inside the room (D-28).
        const float h = m_sim.playArea() ? m_sim.playArea()->halfSize : 0.6f;
        m_camera.target.x = std::clamp(m_camera.target.x, -h, h);
        m_camera.target.z = std::clamp(m_camera.target.z, -h, h);
        m_camera.target.y = std::clamp(m_camera.target.y, 0.0f, 0.4f);
        m_camera.distance = std::clamp(m_camera.distance, 0.12f, 1.6f);
        m_camera.pitch = std::clamp(m_camera.pitch, 0.06f, 1.45f);
    }

    // --- Picking / holding -----------------------------------------------------------
    std::optional<physics::RayHit> pick(float x, float y, bool blocksOnly = true) const {
        int w = 0, h = 0;
        SDL_GetRenderOutputSize(m_renderer, &w, &h);
        const auto [origin, dir] = viewer::DebugRenderer::pickRay(m_camera, x, y, w, h);
        return m_sim.world().castRay(origin, dir, 20.0f, blocksOnly);
    }

    void tryGrab(float x, float y) {
        if (const auto hit = pick(x, y)) beginGrabAt(*hit);
    }

    void beginGrabAt(const physics::RayHit& hit) {
        pushUndo();
        auto& world = m_sim.world();
        world.beginGrab(hit.body, hit.point);
        m_grabStart = hit.point;
        m_handHeight = hit.point.y + 0.02f; // lift a little, as a hand does
        m_handTarget = {hit.point.x, m_handHeight, hit.point.z};
        world.moveGrab(m_handTarget);
        // Natural grip (D-26): keep the pose it was picked up in.
        m_targetRotation = world.transform(hit.body).rotation;
        world.setGrabOrientation(m_targetRotation);
    }

    void moveHand(float x, float y) {
        int w = 0, h = 0;
        SDL_GetRenderOutputSize(m_renderer, &w, &h);
        const auto [origin, dir] = viewer::DebugRenderer::pickRay(m_camera, x, y, w, h);
        if (std::abs(dir.y) < 1e-4f) return;
        const float t = (m_handHeight - origin.y) / dir.y;
        if (t <= 0.0f) return;
        m_handTarget = origin + dir * t;
        const float limit = m_sim.playArea() ? m_sim.playArea()->halfSize - 0.01f : 1.0f;
        m_handTarget.x = std::clamp(m_handTarget.x, -limit, limit);
        m_handTarget.z = std::clamp(m_handTarget.z, -limit, limit);
        m_sim.world().moveGrab(m_handTarget);
    }

    void release() { m_sim.world().endGrab(); }

    void turnInHand(float dx, float dy) {
        const glm::vec3 fwd = glm::normalize(m_camera.target - m_camera.eye());
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
        m_targetRotation = glm::normalize(glm::angleAxis(dx * 0.012f, glm::vec3(0, 1, 0)) *
                                          glm::angleAxis(dy * 0.012f, right) * m_targetRotation);
        m_sim.world().setGrabOrientation(m_targetRotation);
    }

    void turnStep(const glm::quat& r) {
        if (!m_sim.world().grabbedBody()) return;
        m_targetRotation = glm::normalize(r * m_targetRotation);
        m_sim.world().setGrabOrientation(m_targetRotation);
    }

    void tip() {
        const glm::vec3 fwd = m_camera.target - m_camera.eye();
        const glm::vec3 axis = std::abs(fwd.x) > std::abs(fwd.z) ? glm::vec3(0, 0, fwd.x > 0 ? 1.0f : -1.0f)
                                                               : glm::vec3(fwd.z > 0 ? -1.0f : 1.0f, 0, 0);
        turnStep(glm::angleAxis(glm::half_pi<float>(), axis));
    }

    void alignWhileSpaceHeld(float dt) {
        if (!m_sim.world().grabbedBody()) return;
        if (!SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_SPACE]) return;
        // Option B: alignment only on demand, and gradual rather than a snap.
        const float k = 1.0f - std::exp(-8.0f * dt);
        m_targetRotation = glm::normalize(glm::slerp(m_targetRotation, snapRotation(m_targetRotation), k));
        m_sim.world().setGrabOrientation(m_targetRotation);
    }

    // --- Creating / recoloring ---------------------------------------------------------
    void spawnAtCursor(std::size_t typeIndex) {
        if (typeIndex >= blocks::kAllBlockTypes.size()) return;
        pushUndo();
        const auto type = blocks::kAllBlockTypes[typeIndex];
        const auto& def = blocks::definition(type);
        glm::vec3 at = m_camera.target;
        if (const auto hit = pick(m_mouse.x, m_mouse.y, false)) at = hit->point;
        const float limit = (m_sim.playArea() ? m_sim.playArea()->halfSize : 0.6f) - 0.07f;
        at.x = std::clamp(at.x, -limit, limit);
        at.z = std::clamp(at.z, -limit, limit);
        std::uniform_real_distribution<float> yaw(0.0f, glm::two_pi<float>());
        physics::Transform t;
        t.position = at + glm::vec3(0.0f, def.halfExtents.y + 0.05f, 0.0f);
        t.rotation = glm::angleAxis(yaw(m_rng), glm::vec3(0, 1, 0));
        m_sim.addBlock(type, m_species, t, m_nextStain);
        m_nextStain = materials::nextStain(m_nextStain);
    }

    void recolor() {
        std::optional<std::size_t> target;
        if (const auto body = m_sim.world().grabbedBody()) target = m_sim.blockIndex(*body);
        else if (const auto hit = pick(m_mouse.x, m_mouse.y)) target = m_sim.blockIndex(hit->body);
        if (!target) return;
        pushUndo();
        m_sim.setStain(*target, materials::nextStain(m_sim.blocks()[*target].stain));
    }

    // --- Sound -----------------------------------------------------------------------------
    void playSounds() {
        const auto impacts = m_sim.takeImpacts();
        if (m_muted || !m_audio || !m_audio->ok()) return;
        const glm::vec3 eye = m_camera.eye();
        const glm::vec3 fwd = glm::normalize(m_camera.target - eye);
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
        for (const auto& e : impacts) {
            const glm::vec3 rel = e.point - eye;
            const float dist = glm::length(rel);
            const float pan = std::clamp(glm::dot(rel, right) / std::max(dist, 0.2f) * 1.3f, -1.0f, 1.0f);
            const float gain = 1.0f / (1.0f + 0.8f * dist);
            simulation::playImpact(m_synth, m_sim, e, pan, gain);
        }
    }

    // --- Overlay / HUD -------------------------------------------------------------------
    viewer::Overlay buildOverlay() const {
        viewer::Overlay o;
        auto& world = m_sim.world();
        if (const auto body = world.grabbedBody()) {
            o.grabbed = m_sim.blockIndex(*body);
            if (o.grabbed) {
                if (const auto preview = world.dropPreview(*body)) {
                    if (world.transform(*body).position.y - preview->position.y > 0.003f) {
                        o.ghost = viewer::Overlay::Ghost{m_sim.blocks()[*o.grabbed].type, *preview};
                    }
                }
            }
            if (const auto p = world.grabPoint()) o.rubberBand = std::make_pair(*p, m_handTarget);
        } else if (const auto hit = pick(m_mouse.x, m_mouse.y)) {
            o.hovered = m_sim.blockIndex(hit->body);
        }
        return o;
    }

    void text(float x, float y, const std::string& s) {
        SDL_SetRenderDrawColorFloat(m_renderer, 0.25f, 0.20f, 0.18f, 1.0f);
        SDL_RenderDebugText(m_renderer, x, y, s.c_str());
    }

    void drawHud() {
        char line[256];
        std::snprintf(line, sizeof(line), "WOODYN | %.0f fps | physics %.2f ms @ %.0f Hz | blocks %zu (active %u)%s",
                      m_fps, m_physicsMs, static_cast<double>(m_sim.world().tuning().stepHz),
                      m_sim.blocks().size(), m_sim.world().activeBodyCount(), m_sim.paused ? " | PAUSED" : "");
        text(10.0f, 10.0f, line);
        std::snprintf(line, sizeof(line), "%s | next color: %s | sound: %s%s",
                      std::string(materials::toString(m_species)).c_str(),
                      std::string(materials::toString(m_nextStain)).c_str(),
                      m_muted ? "muted" : (m_audio && m_audio->ok() ? "on" : "unavailable"),
                      m_sim.world().grabbedBody() ? " | HOLDING" : "");
        text(10.0f, 24.0f, line);
        if (m_showHelp) {
            text(10.0f, 44.0f, "Left-drag block: carry | Shift+mouse/wheel: lift | Ctrl+mouse: turn | hold Space: align | Q/E/R: 90 deg");
            text(10.0f, 58.0f, "WASD: move | right-drag: orbit | middle/Shift+right-drag: pan | wheel: zoom");
            text(10.0f, 72.0f, "1-9: new block at cursor | C: recolor | O: beech/oak | F2-F6: scenes | Del: clear");
            text(10.0f, 86.0f, "Ctrl+Z: undo | Ctrl+Y: redo | P: pause | M: mute | F1: help | Esc: quit");
        }
    }

    simulation::Simulation m_sim;
    viewer::OrbitCamera m_camera;
    viewer::DebugRenderer m_draw;
    SDL_Renderer* m_renderer;
    audio::ImpactSynth m_synth;
    std::unique_ptr<AudioOutput> m_audio;
    std::vector<simulation::SceneSnapshot> m_undo;
    std::vector<simulation::SceneSnapshot> m_redo;
    materials::WoodSpecies m_species = materials::WoodSpecies::Beech;
    materials::Stain m_nextStain = materials::Stain::Red;
    std::mt19937 m_rng{7};
    std::uint32_t m_seed = 1;
    double m_physicsMs = 0.0;
    double m_fps = 60.0;
    bool m_showHelp = true;
    bool m_muted = false;
    SDL_FPoint m_mouse{-1.0f, -1.0f};
    float m_handHeight = 0.05f;
    glm::vec3 m_handTarget{0.0f};
    glm::vec3 m_grabStart{0.0f};
    glm::quat m_targetRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace

int main(int argc, char** argv) {
    const Options opt = parse(argc, argv);
    log::init(log::Level::Info);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        log::warn("SDL audio init failed ({}), continuing without sound", SDL_GetError());
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            log::error("SDL_Init failed: {}", SDL_GetError());
            return 1;
        }
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (opt.hidden) flags |= SDL_WINDOW_HIDDEN;

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("WOODYN - Real Wood. Real Physics.", 1280, 800, flags, &window, &renderer)) {
        log::error("SDL_CreateWindowAndRenderer failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, opt.frames > 0 ? 0 : 1);
    log::info("SDL {} renderer: {}", SDL_GetRevision(), SDL_GetRendererName(renderer));

    int exitCode = 0;
    {
        App app(renderer, opt.mute);
        app.loadScene(opt.scene, false);
        if (opt.zoom > 0.0f) app.camera().distance = opt.zoom;
        if (opt.yaw > -99.0f) app.camera().yaw = opt.yaw;

        Uint64 last = SDL_GetTicksNS();
        bool running = true;
        int frame = 0;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                SDL_ConvertEventToRenderCoordinates(renderer, &e); // window points -> render pixels
                running = app.handle(e) && running;
            }
            float dt = 1.0f / 60.0f; // fixed in scripted mode for reproducibility
            const Uint64 now = SDL_GetTicksNS();
            if (opt.frames == 0) dt = static_cast<float>(static_cast<double>(now - last) * 1e-9);
            last = now;

            if (opt.demoGrab) app.scriptedGrab(frame);
            app.frame(dt);
            ++frame;
            const bool lastFrame = opt.frames > 0 && frame >= opt.frames;
            if (lastFrame && !opt.screenshot.empty()) {
                if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
                    if (!SDL_SaveBMP(shot, opt.screenshot.c_str())) {
                        log::error("SDL_SaveBMP failed: {}", SDL_GetError());
                        exitCode = 1;
                    } else {
                        log::info("screenshot written to {}", opt.screenshot);
                    }
                    SDL_DestroySurface(shot);
                }
            }
            SDL_RenderPresent(renderer);
            if (lastFrame) running = false;
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}
