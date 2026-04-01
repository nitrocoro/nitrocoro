/**
 * @file http_parser_test.cc
 * @brief Tests for HttpParser
 */
#include "../src/HttpParser.h"
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro::http;

// ── Request Parser Tests ──────────────────────────────────────────────────────

NITRO_TEST(http_parser_request_basic)
{
    HttpParser<HttpRequest> parser;

    auto state = parser.feedLine("GET /hello HTTP/1.1");
    NITRO_CHECK_EQ(state, HttpParserState::ExpectHeader);

    state = parser.feedLine("Host: example.com");
    NITRO_CHECK_EQ(state, HttpParserState::ExpectHeader);

    state = parser.feedLine("");
    NITRO_CHECK_EQ(state, HttpParserState::HeaderComplete);

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).method, methods::Get);
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).path, "/hello");
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).version, Version::kHttp11);
    HttpRequestAccessor accessor(std::get<HttpRequest>(std::move(result)));
    NITRO_CHECK_EQ(accessor.method(), methods::Get);
    NITRO_CHECK_EQ(accessor.path(), "/hello");
    NITRO_CHECK_EQ(accessor.version(), Version::kHttp11);
    NITRO_CHECK_EQ(accessor.getHeader("host"), "example.com");
    NITRO_CHECK_EQ(accessor.getHeader(HttpHeader::NameCode::Host), "example.com");
    NITRO_CHECK(accessor.getHeader("missing").empty());
    co_return;
}

NITRO_TEST(http_parser_request_with_query)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("GET /search?q=hello+world&page=1 HTTP/1.1");
    parser.feedLine("Host: example.com");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).path, "/search");
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).query, "q=hello+world&page=1");
    NITRO_CHECK(std::get<HttpRequest>(result).queries.contains("q"));
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).queries.at("q"), "hello world");
    NITRO_CHECK(std::get<HttpRequest>(result).queries.contains("page"));
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).queries.at("page"), "1");
    co_return;
}

NITRO_TEST(http_parser_request_query_accessor)
{
    // queryString() returns raw query
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("GET /search?q=hello+world&page=1 HTTP/1.1");
        parser.feedLine("");
        NITRO_REQUIRE_EQ(parser.state(), HttpParserState::HeaderComplete);
        auto message = std::get<HttpRequest>(parser.extractResult());
        HttpRequestAccessor accessor(message);
        NITRO_CHECK_EQ(accessor.queryString(), "q=hello+world&page=1");
        NITRO_CHECK_EQ(accessor.getQuery("q"), "hello world");
        NITRO_CHECK_EQ(accessor.getQuery("page"), "1");
        NITRO_CHECK(accessor.getQuery("missing").empty());
    }
    // first-wins on duplicate keys
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("GET /search?tag=a&tag=b HTTP/1.1");
        parser.feedLine("");
        NITRO_REQUIRE_EQ(parser.state(), HttpParserState::HeaderComplete);
        auto message = std::get<HttpRequest>(parser.extractResult());
        HttpRequestAccessor accessor(message);
        NITRO_CHECK_EQ(accessor.getQuery("tag"), "a");
        NITRO_CHECK_EQ(accessor.queries().size(), 1);
    }
    // no query string
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("GET /search HTTP/1.1");
        parser.feedLine("");
        NITRO_REQUIRE_EQ(parser.state(), HttpParserState::HeaderComplete);
        auto message = std::get<HttpRequest>(parser.extractResult());
        HttpRequestAccessor accessor(message);
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
        parser.feedLine("GET /search?tag=a&tag=b&tag=c HTTP/1.1");
        parser.feedLine("");
        auto result = parser.extractResult();
        HttpRequestAccessor accessor(std::move(std::get<HttpRequest>(result)));
        auto mq = accessor.multiQueries();
        NITRO_CHECK_EQ(mq["tag"].size(), 3);
        NITRO_CHECK_EQ(mq["tag"][0], "a");
        NITRO_CHECK_EQ(mq["tag"][1], "b");
        NITRO_CHECK_EQ(mq["tag"][2], "c");
    }
    // decode
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("GET /search?key=%2B%26%3D&val=%E4%B8%AD%E6%96%87 HTTP/1.1");
        parser.feedLine("");
        auto result = parser.extractResult();
        HttpRequestAccessor accessor(std::move(std::get<HttpRequest>(result)));
        auto mq = accessor.multiQueries();
        NITRO_CHECK_EQ(mq["key"][0], "+&=");
        NITRO_CHECK_EQ(mq["val"][0], "中文");
    }
    // key with no value
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("GET /search?flag&other=1 HTTP/1.1");
        parser.feedLine("");
        auto result = parser.extractResult();
        HttpRequestAccessor accessor(std::move(std::get<HttpRequest>(result)));
        auto mq = accessor.multiQueries();
        NITRO_CHECK(mq.contains("flag"));
        NITRO_CHECK(mq["flag"].empty());
        NITRO_CHECK_EQ(mq["other"][0], "1");
    }
    // empty query string
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("GET /search HTTP/1.1");
        parser.feedLine("");
        auto result = parser.extractResult();
        HttpRequestAccessor accessor(std::move(std::get<HttpRequest>(result)));
        NITRO_CHECK(accessor.multiQueries().empty());
    }
    co_return;
}

