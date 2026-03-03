find_package(PostgreSQL REQUIRED)

add_library(nitrocoro-pg STATIC
    src/PgConnection.cc
    src/PgPool.cc
    src/PgResult.cc
    src/PgTransaction.cc
)

target_include_directories(nitrocoro-pg PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nitrocoro-pg PUBLIC
    nitrocoro
    PostgreSQL::PostgreSQL
)

add_executable(pg_test tests/pg_test.cc)
target_link_libraries(pg_test PRIVATE nitrocoro-pg)
add_test(NAME pg_test COMMAND pg_test)

add_executable(pg_pool_test tests/pg_pool_test.cc)
target_link_libraries(pg_pool_test PRIVATE nitrocoro-pg)
add_test(NAME pg_pool_test COMMAND pg_pool_test)

add_executable(pg_transaction_test tests/pg_transaction_test.cc)
target_link_libraries(pg_transaction_test PRIVATE nitrocoro-pg)
add_test(NAME pg_transaction_test COMMAND pg_transaction_test)
