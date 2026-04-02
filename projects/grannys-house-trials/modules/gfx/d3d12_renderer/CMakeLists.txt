cmake_minimum_required(VERSION 3.20)

# D3D12 Graphics Renderer Module
# 
# Provides RAII-based Direct3D12 device management, frame synchronization,
# GPU buffer allocation, and pipeline state creation.
#
# Status: Experimental (Phase 1: Core extraction + documentation)
# 
# This module is built as part of grannys_house_trials::core and does not
# create a separate library boundary at this stage.

# Source files for the renderer module
set(D3D12_RENDERER_SOURCES
    # src/d3d12_context.cpp
    # src/graphics_frame.cpp
    # src/device_resources.cpp
    # src/pipeline_builder.cpp
    # (Implementations to follow in Phase 2)
)

# Public headers (included by consumers)
set(D3D12_RENDERER_HEADERS
    # include/grannys_house_trials/gfx/d3d12_context.h
    # include/grannys_house_trials/gfx/graphics_frame.h
    # include/grannys_house_trials/gfx/device_resources.h
    # include/grannys_house_trials/gfx/pipeline_builder.h
    # include/grannys_house_trials/gfx/render_constants.h
    # (Declarations to follow in Phase 2)
)

# Note: This module depends on grannys_house_trials existing target being 
# defined in parent CMakeLists.txt. Headers are published via the shared
# project library's include directories.
#
# Link requirements (handled by parent):
#  - d3d12.lib
#  - dxgi.lib
#  - d3dcompiler.lib
#
# Usage in parent CMakeLists.txt:
#
#   # Add d3d12_renderer source group
#   target_sources(grannys_house_trials PRIVATE
#       ${D3D12_RENDERER_SOURCES}
#   )
#   
#   # Ensure headers are discoverable
#   target_include_directories(grannys_house_trials PUBLIC
#       ${CMAKE_CURRENT_SOURCE_DIR}/d3d12_renderer/include
#   )

# For Phase 2 implementation, add:
#
# add_library(gfx_d3d12_renderer OBJECT
#     ${D3D12_RENDERER_SOURCES}
# )
# 
# target_include_directories(gfx_d3d12_renderer PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}/include
# )
# 
# target_compile_features(gfx_d3d12_renderer PUBLIC cxx_std_20)
# 
# target_link_libraries(gfx_d3d12_renderer PUBLIC
#     d3d12.lib
#     dxgi.lib
#     d3dcompiler.lib
# )
#
# set_target_properties(gfx_d3d12_renderer PROPERTIES
#     FOLDER "modules/gfx"
#     VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
# )
