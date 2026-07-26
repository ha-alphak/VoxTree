add_executable(hvc_load_driver src/main.cpp)

target_compile_features(hvc_load_driver PRIVATE cxx_std_20)
target_link_libraries(
  hvc_load_driver
  PRIVATE
    hvc::persistence
    hvc::project_options
)
set_target_properties(
  hvc_load_driver
  PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME hvc-load-driver
)

install(TARGETS hvc_load_driver RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
