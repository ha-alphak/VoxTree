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
