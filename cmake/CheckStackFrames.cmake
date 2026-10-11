# CheckStackFrames.cmake
#
# Builds an object target compiled with -fstack-usage, then fails if any frame
# in its .su files exceeds MAX_BYTES. Run as a standalone script:
#
#   cmake -D BUILD_DIR=<build> -D TARGET=<target> -D SU_DIR=<dir> \
#         -D MAX_BYTES=<n> [-D CONFIG=<cfg>] -P cmake/CheckStackFrames.cmake
#
# Finding no .su file is a failure: a check that measured nothing would
# otherwise pass.

foreach(_var IN ITEMS BUILD_DIR TARGET SU_DIR MAX_BYTES)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "CheckStackFrames: ${_var} must be defined")
  endif()
endforeach()

set(_config_args "")
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
  set(_config_args --config ${CONFIG})
endif()
execute_process(
  COMMAND ${CMAKE_COMMAND} --build ${BUILD_DIR} --target ${TARGET} ${_config_args}
  RESULT_VARIABLE _build_result)
if(NOT _build_result EQUAL 0)
  message(FATAL_ERROR "CheckStackFrames: building ${TARGET} failed")
endif()

file(GLOB_RECURSE _su_files "${SU_DIR}/*.su")
if(NOT _su_files)
  message(FATAL_ERROR "CheckStackFrames: no .su file under ${SU_DIR} -- was -fstack-usage dropped?")
endif()

set(_frames 0)
set(_over "")
foreach(_su IN LISTS _su_files)
  file(STRINGS "${_su}" _lines)
  foreach(_line IN LISTS _lines)
    # file:line:column:function<TAB>bytes<TAB>qualifier
    if(_line MATCHES "^(.*)\t([0-9]+)\t([a-z,]+)$")
      set(_where "${CMAKE_MATCH_1}")
      set(_bytes "${CMAKE_MATCH_2}")
      set(_qualifier "${CMAKE_MATCH_3}")
      math(EXPR _frames "${_frames} + 1")
      message(STATUS "${_bytes}\t${_qualifier}\t${_where}")
      if(_bytes GREATER MAX_BYTES OR NOT _qualifier STREQUAL "static")
        list(APPEND _over "${_where}: ${_bytes} bytes (${_qualifier})")
      endif()
    endif()
  endforeach()
endforeach()

if(_frames EQUAL 0)
  message(FATAL_ERROR "CheckStackFrames: the .su files under ${SU_DIR} list no frame")
endif()
if(_over)
  string(REPLACE ";" "\n  " _over_text "${_over}")
  message(FATAL_ERROR "CheckStackFrames: frame over ${MAX_BYTES} bytes, or not static:\n  ${_over_text}")
endif()
message(STATUS "CheckStackFrames: ${_frames} frame(s), none over ${MAX_BYTES} bytes")
