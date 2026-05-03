cmake_minimum_required(VERSION 3.10)
project(ruyi-project C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_definitions(_POSIX_C_SOURCE=200809L)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Ruyi network library requires 64-bit platform. Detected: ${CMAKE_SIZEOF_VOID_P}-bit")
endif()

string(TOLOWER "${CMAKE_SYSTEM_NAME}" SYS_NAME)
if(NOT SYS_NAME MATCHES "linux|darwin|freebsd|openbsd|netbsd")
    message(FATAL_ERROR "Unsupported platform '${CMAKE_SYSTEM_NAME}'. Only Linux, BSD and macOS are supported.")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif()

set(CMAKE_C_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG -Wall -Wextra")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(ruyi-src)
option(BUILD_EXAMPLES "Build example programs" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(ruyi-example)
endif()
