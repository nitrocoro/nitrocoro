/**
 * @file PgConnection.cc
 * @brief PostgreSQL async connection implementation
 */
#include "nitrocoro/pg/PgConnection.h"
#include <libpq-fe.h>
#include <nitrocoro/utils/Debug.h>

#include <stdexcept>

namespace nitrocoro::pg
{

PgConnection::PgConnection(std::shared_ptr<PGconn> conn, std::unique_ptr<IoChannel> channel)
    : pgConn_(std::move(conn))
    , channel_(std::move(channel))
{
}

PgConnection::~PgConnection()
{
    // channel_->cancelAll();
    channel_->disableAll();
}

Task<std::unique_ptr<PgConnection>> PgConnection::connect(std::string connStr, Scheduler * scheduler)
{
    auto pgConn = std::shared_ptr<PGconn>(PQconnectStart(connStr.c_str()), PQfinish);
    if (!pgConn)
        throw std::runtime_error("PQconnectStart: out of memory");

    if (PQstatus(pgConn.get()) == CONNECTION_BAD)
        throw std::runtime_error("PgConnection::connect: " + std::string(PQerrorMessage(pgConn.get())));

    co_await scheduler->switch_to();
    auto channel = std::make_unique<IoChannel>(PQsocket(pgConn.get()), TriggerMode::EdgeTriggered, scheduler);
    channel->enableReading();

    auto connectResult = co_await channel->perform([&pgConn](int, IoChannel * ch) -> IoChannel::IoStatus {
        PostgresPollingStatusType s = PQconnectPoll(pgConn.get());
        NITRO_TRACE("PgConnection: PQconnectPoll=%d\n", (int)s);
        if (s == PGRES_POLLING_FAILED)
            return IoChannel::IoStatus::Error;
        if (s == PGRES_POLLING_WRITING)
        {
            ch->enableWriting();
            return IoChannel::IoStatus::NeedWrite;
        }
        if (s == PGRES_POLLING_READING)
        {
            ch->disableWriting();
            return IoChannel::IoStatus::NeedRead;
        }
        return IoChannel::IoStatus::Success; // PGRES_POLLING_OK
    });
    if (connectResult != IoChannel::IoResult::Success)
        throw std::runtime_error("PgConnection: handshake failed");
    NITRO_TRACE("PgConnection: connected (fd=%d)\n", PQsocket(pgConn.get()));
    if (PQsetnonblocking(pgConn.get(), 1) != 0)
        throw std::runtime_error("PQsetnonblocking: " + std::string(PQerrorMessage(pgConn.get())));
    co_return std::unique_ptr<PgConnection>(new PgConnection(std::move(pgConn), std::move(channel)));
}

bool PgConnection::isAlive() const
{
    return pgConn_ && PQstatus(pgConn_.get()) == CONNECTION_OK;
}

Task<PgResult> PgConnection::sendAndReceive(std::string_view sql, std::vector<PgValue> params)
{
    std::vector<std::string> strBufs;
    std::vector<const char *> paramValues;
    std::vector<int> paramLengths;
    std::vector<int> paramFormats;

    strBufs.reserve(params.size());
    paramValues.reserve(params.size());
    paramLengths.reserve(params.size());
    paramFormats.reserve(params.size());

    for (auto & v : params)
    {
        paramFormats.push_back(0);
        std::visit(
            [&](auto && arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                {
                    paramValues.push_back(nullptr);
                    paramLengths.push_back(0);
                }
                else if constexpr (std::is_same_v<T, bool>)
                {
                    strBufs.push_back(arg ? "t" : "f");
                    paramValues.push_back(strBufs.back().c_str());
                    paramLengths.push_back(static_cast<int>(strBufs.back().size()));
                }
                else if constexpr (std::is_same_v<T, int64_t>)
                {
                    strBufs.push_back(std::to_string(arg));
                    paramValues.push_back(strBufs.back().c_str());
                    paramLengths.push_back(static_cast<int>(strBufs.back().size()));
                }
                else if constexpr (std::is_same_v<T, double>)
                {
                    strBufs.push_back(std::to_string(arg));
                    paramValues.push_back(strBufs.back().c_str());
                    paramLengths.push_back(static_cast<int>(strBufs.back().size()));
                }
                else if constexpr (std::is_same_v<T, std::string>)
                {
                    paramValues.push_back(arg.c_str());
                    paramLengths.push_back(static_cast<int>(arg.size()));
                }
                else
                {
                    // bytea: send as binary
                    static_assert(std::is_same_v<T, std::vector<uint8_t>>);
                    paramValues.push_back(reinterpret_cast<const char *>(arg.data()));
                    paramLengths.push_back(static_cast<int>(arg.size()));
                    paramFormats.back() = 1; // binary
                }
            },
            v);
    }

    std::string sqlStr(sql);
    int ok = PQsendQueryParams(pgConn_.get(),
                               sqlStr.c_str(),
                               static_cast<int>(params.size()),
                               nullptr,
                               params.empty() ? nullptr : paramValues.data(),
                               params.empty() ? nullptr : paramLengths.data(),
                               params.empty() ? nullptr : paramFormats.data(),
                               0);
    if (!ok)
    {
        throw std::runtime_error(std::string("PQsendQueryParams: ") + PQerrorMessage(pgConn_.get()));
    }

    auto flushResult = co_await channel_->performWrite([this](int, IoChannel * c) -> IoChannel::IoStatus {
        int r = PQflush(pgConn_.get());
        NITRO_TRACE("PgConnection: PQflush=%d\n", r);
        if (r == 0)
        {
            c->disableWriting();
            return IoChannel::IoStatus::Success;
        }
        if (r > 0)
        {
            c->enableWriting();
            return IoChannel::IoStatus::NeedWrite;
        }
        return IoChannel::IoStatus::Error;
    });
    if (flushResult == IoChannel::IoResult::Error)
    {
        throw std::runtime_error(std::string("PQflush: ") + PQerrorMessage(pgConn_.get()));
    }
    if (flushResult != IoChannel::IoResult::Success)
    {
        throw std::runtime_error("PQflush: canceled");
    }

    std::shared_ptr<PGresult> res;
    auto readResult = co_await channel_->performRead([this, &res](int, IoChannel *) -> IoChannel::IoStatus {
        if (!PQconsumeInput(pgConn_.get()))
            return IoChannel::IoStatus::Error;
        NITRO_TRACE("PgConnection: PQisBusy=%d\n", PQisBusy(pgConn_.get()));
        if (PQisBusy(pgConn_.get()))
            return IoChannel::IoStatus::NeedRead;
        while (PGresult * r = PQgetResult(pgConn_.get()))
        {
            if (res)
            {
                // TODO
                NITRO_TRACE("PgConnection: dropping extra result status=%s rows=%d\n",
                            PQresStatus(PQresultStatus(res.get())),
                            PQntuples(res.get()));
            }
            res.reset(r, PQclear);
        }
        return IoChannel::IoStatus::Success;
    });
    if (readResult == IoChannel::IoResult::Error)
    {
        throw std::runtime_error(std::string("PQconsumeInput: ") + PQerrorMessage(pgConn_.get()));
    }
    if (readResult != IoChannel::IoResult::Success)
    {
        throw std::runtime_error("PgConnection: read canceled");
    }
    NITRO_TRACE("PgConnection: result received, res=%p\n", (void *)res.get());

    if (!res)
        throw std::runtime_error("PgConnection: no result returned");

    ExecStatusType status = PQresultStatus(res.get());
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK)
    {
        std::string err = PQresultErrorMessage(res.get());
        throw std::runtime_error("PgConnection query error: " + err);
    }

    co_return PgResult(std::move(res));
}

Task<PgResult> PgConnection::query(std::string_view sql, std::vector<PgValue> params)
{
    co_return co_await sendAndReceive(sql, std::move(params));
}

Task<> PgConnection::execute(std::string_view sql, std::vector<PgValue> params)
{
    co_await sendAndReceive(sql, std::move(params));
}

Task<> PgConnection::begin()
{
    co_await execute("BEGIN");
}

Task<> PgConnection::commit()
{
    co_await execute("COMMIT");
}

Task<> PgConnection::rollback()
{
    co_await execute("ROLLBACK");
}

} // namespace nitrocoro::pg
