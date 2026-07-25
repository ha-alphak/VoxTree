include_guard(GLOBAL)

function(hvc_set_project_warnings target warnings_as_errors)
  if(MSVC)
    set(
      warning_options
      /W4
      /permissive-
      /Zc:__cplusplus
      /utf-8
      /w14242
      /w14254
      /w14263
      /w14265
      /w14287
      /w14296
      /w14311
      /w14545
      /w14546
      /w14547
      /w14549
      /w14555
      /w14619
      /w14640
      /w14826
      /w14905
      /w14906
      /w14928
    )

    if(warnings_as_errors)
      list(APPEND warning_options /WX)
    endif()
  else()
    set(
      warning_options
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wshadow
      -Wsign-conversion
    )

    if(warnings_as_errors)
      list(APPEND warning_options -Werror)
    endif()
  endif()

  target_compile_options("${target}" INTERFACE ${warning_options})
endfunction()
