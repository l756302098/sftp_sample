cmake_minimum_required(VERSION 3.0)

project(stp_sample VERSION 1.0)

add_definitions(-std=c++17)

if(HOST_ARCH STREQUAL "x86_64")
    set(PLATFORM "x64")
else()
    set(PLATFORM "arm")
endif()

include(cmake/FindLibSSH.cmake)

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib/${PLATFORM})

add_subdirectory(${PROJECT_SOURCE_DIR}/client)

add_subdirectory(${PROJECT_SOURCE_DIR}/server)
