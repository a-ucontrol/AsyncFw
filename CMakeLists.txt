cmake_minimum_required(VERSION 3.23)

if(NOT AsyncFw_PROJECT_NAME)
  set(AsyncFw_PROJECT_NAME AsyncFw)
endif()

include(cmake/version.cmake)

if(NOT FINAL_RELEASE AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/defines.cmake")
  include("${CMAKE_CURRENT_LIST_DIR}/defines.cmake")
endif()


# defines.cmake
#set(BUILD_SHARED_LIBS ON)

### int main()
### {
###   std::thread t1([]{});
###   std::thread t2([]{});
###   t1.join();
###   t2.join();
### }
### gcc main.cpp -lstdc++ -g -fsanitize=thread,undefined
### -- int main() with -fsanitize=thread,undefined -> ThreadSanitizer: data race

### __tsan_on_report -> break point function for debugger

#add_definitions(-DEXTEND_LOGSTREAM_TRACE)
#add_definitions(-DEXTEND_RRD_TRACE)
#add_definitions(-DEXTEND_THREAD_TRACE)
#add_definitions(-DEXTEND_SOCKET_TRACE)
#add_definitions(-DEXTEND_FILESYSTEMWATCHER_TRACE)
#add_definitions(-DEXTEND_SYSTEMPROCESS_TRACE)
#add_definitions(-DEXTEND_LOG_TRACE)
#add_definitions(-DEXTEND_FUNCTIONCONNECTOR_TRACE)
#add_definitions(-DEXTEND_HTTP_TRACE)
#add_definitions(-DEXTEND_MDNS_TRACE)
# -- defines.cmake

project(${AsyncFw_PROJECT_NAME} VERSION ${VERSION_STRING} LANGUAGES C CXX)

message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")

if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64")
  #need for run examles in wine: export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
  if(NOT AMD64_OPENSSL_INCLUDE_DIR OR NOT AMD64_OPENSSL_LIB_DIR)
    message(FATAL_ERROR "AMD64_OPENSSL_INCLUDE_DIR OR AMD64_OPENSSL_LIB_DIR not set")
  endif()
  include_directories(${AMD64_OPENSSL_INCLUDE_DIR})
  link_directories(${AMD64_OPENSSL_LIB_DIR})
  if(AMD64_USE_CARES)
    message(STATUS "AMD64_USE_CARES")
    find_package(c-ares CONFIG)
  endif()
  if(AMD64_USE_QAPPLICATION)
    message(STATUS "AMD64_USE_QAPPLICATION")
    find_package(Qt6 CONFIG COMPONENTS Core)
  endif()
endif()

file(GLOB AsyncFw_INCLUDE include/AsyncFw/*)

add_custom_target(additional-files SOURCES AsyncFwConfig.cmake README.md example.dox example/snippet.dox ${AsyncFw_INCLUDE})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(AsyncFw_INTERNAL_HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/core/console_msg.hpp"
)

set(AsyncFw_CORE_HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/core/AbstractThread.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/Thread.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/AnyData.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/AbstractSocket.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/AbstractTlsSocket.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/TlsContext.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/FunctionConnector.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/LogStream.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/DataArray.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/function.hpp"
)

set(AsyncFw_CORE_SOURCES
  "core/AbstractThread.cpp"
  "core/Thread.cpp"
  "core/AnyData.cpp"
  "core/AbstractSocket.cpp"
  "core/AbstractTlsSocket.cpp"
  "core/DataArray.cpp"
  "core/LogStream.cpp"
  "core/TlsContext.cpp"
  "core/FunctionConnector.cpp"
  "core/console_msg.cpp"
)

set(AsyncFw_MAIN_HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/main/ListenSocket.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/Coroutine.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/Timer.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/PollNotifier.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/ThreadPool.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/File.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/Cryptor.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/MainThread.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/Rrd.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/Log.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/RrdServer.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/RrdClient.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/DataArraySocket.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/DataArrayTcpServer.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/DataArrayTcpClient.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/DataArrayAbstractTcp.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/Task.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/HttpSocket.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/HttpServer.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/MulticastDns.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/Instance.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/Version.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/log-types.hpp"

  "${CMAKE_CURRENT_LIST_DIR}/main/AddressInfo.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/SystemProcess.h"
  "${CMAKE_CURRENT_LIST_DIR}/main/FileSystemWatcher.h"
)

set(AsyncFw_MAIN_SOURCES
  "main/ListenSocket.cpp"
  "main/DataArraySocket.cpp"
  "main/DataArrayTcpServer.cpp"
  "main/DataArrayTcpClient.cpp"
  "main/DataArrayAbstractTcp.cpp"
  "main/Rrd.cpp"
  "main/Log.cpp"
  "main/RrdServer.cpp"
  "main/RrdClient.cpp"
  "main/Coroutine.cpp"
  "main/Timer.cpp"
  "main/PollNotifier.cpp"
  "main/File.cpp"
  "main/Cryptor.cpp"
  "main/ThreadPool.cpp"
  "main/Instance.cpp"
  "main/Task.cpp"
  "main/HttpSocket.cpp"
  "main/HttpServer.cpp"
  "main/MulticastDns.cpp"
  "main/Version.cpp"
  "main/log-types.cpp"
)

set(AsyncFw_LINUX_SOURCES
  "main/AddressInfo.cpp" #need cares for AMD64
  "linux/FileSystemWatcher.cpp"
  "linux/SystemProcess.cpp"
)

#set(AsyncFw_WINDOWS_SOURCES
#  "main/AddressInfo.cpp" #need cares for AMD64 #!!!
#  "windows/FileSystemWatcher.cpp"
#  "windows/SystemProcess.cpp"
#)

set(AsyncFw_3rdparty_SOURCES
  "3rdparty/WebSocket/WebSocket/WebSocket.cpp"
  "3rdparty/WebSocket/WebSocket/sha1/sha1.cpp"
  "3rdparty/WebSocket/WebSocket/base64/base64.cpp"
  "3rdparty/mdns/mdns.c"
)

if(NOT PROJECT_IS_TOP_LEVEL)
  set(AsyncFw_CORE_HEADERS ${AsyncFw_CORE_HEADERS} PARENT_SCOPE)
  set(AsyncFw_MAIN_HEADERS ${AsyncFw_MAIN_HEADERS} PARENT_SCOPE)
  set(AsyncFw_INCLUDE ${AsyncFw_INCLUDE} PARENT_SCOPE)
endif()

include_directories(include)
include_directories(.)

set(PROJECT_SOURCES ${AsyncFw_CORE_SOURCES} ${AsyncFw_MAIN_SOURCES} ${AsyncFw_INTERNAL_HEADERS} ${AsyncFw_CORE_HEADERS} ${AsyncFw_MAIN_HEADERS} ${AsyncFw_3rdparty_SOURCES})


if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64")
  if(AMD64_USE_CARES)
    set(PROJECT_SOURCES ${PROJECT_SOURCES} "main/AddressInfo.cpp")
  endif()
  if(AMD64_USE_QAPPLICATION)
    set(PROJECT_SOURCES ${PROJECT_SOURCES} "windows/FileSystemWatcher.cpp" "windows/SystemProcess.cpp")
  endif()
  set(PROJECT_SOURCES ${PROJECT_SOURCES})
else()
  set(PROJECT_SOURCES ${PROJECT_SOURCES} ${AsyncFw_LINUX_SOURCES})
endif()

include(GNUInstallDirs)

if(BUILD_SHARED_LIBS AND BUILD_STATIC_LIBS)
  add_library(${AsyncFw_PROJECT_NAME}_objects OBJECT ${PROJECT_SOURCES})
  add_library(${AsyncFw_PROJECT_NAME} SHARED $<TARGET_OBJECTS:${AsyncFw_PROJECT_NAME}_objects>)
  add_library(${AsyncFw_PROJECT_NAME}_static STATIC $<TARGET_OBJECTS:${AsyncFw_PROJECT_NAME}_objects>)
  set_target_properties(${AsyncFw_PROJECT_NAME}_static PROPERTIES OUTPUT_NAME ${AsyncFw_PROJECT_NAME})
  install(TARGETS ${AsyncFw_PROJECT_NAME} ${AsyncFw_PROJECT_NAME}_static EXPORT AsyncFw_TARGETS DESTINATION ${CMAKE_INSTALL_LIBDIR})
  target_compile_options(${AsyncFw_PROJECT_NAME}_objects PRIVATE -fPIC -Wall -Wno-psabi)
  target_compile_definitions(${AsyncFw_PROJECT_NAME}_objects PRIVATE GIT_VERSION="${GIT_VERSION}" VERSION_STRING="${VERSION_STRING}")
  target_link_libraries(${AsyncFw_PROJECT_NAME}_static z crypto ssl cares)
else()
  if(BUILD_SHARED_LIBS)
    add_library(${AsyncFw_PROJECT_NAME} SHARED ${PROJECT_SOURCES})
  else()
    add_library(${AsyncFw_PROJECT_NAME} STATIC ${PROJECT_SOURCES})
  endif()
  target_compile_options(${AsyncFw_PROJECT_NAME} PRIVATE -fPIC -Wall -Wno-psabi)
  target_compile_definitions(${AsyncFw_PROJECT_NAME} PRIVATE GIT_VERSION="${GIT_VERSION}" VERSION_STRING="${VERSION_STRING}")
  install(TARGETS ${AsyncFw_PROJECT_NAME} EXPORT AsyncFw_TARGETS DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

target_link_libraries(${AsyncFw_PROJECT_NAME} z crypto ssl)
if(NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64")
  target_link_libraries(${AsyncFw_PROJECT_NAME} cares)
else()
  target_link_libraries(${AsyncFw_PROJECT_NAME} iphlpapi Ws2_32)
  if(AMD64_USE_CARES)
    target_link_libraries(${AsyncFw_PROJECT_NAME} c-ares::cares)
  endif()
  if(AMD64_USE_QAPPLICATION)
    target_link_libraries(${AsyncFw_PROJECT_NAME} Qt6::Core)
  endif()
endif()

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "AsyncFwConfigVersion.cmake"
    VERSION ${VERSION_STRING}
    COMPATIBILITY AnyNewerVersion)

install(EXPORT AsyncFw_TARGETS FILE AsyncFwTargets.cmake NAMESPACE AsyncFw:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AsyncFw)
install(FILES ${AsyncFw_CORE_HEADERS} DESTINATION include/AsyncFw/core)
install(FILES ${AsyncFw_MAIN_HEADERS} DESTINATION include/AsyncFw/main)
install(FILES ${AsyncFw_INCLUDE} DESTINATION include/AsyncFw)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/AsyncFwConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/AsyncFwConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AsyncFw)

if(NOT AsyncFw_DISABLE_BUILD_EXAMPLES)
  add_subdirectory(example/FunctionConnector)
  add_subdirectory(example/Invoke)
  add_subdirectory(example/ListenSocket)
  add_subdirectory(example/Log)
  add_subdirectory(example/HttpServer)
  add_subdirectory(example/ThreadPool)
  add_subdirectory(example/Timer)
  add_subdirectory(example/Coroutine)
  add_subdirectory(example/ContainerTask)
  add_subdirectory(example/CoroutineWait)
  add_subdirectory(example/MulticastDns)
  if(NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64" OR AMD64_USE_CARES)
    add_subdirectory(example/Socket)
    add_subdirectory(example/HttpSocket)
  endif()
  if(NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64" OR AMD64_USE_QAPPLICATION)
    add_subdirectory(example/FileSystemWatcher)
    add_subdirectory(example/SystemProcess)
    add_subdirectory(example/run-examples)
   endif()
  if(NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64")
    add_subdirectory(example/Stdin)
    add_subdirectory(example/Uart)
  endif()
endif()
