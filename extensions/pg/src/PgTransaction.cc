/**
 * @file PgTransaction.cc
 * @brief PgTransaction implementation
 */
#include "nitrocoro/pg/PgTransaction.h"
#include "nitrocoro/pg/PgConnection.h"
#include "nitrocoro/pg/PgResult.h"
#include <nitrocoro/utils/Debug.h>

namespace nitrocoro::pg
{

// Static factory methods
Task<PgTransaction> PgTransaction::begin(PooledConnection && conn)
{
    auto scheduler = conn->scheduler();
    co_await conn->execute("BEGIN");
    co_return PgTransaction(std::move(conn), scheduler);
}

Task<PgTransaction> PgTransaction::begin(PgConnection && conn)
{
    auto scheduler = conn.scheduler();
    co_await conn.execute("BEGIN");
    co_return PgTransaction(std::move(conn), scheduler);
}

// Private constructors
PgTransaction::PgTransaction(PooledConnection conn, Scheduler * scheduler)
    : pooledConn_(std::move(conn)), scheduler_(scheduler)
{
    conn_ = pooledConn_.operator->();
}

PgTransaction::PgTransaction(PgConnection conn, Scheduler * scheduler)
    : ownedConn_(std::make_unique<PgConnection>(std::move(conn))), scheduler_(scheduler)
{
    conn_ = ownedConn_.get();
}

PgTransaction::PgTransaction(PgTransaction && other) noexcept
    : conn_(other.conn_)
    , pooledConn_(std::move(other.pooledConn_))
    , ownedConn_(std::move(other.ownedConn_))
    , scheduler_(other.scheduler_)
    , done_(other.done_)
{
    other.conn_ = nullptr;
    other.done_ = true;
}

PgTransaction & PgTransaction::operator=(PgTransaction && other) noexcept
{
    if (this != &other)
    {
        if (!done_ && conn_)
        {
            scheduler_->spawn([conn = conn_,
                               pooled = std::move(pooledConn_),
                               owned = std::move(ownedConn_)]() -> Task<> {
                try
                {
                    co_await conn->execute("ROLLBACK");
                    NITRO_TRACE("PgTransaction: auto rollback successful\n");
                }
                catch (const std::exception & e)
                {
                    NITRO_ERROR("PgTransaction: auto rollback failed: %s\n", e.what());
                }
            });
        }
        conn_ = other.conn_;
        pooledConn_ = std::move(other.pooledConn_);
        ownedConn_ = std::move(other.ownedConn_);
        scheduler_ = other.scheduler_;
        done_ = other.done_;
        other.conn_ = nullptr;
        other.done_ = true;
    }
    return *this;
}

PgTransaction::~PgTransaction()
{
    if (!done_ && conn_)
    {
        scheduler_->spawn([conn = conn_,
                           pooled = std::move(pooledConn_),
                           owned = std::move(ownedConn_)]() -> Task<> {
            try
            {
                co_await conn->execute("ROLLBACK");
                NITRO_TRACE("PgTransaction: auto rollback successful\n");
            }
            catch (const std::exception & e)
            {
                NITRO_ERROR("PgTransaction: auto rollback failed: %s\n", e.what());
            }
        });
    }
}

Task<PgResult> PgTransaction::query(std::string_view sql, std::vector<PgValue> params)
{
    if (!conn_)
    {
        throw std::logic_error("Transaction already finished");
    }
    co_return co_await conn_->query(sql, std::move(params));
}

Task<> PgTransaction::execute(std::string_view sql, std::vector<PgValue> params)
{
    if (!conn_)
    {
        throw std::logic_error("Transaction already finished");
    }
    co_await conn_->execute(sql, std::move(params));
}

Task<> PgTransaction::commit()
{
    if (!conn_)
    {
        throw std::logic_error("Transaction already finished");
    }
    co_await conn_->execute("COMMIT");
    done_ = true;
    conn_ = nullptr;
    pooledConn_.reset();
    ownedConn_.reset();
}

Task<> PgTransaction::rollback()
{
    if (!conn_)
    {
        throw std::logic_error("Transaction already finished");
    }
    co_await conn_->execute("ROLLBACK");
    done_ = true;
    conn_ = nullptr;
    pooledConn_.reset();
    ownedConn_.reset();
}

} // namespace nitrocoro::pg
