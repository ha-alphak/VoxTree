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

if(HVC_BUILD_LIVEKIT_QUALITY_GATE OR HVC_BUILD_WINDOWS_CLIENT)
  include(HvcLiveKitSdk)
  hvc_find_livekit_sdk()
endif()

add_subdirectory(libs/foundation)
add_subdirectory(libs/domain)
add_subdirectory(libs/client)
add_subdirectory(libs/application)
add_subdirectory(libs/persistence)
add_subdirectory(libs/network)
add_subdirectory(libs/livekit)
add_subdirectory(apps/control-plane)

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
