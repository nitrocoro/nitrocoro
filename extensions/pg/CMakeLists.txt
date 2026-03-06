find_package(PostgreSQL REQUIRED)

add_library(nitrocoro-pg STATIC
    src/PgConnectionImpl.cc
    src/PgPool.cc
    src/PgResult.cc
    src/PgTransaction.cc
    src/PooledConnection.cc
    src/PoolState.cc
)

target_include_directories(nitrocoro-pg PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(nitrocoro-pg PUBLIC
    nitrocoro
    PostgreSQL::PostgreSQL
)

include(GNUInstallDirs)

install(TARGETS nitrocoro-pg
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
    add_executable(pg_test tests/pg_test.cc)
    target_link_libraries(pg_test PRIVATE nitrocoro-pg)
    add_test(NAME pg_test COMMAND pg_test)

    add_executable(pg_pool_test tests/pg_pool_test.cc)
    target_link_libraries(pg_pool_test PRIVATE nitrocoro-pg)
    add_test(NAME pg_pool_test COMMAND pg_pool_test)

    add_executable(pg_transaction_test tests/pg_transaction_test.cc)
    target_link_libraries(pg_transaction_test PRIVATE nitrocoro-pg)
    add_test(NAME pg_transaction_test COMMAND pg_transaction_test)

    add_executable(pg_config_test tests/pg_config_test.cc)
    target_link_libraries(pg_config_test PRIVATE nitrocoro-pg)
    add_test(NAME pg_config_test COMMAND pg_config_test)
endif ()
