cmake_minimum_required(VERSION 3.25)

project(
  HierarchicalVoiceCommunication
  VERSION 0.1.0
  DESCRIPTION "Secure hierarchical real-time voice communication"
  LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(HvcProjectOptions)

hvc_initialize_project_options()

option(
  HVC_BUILD_LIVEKIT_QUALITY_GATE
  "Build the native LiveKit two-client quality-gate executable"
  OFF
)
option(
  HVC_BUILD_WINDOWS_CLIENT
  "Build the Windows App SDK / WinUI 3 client"
  OFF
)
option(
  HVC_BUILD_DEBIAN_QUALITY_GATE
  "Build the Qt/PipeWire/portal/HID Debian desktop quality-gate prototype"
  OFF
)
option(HVC_BUILD_EXAMPLES "Build documented SDK integration examples" "${PROJECT_IS_TOP_LEVEL}")
option(HVC_BUILD_DOCUMENTATION "Build the C++ API documentation with Doxygen" OFF)

if(HVC_BUILD_DOCUMENTATION)
  find_package(Doxygen REQUIRED)

  file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}" HVC_DOC_SOURCE_DIR)
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_BINARY_DIR}/documentation" HVC_DOC_OUTPUT_DIR)
  configure_file(
    "${PROJECT_SOURCE_DIR}/doks/Doxyfile.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    @ONLY
  )

  add_custom_target(
    hvc_documentation ALL
    COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Building C++ API documentation"
    VERBATIM
  )
endif()

if(HVC_BUILD_LIVEKIT_QUALITY_GATE OR HVC_BUILD_WINDOWS_CLIENT)
  include(HvcLiveKitSdk)
  hvc_find_livekit_sdk()
endif()

add_subdirectory(libs/foundation)
add_subdirectory(libs/domain)
add_subdirectory(libs/client)
add_subdirectory(libs/presentation)
add_subdirectory(libs/application)
add_subdirectory(libs/persistence)
add_subdirectory(libs/network)
add_subdirectory(libs/livekit)
add_subdirectory(apps/control-plane)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_subdirectory(apps/load-driver)
  if(HVC_BUILD_DEBIAN_QUALITY_GATE)
    add_subdirectory(apps/debian-quality-gate)
  endif()
endif()

if(HVC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(WIN32)
  add_subdirectory(apps/input-quality-gate)
endif()

if(HVC_BUILD_WINDOWS_CLIENT)
  if(NOT WIN32)
    message(FATAL_ERROR "The WinUI client can only be built on Windows.")
  endif()
  add_subdirectory(apps/windows-client)
endif()

if(HVC_BUILD_LIVEKIT_QUALITY_GATE)
  add_subdirectory(apps/livekit-quality-gate)
endif()

if(HVC_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()
