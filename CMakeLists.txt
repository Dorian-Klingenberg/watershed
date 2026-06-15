cmake_minimum_required(VERSION 3.15)
project(watershed CXX)

# Organise projects into named folders in the VS solution
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Suppress CTest dashboard targets (Continuous/Experimental/Nightly/NightlyMemoryCheck)
# They clutter the solution; run ctest directly from the command line instead
set(CTEST_TARGETS_ADDED 1)
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

# Assign CMake infrastructure targets to a hidden folder
foreach(_t ALL_BUILD ZERO_CHECK RUN_TESTS)
    if(TARGET ${_t})
        set_property(TARGET ${_t} PROPERTY FOLDER "_CMake")
    endif()
endforeach()
