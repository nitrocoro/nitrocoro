/**
 * @file HttpRouter.cc
 * @brief HTTP request router implementation
 */
#include <nitrocoro/http/HttpRouter.h>

namespace nitrocoro::http
{

void HttpRouter::route(const std::string & method, const std::string & path, Handler handler)
{
    exactRoutes_[{ method, path }] = std::move(handler);
}

Task<> HttpRouter::dispatch(
    HttpIncomingStream<HttpRequest> request, HttpOutgoingStream<HttpResponse> response) const
{
    auto it = exactRoutes_.find({ std::string(request.method()), std::string(request.path()) });
    if (it == exactRoutes_.end())
    {
        response.setStatus(StatusCode::k404NotFound);
        co_await response.end("Not Found");
        co_return;
    }
    co_await it->second(std::move(request), std::move(response));
}

} // namespace nitrocoro::http
