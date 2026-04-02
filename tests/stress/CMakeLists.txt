if(BUILD_EXT_HTTP)
    add_executable(stress_server stress_server.cc)
    target_link_libraries(stress_server PRIVATE nitrocoro-http)

    add_executable(stress_client stress_client.cc)
    target_link_libraries(stress_client PRIVATE nitrocoro-http)
endif()
