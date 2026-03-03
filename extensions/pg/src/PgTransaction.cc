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

PgTransaction::PgTransaction(PooledConnection conn, Scheduler * scheduler)
    : conn_(std::move(conn)), scheduler_(scheduler)
{
}

PgTransaction::~PgTransaction()
{
    if (!done_ && conn_)
    {
        scheduler_->spawn([conn = std::move(conn_)]() mutable -> Task<> {
            try
            {
                co_await conn->rollback();
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
    co_return co_await conn_->query(sql, std::move(params));
}

Task<> PgTransaction::execute(std::string_view sql, std::vector<PgValue> params)
{
    co_await conn_->execute(sql, std::move(params));
}

Task<> PgTransaction::commit()
{
    co_await conn_->commit();
    done_ = true;
}

Task<> PgTransaction::rollback()
{
    co_await conn_->rollback();
    done_ = true;
}

} // namespace nitrocoro::pg
