/**
 * @file UntilCloseWriter.h
 * @brief Body writer for close-delimited transfer (HTTP/1.0 fallback)
 */
#pragma once
#include "../Http1BodyWriter.h"
#include <nitrocoro/io/Stream.h>

namespace nitrocoro::http
{

class UntilCloseWriter : public Http1BodyWriter
{
public:
    explicit UntilCloseWriter(io::StreamPtr stream)
        : stream_(std::move(stream)) {}

    Task<> write(std::string_view data) override;
    Task<> end() override;

private:
    io::StreamPtr stream_;
};

} // namespace nitrocoro::http
