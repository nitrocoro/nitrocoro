/**
 * @file StaticFiles.cc
 * @brief Static file serving handler implementation
 */
#include <nitrocoro/http/StaticFiles.h>

#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/http/HttpStream.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <sys/stat.h>
#include <unordered_map>

#ifdef _WIN32
#include <io.h>
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#endif

namespace nitrocoro::http
{

namespace fs = std::filesystem;

std::unordered_map<std::string, std::string> StaticFilesOptions::defaultMimeTypes()
{
    return {
        { ".html", "text/html; charset=utf-8" },
        { ".htm", "text/html; charset=utf-8" },
        { ".css", "text/css; charset=utf-8" },
        { ".js", "text/javascript; charset=utf-8" },
        { ".mjs", "text/javascript; charset=utf-8" },
        { ".json", "application/json" },
        { ".xml", "application/xml" },
        { ".txt", "text/plain; charset=utf-8" },
        { ".md", "text/markdown; charset=utf-8" },
        { ".svg", "image/svg+xml" },
        { ".png", "image/png" },
        { ".jpg", "image/jpeg" },
        { ".jpeg", "image/jpeg" },
        { ".gif", "image/gif" },
        { ".ico", "image/x-icon" },
        { ".webp", "image/webp" },
        { ".woff", "font/woff" },
        { ".woff2", "font/woff2" },
        { ".ttf", "font/ttf" },
        { ".otf", "font/otf" },
        { ".pdf", "application/pdf" },
        { ".wasm", "application/wasm" },
    };
}

std::unordered_map<std::string, std::string> StaticFilesOptions::defaultAcceptEncodings()
{
    return { { "br", "br" }, { "gzip", "gz" } };
}

namespace
{

constexpr size_t kChunkSize = 65536;

std::string_view mimeType(const std::string & ext,
                          const std::unordered_map<std::string, std::string> & mime_types)
{
    auto it = mime_types.find(ext);
    return it != mime_types.end() ? std::string_view(it->second) : "application/octet-stream";
}

// Splits a comma-separated header value (e.g. Accept-Encoding) into trimmed tokens,
// stripping optional whitespace and quality parameters (;q=...).
std::vector<std::string_view> splitTokens(std::string_view sv)
{
    std::vector<std::string_view> tokens;
    while (!sv.empty())
    {
        auto comma = sv.find(',');
        std::string_view token = sv.substr(0, comma);
        while (!token.empty() && token.front() == ' ')
            token.remove_prefix(1);
        auto semi = token.find(';');
        token = token.substr(0, semi);
        while (!token.empty() && token.back() == ' ')
            token.remove_suffix(1);
        if (!token.empty())
            tokens.push_back(token);
        if (comma == std::string_view::npos)
            break;
        sv.remove_prefix(comma + 1);
    }
    return tokens;
}

// Generates a strong ETag from mtime and file size as per RFC 7232 §2.3.
std::string makeETag(time_t mtime, int64_t size)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\"%lx-%lx\"",
                  static_cast<unsigned long>(mtime),
                  static_cast<unsigned long>(size));
    return buf;
}

struct CacheNode
{
    const std::string key;
    std::string data;
    std::string etag;
    std::string last_modified;
    std::string mime_type;
    std::string content_encoding;
    std::chrono::steady_clock::time_point expire_at;
};

// Per-scheduler LRU cache for file contents. Each Scheduler owns one instance,
// so all access is single-threaded and requires no locking.
struct FileCache
{
    using List = std::list<CacheNode>;

    List lru_;
    std::unordered_map<std::string, List::iterator> index_;
    size_t currentSize_ = 0;

    const CacheNode * get(const std::string & key)
    {
        auto it = index_.find(key);
        if (it == index_.end())
            return nullptr;
        if (std::chrono::steady_clock::now() > it->second->expire_at)
        {
            evict(it);
            return nullptr;
        }
        lru_.splice(lru_.begin(), lru_, it->second);
        return &*it->second;
    }

    void invalidate(const std::string & key)
    {
        auto it = index_.find(key);
        if (it != index_.end())
            evict(it);
    }

