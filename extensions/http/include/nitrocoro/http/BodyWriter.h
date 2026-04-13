/**
 * @file BodyWriter.h
 * @brief Body writer interface
 */
#pragma once
#include <nitrocoro/core/Task.h>

#include <functional>
#include <string_view>

namespace nitrocoro::http
{

class BodyWriter
{
public:
    virtual ~BodyWriter() = default;
    virtual Task<> write(std::string_view data) = 0;
};

using BodyWriterFn = std::function<Task<>(BodyWriter &)>;

} // namespace nitrocoro::http
