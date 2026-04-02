add_compile_options(-Werror=return-type)

add_executable(self_test self_test.cc)
target_link_libraries(self_test PRIVATE nitrocoro)
add_test(NAME self_test COMMAND self_test)

add_executable(core_test core_test.cc)
target_link_libraries(core_test PRIVATE nitrocoro)
add_test(NAME core_test COMMAND core_test)

add_executable(sync_test sync_test.cc)
target_link_libraries(sync_test PRIVATE nitrocoro)
add_test(NAME sync_test COMMAND sync_test)

add_executable(addr_test addr_test.cc)
target_link_libraries(addr_test PRIVATE nitrocoro)
add_test(NAME addr_test COMMAND addr_test)

add_executable(dns_test dns_test.cc)
target_link_libraries(dns_test PRIVATE nitrocoro)
add_test(NAME dns_test COMMAND dns_test)

add_executable(tcp_test tcp_test.cc)
target_link_libraries(tcp_test PRIVATE nitrocoro)
add_test(NAME tcp_test COMMAND tcp_test)

add_executable(timeout_test timeout_test.cc)
target_link_libraries(timeout_test PRIVATE nitrocoro)
add_test(NAME timeout_test COMMAND timeout_test)

add_executable(cancel_test cancel_test.cc)
target_link_libraries(cancel_test PRIVATE nitrocoro)
add_test(NAME cancel_test COMMAND cancel_test)

add_executable(utils_test utils_test.cc)
target_link_libraries(utils_test PRIVATE nitrocoro)
add_test(NAME utils_test COMMAND utils_test)

add_executable(url_test url_test.cc)
target_link_libraries(url_test PRIVATE nitrocoro)
add_test(NAME url_test COMMAND url_test)

add_executable(net_url_test net_url_test.cc)
target_link_libraries(net_url_test PRIVATE nitrocoro)
add_test(NAME net_url_test COMMAND net_url_test)

add_subdirectory(stress)
