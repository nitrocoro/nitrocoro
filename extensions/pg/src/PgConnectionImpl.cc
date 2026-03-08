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

struct PgConnectionImpl::ConnectionContext
{
    using ResultPromise = Promise<std::shared_ptr<PgResultWrapper>>;

    std::shared_ptr<PgConnWrapper> pgConn;
    std::unique_ptr<io::Channel> channel;
    bool broken{ false };
    std::weak_ptr<ResultPromise> weakPromise;
    std::function<void(std::string, std::string, int)> notifyHandler;
    std::function<void()> disconnectHandler;
};

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

PgConnectionImpl::PgConnectionImpl(std::shared_ptr<ConnectionContext> ctx)
    : ctx_(std::move(ctx))
{
    ctx_->channel->scheduler()->spawn([ctx = ctx_]() -> Task<> {
        co_await readLoop(ctx);
    });
}

PgConnectionImpl::~PgConnectionImpl()
{
    if (!ctx_)
        return;

    auto * sched = ctx_->channel->scheduler();
    sched->dispatch([ctx = std::move(ctx_)] {
        ctx->channel->cancelAll();
        ctx->channel->disableAll();
    });
}

Task<std::unique_ptr<PgConnectionImpl>> PgConnectionImpl::connect(const PgConnectConfig & config, Scheduler * scheduler)
{
    if (config.connectTimeoutMs <= 0)
        co_return co_await connect(config.toConnStr(), {}, scheduler);

    CancelSource timeoutSource(scheduler);
    timeoutSource.cancelAfter(std::chrono::microseconds(config.connectTimeoutMs));

    co_return co_await connect(config.toConnStr(), timeoutSource.token(), scheduler);
}

