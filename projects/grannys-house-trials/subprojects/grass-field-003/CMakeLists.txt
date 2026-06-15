# ── Shader compilation (Step 5) ───────────────────────────────────────────────
# Find fxc.exe using the same search strategy as grass-field-001:
#   1. Check the environment variables set by the VS developer shell.
#   2. Fall back to a glob over all installed SDK versions (picks highest).
find_program(
    GRASS_FIELD_003_FXC_EXECUTABLE
    NAMES fxc.exe
    HINTS
        "$ENV{WindowsSdkVerBinPath}/x64"
        "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
    PATHS
        "C:/Program Files (x86)/Windows Kits/10/bin/x64"
        "C:/Program Files/Windows Kits/10/bin/x64"
)

if(NOT GRASS_FIELD_003_FXC_EXECUTABLE)
    file(GLOB _fxc_candidates
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe"
        "C:/Program Files/Windows Kits/10/bin/*/x64/fxc.exe"
    )
    if(_fxc_candidates)
        list(SORT _fxc_candidates COMPARE NATURAL ORDER DESCENDING)
        list(GET  _fxc_candidates 0 GRASS_FIELD_003_FXC_EXECUTABLE)
    endif()
endif()

if(NOT GRASS_FIELD_003_FXC_EXECUTABLE)
    message(FATAL_ERROR "Could not find fxc.exe. Ensure the Windows SDK is installed.")
endif()

