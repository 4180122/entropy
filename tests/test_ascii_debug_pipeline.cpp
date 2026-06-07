#define BOOST_TEST_MODULE AsciiDebugPipelineTests
#include <boost/test/included/unit_test.hpp>
#include "AsciiDebugPipeline.h"
#include <numeric>

static std::vector<BakedGlyph> makeGlyphs(int N) {
    std::vector<BakedGlyph> g(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) g[static_cast<size_t>(i)].character = static_cast<char>('a' + i);
    return g;
}
static std::vector<int> identityRankOrder(int N) {
    std::vector<int> r(static_cast<size_t>(N));
    std::iota(r.begin(), r.end(), 0);
    return r;
}
static std::vector<int> uniformLumLut(int idx) { return std::vector<int>(256, idx); }
static constexpr float kEps = 1e-5f;

// ── Global normalisation ─────────────────────────────────────────────────
BOOST_AUTO_TEST_SUITE(GlobalNorm)

BOOST_AUTO_TEST_CASE(divides_by_regionMax)
{
    DebugParams p;
    p.input          = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    p.regionMaxAuto  = false;
    p.regionMaxManu  = {1.0f, 2.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    p.densityWindow  = 100;
    p.spatialExp     = 1.0f;
    p.meanLum        = 0.5f;
    std::array<float, kSpatialK> rm = {1,2,1,1,1,1};
    auto result = runPipeline(p, rm,
                              std::vector<GlyphProfile>(5),
                              identityRankOrder(5),
                              uniformLumLut(2),
                              makeGlyphs(5));
    BOOST_TEST(std::abs(result.afterGlobal[0] - 0.5f)  < kEps);
    BOOST_TEST(std::abs(result.afterGlobal[1] - 0.25f) < kEps);  // 0.5 / 2.0
}

BOOST_AUTO_TEST_SUITE_END()

// ── Local normalisation + exponent ───────────────────────────────────────
BOOST_AUTO_TEST_SUITE(LocalNormExp)

BOOST_AUTO_TEST_CASE(exponent_2_sharpens_profile)
{
    DebugParams p;
    p.input          = {1.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f};
    p.regionMaxAuto  = false;
    p.regionMaxManu  = {1,1,1,1,1,1};
    p.spatialExp     = 2.0f;
    p.densityWindow  = 100;
    p.meanLum        = 0.5f;
    std::array<float, kSpatialK> rm = {1,1,1,1,1,1};
    auto r = runPipeline(p, rm,
                         std::vector<GlyphProfile>(5),
                         identityRankOrder(5),
                         uniformLumLut(2),
                         makeGlyphs(5));
    // localMax = 1.0; afterLocal[i] = pow(input[i]/1.0, 2) * 1.0
    BOOST_TEST(std::abs(r.afterLocal[0] - 1.0f)  < kEps);
    BOOST_TEST(std::abs(r.afterLocal[1] - 0.25f) < kEps);
    BOOST_TEST(std::abs(r.afterLocal[2] - 0.0f)  < kEps);
    BOOST_TEST(std::abs(r.localMax - 1.0f) < kEps);
}

BOOST_AUTO_TEST_CASE(exponent_1_is_identity)
{
    DebugParams p;
    p.input          = {0.8f, 0.4f, 0.2f, 0.6f, 0.1f, 0.3f};
    p.regionMaxAuto  = false;
    p.regionMaxManu  = {1,1,1,1,1,1};
    p.spatialExp     = 1.0f;
    p.densityWindow  = 100;
    p.meanLum        = 0.5f;
    std::array<float, kSpatialK> rm = {1,1,1,1,1,1};
    auto r = runPipeline(p, rm,
                         std::vector<GlyphProfile>(5),
                         identityRankOrder(5),
                         uniformLumLut(2),
                         makeGlyphs(5));
    // With exp=1: afterLocal[i] = (v/localMax)^1 * localMax = v
    for (int i = 0; i < kSpatialK; ++i)
        BOOST_TEST(std::abs(r.afterLocal[i] - p.input[i]) < kEps,
                   "region " << i);
}

BOOST_AUTO_TEST_SUITE_END()

// ── LUT lookup ───────────────────────────────────────────────────────────
BOOST_AUTO_TEST_SUITE(LutLookup)

BOOST_AUTO_TEST_CASE(lut_determines_base_rank)
{
    DebugParams p;
    p.input          = {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f};
    p.regionMaxAuto  = false;
    p.regionMaxManu  = {1,1,1,1,1,1};
    p.spatialExp     = 1.0f;
    p.densityWindow  = 0;
    p.meanLum        = 1.0f;
    std::array<float, kSpatialK> rm = {1,1,1,1,1,1};
    std::vector<int> lut(256, 7);  // every bin → rank 7
    auto r = runPipeline(p, rm,
                         std::vector<GlyphProfile>(10),
                         identityRankOrder(10),
                         lut,
                         makeGlyphs(10));
    BOOST_TEST(r.baseRank == 7);
    BOOST_TEST(r.rLo == 7);
    BOOST_TEST(r.rHi == 7);
}

BOOST_AUTO_TEST_SUITE_END()

// ── Candidate ranking ────────────────────────────────────────────────────
BOOST_AUTO_TEST_SUITE(CandidateRanking)

BOOST_AUTO_TEST_CASE(exact_match_wins_with_zero_score)
{
    DebugParams p;
    p.input          = {0.5f, 0.3f, 0.0f, 0.8f, 0.1f, 0.4f};
    p.regionMaxAuto  = false;
    p.regionMaxManu  = {1,1,1,1,1,1};
    p.spatialExp     = 1.0f;  // afterLocal == input
    p.densityWindow  = 100;
    p.meanLum        = 0.5f;
    std::array<float, kSpatialK> rm = {1,1,1,1,1,1};
    std::vector<GlyphProfile> shaped(5);
    // Glyph 2 exactly matches afterLocal (= input with exp=1, localMax=0.8)
    shaped[2].regionFill = {0.5f, 0.3f, 0.0f, 0.8f, 0.1f, 0.4f};
    auto r = runPipeline(p, rm, shaped,
                         identityRankOrder(5),
                         uniformLumLut(2),
                         makeGlyphs(5));
    BOOST_TEST(r.numCandidates >= 1);
    BOOST_TEST(r.top3[0].glyphIdx == 2);
    BOOST_TEST(r.top3[0].score < kEps);
}

BOOST_AUTO_TEST_CASE(top3_are_sorted_by_score)
{
    DebugParams p;
    p.input          = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    p.regionMaxAuto  = false;
    p.regionMaxManu  = {1,1,1,1,1,1};
    p.spatialExp     = 1.0f;
    p.densityWindow  = 100;
    p.meanLum        = 0.5f;
    std::array<float, kSpatialK> rm = {1,1,1,1,1,1};
    std::vector<GlyphProfile> shaped(5);
    shaped[0].regionFill = {0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // closest
    shaped[1].regionFill = {0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // medium
    shaped[2].regionFill = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // farthest
    auto r = runPipeline(p, rm, shaped,
                         identityRankOrder(5),
                         uniformLumLut(2),
                         makeGlyphs(5));
    BOOST_TEST(r.numCandidates == 3);
    BOOST_TEST(r.top3[0].score <= r.top3[1].score);
    BOOST_TEST(r.top3[1].score <= r.top3[2].score);
}

BOOST_AUTO_TEST_SUITE_END()
