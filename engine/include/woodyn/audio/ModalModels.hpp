#pragma once

#include "woodyn/blocks/BlockCatalog.hpp"
#include "woodyn/materials/WoodSpecies.hpp"

#include <vector>

namespace woodyn::audio {

/// One vibration mode: an exponentially decaying sinusoid.
struct Mode {
    float frequency = 0.0f; // [Hz]
    float decay = 0.0f;     // amplitude time constant [s]
    float gain = 0.0f;      // relative amplitude
};

/// Modal sound model of a sounding object (D-27).
struct ModalModel {
    std::vector<Mode> modes;
    float clickGain = 0.0f;     // contact transient (filtered noise burst)
    float clickCutoffHz = 0.0f; // brightness of the transient
    float clickDecay = 0.0f;    // [s]
};

/// What a static surface is made of, for impact sounds.
enum class SurfaceKind {
    PlayMat, // soft EVA foam floor: dull, short thud
    Wood,    // wooden fence / table: hollow knock
    Silent,  // invisible bounds (walls above the fence, ceiling)
};

/// Modal model of a block. Mode frequencies follow the block's size and
/// shape (bar-like blocks: free-free bending ratios 1 : 2.76 : 5.40; compact
/// blocks: closer ratios), damping follows the wood's internal friction.
/// PHYSICALLY MOTIVATED BUT PERCEPTUALLY TUNED, PROVISIONAL: to be calibrated
/// against recordings of real blocks (DESIGN.md §11).
const ModalModel& blockModel(blocks::BlockType type, materials::WoodSpecies species);

const ModalModel& surfaceModel(SurfaceKind kind);

} // namespace woodyn::audio