set(GRASS_FIELD_003_SHADER_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_field_renderer.hlsl"
)
set(GRASS_FIELD_003_WIREFRAME_SHADER_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/wireframe_renderer.hlsl"
)
set(GRASS_FIELD_003_SPLIT_LOD_SHADER_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/split_lod_renderer.hlsl"
)
set(GRASS_FIELD_003_FLUID_COMPUTE_SHADER_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fluid_compute_phase1.hlsl"
)
set(GRASS_FIELD_003_FLUID_COMPUTE_TILED_SHADER_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fluid_compute_phase2_tiled.hlsl"
)
set(GRASS_FIELD_003_GPU_FLUID_RAYCAST_SHADER_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gpu_fluid_raycast_renderer.hlsl"
)
set(GRASS_FIELD_003_SHADER_OUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

# Compile VS and PS from the single .hlsl source into .cso blobs.
# /WX = treat warnings as errors  /nologo = suppress fxc banner
add_custom_command(
    OUTPUT
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/grass_field_vs.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/grass_field_ps.cso"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GRASS_FIELD_003_SHADER_OUT_DIR}"
    COMMAND "${GRASS_FIELD_003_FXC_EXECUTABLE}"
        /nologo /WX /T vs_5_0 /E VSMain
        /Fo "${GRASS_FIELD_003_SHADER_OUT_DIR}/grass_field_vs.cso"
        "${GRASS_FIELD_003_SHADER_SRC}"
    COMMAND "${GRASS_FIELD_003_FXC_EXECUTABLE}"
        /nologo /WX /T ps_5_0 /E PSMain
        /Fo "${GRASS_FIELD_003_SHADER_OUT_DIR}/grass_field_ps.cso"
        "${GRASS_FIELD_003_SHADER_SRC}"
    DEPENDS "${GRASS_FIELD_003_SHADER_SRC}"
    COMMENT "Compiling grass_field_renderer.hlsl (VSMain + PSMain)"
    VERBATIM
)

# Compile the GPU-resident raycast shader variant, which binds separate terrain
# and water resources produced by compute rather than CPU-composed field cells.
add_custom_command(
    OUTPUT
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/gpu_fluid_raycast_vs.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/gpu_fluid_raycast_ps.cso"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GRASS_FIELD_003_SHADER_OUT_DIR}"
    COMMAND "${GRASS_FIELD_003_FXC_EXECUTABLE}"
        /nologo /WX /T vs_5_0 /E VSMain
        /Fo "${GRASS_FIELD_003_SHADER_OUT_DIR}/gpu_fluid_raycast_vs.cso"
        "${GRASS_FIELD_003_GPU_FLUID_RAYCAST_SHADER_SRC}"
    COMMAND "${GRASS_FIELD_003_FXC_EXECUTABLE}"
        /nologo /WX /T ps_5_0 /E PSMain
        /Fo "${GRASS_FIELD_003_SHADER_OUT_DIR}/gpu_fluid_raycast_ps.cso"
        "${GRASS_FIELD_003_GPU_FLUID_RAYCAST_SHADER_SRC}"
    DEPENDS
        "${GRASS_FIELD_003_GPU_FLUID_RAYCAST_SHADER_SRC}"
        "${GRASS_FIELD_003_SHADER_SRC}"
    COMMENT "Compiling gpu_fluid_raycast_renderer.hlsl (VSMain + PSMain)"
    VERBATIM
)

# Compile the wireframe renderer's VS and PS (wireframe_renderer.hlsl).
# The VS generates top-face quad geometry from SV_VertexID + heights SRV.
# The PS outputs a flat wire colour.
add_custom_command(
    OUTPUT
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/wireframe_vs.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/wireframe_ps.cso"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GRASS_FIELD_003_SHADER_OUT_DIR}"
    COMMAND "${GRASS_FIELD_003_FXC_EXECUTABLE}"
        /nologo /WX /T vs_5_0 /E VSMain
        /Fo "${GRASS_FIELD_003_SHADER_OUT_DIR}/wireframe_vs.cso"
        "${GRASS_FIELD_003_WIREFRAME_SHADER_SRC}"
    COMMAND "${GRASS_FIELD_003_FXC_EXECUTABLE}"
        /nologo /WX /T ps_5_0 /E PSMain
        /Fo "${GRASS_FIELD_003_SHADER_OUT_DIR}/wireframe_ps.cso"
        "${GRASS_FIELD_003_WIREFRAME_SHADER_SRC}"
    DEPENDS "${GRASS_FIELD_003_WIREFRAME_SHADER_SRC}"
    COMMENT "Compiling wireframe_renderer.hlsl (VSMain + PSMain)"
    VERBATIM
)

# Compile the split coarse/fine renderer's VS and PS.
add_custom_command(
    OUTPUT
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/split_lod_vs.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/split_lod_ps.cso"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GRASS_FIELD_003_SHADER_OUT_DIR}"
    COMMAND "${GRASS_FIELD_003_FXC_EXECUTABLE}"
        /nologo /WX /T vs_5_0 /E VSMain
        /Fo "${GRASS_FIELD_003_SHADER_OUT_DIR}/split_lod_vs.cso"
        "${GRASS_FIELD_003_SPLIT_LOD_SHADER_SRC}"
    COMMAND "${GRASS_FIELD_003_FXC_EXECUTABLE}"
        /nologo /WX /T ps_5_0 /E PSMain
        /Fo "${GRASS_FIELD_003_SHADER_OUT_DIR}/split_lod_ps.cso"
        "${GRASS_FIELD_003_SPLIT_LOD_SHADER_SRC}"
    DEPENDS "${GRASS_FIELD_003_SPLIT_LOD_SHADER_SRC}"
    COMMENT "Compiling split_lod_renderer.hlsl (VSMain + PSMain)"
    VERBATIM
)

# Compile the Phase 1 cellular-water compute passes.
add_custom_command(
    OUTPUT
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/fluid_propose_cs.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/fluid_apply_cs.cso"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GRASS_FIELD_003_SHADER_OUT_DIR}"
    COMMAND "${GRASS_FIELD_003_FXC_EXECUTABLE}"
        /nologo /WX /T cs_5_0 /E ProposeCS
        /Fo "${GRASS_FIELD_003_SHADER_OUT_DIR}/fluid_propose_cs.cso"
        "${GRASS_FIELD_003_FLUID_COMPUTE_SHADER_SRC}"
    COMMAND "${GRASS_FIELD_003_FXC_EXECUTABLE}"
        /nologo /WX /T cs_5_0 /E ApplyCS
        /Fo "${GRASS_FIELD_003_SHADER_OUT_DIR}/fluid_apply_cs.cso"
        "${GRASS_FIELD_003_FLUID_COMPUTE_SHADER_SRC}"
    DEPENDS "${GRASS_FIELD_003_FLUID_COMPUTE_SHADER_SRC}"
    COMMENT "Compiling fluid_compute_phase1.hlsl (ProposeCS + ApplyCS)"
    VERBATIM
)

# Compile Phase 2 tiled proposal and unchanged apply passes.
add_custom_command(
    OUTPUT
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/fluid_propose_tiled_cs.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/fluid_apply_tiled_cs.cso"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GRASS_FIELD_003_SHADER_OUT_DIR}"
    COMMAND "${GRASS_FIELD_003_FXC_EXECUTABLE}"
        /nologo /WX /T cs_5_0 /E ProposeTiledCS
        /Fo "${GRASS_FIELD_003_SHADER_OUT_DIR}/fluid_propose_tiled_cs.cso"
        "${GRASS_FIELD_003_FLUID_COMPUTE_TILED_SHADER_SRC}"
    COMMAND "${GRASS_FIELD_003_FXC_EXECUTABLE}"
        /nologo /WX /T cs_5_0 /E ApplyCS
        /Fo "${GRASS_FIELD_003_SHADER_OUT_DIR}/fluid_apply_tiled_cs.cso"
        "${GRASS_FIELD_003_FLUID_COMPUTE_TILED_SHADER_SRC}"
    DEPENDS "${GRASS_FIELD_003_FLUID_COMPUTE_TILED_SHADER_SRC}"
    COMMENT "Compiling fluid_compute_phase2_tiled.hlsl (ProposeTiledCS + ApplyCS)"
    VERBATIM
)

add_custom_target(
    grannys_house_trials_grass_field_003_shaders
    DEPENDS
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/grass_field_vs.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/grass_field_ps.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/gpu_fluid_raycast_vs.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/gpu_fluid_raycast_ps.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/wireframe_vs.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/wireframe_ps.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/split_lod_vs.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/split_lod_ps.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/fluid_propose_cs.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/fluid_apply_cs.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/fluid_propose_tiled_cs.cso"
        "${GRASS_FIELD_003_SHADER_OUT_DIR}/fluid_apply_tiled_cs.cso"
)

set_property(
    TARGET grannys_house_trials_grass_field_003_shaders
    PROPERTY FOLDER "Granny's House Trials/Subprojects"
)

# WIN32 tells CMake to build a Windows-subsystem executable (uses wWinMain,
# no console window) instead of a console-subsystem executable (uses main).
# Without WIN32 the linker would look for main() and ignore wWinMain.
add_executable(
    grannys_house_trials_grass_field_003
    WIN32
        main.cpp
        sim/simple_erosion_field.cpp
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_win32.cpp
        third_party/imgui/backends/imgui_impl_dx12.cpp
)

add_dependencies(
    grannys_house_trials_grass_field_003
    grannys_house_trials_grass_field_003_shaders
)

# Copy compiled .cso blobs into the same directory as the exe after every build.
# main.cpp loads them from a path relative to the exe — this keeps them in sync.
add_custom_command(
    TARGET grannys_house_trials_grass_field_003 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:grannys_house_trials_grass_field_003>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${GRASS_FIELD_003_SHADER_OUT_DIR}"
        "$<TARGET_FILE_DIR:grannys_house_trials_grass_field_003>/shaders"
    COMMENT "Copying compiled shaders to output directory"
)

# Require C++23 features. We will use std::optional, std::expected, and
# other C++23 additions in later steps.
target_compile_features(grannys_house_trials_grass_field_003 PRIVATE cxx_std_23)

target_include_directories(grannys_house_trials_grass_field_003 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
)

if(MSVC)
    # /std:c++23 is managed by target_compile_features above; only set /W4 here.
    target_compile_options(grannys_house_trials_grass_field_003 PRIVATE /W4)
endif()

# Set the VS debugger working directory to the repo root so that relative
# paths to assets, shaders, etc. resolve correctly when pressing F5.
# Mirrors the same property set on grass-field-002.
set_target_properties(grannys_house_trials_grass_field_003 PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Group this target with the other grass-field targets in the IDE.
set_property(
    TARGET grannys_house_trials_grass_field_003
    PROPERTY FOLDER "Granny's House Trials/Subprojects"
)

# ── Libraries ────────────────────────────────────────────────────────────────
target_link_libraries(grannys_house_trials_grass_field_003 PRIVATE
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

# Console benchmark for deterministic fluid optimization comparisons. It uses
# the same live terrain seed and ImGui-linked simulator headers, without D3D12.
add_executable(
    grannys_house_trials_grass_field_003_fluid_benchmark
        fluid_sim_benchmark.cpp
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
)

target_compile_features(grannys_house_trials_grass_field_003_fluid_benchmark PRIVATE cxx_std_23)

target_include_directories(grannys_house_trials_grass_field_003_fluid_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
)

if(MSVC)
    target_compile_options(grannys_house_trials_grass_field_003_fluid_benchmark PRIVATE /W4)
endif()

target_link_libraries(grannys_house_trials_grass_field_003_fluid_benchmark PRIVATE
    grannys_house_trials::core
)

set_property(
    TARGET grannys_house_trials_grass_field_003_fluid_benchmark
    PROPERTY FOLDER "Granny's House Trials/Subprojects"
)

# Standalone HLSL compute experiment: compares Phase 1 and Phase 2 ping-pong
# GPU fluid steps, reads back results for CPU-baseline comparison, and reports
# timestamp timings.
add_executable(
    grannys_house_trials_grass_field_003_fluid_compute_phase1_benchmark
        fluid_compute_phase1_benchmark.cpp
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
)

add_dependencies(
    grannys_house_trials_grass_field_003_fluid_compute_phase1_benchmark
    grannys_house_trials_grass_field_003_shaders
)

target_compile_features(
    grannys_house_trials_grass_field_003_fluid_compute_phase1_benchmark PRIVATE cxx_std_23)

target_include_directories(
    grannys_house_trials_grass_field_003_fluid_compute_phase1_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
)

if(MSVC)
    target_compile_options(
        grannys_house_trials_grass_field_003_fluid_compute_phase1_benchmark PRIVATE /W4)
endif()

target_link_libraries(grannys_house_trials_grass_field_003_fluid_compute_phase1_benchmark PRIVATE
    d3d12
    dxgi
    d3dcompiler
    grannys_house_trials::core
)

add_custom_command(
    TARGET grannys_house_trials_grass_field_003_fluid_compute_phase1_benchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:grannys_house_trials_grass_field_003_fluid_compute_phase1_benchmark>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${GRASS_FIELD_003_SHADER_OUT_DIR}"
        "$<TARGET_FILE_DIR:grannys_house_trials_grass_field_003_fluid_compute_phase1_benchmark>/shaders"
    COMMENT "Copying compute experiment shaders to output directory"
)

set_property(
    TARGET grannys_house_trials_grass_field_003_fluid_compute_phase1_benchmark
    PROPERTY FOLDER "Granny's House Trials/Subprojects"
)

# Keep the generated filename short in the OneDrive workspace. The historical
# target name remains stable, while the linker writes a compact neutral .exe.
set_target_properties(
    grannys_house_trials_grass_field_003_fluid_compute_phase1_benchmark PROPERTIES
    OUTPUT_NAME "gf003_fluid_compute"
)

# ── Step snapshot executables ─────────────────────────────────────────────────
#
# Each frozen step is a standalone WIN32 executable that can be launched and
# diffed against the live main target. They live in the same output directory
# as the main exe and share the same shaders/ folder (copied by the main
# target's POST_BUILD command).
#
# Source groupings:
#   IMGUI_SRCS  — the six imgui translation units (steps 3+)
#   SIM_SRCS    — the erosion sim TU (steps 4+)
# Steps 1–2 get neither; the linker strips any unreachable symbols.

set(_GF003_IMGUI_SRCS
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_dx12.cpp
)

set(_GF003_SIM_SRCS
    sim/simple_erosion_field.cpp
)

function(add_gf003_step STEP_NUM EXTRA_SRCS)
    set(TARGET grannys_house_trials_grass_field_003_step${STEP_NUM})

    add_executable(${TARGET} WIN32
        main_step${STEP_NUM}.cpp
        ${EXTRA_SRCS}
    )

    # All steps depend on the compiled shader blobs so the output directory
    # always contains a fresh shaders/ folder even when only a step is rebuilt.
    add_dependencies(${TARGET} grannys_house_trials_grass_field_003_shaders)

    target_compile_features(${TARGET} PRIVATE cxx_std_23)

    target_include_directories(${TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
    )

    if(MSVC)
        target_compile_options(${TARGET} PRIVATE /W4)
    endif()

    set_target_properties(${TARGET} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    set_property(TARGET ${TARGET} PROPERTY
        FOLDER "Granny's House Trials/Subprojects/Step Snapshots"
    )

    target_link_libraries(${TARGET} PRIVATE
        d3d12
        dxgi
        d3dcompiler
        dwmapi
        shcore
        uxtheme
        grannys_house_trials::core
    )
endfunction()

# Steps 1–2: Win32 / D3D12 only — no ImGui, no sim
add_gf003_step(1  "")
add_gf003_step(2  "")

# Step 3: D3D12 + ImGui — no sim yet
add_gf003_step(3  "${_GF003_IMGUI_SRCS}")

# Steps 4+: ImGui + sim (orbit camera, IFieldSim, pluggable renderers live in core)
add_gf003_step(4  "${_GF003_IMGUI_SRCS};${_GF003_SIM_SRCS}")
add_gf003_step(5  "${_GF003_IMGUI_SRCS};${_GF003_SIM_SRCS}")
add_gf003_step(7  "${_GF003_IMGUI_SRCS};${_GF003_SIM_SRCS}")
add_gf003_step(8  "${_GF003_IMGUI_SRCS};${_GF003_SIM_SRCS}")
add_gf003_step(9  "${_GF003_IMGUI_SRCS};${_GF003_SIM_SRCS}")
add_gf003_step(10 "${_GF003_IMGUI_SRCS};${_GF003_SIM_SRCS}")
