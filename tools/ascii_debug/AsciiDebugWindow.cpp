#include "AsciiDebugWindow.h"
#include "rendering/AsciiAtlas.h"   // kPadding, kPixDistScale, kOnedgeValue
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdio>

static const char* kCharsetPresets[] = {
    " .:-=+*#%@",
    "$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\\|()1{}[]?-_+~<>i!lI;:,\"^`'. ",
    " 01"
};
static const char* kCharsetLabels[] = {
    "Short (10 chars)",
    "Paul Bourke (~70 chars)",
    "Binary (01)",
    "All font glyphs (auto)"
};
static constexpr int kCharsetPresetCount = 3;
static constexpr int kCharsetAllFont     = 3;
static constexpr int kCharsetCount       = 4;
static constexpr glm::ivec2 kGlyphPx{16, 32};

static const char* glyphVisModeLabel(GlyphVisMode mode)
{
    switch (mode) {
    case GlyphVisMode::Coverage:    return "Coverage (final)";
    case GlyphVisMode::RawSdf:      return "Raw SDF";
    case GlyphVisMode::SmoothstepT: return "AA band (t)";
    case GlyphVisMode::SlotAtlas:   return "Slot atlas";
    }
    return "Coverage (final)";
}

AsciiDebugWindow::AsciiDebugWindow(const std::string& fontPath)
{
    std::FILE* f = std::fopen(fontPath.c_str(), "rb");
    if (!f) return;
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::rewind(f);
    if (sz <= 0) { std::fclose(f); return; }
    m_ttfData.resize(static_cast<size_t>(sz));
    const size_t nRead = std::fread(m_ttfData.data(), 1, static_cast<size_t>(sz), f);
    std::fclose(f);
    if (nRead != static_cast<size_t>(sz)) { m_ttfData.clear(); return; }

    rebuildAtlas(0);
    rebuildCellDerived();
}

AsciiDebugWindow::~AsciiDebugWindow()
{
    if (m_glyphTex) glDeleteTextures(1, &m_glyphTex);
    if (m_patchTex) glDeleteTextures(1, &m_patchTex);
}

void AsciiDebugWindow::rebuildAtlas(int charsetIdx)
{
    std::vector<int> codepoints;
    if (charsetIdx == kCharsetAllFont) {
        codepoints = enumerateFontCodepoints(
            m_ttfData.data(), static_cast<int>(m_ttfData.size()));
    } else if (charsetIdx >= 0 && charsetIdx < kCharsetPresetCount) {
        for (const char* p = kCharsetPresets[charsetIdx]; *p; ++p)
            codepoints.push_back(static_cast<int>(static_cast<unsigned char>(*p)));
    }

    m_glyphs = bakeGlyphs(
        m_ttfData.data(), static_cast<int>(m_ttfData.size()),
        codepoints, kGlyphPx,
        AsciiAtlas::kPadding, AsciiAtlas::kPixDistScale, AsciiAtlas::kOnedgeValue);

    if (m_glyphs.empty()) return;

    m_slotPx = {kGlyphPx.x + 2 * AsciiAtlas::kPadding,
                kGlyphPx.y + 2 * AsciiAtlas::kPadding};

    m_lastCharsetIdx = charsetIdx;
    m_lastExponent   = -1.0f;
    rebuildCellDerived();
}

glm::vec2 AsciiDebugWindow::computeCellSizePx() const
{
    const float aspect = float(m_slotPx.x) / float(m_slotPx.y);
    const float cellH  = std::max(m_params.cellHeight, 3.0f);
    return {std::max(cellH * aspect, 2.0f), cellH};
}

bool AsciiDebugWindow::cellSizeDirty() const
{
    return computeCellSizePx() != m_lastCellSizePx;
}

