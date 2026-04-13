add_library(nitrocoro-http STATIC
    src/Http1BodyReader.cc
    src/Http1BodyWriter.cc
    src/Cookie.cc
    src/CookieStore.cc
    src/Form.cc
    src/HttpClient.cc
    src/HttpMessage.cc
    src/HttpMethod.cc
    src/HttpRouter.cc
    src/RouterCore.cc
    src/HttpHeader.cc
    src/HttpIncomingStream.cc
    src/HttpOutgoingMessage.cc
    src/HttpServer.cc
    src/Http1RequestSink.cc
    src/Http1ResponseSink.cc
    src/HttpParser.cc
    src/HttpUtils.cc
    src/StaticFiles.cc
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
    add_executable(http_client_test tests/http_client_test.cc)
    target_link_libraries(http_client_test PRIVATE nitrocoro-http)
    add_test(NAME http_client_test COMMAND http_client_test)

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

    add_executable(http_parser_test tests/http_parser_test.cc)
    target_link_libraries(http_parser_test PRIVATE nitrocoro-http)
    add_test(NAME http_parser_test COMMAND http_parser_test)

    add_executable(http_pipeline_test tests/http_pipeline_test.cc)
    target_link_libraries(http_pipeline_test PRIVATE nitrocoro-http)
    add_test(NAME http_pipeline_test COMMAND http_pipeline_test)

    add_executable(static_files_test tests/static_files_test.cc)
    target_link_libraries(static_files_test PRIVATE nitrocoro-http)
    add_test(NAME static_files_test COMMAND static_files_test)

    add_executable(cookie_store_test tests/cookie_store_test.cc)
    target_link_libraries(cookie_store_test PRIVATE nitrocoro-http)
    add_test(NAME cookie_store_test COMMAND cookie_store_test)

    add_executable(cookie_test tests/cookie_test.cc)
    target_link_libraries(cookie_test PRIVATE nitrocoro-http)
    add_test(NAME cookie_test COMMAND cookie_test)

    add_executable(form_test tests/form_test.cc)
    target_link_libraries(form_test PRIVATE nitrocoro-http)
    add_test(NAME form_test COMMAND form_test)

    add_executable(middleware_test tests/middleware_test.cc)
    target_link_libraries(middleware_test PRIVATE nitrocoro-http)
    add_test(NAME middleware_test COMMAND middleware_test)

    add_executable(http_outgoing_message_test tests/http_outgoing_message_test.cc)
    target_link_libraries(http_outgoing_message_test PRIVATE nitrocoro-http)
    add_test(NAME http_outgoing_message_test COMMAND http_outgoing_message_test)
endif ()
