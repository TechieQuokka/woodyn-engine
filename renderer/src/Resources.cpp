#include "woodyn/render/Resources.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <system_error>

namespace woodyn::render {
namespace {

/// `<prefix>/share/woodyn` when this executable is the installed one.
std::filesystem::path installedDataDirectory() {
    static const std::filesystem::path dir = [] {
        const char* base = SDL_GetBasePath(); // directory of the executable, owned by SDL
        if (!base) return std::filesystem::path();
        const std::filesystem::path candidate = std::filesystem::path(base) / WOODYN_INSTALL_DATA_REL;
        std::error_code ec;
        if (!std::filesystem::exists(candidate / "shaders" / "wood.frag.spv", ec)) return std::filesystem::path();
        return candidate.lexically_normal();
    }();
    return dir;
}

} // namespace

std::string shaderDirectory() {
    const auto installed = installedDataDirectory();
    return installed.empty() ? std::string(WOODYN_SHADER_DIR) : (installed / "shaders").string();
}

std::string assetDirectory() {
    const auto installed = installedDataDirectory();
    return installed.empty() ? std::string(WOODYN_ASSET_DIR) : (installed / "assets").string();
}

} // namespace woodyn::render
