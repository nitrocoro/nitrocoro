/**
 * @file dns_test.cc
 * @brief Tests for DnsResolver. Requires network access.
 */
#include <nitrocoro/net/DnsException.h>
#include <nitrocoro/net/DnsResolver.h>
#include <nitrocoro/testing/Test.h>

#include <netdb.h>
#include <set>

using namespace nitrocoro;
using namespace nitrocoro::net;

// Synchronous reference resolver using the same getaddrinfo() that ping uses.
static std::set<std::string> resolveSync(const std::string & host)
{
    std::set<std::string> result;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo * res = nullptr;
    if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res)
        return result;
    for (auto * p = res; p; p = p->ai_next)
    {
        if (p->ai_family == AF_INET)
        {
            InetAddress addr(*reinterpret_cast<sockaddr_in *>(p->ai_addr));
            result.insert(addr.toIp());
        }
        else if (p->ai_family == AF_INET6)
        {
            InetAddress addr(*reinterpret_cast<sockaddr_in6 *>(p->ai_addr));
            result.insert(addr.toIp());
        }
    }
    ::freeaddrinfo(res);
    return result;
}

/** Resolving "localhost" returns at least one loopback address. */
NITRO_TEST(dns_localhost)
{
    DnsResolver resolver;
    auto addrs = co_await resolver.resolve("localhost");
    NITRO_CHECK(!addrs.empty());
    bool hasLoopback = false;
    for (auto & a : addrs)
        if (a.isLoopbackIp())
            hasLoopback = true;
    NITRO_CHECK(hasLoopback);
}

/** Resolving a non-existent domain throws DnsException. */
NITRO_TEST(dns_invalid_domain)
{
    DnsResolver resolver;
    bool threw = false;
    try
    {
        co_await resolver.resolve("this.domain.does.not.exist.invalid");
    }
    catch (const DnsException &)
    {
        threw = true;
    }
    NITRO_CHECK(threw);
}

/**
 * For each well-known globally reachable host, DnsResolver must return at
 * least one IP that also appears in the synchronous getaddrinfo() result
 * (the same source ping uses). CDN domains may return different subsets on
 * each call, so we only require a non-empty intersection.
 */
NITRO_TEST(dns_matches_system_resolver)
{
    static const char * hosts[] = {
        "www.baidu.com",      // CN + global
        "www.cloudflare.com", // global
        "www.microsoft.com",  // global
    };

    DnsResolver resolver;
    for (const char * host : hosts)
    {
        auto ref = resolveSync(host);
        NITRO_CHECK(!ref.empty()); // system resolver must work too

        auto addrs = co_await resolver.resolve(host);
        NITRO_CHECK(!addrs.empty());

        bool intersects = false;
        for (auto & a : addrs)
            if (ref.count(a.toIp()))
                intersects = true;
        NITRO_CHECK(intersects);
    }
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
