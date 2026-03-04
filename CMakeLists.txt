cmake_minimum_required(VERSION 3.15)
project(nitrocoro CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif ()

set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
        message(FATAL_ERROR "GCC 10+ is required")
    elseif (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
        add_compile_options(-fcoroutines)
    endif ()
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
        message(STATUS "Note: GCC versions before 13 have known coroutine bugs. GCC 13+ is recommended.")
    endif ()
endif ()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
        message(FATAL_ERROR "Clang 16+ is required")
    endif ()
    option(USE_LIBCXX "Use libc++ instead of libstdc++ (Clang only)" OFF)
    if (USE_LIBCXX)
        add_compile_options(-stdlib=libc++)
        add_link_options(-stdlib=libc++)
    endif ()
endif ()

# Build library
add_library(nitrocoro STATIC
    src/Scheduler.cc
    src/Socket.cc
    src/TcpServer.cc
    src/TcpConnection.cc
    src/IoChannel.cc
    src/InetAddress.cc
    src/DnsResolver.cc
    src/Dns.cc
    src/Url.cc
    src/Debug.cc
)

target_include_directories(nitrocoro PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link pthread for threading support
find_package(Threads REQUIRED)
target_link_libraries(nitrocoro PUBLIC Threads::Threads)

option(BUILD_TESTING "Build tests" ON)
if (BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif ()

option(BUILD_EXAMPLES "Build examples" ON)
if (BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

# Extensions
option(BUILD_ALL_EXTENSIONS "Build all extensions" OFF)
if (BUILD_ALL_EXTENSIONS)
    set(BUILD_EXT_HTTP ON)
    set(BUILD_EXT_PG   ON)
    set(BUILD_EXT_TLS  ON)
endif ()

option(BUILD_EXT_HTTP "Build HTTP extension" ON)
if (BUILD_EXT_HTTP)
    add_subdirectory(extensions/http)
endif ()

option(BUILD_EXT_PG "Build PostgreSQL extension" OFF)
if (BUILD_EXT_PG)
    add_subdirectory(extensions/pg)
endif ()

option(BUILD_EXT_TLS "Build TLS extension" OFF)
if (BUILD_EXT_TLS)
    add_subdirectory(extensions/tls)
endif ()

# Integration tests (must be after extensions)
if (BUILD_TESTING)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration)
        add_subdirectory(tests/integration)
    endif()
endif ()
