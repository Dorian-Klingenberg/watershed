cmake_minimum_required(VERSION 3.15)
project(Learning2026 CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(experiments/scalar-field-flooding)
add_subdirectory(experiments/agricultural-encounter-001)
add_subdirectory(experiments/tide-logic-regulator-002)
add_subdirectory(experiments/sea-fed-packet-pump-003)
add_subdirectory(experiments/voxel-infrastructure-004)
add_subdirectory(projects/grannys-house-trials)
