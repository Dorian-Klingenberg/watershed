add_library(
  grannys_house_trials_sim
  STATIC
    src/grass_field.cpp
    src/round_log.cpp
    src/grannys_yard_scenario.cpp
)

add_library(grannys_house_trials::sim ALIAS grannys_house_trials_sim)

target_include_directories(
  grannys_house_trials_sim
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
  grannys_house_trials_sim
  PUBLIC
    grannys_house_trials::util
)
