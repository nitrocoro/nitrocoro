/**
 * @file HttpStream.h
 * @brief Compatibility header - includes all HTTP stream components
 */
#pragma once

#include <nitrocoro/http/stream/HttpIncomingStream.h>
#include <nitrocoro/http/stream/HttpOutgoingStream.h>

#include <memory>

namespace nitrocoro::http
{

using IncomingRequest = HttpIncomingStream<HttpRequest>;
using IncomingResponse = HttpIncomingStream<HttpResponse>;

using ClientRequest = HttpOutgoingStream<HttpRequest>;
using ServerResponse = HttpOutgoingStream<HttpResponse>;

using IncomingRequestPtr = std::shared_ptr<IncomingRequest>;
using IncomingResponsePtr = std::shared_ptr<IncomingResponse>;

using ClientRequestPtr = std::shared_ptr<ClientRequest>;
using ServerResponsePtr = std::shared_ptr<ServerResponse>;

} // namespace nitrocoro::http
