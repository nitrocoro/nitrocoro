/**
 * @file PgConnectionImpl.cc
 * @brief PostgreSQL async connection implementation
 */
#include "PgConnectionImpl.h"

#include "PgResultWrapper.h"
#include <nitrocoro/core/CancelToken.h>
#include <nitrocoro/pg/PgConfig.h>
#include <nitrocoro/pg/PgException.h>
#include <nitrocoro/utils/Debug.h>

#include <libpq-fe.h>

namespace nitrocoro::pg
{

using nitrocoro::io::Channel;

std::string PgConnectConfig::toConnStr() const
{
    std::string s;
    s += "host='" + host + "' ";
    s += "port='" + std::to_string(port) + "' ";
    if (!dbname.empty())
        s += "dbname='" + dbname + "' ";
    if (!user.empty())
        s += "user='" + user + "' ";
    if (!password.empty())
        s += "password='" + password + "' ";

    // Merge predefined fields into connOptions; explicit options override predefined fields
    std::unordered_map<std::string, std::string> connOptions;
    if (statementTimeoutMs > 0)
        connOptions["statement_timeout"] = std::to_string(statementTimeoutMs) + "ms";
    if (lockTimeoutMs > 0)
        connOptions["lock_timeout"] = std::to_string(lockTimeoutMs) + "ms";
    if (!applicationName.empty())
        connOptions["application_name"] = applicationName;
    for (auto & [k, v] : options)
        connOptions[k] = v;

    if (!connOptions.empty())
    {
        std::string opts;
        for (auto & [k, v] : connOptions)
            opts += "-c " + k + "=" + v + " ";
        s += "options='" + opts + "' ";
    }
    return s;
}

PgConnectConfig PgConnectConfig::parseConnStr(const std::string & s)
{
    char * errmsg = nullptr;
    PQconninfoOption * opts = PQconninfoParse(s.c_str(), &errmsg);
    if (!opts)
    {
        std::string err = errmsg ? errmsg : "unknown error";
        PQfreemem(errmsg);
        throw PgConnectionError("PgConnectConfig::parseConnStr: " + err);
    }

    PgConnectConfig cfg;
    for (PQconninfoOption * o = opts; o->keyword; ++o)
    {
        if (!o->val)
            continue;
        std::string_view kw(o->keyword);
        std::string_view val(o->val);
        if (kw == "host")
            cfg.host = val;
        else if (kw == "port")
            cfg.port = static_cast<uint16_t>(std::stoi(std::string(val)));
        else if (kw == "dbname")
            cfg.dbname = val;
        else if (kw == "user")
            cfg.user = val;
        else if (kw == "password")
            cfg.password = val;
        else if (kw == "options")
        {
            // parse "-c key=value" pairs from the options string
            std::string optStr(val);
            size_t pos = 0;
            while (pos < optStr.size())
            {
                // skip whitespace
                while (pos < optStr.size() && optStr[pos] == ' ')
                    ++pos;
                if (pos + 2 < optStr.size() && optStr.substr(pos, 2) == "-c")
                {
                    pos += 2;
                    while (pos < optStr.size() && optStr[pos] == ' ')
                        ++pos;
                    size_t eq = optStr.find('=', pos);
                    size_t sp = optStr.find(' ', pos);
                    if (eq != std::string::npos)
                    {
                        std::string key = optStr.substr(pos, eq - pos);
                        std::string v = optStr.substr(eq + 1, sp == std::string::npos ? sp : sp - eq - 1);
                        cfg.options[key] = v;
                        pos = sp == std::string::npos ? optStr.size() : sp;
                    }
                }
                else
                    break;
            }
        }
    }
    PQconninfoFree(opts);
    return cfg;
}

struct PgConnectionImpl::PgConnWrapper
{
    PGconn * raw;

