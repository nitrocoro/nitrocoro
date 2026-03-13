/**
 * @file HttpRouter.h
 * @brief HTTP request router
 */
#pragma once

#include <nitrocoro/http/HttpHandler.h>
#include <nitrocoro/http/HttpTypes.h>

#include <map>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nitrocoro::http
{

struct HttpMethods
{
    explicit HttpMethods(std::string_view method)
        : methods_{ HttpMethod::fromString(method) } {}
    HttpMethods(HttpMethod method)
        : methods_{ method } {}
    HttpMethods(std::initializer_list<std::string_view> methods)
    {
        methods_.reserve(methods.size());
        for (auto s : methods)
            methods_.push_back(HttpMethod::fromString(s));
    }
    HttpMethods(std::initializer_list<HttpMethod> methods)
        : methods_(methods) {}

    std::vector<HttpMethod> methods_;
};

/**
 * @brief HTTP request router with three-tier matching.
 *
 * Routes are matched in the following priority order:
 *
 * 1. **Exact match** — registered via `addRoute()` with a static path.
 *    Matched in O(1). `params` is empty on match.
 *    @code
 *    router.addRoute("/users/me", "GET", handler);
 *    // GET /users/me  →  params: {}
 *    @endcode
 *
 * 2. **Path parameters** (`:name`) — registered via `addRoute()`. Each `:name`
 *    segment matches exactly one path segment (no `/`). Captured into `params`
 *    by name. Multiple parameters per route are supported.
 *    @code
 *    router.addRoute("/users/:id", "GET", handler);
 *    // GET /users/42          →  params: {"id": "42"}
 *    // GET /users/42/profile  →  no match (segment count mismatch)
 *
 *    router.addRoute("/users/:uid/posts/:pid", "GET", handler);
 *    // GET /users/1/posts/99  →  params: {"uid": "1", "pid": "99"}
 *    @endcode
 *
 * 3. **Wildcard** (`*name`) — registered via `addRoute()`. Must appear at the
 *    end of the pattern. Captures all remaining segments including `/`.
 *    @code
 *    router.addRoute("/files/*path", "GET", handler);
 *    // GET /files/a/b/c.txt  →  params: {"path": "a/b/c.txt"}
 *    @endcode
 *
 * 4. **Regex** — registered via `addRouteRegex()`. Full path match via
 *    `std::regex_match`. Capture groups are exposed as `$1`, `$2`, etc.
 *    Evaluated last; linear scan over all registered regex routes.
 *    @code
 *    router.addRouteRegex(R"(/items/(\d+))", "GET", handler);
 *    // GET /items/123  →  params: {"$1": "123"}
 *    // also: router.addRouteRegex(R"(/items/(\d+))", {"GET", "HEAD"}, handler);
 *    @endcode
 *
 * When no route matches, `route()` returns a `RouteResult` with a null handler.
 * `addRoute()` and `addRouteRegex()` throw `std::invalid_argument` if any method is invalid.
 *
 * ## Security limits
 * - Paths longer than 2048 bytes are rejected (prevents ReDoS on regex routes).
 * - Radix tree matching is limited to 32 path segments (prevents CWE-674 stack overflow).
 *
 * ## Handler signatures
 * Any callable returning `Task<>` is accepted. Supported parameter forms:
 * @code
 * // Full signature
 * [](HttpIncomingStream<HttpRequest> req, HttpOutgoingStream<HttpResponse> resp, Params p) -> Task<> {}
 * // Without params
 * [](HttpIncomingStream<HttpRequest> req, HttpOutgoingStream<HttpResponse> resp) -> Task<> {}
 * // Response only
 * [](HttpOutgoingStream<HttpResponse> resp) -> Task<> {}
 * @endcode
 */

class HttpRouter
{
public:
    struct RouteResult
    {
        enum class Reason
        {
            Ok,
            NotFound,
            MethodNotAllowed
        };

        HttpHandlerPtr handler;
        Params params;
        Reason reason = Reason::NotFound;
        std::string allowedMethods;

        explicit operator bool() const { return handler != nullptr; }
    };

    template <typename F>
    void addRoute(const std::string & path, HttpMethods methods, F && handler);
    template <typename F>
    void addRouteRegex(const std::string & pattern, const HttpMethods & methods, F && handler);

    // Returns {handler, params} for the matched route, or {nullptr, {}} if not found.
    RouteResult route(HttpMethod method, const std::string & path) const;

private:
    struct RouteNode;
    using MethodMap = std::unordered_map<HttpMethod, HttpHandlerPtr>;
    using NodeMap = std::map<std::string, std::unique_ptr<RouteNode>, std::less<>>;

    struct RouteEntry
    {
        MethodMap handlers;
        std::string allowedMethods;
    };

    struct RouteNode
    {
        RouteEntry entry;
        NodeMap children;         // static segments
        NodeMap paramChildren;    // key = param name (:id → node)
        NodeMap wildcardChildren; // key = wildcard name (*path → node)
    };

    struct Routes
    {
        std::unordered_map<std::string, RouteEntry> exact;
        RouteNode radixRoot;
        std::vector<std::tuple<std::string, std::regex, RouteEntry>> regexRoutes;
    };

    void addRouteImpl(const std::string & path, const HttpMethods & methods, HttpHandlerPtr handler);

    static void checkInvalidMethods(const HttpMethods & methods);
    static void addMethodToEntry(RouteEntry & entry, HttpMethod method, const HttpHandlerPtr & handler);
    static void insertRadix(RouteNode & node, std::string_view path, const HttpMethods & methods, const HttpHandlerPtr & handler);
    static const RouteEntry * matchRadix(const RouteNode & node, std::string_view path, Params & params, size_t depth = 0);

    Routes routes_;
};

template <typename F>
void HttpRouter::addRoute(const std::string & path, HttpMethods methods, F && handler)
{
    checkInvalidMethods(methods);
    addRouteImpl(path, methods, makeHttpHandler(std::forward<F>(handler)));
}

template <typename F>
void HttpRouter::addRouteRegex(const std::string & pattern, const HttpMethods & methods, F && handler)
{
    checkInvalidMethods(methods);
    auto handlerPtr = makeHttpHandler(std::forward<F>(handler));
    for (auto & [pat, _, entry] : routes_.regexRoutes)
    {
        if (pat == pattern)
        {
            for (const auto & method : methods.methods_)
            {
                addMethodToEntry(entry, method, handlerPtr);
            }
            return;
        }
    }
    RouteEntry entry;
    for (const auto & method : methods.methods_)
    {
        addMethodToEntry(entry, method, handlerPtr);
    }
    routes_.regexRoutes.emplace_back(pattern, std::regex(pattern), std::move(entry));
}

} // namespace nitrocoro::http
