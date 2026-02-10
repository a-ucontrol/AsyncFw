find_package(Git QUIET)
execute_process(
  OUTPUT_VARIABLE GV_
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND ${GIT_EXECUTABLE} describe --long --dirty=-m
  WORKING_DIRECTORY ${SOURCE_DIR})
execute_process(
  OUTPUT_VARIABLE GB_
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${SOURCE_DIR})
if(GB_ AND GV_)
  set(GIT_VERSION ${GB_}/${GV_})
else()
  set(GIT_VERSION unknown)
endif()
if(EXISTS ${BINARY_DIR}/version)
  file(READ ${BINARY_DIR}/version CV_)
endif()
if(NOT "${GIT_VERSION}" STREQUAL "${CV_}")
  file(WRITE ${BINARY_DIR}/version ${GIT_VERSION})
  message("Version updated: ${GIT_VERSION}")
  string(REGEX MATCH ".*(Makefiles).*" _ "${GENERATOR}")
  if(CMAKE_MATCH_1)
    message("Check build system")
    execute_process(
      COMMAND ${CMAKE_COMMAND} -S${SOURCE_DIR} -B${BINARY_DIR} --check-build-system CMakeFiles/Makefile.cmake 0)
  endif()
endif()
