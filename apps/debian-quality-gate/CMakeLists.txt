find_package(Qt6 6.8 REQUIRED COMPONENTS Core DBus LinguistTools Widgets)

qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES de)
qt_add_executable(hvc_debian_quality_gate src/main.cpp)
qt_add_translations(
  hvc_debian_quality_gate
  TS_FILES translations/hvc_de.ts
  RESOURCE_PREFIX "/i18n"
)

target_compile_features(hvc_debian_quality_gate PRIVATE cxx_std_20)
target_link_libraries(
  hvc_debian_quality_gate
  PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Widgets
    hvc::client_linux_evdev_input
    hvc::livekit_transport
    hvc::project_options
)
set_target_properties(
  hvc_debian_quality_gate
  PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME hvc-debian-quality-gate
    BUILD_RPATH "${HVC_LIVEKIT_SDK_ROOT}/lib"
    INSTALL_RPATH "$ORIGIN/../lib"
)
