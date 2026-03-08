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
