add_executable(tcp_echo_server tcp_echo_server.cc)
target_link_libraries(tcp_echo_server PRIVATE nitrocoro)

add_executable(tcp_chat_server tcp_chat_server.cc)
target_link_libraries(tcp_chat_server PRIVATE nitrocoro)

add_executable(tcp_client tcp_client.cc)
target_link_libraries(tcp_client PRIVATE nitrocoro)

if(BUILD_EXT_HTTP)
    add_executable(http_server http_server.cc)
    target_link_libraries(http_server PRIVATE nitrocoro-http)

    add_executable(http_client http_client.cc)
    target_link_libraries(http_client PRIVATE nitrocoro-http)
endif()

if(BUILD_EXT_WEBSOCKET)
    add_executable(websocket_server websocket_server.cc)
    target_link_libraries(websocket_server PRIVATE nitrocoro-websocket)
endif()

if(BUILD_EXT_HTTP2)
    add_executable(http2_server http2_server.cc)
    target_link_libraries(http2_server PRIVATE nitrocoro-http2)

    add_executable(http2_client http2_client.cc)
    target_link_libraries(http2_client PRIVATE nitrocoro-http2)
endif()

if (BUILD_EXT_HTTPS)
    add_executable(https_client https_client.cc)
    target_link_libraries(https_client PRIVATE nitrocoro-https)
endif()
