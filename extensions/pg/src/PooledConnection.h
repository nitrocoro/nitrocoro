/**
 * @file PooledConnection.h
 * @brief RAII handle for a connection borrowed from PgPool
 */
#pragma once

#include <nitrocoro/pg/PgConnection.h>

#include <memory>

namespace nitrocoro::pg
{

struct PoolState;
class PgConnectionImpl;

class PooledConnection : public PgConnection
{
public:
    PooledConnection(std::unique_ptr<PgConnectionImpl> impl, std::weak_ptr<PoolState> state);
    ~PooledConnection() override;

    PooledConnection(const PooledConnection &) = delete;
    PooledConnection & operator=(const PooledConnection &) = delete;
    PooledConnection(PooledConnection &&) = delete;
    PooledConnection & operator=(PooledConnection &&) = delete;

    Scheduler * scheduler() const override;
    bool isAlive() const override;
    Task<PgResult> query(std::string_view sql, std::vector<PgValue> params) override;
    Task<> execute(std::string_view sql, std::vector<PgValue> params) override;

private:
    std::unique_ptr<PgConnectionImpl> impl_;
    std::weak_ptr<PoolState> state_;
};

} // namespace nitrocoro::pg
