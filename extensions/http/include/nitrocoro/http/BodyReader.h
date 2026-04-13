/**
 * @file BodyReader.h
 * @brief Body reader interface
 */
#pragma once
#include <nitrocoro/core/Task.h>

#include <cstddef>
#include <memory>

namespace nitrocoro::http
{

class BodyReader
{
public:
    virtual ~BodyReader() = default;
    virtual Task<size_t> read(char * buf, size_t len) = 0;
    virtual Task<> drain() = 0;
    virtual bool isComplete() const = 0;
};

using BodyReaderPtr = std::shared_ptr<BodyReader>;

} // namespace nitrocoro::http
