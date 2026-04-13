/**
 * @file ResponseSink.h
 * @brief Protocol-agnostic interface for sending HTTP responses
 */
#pragma once
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/http/BodyWriter.h>

#include <nitrocoro/core/Task.h>

namespace nitrocoro::http
{

class ResponseSink
{
public:
    virtual ~ResponseSink() = default;

    virtual Task<> send(const HttpResponse & resp, std::string_view body, bool ignoreBody = false) = 0;
    virtual Task<> sendStream(const HttpResponse & resp, const BodyWriterFn & bodyWriterFn) = 0;
};

} // namespace nitrocoro::http
