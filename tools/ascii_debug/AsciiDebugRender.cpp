#include "AsciiDebugRender.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr int kMinTexW = 2;
constexpr int kMinTexH = 3;

float bilinearSlot(
    const std::vector<uint8_t>& slotPixels,
    int slotW, int slotH,
    float slotPxX, float slotPxY)
{
    const int c0 = std::clamp(static_cast<int>(slotPxX), 0, slotW - 1);
    const int c1 = std::clamp(c0 + 1, 0, slotW - 1);
    const int r0 = std::clamp(static_cast<int>(slotPxY), 0, slotH - 1);
    const int r1 = std::clamp(r0 + 1, 0, slotH - 1);
    const float fx = slotPxX - static_cast<float>(c0);
    const float fy = slotPxY - static_cast<float>(r0);
    auto px = [&](int r, int c) {
        return static_cast<float>(slotPixels[static_cast<size_t>(r * slotW + c)]) / 255.0f;
    };
    return px(r0, c0) * (1.0f - fx) * (1.0f - fy)
         + px(r0, c1) * fx * (1.0f - fy)
         + px(r1, c0) * (1.0f - fx) * fy
         + px(r1, c1) * fx * fy;
}

uint8_t encodeUnit(float v)
{
    return static_cast<uint8_t>(std::round(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

uint8_t encodeRawSdf(float sdf)
{
    return static_cast<uint8_t>(std::round(std::clamp(sdf, 0.0f, 1.0f) * 255.0f));
}

void drawRectOutline(
    std::vector<uint8_t>& img, int W, int H,
    int x0, int y0, int x1, int y1, uint8_t value)
{
    x0 = std::clamp(x0, 0, W - 1);
    x1 = std::clamp(x1, 0, W - 1);
    y0 = std::clamp(y0, 0, H - 1);
    y1 = std::clamp(y1, 0, H - 1);
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    for (int x = x0; x <= x1; ++x) {
        img[static_cast<size_t>(y0 * W + x)] = value;
        img[static_cast<size_t>(y1 * W + x)] = value;
    }
    for (int y = y0; y <= y1; ++y) {
        img[static_cast<size_t>(y * W + x0)] = value;
        img[static_cast<size_t>(y * W + x1)] = value;
    }
}

} // namespace

glm::ivec2 computeDebugTexSize(glm::vec2 cellSizePx, glm::ivec2 slotPx)
{
    const float aspect = float(slotPx.x) / float(slotPx.y);
    const int h = std::max(kMinTexH, static_cast<int>(std::round(cellSizePx.y)));
    const int w = std::max(kMinTexW, static_cast<int>(std::round(float(h) * aspect)));
    return {w, h};
}

glm::ivec2 computeGlyphPreviewTexSize(
    GlyphVisMode mode, glm::vec2 cellSizePx, glm::ivec2 slotPx)
{
    if (mode == GlyphVisMode::SlotAtlas)
        return {slotPx.x * kSlotAtlasTexelScale, slotPx.y * kSlotAtlasTexelScale};
    return computeDebugTexSize(cellSizePx, slotPx);
}

DebugSdfSample sampleDebugSdf(
    const std::vector<uint8_t>& slotPixels,
    int slotW, int slotH,
    int padding, float pixDistScale, uint8_t onedgeValue,
    glm::vec2 cellSizePx, float uvCellX, float uvCellY)
{
    const float fSlotW = static_cast<float>(slotW);
    const float fSlotH = static_cast<float>(slotH);
    const float fPad   = static_cast<float>(padding);

    const float halfTexelX = 0.5f / fSlotW;
    const float halfTexelY = 0.5f / fSlotH;
    const float padFracX   = fPad / fSlotW + halfTexelX;
    const float padFracY   = fPad / fSlotH + halfTexelY;

    const float atlasTexelsPerScreenPx = fSlotH / std::max(cellSizePx.y, 1e-4f);
    const float sdfPerScreenPx = (pixDistScale / 255.0f) * atlasTexelsPerScreenPx;
    const float soft    = std::clamp((atlasTexelsPerScreenPx - 2.0f) * 0.5f, 0.0f, 1.0f);
    const float aaSharp = std::max(sdfPerScreenPx * 0.5f, 1.0f / 255.0f);
    const float aaSoft  = 0.25f;
    const float aa      = aaSharp + soft * (aaSoft - aaSharp);
    const float edge    = static_cast<float>(onedgeValue) / 255.0f;
    const float lo      = edge - aa;
    const float hi      = edge + aa;

    const float uvSlotX = padFracX + uvCellX * (1.0f - 2.0f * padFracX);
    const float uvSlotY = padFracY + uvCellY * (1.0f - 2.0f * padFracY);
    const float slotPxX = uvSlotX * fSlotW - 0.5f;
    const float slotPxY = uvSlotY * fSlotH - 0.5f;

    DebugSdfSample out{};
    out.sdf = bilinearSlot(slotPixels, slotW, slotH, slotPxX, slotPxY);
    out.t   = (hi > lo) ? std::clamp((out.sdf - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    out.coverage = out.t * out.t * (3.0f - 2.0f * out.t);
    return out;
}

std::vector<uint8_t> renderGlyphDebugImage(
    const std::vector<uint8_t>& slotPixels,
    int slotW, int slotH,
    int padding, float pixDistScale, uint8_t onedgeValue,
    glm::vec2 cellSizePx, glm::ivec2 slotPx, GlyphVisMode mode)
{
    if (mode == GlyphVisMode::SlotAtlas) {
        // Native slot texels enlarged — no letterbox/resample into cell size.
        const int W = slotW * kSlotAtlasTexelScale;
        const int H = slotH * kSlotAtlasTexelScale;
        std::vector<uint8_t> img(static_cast<size_t>(W * H));

        for (int sy = 0; sy < H; ++sy) {
            const int slotRow = sy / kSlotAtlasTexelScale;
            for (int sx = 0; sx < W; ++sx) {
                const int slotCol = sx / kSlotAtlasTexelScale;
                img[static_cast<size_t>(sy * W + sx)] =
                    slotPixels[static_cast<size_t>(slotRow * slotW + slotCol)];
            }
        }

        // Shader UV crop (not the glyph ink bounding box — SDF influence extends
        // into the padding band by design).
        const float padFracX = float(padding) / float(slotW) + 0.5f / float(slotW);
        const float padFracY = float(padding) / float(slotH) + 0.5f / float(slotH);
        const int ix0 = static_cast<int>(std::floor(padFracX * float(slotW) * float(kSlotAtlasTexelScale)));
        const int ix1 = static_cast<int>(std::ceil((1.0f - padFracX) * float(slotW) * float(kSlotAtlasTexelScale))) - 1;
        const int iy0 = static_cast<int>(std::floor(padFracY * float(slotH) * float(kSlotAtlasTexelScale)));
        const int iy1 = static_cast<int>(std::ceil((1.0f - padFracY) * float(slotH) * float(kSlotAtlasTexelScale))) - 1;
        drawRectOutline(img, W, H, ix0, iy0, ix1, iy1, 96);
        return img;
    }

    const glm::ivec2 texSize = computeDebugTexSize(cellSizePx, slotPx);
    const int W = texSize.x;
    const int H = texSize.y;

    std::vector<uint8_t> img(static_cast<size_t>(W * H));
    for (int sy = 0; sy < H; ++sy) {
        const float uvY = (float(sy) + 0.5f) / float(H);
        for (int sx = 0; sx < W; ++sx) {
            const float uvX = (float(sx) + 0.5f) / float(W);
            const DebugSdfSample s = sampleDebugSdf(
                slotPixels, slotW, slotH, padding, pixDistScale, onedgeValue,
                cellSizePx, uvX, uvY);

            uint8_t v = 0;
            switch (mode) {
            case GlyphVisMode::Coverage:    v = encodeUnit(s.coverage); break;
            case GlyphVisMode::RawSdf:      v = encodeRawSdf(s.sdf); break;
            case GlyphVisMode::SmoothstepT: v = encodeUnit(s.t); break;
            case GlyphVisMode::SlotAtlas:   break;
            }
            img[static_cast<size_t>(sy * W + sx)] = v;
        }
    }
    return img;
}

std::vector<uint8_t> renderPatchImage(
    const std::array<float, kSpatialK>& intensity,
    glm::vec2 cellSizePx, glm::ivec2 slotPx)
{
    const glm::ivec2 texSize = computeDebugTexSize(cellSizePx, slotPx);
    const int W = texSize.x;
    const int H = texSize.y;

    std::vector<uint8_t> img(static_cast<size_t>(W * H));
    for (int sy = 0; sy < H; ++sy) {
        const int row = (sy >= (2 * H) / 3) ? 0 : (sy >= H / 3) ? 1 : 2;
        for (int sx = 0; sx < W; ++sx) {
            const int col = (sx < W / 2) ? 0 : 1;
            const int reg = row * 2 + col;
            img[static_cast<size_t>(sy * W + sx)] =
                static_cast<uint8_t>(std::clamp(intensity[reg], 0.0f, 1.0f) * 255.0f);
        }
    }
    return img;
}
