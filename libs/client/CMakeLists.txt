add_library(hvc_client STATIC)
add_library(hvc::client ALIAS hvc_client)

target_sources(
  hvc_client
  PRIVATE
    src/voice_client.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
      include/hvc/client/voice_client.hpp
      include/hvc/client/voice_transport.hpp
)

target_include_directories(
  hvc_client
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_compile_features(hvc_client PUBLIC cxx_std_20)
target_link_libraries(
  hvc_client
  PUBLIC
    hvc::domain
  PRIVATE
    "$<BUILD_INTERFACE:hvc_project_options>"
)

set_target_properties(
  hvc_client
  PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME client
    OUTPUT_NAME hvc-client
)

install(
  TARGETS hvc_client
  EXPORT HvcFoundationTargets
  FILE_SET public_headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
