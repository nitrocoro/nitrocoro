#include <nitrocoro/testing/Test.h>
#include <nitrocoro/utils/Base64.h>
#include <nitrocoro/utils/Md5.h>
#include <nitrocoro/utils/Sha1.h>
#include <nitrocoro/utils/UrlEncode.h>

using namespace nitrocoro::utils;

// ── Base64 ────────────────────────────────────────────────────────────────────

NITRO_TEST(base64_encode)
{
    NITRO_CHECK_EQ(base64Encode(""), "");
    NITRO_CHECK_EQ(base64Encode("f"), "Zg==");
    NITRO_CHECK_EQ(base64Encode("fo"), "Zm8=");
    NITRO_CHECK_EQ(base64Encode("foo"), "Zm9v");
    NITRO_CHECK_EQ(base64Encode("foob"), "Zm9vYg==");
    NITRO_CHECK_EQ(base64Encode("foobar"), "Zm9vYmFy");
    co_return;
}

NITRO_TEST(base64_decode)
{
    NITRO_CHECK_EQ(base64Decode(""), "");
    NITRO_CHECK_EQ(base64Decode("Zg=="), "f");
    NITRO_CHECK_EQ(base64Decode("Zm8="), "fo");
    NITRO_CHECK_EQ(base64Decode("Zm9v"), "foo");
    NITRO_CHECK_EQ(base64Decode("Zm9vYg=="), "foob");
    NITRO_CHECK_EQ(base64Decode("Zm9vYmFy"), "foobar");
    co_return;
}

NITRO_TEST(base64_roundtrip)
{
    std::string input = "Hello, NitroCoro! \x01\x02\xff";
    NITRO_CHECK_EQ(base64Decode(base64Encode(input)), input);
    co_return;
}

NITRO_TEST(base64_decode_autopad)
{
    NITRO_CHECK_EQ(base64Decode("Zg"), "f");
    NITRO_CHECK_EQ(base64Decode("Zm8"), "fo");
    co_return;
}

NITRO_TEST(base64_decode_invalid)
{
    NITRO_CHECK_THROWS_AS(base64Decode("Z"), std::invalid_argument);
    NITRO_CHECK_THROWS_AS(base64Decode("Z!=="), std::invalid_argument);
    co_return;
}

// ── UrlEncode ─────────────────────────────────────────────────────────────────

NITRO_TEST(url_encode_component)
{
    NITRO_CHECK_EQ(urlEncodeComponent("hello world"), "hello%20world");
    NITRO_CHECK_EQ(urlEncodeComponent("foo=bar&baz"), "foo%3Dbar%26baz");
    NITRO_CHECK_EQ(urlEncodeComponent("a+b"), "a%2Bb");
    NITRO_CHECK_EQ(urlEncodeComponent("abc-_.~"), "abc-_.~");
    NITRO_CHECK_EQ(urlEncodeComponent(""), "");
    co_return;
}

NITRO_TEST(url_decode_component)
{
    NITRO_CHECK_EQ(urlDecodeComponent("hello%20world"), "hello world");
    NITRO_CHECK_EQ(urlDecodeComponent("foo%3Dbar%26baz"), "foo=bar&baz");
    NITRO_CHECK_EQ(urlDecodeComponent("a%2Bb"), "a+b");
    NITRO_CHECK_EQ(urlDecodeComponent(""), "");
    co_return;
}

NITRO_TEST(form_encode)
{
    NITRO_CHECK_EQ(formEncode("hello world"), "hello+world");
    NITRO_CHECK_EQ(formEncode("foo=bar&baz"), "foo%3Dbar%26baz");
    NITRO_CHECK_EQ(formEncode("a+b"), "a%2Bb");
    NITRO_CHECK_EQ(formEncode("abc-_.~"), "abc-_.~");
    NITRO_CHECK_EQ(formEncode(""), "");
    co_return;
}

NITRO_TEST(form_decode)
{
    NITRO_CHECK_EQ(formDecode("hello+world"), "hello world");
    NITRO_CHECK_EQ(formDecode("foo%3Dbar%26baz"), "foo=bar&baz");
    NITRO_CHECK_EQ(formDecode("a%2Bb"), "a+b");
    NITRO_CHECK_EQ(formDecode(""), "");
    co_return;
}

NITRO_TEST(form_roundtrip)
{
    std::string input = "key=value with spaces & special=chars!@#";
    NITRO_CHECK_EQ(formDecode(formEncode(input)), input);
    co_return;
}

// ── SHA-1 ─────────────────────────────────────────────────────────────────────

NITRO_TEST(sha1_hex)
{
    // RFC 3174 test vectors
    NITRO_CHECK_EQ(sha1Hex("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
    NITRO_CHECK_EQ(sha1Hex(""), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    NITRO_CHECK_EQ(sha1Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
                   "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    co_return;
}

// ── MD5 ───────────────────────────────────────────────────────────────────────

NITRO_TEST(md5_hex)
{
    // RFC 1321 test vectors
    NITRO_CHECK_EQ(md5Hex(""), "d41d8cd98f00b204e9800998ecf8427e");
    NITRO_CHECK_EQ(md5Hex("a"), "0cc175b9c0f1b6a831c399e269772661");
    NITRO_CHECK_EQ(md5Hex("abc"), "900150983cd24fb0d6963f7d28e17f72");
    NITRO_CHECK_EQ(md5Hex("message digest"), "f96b697d7cb7938d525a2f31aaf161d0");
    co_return;
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
