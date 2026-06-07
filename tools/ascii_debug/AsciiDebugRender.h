#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <glm/vec2.hpp>
#include "rendering/AsciiAtlasBaker.h"

enum class GlyphVisMode {
    Coverage,    // final smoothstep coverage (matches shader output)
    RawSdf,      // bilinear SDF sample before thresholding
    SmoothstepT, // normalized position inside the AA band
    SlotAtlas,   // raw slot buffer at native texel resolution (×8)
};

constexpr int kSlotAtlasTexelScale = 8;

struct DebugSdfSample {
    float sdf       = 0.0f;
    float t         = 0.0f;
    float coverage  = 0.0f;
};

DebugSdfSample sampleDebugSdf(
    const std::vector<uint8_t>& slotPixels,
    int slotW, int slotH,
    int padding, float pixDistScale, uint8_t onedgeValue,
    glm::vec2 cellSizePx, float uvCellX, float uvCellY);

// Cell-sized preview for Coverage / Raw / AA modes.
glm::ivec2 computeDebugTexSize(glm::vec2 cellSizePx, glm::ivec2 slotPx);

// Output size for the glyph preview texture (slot atlas uses native slot scale).
glm::ivec2 computeGlyphPreviewTexSize(
    GlyphVisMode mode, glm::vec2 cellSizePx, glm::ivec2 slotPx);

std::vector<uint8_t> renderGlyphDebugImage(
    const std::vector<uint8_t>& slotPixels,
    int slotW, int slotH,
    int padding, float pixDistScale, uint8_t onedgeValue,
    glm::vec2 cellSizePx, glm::ivec2 slotPx, GlyphVisMode mode);

std::vector<uint8_t> renderPatchImage(
    const std::array<float, kSpatialK>& intensity,
    glm::vec2 cellSizePx, glm::ivec2 slotPx);
