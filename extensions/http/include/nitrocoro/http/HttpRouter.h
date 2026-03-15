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

namespace detail
{

struct MethodList
{
    explicit MethodList(std::string_view method)
        : methods_{ HttpMethod::fromString(method) } {}
    MethodList(HttpMethod method)
        : methods_{ method } {}
    MethodList(std::initializer_list<std::string_view> methods)
    {
        methods_.reserve(methods.size());
        for (auto s : methods)
            methods_.push_back(HttpMethod::fromString(s));
    }
    MethodList(std::initializer_list<HttpMethod> methods)
        : methods_(methods) {}

    std::vector<HttpMethod> methods_;
};

} // namespace detail

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
 * [](HttpIncomingStream<HttpRequest> req, HttpOutgoingStream<HttpResponse> resp, PathParams p) -> Task<> {}
 * // Without params
 * [](HttpIncomingStream<HttpRequest> req, HttpOutgoingStream<HttpResponse> resp) -> Task<> {}
 * // Response only
 * [](HttpOutgoingStream<HttpResponse> resp) -> Task<> {}
 * @endcode
 */

class HttpRouter
{
public:
    using MethodList = detail::MethodList;

    struct RouteResult
    {
        enum class Reason
        {
            Ok,
            NotFound,
            MethodNotAllowed
        };

        HttpHandlerPtr handler;
        PathParams params;
        Reason reason = Reason::NotFound;
        std::string allowedMethods;

        explicit operator bool() const { return handler != nullptr; }
    };

    template <typename F>
    void addRoute(const std::string & path, MethodList methods, F && handler);
    template <typename F>
    void addRouteRegex(const std::string & pattern, const MethodList & methods, F && handler);

    // Returns {handler, params} for the matched route, or {nullptr, {}} if not found.
    RouteResult route(HttpMethod method, const std::string & path) const;

private:
    struct Entry
    {
        std::unordered_map<HttpMethod, HttpHandlerPtr> handlers;
        std::string allowedMethods;
    };

    struct RadixNode;
    using RadixNodeMap = std::map<std::string, std::unique_ptr<RadixNode>, std::less<>>;
    struct RadixNode
    {
        Entry entry;
        RadixNodeMap children;         // static segments
        RadixNodeMap paramChildren;    // key = param name (:id → node)
        RadixNodeMap wildcardChildren; // key = wildcard name (*path → node)
    };

    struct RegexEntry
    {
        std::string pattern;
        std::regex regex;
        Entry entry;
    };

    void addRouteImpl(const std::string & path, const MethodList & methods, HttpHandlerPtr handler);

    static void checkInvalidMethods(const MethodList & methods);
    static void addMethodToEntry(Entry & entry, HttpMethod method, const HttpHandlerPtr & handler);
    static void insertRadix(RadixNode & node, std::string_view path, const MethodList & methods, const HttpHandlerPtr & handler);
    static const Entry * matchRadix(const RadixNode & node, std::string_view path, PathParams & params, size_t depth = 0);

    std::unordered_map<std::string, Entry> exactRoutes_;
    RadixNode radixRoot_;
    std::vector<RegexEntry> regexRoutes_;
};

template <typename F>
void HttpRouter::addRoute(const std::string & path, MethodList methods, F && handler)
{
    checkInvalidMethods(methods);
    if constexpr (std::is_same_v<std::decay_t<F>, HttpHandlerPtr>)
        addRouteImpl(path, methods, std::forward<F>(handler));
    else
        addRouteImpl(path, methods, makeHttpHandler(std::forward<F>(handler)));
}

template <typename F>
void HttpRouter::addRouteRegex(const std::string & pattern, const MethodList & methods, F && handler)
{
    checkInvalidMethods(methods);
    auto handlerPtr = makeHttpHandler(std::forward<F>(handler));
    for (auto & r : regexRoutes_)
    {
        if (r.pattern == pattern)
        {
            for (const auto & method : methods.methods_)
                addMethodToEntry(r.entry, method, handlerPtr);
            return;
        }
    }
    Entry entry;
    for (const auto & method : methods.methods_)
        addMethodToEntry(entry, method, handlerPtr);
    regexRoutes_.push_back({ pattern, std::regex(pattern), std::move(entry) });
}

} // namespace nitrocoro::http
