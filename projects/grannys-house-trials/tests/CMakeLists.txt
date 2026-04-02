include(FetchContent)

set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS ON CACHE BOOL "" FORCE)
set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  Catch2
  URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.9.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)

FetchContent_MakeAvailable(Catch2)

# Keep Catch2 out of the main project list in VS
set_property(TARGET Catch2 PROPERTY FOLDER "_Dependencies")
set_property(TARGET Catch2WithMain PROPERTY FOLDER "_Dependencies")

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(
  grannys_house_trials_tests
    util/non_empty_string_tests.cpp
    sim/round_log_tests.cpp
    sim/grannys_yard_scenario_tests.cpp
    sim/grass_field_tests.cpp
    sim/gravity_erosion_field_tests.cpp
    sim/adaptive_terrain_ownership_field_tests.cpp
    sim/sparse_refined_patch_field_tests.cpp
    gfx/orbit_camera_tests.cpp
    playtest/playtest_protocol_tests.cpp
    playtest/agent_command_tests.cpp
    playtest/evidence_board_view_tests.cpp
    playtest/grannys_yard_session_tests.cpp
    playtest/scenario_001_script_regression_tests.cpp
    playtest/round_result_tests.cpp
)

target_link_libraries(
  grannys_house_trials_tests
  PRIVATE
    Catch2::Catch2WithMain
    grannys_house_trials::core
)

set_property(TARGET grannys_house_trials_tests PROPERTY FOLDER "Granny's House Trials")

catch_discover_tests(
  grannys_house_trials_tests
  TEST_PREFIX "grannys_house_trials_tests :: "
)
