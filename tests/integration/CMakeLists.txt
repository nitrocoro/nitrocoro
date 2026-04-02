# Integration tests requiring multiple extensions

# HTTPS integration test (requires both HTTP and TLS)
if(TARGET nitrocoro-http AND TARGET nitrocoro-tls)
    add_executable(https_integration_test https_test.cc)
    target_link_libraries(https_integration_test PRIVATE 
        nitrocoro-http 
        nitrocoro-tls
        nitrocoro-https
    )
    
    # OpenSSL is needed for test certificate generation
    find_package(OpenSSL REQUIRED)
    target_link_libraries(https_integration_test PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    
    add_test(NAME https_integration_test COMMAND https_integration_test)
    
    message(STATUS "Building HTTPS integration tests")
else()
    message(STATUS "Skipping HTTPS integration tests (requires both nitrocoro-http and nitrocoro-tls)")
endif()
