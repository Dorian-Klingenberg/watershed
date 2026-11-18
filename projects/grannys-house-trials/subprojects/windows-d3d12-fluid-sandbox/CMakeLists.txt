find_program(
    WINDOWS_D3D12_FLUID_FXC_EXECUTABLE
    NAMES fxc.exe
    HINTS
        "$ENV{WindowsSdkVerBinPath}/arm64"
        "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/arm64"
    PATHS
        "C:/Program Files (x86)/Windows Kits/10/bin/x64"
        "C:/Program Files/Windows Kits/10/bin/x64"
)

if(NOT WINDOWS_D3D12_FLUID_FXC_EXECUTABLE)
    file(GLOB _fxc_candidates
        "C:/Program Files (x86)/Windows Kits/10/bin/*/arm64/fxc.exe"
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe"
    )
    if(_fxc_candidates)
        list(SORT _fxc_candidates COMPARE NATURAL ORDER DESCENDING)
        list(GET _fxc_candidates 0 WINDOWS_D3D12_FLUID_FXC_EXECUTABLE)
    endif()
endif()

if(NOT WINDOWS_D3D12_FLUID_FXC_EXECUTABLE)
    message(FATAL_ERROR "Could not find fxc.exe. Ensure the Windows 11 SDK is installed.")
endif()

set(WINDOWS_D3D12_FLUID_NATIVE_SHADER_OUT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/native/shaders"
)

add_custom_command(
    OUTPUT
        "${WINDOWS_D3D12_FLUID_NATIVE_SHADER_OUT_DIR}/field_render_vs.cso"
        "${WINDOWS_D3D12_FLUID_NATIVE_SHADER_OUT_DIR}/field_render_ps.cso"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${WINDOWS_D3D12_FLUID_NATIVE_SHADER_OUT_DIR}"
    COMMAND "${WINDOWS_D3D12_FLUID_FXC_EXECUTABLE}"
        /nologo /WX /T vs_5_0 /E VSMain
        /Fo "${WINDOWS_D3D12_FLUID_NATIVE_SHADER_OUT_DIR}/field_render_vs.cso"
        "${CMAKE_CURRENT_SOURCE_DIR}/native/field_render.hlsl"
    COMMAND "${WINDOWS_D3D12_FLUID_FXC_EXECUTABLE}"
        /nologo /WX /T ps_5_0 /E PSMain
        /Fo "${WINDOWS_D3D12_FLUID_NATIVE_SHADER_OUT_DIR}/field_render_ps.cso"
        "${CMAKE_CURRENT_SOURCE_DIR}/native/field_render.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/native/field_render.hlsl"
)

add_custom_target(windows_d3d12_fluid_native_shaders
    DEPENDS
        "${WINDOWS_D3D12_FLUID_NATIVE_SHADER_OUT_DIR}/field_render_vs.cso"
        "${WINDOWS_D3D12_FLUID_NATIVE_SHADER_OUT_DIR}/field_render_ps.cso"
)

add_library(windows_d3d12_fluid_native SHARED
    native/native_d3d12_fluid_renderer.cpp
)

target_include_directories(windows_d3d12_fluid_native PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/native"
)

target_compile_definitions(windows_d3d12_fluid_native PRIVATE
    UNICODE
    _UNICODE
)

target_link_libraries(windows_d3d12_fluid_native PRIVATE
    d3d12
    dxgi
    d3dcompiler
)

set_property(TARGET windows_d3d12_fluid_native PROPERTY FOLDER "Granny's House Trials/Subprojects")
set_property(TARGET windows_d3d12_fluid_native PROPERTY OUTPUT_NAME "NativeD3D12FluidRenderer")
set_property(TARGET windows_d3d12_fluid_native PROPERTY
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/native"
)
set_property(TARGET windows_d3d12_fluid_native PROPERTY
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/native/Debug"
)
set_property(TARGET windows_d3d12_fluid_native PROPERTY
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/native/Release"
)

add_dependencies(windows_d3d12_fluid_native windows_d3d12_fluid_native_shaders)

add_executable(windows_d3d12_compatibility_probe
    compatibility_probe.cpp
)

target_link_libraries(windows_d3d12_compatibility_probe PRIVATE
    d3d12
    d3d11
    dxgi
)

set_property(TARGET windows_d3d12_compatibility_probe PROPERTY FOLDER "Granny's House Trials/Subprojects")
set_property(TARGET windows_d3d12_compatibility_probe PROPERTY
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