NITRO_TEST(http_parser_request_content_length)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("POST /data HTTP/1.1");
    parser.feedLine("Host: example.com");
    parser.feedLine("Content-Length: 100");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).transferMode, TransferMode::ContentLength);
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).contentLength, 100);
    co_return;
}

NITRO_TEST(http_parser_request_transfer_encoding)
{
    // chunked
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("POST /data HTTP/1.1");
        parser.feedLine("Transfer-Encoding: chunked");
        parser.feedLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
        NITRO_CHECK_EQ(std::get<HttpRequest>(result).transferMode, TransferMode::Chunked);
    }
    // gzip, chunked
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("POST /data HTTP/1.1");
        parser.feedLine("Transfer-Encoding: gzip, chunked");
        parser.feedLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
        NITRO_CHECK_EQ(std::get<HttpRequest>(result).transferMode, TransferMode::Chunked);
    }
    // unsupported
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("POST /data HTTP/1.1");
        parser.feedLine("Transfer-Encoding: gzip");
        auto state = parser.feedLine("");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(std::get<HttpParseError>(parser.extractResult()).code, HttpParseError::UnsupportedTransferEncoding);
    }
    co_return;
}

NITRO_TEST(http_parser_request_keep_alive_http11)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("GET /hello HTTP/1.1");
    parser.feedLine("Host: example.com");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK(std::get<HttpRequest>(result).keepAlive); // HTTP/1.1 default
    co_return;
}

NITRO_TEST(http_parser_request_keep_alive_http10)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("GET /hello HTTP/1.0");
    parser.feedLine("Host: example.com");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK(!std::get<HttpRequest>(result).keepAlive); // HTTP/1.0 default
    co_return;
}

NITRO_TEST(http_parser_request_connection_close)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("GET /hello HTTP/1.1");
    parser.feedLine("Host: example.com");
    parser.feedLine("Connection: close");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK(!std::get<HttpRequest>(result).keepAlive);
    co_return;
}

// ── Response Parser Tests ─────────────────────────────────────────────────────

