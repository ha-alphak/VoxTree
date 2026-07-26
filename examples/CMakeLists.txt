add_executable(hvc_client_core_example client-core/basic_integration.cpp)

target_link_libraries(
  hvc_client_core_example
  PRIVATE
    hvc::client_core
    hvc::project_options
)

set_target_properties(hvc_client_core_example PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
  add_custom_command(
    TARGET hvc_client_core_example
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE:hvc_client_core>"
      "$<TARGET_FILE_DIR:hvc_client_core_example>"
    VERBATIM
  )
endif()
