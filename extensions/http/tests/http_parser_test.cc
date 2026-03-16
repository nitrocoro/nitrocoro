/**
 * @file http_parser_test.cc
 * @brief Tests for HttpParser
 */
#include "../src/HttpParser.h"
#include <nitrocoro/http/HttpMessageAccessor.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro::http;

// ── Request Parser Tests ──────────────────────────────────────────────────────

NITRO_TEST(http_parser_request_basic)
{
    HttpParser<HttpRequest> parser;

    auto state = parser.parseLine("GET /hello HTTP/1.1");
    NITRO_CHECK_EQ(state, HttpParserState::ExpectHeader);

    state = parser.parseLine("Host: example.com");
    NITRO_CHECK_EQ(state, HttpParserState::ExpectHeader);

    state = parser.parseLine("");
    NITRO_CHECK_EQ(state, HttpParserState::HeaderComplete);

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.method, methods::Get);
    NITRO_CHECK_EQ(result.message.path, "/hello");
    NITRO_CHECK_EQ(result.message.version, Version::kHttp11);
    co_return;
}

NITRO_TEST(http_parser_request_with_query)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("GET /search?q=hello+world&page=1 HTTP/1.1");
    parser.parseLine("Host: example.com");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.path, "/search");
    NITRO_CHECK_EQ(result.message.query, "q=hello+world&page=1");
    NITRO_CHECK(result.message.queries.contains("q"));
    NITRO_CHECK_EQ(result.message.queries.at("q"), "hello world");
    NITRO_CHECK(result.message.queries.contains("page"));
    NITRO_CHECK_EQ(result.message.queries.at("page"), "1");
    co_return;
}

NITRO_TEST(http_parser_request_query_accessor)
{
    // queryString() returns raw query
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("GET /search?q=hello+world&page=1 HTTP/1.1");
        parser.parseLine("");
        HttpRequestAccessor accessor(parser.extractResult().message);
        NITRO_CHECK_EQ(accessor.queryString(), "q=hello+world&page=1");
        NITRO_CHECK_EQ(accessor.getQuery("q"), "hello world");
        NITRO_CHECK_EQ(accessor.getQuery("page"), "1");
        NITRO_CHECK(accessor.getQuery("missing").empty());
    }
    // first-wins on duplicate keys
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("GET /search?tag=a&tag=b HTTP/1.1");
        parser.parseLine("");
        HttpRequestAccessor accessor(parser.extractResult().message);
        NITRO_CHECK_EQ(accessor.getQuery("tag"), "a");
    }
    // no query string
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("GET /search HTTP/1.1");
        parser.parseLine("");
        HttpRequestAccessor accessor(parser.extractResult().message);
        NITRO_CHECK(accessor.queryString().empty());
        NITRO_CHECK(accessor.queries().empty());
    }
    co_return;
}

NITRO_TEST(http_parser_multi_queries)
{
    // basic multi-value
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("GET /search?tag=a&tag=b&tag=c HTTP/1.1");
        parser.parseLine("");
        auto result = parser.extractResult();
        HttpRequestAccessor accessor(std::move(result.message));
        auto mq = accessor.multiQueries();
        NITRO_CHECK_EQ(mq["tag"].size(), 3);
        NITRO_CHECK_EQ(mq["tag"][0], "a");
        NITRO_CHECK_EQ(mq["tag"][1], "b");
        NITRO_CHECK_EQ(mq["tag"][2], "c");
    }
    // decode
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("GET /search?key=%2B%26%3D&val=%E4%B8%AD%E6%96%87 HTTP/1.1");
        parser.parseLine("");
        auto result = parser.extractResult();
        HttpRequestAccessor accessor(std::move(result.message));
        auto mq = accessor.multiQueries();
        NITRO_CHECK_EQ(mq["key"][0], "+&=");
        NITRO_CHECK_EQ(mq["val"][0], "中文");
    }
    // key with no value
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("GET /search?flag&other=1 HTTP/1.1");
        parser.parseLine("");
        auto result = parser.extractResult();
        HttpRequestAccessor accessor(std::move(result.message));
        auto mq = accessor.multiQueries();
        NITRO_CHECK(mq.contains("flag"));
        NITRO_CHECK(mq["flag"].empty());
        NITRO_CHECK_EQ(mq["other"][0], "1");
    }
    // empty query string
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("GET /search HTTP/1.1");
        parser.parseLine("");
        auto result = parser.extractResult();
        HttpRequestAccessor accessor(std::move(result.message));
        NITRO_CHECK(accessor.multiQueries().empty());
    }
    co_return;
}

NITRO_TEST(http_parser_request_content_length)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("POST /data HTTP/1.1");
    parser.parseLine("Host: example.com");
    parser.parseLine("Content-Length: 100");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.transferMode, TransferMode::ContentLength);
    NITRO_CHECK_EQ(result.message.contentLength, 100);
    co_return;
}

