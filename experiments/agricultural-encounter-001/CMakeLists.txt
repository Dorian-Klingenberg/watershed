cmake_minimum_required(VERSION 3.15)

if(MSVC)
  add_compile_options(/W4 /Zi /EHsc)
else()
  add_compile_options(-Wall -Wextra -g)
endif()

add_executable(
  agricultural_encounter_001_experiment
  main.cpp
  encounter.cpp
  localized_intervention.cpp
  console_ui.cpp
)

set_property(TARGET agricultural_encounter_001_experiment PROPERTY FOLDER "Experiments")
