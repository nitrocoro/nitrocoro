/**
 * @file HttpHandler.h
 * @brief Type-erased wrapper for HTTP route handlers
 */
#pragma once
#include <nitrocoro/http/HttpStream.h>

#include <nitrocoro/core/CoroTraits.h>
#include <nitrocoro/core/Task.h>

#include <memory>

namespace nitrocoro::http
{

struct HttpHandlerBase
{
    virtual Task<> invoke(IncomingRequestPtr request, ServerResponsePtr response) = 0;
    virtual ~HttpHandlerBase() = default;
};

using HttpHandlerPtr = std::shared_ptr<HttpHandlerBase>;

template <typename F>
struct HttpHandler : HttpHandlerBase
{
    explicit HttpHandler(F f)
        : f_(std::move(f)) {}

    Task<> invoke(IncomingRequestPtr request,
                  ServerResponsePtr response) override
    {
        if constexpr (std::is_invocable_v<F, IncomingRequestPtr, ServerResponsePtr>)
            if constexpr (is_awaitable_v<std::invoke_result_t<F, IncomingRequestPtr, ServerResponsePtr>>)
            {
                co_await f_(request, response);
            }
            else
            {
                f_(request, response);
            }
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