NITRO_TEST(http_parser_request_transfer_encoding)
{
    // chunked
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("POST /data HTTP/1.1");
        parser.parseLine("Transfer-Encoding: chunked");
        parser.parseLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!result.error());
        NITRO_CHECK_EQ(result.message.transferMode, TransferMode::Chunked);
    }
    // gzip, chunked
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("POST /data HTTP/1.1");
        parser.parseLine("Transfer-Encoding: gzip, chunked");
        parser.parseLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!result.error());
        NITRO_CHECK_EQ(result.message.transferMode, TransferMode::Chunked);
    }
    // unsupported
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("POST /data HTTP/1.1");
        parser.parseLine("Transfer-Encoding: gzip");
        auto state = parser.parseLine("");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(parser.extractResult().errorCode, HttpParseError::UnsupportedTransferEncoding);
    }
    co_return;
}

NITRO_TEST(http_parser_request_keep_alive_http11)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("GET /hello HTTP/1.1");
    parser.parseLine("Host: example.com");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK(result.message.keepAlive); // HTTP/1.1 default
    co_return;
}

NITRO_TEST(http_parser_request_keep_alive_http10)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("GET /hello HTTP/1.0");
    parser.parseLine("Host: example.com");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK(!result.message.keepAlive); // HTTP/1.0 default
    co_return;
}

NITRO_TEST(http_parser_request_connection_close)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("GET /hello HTTP/1.1");
    parser.parseLine("Host: example.com");
    parser.parseLine("Connection: close");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK(!result.message.keepAlive);
    co_return;
}

// ── Response Parser Tests ─────────────────────────────────────────────────────

NITRO_TEST(http_parser_response_basic)
{
    HttpParser<HttpResponse> parser;

    auto state = parser.parseLine("HTTP/1.1 200 OK");
    NITRO_CHECK_EQ(state, HttpParserState::ExpectHeader);

    state = parser.parseLine("Content-Type: text/plain");
    NITRO_CHECK_EQ(state, HttpParserState::ExpectHeader);

    state = parser.parseLine("");
    NITRO_CHECK_EQ(state, HttpParserState::HeaderComplete);

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.version, Version::kHttp11);
    NITRO_CHECK_EQ(result.message.statusCode, StatusCode::k200OK);
    NITRO_CHECK_EQ(result.message.statusReason, "OK");
    co_return;
}

NITRO_TEST(http_parser_response_content_length)
{
    HttpParser<HttpResponse> parser;

    parser.parseLine("HTTP/1.1 200 OK");
    parser.parseLine("Content-Length: 50");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.transferMode, TransferMode::ContentLength);
    NITRO_CHECK_EQ(result.message.contentLength, 50);
    co_return;
}

NITRO_TEST(http_parser_response_transfer_encoding)
{
    // chunked
    {
        HttpParser<HttpResponse> parser;
        parser.parseLine("HTTP/1.1 200 OK");
        parser.parseLine("Transfer-Encoding: chunked");
        parser.parseLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!result.error());
        NITRO_CHECK_EQ(result.message.transferMode, TransferMode::Chunked);
    }
    // gzip, chunked
    {
        HttpParser<HttpResponse> parser;
        parser.parseLine("HTTP/1.1 200 OK");
        parser.parseLine("Transfer-Encoding: gzip, chunked");
        parser.parseLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!result.error());
        NITRO_CHECK_EQ(result.message.transferMode, TransferMode::Chunked);
    }
    // unsupported
    {
        HttpParser<HttpResponse> parser;
        parser.parseLine("HTTP/1.1 200 OK");
        parser.parseLine("Transfer-Encoding: deflate");
        auto state = parser.parseLine("");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(parser.extractResult().errorCode, HttpParseError::UnsupportedTransferEncoding);
    }
    co_return;
}

NITRO_TEST(http_parser_response_until_close)
{
    HttpParser<HttpResponse> parser;

    parser.parseLine("HTTP/1.1 200 OK");
    parser.parseLine(""); // No Content-Length or Transfer-Encoding

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.transferMode, TransferMode::UntilClose);
    co_return;
}

NITRO_TEST(http_parser_response_connection_close)
{
    HttpParser<HttpResponse> parser;

    parser.parseLine("HTTP/1.1 200 OK");
    parser.parseLine("Connection: close");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK(result.message.shouldClose);
    co_return;
}

NITRO_TEST(http_parser_response_http10_default_close)
{
    HttpParser<HttpResponse> parser;

    parser.parseLine("HTTP/1.0 200 OK");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK(result.message.shouldClose); // HTTP/1.0 default
    co_return;
}

