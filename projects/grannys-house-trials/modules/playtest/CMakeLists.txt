add_library(
  grannys_house_trials_playtest
  STATIC
    src/turn_packet.cpp
    src/evidence_board_view.cpp
    src/evidence_board_panel.cpp
)

add_library(grannys_house_trials::playtest ALIAS grannys_house_trials_playtest)

target_include_directories(
  grannys_house_trials_playtest
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
  grannys_house_trials_playtest
  PUBLIC
    grannys_house_trials::sim
    grannys_house_trials::util
)
