cmake_minimum_required(VERSION 3.15)

include(CTest)

if(MSVC)
  add_compile_options(/W4 /Zi /EHsc)
else()
  add_compile_options(-Wall -Wextra -g)
endif()

set(SCALAR_FIELD_FLOODING_DOMAIN_SOURCES
  simulation.cpp
  terrain.cpp
  map_generation.cpp
)

add_executable(
  scalar_field_flooding_experiment
  1-scalar-field-flooding.cpp
  ${SCALAR_FIELD_FLOODING_DOMAIN_SOURCES}
  ui.cpp
)

add_executable(
  scalar_field_flooding_tests
  test_main.cpp
  ${SCALAR_FIELD_FLOODING_DOMAIN_SOURCES}
  simulation_tests.cpp
)

add_test(NAME scalar_field_flooding_tests COMMAND scalar_field_flooding_tests)
