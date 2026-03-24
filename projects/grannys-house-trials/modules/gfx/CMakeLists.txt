add_library(
  grannys_house_trials_gfx
  STATIC
    src/orbit_camera.cpp
)

add_library(grannys_house_trials::gfx ALIAS grannys_house_trials_gfx)

target_include_directories(
  grannys_house_trials_gfx
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

