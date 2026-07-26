add_executable(hvc_control_plane src/main.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(Threads REQUIRED)
  target_sources(
    hvc_control_plane
    PRIVATE
      src/linux_http_server.cpp
      src/linux_http_server.hpp
      src/livekit_room_service_client.cpp
      src/livekit_room_service_client.hpp
  )
  target_compile_definitions(hvc_control_plane PRIVATE HVC_HAS_LINUX_HTTP_SERVER=1)
  target_link_libraries(hvc_control_plane PRIVATE Threads::Threads)
endif()

target_compile_features(hvc_control_plane PRIVATE cxx_std_20)
target_link_libraries(
  hvc_control_plane
  PRIVATE
    hvc::application
    hvc::livekit
    hvc::network
    hvc::persistence
    hvc::project_options
)

set_target_properties(
  hvc_control_plane
  PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME hvc-control-plane
)

install(TARGETS hvc_control_plane RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
