// woodyn_viewer: interactive toy viewer for the physics prototype.
//
// Controls (D-31)
//   Camera     W A S D move · right-drag orbit · middle-drag or Shift+right-drag pan · wheel zoom
//   Blocks     left-drag a block: carry it (natural grip keeps its pose)
//              + Shift + mouse up/down: raise/lower    + wheel: raise/lower
//              + Ctrl + mouse drag: turn it in the hand
//              + hold Space: gently align to the nearest square orientation
//              + Q / E: turn, R / F: tip (tap 1 deg, hold = continuous, Shift = 90 deg)
//              + Ctrl+wheel: 1 deg   Ctrl+Shift+mouse: fine turning   release: drop or throw
//   Shelf      press a block on the bottom shelf to take a new one; color swatches; Tab hides it
//   Window     F11 fullscreen
//   Create     1-9 drop a block at the cursor (Cube, Brick, Beam, Roof, Cylinder, HalfCyl,
//              Arch, Cone, Pyramid) · C recolor held/hovered block · O beech/oak
//   Scene      F2 tower  F3 house  F4 showcase  F5 pile  F6 grain demo  Delete clear
//   Edit       Ctrl+Z undo · Ctrl+Y / Ctrl+Shift+Z redo
//   Rendering  F7 depth of field · F8 ambient occlusion + contact shadows · F9 bloom
//   Other      P pause · M mute · F1 help · Esc quit
//
// Headless verification:
//   woodyn_viewer --scene house --frames 300 --screenshot out.bmp [--hidden] [--demo-grab]

#include "DebugRenderer.hpp"
#include "GpuView.hpp"
#include "OrbitCamera.hpp"
#include "Playroom.hpp"
#include "Shelf.hpp"

