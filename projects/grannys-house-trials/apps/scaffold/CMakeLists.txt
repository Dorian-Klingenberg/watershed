add_executable(
  grannys_house_trials_scaffold
  main.cpp
)

target_link_libraries(
  grannys_house_trials_scaffold
  PRIVATE
    grannys_house_trials::util
    grannys_house_trials::sim
    grannys_house_trials::gfx
)

