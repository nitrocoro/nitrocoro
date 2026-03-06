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

static PgPoolConfig makeConfig(size_t maxSize = 2)
{
    PgPoolConfig cfg;
    const char * env = std::getenv("PG_CONN_STR");
    if (env)
        cfg.connect.connStr = env;
    else
    {
        cfg.connect.host = "localhost";
        cfg.connect.dbname = "test";
        cfg.connect.user = "postgres";
    }
    cfg.maxSize = maxSize;
    return cfg;
}

NITRO_TEST(pool_acquire)
{
    PgPool pool(makeConfig(2));
    auto conn = co_await pool.acquire();
    NITRO_CHECK(conn);
    auto result = co_await conn->query("SELECT 'pool' AS src");
    NITRO_CHECK(std::get<std::string>(result.get(0, 0)) == "pool");
}

NITRO_TEST(pool_reuse)
{
    PgPool pool(makeConfig(2));
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
    PgPool pool(makeConfig(1));
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
    PgPool pool(makeConfig(1));
    auto conn = co_await pool.acquire();
    NITRO_CHECK_EQ(pool.idleCount(), 0);

    conn.reset();
    co_await Scheduler::current()->sleep_for(0.1);
    NITRO_CHECK_EQ(pool.idleCount(), 1);
}

NITRO_TEST(pooled_connection_bool_operator)
{
    PgPool pool(makeConfig(1));
    auto conn = co_await pool.acquire();
    NITRO_CHECK(conn);

    conn.reset();
    NITRO_CHECK(!conn);
}

int main()
{
    return nitrocoro::test::run_all();
}