NITRO_TEST(http_parser_response_basic)
{
    HttpParser<HttpResponse> parser;

    auto state = parser.feedLine("HTTP/1.1 200 OK");
    NITRO_CHECK_EQ(state, HttpParserState::ExpectHeader);

    state = parser.feedLine("Content-Type: text/plain");
    NITRO_CHECK_EQ(state, HttpParserState::ExpectHeader);

    state = parser.feedLine("");
    NITRO_CHECK_EQ(state, HttpParserState::HeaderComplete);

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK_EQ(std::get<HttpResponse>(result).version, Version::kHttp11);
    NITRO_CHECK_EQ(std::get<HttpResponse>(result).statusCode, StatusCode::k200OK);
    NITRO_CHECK_EQ(std::get<HttpResponse>(result).statusReason, "OK");
    HttpResponseAccessor accessor(std::get<HttpResponse>(std::move(result)));
    NITRO_CHECK_EQ(accessor.version(), Version::kHttp11);
    NITRO_CHECK_EQ(accessor.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(accessor.statusReason(), "OK");
    NITRO_CHECK_EQ(accessor.getHeader("content-type"), "text/plain");
    NITRO_CHECK_EQ(accessor.getHeader(HttpHeader::NameCode::ContentType), "text/plain");
    NITRO_CHECK(accessor.getHeader("missing").empty());
    co_return;
}

NITRO_TEST(http_parser_response_content_length)
{
    HttpParser<HttpResponse> parser;

    parser.feedLine("HTTP/1.1 200 OK");
    parser.feedLine("Content-Length: 50");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK_EQ(std::get<HttpResponse>(result).transferMode, TransferMode::ContentLength);
    NITRO_CHECK_EQ(std::get<HttpResponse>(result).contentLength, 50);
    co_return;
}

NITRO_TEST(http_parser_response_transfer_encoding)
{
    // chunked
    {
        HttpParser<HttpResponse> parser;
        parser.feedLine("HTTP/1.1 200 OK");
        parser.feedLine("Transfer-Encoding: chunked");
        parser.feedLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
        NITRO_CHECK_EQ(std::get<HttpResponse>(result).transferMode, TransferMode::Chunked);
    }
    // gzip, chunked
    {
        HttpParser<HttpResponse> parser;
        parser.feedLine("HTTP/1.1 200 OK");
        parser.feedLine("Transfer-Encoding: gzip, chunked");
        parser.feedLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
        NITRO_CHECK_EQ(std::get<HttpResponse>(result).transferMode, TransferMode::Chunked);
    }
    // unsupported
    {
        HttpParser<HttpResponse> parser;
        parser.feedLine("HTTP/1.1 200 OK");
        parser.feedLine("Transfer-Encoding: deflate");
        auto state = parser.feedLine("");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(std::get<HttpParseError>(parser.extractResult()).code, HttpParseError::UnsupportedTransferEncoding);
    }
    co_return;
}

NITRO_TEST(http_parser_response_until_close)
{
    HttpParser<HttpResponse> parser;

    parser.feedLine("HTTP/1.1 200 OK");
    parser.feedLine(""); // No Content-Length or Transfer-Encoding

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK_EQ(std::get<HttpResponse>(result).transferMode, TransferMode::UntilClose);
    co_return;
}

NITRO_TEST(http_parser_response_connection_close)
{
    HttpParser<HttpResponse> parser;

    parser.feedLine("HTTP/1.1 200 OK");
    parser.feedLine("Connection: close");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK(std::get<HttpResponse>(result).shouldClose);
    co_return;
}

NITRO_TEST(http_parser_response_http10_default_close)
{
    HttpParser<HttpResponse> parser;

    parser.feedLine("HTTP/1.0 200 OK");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK(std::get<HttpResponse>(result).shouldClose); // HTTP/1.0 default
    co_return;
}

NITRO_TEST(http_parser_response_status_code)
{
    // normal: with reason phrase
    {
        HttpParser<HttpResponse> parser;
        parser.feedLine("HTTP/1.1 200 OK");
        parser.feedLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
        NITRO_CHECK_EQ(std::get<HttpResponse>(result).statusCode, StatusCode::k200OK);
        NITRO_CHECK_EQ(std::get<HttpResponse>(result).statusReason, "OK");
    }
    // normal: without reason phrase
    {
        HttpParser<HttpResponse> parser;
        parser.feedLine("HTTP/1.1 204 ");
        parser.feedLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
        NITRO_CHECK_EQ(std::get<HttpResponse>(result).statusCode, StatusCode::k204NoContent);
        NITRO_CHECK(std::get<HttpResponse>(result).statusReason.empty());
    }
    // normal: without reason phrase and trailing space
    {
        HttpParser<HttpResponse> parser;
        parser.feedLine("HTTP/1.1 204");
        parser.feedLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
        NITRO_CHECK_EQ(std::get<HttpResponse>(result).statusCode, StatusCode::k204NoContent);
        NITRO_CHECK(std::get<HttpResponse>(result).statusReason.empty());
    }
    // non-standard code
    {
        HttpParser<HttpResponse> parser;
        parser.feedLine("HTTP/1.1 999 Custom");
        parser.feedLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
        NITRO_CHECK_EQ(std::get<HttpResponse>(result).statusCode, 999);
    }
    // invalid version
    {
        HttpParser<HttpResponse> parser;
        auto state = parser.feedLine("HTTP/2.0 200 OK");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK(std::holds_alternative<HttpParseError>(parser.extractResult()));
    }
    // missing first space
    {
        HttpParser<HttpResponse> parser;
        auto state = parser.feedLine("HTTP/1.1");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK(std::holds_alternative<HttpParseError>(parser.extractResult()));
    }
    // non-numeric status code
    {
        HttpParser<HttpResponse> parser;
        auto state = parser.feedLine("HTTP/1.1 abc OK");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(std::get<HttpParseError>(parser.extractResult()).code, HttpParseError::MalformedRequestLine);
    }
    // status code too short (2 digits)
    {
        HttpParser<HttpResponse> parser;
        auto state = parser.feedLine("HTTP/1.1 99 OK");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(std::get<HttpParseError>(parser.extractResult()).code, HttpParseError::MalformedRequestLine);
    }
    // status code too long (4 digits) but in range
    {
        HttpParser<HttpResponse> parser;
        auto state = parser.feedLine("HTTP/1.1 1000 OK");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(std::get<HttpParseError>(parser.extractResult()).code, HttpParseError::MalformedRequestLine);
    }
    co_return;
}

NITRO_TEST(http_parser_empty_header_value)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("GET /hello HTTP/1.1");
    parser.feedLine("Host: example.com");
    parser.feedLine("X-Empty:"); // Empty header value
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK(std::get<HttpRequest>(result).headers.contains("x-empty"));
    NITRO_CHECK(std::get<HttpRequest>(result).headers.at("x-empty").value().empty());
    co_return;
}

