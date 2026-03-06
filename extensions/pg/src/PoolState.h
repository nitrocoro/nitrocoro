/**
 * @file PoolState.h
 * @brief Internal pool state shared between PgPool and PooledConnection
 */
#pragma once

#include "PgConnectionImpl.h"
#include <nitrocoro/core/CancelToken.h>
#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>

#include <memory>
#include <queue>

namespace nitrocoro::pg
{

using nitrocoro::CancelRegistration;
using nitrocoro::CancelToken;
using nitrocoro::Promise;
using nitrocoro::Scheduler;
using nitrocoro::Task;

struct PoolState
{
    struct Waiter
    {
        Promise<std::unique_ptr<PgConnectionImpl>> promise;
        CancelRegistration cancelReg;
        bool cancelled{ false };
    };

    Scheduler * scheduler;
    size_t maxSize;
    size_t totalCount = 0;
    std::string connStr;
    int connectTimeoutMs = 0;
    std::queue<std::unique_ptr<PgConnectionImpl>> idle;
    std::queue<std::shared_ptr<Waiter>> waiters;

    static void dispatch(const std::shared_ptr<PoolState> & state);

    static void returnConnection(const std::weak_ptr<PoolState> & state,
                                 std::unique_ptr<PgConnectionImpl> conn) noexcept;
    static void detachConnection(const std::weak_ptr<PoolState> & state) noexcept;
};

} // namespace nitrocoro::pg
