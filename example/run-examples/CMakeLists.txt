cmake_minimum_required(VERSION 3.23)

set(TARGET run-examples)

project(${TARGET} VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64" AND USE_QAPPLICATION)
  find_package(Qt6 CONFIG COMPONENTS Core)
endif()

set(PROJECT_SOURCES
  main.cpp
)

set(EXAMPLES_PATH "${CMAKE_BINARY_DIR}/example-bin/")
add_definitions(-DEXAMPLES_PATH="${EXAMPLES_PATH}")

add_executable(${TARGET} ${PROJECT_SOURCES})
target_link_libraries(${TARGET} ${AsyncFw_PROJECT_NAME})
set_target_properties(${TARGET} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/example-bin"
)

if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64" AND USE_QAPPLICATION)
  target_link_libraries(${AsyncFw_PROJECT_NAME} Qt6::Core)
endif()
