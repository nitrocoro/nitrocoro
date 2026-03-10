add_library(nitrocoro-http STATIC
    src/BodyReader.cc
    src/BodyWriter.cc
    src/HttpClient.cc
    src/HttpMethod.cc
    src/HttpContext.cc
    src/HttpRouter.cc
    src/HttpHeader.cc
    src/HttpIncomingStream.cc
    src/HttpOutgoingStream.cc
    src/HttpServer.cc
    src/HttpParser.cc
    src/body_reader/ContentLengthReader.cc
    src/body_reader/ChunkedReader.cc
    src/body_reader/UntilCloseReader.cc
    src/body_writer/ContentLengthWriter.cc
    src/body_writer/ChunkedWriter.cc
    src/body_writer/UntilCloseWriter.cc
)

target_include_directories(nitrocoro-http PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(nitrocoro-http PUBLIC nitrocoro)

include(GNUInstallDirs)

install(TARGETS nitrocoro-http
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
    add_executable(http_stream_test tests/http_stream_test.cc)
    target_link_libraries(http_stream_test PRIVATE nitrocoro-http)
    add_test(NAME http_stream_test COMMAND http_stream_test)

    add_executable(http_test tests/http_test.cc)
    target_link_libraries(http_test PRIVATE nitrocoro-http)
    add_test(NAME http_test COMMAND http_test)

    add_executable(http_router_test tests/http_router_test.cc)
    target_link_libraries(http_router_test PRIVATE nitrocoro-http)
    add_test(NAME http_router_test COMMAND http_router_test)

    add_executable(http_method_test tests/http_method_test.cc)
    target_link_libraries(http_method_test PRIVATE nitrocoro-http)
    add_test(NAME http_method_test COMMAND http_method_test)
endif ()
