cmake_minimum_required(VERSION 3.23)

set(TARGET HttpServerExample)

project(${TARGET} VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DHTTP_SERVER_HOME="${CMAKE_SOURCE_DIR}/docs")

set(PROJECT_SOURCES
  main.cpp
)

add_executable(${TARGET} ${PROJECT_SOURCES})
target_link_libraries(${TARGET} ${AsyncFw_PROJECT_NAME})
