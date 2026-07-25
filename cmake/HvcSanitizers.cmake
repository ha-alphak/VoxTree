include_guard(GLOBAL)

function(hvc_enable_sanitizers target enable_sanitizers)
  if(NOT enable_sanitizers)
    return()
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(
      "${target}"
      INTERFACE
        -fno-omit-frame-pointer
        -fsanitize=address,undefined
    )
    target_link_options(
      "${target}"
      INTERFACE
        -fno-omit-frame-pointer
        -fsanitize=address,undefined
    )
  else()
    message(
      WARNING
      "HVC_ENABLE_SANITIZERS is enabled, but no sanitizer profile is configured "
      "for compiler ${CMAKE_CXX_COMPILER_ID}."
    )
  endif()
endfunction()
