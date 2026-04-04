cmake_minimum_required(VERSION 3.23)

if(NOT AsyncFw_PROJECT_NAME)
  set(AsyncFw_PROJECT_NAME AsyncFw)
endif()

include(cmake/version.cmake)

if(NOT FINAL_RELEASE AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/defines.cmake")
  include("${CMAKE_CURRENT_LIST_DIR}/defines.cmake")
endif()

set(QT_CREATOR_SKIP_MAINTENANCE_TOOL_PROVIDER ON)

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

file(GLOB AsyncFw_INSTALL_INCLUDE install_include/*)

add_custom_target(additional-files SOURCES AsyncFwConfig.cmake example/snippet.dox ${AsyncFw_INSTALL_INCLUDE})

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
  "${CMAKE_CURRENT_LIST_DIR}/core/ListenSocket.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/TlsContext.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/FunctionConnector.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/LogStream.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/DataArray.h"
  "${CMAKE_CURRENT_LIST_DIR}/core/function.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/core/log-types.hpp"
)

set(AsyncFw_CORE_SOURCES
  "core/AbstractThread.cpp"
  "core/Thread.cpp"
  "core/AnyData.cpp"
  "core/AbstractSocket.cpp"
  "core/AbstractTlsSocket.cpp"
  "core/ListenSocket.cpp"
  "core/DataArray.cpp"
  "core/LogStream.cpp"
  "core/TlsContext.cpp"
  "core/FunctionConnector.cpp"
  "core/log-types.cpp"
  "core/console_msg.cpp"
)

set(AsyncFw_HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/Coroutine.h"
  "${CMAKE_CURRENT_LIST_DIR}/Timer.h"
  "${CMAKE_CURRENT_LIST_DIR}/AddressInfo.h"
  "${CMAKE_CURRENT_LIST_DIR}/PollNotifier.h"
  "${CMAKE_CURRENT_LIST_DIR}/ThreadPool.h"
  "${CMAKE_CURRENT_LIST_DIR}/File.h"
  "${CMAKE_CURRENT_LIST_DIR}/Cryptor.h"

  "${CMAKE_CURRENT_LIST_DIR}/MainThread.h"

  "${CMAKE_CURRENT_LIST_DIR}/Rrd.h"
  "${CMAKE_CURRENT_LIST_DIR}/Log.h"
  "${CMAKE_CURRENT_LIST_DIR}/RrdServer.h"
  "${CMAKE_CURRENT_LIST_DIR}/RrdClient.h"

  "${CMAKE_CURRENT_LIST_DIR}/DataArraySocket.h"
  "${CMAKE_CURRENT_LIST_DIR}/DataArrayTcpServer.h"
  "${CMAKE_CURRENT_LIST_DIR}/DataArrayTcpClient.h"
  "${CMAKE_CURRENT_LIST_DIR}/DataArrayAbstractTcp.h"

  "${CMAKE_CURRENT_LIST_DIR}/Task.h"
  "${CMAKE_CURRENT_LIST_DIR}/HttpSocket.h"
  "${CMAKE_CURRENT_LIST_DIR}/HttpServer.h"

  "${CMAKE_CURRENT_LIST_DIR}/MulticastDns.h"
  "${CMAKE_CURRENT_LIST_DIR}/Instance.h"

  "${CMAKE_CURRENT_LIST_DIR}/Version.h"
)

set(AsyncFw_SOURCES
  "DataArraySocket.cpp"
  "DataArrayTcpServer.cpp"
  "DataArrayTcpClient.cpp"
  "DataArrayAbstractTcp.cpp"

  "Rrd.cpp"
  "Log.cpp"
  "RrdServer.cpp"
  "RrdClient.cpp"

  "Coroutine.cpp"
  "Timer.cpp"
  "PollNotifier.cpp"
  "File.cpp"
  "Cryptor.cpp"
  "ThreadPool.cpp"
  "Instance.cpp"

  "Task.cpp"
  "HttpSocket.cpp"
  "HttpServer.cpp"

  "MulticastDns.cpp"

  "Version.cpp"
)

set(AsyncFw_LINUX_HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/linux/SystemProcess.h"
  "${CMAKE_CURRENT_LIST_DIR}/linux/FileSystemWatcher.h"
)

set(AsyncFw_LINUX_SOURCES
  "AddressInfo.cpp" #need build cares for AMD64
  "linux/FileSystemWatcher.cpp"
  "linux/SystemProcess.cpp"
)

set(AsyncFw_3rdparty_SOURCES
  "3rdparty/WebSocket/WebSocket/WebSocket.cpp"
  "3rdparty/WebSocket/WebSocket/sha1/sha1.cpp"
  "3rdparty/WebSocket/WebSocket/base64/base64.cpp"
  "3rdparty/mdns/mdns.c"
)

if(NOT PROJECT_IS_TOP_LEVEL)
  set(AsyncFw_CORE_HEADERS ${AsyncFw_CORE_HEADERS} PARENT_SCOPE)
  set(AsyncFw_LINUX_HEADERS ${AsyncFw_LINUX_HEADERS} PARENT_SCOPE)
  set(AsyncFw_HEADERS ${AsyncFw_HEADERS} PARENT_SCOPE)
endif()

include_directories(.)

set(PROJECT_SOURCES ${AsyncFw_CORE_SOURCES} ${AsyncFw_SOURCES} ${AsyncFw_INTERNAL_HEADERS} ${AsyncFw_CORE_HEADERS} ${AsyncFw_HEADERS} ${AsyncFw_3rdparty_SOURCES})

if(NOT PLATFORM STREQUAL "AMD64")
  set(PROJECT_SOURCES ${PROJECT_SOURCES} ${AsyncFw_LINUX_HEADERS} ${AsyncFw_LINUX_SOURCES})
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

target_link_libraries(${AsyncFw_PROJECT_NAME} z crypto ssl cares)

#foreach(H ${AsyncFw_CORE_HEADERS} ${AsyncFw_HEADERS})
#  string(FIND ${H} ".hpp" F)
#  if(F EQUAL -1)
#    string (REPLACE "${CMAKE_CURRENT_LIST_DIR}/" "" H ${H})
#    string (REPLACE ".h" "" H ${H})
#    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/install_include/${H}" "#include \"${H}.h\"\n")
#  endif()
#endforeach()

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "AsyncFwConfigVersion.cmake"
    VERSION ${VERSION_STRING}
    COMPATIBILITY AnyNewerVersion)

install(EXPORT AsyncFw_TARGETS FILE AsyncFwTargets.cmake NAMESPACE AsyncFw:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AsyncFw)
install(FILES ${AsyncFw_CORE_HEADERS} DESTINATION include/AsyncFw/core)
install(FILES ${AsyncFw_LINUX_HEADERS} DESTINATION include/AsyncFw/linux)
install(FILES ${AsyncFw_HEADERS} DESTINATION include/AsyncFw)
install(FILES ${AsyncFw_INSTALL_INCLUDE} DESTINATION include/AsyncFw)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/AsyncFwConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/AsyncFwConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AsyncFw)

if(NOT AsyncFw_DISABLE_BUILD_EXAMPLES)
  add_subdirectory(example/FunctionConnector)
  add_subdirectory(example/Invoke)
  add_subdirectory(example/ListenSocket)
  add_subdirectory(example/Log)
  add_subdirectory(example/Socket)
  add_subdirectory(example/HttpSocket)
  add_subdirectory(example/HttpServer)
  add_subdirectory(example/ThreadPool)
  add_subdirectory(example/Timer)
  add_subdirectory(example/Coroutine)
  add_subdirectory(example/ContainerTask)
  add_subdirectory(example/CoroutineWait)
  add_subdirectory(example/MulticastDns)
  if(NOT PLATFORM STREQUAL "AMD64")
    add_subdirectory(example/Stdin)
    add_subdirectory(example/Uart)
    add_subdirectory(example/SystemProcess)
    add_subdirectory(example/FileSystemWatcher)
    add_subdirectory(example/run-examples)
  endif()
endif()
