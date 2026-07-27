add_executable(hvc_foundation_tests foundation/version_tests.cpp)

target_link_libraries(
  hvc_foundation_tests
  PRIVATE
    hvc::foundation
    hvc::project_options
)

set_target_properties(hvc_foundation_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME foundation.version COMMAND hvc_foundation_tests)

add_executable(hvc_domain_tests domain/domain_tests.cpp)

target_link_libraries(
  hvc_domain_tests
  PRIVATE
    hvc::domain
    hvc::project_options
)

set_target_properties(hvc_domain_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME domain.routing COMMAND hvc_domain_tests)

add_executable(hvc_domain_state_machine_tests domain/state_machine_tests.cpp)

target_link_libraries(
  hvc_domain_state_machine_tests
  PRIVATE
    hvc::domain
    hvc::project_options
)

set_target_properties(hvc_domain_state_machine_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME domain.state_machine COMMAND hvc_domain_state_machine_tests)

add_executable(hvc_client_tests client/voice_client_tests.cpp)

target_link_libraries(
  hvc_client_tests
  PRIVATE
    hvc::client
    hvc::project_options
)

set_target_properties(hvc_client_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME client.voice_client COMMAND hvc_client_tests)

add_executable(
  hvc_client_control_plane_tests
  client/control_plane_client_tests.cpp
)

target_link_libraries(
  hvc_client_control_plane_tests
  PRIVATE
    hvc::client
    hvc::project_options
)

set_target_properties(hvc_client_control_plane_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME client.control_plane COMMAND hvc_client_control_plane_tests)

add_executable(hvc_client_ptt_input_tests client/ptt_input_tests.cpp)

target_link_libraries(
  hvc_client_ptt_input_tests
  PRIVATE
    hvc::client
    hvc::project_options
)

set_target_properties(hvc_client_ptt_input_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME client.ptt_input COMMAND hvc_client_ptt_input_tests)

add_executable(
  hvc_presentation_tests
  presentation/desktop_model_tests.cpp
)

target_link_libraries(
  hvc_presentation_tests
  PRIVATE
    hvc::presentation
    hvc::project_options
)

set_target_properties(hvc_presentation_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME presentation.desktop_model COMMAND hvc_presentation_tests)

add_executable(hvc_client_core_tests client/client_core_tests.cpp)

target_link_libraries(
  hvc_client_core_tests
  PRIVATE
    hvc::client_core
    hvc::project_options
)

set_target_properties(hvc_client_core_tests PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
  add_custom_command(
    TARGET hvc_client_core_tests
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE:hvc_client_core>"
      "$<TARGET_FILE_DIR:hvc_client_core_tests>"
    VERBATIM
  )
endif()

add_test(NAME client.client_core_abi COMMAND hvc_client_core_tests)

if(MSVC)
  add_custom_target(
    hvc_client_core_c_header_check ALL
    COMMAND
      "${CMAKE_CXX_COMPILER}" /nologo /TC /Zs
      "/I${PROJECT_SOURCE_DIR}/libs/client/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/client/client_core_c_abi_tests.c"
    COMMENT "Checking the client-core ABI header with the C compiler"
    VERBATIM
  )
else()
  add_custom_target(
    hvc_client_core_c_header_check ALL
    COMMAND
      "${CMAKE_CXX_COMPILER}" -x c -std=c11 -fsyntax-only
      "-I${PROJECT_SOURCE_DIR}/libs/client/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/client/client_core_c_abi_tests.c"
    COMMENT "Checking the client-core ABI header with the C compiler"
    VERBATIM
  )
endif()

if(WIN32)
  add_executable(hvc_client_win_raw_input_tests client/win_raw_input_tests.cpp)

  target_link_libraries(
    hvc_client_win_raw_input_tests
    PRIVATE
      hvc::client_win_raw_input
      hvc::project_options
  )

  set_target_properties(hvc_client_win_raw_input_tests PROPERTIES CXX_EXTENSIONS OFF)

  add_test(NAME client.win_raw_input COMMAND hvc_client_win_raw_input_tests)
endif()

add_executable(hvc_application_tests application/control_plane_tests.cpp)

target_link_libraries(
  hvc_application_tests
  PRIVATE
    hvc::application
    hvc::project_options
)

set_target_properties(hvc_application_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME application.control_plane COMMAND hvc_application_tests)

add_executable(hvc_application_identity_tests application/identity_tests.cpp)

target_link_libraries(
  hvc_application_identity_tests
  PRIVATE
    hvc::application
    hvc::project_options
)

set_target_properties(hvc_application_identity_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME application.identity COMMAND hvc_application_identity_tests)

add_executable(
  hvc_application_in_memory_tests
  application/in_memory_control_plane_tests.cpp
)

target_link_libraries(
  hvc_application_in_memory_tests
  PRIVATE
    hvc::application
    hvc::project_options
)

set_target_properties(hvc_application_in_memory_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME application.in_memory_control_plane COMMAND hvc_application_in_memory_tests)

add_executable(
  hvc_persistence_tests
  persistence/sqlite_session_repository_tests.cpp
)

target_link_libraries(
  hvc_persistence_tests
  PRIVATE
    hvc::persistence
    hvc::project_options
)

set_target_properties(hvc_persistence_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME persistence.sqlite_control_plane COMMAND hvc_persistence_tests)

add_executable(
  hvc_network_tests
  network/control_plane_http_tests.cpp
)

target_link_libraries(
  hvc_network_tests
  PRIVATE
    hvc::network
    hvc::project_options
)

set_target_properties(hvc_network_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME network.control_plane_http COMMAND hvc_network_tests)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME load_driver.help COMMAND hvc_load_driver --help)

  find_package(Threads REQUIRED)
  add_executable(hvc_client_linux_evdev_input_tests client/linux_evdev_input_tests.cpp)
  target_link_libraries(
    hvc_client_linux_evdev_input_tests
    PRIVATE
      hvc::client_linux_evdev_input
      hvc::project_options
  )
  set_target_properties(
    hvc_client_linux_evdev_input_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  add_test(NAME client.linux_evdev_input COMMAND hvc_client_linux_evdev_input_tests)

  add_executable(
    hvc_linux_http_server_tests
    control_plane/linux_http_server_tests.cpp
    "${PROJECT_SOURCE_DIR}/apps/control-plane/src/linux_http_server.cpp"
  )
  target_include_directories(
    hvc_linux_http_server_tests
    PRIVATE "${PROJECT_SOURCE_DIR}/apps/control-plane/src"
  )
  target_link_libraries(
    hvc_linux_http_server_tests
    PRIVATE
      hvc::network
      hvc::project_options
      Threads::Threads
  )
  set_target_properties(hvc_linux_http_server_tests PROPERTIES CXX_EXTENSIONS OFF)
  add_test(NAME control_plane.linux_http_server COMMAND hvc_linux_http_server_tests)
endif()

add_executable(hvc_livekit_tests livekit/livekit_token_tests.cpp)
target_link_libraries(
  hvc_livekit_tests
  PRIVATE hvc::livekit hvc::project_options
)
set_target_properties(hvc_livekit_tests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME livekit.token COMMAND hvc_livekit_tests)
