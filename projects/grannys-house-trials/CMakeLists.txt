cmake_minimum_required(VERSION 3.15)

add_library(
  grannys_house_trials
  STATIC
    modules/util/src/non_empty_string.cpp
    modules/sim/src/grass_field.cpp
    modules/sim/src/gravity_erosion_field.cpp
    modules/sim/src/adaptive_terrain_ownership_field.cpp
    modules/sim/src/sparse_refined_patch_field.cpp
    modules/sim/src/round_log.cpp
    modules/sim/src/grannys_yard_scenario.cpp
    modules/playtest/src/turn_packet.cpp
    modules/playtest/src/evidence_board_view.cpp
    modules/gfx/src/orbit_camera.cpp
)

add_library(grannys_house_trials::core ALIAS grannys_house_trials)

target_include_directories(
  grannys_house_trials
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/util/include
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/sim/include
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/playtest/include
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/gfx/include
)

add_subdirectory(subprojects)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
