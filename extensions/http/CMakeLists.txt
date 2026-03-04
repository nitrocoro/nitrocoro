add_library(nitrocoro-http STATIC
    src/BodyReader.cc
    src/BodyWriter.cc
    src/HttpClient.cc
    src/HttpContext.cc
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
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nitrocoro-http PUBLIC nitrocoro)

add_executable(http_stream_test tests/http_stream_test.cc)
target_link_libraries(http_stream_test PRIVATE nitrocoro-http)
add_test(NAME http_stream_test COMMAND http_stream_test)

add_executable(http_test tests/http_test.cc)
target_link_libraries(http_test PRIVATE nitrocoro-http)
add_test(NAME http_test COMMAND http_test)
