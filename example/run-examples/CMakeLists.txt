cmake_minimum_required(VERSION 3.23)

set(TARGET run-examples)

project(${TARGET} VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(USE_QAPPLICATION)
  add_definitions(-DUSE_QAPPLICATION)
endif()

set(EXAMPLES_PATH "${CMAKE_BINARY_DIR}/example-bin/")
add_definitions(-DEXAMPLES_PATH="${EXAMPLES_PATH}")
if(NOT AsyncFw_DISABLE_BUILD_TESTS)
  set(TESTS_PATH "${CMAKE_BINARY_DIR}/test-bin/")
  add_definitions(-DTESTS_PATH="${TESTS_PATH}")
endif()

set(PROJECT_SOURCES
  main.cpp
)

add_executable(${TARGET} ${PROJECT_SOURCES})
target_link_libraries(${TARGET} ${AsyncFw_PROJECT_NAME})
set_target_properties(${TARGET} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/example-bin"
)
