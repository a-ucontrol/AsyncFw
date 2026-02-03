cmake_minimum_required(VERSION 3.23)

project(AsyncFw VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DABSTRACT_THREAD_ATOMIC_FLAG_SYNC_INVOKE_METHOD) #ThreadSanitizer: data race

set(AsyncFw_CORE_HEADERS
  "core/Socket.h"
  "core/TlsSocket.h"
  "core/Thread.h"
  "core/AnyData.h"
  "core/TlsContext.h"
  "core/FunctionConnector.h"
  "core/LogStream.h"
  "core/DataArray.h"
  "core/log-types.hpp"
  "core/console_msg.hpp"
)

set(AsyncFw_CORE_SOURCES
  "core/Thread.cpp"
  "core/AnyData.cpp"
  "core/Socket.cpp"
  "core/TlsSocket.cpp"
  "core/DataArray.cpp"
  "core/LogStream.cpp"
  "core/TlsContext.cpp"
  "core/FunctionConnector.cpp"
  "core/log-types.cpp"
  "core/console_msg.cpp"
)

set(AsyncFw_HEADERS
  "Coroutine.h"
  "Timer.h"
  "AddressInfo.h"
  "PollNotifier.h"
  "ThreadPool.h"
  "File.h"
  "Cryptor.h"

  "MainThread.h"

  "linux/SystemProcess.h"
  "linux/FileSystemWatcher.h"

  "Rrd.h"
  "Log.h"
  "RrdServer.h"
  "RrdClient.h"

  "DataArraySocket.h"
  "DataArrayTcpServer.h"
  "DataArrayTcpClient.h"
  "DataArrayAbstractTcp.h"

  "Task.h"
  "HttpSocket.h"
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

  "Task.cpp"
  "HttpSocket.cpp"
)

if(NOT PLATFORM STREQUAL "AMD64")
  set(AsyncFw_HEADERS ${AsyncFw_HEADERS}
    "AddressInfo.h" #need build cares for AMD64
    "linux/FileSystemWatcher.h"
    "linux/SystemProcess.h"
  )
  set(AsyncFw_SOURCES ${AsyncFw_SOURCES}
    "AddressInfo.cpp" #need build cares for AMD64
    "linux/FileSystemWatcher.cpp"
    "linux/SystemProcess.cpp"
  )
endif()

include_directories(.)

set(PROJECT_SOURCES ${AsyncFw_CORE_SOURCES} ${AsyncFw_SOURCES} ${AsyncFw_CORE_HEADERS} ${AsyncFw_HEADERS})

if(BUILD_SHARED_LIBS)
  add_library(${CMAKE_PROJECT_NAME} SHARED ${PROJECT_SOURCES})
else()
  add_library(${CMAKE_PROJECT_NAME} STATIC ${PROJECT_SOURCES})
endif()

link_libraries(z crypto ssl cares)

add_subdirectory(example/FunctionConnector)
add_subdirectory(example/Invoke)
add_subdirectory(example/ListenSocket)
add_subdirectory(example/Log)
add_subdirectory(example/Socket)
add_subdirectory(example/HttpSocket)
add_subdirectory(example/Stdin)
add_subdirectory(example/ThreadPool)
add_subdirectory(example/Timer)
add_subdirectory(example/Coroutine)
add_subdirectory(example/ContainerTask)
add_subdirectory(example/CoroutineWait)
add_subdirectory(example/SystemProcess)
add_subdirectory(example/run-examples)


