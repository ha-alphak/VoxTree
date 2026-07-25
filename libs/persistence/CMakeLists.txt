if(WIN32)
  set(hvc_sqlite_library winsqlite3)
else()
  find_package(SQLite3 REQUIRED)
  set(hvc_sqlite_library SQLite::SQLite3)
endif()

add_library(hvc_persistence STATIC)
add_library(hvc::persistence ALIAS hvc_persistence)

target_sources(
  hvc_persistence
  PRIVATE
    src/sqlite_session_repository.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
      include/hvc/persistence/sqlite_control_plane_repository.hpp
      include/hvc/persistence/sqlite_session_repository.hpp
)

target_include_directories(
  hvc_persistence
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_compile_features(hvc_persistence PUBLIC cxx_std_20)
target_link_libraries(
  hvc_persistence
  PUBLIC
    hvc::application
  PRIVATE
    ${hvc_sqlite_library}
    "$<BUILD_INTERFACE:hvc_project_options>"
)

set_target_properties(
  hvc_persistence
  PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME persistence
    OUTPUT_NAME hvc-persistence
)

install(
  TARGETS hvc_persistence
  EXPORT HvcFoundationTargets
  FILE_SET public_headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
