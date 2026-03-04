/**
 * @file ChunkedWriter.h
 * @brief Body writer for chunked transfer encoding
 */
#pragma once
#include <nitrocoro/http/BodyWriter.h>
#include <nitrocoro/io/Stream.h>

namespace nitrocoro::http
{

class ChunkedWriter : public BodyWriter
{
public:
    explicit ChunkedWriter(io::StreamPtr stream)
        : stream_(std::move(stream)) {}

    Task<> write(std::string_view data) override;
    Task<> end() override;

private:
    io::StreamPtr stream_;
};

} // namespace nitrocoro::http
