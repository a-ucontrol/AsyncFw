cmake_minimum_required(VERSION 3.23)

project(AsyncFw VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
)

set(AsyncFw_CORE_SOURCES
  "core/Thread.cpp"
  "core/Socket.cpp"
  "core/TlsSocket.cpp"
  "core/DataArray.cpp"
  "core/LogStream.cpp"
  "core/TlsContext.cpp"
  "core/FunctionConnector.cpp"
  "core/log-types.cpp"
)

set(AsyncFw_HEADERS
  "Coroutine.h"
  "Timer.h"
  "AddressInfo.h"
  "PollNotifier.h"
  "ThreadPool.h"
  "File.h"
  "Cryptor.h"

  "core/MainThread.h"

  "linux/SystemProcess.h"
  "linux/FileSystemWatcher.h"

  "Rrd.h"
  "Log.h"
  "LogTcpServer.h"
  "LogTcpClient.h"

  "DataArraySocket.h"
  "DataArrayTcpServer.h"
  "DataArrayTcpClient.h"
  "DataArrayAbstractTcp.h"
)

set(AsyncFw_SOURCES
  "DataArraySocket.cpp"
  "DataArrayTcpServer.cpp"
  "DataArrayTcpClient.cpp"
  "DataArrayAbstractTcp.cpp"

  "Rrd.cpp"
  "Log.cpp"
  "LogTcpServer.cpp"
  "LogTcpClient.cpp"

  "Coroutine.cpp"
  "Timer.cpp"
  "PollNotifier.cpp"
  "File.cpp"
  "Cryptor.cpp"
  "ThreadPool.cpp"
)

if(NOT PLATFORM STREQUAL "AMD64")
  set(AsyncFw_HEADERS ${AsyncFw_HEADERS}
    "AddressInfo.h" #need build cares for AMD64
  )
  set(AsyncFw_SOURCES ${AsyncFw_SOURCES}
    "AddressInfo.cpp" #need build cares for AMD64
  )
endif()

set(PROJECT_SOURCES ${AsyncFw_CORE_SOURCES} ${AsyncFw_SOURCES} ${AsyncFw_CORE_HEADERS} ${AsyncFw_HEADERS})

if(BUILD_SHARED_LIBS)
  add_library(${CMAKE_PROJECT_NAME} SHARED ${PROJECT_SOURCES})
else()
  add_library(${CMAKE_PROJECT_NAME} STATIC ${PROJECT_SOURCES})
endif()
