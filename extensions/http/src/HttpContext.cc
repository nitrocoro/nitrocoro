/**
 * @file HttpContext.cc
 * @brief HTTP context implementations
 */
#include "HttpContext.h"
#include "HttpParser.h"
#include <nitrocoro/http/HttpMessage.h>

namespace nitrocoro::http
{

template <typename MessageType>
Task<std::optional<MessageType>> HttpContext<MessageType>::receiveMessage()
{
    HttpParser<MessageType> parser;

    while (!parser.isHeaderComplete())
    {
        size_t pos = buffer_->find("\r\n");
        if (pos == std::string::npos)
        {
            char * writePtr = buffer_->prepareWrite(4096);
            size_t n = co_await stream_->read(writePtr, 4096);
            // TODO: last message, or half message?
            if (n == 0)
                co_return std::nullopt;
            buffer_->commitWrite(n);
            continue;
        }

        std::string_view line = buffer_->view().substr(0, pos);
        parser.parseLine(line);
        buffer_->consume(pos + 2);
    }

    co_return parser.extractMessage();
}

// Explicit instantiations
template class HttpContext<HttpRequest>;
template class HttpContext<HttpResponse>;

} // namespace nitrocoro::http