NITRO_TEST(http_parser_response_status_code)
{
    // normal: with reason phrase
    {
        HttpParser<HttpResponse> parser;
        parser.parseLine("HTTP/1.1 200 OK");
        parser.parseLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!result.error());
        NITRO_CHECK_EQ(result.message.statusCode, StatusCode::k200OK);
        NITRO_CHECK_EQ(result.message.statusReason, "OK");
    }
    // normal: without reason phrase
    {
        HttpParser<HttpResponse> parser;
        parser.parseLine("HTTP/1.1 204 ");
        parser.parseLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!result.error());
        NITRO_CHECK_EQ(result.message.statusCode, StatusCode::k204NoContent);
        NITRO_CHECK(result.message.statusReason.empty());
    }
    // normal: without reason phrase and trailing space
    {
        HttpParser<HttpResponse> parser;
        parser.parseLine("HTTP/1.1 204");
        parser.parseLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!result.error());
        NITRO_CHECK_EQ(result.message.statusCode, StatusCode::k204NoContent);
        NITRO_CHECK(result.message.statusReason.empty());
    }
    // non-standard code
    {
        HttpParser<HttpResponse> parser;
        parser.parseLine("HTTP/1.1 999 Custom");
        parser.parseLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!result.error());
        NITRO_CHECK_EQ(result.message.statusCode, 999);
    }
    // invalid version
    {
        HttpParser<HttpResponse> parser;
        auto state = parser.parseLine("HTTP/2.0 200 OK");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK(parser.extractResult().error());
    }
    // missing first space
    {
        HttpParser<HttpResponse> parser;
        auto state = parser.parseLine("HTTP/1.1");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK(parser.extractResult().error());
    }
    // non-numeric status code
    {
        HttpParser<HttpResponse> parser;
        auto state = parser.parseLine("HTTP/1.1 abc OK");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(parser.extractResult().errorCode, HttpParseError::MalformedRequestLine);
    }
    // status code too short (2 digits)
    {
        HttpParser<HttpResponse> parser;
        auto state = parser.parseLine("HTTP/1.1 99 OK");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(parser.extractResult().errorCode, HttpParseError::MalformedRequestLine);
    }
    // status code too long (4 digits) but in range
    {
        HttpParser<HttpResponse> parser;
        auto state = parser.parseLine("HTTP/1.1 1000 OK");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(parser.extractResult().errorCode, HttpParseError::MalformedRequestLine);
    }
    co_return;
}

NITRO_TEST(http_parser_empty_header_value)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("GET /hello HTTP/1.1");
    parser.parseLine("Host: example.com");
    parser.parseLine("X-Empty:"); // Empty header value
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK(result.message.headers.contains("x-empty"));
    NITRO_CHECK(result.message.headers.at("x-empty").value().empty());
    co_return;
}

NITRO_TEST(http_parser_header_with_spaces)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("GET /hello HTTP/1.1");
    parser.parseLine("Host: example.com");
    parser.parseLine("  X-Spaced  :  value with spaces  ");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK(result.message.headers.contains("x-spaced"));
    NITRO_CHECK_EQ(result.message.headers.at("x-spaced").value(), "value with spaces");
    co_return;
}

NITRO_TEST(http_parser_identity_encoding)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("POST /data HTTP/1.1");
    parser.parseLine("Host: example.com");
    parser.parseLine("Transfer-Encoding: identity");
    parser.parseLine("Content-Length: 10");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.transferMode, TransferMode::ContentLength);
    NITRO_CHECK_EQ(result.message.contentLength, 10);
    co_return;
}
NITRO_TEST(http_parser_request_duplicate_content_length_same)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("POST /data HTTP/1.1");
    parser.parseLine("Host: example.com");
    parser.parseLine("Content-Length: 42");
    parser.parseLine("Content-Length: 42");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.contentLength, 42);
    co_return;
}

NITRO_TEST(http_parser_request_duplicate_content_length_conflict)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("POST /data HTTP/1.1");
    parser.parseLine("Host: example.com");
    parser.parseLine("Content-Length: 42");
    parser.parseLine("Content-Length: 99");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(result.error());
    NITRO_CHECK_EQ(result.errorCode, HttpParseError::AmbiguousContentLength);
    co_return;
}

NITRO_TEST(http_parser_request_cookie)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("GET /hello HTTP/1.1");
    parser.parseLine("Host: example.com");
    parser.parseLine("Cookie: session=abc123; user=john");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.cookies.at("session"), "abc123");
    NITRO_CHECK_EQ(result.message.cookies.at("user"), "john");
    co_return;
}

NITRO_TEST(http_parser_response_set_cookie)
{
    HttpParser<HttpResponse> parser;

    parser.parseLine("HTTP/1.1 200 OK");
    parser.parseLine("Set-Cookie: session=abc123; Path=/; HttpOnly");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.cookies.size(), 1);
    NITRO_CHECK_EQ(result.message.cookies[0].name, "session");
    NITRO_CHECK_EQ(result.message.cookies[0].value, "abc123");
    co_return;
}

