#pragma once
#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <array>
#include <string>
#include <vector>
#include "rendering/AsciiAtlasBaker.h"
#include "AsciiDebugPipeline.h"
#include "AsciiDebugRender.h"

class AsciiDebugWindow {
public:
    explicit AsciiDebugWindow(const std::string& fontPath);
    ~AsciiDebugWindow();
    AsciiDebugWindow(const AsciiDebugWindow&) = delete;
    AsciiDebugWindow& operator=(const AsciiDebugWindow&) = delete;

    void draw();
    bool valid() const { return !m_glyphs.empty(); }

private:
    std::vector<uint8_t> m_ttfData;

    // Fixed-resolution SDF bake (charset change only)
    std::vector<BakedGlyph>      m_glyphs;
    std::array<float, kSpatialK> m_regionMax{};
    std::vector<GlyphProfile>    m_normProfiles;
    glm::ivec2                   m_slotPx{};

    // Cell-size-dependent derived state
    std::vector<GlyphProfile> m_shapedProfiles;
    std::vector<float>        m_glyphCoverages;
    std::vector<float>        m_glyphRankNorms;
    CoverageRange             m_coverageRange{};
    glm::vec2                 m_cellSizePx{0.0f, 3.0f};
    glm::ivec2                m_texSize{2, 3};
    glm::ivec2                m_glyphPreviewSize{2, 3};

    int   m_lastCharsetIdx   = -1;
    glm::vec2 m_lastCellSizePx{-1.0f, -1.0f};
    float m_lastExponent     = -1.0f;

    DebugParams   m_params;
    GlyphVisMode  m_glyphVisMode = GlyphVisMode::Coverage;
    int           m_previewGlyphIdx = -1;

    PipelineResult m_result{};

    GLuint m_glyphTex = 0;
    GLuint m_patchTex = 0;

    [[nodiscard]] glm::vec2 computeCellSizePx() const;
    [[nodiscard]] bool      cellSizeDirty() const;

    void rebuildAtlas(int charsetIdx);
    void rebuildCellDerived();
    void reshapeProfiles();
    void refreshPreviewTextures();

    static GLuint createTex();
    static void   uploadTex(GLuint tex, int w, int h, const std::vector<uint8_t>& px);
    void ensureTextures(glm::ivec2 texSize);
    void ensureGlyphTexture(glm::ivec2 texSize);
    void uploadGlyphPreview(int glyphIdx);
    void uploadPatchPreview();

    void drawControls();
    void drawPipeline();
    void drawResults();
};
