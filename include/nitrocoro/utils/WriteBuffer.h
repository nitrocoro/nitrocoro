/**
 * @file WriteBuffer.h
 * @brief Interface for writable buffer
 */
#pragma once

#include <cstddef>

namespace nitrocoro::utils
{

class WriteBuffer
{
public:
    virtual ~WriteBuffer() = default;
    virtual char * prepareWrite(size_t n) = 0;
    virtual char * beginWrite() = 0;
    virtual size_t writableSize() const = 0;
    virtual void commitWrite(size_t n) = 0;
};

} // namespace nitrocoro::utils
