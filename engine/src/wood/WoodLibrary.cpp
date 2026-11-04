#include "woodyn/wood/WoodLibrary.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace woodyn::wood {

int fiberAxisIndex(blocks::BlockType type, FiberAxis axis) {
    switch (axis) {
    case FiberAxis::X: return 0;
    case FiberAxis::Y: return 1;
    case FiberAxis::Z: return 2;
    case FiberAxis::Longest: break;
    }
    const glm::vec3 h = blocks::definition(type).halfExtents;
    // Ties (cube, pyramid, cone) keep X: consistent and easy to reason about.
    int best = 0;
    for (int i = 1; i < 3; ++i) {
        if (h[i] > h[best] + 1e-6f) best = i;
    }
    return best;
}

std::uint32_t WoodLibrary::newSource(materials::WoodSpecies species) {
    const auto id = static_cast<std::uint32_t>(m_sources.size());
    // Seed from id and species so every log looks different but reproducible.
    const std::uint32_t seed = 1000u + id * 7919u + static_cast<std::uint32_t>(species) * 104729u;
    m_sources.emplace_back(id, defaultParams(species, seed));
    return id;
}

const WoodSource& WoodLibrary::source(std::uint32_t id) const {
    if (id >= m_sources.size()) throw std::out_of_range("WoodLibrary: unknown source");
    return m_sources[id];
}

std::vector<glm::vec2> WoodLibrary::cellsOf(const WoodSource& source) const {
    // Square cells fully inside the log (1.2 cm of bark/sapwood margin).
    const float usable = source.params().radius - 0.012f;
    std::vector<glm::vec2> cells;
    const int n = static_cast<int>(std::ceil(usable / kCellSize));
    for (int i = -n; i < n; ++i) {
        for (int j = -n; j < n; ++j) {
            const glm::vec2 c{(static_cast<float>(i) + 0.5f) * kCellSize, (static_cast<float>(j) + 0.5f) * kCellSize};
            const float corner = glm::length(glm::abs(c) + glm::vec2(0.5f * kCellSize));
            if (corner <= usable) cells.push_back(c);
        }
    }
    return cells;
}

WoodLibrary::Cursor& WoodLibrary::cursorFor(materials::WoodSpecies species) {
    Cursor& c = m_cursors.at(static_cast<std::size_t>(species));
    if (!c.valid) {
        c.source = newSource(species);
        c.cell = 0;
        c.z = 0.0f;
        c.valid = true;
    }
    return c;
}

void WoodLibrary::advanceCell(Cursor& c, materials::WoodSpecies species) {
    ++c.cell;
    c.z = 0.0f;
    if (c.cell >= cellsOf(m_sources[c.source]).size()) {
        c.source = newSource(species);
        c.cell = 0;
    }
}

void WoodLibrary::startRun(materials::WoodSpecies species) {
    Cursor& c = cursorFor(species);
    if (c.z > 0.0f) advanceCell(c, species);
}

WoodCut WoodLibrary::cut(blocks::BlockType type, materials::WoodSpecies species, FiberAxis axis) {
    return cutBox(blocks::definition(type).halfExtents, species, fiberAxisIndex(type, axis));
}

WoodCut WoodLibrary::cutBox(const glm::vec3& halfExtents, materials::WoodSpecies species, int f) {
    const float length = 2.0f * halfExtents[f];

    Cursor& c = cursorFor(species);
    if (c.z + length > m_sources[c.source].params().length) advanceCell(c, species);
    const WoodSource& src = m_sources[c.source];
    const glm::vec2 cell = cellsOf(src)[c.cell];

    // Local basis: fiber axis, "up" axis (Y unless Y is the fiber axis).
    glm::vec3 ef{0.0f};
    ef[f] = 1.0f;
    const glm::vec3 eu = f == 1 ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    // Log basis: fibers along +Z, "up" pointing away from the pith, snapped to
    // the plank grid (boards are sawn square to the grid, like at a real mill;
    // a free radial direction would rotate the block out of its cell). Blocks
    // near the pith axis come out quarter-sawn, others flat-sawn.
    const glm::vec2 r = cell - src.params().pithOffset;
    const glm::vec2 radial = std::abs(r.x) >= std::abs(r.y) ? glm::vec2(r.x >= 0.0f ? 1.0f : -1.0f, 0.0f)
                                                            : glm::vec2(0.0f, r.y >= 0.0f ? 1.0f : -1.0f);
    const glm::vec3 wf{0.0f, 0.0f, 1.0f};
    const glm::vec3 wu{radial.x, radial.y, 0.0f};

    const glm::mat3 local(ef, eu, glm::cross(ef, eu));
    const glm::mat3 world(wf, wu, glm::cross(wf, wu));
    const glm::mat3 m = world * glm::transpose(local);

    WoodCut cut;
    cut.source = src.id();
    cut.rotation = glm::normalize(glm::quat_cast(m));
    cut.center = {cell.x, cell.y, c.z + 0.5f * length};
    c.z += length; // zero kerf: the next block starts exactly where this one ends
    return cut;
}

} // namespace woodyn::wood
