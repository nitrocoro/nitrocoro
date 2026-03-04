/**
 * @file BodyWriter.cc
 * @brief Factory implementation for creating body writers
 */
#include "body_writer/ChunkedWriter.h"
#include "body_writer/ContentLengthWriter.h"
#include "body_writer/UntilCloseWriter.h"
#include <nitrocoro/http/BodyWriter.h>

namespace nitrocoro::http
{

std::unique_ptr<BodyWriter> BodyWriter::create(
    TransferMode mode,
    io::StreamPtr stream,
    size_t contentLength)
{
    if (mode == TransferMode::ContentLength)
        return std::make_unique<ContentLengthWriter>(std::move(stream), contentLength);
    if (mode == TransferMode::UntilClose)
        return std::make_unique<UntilCloseWriter>(std::move(stream));
    return std::make_unique<ChunkedWriter>(std::move(stream));
}

} // namespace nitrocoro::http
