cmake_minimum_required(VERSION 3.23)

set(TARGET InvokeExample)

project(${TARGET} VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROJECT_SOURCES
  main.cpp
)

add_executable(${TARGET} ${PROJECT_SOURCES})
target_link_libraries(${TARGET} AsyncFw)

target_compile_options(${TARGET} PRIVATE ${SANITIZE_COMPILE_OPTIONS})
target_link_libraries(${TARGET} ${SANITIZE_LINK_LIBRARIES})