    const CacheNode & put(CacheNode node, size_t max_cache_size)
    {
        // evict existing entry for this key
        if (auto it = index_.find(node.key); it != index_.end())
        {
            currentSize_ -= it->second->data.size();
            lru_.erase(it->second);
            index_.erase(it);
        }
        size_t incoming = node.data.size();
        while (!lru_.empty() && currentSize_ + incoming > max_cache_size)
        {
            currentSize_ -= lru_.back().data.size();
            index_.erase(lru_.back().key);
            lru_.pop_back();
        }
        lru_.push_front(std::move(node));
        index_.emplace(lru_.front().key, lru_.begin());
        currentSize_ += incoming;
        return lru_.front();
    }

private:
    void evict(std::unordered_map<std::string, List::iterator>::iterator it)
    {
        currentSize_ -= it->second->data.size();
        lru_.erase(it->second);
        index_.erase(it);
    }
};

// Holds one FileCache per Scheduler, created on first access.
// The map itself is protected by a shared_mutex: reads use shared_lock,
// insertion uses unique_lock.
struct SchedulerCaches
{
    std::shared_mutex mutex;
    std::unordered_map<Scheduler *, std::unique_ptr<FileCache>> cacheMap;

    FileCache * getCurrent()
    {
        Scheduler * sched = Scheduler::current();
        {
            std::shared_lock lock(mutex);
            auto it = cacheMap.find(sched);
            if (it != cacheMap.end())
                return it->second.get();
        }
        std::unique_lock lock(mutex);
        auto & ptr = cacheMap[sched];
        if (!ptr)
            ptr = std::make_unique<FileCache>();
        return ptr.get();
    }
};

} // namespace

struct PreCalculated
{
    fs::path root;
    std::string cacheControlValue;
};