    explicit PgConnWrapper(PGconn * raw)
        : raw(raw) {}
    ~PgConnWrapper()
    {
        if (raw)
            PQfinish(raw);
    }
};

PgConnectionImpl::PgConnectionImpl(std::shared_ptr<PgConnWrapper> conn, std::unique_ptr<Channel> channel)
    : pgConn_(std::move(conn))
    , channel_(std::move(channel))
{
}

Task<std::unique_ptr<PgConnectionImpl>> PgConnectionImpl::connect(const PgConnectConfig & config, Scheduler * scheduler)
{
    if (config.connectTimeoutMs <= 0)
        co_return co_await connect(config.toConnStr(), scheduler);

    CancelSource timeoutSource(scheduler);
    timeoutSource.cancelAfter(std::chrono::microseconds(config.connectTimeoutMs));

    co_return co_await connect(config.toConnStr(), scheduler, timeoutSource.token());
}

Task<std::unique_ptr<PgConnectionImpl>> PgConnectionImpl::connect(std::string connStr,
                                                                  Scheduler * scheduler,
                                                                  CancelToken cancelToken)
{
    auto pgConn = std::make_shared<PgConnectionImpl::PgConnWrapper>(PQconnectStart(connStr.c_str()));
    if (!pgConn->raw)
        throw PgConnectionError("PQconnectStart: out of memory");

    if (PQstatus(pgConn->raw) == CONNECTION_BAD)
        throw PgConnectionError("PgConnection::connect: " + std::string(PQerrorMessage(pgConn->raw)));

    co_await scheduler->switch_to();
    auto channel = std::make_unique<io::Channel>(PQsocket(pgConn->raw), TriggerMode::LevelTriggered, scheduler);
    channel->setGuard(pgConn);
    channel->enableReading();

    // auto un-reg when leaving scope
    auto reg = cancelToken.onCancel([ch = channel.get()] { ch->cancelAll(); });

    auto connectResult = co_await channel->perform([&pgConn, &cancelToken](int, Channel * ch) -> Channel::IoStatus {
        if (cancelToken.isCancelled())
            return Channel::IoStatus::Error;
        PostgresPollingStatusType s = PQconnectPoll(pgConn->raw);
        NITRO_TRACE("PgConnection: PQconnectPoll=%d", (int)s);
        if (s == PGRES_POLLING_FAILED)
            return Channel::IoStatus::Error;
        if (s == PGRES_POLLING_WRITING)
        {
            ch->enableWriting();
            return Channel::IoStatus::NeedWrite;
        }
        if (s == PGRES_POLLING_READING)
        {
            ch->disableWriting();
            return Channel::IoStatus::NeedRead;
        }
        return Channel::IoStatus::Success; // PGRES_POLLING_OK
    });
    if (connectResult == Channel::IoResult::Canceled || cancelToken.isCancelled())
        throw PgTimeoutError("PgConnection: connect timed out");
    if (connectResult != Channel::IoResult::Success)
        throw PgConnectionError("PgConnection: handshake failed");
    NITRO_TRACE("PgConnection: connected (fd=%d)", PQsocket(pgConn->raw));
    if (PQsetnonblocking(pgConn->raw, 1) != 0)
        throw PgConnectionError("PQsetnonblocking: " + std::string(PQerrorMessage(pgConn->raw)));
    co_return std::make_unique<PgConnectionImpl>(std::move(pgConn), std::move(channel));
}

Task<std::unique_ptr<PgConnection>> PgConnection::connect(const PgConnectConfig & config, Scheduler * scheduler)
{
    co_return co_await PgConnectionImpl::connect(config, scheduler);
}

Task<std::unique_ptr<PgConnection>> PgConnection::connect(std::string connStr, Scheduler * scheduler)
{
    co_return co_await PgConnectionImpl::connect(std::move(connStr), scheduler);
}

Scheduler * PgConnectionImpl::scheduler() const
{
    return channel_->scheduler();
}

bool PgConnectionImpl::isAlive() const
{
    return PQstatus(pgConn_->raw) == CONNECTION_OK;
}

Task<PgResult> PgConnectionImpl::sendAndReceive(std::string_view sql, std::vector<PgValue> params)
{
    if (!pgConn_)
        throw PgConnectionError("PgConnection: operation on empty connection");

    std::vector<std::string> strBuffers;
    std::vector<const char *> paramValues;
    std::vector<int> paramLengths;
    std::vector<int> paramFormats;

    strBuffers.reserve(params.size());
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
                    strBuffers.push_back(arg ? "t" : "f");
                    paramValues.push_back(strBuffers.back().c_str());
                    paramLengths.push_back(static_cast<int>(strBuffers.back().size()));
                }
                else if constexpr (std::is_same_v<T, int64_t>)
                {
                    strBuffers.push_back(std::to_string(arg));
                    paramValues.push_back(strBuffers.back().c_str());
                    paramLengths.push_back(static_cast<int>(strBuffers.back().size()));
                }
                else if constexpr (std::is_same_v<T, double>)
                {
                    strBuffers.push_back(std::to_string(arg));
                    paramValues.push_back(strBuffers.back().c_str());
                    paramLengths.push_back(static_cast<int>(strBuffers.back().size()));
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
    int ok = PQsendQueryParams(pgConn_->raw,
                               sqlStr.c_str(),
                               static_cast<int>(params.size()),
                               nullptr,
                               params.empty() ? nullptr : paramValues.data(),
                               params.empty() ? nullptr : paramLengths.data(),
                               params.empty() ? nullptr : paramFormats.data(),
                               0);
    if (!ok)
        throw PgConnectionError(std::string("PQsendQueryParams: ") + PQerrorMessage(pgConn_->raw));

    auto flushResult = co_await channel_->performWrite([this](int, Channel * c) -> Channel::IoStatus {
        int r = PQflush(pgConn_->raw);
        NITRO_TRACE("PgConnection: PQflush=%d", r);
        if (r == 0)
        {
            c->disableWriting();
            return Channel::IoStatus::Success;
        }
        if (r > 0)
        {
            c->enableWriting();
            return Channel::IoStatus::NeedWrite;
        }
        return Channel::IoStatus::Error;
    });
    if (flushResult == Channel::IoResult::Error)
    {
        throw PgConnectionError(std::string("PQflush: ") + PQerrorMessage(pgConn_->raw));
    }
    if (flushResult != Channel::IoResult::Success)
    {
        throw PgConnectionError("PQflush: canceled");
    }

    std::shared_ptr<PgResultWrapper> res;
    auto readResult = co_await channel_->performRead([this, &res](int, Channel *) -> Channel::IoStatus {
        if (!PQconsumeInput(pgConn_->raw))
            return Channel::IoStatus::Error;
        NITRO_TRACE("PgConnection: PQisBusy=%d", PQisBusy(pgConn_->raw));
        if (PQisBusy(pgConn_->raw))
            return Channel::IoStatus::NeedRead;
        while (PGresult * r = PQgetResult(pgConn_->raw))
        {
            if (res)
            {
                // TODO
                NITRO_TRACE("PgConnection: dropping extra result status=%s rows=%d",
                            PQresStatus(PQresultStatus(res->raw)),
                            PQntuples(res->raw));
            }
            res = std::make_shared<PgResultWrapper>(r);
        }
        return Channel::IoStatus::Success;
    });
    if (readResult == Channel::IoResult::Error)
    {
        throw PgConnectionError(std::string("PQconsumeInput: ") + PQerrorMessage(pgConn_->raw));
    }
    if (readResult != Channel::IoResult::Success)
    {
        throw PgConnectionError("PgConnection: read canceled");
    }
    NITRO_TRACE("PgConnection: result received, res=%p", (void *)res.get());

    if (!res)
        throw PgConnectionError("PgConnection: no result returned");

    ExecStatusType status = PQresultStatus(res->raw);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK)
    {
        std::string err = PQresultErrorMessage(res->raw);
        throw PgQueryError("PgConnection query error: " + err);
    }

    co_return PgResult(std::move(res));
}

Task<PgResult> PgConnectionImpl::query(std::string_view sql, std::vector<PgValue> params)
{
    co_return co_await sendAndReceive(sql, std::move(params));
}

Task<> PgConnectionImpl::execute(std::string_view sql, std::vector<PgValue> params)
{
    co_await sendAndReceive(sql, std::move(params));
}

} // namespace nitrocoro::pg
