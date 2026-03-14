/**
 * @file StaticFiles.h
 * @brief Static file serving handler for HttpRouter
 */
#pragma once

#include <nitrocoro/http/HttpHandler.h>

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace nitrocoro::http
{

struct StaticFilesOptions
{
    static std::unordered_map<std::string, std::string> defaultMimeTypes();
    static std::unordered_map<std::string, std::string> defaultAcceptEncodings();

    std::string index_file = "index.html";
    bool enable_etag = true;
    int max_age = 3600;
    std::unordered_map<std::string, std::string> mime_types = defaultMimeTypes();
    std::unordered_map<std::string, std::string> accept_encodings = defaultAcceptEncodings();

    // cache
    int cache_ttl = 0;                              // seconds; 0 = disabled
    size_t cache_max_file_size = 1024 * 1024;       // files larger than this are not cached (default 1MB)
    size_t cache_max_cache_size = 64 * 1024 * 1024; // total cache capacity (default 64MB)
    std::string cache_header;                       // if non-empty, add this header with HIT/MISS value
};

/**
 * @brief Returns a handler that serves static files from @p root.
 *
 * Intended for use with a wildcard route:
 * @code
 * server.route("/static/*path", {"GET", "HEAD"}, staticFiles("./public"));
 * @endcode
 *
 * The captured `path` param is resolved relative to @p root.
 * Path traversal attempts (e.g. `../../etc/passwd`) are rejected with 403.
 * Unknown file extensions are served as `application/octet-stream`.
 */
HttpHandlerPtr staticFiles(std::string_view root, StaticFilesOptions opts = {});

} // namespace nitrocoro::http
