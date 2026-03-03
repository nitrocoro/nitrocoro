/**
 * @file pg_test.cc
 * @brief Tests for PgConnection basic functionality
 *
 * Requires a running PostgreSQL instance. Set the connection string via
 * environment variable PG_CONN_STR, e.g.:
 *   PG_CONN_STR="host=localhost dbname=test user=postgres password=secret" ./pg_test
 */
#include <nitrocoro/pg/PgConnection.h>
#include <nitrocoro/testing/Test.h>

#include <cstdlib>

using namespace nitrocoro;
using namespace nitrocoro::pg;

static std::string connStr()
{
    const char * env = std::getenv("PG_CONN_STR");
    return env ? env : "host=localhost dbname=test user=postgres";
}

NITRO_TEST(connect)
{
    auto conn = co_await PgConnection::connect(connStr());
    NITRO_REQUIRE(conn != nullptr);
    NITRO_CHECK(conn->isAlive());
}

NITRO_TEST(simple_query)
{
    auto conn = co_await PgConnection::connect(connStr());
    auto result = co_await conn->query("SELECT 1 AS val, 'hello' AS str");
    NITRO_CHECK_EQ(result.rowCount(), 1);
    NITRO_CHECK_EQ(result.colCount(), 2);
    NITRO_CHECK(result.colName(0) == std::string("val"));
    NITRO_CHECK(result.colName(1) == std::string("str"));
    NITRO_CHECK(std::get<int64_t>(result.get(0, 0)) == 1);
    NITRO_CHECK(std::get<std::string>(result.get(0, 1)) == "hello");
}

NITRO_TEST(execute)
{
    auto conn = co_await PgConnection::connect(connStr());
    co_await conn->execute("CREATE TEMP TABLE exec_test (v INT)");
    co_await conn->execute("INSERT INTO exec_test VALUES (1)");
    auto result = co_await conn->query("SELECT v FROM exec_test");
    NITRO_CHECK_EQ(std::get<int64_t>(result.get(0, 0)), 1);
}

NITRO_TEST(params)
{
    auto conn = co_await PgConnection::connect(connStr());
    std::vector<PgValue> params{ int64_t(3), int64_t(4) };
    auto result = co_await conn->query("SELECT $1::int8 + $2::int8 AS sum", std::move(params));
    NITRO_CHECK(std::get<int64_t>(result.get(0, 0)) == 7);
}

NITRO_TEST(null_value)
{
    auto conn = co_await PgConnection::connect(connStr());
    auto result = co_await conn->query("SELECT NULL::text AS n");
    NITRO_CHECK(std::holds_alternative<std::monostate>(result.get(0, 0)));
}

NITRO_TEST(bool_value)
{
    auto conn = co_await PgConnection::connect(connStr());
    auto result = co_await conn->query("SELECT true AS t, false AS f");
    NITRO_CHECK(std::get<bool>(result.get(0, 0)) == true);
    NITRO_CHECK(std::get<bool>(result.get(0, 1)) == false);
}

NITRO_TEST(double_value)
{
    auto conn = co_await PgConnection::connect(connStr());
    auto result = co_await conn->query("SELECT 3.14::float8 AS pi, -2.5::float8 AS neg");
    NITRO_CHECK(std::abs(std::get<double>(result.get(0, 0)) - 3.14) < 0.001);
    NITRO_CHECK(std::abs(std::get<double>(result.get(0, 1)) - (-2.5)) < 0.001);
}

NITRO_TEST(bytea_value)
{
    auto conn = co_await PgConnection::connect(connStr());
    auto result = co_await conn->query("SELECT '\\x010203'::bytea AS data");
    auto bytes = std::get<std::vector<uint8_t>>(result.get(0, 0));
    NITRO_CHECK_EQ(bytes.size(), 3);
    NITRO_CHECK_EQ(bytes[0], 1);
    NITRO_CHECK_EQ(bytes[1], 2);
    NITRO_CHECK_EQ(bytes[2], 3);
}

NITRO_TEST(multiple_rows)
{
    auto conn = co_await PgConnection::connect(connStr());
    auto result = co_await conn->query("SELECT generate_series(1, 3) AS n");
    NITRO_CHECK_EQ(result.rowCount(), 3);
    NITRO_CHECK_EQ(std::get<int64_t>(result.get(0, 0)), 1);
    NITRO_CHECK_EQ(std::get<int64_t>(result.get(1, 0)), 2);
    NITRO_CHECK_EQ(std::get<int64_t>(result.get(2, 0)), 3);
}

NITRO_TEST(result_copy)
{
    auto conn = co_await PgConnection::connect(connStr());
    auto result1 = co_await conn->query("SELECT 42 AS val, 'test' AS str");

    auto result2 = result1;
    NITRO_CHECK_EQ(result2.rowCount(), 1);
    NITRO_CHECK_EQ(result2.colCount(), 2);
    NITRO_CHECK_EQ(std::get<int64_t>(result2.get(0, 0)), 42);
    NITRO_CHECK_EQ(std::get<std::string>(result2.get(0, 1)), "test");

    PgResult result3;
    result3 = result1;
    NITRO_CHECK_EQ(result3.rowCount(), 1);
    NITRO_CHECK_EQ(std::get<int64_t>(result3.get(0, 0)), 42);
}

NITRO_TEST(transaction_basic)
{
    auto conn = co_await PgConnection::connect(connStr());
    co_await conn->execute("CREATE TEMP TABLE tx_test (v INT)");

    co_await conn->begin();
    co_await conn->execute("INSERT INTO tx_test VALUES (42)");
    co_await conn->rollback();
    auto result = co_await conn->query("SELECT COUNT(*) FROM tx_test");
    NITRO_CHECK(std::get<int64_t>(result.get(0, 0)) == 0);

    co_await conn->begin();
    co_await conn->execute("INSERT INTO tx_test VALUES (99)");
    co_await conn->commit();
    result = co_await conn->query("SELECT v FROM tx_test");
    NITRO_CHECK(std::get<int64_t>(result.get(0, 0)) == 99);
}

int main()
{
    return nitrocoro::test::run_all();
}
