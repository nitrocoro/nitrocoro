/**
 * @file RequestSink.h
 * @brief Protocol-agnostic interface for sending HTTP requests
 */
#pragma once
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/http/BodyWriter.h>

#include <nitrocoro/core/Task.h>

namespace nitrocoro::http
{

class RequestSink
{
public:
    virtual ~RequestSink() = default;

    virtual Task<> send(const HttpRequest & req, std::string_view body) = 0;
    virtual Task<> sendStream(const HttpRequest & req, const BodyWriterFn & bodyWriterFn) = 0;
};

} // namespace nitrocoro::http
