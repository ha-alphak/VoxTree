add_library(hvc_client STATIC)
add_library(hvc::client ALIAS hvc_client)

target_sources(
  hvc_client
  PRIVATE
    src/authorized_voice_client.cpp
    src/control_plane_client.cpp
    src/ptt_input.cpp
    src/voice_client.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
      include/hvc/client/authorized_voice_client.hpp
      include/hvc/client/control_plane_client.hpp
      include/hvc/client/ptt_input.hpp
      include/hvc/client/voice_client.hpp
      include/hvc/client/voice_transport.hpp
)

target_include_directories(
  hvc_client
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_compile_features(hvc_client PUBLIC cxx_std_20)
target_compile_options(hvc_client PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
target_link_libraries(
  hvc_client
  PUBLIC
    hvc::domain
  PRIVATE
    "$<BUILD_INTERFACE:hvc_project_options>"
)

set_target_properties(
  hvc_client
  PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME client
    OUTPUT_NAME hvc-client
    POSITION_INDEPENDENT_CODE ON
)

install(
  TARGETS hvc_client
  EXPORT HvcFoundationTargets
  FILE_SET public_headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

add_library(hvc_client_core SHARED)
add_library(hvc::client_core ALIAS hvc_client_core)

target_sources(
  hvc_client_core
  PRIVATE
    src/client_core.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
      include/hvc/client_core.h
      include/hvc/client_core.hpp
)

target_compile_features(hvc_client_core PUBLIC cxx_std_20)
target_compile_definitions(hvc_client_core PRIVATE HVC_CLIENT_CORE_EXPORTS)
target_compile_options(hvc_client_core PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
target_link_libraries(
  hvc_client_core
  PRIVATE
    hvc::client
    "$<BUILD_INTERFACE:hvc_project_options>"
)

set_target_properties(
  hvc_client_core
  PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    EXPORT_NAME client_core
    OUTPUT_NAME hvc-client-core
    VISIBILITY_INLINES_HIDDEN YES
)

install(
  TARGETS hvc_client_core
  EXPORT HvcFoundationTargets
  FILE_SET public_headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

if(WIN32)
  add_library(hvc_client_win_raw_input STATIC)
  add_library(hvc::client_win_raw_input ALIAS hvc_client_win_raw_input)

  target_sources(
    hvc_client_win_raw_input
    PRIVATE src/win_raw_input.cpp
    PUBLIC
      FILE_SET public_headers
      TYPE HEADERS
      BASE_DIRS include
      FILES include/hvc/client/win_raw_input.hpp
  )
  target_compile_features(hvc_client_win_raw_input PUBLIC cxx_std_20)
  target_compile_options(
    hvc_client_win_raw_input
    PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>"
  )
  target_link_libraries(
    hvc_client_win_raw_input
    PUBLIC hvc::client
    PRIVATE
      hid
      user32
      "$<BUILD_INTERFACE:hvc_project_options>"
  )
  set_target_properties(
    hvc_client_win_raw_input
    PROPERTIES
      CXX_EXTENSIONS OFF
      EXPORT_NAME client_win_raw_input
      OUTPUT_NAME hvc-client-win-raw-input
  )

  install(
    TARGETS hvc_client_win_raw_input
    EXPORT HvcFoundationTargets
    FILE_SET public_headers
      DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    ARCHIVE
      DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  )

  add_library(hvc_client_winhttp STATIC)
  add_library(hvc::client_winhttp ALIAS hvc_client_winhttp)

  target_sources(
    hvc_client_winhttp
    PRIVATE src/win_http_transport.cpp
    PUBLIC
      FILE_SET public_headers
      TYPE HEADERS
      BASE_DIRS include
      FILES include/hvc/client/win_http_transport.hpp
  )
  target_compile_features(hvc_client_winhttp PUBLIC cxx_std_20)
  target_compile_options(
    hvc_client_winhttp
    PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>"
  )
  target_link_libraries(
    hvc_client_winhttp
    PUBLIC hvc::client
    PRIVATE
      winhttp
      "$<BUILD_INTERFACE:hvc_project_options>"
  )
  set_target_properties(
    hvc_client_winhttp
    PROPERTIES
      CXX_EXTENSIONS OFF
      EXPORT_NAME client_winhttp
      OUTPUT_NAME hvc-client-winhttp
  )

  install(
    TARGETS hvc_client_winhttp
    EXPORT HvcFoundationTargets
    FILE_SET public_headers
      DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    ARCHIVE
      DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  )
endif()