void AsciiDebugWindow::rebuildCellDerived()
{
    if (m_glyphs.empty()) return;

    m_cellSizePx = computeCellSizePx();

    // Gather slot pixels
    std::vector<std::vector<uint8_t>> slotPixels;
    slotPixels.reserve(m_glyphs.size());
    for (const auto& g : m_glyphs) slotPixels.push_back(g.slotPixels);

    // Spatial profiles at current cell size (float dimensions match shader)
    auto raw = computeGlyphSpatialProfiles(
        slotPixels, m_slotPx,
        AsciiAtlas::kPadding, AsciiAtlas::kPixDistScale,
        AsciiAtlas::kOnedgeValue, m_cellSizePx);

    m_regionMax    = computePerRegionMax(raw);
    m_normProfiles = raw;
    normalizeGlyphProfilesInPlace(m_normProfiles, m_regionMax);

    m_glyphCoverages = computeGlyphCoverages(
        slotPixels, m_slotPx,
        AsciiAtlas::kPadding, AsciiAtlas::kPixDistScale,
        AsciiAtlas::kOnedgeValue, m_cellSizePx);
    m_coverageRange  = computeCoverageRange(m_glyphCoverages);
    m_glyphRankNorms = computeCoverageRankNorms(m_glyphCoverages);

    reshapeProfiles();

    m_texSize = computeDebugTexSize(m_cellSizePx, m_slotPx);
    ensureTextures(m_texSize);
    m_lastCellSizePx = m_cellSizePx;
}

void AsciiDebugWindow::ensureGlyphTexture(glm::ivec2 texSize)
{
    if (texSize == m_glyphPreviewSize && m_glyphTex) return;
    if (m_glyphTex) glDeleteTextures(1, &m_glyphTex);
    m_glyphTex = createTex();
    m_glyphPreviewSize = texSize;
}

void AsciiDebugWindow::ensureTextures(glm::ivec2 texSize)
{
    if (texSize == m_texSize && m_patchTex) return;
    if (m_patchTex) glDeleteTextures(1, &m_patchTex);
    m_patchTex = createTex();
    m_texSize  = texSize;
}

void AsciiDebugWindow::reshapeProfiles()
{
    m_shapedProfiles = m_normProfiles;
    shapeGlyphProfilesInPlace(m_shapedProfiles, m_params.spatialExp);
    m_lastExponent = m_params.spatialExp;
}

