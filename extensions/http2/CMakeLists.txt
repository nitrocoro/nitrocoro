add_library(nitrocoro-http2 STATIC
    src/frame/FrameReader.cc
    src/hpack/HpackTable.cc
    src/hpack/Hpack.cc
    src/Http2ResponseSink.cc
    src/Http2Session.cc
    src/Http2Server.cc
)

target_include_directories(nitrocoro-http2
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(nitrocoro-http2 PUBLIC nitrocoro nitrocoro-http nitrocoro-tls)

include(GNUInstallDirs)

install(TARGETS nitrocoro-http2
    EXPORT nitrocoroTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/nitrocoro
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

option(BUILD_TESTING "Build tests" ON)
if (BUILD_TESTING)
    enable_testing()
    add_executable(http2_test tests/http2_test.cc)
    target_link_libraries(http2_test PRIVATE nitrocoro-http2)
    add_test(NAME http2_test COMMAND http2_test)
endif ()
