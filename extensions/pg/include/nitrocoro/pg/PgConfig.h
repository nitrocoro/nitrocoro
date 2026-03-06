/**
 * @file PgConfig.h
 * @brief PostgreSQL connection and pool configuration
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace nitrocoro::pg
{

struct PgConnectConfig
{
    std::string host = "localhost";
    uint16_t port = 5432;
    std::string dbname;
    std::string user;
    std::string password;

    int connectTimeoutMs = 0;                              ///< 0 = disabled; max time to establish a connection
    int statementTimeoutMs = 0;                           ///< 0 = disabled; maps to statement_timeout conn option (ms)
    int lockTimeoutMs = 0;                                ///< 0 = disabled; maps to lock_timeout conn option (ms)
    std::string applicationName;                          ///< maps to application_name conn option
    std::unordered_map<std::string, std::string> options; ///< arbitrary conn options (-c key=value); overrides predefined fields if same key

    std::string toConnStr() const;
    static PgConnectConfig parseConnStr(const std::string & s);
};

struct PgPoolConfig
{
    PgConnectConfig connect;
    size_t maxSize = 10; ///< maximum number of connections in the pool

    // Future parameters (not yet implemented):
    // size_t minIdle          = 0;   ///< minimum idle connections to keep alive
    // int    idleTimeoutSecs  = 600; ///< close idle connections older than this
    // int    maxLifetimeSecs  = 0;   ///< recycle connections older than this (0 = unlimited)
    // int    acquireTimeoutMs = 0;   ///< max wait time for acquire() (0 = unlimited)
};

} // namespace nitrocoro::pg
