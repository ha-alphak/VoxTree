add_executable(hvc_livekit_quality_gate src/main.cpp)

target_compile_features(hvc_livekit_quality_gate PRIVATE cxx_std_20)
target_compile_options(
  hvc_livekit_quality_gate
  PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>"
)
target_link_libraries(
  hvc_livekit_quality_gate
  PRIVATE
    LiveKit::livekit
    hvc::livekit_transport
    hvc::project_options
)
set_target_properties(
  hvc_livekit_quality_gate
  PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME hvc-livekit-quality-gate
)

if(WIN32)
  add_custom_command(
    TARGET hvc_livekit_quality_gate
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different
      "${HVC_LIVEKIT_SDK_ROOT}/bin/livekit.dll"
      "$<TARGET_FILE_DIR:hvc_livekit_quality_gate>"
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different
      "${HVC_LIVEKIT_SDK_ROOT}/bin/livekit_ffi.dll"
      "$<TARGET_FILE_DIR:hvc_livekit_quality_gate>"
    VERBATIM
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set_target_properties(
    hvc_livekit_quality_gate
    PROPERTIES
      BUILD_RPATH "${HVC_LIVEKIT_SDK_ROOT}/lib"
      INSTALL_RPATH "$ORIGIN/../lib"
  )
endif()
