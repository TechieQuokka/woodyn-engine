#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace woodyn::render::detail {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; // 8-bit RGBA, row-major, top row first
};

/// Loads a PNG as RGBA8. Returns an empty image on failure.
Image loadPng(const std::string& path);

} // namespace woodyn::render::detail
