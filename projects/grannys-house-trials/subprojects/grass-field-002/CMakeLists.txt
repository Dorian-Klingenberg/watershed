cmake_minimum_required(VERSION 3.20)

project(grannys_house_trials_grass_field_002 CXX)

# ImGui source
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
if(NOT EXISTS "${IMGUI_DIR}")
    message(FATAL_ERROR "ImGui not found at ${IMGUI_DIR}. Run scripts/setup-imgui-dependency.ps1 first.")
endif()

# Add executable
add_executable(grannys_house_trials_grass_field_002 WIN32
    main.cpp
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx12.cpp"
)

target_include_directories(grannys_house_trials_grass_field_002 PRIVATE
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

target_link_libraries(grannys_house_trials_grass_field_002 PRIVATE
    grannys_house_trials::core
    d3d12
    dxgi
    d3dcompiler
    dwmapi
    shcore
    uxtheme
)

target_compile_features(grannys_house_trials_grass_field_002 PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(grannys_house_trials_grass_field_002 PRIVATE /W4)
endif()

set_target_properties(grannys_house_trials_grass_field_002 PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

set_property(TARGET grannys_house_trials_grass_field_002 PROPERTY FOLDER "Granny's House Trials/Subprojects")
