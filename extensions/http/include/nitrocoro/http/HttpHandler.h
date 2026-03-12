/**
 * @file HttpHandler.h
 * @brief Type-erased wrapper for HTTP route handlers
 */
#pragma once
#include <nitrocoro/http/HttpStream.h>

#include <nitrocoro/core/Task.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace nitrocoro::http
{

using Params = std::unordered_map<std::string, std::string>;

// ── Abstract base ─────────────────────────────────────────────────────────────

struct HttpHandlerBase
{
    virtual Task<> invoke(HttpIncomingStream<HttpRequest> request,
                          HttpOutgoingStream<HttpResponse> response,
                          Params params)
        = 0;
    virtual ~HttpHandlerBase() = default;
};

using HttpHandlerPtr = std::shared_ptr<HttpHandlerBase>;

// ── Concrete handler ──────────────────────────────────────────────────────────
//
// Supported signatures:
//   (HttpIncomingStream<HttpRequest>, HttpOutgoingStream<HttpResponse>, Params)
//   (HttpIncomingStream<HttpRequest>, HttpOutgoingStream<HttpResponse>)
//   (HttpOutgoingStream<HttpResponse>)

template <typename F>
struct HttpHandler : HttpHandlerBase
{
    explicit HttpHandler(F f)
        : f_(std::move(f)) {}

    Task<> invoke(HttpIncomingStream<HttpRequest> request,
                  HttpOutgoingStream<HttpResponse> response,
                  Params params) override
    {
        using Req = HttpIncomingStream<HttpRequest>;
        using Resp = HttpOutgoingStream<HttpResponse>;

        if constexpr (std::is_invocable_v<F, Req &&, Resp &&, Params>)
            co_await f_(std::move(request), std::move(response), std::move(params));
        else if constexpr (std::is_invocable_v<F, Req &&, Resp &&>)
            co_await f_(std::move(request), std::move(response));
        else if constexpr (std::is_invocable_v<F, Resp &&>)
            co_await f_(std::move(response));
        else
            static_assert(sizeof(F) == 0, "Unsupported handler signature");
    }

    F f_;
};

template <typename F>
HttpHandlerPtr makeHttpHandler(F && f)
{
    return std::make_shared<HttpHandler<std::decay_t<F>>>(std::forward<F>(f));
}

} // namespace nitrocoro::http
