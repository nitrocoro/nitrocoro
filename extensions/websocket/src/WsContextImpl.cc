/**
 * @file WsContextImpl.cc
 * @brief WsContextImpl implementation
 */
#include "WsContextImpl.h"

namespace nitrocoro::websocket
{

WsContextImpl::WsContextImpl(http::IncomingRequestPtr req,
                             http::ServerResponsePtr resp,
                             Future<WsConnection> connFuture)
    : req_(std::move(req))
    , resp_(std::move(resp))
    , acceptFuture_(acceptPromise_.get_future().share())
    , connFuture_(std::move(connFuture))
{
}

WsContextImpl::~WsContextImpl()
{
    if (!accepted_.test_and_set())
    {
        acceptPromise_.set_value(false);
    }
}

Task<WsConnection> WsContextImpl::accept()
{
    accepted_.test_and_set();
    acceptPromise_.set_value(true);
    co_return co_await connFuture_.get();
}

} // namespace nitrocoro::websocket
