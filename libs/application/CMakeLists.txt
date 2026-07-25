add_library(hvc_application STATIC)
add_library(hvc::application ALIAS hvc_application)

target_sources(
  hvc_application
  PRIVATE
    src/control_plane.cpp
    src/in_memory_control_plane.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
      include/hvc/application/control_plane.hpp
      include/hvc/application/in_memory_control_plane.hpp
      include/hvc/application/transmission_audit.hpp
)

target_include_directories(
  hvc_application
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_compile_features(hvc_application PUBLIC cxx_std_20)
target_link_libraries(
  hvc_application
  PUBLIC
    hvc::domain
  PRIVATE
    "$<BUILD_INTERFACE:hvc_project_options>"
)

set_target_properties(
  hvc_application
  PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME application
    OUTPUT_NAME hvc-application
)

install(
  TARGETS hvc_application
  EXPORT HvcFoundationTargets
  FILE_SET public_headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
