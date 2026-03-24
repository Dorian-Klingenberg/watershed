add_library(
  grannys_house_trials_util
  STATIC
    src/non_empty_string.cpp
)

add_library(grannys_house_trials::util ALIAS grannys_house_trials_util)

target_include_directories(
  grannys_house_trials_util
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

