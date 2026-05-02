cmake_minimum_required(VERSION 3.10)
project(ruyi C)

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

file(GLOB RUYI_ROOT_SRC "*.c")
file(GLOB RUYI_DS_SRC   "ruyi-ds/*.c")
file(GLOB RUYI_LOCK_SRC "ruyi-lock/*.c")
set(RUYI_ALL_SRC ${RUYI_ROOT_SRC} ${RUYI_DS_SRC} ${RUYI_LOCK_SRC})

add_library(ruyi_static STATIC ${RUYI_ALL_SRC})
set_target_properties(ruyi_static PROPERTIES OUTPUT_NAME ruyi)
add_library(ruyi_shared SHARED ${RUYI_ALL_SRC})
set_target_properties(ruyi_shared PROPERTIES OUTPUT_NAME ruyi)

set(RUYI_INC_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ruyi-ds
    ${CMAKE_CURRENT_SOURCE_DIR}/ruyi-lock
)
target_include_directories(ruyi_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ruyi-ds
    ${CMAKE_CURRENT_SOURCE_DIR}/ruyi-lock
)
target_include_directories(ruyi_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ruyi-ds
    ${CMAKE_CURRENT_SOURCE_DIR}/ruyi-lock
)

find_package(Threads REQUIRED)
target_link_libraries(ruyi_static PUBLIC Threads::Threads)
target_link_libraries(ruyi_shared PUBLIC Threads::Threads)

include(GNUInstallDirs)

install(TARGETS ruyi_static ruyi_shared
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

file(GLOB_RECURSE RUYI_HEADERS "*.h")
foreach(hdr ${RUYI_HEADERS})
    file(RELATIVE_PATH rel ${CMAKE_CURRENT_SOURCE_DIR} ${hdr})
    get_filename_component(dir ${rel} DIRECTORY)
    install(FILES ${hdr} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ruyi/${dir})
endforeach()
