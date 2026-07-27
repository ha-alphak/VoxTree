include_guard(GLOBAL)

set(HVC_LIVEKIT_SDK_VERSION "1.4.0" CACHE STRING "Pinned LiveKit C++ SDK version")
set(
  HVC_LIVEKIT_SDK_SHA256_WINDOWS_X64
  "66a1325b35e21c6501ee32f985ba57cf821e0c2aa533a6dff1577c95ff0ccf38"
  CACHE STRING
  "SHA-256 of the pinned LiveKit Windows x64 SDK archive"
)
set(
  HVC_LIVEKIT_SDK_SHA256_LINUX_X64
  "7b4b0e5e86b597e0ff108dd36559d57879d46021b0190943f409dfcd58b80bb7"
  CACHE STRING
  "SHA-256 of the pinned LiveKit Linux x64 SDK archive"
)
set(
  HVC_LIVEKIT_SDK_ROOT
  ""
  CACHE PATH
  "Existing extracted LiveKit SDK root; empty downloads the pinned release"
)

function(hvc_find_livekit_sdk)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(
      FATAL_ERROR
      "The LiveKit quality gate supports x64 builds only."
    )
  endif()

  if(WIN32)
    set(_platform "windows")
    set(_archive_extension "zip")
    set(_archive_hash "${HVC_LIVEKIT_SDK_SHA256_WINDOWS_X64}")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_platform "linux")
    set(_archive_extension "tar.gz")
    set(_archive_hash "${HVC_LIVEKIT_SDK_SHA256_LINUX_X64}")
  else()
    message(
      FATAL_ERROR
      "The LiveKit quality gate supports Windows x64 and Linux x64 only."
    )
  endif()

  if(HVC_LIVEKIT_SDK_ROOT)
    set(_sdk_root "${HVC_LIVEKIT_SDK_ROOT}")
  else()
    set(_archive_name
        "livekit-sdk-${_platform}-x64-${HVC_LIVEKIT_SDK_VERSION}.${_archive_extension}")
    set(_download_dir "${CMAKE_BINARY_DIR}/_downloads")
    set(_sdk_dir "${CMAKE_BINARY_DIR}/_deps/livekit-sdk")
    set(
      _sdk_root
      "${_sdk_dir}/livekit-sdk-${_platform}-x64-${HVC_LIVEKIT_SDK_VERSION}"
    )
    set(_archive "${_download_dir}/${_archive_name}")
    set(
      _url
      "https://github.com/livekit/client-sdk-cpp/releases/download/v${HVC_LIVEKIT_SDK_VERSION}/${_archive_name}"
    )

    if(NOT EXISTS "${_sdk_root}/lib/cmake/LiveKit")
      file(MAKE_DIRECTORY "${_download_dir}" "${_sdk_dir}")
      message(STATUS "Downloading pinned LiveKit C++ SDK ${HVC_LIVEKIT_SDK_VERSION}")
      file(
        DOWNLOAD "${_url}" "${_archive}"
        EXPECTED_HASH "SHA256=${_archive_hash}"
        TLS_VERIFY ON
        SHOW_PROGRESS
        STATUS _download_status
      )
      list(GET _download_status 0 _download_code)
      list(GET _download_status 1 _download_message)
      if(NOT _download_code EQUAL 0)
        message(
          FATAL_ERROR
          "LiveKit SDK download failed: ${_download_message}"
        )
      endif()
      file(REMOVE_RECURSE "${_sdk_root}")
      file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_sdk_dir}")
    endif()
  endif()

  if(NOT EXISTS "${_sdk_root}/lib/cmake/LiveKit")
    message(
      FATAL_ERROR
      "HVC_LIVEKIT_SDK_ROOT is not an extracted LiveKit SDK: ${_sdk_root}"
    )
  endif()

  find_package(
    LiveKit ${HVC_LIVEKIT_SDK_VERSION}
    CONFIG REQUIRED
    PATHS "${_sdk_root}/lib/cmake/LiveKit"
    NO_DEFAULT_PATH
  )
  set(HVC_LIVEKIT_SDK_ROOT "${_sdk_root}" CACHE PATH "" FORCE)
  set(HVC_LIVEKIT_SDK_ROOT "${_sdk_root}" PARENT_SCOPE)
endfunction()
