set(
  HVC_WINDOWS_APP_SDK_VERSION
  "2.2.0"
  CACHE STRING
  "Pinned Microsoft.WindowsAppSDK NuGet package version"
)
set(
  HVC_CPPWINRT_VERSION
  "3.0.260715.1"
  CACHE STRING
  "Pinned Microsoft.Windows.CppWinRT NuGet package version"
)

add_executable(
  hvc_windows_client
  WIN32
  resources/resource.h
  resources/windows-client.rc
  src/client_session.cpp
  src/client_session.hpp
  src/localized_text.cpp
  src/localized_text.hpp
  src/login_dialog.cpp
  src/login_dialog.hpp
  src/main.cpp
)

target_compile_features(hvc_windows_client PRIVATE cxx_std_20)
target_include_directories(
  hvc_windows_client
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
)
target_compile_options(
  hvc_windows_client
  PRIVATE /W4 /WX /permissive- /Zc:preprocessor /utf-8 /EHsc /wd4265
)
target_link_libraries(
  hvc_windows_client
  PRIVATE
    hvc::client
    hvc::client_win_raw_input
    hvc::client_winhttp
    hvc::livekit_transport
    ole32
    shell32
)

set_target_properties(
  hvc_windows_client
  PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME hvc-windows-client
    VS_GLOBAL_AppContainerApplication false
    VS_GLOBAL_AppxPackage false
    VS_GLOBAL_CppWinRTOptimized true
    VS_GLOBAL_CppWinRTRootNamespaceAutoMerge true
    VS_GLOBAL_NuGetTargetMoniker "native,Version=v0.0"
    VS_GLOBAL_TargetFramework native
    VS_GLOBAL_TargetFrameworkIdentifier native
    VS_GLOBAL_TargetFrameworkMoniker "native,Version=v0.0"
    VS_GLOBAL_TargetFrameworkVersion v0.0
    VS_GLOBAL_UseWinUI true
    VS_GLOBAL_WebView2UseWinRT true
    VS_GLOBAL_WindowsAppSDKSelfContained true
    VS_GLOBAL_WindowsPackageType None
    VS_PACKAGE_REFERENCES
      "Microsoft.WindowsAppSDK_${HVC_WINDOWS_APP_SDK_VERSION};Microsoft.Windows.CppWinRT_${HVC_CPPWINRT_VERSION}"
)

add_custom_command(
  TARGET hvc_windows_client
  POST_BUILD
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
    "${HVC_LIVEKIT_SDK_ROOT}/bin/livekit.dll"
    "$<TARGET_FILE_DIR:hvc_windows_client>"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
    "${HVC_LIVEKIT_SDK_ROOT}/bin/livekit_ffi.dll"
    "$<TARGET_FILE_DIR:hvc_windows_client>"
  VERBATIM
)
