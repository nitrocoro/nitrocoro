/**
 * @file Http1BodyWriter.h
 * @brief HTTP/1.1 body writer base class
 */
#pragma once
#include <nitrocoro/http/BodyWriter.h>
#include <nitrocoro/http/HttpTypes.h>

#include <nitrocoro/core/Task.h>
#include <nitrocoro/io/Stream.h>

#include <memory>

namespace nitrocoro::http
{

class Http1BodyWriter : public BodyWriter
{
public:
    virtual Task<> end() = 0;

    static std::unique_ptr<Http1BodyWriter> create(
        TransferMode mode,
        io::StreamPtr stream,
        size_t contentLength = 0);
};

} // namespace nitrocoro::http
