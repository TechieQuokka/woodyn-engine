#pragma once

#include "woodyn/blocks/BlockCatalog.hpp"
#include "woodyn/wood/WoodCut.hpp"
#include "woodyn/wood/WoodSource.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace woodyn::wood {

/// Which block-local axis follows the fibers.
enum class FiberAxis { Longest, X, Y, Z };

/// The "sawmill": owns the logs and cuts blocks out of them (DESIGN.md §5.1).
///
/// Each log's cross section is divided into square plank cells; blocks are cut
/// one after another along a cell with zero kerf, so consecutive cuts share
/// their contact faces exactly and their grain continues across the seam. The
/// fibers follow the block's longest axis (as toy makers cut them) unless told
/// otherwise, and the block's "up" axis points away from the pith, so end grain
/// shows growth-ring arcs.
class WoodLibrary {
public:
    WoodLibrary() = default;

    /// Cuts the next block of `type` from the current plank of `species`.
    WoodCut cut(blocks::BlockType type, materials::WoodSpecies species, FiberAxis axis = FiberAxis::Longest);

    /// Cuts an arbitrary box (e.g. floorboards, fence rails) of the given half
    /// extents; fibers follow `fiberAxis` (0 = X, 1 = Y, 2 = Z).
    WoodCut cutBox(const glm::vec3& halfExtents, materials::WoodSpecies species, int fiberAxis);

    /// Moves `species` to a fresh plank so the next cuts form one continuous run.
    void startRun(materials::WoodSpecies species);

    [[nodiscard]] const WoodSource& source(std::uint32_t id) const;
    [[nodiscard]] std::size_t sourceCount() const noexcept { return m_sources.size(); }

    /// Plank cross-section size [m] (fits the largest block cross section, 2 x 1 modules).
    static constexpr float kCellSize = 0.065f;

private:
    struct Cursor {
        std::uint32_t source = 0;
        std::size_t cell = 0;
        float z = 0.0f;
        bool valid = false;
    };

    std::uint32_t newSource(materials::WoodSpecies species);
    std::vector<glm::vec2> cellsOf(const WoodSource& source) const;
    Cursor& cursorFor(materials::WoodSpecies species);
    void advanceCell(Cursor& cursor, materials::WoodSpecies species);

    std::deque<WoodSource> m_sources; // stable references
    std::array<Cursor, 2> m_cursors{}; // per species
};

/// Index of the block-local axis (0 = X, 1 = Y, 2 = Z) the fibers follow.
int fiberAxisIndex(blocks::BlockType type, FiberAxis axis);

} // namespace woodyn::wood
