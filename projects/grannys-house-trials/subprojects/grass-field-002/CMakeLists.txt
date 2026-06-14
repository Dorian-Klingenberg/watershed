cmake_minimum_required(VERSION 3.20)

project(grannys_house_trials_grass_field_002 CXX)

set(grass_field_002_shader_source
    ${CMAKE_CURRENT_SOURCE_DIR}/../grass-field-001/shaders/grass_field_renderer.hlsl
)

find_program(
    GRASS_FIELD_002_FXC_EXECUTABLE
    NAMES fxc.exe
    HINTS
        "$ENV{WindowsSdkVerBinPath}/x64"
        "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
    PATHS
        "C:/Program Files (x86)/Windows Kits/10/bin/x64"
        "C:/Program Files/Windows Kits/10/bin/x64"
)

if(NOT GRASS_FIELD_002_FXC_EXECUTABLE)
    file(GLOB grass_field_002_fxc_candidates
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe"
        "C:/Program Files/Windows Kits/10/bin/*/x64/fxc.exe"
    )
    if(grass_field_002_fxc_candidates)
        list(SORT grass_field_002_fxc_candidates COMPARE NATURAL ORDER DESCENDING)
        list(GET grass_field_002_fxc_candidates 0 GRASS_FIELD_002_FXC_EXECUTABLE)
    endif()
endif()

if(NOT GRASS_FIELD_002_FXC_EXECUTABLE)
    message(FATAL_ERROR "Could not find fxc.exe for grass-field-002 shader compilation.")
endif()

set(grass_field_002_shader_output_dir
    ${CMAKE_CURRENT_BINARY_DIR}/shaders
)

set(grass_field_002_shader_outputs
    ${grass_field_002_shader_output_dir}/grass_field_vs.cso
    ${grass_field_002_shader_output_dir}/grass_field_coarse_ps.cso
)

add_custom_command(
    OUTPUT ${grass_field_002_shader_outputs}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${grass_field_002_shader_output_dir}
    COMMAND ${GRASS_FIELD_002_FXC_EXECUTABLE} /nologo /WX /T vs_5_0 /E VSMain /Fo ${grass_field_002_shader_output_dir}/grass_field_vs.cso ${grass_field_002_shader_source}
    COMMAND ${GRASS_FIELD_002_FXC_EXECUTABLE} /nologo /WX /T ps_5_0 /E PSCoarse /Fo ${grass_field_002_shader_output_dir}/grass_field_coarse_ps.cso ${grass_field_002_shader_source}
    DEPENDS ${grass_field_002_shader_source}
    VERBATIM
)

add_custom_target(
    grannys_house_trials_grass_field_002_shaders
    DEPENDS ${grass_field_002_shader_outputs}
)

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

add_dependencies(
    grannys_house_trials_grass_field_002
    grannys_house_trials_grass_field_002_shaders
)

add_custom_command(
    TARGET grannys_house_trials_grass_field_002
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:grannys_house_trials_grass_field_002>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${grass_field_002_shader_output_dir} $<TARGET_FILE_DIR:grannys_house_trials_grass_field_002>/shaders
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
set_property(TARGET grannys_house_trials_grass_field_002_shaders PROPERTY FOLDER "Granny's House Trials/Subprojects")
