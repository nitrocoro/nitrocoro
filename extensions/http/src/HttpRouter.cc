/**
 * @file HttpRouter.cc
 * @brief HTTP request router implementation
 */
#include <nitrocoro/http/HttpRouter.h>

namespace nitrocoro::http
{

// ── Radix Tree helpers ────────────────────────────────────────────────────────

static std::string_view nextSegment(std::string_view path, size_t & pos)
{
    if (pos >= path.size())
        return {};
    if (path[pos] == '/')
        ++pos;
    size_t start = pos;
    while (pos < path.size() && path[pos] != '/')
        ++pos;
    return path.substr(start, pos - start);
}

void HttpRouter::insertRadix(RouteNode & node, std::string_view path, const HttpMethods & methods, const HttpHandlerPtr & handler)
{
    size_t pos = 0;
    RouteNode * cur = &node;

    while (true)
    {
        std::string_view seg = nextSegment(path, pos);
        if (seg.empty())
        {
            for (const auto & m : methods.methods_)
                cur->handlers[m] = handler;
            return;
        }

        if (seg[0] == ':')
        {
            std::string name(seg.substr(1));
            auto & child = cur->paramChildren[name];
            if (!child)
                child = std::make_unique<RouteNode>();
            cur = child.get();
        }
        else if (seg[0] == '*')
        {
            std::string name(seg.substr(1));
            auto & child = cur->wildcardChildren[name];
            if (!child)
                child = std::make_unique<RouteNode>();
            for (const auto & m : methods.methods_)
                child->handlers[m] = handler;
            return;
        }
        else
        {
            auto & child = cur->children[std::string(seg)];
            if (!child)
                child = std::make_unique<RouteNode>();
            cur = child.get();
        }
    }
}

static constexpr size_t kMaxPathLength = 2048;
static constexpr size_t kMaxPathSegments = 32;

const HttpRouter::MethodMap * HttpRouter::matchRadix(const RouteNode & node, std::string_view path, Params & params, size_t depth)
{
    if (depth > kMaxPathSegments)
        return nullptr;
    size_t pos = 0;
    const RouteNode * cur = &node;

    std::string_view seg = nextSegment(path, pos);
    if (seg.empty())
        return cur->handlers.empty() ? nullptr : &cur->handlers;

    // 1. static
    auto it = cur->children.find(seg);
    if (it != cur->children.end())
    {
        if (auto * methodMap = matchRadix(*it->second, path.substr(pos), params, depth + 1))
            return methodMap;
    }

    // 2. param — try all named param branches
    for (const auto & [pname, pnode] : cur->paramChildren)
    {
        params[pname] = std::string(seg);
        if (auto * methodMap = matchRadix(*pnode, path.substr(pos), params, depth + 1))
            return methodMap;
        params.erase(pname);
    }

    // 3. wildcard — try all named wildcard branches
    for (const auto & [wname, wnode] : cur->wildcardChildren)
    {
        if (!wnode->handlers.empty())
        {
            params[wname] = std::string(path.substr(pos - seg.size()));
            return &wnode->handlers;
        }
    }

    return nullptr;
}

// ── Public API ────────────────────────────────────────────────────────────────

void HttpRouter::addRouteImpl(const std::string & path, const HttpMethods & methods, HttpHandlerPtr handler)
{
    auto isParamOrWild = [](std::string_view p, char c) {
        if (!p.empty() && p[0] == c)
            return true;
        for (size_t i = 1; i < p.size(); ++i)
            if (p[i] == c && p[i - 1] == '/')
                return true;
        return false;
    };
    bool hasParam = isParamOrWild(path, ':');
    bool hasWild = isParamOrWild(path, '*');

    if (!hasParam && !hasWild)
    {
        for (const auto & m : methods.methods_)
            routes_.exact[path][m] = handler;
    }
    else
    {
        insertRadix(routes_.radixRoot, path, methods, std::move(handler));
    }
}

HttpRouter::RouteResult HttpRouter::route(const std::string & method, const std::string & path) const
{
    if (path.size() > kMaxPathLength)
        return {};

    auto lookupMethod = [&](const MethodMap & methodMap) -> RouteResult {
        auto it = methodMap.find(method);
        if (it != methodMap.end())
            return { it->second, {}, RouteResult::Reason::Ok };
        return { nullptr, {}, RouteResult::Reason::MethodNotAllowed };
    };

    // 1. exact
    auto exactIt = routes_.exact.find(path);
    if (exactIt != routes_.exact.end())
        return lookupMethod(exactIt->second);

    // 2. radix (param / wildcard)
    Params params;
    if (const MethodMap * methodMap = matchRadix(routes_.radixRoot, path, params))
    {
        auto r = lookupMethod(*methodMap);
        r.params = std::move(params);
        return r;
    }

    // 3. regex
    for (const auto & [pat, re, methodMap] : routes_.regexRoutes)
    {
        std::smatch m;
        if (std::regex_match(path, m, re))
        {
            Params regexParams;
            for (size_t i = 1; i < m.size(); ++i)
                regexParams["$" + std::to_string(i)] = m[i].str();
            auto r = lookupMethod(methodMap);
            r.params = std::move(regexParams);
            return r;
        }
    }

    return {};
}

} // namespace nitrocoro::http
