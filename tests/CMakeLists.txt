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

add_executable(hvc_livekit_tests livekit/livekit_token_tests.cpp)
target_link_libraries(
  hvc_livekit_tests
  PRIVATE hvc::livekit hvc::project_options
)
set_target_properties(hvc_livekit_tests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME livekit.token COMMAND hvc_livekit_tests)
