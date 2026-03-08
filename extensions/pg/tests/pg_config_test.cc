/**
 * @file pg_config_test.cc
 * @brief Unit tests for PgConnectConfig::toConnStr()
 */
#include <nitrocoro/pg/PgConfig.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro::pg;

NITRO_TEST(toConnStr_defaults)
{
    PgConnectConfig cfg;
    auto s = cfg.toConnStr();
    NITRO_CHECK(s.find("host='localhost'") != std::string::npos);
    NITRO_CHECK(s.find("port='5432'") != std::string::npos);
    // optional fields absent when empty
    NITRO_CHECK(s.find("dbname=") == std::string::npos);
    NITRO_CHECK(s.find("user=") == std::string::npos);
    NITRO_CHECK(s.find("password=") == std::string::npos);

    co_return;
}

NITRO_TEST(toConnStr_all_fields)
{
    PgConnectConfig cfg;
    cfg.host = "db.example.com";
    cfg.port = 5433;
    cfg.dbname = "mydb";
    cfg.user = "alice";
    cfg.password = "s3cr3t";

    auto s = cfg.toConnStr();
    NITRO_CHECK(s.find("host='db.example.com'") != std::string::npos);
    NITRO_CHECK(s.find("port='5433'") != std::string::npos);
    NITRO_CHECK(s.find("dbname='mydb'") != std::string::npos);
    NITRO_CHECK(s.find("user='alice'") != std::string::npos);
    NITRO_CHECK(s.find("password='s3cr3t'") != std::string::npos);

    co_return;
}

NITRO_TEST(toConnStr_partial_fields)
{
    PgConnectConfig cfg;
    cfg.dbname = "testdb";
    // user and password left empty

    auto s = cfg.toConnStr();
    NITRO_CHECK(s.find("dbname='testdb'") != std::string::npos);
    NITRO_CHECK(s.find("user=") == std::string::npos);
    NITRO_CHECK(s.find("password=") == std::string::npos);

    co_return;
}

NITRO_TEST(toConnStr_statement_timeout)
{
    PgConnectConfig cfg;
    cfg.statementTimeoutMs = 5000;

    auto s = cfg.toConnStr();
    NITRO_CHECK(s.find("statement_timeout=5000ms") != std::string::npos);
    NITRO_CHECK(s.find("options=") != std::string::npos);

    co_return;
}

NITRO_TEST(toConnStr_lock_timeout)
{
    PgConnectConfig cfg;
    cfg.lockTimeoutMs = 2000;

    auto s = cfg.toConnStr();
    NITRO_CHECK(s.find("lock_timeout=2000ms") != std::string::npos);

    co_return;
}

NITRO_TEST(toConnStr_options_override_predefined)
{
    PgConnectConfig cfg;
    cfg.statementTimeoutMs = 5000;
    cfg.options["statement_timeout"] = "9999ms"; // explicit override

    auto s = cfg.toConnStr();
    NITRO_CHECK(s.find("statement_timeout=9999ms") != std::string::npos);
    NITRO_CHECK(s.find("statement_timeout=5000ms") == std::string::npos);

    co_return;
}

NITRO_TEST(toConnStr_application_name)
{
    PgConnectConfig cfg;
    cfg.applicationName = "myapp";

    auto s = cfg.toConnStr();
    NITRO_CHECK(s.find("application_name=myapp") != std::string::npos);

    co_return;
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
