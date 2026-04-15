/**
 * @file Http2ResponseSink.cc
 * @brief HTTP/2 ResponseSink implementation
 */
#include "Http2ResponseSink.h"

#include "Http2Session.h"

namespace nitrocoro::http2
{

namespace
{

class Http2BodyWriter : public http::BodyWriter
{
public:
    Http2BodyWriter(std::shared_ptr<Http2Session> session, uint32_t streamId)
        : session_(std::move(session)), streamId_(streamId) {}

    Task<> write(std::string_view data) override
    {
        co_await session_->sendData(streamId_, data, false);
    }

private:
    std::shared_ptr<Http2Session> session_;
    uint32_t streamId_;
};

} // namespace

Http2ResponseSink::Http2ResponseSink(std::weak_ptr<Http2Session> session, uint32_t streamId, bool isHeadMethod)
    : session_(std::move(session)), streamId_(streamId), isHeadMethod_(isHeadMethod)
{
}

Task<> Http2ResponseSink::write(const http::HttpResponse & resp, std::string_view body)
{
    auto s = session_.lock();
    if (!s)
        co_return;
    bool endStream = isHeadMethod_ || body.empty();
    assert(body.size() == resp.contentLength);
    co_await s->sendHeaders(streamId_, resp, endStream);
    if (!endStream)
        co_await s->sendData(streamId_, body, true);
}

Task<> Http2ResponseSink::write(const http::HttpResponse & resp, const http::BodyWriterFn & bodyWriterFn)
{
    auto s = session_.lock();
    if (!s)
        co_return;
    if (isHeadMethod_)
    {
        co_await s->sendHeaders(streamId_, resp, true);
        co_return;
    }
    co_await s->sendHeaders(streamId_, resp, false);
    Http2BodyWriter writer(s, streamId_);
    co_await bodyWriterFn(writer);
    co_await s->sendData(streamId_, {}, true);
}

} // namespace nitrocoro::http2
