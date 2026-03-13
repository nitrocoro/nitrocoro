/**
 * @file HttpRouter.cc
 * @brief HTTP request router implementation
 */
#include <nitrocoro/http/HttpRouter.h>
#include <stdexcept>

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

void HttpRouter::addMethodToEntry(RouteEntry & entry, HttpMethod method, const HttpHandlerPtr & handler)
{
    entry.handlers[method] = handler;
    if (method == methods::Get && !entry.handlers.contains(methods::Head))
    {
        entry.handlers[methods::Head] = handler;
    }

    entry.allowedMethods.clear();
    for (const auto & [m, _] : entry.handlers)
    {
        if (!entry.allowedMethods.empty())
        {
            entry.allowedMethods += ", ";
        }
        entry.allowedMethods += m.toString();
    }
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
            {
                addMethodToEntry(cur->entry, m, handler);
            }
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
                addMethodToEntry(child->entry, m, handler);
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

const HttpRouter::RouteEntry * HttpRouter::matchRadix(const RouteNode & node, std::string_view path, Params & params, size_t depth)
{
    if (depth > kMaxPathSegments)
        return nullptr;
    size_t pos = 0;
    const RouteNode * cur = &node;

    std::string_view seg = nextSegment(path, pos);
    if (seg.empty())
    {
        return cur->entry.handlers.empty() ? nullptr : &cur->entry;
    }

    // 1. static
    auto it = cur->children.find(seg);
    if (it != cur->children.end())
    {
        if (auto * entry = matchRadix(*it->second, path.substr(pos), params, depth + 1))
            return entry;
    }

    // 2. param — try all named param branches
    for (const auto & [pname, pnode] : cur->paramChildren)
    {
        params[pname] = std::string(seg);
        if (auto * entry = matchRadix(*pnode, path.substr(pos), params, depth + 1))
            return entry;
        params.erase(pname);
    }

    // 3. wildcard — try all named wildcard branches
    for (const auto & [wname, wnode] : cur->wildcardChildren)
    {
        if (!wnode->entry.handlers.empty())
        {
            params[wname] = std::string(path.substr(pos - seg.size()));
            return &wnode->entry;
        }
    }

    return nullptr;
}

// ── Public API ────────────────────────────────────────────────────────────────

void HttpRouter::checkInvalidMethods(const HttpMethods & methods)
{
    for (const auto & m : methods.methods_)
        if (m == methods::_Invalid)
            throw std::invalid_argument("HttpRouter: invalid HTTP method");
}

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
        {
            addMethodToEntry(routes_.exact[path], m, handler);
        }
    }
    else
    {
        insertRadix(routes_.radixRoot, path, methods, handler);
    }
}

HttpRouter::RouteResult HttpRouter::route(HttpMethod method, const std::string & path) const
{
    if (path.size() > kMaxPathLength)
        return {};

    auto lookupMethod = [&](const RouteEntry & entry) -> RouteResult {
        auto it = entry.handlers.find(method);
        if (it != entry.handlers.end())
            return { it->second, {}, RouteResult::Reason::Ok, {} };
        return { nullptr, {}, RouteResult::Reason::MethodNotAllowed, entry.allowedMethods };
    };

    // 1. exact
    auto exactIt = routes_.exact.find(path);
    if (exactIt != routes_.exact.end())
    {
        return lookupMethod(exactIt->second);
    }

    // 2. radix (param / wildcard)
    Params params;
    if (const RouteEntry * entry = matchRadix(routes_.radixRoot, path, params))
    {
        auto r = lookupMethod(*entry);
        r.params = std::move(params);
        return r;
    }

    // 3. regex
    for (const auto & [pat, re, entry] : routes_.regexRoutes)
    {
        std::smatch m;
        if (std::regex_match(path, m, re))
        {
            Params regexParams;
            for (size_t i = 1; i < m.size(); ++i)
                regexParams["$" + std::to_string(i)] = m[i].str();
            auto r = lookupMethod(entry);
            r.params = std::move(regexParams);
            return r;
        }
    }

    return {};
}

} // namespace nitrocoro::http
