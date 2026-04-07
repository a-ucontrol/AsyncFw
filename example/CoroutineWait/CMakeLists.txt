cmake_minimum_required(VERSION 3.23)

set(TARGET CoroutineWaitExample)

project(${TARGET} VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROJECT_SOURCES
  main.cpp
)

add_executable(${TARGET} ${PROJECT_SOURCES})
target_link_libraries(${TARGET} ${AsyncFw_PROJECT_NAME})
set_target_properties(${TARGET} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/example-bin"
)

