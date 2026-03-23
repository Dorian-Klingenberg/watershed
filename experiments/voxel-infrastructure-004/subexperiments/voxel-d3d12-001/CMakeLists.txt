cmake_minimum_required(VERSION 3.15)

add_executable(
  voxel_d3d12_001
  WIN32
  main.cpp
  ../../world_definition.cpp
)

target_include_directories(
  voxel_d3d12_001
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(
  voxel_d3d12_001
  PRIVATE
    d3d12
    dxgi
    d3dcompiler
)
