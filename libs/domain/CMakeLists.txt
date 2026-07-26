add_library(hvc_domain STATIC)
add_library(hvc::domain ALIAS hvc_domain)

target_sources(
  hvc_domain
  PRIVATE
    src/model.cpp
    src/routing.cpp
    src/state_machine.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
      include/hvc/domain/id.hpp
      include/hvc/domain/model.hpp
      include/hvc/domain/routing.hpp
      include/hvc/domain/state_machine.hpp
)

target_include_directories(
  hvc_domain
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_compile_features(hvc_domain PUBLIC cxx_std_20)
target_link_libraries(
  hvc_domain
  PRIVATE
    "$<BUILD_INTERFACE:hvc_project_options>"
)

set_target_properties(
  hvc_domain
  PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME domain
    OUTPUT_NAME hvc-domain
    POSITION_INDEPENDENT_CODE ON
)

install(
  TARGETS hvc_domain
  EXPORT HvcFoundationTargets
  FILE_SET public_headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
