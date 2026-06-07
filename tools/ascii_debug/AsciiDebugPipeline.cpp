#include "AsciiDebugPipeline.h"
#include <algorithm>
#include <cmath>

PipelineResult runPipeline(
    const DebugParams&                  params,
    const std::array<float, kSpatialK>& regionMax,
    const std::vector<GlyphProfile>&    shapedProfiles,
    const std::vector<float>&           glyphRankNorms,
    const std::vector<float>&           glyphCoverages,
    const std::vector<BakedGlyph>&      glyphs)
{
    const int N = static_cast<int>(glyphs.size());
    PipelineResult r{};

    const auto& rm = params.regionMaxAuto ? regionMax : params.regionMaxManu;
    for (int i = 0; i < kSpatialK; ++i)
        r.afterGlobal[i] = params.input[i] / std::max(rm[static_cast<size_t>(i)], 1e-6f);

    r.cellFeatures = computeSpatialMatchFeatures(r.afterGlobal, params.spatialExp);
    r.cellFeatures.luminance = computeMeanLuminance(params.input);

    if (N <= 0 || static_cast<int>(shapedProfiles.size()) < N
        || static_cast<int>(glyphRankNorms.size()) < N
        || static_cast<int>(glyphCoverages.size()) < N) {
        r.numCandidates = 0;
        return r;
    }

    struct Scored {
        float score;
        float shapeScore;
        float densityScore;
        int   gIdx;
    };
    std::vector<Scored> scores;
    scores.reserve(static_cast<size_t>(N));

    for (int g = 0; g < N; ++g) {
        const auto& glyph = shapedProfiles[static_cast<size_t>(g)];
        const float rankNorm = glyphRankNorms[static_cast<size_t>(g)];
        const float combined = spatialMatchScore(
            r.cellFeatures, glyph, rankNorm, params.intensityWeight);

        float shapeD2 = 0.0f;
        for (int i = 0; i < kSpatialK; ++i) {
            const float diff = r.cellFeatures.shape[static_cast<size_t>(i)]
                             - glyph.regionFill[static_cast<size_t>(i)];
            shapeD2 += diff * diff;
        }
        shapeD2 /= static_cast<float>(kSpatialK);

        const float densityDiff = r.cellFeatures.luminance - rankNorm;
        const float densityD2 = densityDiff * densityDiff;

        scores.push_back({combined, shapeD2, densityD2, g});
    }

    const int topN = std::min(3, N);
    std::partial_sort(scores.begin(), scores.begin() + topN, scores.end(),
                      [](const Scored& a, const Scored& b){ return a.score < b.score; });

    r.numCandidates = topN;
    for (int k = 0; k < topN; ++k) {
        const auto& s = scores[static_cast<size_t>(k)];
        const auto& glyph = shapedProfiles[static_cast<size_t>(s.gIdx)];
        auto& c = r.top3[static_cast<size_t>(k)];
        c.glyphIdx       = s.gIdx;
        c.codepoint      = glyphs[static_cast<size_t>(s.gIdx)].codepoint;
        c.score          = s.score;
        c.shapeScore     = s.shapeScore;
        c.densityScore   = s.densityScore;
        c.glyphRankNorm  = glyphRankNorms[static_cast<size_t>(s.gIdx)];
        c.glyphCoverage  = glyphCoverages[static_cast<size_t>(s.gIdx)];
        c.shapedProfile  = glyph.regionFill;
    }
    return r;
}
