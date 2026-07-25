add_executable(hvc_control_plane src/main.cpp)

target_compile_features(hvc_control_plane PRIVATE cxx_std_20)
target_link_libraries(
  hvc_control_plane
  PRIVATE
    hvc::application
    hvc::project_options
)

set_target_properties(
  hvc_control_plane
  PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME hvc-control-plane
)

install(TARGETS hvc_control_plane RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
