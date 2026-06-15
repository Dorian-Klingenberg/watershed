# ── Shader compilation (Step 5) ───────────────────────────────────────────────
# Find fxc.exe using the same search strategy as grass-field-001:
#   1. Check the environment variables set by the VS developer shell.
#   2. Fall back to a glob over all installed SDK versions (picks highest).
find_program(
    grass_field_004_FXC_EXECUTABLE
    NAMES fxc.exe
    HINTS
        "$ENV{WindowsSdkVerBinPath}/x64"
        "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
    PATHS
        "C:/Program Files (x86)/Windows Kits/10/bin/x64"
        "C:/Program Files/Windows Kits/10/bin/x64"
)

if(NOT grass_field_004_FXC_EXECUTABLE)
    file(GLOB _fxc_candidates
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe"
        "C:/Program Files/Windows Kits/10/bin/*/x64/fxc.exe"
    )
    if(_fxc_candidates)
        list(SORT _fxc_candidates COMPARE NATURAL ORDER DESCENDING)
        list(GET  _fxc_candidates 0 grass_field_004_FXC_EXECUTABLE)
    endif()
endif()

if(NOT grass_field_004_FXC_EXECUTABLE)
    message(FATAL_ERROR "Could not find fxc.exe. Ensure the Windows SDK is installed.")
endif()

