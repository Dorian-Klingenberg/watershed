set(grass_field_001_shader_source
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_field_renderer.hlsl
)

find_program(
  GRASS_FIELD_001_FXC_EXECUTABLE
  NAMES fxc.exe
  HINTS
    "$ENV{WindowsSdkVerBinPath}/x64"
    "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
  PATHS
    "C:/Program Files (x86)/Windows Kits/10/bin/x64"
    "C:/Program Files/Windows Kits/10/bin/x64"
)

if(NOT GRASS_FIELD_001_FXC_EXECUTABLE)
  file(GLOB grass_field_001_fxc_candidates
    "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe"
    "C:/Program Files/Windows Kits/10/bin/*/x64/fxc.exe"
  )
  if(grass_field_001_fxc_candidates)
    list(SORT grass_field_001_fxc_candidates COMPARE NATURAL ORDER DESCENDING)
    list(GET grass_field_001_fxc_candidates 0 GRASS_FIELD_001_FXC_EXECUTABLE)
  endif()
endif()

if(NOT GRASS_FIELD_001_FXC_EXECUTABLE)
  message(FATAL_ERROR "Could not find fxc.exe for grass-field-001 shader compilation.")
endif()

set(grass_field_001_shader_output_dir
  ${CMAKE_CURRENT_BINARY_DIR}/shaders
)

set(grass_field_001_shader_outputs
  ${grass_field_001_shader_output_dir}/grass_field_vs.cso
  ${grass_field_001_shader_output_dir}/grass_field_coarse_ps.cso
  ${grass_field_001_shader_output_dir}/grass_field_refined_ps.cso
  ${grass_field_001_shader_output_dir}/grass_field_hybrid_ps.cso
)

add_custom_command(
  OUTPUT ${grass_field_001_shader_outputs}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${grass_field_001_shader_output_dir}
  COMMAND ${GRASS_FIELD_001_FXC_EXECUTABLE} /nologo /WX /T vs_5_0 /E VSMain /Fo ${grass_field_001_shader_output_dir}/grass_field_vs.cso ${grass_field_001_shader_source}
  COMMAND ${GRASS_FIELD_001_FXC_EXECUTABLE} /nologo /WX /T ps_5_0 /E PSCoarse /Fo ${grass_field_001_shader_output_dir}/grass_field_coarse_ps.cso ${grass_field_001_shader_source}
  COMMAND ${GRASS_FIELD_001_FXC_EXECUTABLE} /nologo /WX /T ps_5_0 /E PSRefined /Fo ${grass_field_001_shader_output_dir}/grass_field_refined_ps.cso ${grass_field_001_shader_source}
  COMMAND ${GRASS_FIELD_001_FXC_EXECUTABLE} /nologo /WX /T ps_5_0 /E PSHybrid /Fo ${grass_field_001_shader_output_dir}/grass_field_hybrid_ps.cso ${grass_field_001_shader_source}
  DEPENDS ${grass_field_001_shader_source}
  VERBATIM
)

add_custom_target(
  grannys_house_trials_grass_field_001_shaders
  DEPENDS ${grass_field_001_shader_outputs}
)

add_executable(
  grannys_house_trials_grass_field_001
  WIN32
  main.cpp
)

add_dependencies(
  grannys_house_trials_grass_field_001
  grannys_house_trials_grass_field_001_shaders
)

add_custom_command(
  TARGET grannys_house_trials_grass_field_001
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:grannys_house_trials_grass_field_001>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${grass_field_001_shader_output_dir} $<TARGET_FILE_DIR:grannys_house_trials_grass_field_001>/shaders
)

target_link_libraries(
  grannys_house_trials_grass_field_001
  PRIVATE
    grannys_house_trials::core
    d3d12
    dxgi
    d3dcompiler
    comctl32
    dwmapi
    uxtheme
)
