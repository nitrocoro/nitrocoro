/**
 * @file pg_pool_test.cc
 * @brief Tests for PgPool and PooledConnection
 *
 * Requires a running PostgreSQL instance. Set the connection string via
 * environment variable PG_CONN_STR, e.g.:
 *   PG_CONN_STR="host=localhost dbname=test user=postgres password=secret" ./pg_pool_test
 */
#include <nitrocoro/core/CancelToken.h>
#include <nitrocoro/pg/PgConnection.h>
#include <nitrocoro/pg/PgException.h>
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
        cfg.connect = PgConnectConfig::parseConnStr(env);
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

NITRO_TEST(pooled_connection_reset)
{
    PgPool pool(makeConfig(1));
    auto conn = co_await pool.acquire();
    NITRO_CHECK_EQ(pool.idleCount(), 0);

    conn.reset();
    co_await Scheduler::current()->sleep_for(0.1);
    NITRO_CHECK_EQ(pool.idleCount(), 1);
}

// acquire all slots, then verify further acquire waits until one is released
NITRO_TEST(pool_waiter_fifo)
{
    PgPool pool(makeConfig(2));
    auto c1 = co_await pool.acquire();
    auto c2 = co_await pool.acquire();

    std::vector<int> order;
    int remaining = 5;
    Promise<> done(Scheduler::current());
    auto doneFuture = done.get_future();

    auto makeWaiter = [&, TEST_CTX](int id) {
        Scheduler::current()->spawn([&, TEST_CTX, id]() mutable -> Task<> {
            auto c = co_await pool.acquire();
            order.push_back(id);
            auto result = co_await c->query("SELECT 1");
            NITRO_CHECK_EQ(std::get<int64_t>(result.get(0, 0)), 1);
            if (--remaining == 0)
                done.set_value();
        });
    };
    for (int i = 1; i <= 5; ++i)
        makeWaiter(i);

    c1.reset();
    c2.reset();
    co_await doneFuture.get();

    NITRO_REQUIRE_EQ(order.size(), 5u);
    for (int i = 0; i < 5; ++i)
        NITRO_CHECK_EQ(order[i], i + 1);
}

// cancelled waiter is skipped; next waiter receives the connection
NITRO_TEST(pool_acquire_cancelled_waiter_skipped)
{
    PgPool pool(makeConfig(1));
    auto c1 = co_await pool.acquire();

    Promise<> done;
    auto doneFuture = done.get_future();
    int remaining = 2;
    CancelSource src1;
    bool waiter1Got = false;
    bool waiter2Got = false;

    Scheduler::current()->spawn([&, TEST_CTX, token = src1.token()]() mutable -> Task<> {
        NITRO_CHECK_THROWS_AS(co_await pool.acquire(token), PgTimeoutError);
        waiter1Got = true;
        if (--remaining == 0)
            done.set_value();
    });
    Scheduler::current()->spawn([&, TEST_CTX]() mutable -> Task<> {
        auto c = co_await pool.acquire();
        waiter2Got = true;
        if (--remaining == 0)
            done.set_value();
    });

    src1.cancel();
    c1.reset();
    co_await doneFuture.get();

    NITRO_CHECK(waiter1Got);
    NITRO_CHECK(waiter2Got);
}

// acquire times out via CancelToken
NITRO_TEST(pool_acquire_timeout)
{
    PgPool pool(makeConfig(1));
    auto c1 = co_await pool.acquire();

    CancelSource src(Scheduler::current());
    src.cancelAfter(std::chrono::milliseconds(100));
    NITRO_CHECK_THROWS_AS(co_await pool.acquire(src.token()), PgTimeoutError);
}

// acquire times out via connectTimeoutMs config
NITRO_TEST(pool_acquire_default_timeout)
{
    auto cfg = makeConfig(1);
    cfg.connect.connectTimeoutMs = 1000;
    PgPool pool(cfg);
    auto c1 = co_await pool.acquire(); // exhaust the pool

    NITRO_CHECK_THROWS_AS(co_await pool.acquire(), PgTimeoutError);
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
