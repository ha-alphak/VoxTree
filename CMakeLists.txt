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

add_subdirectory(libs/foundation)
add_subdirectory(libs/domain)
add_subdirectory(libs/application)
add_subdirectory(libs/persistence)
add_subdirectory(libs/network)
add_subdirectory(apps/control-plane)

if(HVC_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()
