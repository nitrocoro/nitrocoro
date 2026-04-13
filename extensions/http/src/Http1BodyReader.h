/**
 * @file Http1BodyReader.h
 * @brief HTTP/1.1 body reader base class
 */
#pragma once
#include <nitrocoro/http/BodyReader.h>
#include <nitrocoro/http/HttpTypes.h>

#include <nitrocoro/core/Mutex.h>
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/utils/StringBuffer.h>

namespace nitrocoro::http
{

class Http1BodyReader : public BodyReader
{
public:
    static std::shared_ptr<BodyReader> create(
        io::StreamPtr stream,
        std::shared_ptr<utils::StringBuffer> buffer,
        TransferMode mode,
        size_t contentLength);

    Task<size_t> read(char * buf, size_t len) override;
    Task<> drain() override;

protected:
    virtual Task<size_t> readImpl(char * buf, size_t len) = 0;

private:
    Mutex mutex_;
    bool draining_{ false };
};

} // namespace nitrocoro::http
