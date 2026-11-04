#include "ImageLoader.hpp"

#include "woodyn/core/Log.hpp"

#include <stb_image.h>

#include <cstring>

namespace woodyn::render::detail {

Image loadPng(const std::string& path) {
    Image img;
    int channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &img.width, &img.height, &channels, 4);
    if (!data) {
        log::warn("could not load {}: {}", path, stbi_failure_reason());
        return {};
    }
    img.rgba.resize(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4);
    std::memcpy(img.rgba.data(), data, img.rgba.size());
    stbi_image_free(data);
    return img;
}

} // namespace woodyn::render::detail
