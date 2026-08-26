function(fatty_resolve_git_version OUT_VERSION OUT_TUPLE)
  set(_ver "")
  execute_process(
    COMMAND git rev-parse --is-inside-work-tree
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _inside
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(_inside STREQUAL "true")
    execute_process(
      COMMAND git status --porcelain
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE _dirty
      ERROR_QUIET
    )
    execute_process(
      COMMAND git tag --list "v[0-9]*" --sort=-version:refname
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE _tags
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(_chosen "")
    if(_tags)
      string(REPLACE "\n" ";" _tag_list "${_tags}")
      foreach(_tag IN LISTS _tag_list)
        execute_process(
          COMMAND git merge-base --is-ancestor "${_tag}" HEAD
          WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
          RESULT_VARIABLE _anc
          ERROR_QUIET
        )
        if(_anc EQUAL 0)
          set(_chosen "${_tag}")
          break()
        endif()
      endforeach()
    endif()
    execute_process(
      COMMAND git rev-parse --short HEAD
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE _short
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_chosen STREQUAL "")
      if(_short)
        set(_ver "0.0.0-g${_short}")
      endif()
    else()
      string(REGEX REPLACE "^v" "" _norm "${_chosen}")
      execute_process(
        COMMAND git rev-list --count "${_chosen}..HEAD"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _count
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      if(_count STREQUAL "0" OR _count STREQUAL "")
        set(_ver "${_norm}")
      else()
        set(_ver "${_norm}-${_count}-g${_short}")
      endif()
    endif()
    if(_dirty AND NOT _ver STREQUAL "")
      set(_ver "${_ver}-dirty")
    endif()
  endif()
  if(_ver STREQUAL "")
    set(_ver "0.0.0-dev")
  endif()
  set(_maj 0)
  set(_min 0)
  set(_pat 0)
  set(_rev 0)
  if(_ver MATCHES "^v?([0-9]+)(\\.([0-9]+))?(\\.([0-9]+))?(-([0-9]+)-g)?")
    set(_maj "${CMAKE_MATCH_1}")
    if(CMAKE_MATCH_3)
      set(_min "${CMAKE_MATCH_3}")
    endif()
    if(CMAKE_MATCH_5)
      set(_pat "${CMAKE_MATCH_5}")
    endif()
    if(CMAKE_MATCH_7)
      set(_rev "${CMAKE_MATCH_7}")
    endif()
  endif()
  set(${OUT_VERSION} "${_ver}" PARENT_SCOPE)
  set(${OUT_TUPLE} "${_maj},${_min},${_pat},${_rev}" PARENT_SCOPE)
endfunction()
