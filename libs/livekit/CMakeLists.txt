add_library(hvc_livekit STATIC)
add_library(hvc::livekit ALIAS hvc_livekit)

target_sources(
  hvc_livekit
  PRIVATE src/livekit_token.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES include/hvc/livekit/livekit_token.hpp
)

target_compile_features(hvc_livekit PUBLIC cxx_std_20)
target_link_libraries(
  hvc_livekit
  PUBLIC hvc::application
  PRIVATE "$<BUILD_INTERFACE:hvc_project_options>"
)
set_target_properties(
  hvc_livekit
  PROPERTIES CXX_EXTENSIONS OFF EXPORT_NAME livekit OUTPUT_NAME hvc-livekit
)

install(
  TARGETS hvc_livekit
  EXPORT HvcFoundationTargets
  FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

if(TARGET LiveKit::livekit)
  if(WIN32)
    set(_hvc_audio_playout_source src/xaudio2_audio_playout.cpp)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_path(
      HVC_PIPEWIRE_INCLUDE_DIR
      pipewire/pipewire.h
      PATH_SUFFIXES pipewire-0.3
      REQUIRED
    )
    find_path(
      HVC_SPA_INCLUDE_DIR
      spa/param/audio/format-utils.h
      PATH_SUFFIXES spa-0.2
      REQUIRED
    )
    find_library(HVC_PIPEWIRE_LIBRARY NAMES pipewire-0.3 REQUIRED)
    add_library(hvc_pipewire UNKNOWN IMPORTED)
    set_target_properties(
      hvc_pipewire
      PROPERTIES
        IMPORTED_LOCATION "${HVC_PIPEWIRE_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES
          "${HVC_PIPEWIRE_INCLUDE_DIR};${HVC_SPA_INCLUDE_DIR}"
    )
    set(_hvc_audio_playout_source src/pipewire_audio_playout.cpp)
  else()
    message(FATAL_ERROR "The native LiveKit transport has no audio backend for this platform.")
  endif()

  add_library(hvc_livekit_transport STATIC)
  add_library(hvc::livekit_transport ALIAS hvc_livekit_transport)

  target_sources(
    hvc_livekit_transport
    PRIVATE
      src/audio_playout.hpp
      src/livekit_voice_transport.cpp
      "${_hvc_audio_playout_source}"
    PUBLIC
      FILE_SET public_headers
      TYPE HEADERS
      BASE_DIRS include
      FILES include/hvc/livekit/livekit_voice_transport.hpp
  )

  target_compile_features(hvc_livekit_transport PUBLIC cxx_std_20)
  target_compile_options(
    hvc_livekit_transport
    PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>"
  )
  target_link_libraries(
    hvc_livekit_transport
    PUBLIC hvc::client
    PRIVATE
      LiveKit::livekit
      "$<BUILD_INTERFACE:hvc_project_options>"
  )
  if(WIN32)
    target_link_libraries(hvc_livekit_transport PRIVATE xaudio2)
  else()
    target_link_libraries(hvc_livekit_transport PRIVATE hvc_pipewire)
  endif()
  set_target_properties(
    hvc_livekit_transport
    PROPERTIES
      CXX_EXTENSIONS OFF
      EXPORT_NAME livekit_transport
      OUTPUT_NAME hvc-livekit-transport
  )
endif()
