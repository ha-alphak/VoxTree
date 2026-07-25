add_library(hvc_livekit STATIC)
add_library(hvc::livekit ALIAS hvc_livekit)

target_sources(
  hvc_livekit
  PRIVATE src/livekit_token.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES include/hvc/livekit/livekit_token.hpp
)

target_compile_features(hvc_livekit PUBLIC cxx_std_20)
target_link_libraries(
  hvc_livekit
  PUBLIC hvc::application
  PRIVATE "$<BUILD_INTERFACE:hvc_project_options>"
)
set_target_properties(
  hvc_livekit
  PROPERTIES CXX_EXTENSIONS OFF EXPORT_NAME livekit OUTPUT_NAME hvc-livekit
)

install(
  TARGETS hvc_livekit
  EXPORT HvcFoundationTargets
  FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
