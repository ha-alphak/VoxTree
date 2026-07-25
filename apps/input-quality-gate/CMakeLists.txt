add_executable(hvc_input_quality_gate src/main.cpp)

target_compile_features(hvc_input_quality_gate PRIVATE cxx_std_20)
target_link_libraries(
  hvc_input_quality_gate
  PRIVATE
    hvc::client_win_raw_input
    hvc::project_options
    user32
)

set_target_properties(
  hvc_input_quality_gate
  PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME hvc-input-quality-gate
)