HttpHandlerPtr staticFiles(std::string_view root, StaticFilesOptions opts)
{
    if (opts.cache_max_file_size > opts.cache_max_cache_size)
        opts.cache_max_file_size = opts.cache_max_cache_size;

    PreCalculated preCalc{
        .root = fs::weakly_canonical(fs::path(root)),
        .cacheControlValue = opts.max_age > 0 ? "public, max-age=" + std::to_string(opts.max_age) : "no-cache"
    };

    return makeHttpHandler(
        [opts = std::move(opts),
         preCalc = std::move(preCalc),
         caches = std::make_shared<SchedulerCaches>()](IncomingRequestPtr req,
                                                       ServerResponsePtr resp) mutable -> Task<> {
            const fs::path & root = preCalc.root;
            const std::string & cacheControlValue = preCalc.cacheControlValue;
            FileCache * cache = caches->getCurrent();

            // Resolve path: take the first (and only) wildcard param
            std::string relPath;
            const PathParams & pathParams = req->pathParams();
            if (!pathParams.empty())
            {
                if (auto iter = pathParams.find("path"); iter != pathParams.end())
                {
                    relPath = iter->second;
                }
                else
                {
                    relPath = pathParams.begin()->second;
                }
            }
            if (!relPath.empty() && relPath.front() == '/')
            {
                relPath.erase(0, 1);
            }

            fs::path filePath = fs::weakly_canonical(root / relPath);

            // Directory → index file
            if (fs::is_directory(filePath))
                filePath /= fs::path(opts.index_file).filename();

            // Path traversal check
            auto rel = filePath.lexically_relative(root);
            if (rel.empty() || *rel.begin() == "..")
            {
                resp->setStatus(403);
                co_return;
            }

            // Stat
            struct stat st{};
            if (::stat(filePath.string().c_str(), &st) != 0 || !S_ISREG(st.st_mode))
            {
                resp->setStatus(404);
                co_return;
            }

            // Last-Modified / 304
            std::string lastModified;
            {
                struct tm tm{};
#ifdef _WIN32
                gmtime_s(&tm, &st.st_mtime);
#else
                gmtime_r(&st.st_mtime, &tm);
#endif
                char lm[32]; // strftime ensure ends with \0
                std::strftime(lm, sizeof(lm), "%a, %d %b %Y %H:%M:%S GMT", &tm);
                lastModified = lm;

                const auto & ims = req->getHeader(HttpHeader::NameCode::IfModifiedSince);
                if (!ims.empty() && ims == lastModified)
                {
                    resp->setStatus(304);
                    co_return;
                }
                resp->setHeader(HttpHeader::NameCode::LastModified, lastModified);
            }

            // ETag / 304
            std::string etag;
            if (opts.enable_etag)
            {
                etag = makeETag(st.st_mtime, st.st_size);
                const auto & ifNoneMatch = req->getHeader(HttpHeader::NameCode::IfNoneMatch);
                if (!ifNoneMatch.empty() && ifNoneMatch == etag)
                {
                    resp->setStatus(304);
                    co_return;
                }
                resp->setHeader("ETag", etag);
            }

            // Pre-compressed static file: iterate Accept-Encoding in order
            fs::path actualPath = filePath;
            std::string selectedEncoding;
            const auto & acceptEncoding = req->getHeader(HttpHeader::NameCode::AcceptEncoding);
            if (!acceptEncoding.empty() && !opts.accept_encodings.empty())
            {
                for (auto token : splitTokens(acceptEncoding))
                {
                    auto extIt = opts.accept_encodings.find(std::string(token));
                    if (extIt == opts.accept_encodings.end())
                        continue;
                    fs::path candidate(filePath.string() + "." + extIt->second);
                    struct stat cst{};
                    if (::stat(candidate.string().c_str(), &cst) == 0 && S_ISREG(cst.st_mode))
                    {
                        actualPath = candidate;
                        st = cst;
                        selectedEncoding = std::string(token);
                        break;
                    }
                }
            }

            const std::string cacheKey = actualPath.string();
            const bool cacheEnabled = opts.cache_ttl > 0 && static_cast<size_t>(st.st_size) <= opts.cache_max_file_size;

            // Try cache
            if (cacheEnabled)
            {
                if (const CacheNode * cached = cache->get(cacheKey))
                {
                    bool fresh;
                    if (opts.enable_etag)
                    {
                        fresh = cached->etag == etag;
                    }
                    else
                    {
                        fresh = cached->last_modified == lastModified;
                    }
                    if (!fresh)
                    {
                        cache->invalidate(cacheKey);
                    }
                    else
                    {
                        resp->setStatus(200);
                        resp->setHeader(HttpHeader::NameCode::ContentType, cached->mime_type);
                        resp->setHeader(HttpHeader::NameCode::LastModified, cached->last_modified);
                        if (!cached->etag.empty())
                            resp->setHeader("ETag", cached->etag);
                        if (!cached->content_encoding.empty())
                            resp->setHeader(HttpHeader::NameCode::ContentEncoding, cached->content_encoding);
                        resp->setHeader(HttpHeader::NameCode::ContentLength, std::to_string(cached->data.size()));
                        resp->setHeader(HttpHeader::NameCode::CacheControl, cacheControlValue);
                        if (!opts.cache_header.empty())
                            resp->setHeader(opts.cache_header, "HIT");

                        // Skip body when HEAD
                        if (req->method() != methods::Head)
                            resp->setBody(cached->data.data(), cached->data.size());
                        co_return;
                    }
                }
            }

            // Headers
            resp->setStatus(200);
            std::string mimeTypeStr(mimeType(filePath.extension().string(), opts.mime_types));
            resp->setHeader(HttpHeader::NameCode::ContentType, mimeTypeStr);
            resp->setHeader(HttpHeader::NameCode::ContentLength, std::to_string(st.st_size));
            if (!selectedEncoding.empty())
                resp->setHeader(HttpHeader::NameCode::ContentEncoding, selectedEncoding);

            resp->setHeader(HttpHeader::NameCode::CacheControl, cacheControlValue);

            // HEAD: headers only
            if (req->method() == methods::Head)
            {
                co_return;
            }

            // Stream file body
            std::unique_ptr<FILE, decltype(&std::fclose)> fp(
                std::fopen(actualPath.string().c_str(), "rb"), &std::fclose);
            if (!fp)
            {
                resp->setStatus(500);
                co_return;
            }

            if (cacheEnabled)
            {
                if (!opts.cache_header.empty())
                    resp->setHeader(opts.cache_header, "MISS");
                std::string fileData(static_cast<size_t>(st.st_size), '\0');
                if (std::fread(fileData.data(), 1, fileData.size(), fp.get()) != fileData.size())
                {
                    resp->setStatus(500);
                    co_return;
                }
                const CacheNode & node = cache->put(
                    {
                        .key = cacheKey,
                        .data = std::move(fileData),
                        .etag = etag,
                        .last_modified = lastModified,
                        .mime_type = mimeTypeStr,
                        .content_encoding = selectedEncoding,
                        .expire_at = std::chrono::steady_clock::now() + std::chrono::seconds(opts.cache_ttl),
                    },
                    opts.cache_max_cache_size);

                resp->setBody(node.data.data(), node.data.size());
            }
            else
            {
                // TODO: send file
                char buf[kChunkSize];
                size_t remaining = static_cast<size_t>(st.st_size);
                while (remaining > 0)
                {
                    size_t n = std::fread(buf, 1, std::min(remaining, kChunkSize), fp.get());
                    if (n == 0)
                        break;
                    resp->setBody(buf, n);
                    remaining -= n;
                }
            }
        });
}

} // namespace nitrocoro::http
