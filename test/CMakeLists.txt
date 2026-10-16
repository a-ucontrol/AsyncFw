cmake_minimum_required(VERSION 3.23)

set(TARGET tests)

project(${TARGET} VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${TARGET}-core core.cpp)
target_link_libraries(${TARGET}-core ${AsyncFw_PROJECT_NAME})
set_target_properties(${TARGET}-core PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-bin"
)

add_executable(${TARGET}-main main.cpp)
target_link_libraries(${TARGET}-main ${AsyncFw_PROJECT_NAME})
set_target_properties(${TARGET}-main PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-bin"
)
