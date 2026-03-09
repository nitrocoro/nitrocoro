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

void HttpRouter::insertRadix(RouteNode & node, std::string_view path, HttpHandlerPtr handler)
{
    size_t pos = 0;
    RouteNode * cur = &node;

    while (true)
    {
        std::string_view seg = nextSegment(path, pos);
        if (seg.empty())
        {
            cur->handler = std::move(handler);
            return;
        }

        if (seg[0] == ':')
        {
            if (!cur->paramChild)
                cur->paramChild = std::make_unique<RouteNode>();
            cur->paramName = std::string(seg.substr(1));
            cur = cur->paramChild.get();
        }
        else if (seg[0] == '*')
        {
            if (!cur->wildcardChild)
                cur->wildcardChild = std::make_unique<RouteNode>();
            cur->wildcardName = std::string(seg.substr(1));
            cur->wildcardChild->handler = std::move(handler);
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

HttpHandlerPtr HttpRouter::matchRadix(const RouteNode & node, std::string_view path, Params & params)
{
    size_t pos = 0;
    const RouteNode * cur = &node;

    while (true)
    {
        std::string_view seg = nextSegment(path, pos);
        if (seg.empty())
            return cur->handler;

        // 1. static
        auto it = cur->children.find(seg);
        if (it != cur->children.end())
        {
            if (auto h = matchRadix(*it->second, path.substr(pos), params))
                return h;
        }

        // 2. param
        if (cur->paramChild)
        {
            Params branch = params;
            branch[cur->paramName] = std::string(seg);
            if (auto h = matchRadix(*cur->paramChild, path.substr(pos), branch))
            {
                params = std::move(branch);
                return h;
            }
        }

        // 3. wildcard — consumes the rest
        if (cur->wildcardChild && cur->wildcardChild->handler)
        {
            params[cur->wildcardName] = std::string(path.substr(pos - seg.size()));
            return cur->wildcardChild->handler;
        }

        return nullptr;
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void HttpRouter::addRouteImpl(const std::string & method, const std::string & path, HttpHandlerPtr handler)
{
    auto & mr = routes_[method];

    bool hasParam = path.find(':') != std::string::npos;
    bool hasWild = path.find('*') != std::string::npos;

    if (!hasParam && !hasWild)
    {
        mr.exact[path] = std::move(handler);
    }
    else
    {
        insertRadix(mr.radixRoot, path, std::move(handler));
    }
}

HttpRouter::RouteResult HttpRouter::route(const std::string & method, const std::string & path) const
{
    auto it = routes_.find(method);
    if (it == routes_.end())
        return {};

    const MethodRoutes & mr = it->second;

    // 1. exact
    auto exactIt = mr.exact.find(path);
    if (exactIt != mr.exact.end())
        return { exactIt->second, {} };

    // 2. radix (param / wildcard)
    Params params;
    if (auto h = matchRadix(mr.radixRoot, path, params))
        return { h, std::move(params) };

    // 3. regex
    for (const auto & [re, handler] : mr.regexRoutes)
    {
        std::smatch m;
        if (std::regex_match(path, m, re))
        {
            Params rparams;
            for (size_t i = 1; i < m.size(); ++i)
                rparams["$" + std::to_string(i)] = m[i].str();
            return { handler, std::move(rparams) };
        }
    }

    return {};
}

} // namespace nitrocoro::http
