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

using PathParams = std::unordered_map<std::string, std::string>;

// ── Abstract base ─────────────────────────────────────────────────────────────

struct HttpHandlerBase
{
    virtual Task<> invoke(IncomingRequestPtr request,
                          ServerResponsePtr response,
                          PathParams params)
        = 0;
    virtual ~HttpHandlerBase() = default;
};

using HttpHandlerPtr = std::shared_ptr<HttpHandlerBase>;

// ── Concrete handler ──────────────────────────────────────────────────────────
//
// Supported signatures:
//   (HttpIncomingStream<HttpRequest>, HttpOutgoingStream<HttpResponse>, PathParams)
//   (HttpIncomingStream<HttpRequest>, HttpOutgoingStream<HttpResponse>)
//   (HttpOutgoingStream<HttpResponse>)

template <typename F>
struct HttpHandler : HttpHandlerBase
{
    explicit HttpHandler(F f)
        : f_(std::move(f)) {}

    Task<> invoke(IncomingRequestPtr request,
                  ServerResponsePtr response,
                  PathParams params) override
    {
        using ReqPtr = IncomingRequestPtr;
        using RespPtr = ServerResponsePtr;

        if constexpr (std::is_invocable_v<F, ReqPtr, RespPtr, PathParams>)
            co_await f_(request, response, std::move(params));
        else if constexpr (std::is_invocable_v<F, ReqPtr, RespPtr>)
            co_await f_(request, response);
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
