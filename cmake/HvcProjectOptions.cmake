include_guard(GLOBAL)

include(HvcSanitizers)
include(HvcWarnings)

function(hvc_initialize_project_options)
  option(HVC_BUILD_TESTS "Build the HVC test suite" "${PROJECT_IS_TOP_LEVEL}")
  option(HVC_ENABLE_CLANG_TIDY "Run clang-tidy while compiling project targets" OFF)
  option(HVC_ENABLE_SANITIZERS "Enable supported runtime sanitizers" OFF)
  option(HVC_WARNINGS_AS_ERRORS "Treat compiler warnings in HVC code as errors" ON)

  add_library(hvc_project_options INTERFACE)
  add_library(hvc::project_options ALIAS hvc_project_options)

  target_compile_features(hvc_project_options INTERFACE cxx_std_20)

  hvc_set_project_warnings(hvc_project_options "${HVC_WARNINGS_AS_ERRORS}")
  hvc_enable_sanitizers(hvc_project_options "${HVC_ENABLE_SANITIZERS}")

  if(HVC_ENABLE_CLANG_TIDY)
    find_program(HVC_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
    set(
      CMAKE_CXX_CLANG_TIDY
      "${HVC_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
      CACHE STRING "clang-tidy command" FORCE
    )
  endif()
endfunction()
