#pragma once

#include <nitrocoro/pg/PgConfig.h>
#include <nitrocoro/pg/PgPool.h>

#include <cstdlib>

namespace nitrocoro::pg::test
{

inline std::string connStr()
{
    const char * env = std::getenv("PG_CONN_STR");
    return env ? env : "host=localhost dbname=test user=postgres";
}

inline PgConnectConfig baseConfig()
{
    const char * env = std::getenv("PG_CONN_STR");
    if (env)
        return PgConnectConfig::parseConnStr(env);
    PgConnectConfig cfg;
    cfg.host = "localhost";
    cfg.dbname = "test";
    cfg.user = "postgres";
    return cfg;
}

inline PgPoolConfig makePoolConfig(size_t maxSize = 2)
{
    PgPoolConfig cfg;
    cfg.connect = baseConfig();
    cfg.maxSize = maxSize;
    return cfg;
}

} // namespace nitrocoro::pg::test
