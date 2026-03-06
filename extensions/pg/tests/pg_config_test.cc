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

int main()
{
    return nitrocoro::test::run_all();
}
