/**
 * @file BodyWriter.h
 * @brief Body writer interface and factory
 */
#pragma once
#include <nitrocoro/http/HttpTypes.h>

#include <nitrocoro/core/Task.h>
#include <nitrocoro/io/Stream.h>

#include <memory>
#include <string_view>

namespace nitrocoro::http
{

class BodyWriter
{
public:
    virtual ~BodyWriter() = default;

    virtual Task<> write(std::string_view data) = 0;
    virtual Task<> end() = 0;

    static std::unique_ptr<BodyWriter> create(
        TransferMode mode,
        io::StreamPtr stream,
        size_t contentLength = 0);
};

} // namespace nitrocoro::http