set(grass_field_004_SHADER_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_field_renderer.hlsl"
)
set(grass_field_004_WIREFRAME_SHADER_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/wireframe_renderer.hlsl"
)
set(grass_field_004_SPLIT_LOD_SHADER_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/split_lod_renderer.hlsl"
)
set(grass_field_004_SHADER_OUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

# Compile VS and PS from the single .hlsl source into .cso blobs.
# /WX = treat warnings as errors  /nologo = suppress fxc banner
add_custom_command(
    OUTPUT
        "${grass_field_004_SHADER_OUT_DIR}/grass_field_vs.cso"
        "${grass_field_004_SHADER_OUT_DIR}/grass_field_ps.cso"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${grass_field_004_SHADER_OUT_DIR}"
    COMMAND "${grass_field_004_FXC_EXECUTABLE}"
        /nologo /WX /T vs_5_0 /E VSMain
        /Fo "${grass_field_004_SHADER_OUT_DIR}/grass_field_vs.cso"
        "${grass_field_004_SHADER_SRC}"
    COMMAND "${grass_field_004_FXC_EXECUTABLE}"
        /nologo /WX /T ps_5_0 /E PSMain
        /Fo "${grass_field_004_SHADER_OUT_DIR}/grass_field_ps.cso"
        "${grass_field_004_SHADER_SRC}"
    DEPENDS "${grass_field_004_SHADER_SRC}"
    COMMENT "Compiling grass_field_renderer.hlsl (VSMain + PSMain)"
    VERBATIM
)

# Compile the wireframe renderer's VS and PS (wireframe_renderer.hlsl).
# The VS generates top-face quad geometry from SV_VertexID + heights SRV.
# The PS outputs a flat wire colour.
add_custom_command(
    OUTPUT
        "${grass_field_004_SHADER_OUT_DIR}/wireframe_vs.cso"
        "${grass_field_004_SHADER_OUT_DIR}/wireframe_ps.cso"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${grass_field_004_SHADER_OUT_DIR}"
    COMMAND "${grass_field_004_FXC_EXECUTABLE}"
        /nologo /WX /T vs_5_0 /E VSMain
        /Fo "${grass_field_004_SHADER_OUT_DIR}/wireframe_vs.cso"
        "${grass_field_004_WIREFRAME_SHADER_SRC}"
    COMMAND "${grass_field_004_FXC_EXECUTABLE}"
        /nologo /WX /T ps_5_0 /E PSMain
        /Fo "${grass_field_004_SHADER_OUT_DIR}/wireframe_ps.cso"
        "${grass_field_004_WIREFRAME_SHADER_SRC}"
    DEPENDS "${grass_field_004_WIREFRAME_SHADER_SRC}"
    COMMENT "Compiling wireframe_renderer.hlsl (VSMain + PSMain)"
    VERBATIM
)

# Compile the split coarse/fine renderer's VS and PS.
add_custom_command(
    OUTPUT
        "${grass_field_004_SHADER_OUT_DIR}/split_lod_vs.cso"
        "${grass_field_004_SHADER_OUT_DIR}/split_lod_ps.cso"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${grass_field_004_SHADER_OUT_DIR}"
    COMMAND "${grass_field_004_FXC_EXECUTABLE}"
        /nologo /WX /T vs_5_0 /E VSMain
        /Fo "${grass_field_004_SHADER_OUT_DIR}/split_lod_vs.cso"
        "${grass_field_004_SPLIT_LOD_SHADER_SRC}"
    COMMAND "${grass_field_004_FXC_EXECUTABLE}"
        /nologo /WX /T ps_5_0 /E PSMain
        /Fo "${grass_field_004_SHADER_OUT_DIR}/split_lod_ps.cso"
        "${grass_field_004_SPLIT_LOD_SHADER_SRC}"
    DEPENDS "${grass_field_004_SPLIT_LOD_SHADER_SRC}"
    COMMENT "Compiling split_lod_renderer.hlsl (VSMain + PSMain)"
    VERBATIM
)

add_custom_target(
    grannys_house_trials_grass_field_004_shaders
    DEPENDS
        "${grass_field_004_SHADER_OUT_DIR}/grass_field_vs.cso"
        "${grass_field_004_SHADER_OUT_DIR}/grass_field_ps.cso"
        "${grass_field_004_SHADER_OUT_DIR}/wireframe_vs.cso"
        "${grass_field_004_SHADER_OUT_DIR}/wireframe_ps.cso"
        "${grass_field_004_SHADER_OUT_DIR}/split_lod_vs.cso"
        "${grass_field_004_SHADER_OUT_DIR}/split_lod_ps.cso"
)

set_property(
    TARGET grannys_house_trials_grass_field_004_shaders
    PROPERTY FOLDER "Granny's House Trials/Subprojects"
)

# WIN32 tells CMake to build a Windows-subsystem executable (uses wWinMain,
# no console window) instead of a console-subsystem executable (uses main).
# Without WIN32 the linker would look for main() and ignore wWinMain.
add_executable(
    grannys_house_trials_grass_field_004
    WIN32
        main.cpp
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_win32.cpp
        third_party/imgui/backends/imgui_impl_dx12.cpp
)

add_dependencies(
    grannys_house_trials_grass_field_004
    grannys_house_trials_grass_field_004_shaders
)

# Copy compiled .cso blobs into the same directory as the exe after every build.
# main.cpp loads them from a path relative to the exe — this keeps them in sync.
add_custom_command(
    TARGET grannys_house_trials_grass_field_004 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:grannys_house_trials_grass_field_004>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${grass_field_004_SHADER_OUT_DIR}"
        "$<TARGET_FILE_DIR:grannys_house_trials_grass_field_004>/shaders"
    COMMENT "Copying compiled shaders to output directory"
)

# Require C++23 features. We will use std::optional, std::expected, and
# other C++23 additions in later steps.
target_compile_features(grannys_house_trials_grass_field_004 PRIVATE cxx_std_23)

target_include_directories(grannys_house_trials_grass_field_004 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
)

if(MSVC)
    # /std:c++23 is managed by target_compile_features above; only set /W4 here.
    target_compile_options(grannys_house_trials_grass_field_004 PRIVATE /W4)
endif()

# Set the VS debugger working directory to the repo root so that relative
# paths to assets, shaders, etc. resolve correctly when pressing F5.
# Mirrors the same property set on grass-field-002.
set_target_properties(grannys_house_trials_grass_field_004 PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Group this target with the other grass-field targets in the IDE.
set_property(
    TARGET grannys_house_trials_grass_field_004
    PROPERTY FOLDER "Granny's House Trials/Subprojects"
)

# ── Libraries ────────────────────────────────────────────────────────────────
target_link_libraries(grannys_house_trials_grass_field_004 PRIVATE
    d3d12
    dxgi
    d3dcompiler
    dwmapi
    shcore
    uxtheme
    # grannys_house_trials::core includes the gfx module (OrbitCamera is compiled
    # directly into the core library alongside the sim modules).
    grannys_house_trials::core
)
