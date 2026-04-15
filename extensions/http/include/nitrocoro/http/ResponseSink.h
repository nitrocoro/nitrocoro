/**
 * @file ResponseSink.h
 * @brief Protocol-agnostic interface for sending HTTP responses
 */
#pragma once
#include <nitrocoro/http/BodyWriter.h>
#include <nitrocoro/http/HttpMessage.h>

#include <nitrocoro/core/Task.h>

namespace nitrocoro::http
{

class ResponseSink
{
public:
    virtual ~ResponseSink() = default;

    virtual Task<> write(const HttpResponse & resp, std::string_view body) = 0;
    virtual Task<> write(const HttpResponse & resp, const BodyWriterFn & bodyWriterFn) = 0;
};

} // namespace nitrocoro::http
