cmake_minimum_required(VERSION 3.15)

if(MSVC)
  add_compile_options(/W4 /Zi /EHsc)
else()
  add_compile_options(-Wall -Wextra -g)
endif()

add_executable(
  voxel_infrastructure_004_experiment
  main.cpp
  simulator.cpp
  dashboard.cpp
  world_definition.cpp
)

target_include_directories(
  voxel_infrastructure_004_experiment
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_property(TARGET voxel_infrastructure_004_experiment PROPERTY FOLDER "Experiments")

add_subdirectory(subexperiments/voxel-d3d12-001)
