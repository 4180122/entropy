cmake_minimum_required(VERSION 3.24.0)
project(EntropyTests CXX)

# ============================================================================
# This CMakeLists is intentionally standalone: it is NOT wired into the
# Entropy SuperBuild.  Build and run it from the existing build tree:
#
#   cd /path/to/entropy/build
#   cmake -DENTROPY_TESTS_DIR=../tests -P ../tests/run_tests.cmake
#
# Or, more directly, from the entropy/ root:
#
#   cmake -S tests -B build_tests        \
#         -DENTROPY_SRC_ROOT=<path>      \
#         -DENTROPY_BUILD_ROOT=<path>
#   cmake --build build_tests
#   ctest --test-dir build_tests -V
#
# Required CMake variables (pass via -D or set in the cache):
#   ENTROPY_SRC_ROOT   — path to entropy/src          (default: ../src)
#   ENTROPY_BUILD_ROOT — path to entropy build tree   (default: ../build)
# ============================================================================

if(NOT DEFINED ENTROPY_SRC_ROOT)
    set(ENTROPY_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../src")
endif()

if(NOT DEFINED ENTROPY_BUILD_ROOT)
    set(ENTROPY_BUILD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../build")
endif()

get_filename_component(ENTROPY_SRC_ROOT   "${ENTROPY_SRC_ROOT}"   ABSOLUTE)
get_filename_component(ENTROPY_BUILD_ROOT "${ENTROPY_BUILD_ROOT}" ABSOLUTE)

# ---------------------------------------------------------------------------
# Convenience variables pointing into the vendored external dependencies
# ---------------------------------------------------------------------------
set(EXT "${ENTROPY_BUILD_ROOT}/external")

set(boost_INC      "${EXT}/boost-1.87.0/src")
set(glm_INC        "${EXT}/glm-1.0.1/install/include")
set(spdlog_INC     "${EXT}/spdlog-1.15.1/install/include")
set(imgui_SRC      "${EXT}/imgui-1.91.8/src")
set(glad_INC       "${CMAKE_CURRENT_SOURCE_DIR}/../external/glad/glad_gl_3.3_core/include")

set(spdlog_LIB     "${EXT}/spdlog-1.15.1/install/lib/libspdlog.a")
set(imgui_LIB      "${ENTROPY_BUILD_ROOT}/lib/libimgui.a")
set(glad_LIB       "${ENTROPY_BUILD_ROOT}/lib/libglad.a")

# ---------------------------------------------------------------------------
# Helper: create a Boost.Test header-only test executable
# ---------------------------------------------------------------------------
function(add_entropy_test target_name source_file)
    add_executable(${target_name} "${source_file}")

    target_compile_features(${target_name} PRIVATE cxx_std_23)

    target_include_directories(${target_name} PRIVATE
        "${ENTROPY_SRC_ROOT}"
        "${glm_INC}"
    )
    # Boost (header-only, system include to suppress its warnings)
    target_include_directories(${target_name} SYSTEM PRIVATE "${boost_INC}")

    # spdlog is pulled in transitively by ShaderType / AsciiAtlas headers
    target_include_directories(${target_name} SYSTEM PRIVATE "${spdlog_INC}")

    target_compile_definitions(${target_name} PRIVATE SPDLOG_COMPILED_LIB)

    target_link_libraries(${target_name} PRIVATE "${spdlog_LIB}")

    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

# ---------------------------------------------------------------------------
# Test 1: ShaderType::to_string for the 3 new ASCII enum values
#         + stream operator + regression for existing values
#         No GL context required.
# ---------------------------------------------------------------------------
add_entropy_test(test_shader_type "${CMAKE_CURRENT_SOURCE_DIR}/test_shader_type.cpp")

target_sources(test_shader_type PRIVATE
    "${ENTROPY_SRC_ROOT}/rendering/common/ShaderType.cpp"
)

# ---------------------------------------------------------------------------
# Test 2: RenderData ASCII field defaults (mirror struct, no GL required)
# ---------------------------------------------------------------------------
add_entropy_test(test_render_data_ascii_defaults
    "${CMAKE_CURRENT_SOURCE_DIR}/test_render_data_ascii_defaults.cpp"
)

# ---------------------------------------------------------------------------
# Test 3: AsciiAtlas — no-GL tests (construction, move, invalid build args)
#
# AsciiAtlas.cpp includes glad/glad.h and imstb_truetype.h, so we need
# glad headers and the imgui static library (which contains the stb
# implementation compiled from imgui_draw.cpp).
# ---------------------------------------------------------------------------
add_entropy_test(test_ascii_atlas_no_gl
    "${CMAKE_CURRENT_SOURCE_DIR}/test_ascii_atlas_no_gl.cpp"
)

target_sources(test_ascii_atlas_no_gl PRIVATE
    "${ENTROPY_SRC_ROOT}/rendering/AsciiAtlas.cpp"
    "${ENTROPY_SRC_ROOT}/rendering/AsciiAtlasBaker.cpp"
)

target_include_directories(test_ascii_atlas_no_gl PRIVATE
    "${glad_INC}"
    "${imgui_SRC}/imgui"
)

target_link_libraries(test_ascii_atlas_no_gl PRIVATE
    "${glad_LIB}"
)

if(APPLE)
    target_link_libraries(test_ascii_atlas_no_gl PRIVATE
        "-framework OpenGL"
        "-framework CoreFoundation"
    )
endif()

# ---------------------------------------------------------------------------
# Test 4: cell-mean box filter invariance — pure C++, no GL required
# ---------------------------------------------------------------------------
add_entropy_test(test_cell_mean_invariance
    "${CMAKE_CURRENT_SOURCE_DIR}/test_cell_mean_invariance.cpp"
)

# ---------------------------------------------------------------------------
# Test 5: AsciiAtlas algorithmic invariants — pure C++, no GL required
#   - Fill-fraction computation (threshold, edge cases)
#   - Row-flip indexing (OpenGL UV convention)
#   - Luminance quantizer (clamping, bucket coverage)
# ---------------------------------------------------------------------------
add_entropy_test(test_ascii_atlas
    "${CMAKE_CURRENT_SOURCE_DIR}/test_ascii_atlas.cpp"
)

# ---------------------------------------------------------------------------
# Test 6: SDF UV inset mapping + cell aspect-ratio derivation — pure C++, no GL
# ---------------------------------------------------------------------------
add_entropy_test(test_sdf_uv_mapping
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sdf_uv_mapping.cpp"
)

# ---------------------------------------------------------------------------
# Test 7: AsciiAtlasBaker — pure CPU baking logic, no GL context required
# ---------------------------------------------------------------------------
add_entropy_test(test_ascii_atlas_baker
    "${CMAKE_CURRENT_SOURCE_DIR}/test_ascii_atlas_baker.cpp"
)

target_sources(test_ascii_atlas_baker PRIVATE
    "${ENTROPY_SRC_ROOT}/rendering/AsciiAtlasBaker.cpp"
)

target_include_directories(test_ascii_atlas_baker PRIVATE
    "${ENTROPY_SRC_ROOT}/rendering"
    "${imgui_SRC}/imgui"
)

# ---------------------------------------------------------------------------
# Test 8: ASCII debug pipeline — pure CPU normalization + matching logic
# ---------------------------------------------------------------------------
add_entropy_test(test_ascii_debug_pipeline
    "${CMAKE_CURRENT_SOURCE_DIR}/test_ascii_debug_pipeline.cpp"
)

target_sources(test_ascii_debug_pipeline PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/ascii_debug/AsciiDebugPipeline.cpp"
)

target_include_directories(test_ascii_debug_pipeline PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/ascii_debug"
)

# ---------------------------------------------------------------------------
enable_testing()
