/**
 * @file pg_pool_test.cc
 * @brief Tests for PgPool and PooledConnection
 *
 * Requires a running PostgreSQL instance. Set the connection string via
 * environment variable PG_CONN_STR, e.g.:
 *   PG_CONN_STR="host=localhost dbname=test user=postgres password=secret" ./pg_pool_test
 */
#include <nitrocoro/pg/PgConnection.h>
#include <nitrocoro/pg/PgPool.h>
#include <nitrocoro/testing/Test.h>

#include <cstdlib>

using namespace nitrocoro;
using namespace nitrocoro::pg;

static std::string connStr()
{
    const char * env = std::getenv("PG_CONN_STR");
    return env ? env : "host=localhost dbname=test user=postgres";
}

Task<std::unique_ptr<PgConnection>> makeConn()
{
    co_return co_await PgConnection::connect(connStr());
}

NITRO_TEST(pool_acquire)
{
    PgPool pool(2, makeConn);
    auto conn = co_await pool.acquire();
    NITRO_CHECK(conn);
    auto result = co_await conn->query("SELECT 'pool' AS src");
    NITRO_CHECK(std::get<std::string>(result.get(0, 0)) == "pool");
}

NITRO_TEST(pool_reuse)
{
    PgPool pool(2, makeConn);
    {
        auto c1 = co_await pool.acquire();
        auto result = co_await c1->query("SELECT 1");
        NITRO_CHECK_EQ(std::get<int64_t>(result.get(0, 0)), 1);
    }
    co_await Scheduler::current()->sleep_for(0.5);
    NITRO_CHECK_EQ(pool.idleCount(), 1);
}

NITRO_TEST(pool_waiter)
{
    PgPool pool(1, makeConn);
    auto c1 = co_await pool.acquire();
    NITRO_CHECK_EQ(pool.idleCount(), 0);

    bool waiterRan = false;
    Scheduler::current()->spawn([TEST_CTX, &pool, &waiterRan]() mutable -> Task<> {
        auto c2 = co_await pool.acquire();
        waiterRan = true;
        auto result = co_await c2->query("SELECT 1");
        NITRO_CHECK_EQ(result.rowCount(), 1);
    });

    c1.reset();
    co_await Scheduler::current()->sleep_for(0.1);
    NITRO_CHECK(waiterRan);
}

NITRO_TEST(pooled_connection_reset)
{
    PgPool pool(1, makeConn);
    auto conn = co_await pool.acquire();
    NITRO_CHECK_EQ(pool.idleCount(), 0);

    conn.reset();
    co_await Scheduler::current()->sleep_for(0.1);
    NITRO_CHECK_EQ(pool.idleCount(), 1);
}

NITRO_TEST(pooled_connection_detach)
{
    PgPool pool(1, makeConn);
    auto pooled = co_await pool.acquire();
    NITRO_CHECK_EQ(pool.idleCount(), 0);

    auto detached = pooled.detach();
    NITRO_REQUIRE(detached != nullptr);
    NITRO_CHECK(!pooled);

    auto result = co_await detached->query("SELECT 42");
    NITRO_CHECK_EQ(std::get<int64_t>(result.get(0, 0)), 42);

    co_await Scheduler::current()->sleep_for(0.1);
    NITRO_CHECK_EQ(pool.idleCount(), 0);
}

NITRO_TEST(pooled_connection_move)
{
    PgPool pool(1, makeConn);
    auto c1 = co_await pool.acquire();
    auto result = co_await c1->query("SELECT 1");
    NITRO_CHECK_EQ(std::get<int64_t>(result.get(0, 0)), 1);

    auto c2 = std::move(c1);
    NITRO_CHECK(!c1);
    NITRO_CHECK(c2);

    result = co_await c2->query("SELECT 2");
    NITRO_CHECK_EQ(std::get<int64_t>(result.get(0, 0)), 2);
}

NITRO_TEST(pooled_connection_bool_operator)
{
    PgPool pool(1, makeConn);
    PooledConnection empty;
    NITRO_CHECK(!empty);

    auto conn = co_await pool.acquire();
    NITRO_CHECK(conn);

    conn.reset();
    NITRO_CHECK(!conn);
}

int main()
{
    return nitrocoro::test::run_all();
}
