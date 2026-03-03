/**
 * @file pg_transaction_test.cc
 * @brief Tests for PgTransaction
 *
 * Requires a running PostgreSQL instance. Set the connection string via
 * environment variable PG_CONN_STR, e.g.:
 *   PG_CONN_STR="host=localhost dbname=test user=postgres password=secret" ./pg_transaction_test
 */
#include <nitrocoro/pg/PgConnection.h>
#include <nitrocoro/pg/PgPool.h>
#include <nitrocoro/pg/PgTransaction.h>
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

NITRO_TEST(transaction_raii_rollback)
{
    PgPool pool(1, makeConn);
    co_await (co_await pool.acquire())->execute("CREATE TEMP TABLE tx_raii_test (v INT)");
    {
        auto tx = co_await pool.newTransaction();
        co_await tx.execute("INSERT INTO tx_raii_test VALUES (1)");
    }
    co_await Scheduler::current()->sleep_for(1.0);
    auto conn = co_await pool.acquire();
    auto result = co_await conn->query("SELECT COUNT(*) FROM tx_raii_test");
    NITRO_CHECK(std::get<int64_t>(result.get(0, 0)) == 0);
}

NITRO_TEST(transaction_commit)
{
    PgPool pool(1, makeConn);
    co_await (co_await pool.acquire())->execute("CREATE TEMP TABLE tx_commit_test (v INT)");
    {
        auto tx = co_await pool.newTransaction();
        co_await tx.execute("INSERT INTO tx_commit_test VALUES (42)");
        co_await tx.commit();
    }
    auto conn = co_await pool.acquire();
    auto result = co_await conn->query("SELECT v FROM tx_commit_test");
    NITRO_CHECK(std::get<int64_t>(result.get(0, 0)) == 42);
}

NITRO_TEST(transaction_rollback)
{
    PgPool pool(1, makeConn);
    co_await (co_await pool.acquire())->execute("CREATE TEMP TABLE tx_rollback_test (v INT)");
    {
        auto tx = co_await pool.newTransaction();
        co_await tx.execute("INSERT INTO tx_rollback_test VALUES (99)");
        co_await tx.rollback();
    }
    auto conn = co_await pool.acquire();
    auto result = co_await conn->query("SELECT COUNT(*) FROM tx_rollback_test");
    NITRO_CHECK(std::get<int64_t>(result.get(0, 0)) == 0);
}

NITRO_TEST(transaction_query)
{
    PgPool pool(1, makeConn);
    co_await (co_await pool.acquire())->execute("CREATE TEMP TABLE tx_query_test (v INT)");
    {
        auto tx = co_await pool.newTransaction();
        co_await tx.execute("INSERT INTO tx_query_test VALUES (1), (2), (3)");
        auto result = co_await tx.query("SELECT SUM(v) FROM tx_query_test");
        NITRO_CHECK_EQ(result.rowCount(), 1);
        NITRO_CHECK_EQ(result.colCount(), 1);
        NITRO_CHECK_EQ(std::get<int64_t>(result.get(0, 0)), 6);
        co_await tx.commit();
    }
}

NITRO_TEST(transaction_pool_return)
{
    PgPool pool(1, makeConn);
    {
        auto tx = co_await pool.newTransaction();
        NITRO_CHECK_EQ(pool.idleCount(), 0);
        co_await tx.rollback();
    }
    co_await Scheduler::current()->sleep_for(0.5);
    NITRO_CHECK_EQ(pool.idleCount(), 1);
}

NITRO_TEST(transaction_move)
{
    PgPool pool(1, makeConn);
    co_await (co_await pool.acquire())->execute("CREATE TEMP TABLE tx_move_test (v INT)");

    {
        auto tx1 = co_await pool.newTransaction();
        NITRO_CHECK_EQ(pool.idleCount(), 0);
        co_await tx1.execute("INSERT INTO tx_move_test VALUES (10)");

        auto tx2 = std::move(tx1);
        NITRO_CHECK_EQ(pool.idleCount(), 0);
        co_await tx2.execute("INSERT INTO tx_move_test VALUES (20)");
        co_await tx2.commit();
    }
    co_await Scheduler::current()->sleep_for(0.5);
    NITRO_CHECK_EQ(pool.idleCount(), 1);

    auto conn = co_await pool.acquire();
    auto result = co_await conn->query("SELECT SUM(v) FROM tx_move_test");
    NITRO_CHECK_EQ(std::get<int64_t>(result.get(0, 0)), 30);
}

NITRO_TEST(transaction_move_assignment)
{
    PgPool pool(2, makeConn);
    {
        auto setupConn = co_await pool.acquire();
        co_await setupConn->execute("DROP TABLE IF EXISTS tx_assign_test");
        co_await setupConn->execute("CREATE TABLE tx_assign_test (v INT)");
    }
    co_await Scheduler::current()->sleep_for(0.5);

    {
        // tx1 inserts value 1 (uncommitted)
        auto tx1 = co_await pool.newTransaction();
        co_await tx1.execute("INSERT INTO tx_assign_test VALUES (1)");

        // tx2 inserts value 2 (uncommitted)
        auto tx2 = co_await pool.newTransaction();
        co_await tx2.execute("INSERT INTO tx_assign_test VALUES (2)");

        // Move assignment: if not properly implemented, tx1's connection will be returned
        // to pool WITHOUT rollback, leaving an uncommitted transaction that may auto-commit
        tx1 = std::move(tx2);
        co_await Scheduler::current()->sleep_for(0.5);

        // Commit tx1 (which now holds tx2's transaction)
        co_await tx1.commit();
    }
    co_await Scheduler::current()->sleep_for(0.5);

    // Without proper move assignment: one connection left in uncommitted state (BUG)
    // With proper move assignment: only value 2 is persisted (value 1 rolled back)
    {
        auto conn1 = co_await pool.acquire();
        auto result1 = co_await conn1->query("SELECT v FROM tx_assign_test ORDER BY v");
        auto conn2 = co_await pool.acquire();
        auto result2 = co_await conn2->query("SELECT v FROM tx_assign_test ORDER BY v");

        NITRO_CHECK_EQ(result1.rowCount(), 1);
        NITRO_CHECK_EQ(result2.rowCount(), 1);
        NITRO_CHECK_EQ(std::get<int64_t>(result1.get(0, 0)), 2);
        NITRO_CHECK_EQ(std::get<int64_t>(result2.get(0, 0)), 2);
    }

    {
        auto conn = co_await pool.acquire();
        co_await conn->execute("DROP TABLE tx_assign_test");
    }
}

int main()
{
    return nitrocoro::test::run_all();
}
