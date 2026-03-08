/**
 * @file addr_test.cc
 * @brief Tests for InetAddress and Url.
 */
#include <nitrocoro/net/InetAddress.h>
#include <nitrocoro/net/Url.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro::net;

// ── InetAddress ───────────────────────────────────────────────────────────────

/** IPv4 address construction and accessors. */
NITRO_TEST(inetaddr_ipv4)
{
    InetAddress addr("127.0.0.1", 8080);
    NITRO_CHECK_EQ(addr.toIp(), "127.0.0.1");
    NITRO_CHECK_EQ(addr.toPort(), 8080);
    NITRO_CHECK(!addr.isIpV6());
    NITRO_CHECK(addr.isLoopbackIp());
    co_return;
}

/** Port-only constructor binds to loopback. */
NITRO_TEST(inetaddr_port_only)
{
    InetAddress addr(9090, true);
    NITRO_CHECK_EQ(addr.toPort(), 9090);
    NITRO_CHECK(addr.isLoopbackIp());
    co_return;
}

/** toIpPort() formats as "ip:port". */
NITRO_TEST(inetaddr_to_ip_port)
{
    InetAddress addr("192.168.1.1", 443);
    NITRO_CHECK(addr.toIpPort() == "192.168.1.1:443");
    co_return;
}

// ── Url ───────────────────────────────────────────────────────────────────────

/** HTTP URL with path and query string is parsed correctly. */
NITRO_TEST(url_http)
{
    Url u("http://example.com/path?q=1");
    NITRO_REQUIRE(u.isValid());
    NITRO_CHECK(u.scheme() == "http");
    NITRO_CHECK(u.host() == "example.com");
    NITRO_CHECK_EQ(u.port(), 80);
    NITRO_CHECK(u.path() == "/path");
    NITRO_CHECK(u.query() == "q=1");
    co_return;
}

/** HTTPS URL with a non-default port is parsed correctly. */
NITRO_TEST(url_https_custom_port)
{
    Url u("https://api.example.com:8443/v1");
    NITRO_REQUIRE(u.isValid());
    NITRO_CHECK(u.scheme() == "https");
    NITRO_CHECK(u.host() == "api.example.com");
    NITRO_CHECK_EQ(u.port(), 8443);
    NITRO_CHECK(u.path() == "/v1");
    co_return;
}

/** URL without a path component defaults path to empty or root. */
NITRO_TEST(url_no_path)
{
    Url u("http://localhost:3000");
    NITRO_REQUIRE(u.isValid());
    NITRO_CHECK(u.host() == "localhost");
    NITRO_CHECK_EQ(u.port(), 3000);
    co_return;
}

/** A malformed URL is marked invalid. */
NITRO_TEST(url_invalid)
{
    Url u("not-a-url");
    NITRO_CHECK(!u.isValid());
    co_return;
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
