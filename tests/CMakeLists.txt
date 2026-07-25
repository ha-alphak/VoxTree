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

add_executable(hvc_application_tests application/control_plane_tests.cpp)

target_link_libraries(
  hvc_application_tests
  PRIVATE
    hvc::application
    hvc::project_options
)

set_target_properties(hvc_application_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME application.control_plane COMMAND hvc_application_tests)
