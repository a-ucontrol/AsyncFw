cmake_minimum_required(VERSION 3.23)

project(CoroutineExample VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROJECT_SOURCES
        main.cpp
)

add_executable(${CMAKE_PROJECT_NAME} ${PROJECT_SOURCES})
target_link_libraries(${CMAKE_PROJECT_NAME} ${LRW_APP_LIB} crypto ssl cares)

#target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -g -fsanitize=address)
#target_link_libraries(${CMAKE_PROJECT_NAME} -Wl,-Bstatic,-lasan,-Bdynamic)

#target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -g -fsanitize=thread)
#target_link_libraries(${CMAKE_PROJECT_NAME} tsan)

include(GNUInstallDirs)
install(TARGETS ${CMAKE_PROJECT_NAME}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
