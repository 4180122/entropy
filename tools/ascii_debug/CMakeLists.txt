add_executable(ascii_debug
    main.cpp
    AsciiDebugPipeline.cpp
    AsciiDebugRender.cpp
    AsciiDebugWindow.cpp
    "${entropy_SRC_DIR}/rendering/AsciiAtlasBaker.cpp"
)

set_target_properties(ascii_debug PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(ascii_debug PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${entropy_SRC_DIR}"
    "${glad_INCLUDE_DIR}"
    "${imgui_SRC_DIR}/imgui"
    "${imgui_SRC_DIR}/imgui/backends"
)

target_link_libraries(ascii_debug PRIVATE
    glfw
    glad
    glm::glm
    imgui
)
