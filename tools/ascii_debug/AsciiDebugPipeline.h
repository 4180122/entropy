#pragma once
#include <array>
#include <vector>
#include "rendering/AsciiAtlasBaker.h"  // BakedGlyph, GlyphProfile, kSpatialK

struct DebugParams {
    std::array<float, kSpatialK> input            = {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f};
    float                        cellHeight       = 16.0f;
    float                        spatialExp       = 1.0f;
    float                        intensityWeight   = 0.5f;
    int                          charsetIdx       = 0;
    bool                         regionMaxAuto    = true;
    std::array<float, kSpatialK> regionMaxManu     = {1.f,1.f,1.f,1.f,1.f,1.f};
};

struct PipelineResult {
    std::array<float, kSpatialK> afterGlobal{};
    SpatialMatchFeatures         cellFeatures{};

    struct Candidate {
        int                          glyphIdx      = 0;
        int                          codepoint     = 0;
        float                        score         = 0.0f;
        float                        shapeScore    = 0.0f;
        float                        densityScore  = 0.0f;
        float                        glyphRankNorm = 0.0f;
        float                        glyphCoverage = 0.0f;
        std::array<float, kSpatialK> shapedProfile{};
    };
    std::array<Candidate, 3> top3{};
    int                      numCandidates = 0;
};

PipelineResult runPipeline(
    const DebugParams&                  params,
    const std::array<float, kSpatialK>& regionMax,
    const std::vector<GlyphProfile>&    shapedProfiles,
    const std::vector<float>&           glyphRankNorms,
    const std::vector<float>&           glyphCoverages,
    const std::vector<BakedGlyph>&      glyphs);