NITRO_TEST(http_parser_header_with_spaces)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("GET /hello HTTP/1.1");
    parser.feedLine("Host: example.com");
    parser.feedLine("  X-Spaced  :  value with spaces  ");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK(std::get<HttpRequest>(result).headers.contains("x-spaced"));
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).headers.at("x-spaced").value(), "value with spaces");
    co_return;
}

NITRO_TEST(http_parser_identity_encoding)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("POST /data HTTP/1.1");
    parser.feedLine("Host: example.com");
    parser.feedLine("Transfer-Encoding: identity");
    parser.feedLine("Content-Length: 10");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).transferMode, TransferMode::ContentLength);
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).contentLength, 10);
    co_return;
}
NITRO_TEST(http_parser_request_duplicate_content_length_same)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("POST /data HTTP/1.1");
    parser.feedLine("Host: example.com");
    parser.feedLine("Content-Length: 42");
    parser.feedLine("Content-Length: 42");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).contentLength, 42);
    co_return;
}

NITRO_TEST(http_parser_request_duplicate_content_length_conflict)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("POST /data HTTP/1.1");
    parser.feedLine("Host: example.com");
    parser.feedLine("Content-Length: 42");
    parser.feedLine("Content-Length: 99");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK_EQ(std::get<HttpParseError>(result).code, HttpParseError::AmbiguousContentLength);
    co_return;
}

NITRO_TEST(http_parser_request_cookie)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("GET /hello HTTP/1.1");
    parser.feedLine("Host: example.com");
    parser.feedLine("Cookie: session=abc123; user=john");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).cookies.at("session"), "abc123");
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).cookies.at("user"), "john");
    HttpRequestAccessor accessor(std::get<HttpRequest>(std::move(result)));
    NITRO_CHECK_EQ(accessor.getCookie("session"), "abc123");
    NITRO_CHECK_EQ(accessor.getCookie("user"), "john");
    NITRO_CHECK(accessor.getCookie("missing").empty());
    NITRO_CHECK_EQ(accessor.cookies().size(), 2);
    co_return;
}

NITRO_TEST(http_parser_response_set_cookie)
{
    HttpParser<HttpResponse> parser;

    parser.feedLine("HTTP/1.1 200 OK");
    parser.feedLine("Set-Cookie: session=abc123; Path=/; HttpOnly");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK_EQ(std::get<HttpResponse>(result).cookies.size(), 1);
    NITRO_CHECK_EQ(std::get<HttpResponse>(result).cookies[0].name, "session");
    NITRO_CHECK_EQ(std::get<HttpResponse>(result).cookies[0].value, "abc123");
    HttpResponseAccessor accessor(std::get<HttpResponse>(std::move(result)));
    NITRO_CHECK_EQ(accessor.cookies().size(), 1);
    NITRO_CHECK_EQ(accessor.cookies()[0].name, "session");
    co_return;
}

NITRO_TEST(http_parser_request_encoded_path)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("GET /user/john%20doe HTTP/1.1");
    parser.feedLine("Host: example.com");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).rawPath, "/user/john%20doe");
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).path, "/user/john doe");
    co_return;
}

NITRO_TEST(http_parser_request_keep_alive_http10_explicit)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("GET /hello HTTP/1.0");
    parser.feedLine("Host: example.com");
    parser.feedLine("Connection: keep-alive");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK(std::get<HttpRequest>(result).keepAlive);
    co_return;
}

NITRO_TEST(http_parser_response_keep_alive_http10_explicit)
{
    HttpParser<HttpResponse> parser;

    parser.feedLine("HTTP/1.0 200 OK");
    parser.feedLine("Connection: keep-alive");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK(!std::get<HttpResponse>(result).shouldClose);
    co_return;
}