void AsciiDebugWindow::draw()
{
    ImGui::SetNextWindowSize(ImVec2(1380.0f, 760.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("ASCII Debug")) { ImGui::End(); return; }

    if (!valid()) {
        ImGui::TextColored(ImVec4(1,0,0,1), "Font failed to load — pass a valid font path as argv[1].");
        ImGui::End();
        return;
    }

    if (m_params.charsetIdx != m_lastCharsetIdx)
        rebuildAtlas(m_params.charsetIdx);
    else if (cellSizeDirty())
        rebuildCellDerived();
    else if (m_params.spatialExp != m_lastExponent)
        reshapeProfiles();

    {
        const auto& rm = m_params.regionMaxAuto ? m_regionMax : m_params.regionMaxManu;
        m_result = runPipeline(m_params, rm, m_shapedProfiles,
                               m_glyphRankNorms, m_glyphCoverages, m_glyphs);
        if (m_result.numCandidates > 0)
            m_previewGlyphIdx = m_result.top3[0].glyphIdx;
    }

    refreshPreviewTextures();

    if (ImGui::BeginTable("##main", 3,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
        ImGui::TableNextColumn(); drawControls();
        ImGui::TableNextColumn(); drawPipeline();
        ImGui::TableNextColumn(); drawResults();
        ImGui::EndTable();
    }

    ImGui::End();
}

GLuint AsciiDebugWindow::createTex()
{
    GLuint t = 0;
    glGenTextures(1, &t);
    if (!t) return 0;
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GLint swiz[] = {GL_RED, GL_RED, GL_RED, GL_ONE};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swiz);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

void AsciiDebugWindow::uploadTex(GLuint tex, int w, int h, const std::vector<uint8_t>& px)
{
    assert(static_cast<int>(px.size()) == w * h);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, px.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void AsciiDebugWindow::refreshPreviewTextures()
{
    if (!m_patchTex) return;
    uploadPatchPreview();
    if (m_previewGlyphIdx >= 0)
        uploadGlyphPreview(m_previewGlyphIdx);
}

void AsciiDebugWindow::uploadGlyphPreview(int glyphIdx)
{
    if (glyphIdx < 0 || static_cast<size_t>(glyphIdx) >= m_glyphs.size()) return;

    const glm::ivec2 texSize = computeGlyphPreviewTexSize(
        m_glyphVisMode, m_cellSizePx, m_slotPx);
    ensureGlyphTexture(texSize);
    if (!m_glyphTex) return;

    const auto& g = m_glyphs[static_cast<size_t>(glyphIdx)];
    auto px = renderGlyphDebugImage(
        g.slotPixels, m_slotPx.x, m_slotPx.y,
        AsciiAtlas::kPadding, AsciiAtlas::kPixDistScale,
        AsciiAtlas::kOnedgeValue, m_cellSizePx, m_slotPx, m_glyphVisMode);
    if (!px.empty())
        uploadTex(m_glyphTex, texSize.x, texSize.y, px);
}

void AsciiDebugWindow::uploadPatchPreview()
{
    if (!m_patchTex) return;
    auto px = renderPatchImage(m_params.input, m_cellSizePx, m_slotPx);
    if (!px.empty())
        uploadTex(m_patchTex, m_texSize.x, m_texSize.y, px);
}

void AsciiDebugWindow::drawControls()
{
    ImGui::Text("Controls");
    ImGui::Separator();

    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("Charset", &m_params.charsetIdx,
                      kCharsetLabels, kCharsetCount))
        m_lastCharsetIdx = -1;
    if (m_params.charsetIdx == kCharsetAllFont)
        ImGui::TextDisabled("%d glyphs from font cmap", static_cast<int>(m_glyphs.size()));

    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderFloat("Cell height (px)", &m_params.cellHeight, 3.0f, 64.0f, "%.1f");
    ImGui::SameLine();
    ImGui::TextDisabled("→ %.1f × %.1f", m_cellSizePx.x, m_cellSizePx.y);

    ImGui::Spacing();
    ImGui::Text("2×3 intensity input (top-left → top-right, then mid row, then bottom row):");

    ImGui::Text("Top  ");
    for (int i = 0; i < 2; ++i) {
        ImGui::SameLine();
        char id[8]; std::snprintf(id, sizeof(id), "##r%d", i);
        ImGui::SetNextItemWidth(72.0f);
        ImGui::SliderFloat(id, &m_params.input[i], 0.0f, 1.0f, "%.2f");
    }
    ImGui::Text("Mid  ");
    for (int i = 2; i < 4; ++i) {
        ImGui::SameLine();
        char id[8]; std::snprintf(id, sizeof(id), "##r%d", i);
        ImGui::SetNextItemWidth(72.0f);
        ImGui::SliderFloat(id, &m_params.input[i], 0.0f, 1.0f, "%.2f");
    }
    ImGui::Text("Bot  ");
    for (int i = 4; i < 6; ++i) {
        ImGui::SameLine();
        char id[8]; std::snprintf(id, sizeof(id), "##r%d", i);
        ImGui::SetNextItemWidth(72.0f);
        ImGui::SliderFloat(id, &m_params.input[i], 0.0f, 1.0f, "%.2f");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Shader parameters:");

    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderFloat("Spatial exponent", &m_params.spatialExp, 0.1f, 5.0f, "%.2f");

    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderFloat("Intensity weight", &m_params.intensityWeight, 0.0f, 1.0f, "%.2f");
    ImGui::SameLine();
    ImGui::TextDisabled("(0=shape, 1=density)");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("regionMax override:");
    ImGui::Checkbox("Auto (= per-region glyph max)", &m_params.regionMaxAuto);
    if (!m_params.regionMaxAuto) {
        ImGui::Text("Top  ");
        for (int i = 0; i < 2; ++i) {
            ImGui::SameLine();
            char id[12]; std::snprintf(id, sizeof(id), "##rm%d", i);
            ImGui::SetNextItemWidth(72.0f);
            ImGui::SliderFloat(id, &m_params.regionMaxManu[i], 1e-4f, 2.0f, "%.3f");
        }
        ImGui::Text("Mid  ");
        for (int i = 2; i < 4; ++i) {
            ImGui::SameLine();
            char id[12]; std::snprintf(id, sizeof(id), "##rm%d", i);
            ImGui::SetNextItemWidth(72.0f);
            ImGui::SliderFloat(id, &m_params.regionMaxManu[i], 1e-4f, 2.0f, "%.3f");
        }
        ImGui::Text("Bot  ");
        for (int i = 4; i < 6; ++i) {
            ImGui::SameLine();
            char id[12]; std::snprintf(id, sizeof(id), "##rm%d", i);
            ImGui::SetNextItemWidth(72.0f);
            ImGui::SliderFloat(id, &m_params.regionMaxManu[i], 1e-4f, 2.0f, "%.3f");
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Atlas regionMax (for reference):");
        ImGui::Text("Top  ");
        for (int i = 0; i < 2; ++i) {
            ImGui::SameLine();
            ImGui::TextDisabled("%.3f", m_regionMax[i]);
        }
        ImGui::Text("Mid  ");
        for (int i = 2; i < 4; ++i) {
            ImGui::SameLine();
            ImGui::TextDisabled("%.3f", m_regionMax[i]);
        }
        ImGui::Text("Bot  ");
        for (int i = 4; i < 6; ++i) {
            ImGui::SameLine();
            ImGui::TextDisabled("%.3f", m_regionMax[i]);
        }
    }
}

void AsciiDebugWindow::drawPipeline()
{
    ImGui::Text("Pipeline");
    ImGui::Separator();

    static const char* kRegionNames[kSpatialK] = {
        "TL","TR","ML","MR","BL","BR"
    };

    auto showRow = [&](const char* label, const std::array<float, kSpatialK>& v) {
        ImGui::Text("%-20s", label);
        for (int i = 0; i < kSpatialK; ++i) {
            ImGui::SameLine();
            ImGui::TextUnformatted(kRegionNames[i]);
            ImGui::SameLine();
            ImGui::Text("%.3f", v[i]);
            if (i < kSpatialK - 1) ImGui::SameLine(0, 14);
        }
    };

    showRow("1. Raw input:", m_params.input);
    ImGui::Text("   mean luminance = %.5f  (intensity axis, not regionMax-normalised)",
                m_result.cellFeatures.luminance);
    ImGui::Spacing();

    {
        const auto& rm = m_params.regionMaxAuto ? m_regionMax : m_params.regionMaxManu;
        std::array<float, kSpatialK> rmArr{};
        for (int i = 0; i < kSpatialK; ++i) rmArr[i] = std::max(rm[i], 1e-6f);
        showRow("   regionMax:", rmArr);
    }
    ImGui::Spacing();
    showRow("2. After ÷ regionMax:", m_result.afterGlobal);
    ImGui::Spacing();

    ImGui::Text("   spatial density (post regionMax) = %.5f", m_result.cellFeatures.density);
    ImGui::Text("   rendered coverage range = [%.5f .. %.5f]  (%d glyphs)",
                m_coverageRange.min, m_coverageRange.max,
                static_cast<int>(m_glyphs.size()));
    ImGui::TextDisabled("   intensity uses coverage rank [0,1], not raw coverage value");
    ImGui::Text("   exponent = %.2f", m_params.spatialExp);
    showRow("3. Shaped shape vector:", m_result.cellFeatures.shape);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("4. Score = (1-w)*shapeL2 + w*(lum - rankNorm)^2  (w=%.2f)", m_params.intensityWeight);
    ImGui::Text("   argmin over %d glyphs", static_cast<int>(m_glyphs.size()));
    ImGui::TextDisabled("   cell %.1f × %.1f px  (tex %d × %d)",
                        m_cellSizePx.x, m_cellSizePx.y,
                        m_texSize.x, m_texSize.y);
}

void AsciiDebugWindow::drawResults()
{
    ImGui::Text("Results");
    ImGui::Separator();

    static const char* kRegionNames[kSpatialK] = {"TL","TR","ML","MR","BL","BR"};

    for (int k = 0; k < m_result.numCandidates; ++k) {
        const auto& c = m_result.top3[k];
        ImGui::Text("#%d  U+%04X  idx=%d  combined=%.5f  shape=%.5f  density=%.5f",
                    k + 1, c.codepoint, c.glyphIdx, c.score, c.shapeScore, c.densityScore);
        ImGui::Text("    cell lum=%.4f  glyph rank=%.4f  cov=%.4f  Δ=%.4f",
                    m_result.cellFeatures.luminance, c.glyphRankNorm, c.glyphCoverage,
                    m_result.cellFeatures.luminance - c.glyphRankNorm);

        if (ImGui::BeginTable("##bars", kSpatialK + 1,
                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit)) {
            for (int i = 0; i < kSpatialK; ++i)
                ImGui::TableSetupColumn(kRegionNames[i], ImGuiTableColumnFlags_WidthFixed, 52.0f);
            ImGui::TableSetupColumn("D", ImGuiTableColumnFlags_WidthFixed, 52.0f);
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            for (int i = 0; i < kSpatialK; ++i) {
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f,0.5f,1.0f,1.0f));
                const float cellV = m_result.cellFeatures.shape[static_cast<size_t>(i)];
                ImGui::ProgressBar(std::clamp(std::abs(cellV) * 4.0f, 0.0f, 1.0f), ImVec2(-1.0f, 8.0f), "");
                ImGui::PopStyleColor();
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f,0.6f,0.1f,1.0f));
                const float glyphV = c.shapedProfile[static_cast<size_t>(i)];
                ImGui::ProgressBar(std::clamp(std::abs(glyphV) * 4.0f, 0.0f, 1.0f), ImVec2(-1.0f, 8.0f), "");
                ImGui::PopStyleColor();
            }
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f,0.5f,1.0f,1.0f));
            ImGui::ProgressBar(std::clamp(m_result.cellFeatures.luminance, 0.0f, 1.0f), ImVec2(-1.0f, 8.0f), "");
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f,0.6f,0.1f,1.0f));
            ImGui::ProgressBar(std::clamp(c.glyphRankNorm, 0.0f, 1.0f), ImVec2(-1.0f, 8.0f), "");
            ImGui::PopStyleColor();
            ImGui::EndTable();
        }
        if (k < m_result.numCandidates - 1) ImGui::Separator();
    }

    if (m_result.numCandidates == 0)
        ImGui::TextColored(ImVec4(1,1,0,1), "No candidates.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Side-by-side at %.1f×%.1f px  (scale ×8)",
                m_cellSizePx.x, m_cellSizePx.y);

    static const char* kVisModes[] = {
        "Coverage (final)",
        "Raw SDF",
        "AA band (t)",
        "Slot atlas",
    };
    int visIdx = static_cast<int>(m_glyphVisMode);
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::Combo("Glyph view", &visIdx, kVisModes, IM_ARRAYSIZE(kVisModes)))
        m_glyphVisMode = static_cast<GlyphVisMode>(visIdx);

    if (m_glyphVisMode == GlyphVisMode::SlotAtlas) {
        ImGui::TextWrapped(
            "Slot atlas: exact %d×%d bake buffer (×%d). Gray outline = shader UV crop, not glyph bounds. "
            "SDF values naturally extend into the padding band — Raw SDF resamples the inner crop at cell size.",
            m_slotPx.x, m_slotPx.y, kSlotAtlasTexelScale);
    } else {
        ImGui::TextDisabled(
            "Rendered views crop to inner slot UV [%d..%d]×[%d..%d] (padding excluded).",
            AsciiAtlas::kPadding, m_slotPx.x - AsciiAtlas::kPadding - 1,
            AsciiAtlas::kPadding, m_slotPx.y - AsciiAtlas::kPadding - 1);
    }

    ImGui::TextColored(ImVec4(0.2f,0.5f,1.0f,1), "  Input patch");
    ImGui::SameLine(0, 20);
    ImGui::TextColored(ImVec4(1.0f,0.6f,0.1f,1), "Winning glyph (%s)",
                       glyphVisModeLabel(m_glyphVisMode));

    const float scale = 8.0f;
    const ImVec2 patchSz(m_cellSizePx.x * scale, m_cellSizePx.y * scale);
    const ImVec2 glyphSz = (m_glyphVisMode == GlyphVisMode::SlotAtlas)
        ? ImVec2(float(m_glyphPreviewSize.x), float(m_glyphPreviewSize.y))
        : patchSz;
    if (m_patchTex)
        ImGui::Image((ImTextureID)(intptr_t)m_patchTex, patchSz,
                     ImVec2(0,1), ImVec2(1,0));
    ImGui::SameLine(0, 12);
    if (m_glyphTex && m_result.numCandidates > 0)
        ImGui::Image((ImTextureID)(intptr_t)m_glyphTex, glyphSz,
                     ImVec2(0,1), ImVec2(1,0));
}
