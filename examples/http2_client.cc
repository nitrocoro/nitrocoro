/**
 * @file http2_client.cc
 * @brief HTTP/2 client example
 */
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/http2/Http2Client.h>

using namespace nitrocoro;
using namespace nitrocoro::http2;

Task<> client_main(const char * url)
{
    printf("GET %s\n", url);
    try
    {
        auto resp = co_await get(url);
        printf("Status: %d %s\n", (int)resp.statusCode(), resp.statusReason().data());
        printf("Body: %s\n", resp.body().c_str());
    }
    catch (const std::exception & e)
    {
        printf("Error: %s\n", e.what());
    }
}

int main(int argc, char * argv[])
{
    if (argc < 2)
    {
        printf("Usage: %s <url>\n", argv[0]);
        printf("Example: %s http://localhost:8080/\n", argv[0]);
        printf("Example: %s https://httpbin.org/get\n", argv[0]);
        return 1;
    }

    Scheduler scheduler;
    scheduler.spawn([url = argv[1], &scheduler]() -> Task<> {
        co_await client_main(url);
        scheduler.stop();
    });
    scheduler.run();

    return 0;
}
