add_library(hvc_presentation STATIC)
add_library(hvc::presentation ALIAS hvc_presentation)

target_sources(
  hvc_presentation
  PRIVATE
    src/desktop_model.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
      include/hvc/presentation/desktop_model.hpp
)

target_include_directories(
  hvc_presentation
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_compile_features(hvc_presentation PUBLIC cxx_std_20)
target_compile_options(hvc_presentation PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
target_link_libraries(
  hvc_presentation
  PUBLIC
    hvc::client
  PRIVATE
    "$<BUILD_INTERFACE:hvc_project_options>"
)

set_target_properties(
  hvc_presentation
  PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME presentation
    OUTPUT_NAME hvc-presentation
    POSITION_INDEPENDENT_CODE ON
)

install(
  TARGETS hvc_presentation
  EXPORT HvcFoundationTargets
  FILE_SET public_headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
