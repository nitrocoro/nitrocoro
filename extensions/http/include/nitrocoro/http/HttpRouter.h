/**
 * @file HttpRouter.h
 * @brief HTTP request router
 */
#pragma once

#include <nitrocoro/core/Task.h>
#include <nitrocoro/http/HttpStream.h>

#include <functional>
#include <map>
#include <string>
#include <utility>

namespace nitrocoro::http
{

class HttpRouter
{
public:
    using Handler = std::function<Task<>(HttpIncomingStream<HttpRequest> &&, HttpOutgoingStream<HttpResponse> &&)>;

    void route(const std::string & method, const std::string & path, Handler handler);

    Task<> dispatch(HttpIncomingStream<HttpRequest> request, HttpOutgoingStream<HttpResponse> response) const;

private:
    // Phase 1: exact match only.
    // Phase 2 (future): pattern list for parameterized / regex routes.
    std::map<std::pair<std::string, std::string>, Handler> exactRoutes_;
};

} // namespace nitrocoro::http
