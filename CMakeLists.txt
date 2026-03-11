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
    src/Channel.cc
    src/InetAddress.cc
    src/TaskQueue.cc
    src/DnsResolver.cc
    src/Dns.cc
    src/Url.cc
    src/Debug.cc
    src/utils/Base64.cc
    src/utils/UrlEncode.cc
    src/utils/Sha1.cc
    src/utils/Md5.cc
)

target_include_directories(nitrocoro PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link pthread for threading support
find_package(Threads REQUIRED)
target_link_libraries(nitrocoro PUBLIC Threads::Threads)

option(BUILD_TESTING "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)

if (BUILD_TESTING)
    enable_testing()
endif ()

# Extensions
option(BUILD_ALL_EXTENSIONS "Build all extensions" OFF)
if (BUILD_ALL_EXTENSIONS)
    option(BUILD_EXT_HTTP "Build HTTP extension" ON)
    option(BUILD_EXT_TLS "Build TLS extension" ON)
    option(BUILD_EXT_PG "Build PostgreSQL extension" ON)
    option(BUILD_EXT_WEBSOCKET "Build WebSocket extension" ON)
    option(BUILD_EXT_HTTP2 "Build HTTP/2 extension" ON)
else ()
    option(BUILD_EXT_HTTP "Build HTTP extension" ON)
    option(BUILD_EXT_TLS "Build TLS extension" OFF)
    option(BUILD_EXT_PG "Build PostgreSQL extension" OFF)
    option(BUILD_EXT_WEBSOCKET "Build WebSocket extension" OFF)
endif ()

if (BUILD_EXT_HTTP)
    message(STATUS "Building HTTP extension")
    add_subdirectory(extensions/http)
else ()
    message(STATUS "Skipping HTTP extension")
endif ()

if (BUILD_EXT_WEBSOCKET)
    if (NOT BUILD_EXT_HTTP)
        message(FATAL_ERROR "BUILD_EXT_WEBSOCKET requires BUILD_EXT_HTTP")
    endif ()
    message(STATUS "Building WebSocket extension")
    add_subdirectory(extensions/websocket)
else ()
    message(STATUS "Skipping WebSocket extension")
endif ()

option(BUILD_EXT_HTTP2 "Build HTTP/2 extension" OFF)
if (BUILD_EXT_HTTP2)
    if (NOT BUILD_EXT_HTTP)
        message(FATAL_ERROR "BUILD_EXT_HTTP2 requires BUILD_EXT_HTTP")
    endif ()
    message(STATUS "Building HTTP/2 extension")
    add_subdirectory(extensions/http2)
else ()
    message(STATUS "Skipping HTTP/2 extension")
endif ()

if (BUILD_EXT_TLS)
    message(STATUS "Building TLS extension")
    add_subdirectory(extensions/tls)
else ()
    message(STATUS "Skipping TLS extension")
endif ()

if (BUILD_EXT_HTTP AND BUILD_EXT_TLS)
    option(BUILD_EXT_HTTPS "Build HTTPS extension" ON)
else ()
    option(BUILD_EXT_HTTPS "Build HTTPS extension" OFF)
endif ()

if (BUILD_EXT_HTTPS)
    message(STATUS "Building HTTPS extension")
    add_subdirectory(extensions/https)
else ()
    message(STATUS "Skipping HTTPS extension")
endif ()

if (BUILD_EXT_WEBSOCKET AND BUILD_EXT_TLS)
    option(BUILD_EXT_WSS "Build WSS extension" ON)
else ()
    option(BUILD_EXT_WSS "Build WSS extension" OFF)
endif ()

if (BUILD_EXT_WSS)
    message(STATUS "Building WSS extension")
    add_subdirectory(extensions/wss)
else ()
    message(STATUS "Skipping WSS extension")
endif ()

if (BUILD_EXT_PG)
    message(STATUS "Building PostgreSQL extension")
    add_subdirectory(extensions/pg)
else ()
    message(STATUS "Skipping PostgreSQL extension")
endif ()

# Integration tests (must be after extensions)
if (BUILD_TESTING)
    add_subdirectory(tests)
    add_subdirectory(tests/integration)
endif ()

if (BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

# Installation
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(NITROCORO_VERSION 0.1.0)

install(TARGETS nitrocoro
    EXPORT nitrocoroTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/nitrocoro
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT nitrocoroTargets
    FILE nitrocoroTargets.cmake
    NAMESPACE nitrocoro::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nitrocoro
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/nitrocoroConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/nitrocoroConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nitrocoro
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/nitrocoroConfigVersion.cmake
    VERSION ${NITROCORO_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/nitrocoroConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/nitrocoroConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nitrocoro
)