#include "woodyn/core/Log.hpp"
#include "woodyn/simulation/ImpactSound.hpp"
#include "woodyn/simulation/Scenes.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <variant>
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
    std::string renderer = "gpu"; // gpu (Vulkan PBR) or soft (debug software renderer)
    bool noPhotoDetail = false;   // compare procedural-only grain
    bool clean = false;           // no HUD and no shelf (screenshots)
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
        else if (a == "--renderer") o.renderer = next();
        else if (a == "--no-photo-detail") o.noPhotoDetail = true;
        else if (a == "--clean") o.clean = true;
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
    App(SDL_Renderer* renderer, SDL_Window* window, SDL_GPUDevice* device, bool mute)
        : m_renderer(renderer), m_muted(mute) {
        m_window = window;
        m_sim.addPlayArea();
        if (device) {
            m_gpuView = std::make_unique<viewer::GpuView>(device, renderer, *m_sim.playArea());
            if (!m_gpuView->ok()) {
                log::warn("GPU renderer unavailable, using the software renderer");
                m_gpuView.reset();
            }
        }
        if (!m_gpuView) {
            m_draw = std::make_unique<viewer::DebugRenderer>(renderer);
            m_draw->setRoom(viewer::buildPlayroom(viewer::Skin{}, &*m_sim.playArea()));
        }
        m_audio = std::make_unique<AudioOutput>(m_synth);
    }

    viewer::OrbitCamera& camera() { return m_camera; }
    void setClean() {
        m_clean = true;
        m_shelf.visible = false;
    }
    void setPhotoDetail(bool on) {
        if (m_gpuView) m_gpuView->settings.photoDetail = on;
    }

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
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (const auto hit = m_shelf.hitTest(e.button.x, e.button.y)) {
                    if (const auto* slot = std::get_if<viewer::Shelf::BlockSlot>(&*hit)) {
                        spawnIntoHand(slot->type, e.button.x, e.button.y);
                    } else if (const auto* sw = std::get_if<viewer::Shelf::StainSwatch>(&*hit)) {
                        m_nextStain = sw->stain;
                    }
                } else if (!m_shelf.contains(e.button.x, e.button.y)) {
                    tryGrab(e.button.x, e.button.y);
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) release();
            break;
        case SDL_EVENT_MOUSE_MOTION:
            m_mouse = {e.motion.x, e.motion.y};
            if (world.grabbedBody()) {
                if (ctrl) {
                    // Ctrl: turn in the hand; Ctrl+Shift: precision (4x slower).
                    turnInHand(e.motion.xrel, e.motion.yrel, shift ? 0.001f : 0.004f);
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
            if (world.grabbedBody() && ctrl) {
                turnDegrees(e.wheel.y, glm::vec3(0, 1, 0)); // 1 deg per notch
            } else if (world.grabbedBody()) {
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
            else if (k == SDLK_F7 && m_gpuView) m_gpuView->settings.dof = !m_gpuView->settings.dof;
            else if (k == SDLK_F8 && m_gpuView) {
                m_gpuView->settings.ssao = !m_gpuView->settings.ssao;
                m_gpuView->settings.contactShadows = m_gpuView->settings.ssao;
            }
            else if (k == SDLK_F9 && m_gpuView) m_gpuView->settings.bloom = !m_gpuView->settings.bloom;
            else if (k == SDLK_F10 && m_gpuView) m_gpuView->settings.photoDetail = !m_gpuView->settings.photoDetail;
            else if (k == SDLK_DELETE) { pushUndo(); release(); m_sim.clearBlocks(); }
            else if (k == SDLK_P) m_sim.paused = !m_sim.paused;
            else if (k == SDLK_M) m_muted = !m_muted;
            else if (k == SDLK_F1) m_showHelp = !m_showHelp;
            else if (k == SDLK_Q || k == SDLK_E || k == SDLK_R || k == SDLK_F) {
                // Tap: 1 deg. Hold: continuous turning (see turnWhileKeysHeld). Shift: 90 deg.
                const float sign = (k == SDLK_Q || k == SDLK_R) ? 1.0f : -1.0f;
                const glm::vec3 axis = (k == SDLK_Q || k == SDLK_E) ? glm::vec3(0, 1, 0) : tipAxis();
                turnDegrees(sign * (shift ? 90.0f : 1.0f), axis);
                m_keyPressTime[rotationKeyIndex(k)] = m_clock;
            }
            else if (k == SDLK_F11) toggleFullscreen();
            else if (k == SDLK_TAB) m_shelf.visible = !m_shelf.visible;
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
        m_clock += static_cast<double>(dt);
        moveCameraWithKeys(dt);
        alignWhileSpaceHeld(dt);
        turnWhileKeysHeld(dt);

        const auto stats = m_sim.advance(dt);
        m_physicsMs = 0.9 * m_physicsMs + 0.1 * stats.physicsMilliseconds;
        m_fps = 0.95 * m_fps + 0.05 * (dt > 0.0f ? 1.0 / static_cast<double>(dt) : 0.0);
        playSounds();

        if (m_gpuView) m_gpuView->draw(m_sim, m_camera, buildOverlay());
        else m_draw->draw(m_sim, m_camera, buildOverlay());
        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize(m_renderer, &w, &h);
        m_shelf.layout(w, h);
        m_shelf.draw(m_renderer, m_nextStain, m_species, m_mouse.x, m_mouse.y);
        if (!m_clean) drawHud();
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

    void turnInHand(float dx, float dy, float radiansPerPixel) {
        const glm::vec3 fwd = glm::normalize(m_camera.target - m_camera.eye());
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
        m_targetRotation = glm::normalize(glm::angleAxis(dx * radiansPerPixel, glm::vec3(0, 1, 0)) *
                                          glm::angleAxis(dy * radiansPerPixel, right) * m_targetRotation);
        m_sim.world().setGrabOrientation(m_targetRotation);
    }

    /// Rotates the held block's target orientation about a world axis.
    void turnDegrees(float degrees, const glm::vec3& axis) {
        if (!m_sim.world().grabbedBody()) return;
        m_targetRotation = glm::normalize(glm::angleAxis(glm::radians(degrees), axis) * m_targetRotation);
        m_sim.world().setGrabOrientation(m_targetRotation);
    }

    /// World axis used for tipping: the horizontal axis closest to the camera's right.
    glm::vec3 tipAxis() const {
        const glm::vec3 fwd = m_camera.target - m_camera.eye();
        return std::abs(fwd.x) > std::abs(fwd.z) ? glm::vec3(0, 0, fwd.x > 0 ? 1.0f : -1.0f)
                                                 : glm::vec3(fwd.z > 0 ? -1.0f : 1.0f, 0, 0);
    }

    static std::size_t rotationKeyIndex(SDL_Keycode k) {
        return k == SDLK_Q ? 0 : k == SDLK_E ? 1 : k == SDLK_R ? 2 : 3;
    }

    /// Holding Q/E/R/F keeps turning: after 0.3 s, 20 deg/s ramping to 90 deg/s.
    void turnWhileKeysHeld(float dt) {
        if (!m_sim.world().grabbedBody()) return;
        if ((SDL_GetModState() & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL)) != 0) return;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        const std::array<std::pair<SDL_Scancode, SDL_Keycode>, 4> map{{{SDL_SCANCODE_Q, SDLK_Q}, {SDL_SCANCODE_E, SDLK_E},
                                                                       {SDL_SCANCODE_R, SDLK_R}, {SDL_SCANCODE_F, SDLK_F}}};
        for (const auto& [scan, key] : map) {
            if (!keys[scan]) continue;
            const double held = m_clock - m_keyPressTime[rotationKeyIndex(key)];
            if (held < 0.3) continue;
            const float rate = std::min(90.0f, 20.0f + 50.0f * static_cast<float>(held - 0.3));
            const float sign = (key == SDLK_Q || key == SDLK_R) ? 1.0f : -1.0f;
            const glm::vec3 axis = (key == SDLK_Q || key == SDLK_E) ? glm::vec3(0, 1, 0) : tipAxis();
            turnDegrees(sign * rate * dt, axis);
        }
    }

    void toggleFullscreen() {
        if (!m_window) return;
        const bool full = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
        SDL_SetWindowFullscreen(m_window, !full);
    }

    /// Takes a new block from the shelf straight into the hand.
    void spawnIntoHand(blocks::BlockType type, float x, float y) {
        pushUndo();
        int w = 0, h = 0;
        SDL_GetRenderOutputSize(m_renderer, &w, &h);
        const auto [origin, dir] = viewer::DebugRenderer::pickRay(m_camera, x, y, w, h);
        glm::vec3 at = m_camera.target;
        if (dir.y < -1e-4f) at = origin + dir * (-origin.y / dir.y);
        const float limit = (m_sim.playArea() ? m_sim.playArea()->halfSize : 0.6f) - 0.07f;
        at.x = std::clamp(at.x, -limit, limit);
        at.z = std::clamp(at.z, -limit, limit);
        const auto& def = blocks::definition(type);
        physics::Transform t;
        t.position = {at.x, def.halfExtents.y + 0.06f, at.z};
        const auto index = m_sim.addBlock(type, m_species, t, m_nextStain);
        m_nextStain = materials::nextStain(m_nextStain);
        auto& world = m_sim.world();
        const glm::vec3 handle = t.position + glm::vec3(0.0f, def.halfExtents.y * 0.8f, 0.0f);
        world.beginGrab(m_sim.blocks()[index].body, handle);
        m_grabStart = handle;
        m_handHeight = handle.y;
        m_handTarget = handle;
        world.moveGrab(m_handTarget);
        m_targetRotation = t.rotation;
        world.setGrabOrientation(m_targetRotation);
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
        } else if (m_shelf.contains(m_mouse.x, m_mouse.y)) {
            // Pointer is on the shelf: nothing in the scene is hovered.
        } else if (const auto hit = pick(m_mouse.x, m_mouse.y)) {
            o.hovered = m_sim.blockIndex(hit->body);
        }
        return o;
    }

    void text(float x, float y, const std::string& s) {
        // Light text with a soft shadow: readable on both the walls and the mat.
        SDL_SetRenderDrawColorFloat(m_renderer, 0.0f, 0.0f, 0.0f, 0.55f);
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderDebugText(m_renderer, x + 1.0f, y + 1.0f, s.c_str());
        SDL_SetRenderDrawColorFloat(m_renderer, 1.0f, 0.98f, 0.94f, 1.0f);
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
        if (m_sim.world().grabbedBody()) {
            // Orientation readout for 1-degree adjustments: heading and tilt.
            const glm::vec3 x = m_targetRotation * glm::vec3(1, 0, 0);
            const glm::vec3 up = m_targetRotation * glm::vec3(0, 1, 0);
            float heading = std::round(glm::degrees(std::atan2(-x.z, x.x)));
            if (heading == 0.0f) heading = 0.0f; // avoid printing "-0"
            const float tilt = glm::degrees(std::acos(std::clamp(up.y, -1.0f, 1.0f)));
            std::snprintf(line, sizeof(line), "holding: heading %+.0f deg | tilt %.0f deg", static_cast<double>(heading),
                          static_cast<double>(tilt));
            text(10.0f, 114.0f, line);
        }
        if (m_gpuView) {
            const auto& st = m_gpuView->settings;
            std::snprintf(line, sizeof(line), "Vulkan PBR | F7 depth of field: %s | F8 AO+contact: %s | F9 bloom: %s | F10 photo detail: %s",
                          st.dof ? "on" : "off", st.ssao ? "on" : "off", st.bloom ? "on" : "off", st.photoDetail ? "on" : "off");
        } else {
            std::snprintf(line, sizeof(line), "software renderer (debug)");
        }
        text(10.0f, 100.0f, line);
        if (m_showHelp) {
            text(10.0f, 44.0f, "Shelf: press a block to take it, release to drop | Tab: hide shelf | 1-9 also create at cursor");
            text(10.0f, 58.0f, "Holding: Q/E turn, R/F tip (tap 1 deg, hold = continuous, Shift = 90) | Ctrl+wheel 1 deg | Ctrl(+Shift)+mouse free/fine");
            text(10.0f, 72.0f, "Shift+mouse or wheel: lift | hold Space: align | WASD move | right-drag orbit | middle-drag pan | wheel zoom");
            text(10.0f, 86.0f, "C recolor | O beech/oak | F2-F6 scenes | Del clear | Ctrl+Z/Y undo/redo | P pause | M mute | F11 fullscreen");
        }
    }

    simulation::Simulation m_sim;
    viewer::OrbitCamera m_camera;
    std::unique_ptr<viewer::GpuView> m_gpuView;      // PBR renderer (Vulkan)
    std::unique_ptr<viewer::DebugRenderer> m_draw;   // software fallback
    SDL_Window* m_window = nullptr;
    viewer::Shelf m_shelf;
    double m_clock = 0.0;
    bool m_clean = false;
    std::array<double, 4> m_keyPressTime{}; // Q, E, R, F
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
    const char* title = "WOODYN - Real Wood. Real Physics.";

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_Texture* offscreen = nullptr;
    if (opt.renderer == "gpu") {
        // SDL3 GPU API (Vulkan): our renderer draws the 3D image, an SDL GPU
        // renderer on the same device composites it with the HUD and presents.
        device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
        if (device) {
            if (!opt.hidden) window = SDL_CreateWindow(title, 1280, 800, flags);
            renderer = SDL_CreateGPURenderer(device, window); // no window -> offscreen
            if (renderer && opt.hidden) {
                offscreen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 1280, 800);
                SDL_SetRenderTarget(renderer, offscreen);
            }
        }
        if (!renderer) {
            log::warn("GPU renderer unavailable ({}), using the software renderer", SDL_GetError());
            if (window) SDL_DestroyWindow(window);
            if (device) SDL_DestroyGPUDevice(device);
            window = nullptr;
            device = nullptr;
        }
    }
    if (!renderer && !SDL_CreateWindowAndRenderer(title, 1280, 800, flags, &window, &renderer)) {
        log::error("SDL_CreateWindowAndRenderer failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, opt.frames > 0 ? 0 : 1);
    log::info("SDL {} renderer: {}", SDL_GetRevision(), SDL_GetRendererName(renderer));

    int exitCode = 0;
    {
        App app(renderer, window, device, opt.mute);
        app.loadScene(opt.scene, false);
        if (opt.zoom > 0.0f) app.camera().distance = opt.zoom;
        if (opt.yaw > -99.0f) app.camera().yaw = opt.yaw;
        if (opt.noPhotoDetail) app.setPhotoDetail(false);
        if (opt.clean) app.setClean();

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

    if (offscreen) SDL_DestroyTexture(offscreen);
    SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (device) SDL_DestroyGPUDevice(device);
    SDL_Quit();
    return exitCode;
}
