configure_file(
  include/hvc/foundation/version_config.hpp.in
  generated/hvc/foundation/version_config.hpp
  @ONLY
)

include(CMakePackageConfigHelpers)

add_library(hvc_foundation STATIC)
add_library(hvc::foundation ALIAS hvc_foundation)

target_sources(
  hvc_foundation
  PRIVATE
    src/version.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
      include/hvc/foundation/version.hpp
)

target_include_directories(
  hvc_foundation
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_compile_features(hvc_foundation PUBLIC cxx_std_20)
target_link_libraries(
  hvc_foundation
  PRIVATE
    "$<BUILD_INTERFACE:hvc_project_options>"
)

set_target_properties(
  hvc_foundation
  PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME foundation
    OUTPUT_NAME hvc-foundation
)

set(hvc_install_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/Hvc")

configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/HvcConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/HvcConfig.cmake"
  INSTALL_DESTINATION "${hvc_install_cmake_dir}"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/HvcConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(
  TARGETS hvc_foundation
  EXPORT HvcFoundationTargets
  FILE_SET public_headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/hvc/foundation/version_config.hpp"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/hvc/foundation"
)

install(
  EXPORT HvcFoundationTargets
  FILE HvcTargets.cmake
  NAMESPACE hvc::
  DESTINATION "${hvc_install_cmake_dir}"
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/HvcConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/HvcConfigVersion.cmake"
  DESTINATION "${hvc_install_cmake_dir}"
)