Task<std::unique_ptr<PgConnectionImpl>> PgConnectionImpl::connect(std::string connStr,
                                                                  CancelToken cancelToken,
                                                                  Scheduler * scheduler)
{
    auto pgConn = std::make_shared<PgConnectionImpl::PgConnWrapper>(PQconnectStart(connStr.c_str()));
    if (!pgConn->raw)
        throw PgConnectionError("PQconnectStart: out of memory");

    if (PQstatus(pgConn->raw) == CONNECTION_BAD)
        throw PgConnectionError("PgConnection::connect: " + std::string(PQerrorMessage(pgConn->raw)));

    int sockfd = PQsocket(pgConn->raw);
    if (sockfd < 0)
        throw PgConnectionError("PgConnection::connect: " + std::string(PQerrorMessage(pgConn->raw)));

    co_await scheduler->switch_to();
    auto channel = std::make_unique<io::Channel>(sockfd, TriggerMode::LevelTriggered, scheduler);
    channel->setGuard(pgConn);
    channel->enableReading();

    // auto un-reg when leaving scope
    auto reg = cancelToken.onCancel([ch = channel.get()] { ch->cancelAll(); });

    auto connectResult = co_await channel->perform([&](int, Channel * ch) -> Channel::IoStatus {
        if (cancelToken.isCancelled())
            return Channel::IoStatus::Error;
        PostgresPollingStatusType s = PQconnectPoll(pgConn->raw);
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
    co_return std::make_unique<PgConnectionImpl>(std::make_shared<ConnectionContext>(ConnectionContext{
        .pgConn = std::move(pgConn),
        .channel = std::move(channel),
    }));
}

struct PGnotifyDeleter
{
    void operator()(PGnotify * p) const { PQfreemem(p); }
};

Task<> PgConnectionImpl::readLoop(std::shared_ptr<ConnectionContext> ctx)
{
    while (true)
    {
        auto r = co_await ctx->channel->performRead([&](int, Channel *) -> Channel::IoStatus {
            if (!PQconsumeInput(ctx->pgConn->raw))
                return Channel::IoStatus::Error;

            while (auto n = std::unique_ptr<PGnotify, PGnotifyDeleter>(PQnotifies(ctx->pgConn->raw)))
            {
                if (ctx->notifyHandler)
                    ctx->notifyHandler({ n->relname }, { n->extra }, n->be_pid);
            }

            if (PQisBusy(ctx->pgConn->raw))
                return Channel::IoStatus::NeedRead;

            int cnt = 0;
            while (PGresult * raw = PQgetResult(ctx->pgConn->raw))
            {
                auto res = std::make_shared<PgResultWrapper>(raw);
                if (++cnt > 1)
                {
                    // TODO
                    NITRO_ERROR("PgConnection: drop extra result status=%s rows=%d",
                                PQresStatus(PQresultStatus(res->raw)),
                                PQntuples(res->raw));
                    continue;
                }
                if (auto p = ctx->weakPromise.lock())
                {
                    p->set_value(std::move(res));
                }
                else
                {
                    NITRO_ERROR("Consume ok but no promise waiting");
                }
            }
            return Channel::IoStatus::NeedRead;
        });

        NITRO_TRACE("read loop quit");
        if (r == Channel::IoResult::Canceled)
        {
            ctx->broken = true;
            if (auto p = ctx->weakPromise.lock())
            {
                p->set_exception(std::make_exception_ptr(
                    PgCancelledError("PgConnection: query canceled (read)")));
            }
            co_return;
        }
        if (r == Channel::IoResult::Error || r == Channel::IoResult::Eof)
        {
            ctx->broken = true;
            if (auto p = ctx->weakPromise.lock())
            {
                p->set_exception(std::make_exception_ptr(
                    PgConnectionError("PQconsumeInput: " + std::string(PQerrorMessage(ctx->pgConn->raw)))));
            }
            if (ctx->disconnectHandler)
                ctx->disconnectHandler();
            co_return;
        }

        // won't reach here
    }
}

Scheduler * PgConnectionImpl::scheduler() const
{
    return ctx_->channel->scheduler();
}

bool PgConnectionImpl::isAlive() const
{
    return !ctx_->broken && PQstatus(ctx_->pgConn->raw) == CONNECTION_OK;
}

Task<PgResult> PgConnectionImpl::sendAndReceive(std::string_view sql, std::vector<PgValue> params, CancelToken cancelToken)
{
    if (!ctx_->pgConn)
        throw PgConnectionError("PgConnection: operation on empty connection");

    co_await ctx_->channel->scheduler()->switch_to();
    if (cancelToken.isCancelled())
        throw PgCancelledError("PgConnection: query cancelled");
    auto reg = cancelToken.onCancel([ch = ctx_->channel.get()] {
        ch->cancelAll();
    });

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

    auto resultPromise = std::make_shared<ConnectionContext::ResultPromise>();
    ctx_->weakPromise = resultPromise;

    std::string sqlStr(sql);
    int ok = PQsendQueryParams(ctx_->pgConn->raw,
                               sqlStr.c_str(),
                               static_cast<int>(params.size()),
                               nullptr,
                               params.empty() ? nullptr : paramValues.data(),
                               params.empty() ? nullptr : paramLengths.data(),
                               params.empty() ? nullptr : paramFormats.data(),
                               0);
    if (!ok)
    {
        ctx_->broken = true;
        throw PgConnectionError(std::string("PQsendQueryParams: ") + PQerrorMessage(ctx_->pgConn->raw));
    }

    auto flushResult = co_await ctx_->channel->performWrite([&](int, Channel * c) -> Channel::IoStatus {
        int r = PQflush(ctx_->pgConn->raw);
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
        ctx_->broken = true;
        throw PgConnectionError(std::string("PQflush: ") + PQerrorMessage(ctx_->pgConn->raw));
    }
    if (flushResult != Channel::IoResult::Success)
    {
        ctx_->broken = true;
        throw PgCancelledError("PgConnection: query canceled (flush)");
    }

    if (cancelToken.isCancelled())
    {
        ctx_->broken = true;
        throw PgCancelledError("PgConnection: query canceled (read)");
    }

    auto res = co_await resultPromise->get_future().get();
    reg.unregister(); // prevent cancel
    resultPromise.reset();

    if (!res)
    {
        ctx_->broken = true;
        throw PgConnectionError("PgConnection: no result returned");
    }

    ExecStatusType status = PQresultStatus(res->raw);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK)
    {
        std::string err = PQresultErrorMessage(res->raw);
        const char * sqlstate = PQresultErrorField(res->raw, PG_DIAG_SQLSTATE);
        throw PgQueryError("PgConnection query error: " + err, sqlstate ? sqlstate : "");
    }

    co_return PgResult(std::move(res));
}

Task<PgResult> PgConnectionImpl::query(std::string_view sql, std::vector<PgValue> params, CancelToken cancelToken)
{
    co_return co_await sendAndReceive(sql, std::move(params), cancelToken);
}

Task<> PgConnectionImpl::execute(std::string_view sql, std::vector<PgValue> params, CancelToken cancelToken)
{
    co_await sendAndReceive(sql, std::move(params), cancelToken);
}

} // namespace nitrocoro::pg
