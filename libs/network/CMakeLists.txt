add_library(hvc_network STATIC)
add_library(hvc::network ALIAS hvc_network)

target_sources(
  hvc_network
  PRIVATE
    src/control_plane_http.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
      include/hvc/network/control_plane_http.hpp
)

target_include_directories(
  hvc_network
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_compile_features(hvc_network PUBLIC cxx_std_20)
target_link_libraries(
  hvc_network
  PUBLIC
    hvc::application
  PRIVATE
    "$<BUILD_INTERFACE:hvc_project_options>"
)

set_target_properties(
  hvc_network
  PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME network
    OUTPUT_NAME hvc-network
)

install(
  TARGETS hvc_network
  EXPORT HvcFoundationTargets
  FILE_SET public_headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
