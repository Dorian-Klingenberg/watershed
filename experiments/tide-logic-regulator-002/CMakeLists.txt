cmake_minimum_required(VERSION 3.15)

if(MSVC)
  add_compile_options(/W4 /Zi /EHsc)
else()
  add_compile_options(-Wall -Wextra -g)
endif()

add_executable(
  tide_logic_regulator_002_experiment
  main.cpp
  simulator.cpp
  dashboard.cpp
)

set_property(TARGET tide_logic_regulator_002_experiment PROPERTY FOLDER "Experiments")
