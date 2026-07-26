if(NOT DEFINED RESOURCE_FILE)
  message(FATAL_ERROR "RESOURCE_FILE is required")
endif()

file(STRINGS "${RESOURCE_FILE}" resource_lines ENCODING UTF-8)

set(current_language "")
set(in_string_table FALSE)
set(english_ids "")
set(german_ids "")

foreach(line IN LISTS resource_lines)
  if(line MATCHES "^[ \t]*LANGUAGE[ \t]+LANG_ENGLISH")
    set(current_language "english")
    set(in_string_table FALSE)
  elseif(line MATCHES "^[ \t]*LANGUAGE[ \t]+LANG_GERMAN")
    set(current_language "german")
    set(in_string_table FALSE)
  elseif(line MATCHES "^[ \t]*STRINGTABLE[ \t]*$")
    set(in_string_table TRUE)
  elseif(in_string_table AND line MATCHES "^[ \t]*END[ \t]*$")
    set(in_string_table FALSE)
  elseif(in_string_table AND line MATCHES "^[ \t]*(IDS_[A-Z0-9_]+)[ \t]+\"")
    set(resource_id "${CMAKE_MATCH_1}")
    if(current_language STREQUAL "english")
      list(APPEND english_ids "${resource_id}")
    elseif(current_language STREQUAL "german")
      list(APPEND german_ids "${resource_id}")
    endif()
  endif()
endforeach()

list(SORT english_ids)
list(SORT german_ids)

if(NOT english_ids STREQUAL german_ids)
  set(english_only "${english_ids}")
  set(german_only "${german_ids}")
  list(REMOVE_ITEM english_only ${german_ids})
  list(REMOVE_ITEM german_only ${english_ids})
  message(
    FATAL_ERROR
    "Windows client resource parity failed. English-only: ${english_only}; "
    "German-only: ${german_only}"
  )
endif()

if(NOT english_ids)
  message(FATAL_ERROR "Windows client resource parity found no string resources")
endif()
