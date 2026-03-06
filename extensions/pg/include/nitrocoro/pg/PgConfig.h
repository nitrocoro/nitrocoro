/**
 * @file PgConfig.h
 * @brief PostgreSQL connection and pool configuration
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace nitrocoro::pg
{

struct PgConnectConfig
{
    std::string connStr; ///< if non-empty, used as-is (overrides all other fields)
    std::string host = "localhost";
    uint16_t port = 5432;
    std::string dbname;
    std::string user;
    std::string password;

    std::string toConnStr() const;
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