NITRO_TEST(http_parser_transfer_encoding_overrides_content_length)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("POST /data HTTP/1.1");
    parser.feedLine("Host: example.com");
    parser.feedLine("Content-Length: 100");
    parser.feedLine("Transfer-Encoding: chunked");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
    NITRO_CHECK_EQ(std::get<HttpRequest>(result).transferMode, TransferMode::Chunked);
    co_return;
}

NITRO_TEST(http_parser_invalid_header_no_colon)
{
    HttpParser<HttpRequest> parser;

    parser.feedLine("GET /hello HTTP/1.1");
    parser.feedLine("Host: example.com");
    parser.feedLine("InvalidHeaderWithoutColon");
    parser.feedLine("");

    auto result = parser.extractResult();
    NITRO_CHECK(!std::holds_alternative<HttpParseError>(result)); // silently ignored
    NITRO_CHECK(!std::get<HttpRequest>(result).headers.contains(HttpHeader::toLower("InvalidHeaderWithoutColon")));
    co_return;
}

NITRO_TEST(http_parser_request_invalid_content_length)
{
    // non-numeric
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("POST /data HTTP/1.1");
        parser.feedLine("Content-Length: abc");
        auto state = parser.feedLine("");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(std::get<HttpParseError>(parser.extractResult()).code, HttpParseError::AmbiguousContentLength);
    }
    // negative string
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("POST /data HTTP/1.1");
        parser.feedLine("Content-Length: -1");
        auto state = parser.feedLine("");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(std::get<HttpParseError>(parser.extractResult()).code, HttpParseError::AmbiguousContentLength);
    }
    co_return;
}

NITRO_TEST(http_parser_response_invalid_content_length)
{
    HttpParser<HttpResponse> parser;
    parser.feedLine("HTTP/1.1 200 OK");
    parser.feedLine("Content-Length: abc");
    auto state = parser.feedLine("");
    NITRO_CHECK_EQ(state, HttpParserState::Error);
    NITRO_CHECK_EQ(std::get<HttpParseError>(parser.extractResult()).code, HttpParseError::AmbiguousContentLength);
    co_return;
}

NITRO_TEST(http_parser_request_line)
{
    // missing first space
    {
        HttpParser<HttpRequest> parser;
        auto state = parser.feedLine("GET");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(std::get<HttpParseError>(parser.extractResult()).code, HttpParseError::MalformedRequestLine);
    }
    // missing second space (no version)
    {
        HttpParser<HttpRequest> parser;
        auto state = parser.feedLine("GET /hello");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(std::get<HttpParseError>(parser.extractResult()).code, HttpParseError::MalformedRequestLine);
    }
    // invalid method
    {
        HttpParser<HttpRequest> parser;
        auto state = parser.feedLine("INVALID /hello HTTP/1.1");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(std::get<HttpParseError>(parser.extractResult()).code, HttpParseError::MalformedRequestLine);
    }
    // invalid version
    {
        HttpParser<HttpRequest> parser;
        auto state = parser.feedLine("GET /hello HTTP/2.0");
        NITRO_CHECK_EQ(state, HttpParserState::Error);
        NITRO_CHECK_EQ(std::get<HttpParseError>(parser.extractResult()).code, HttpParseError::MalformedRequestLine);
    }
    // empty path — tolerated, normalized to /
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("GET  HTTP/1.1");
        parser.feedLine("Host: example.com");
        parser.feedLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
        NITRO_CHECK_EQ(std::get<HttpRequest>(result).path, "/");
        NITRO_CHECK(std::get<HttpRequest>(result).rawPath.empty());
    }
    co_return;
}

NITRO_TEST(http_parser_invalid_header_token)
{
    // header name with space — silently ignored
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("GET /hello HTTP/1.1");
        parser.feedLine("Host: example.com");
        parser.feedLine("Bad Name: value");
        parser.feedLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
        NITRO_CHECK(!std::get<HttpRequest>(result).headers.contains("bad name"));
    }
    // header name with control character — silently ignored
    {
        HttpParser<HttpRequest> parser;
        parser.feedLine("GET /hello HTTP/1.1");
        parser.feedLine("Host: example.com");
        parser.feedLine("Bad\x01Name: value");
        parser.feedLine("");
        auto result = parser.extractResult();
        NITRO_CHECK(!std::holds_alternative<HttpParseError>(result));
        NITRO_CHECK(!std::get<HttpRequest>(result).headers.contains("bad\x01name"));
    }
    co_return;
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