NITRO_TEST(http_parser_request_encoded_path)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("GET /user/john%20doe HTTP/1.1");
    parser.parseLine("Host: example.com");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.rawPath, "/user/john%20doe");
    NITRO_CHECK_EQ(result.message.path, "/user/john doe");
    co_return;
}

NITRO_TEST(http_parser_request_keep_alive_http10_explicit)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("GET /hello HTTP/1.0");
    parser.parseLine("Host: example.com");
    parser.parseLine("Connection: keep-alive");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK(result.message.keepAlive);
    co_return;
}

NITRO_TEST(http_parser_response_keep_alive_http10_explicit)
{
    HttpParser<HttpResponse> parser;

    parser.parseLine("HTTP/1.0 200 OK");
    parser.parseLine("Connection: keep-alive");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK(!result.message.shouldClose);
    co_return;
}

NITRO_TEST(http_parser_transfer_encoding_overrides_content_length)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("POST /data HTTP/1.1");
    parser.parseLine("Host: example.com");
    parser.parseLine("Content-Length: 100");
    parser.parseLine("Transfer-Encoding: chunked");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.transferMode, TransferMode::Chunked);
    co_return;
}

NITRO_TEST(http_parser_invalid_header_no_colon)
{
    HttpParser<HttpRequest> parser;

    parser.parseLine("GET /hello HTTP/1.1");
    parser.parseLine("Host: example.com");
    parser.parseLine("InvalidHeaderWithoutColon");
    parser.parseLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!result.error()); // silently ignored
    NITRO_CHECK(!result.message.headers.contains(HttpHeader::toLower("InvalidHeaderWithoutColon")));
    co_return;
}

NITRO_TEST(http_parser_request_invalid_content_length)
{
    // non-numeric
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("POST /data HTTP/1.1");
        parser.parseLine("Content-Length: abc");
        auto state = parser.parseLine("");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(parser.extractResult().errorCode, HttpParseError::AmbiguousContentLength);
    }
    // negative string
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("POST /data HTTP/1.1");
        parser.parseLine("Content-Length: -1");
        auto state = parser.parseLine("");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(parser.extractResult().errorCode, HttpParseError::AmbiguousContentLength);
    }
    co_return;
}

NITRO_TEST(http_parser_response_invalid_content_length)
{
    HttpParser<HttpResponse> parser;
    parser.parseLine("HTTP/1.1 200 OK");
    parser.parseLine("Content-Length: abc");
    auto state = parser.parseLine("");
    NITRO_CHECK_EQ(state, HttpParserState::Error);
    NITRO_CHECK_EQ(parser.extractResult().errorCode, HttpParseError::AmbiguousContentLength);
    co_return;
}

NITRO_TEST(http_parser_request_line)
{
    // missing first space
    {
        HttpParser<HttpRequest> parser;
        auto state = parser.parseLine("GET");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(parser.extractResult().errorCode, HttpParseError::MalformedRequestLine);
    }
    // missing second space (no version)
    {
        HttpParser<HttpRequest> parser;
        auto state = parser.parseLine("GET /hello");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(parser.extractResult().errorCode, HttpParseError::MalformedRequestLine);
    }
    // invalid method
    {
        HttpParser<HttpRequest> parser;
        auto state = parser.parseLine("INVALID /hello HTTP/1.1");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(parser.extractResult().errorCode, HttpParseError::MalformedRequestLine);
    }
    // invalid version
    {
        HttpParser<HttpRequest> parser;
        auto state = parser.parseLine("GET /hello HTTP/2.0");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(parser.extractResult().errorCode, HttpParseError::MalformedRequestLine);
    }
    // empty path — tolerated, normalized to /
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("GET  HTTP/1.1");
        parser.parseLine("Host: example.com");
        parser.parseLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!result.error());
        NITRO_CHECK_EQ(result.message.path, "/");
        NITRO_CHECK(result.message.rawPath.empty());
    }
    co_return;
}

NITRO_TEST(http_parser_invalid_header_token)
{
    // header name with space — silently ignored
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("GET /hello HTTP/1.1");
        parser.parseLine("Host: example.com");
        parser.parseLine("Bad Name: value");
        parser.parseLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!result.error());
        NITRO_CHECK(!result.message.headers.contains("bad name"));
    }
    // header name with control character — silently ignored
    {
        HttpParser<HttpRequest> parser;
        parser.parseLine("GET /hello HTTP/1.1");
        parser.parseLine("Host: example.com");
        parser.parseLine("Bad\x01Name: value");
        parser.parseLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!result.error());
        NITRO_CHECK(!result.message.headers.contains("bad\x01name"));
    }
    co_return;
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
