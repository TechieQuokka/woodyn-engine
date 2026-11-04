#pragma once

#include <string>

namespace woodyn::render {

/// Where the renderer finds its files at run time.
///
/// An installed WOODYN keeps them in `<prefix>/share/woodyn` next to
/// `<prefix>/bin` (see `cmake --install`); a build-tree executable falls back
/// to the shaders and assets of the build that produced it.
std::string shaderDirectory();
std::string assetDirectory();

} // namespace woodyn::render
