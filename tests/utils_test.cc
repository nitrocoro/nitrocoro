#include <nitrocoro/testing/Test.h>
#include <nitrocoro/utils/Base64.h>
#include <nitrocoro/utils/Format.h>
#include <nitrocoro/utils/Md5.h>
#include <nitrocoro/utils/Sha1.h>

using namespace nitrocoro::utils;

// ── Format ───────────────────────────────────────────────────────────────────

NITRO_TEST(format_no_args)
{
    NITRO_CHECK_EQ(format("hello"), "hello");
    NITRO_CHECK_EQ(format(""), "");
    co_return;
}

NITRO_TEST(format_string_types)
{
    std::string s = "world";
    std::string_view sv = "view";
    NITRO_CHECK_EQ(format("hello {}", s), "hello world");
    NITRO_CHECK_EQ(format("hello {}", sv), "hello view");
    NITRO_CHECK_EQ(format("hello {}", "literal"), "hello literal");
    NITRO_CHECK_EQ(format("hello {}", (const char *)nullptr), "hello (null)");
    co_return;
}

NITRO_TEST(format_integral_types)
{
    NITRO_CHECK_EQ(format("{}", 42), "42");
    NITRO_CHECK_EQ(format("{}", -1), "-1");
    NITRO_CHECK_EQ(format("{}", 0u), "0");
    NITRO_CHECK_EQ(format("{}", true), "true");
    NITRO_CHECK_EQ(format("{}", false), "false");
    NITRO_CHECK_EQ(format("{}", 'A'), "A");
    co_return;
}

NITRO_TEST(format_floating_point)
{
    NITRO_CHECK_EQ(format("{}", 3.14), "3.14");
    NITRO_CHECK_EQ(format("{}", 0.0), "0");
    co_return;
}

NITRO_TEST(format_multiple_args)
{
    NITRO_CHECK_EQ(format("{} + {} = {}", 1, 2, 3), "1 + 2 = 3");
    NITRO_CHECK_EQ(format("Hello, {}! You are {} years old.", "Alice", 30),
                   "Hello, Alice! You are 30 years old.");
    co_return;
}

NITRO_TEST(format_adjacent_placeholders)
{
    NITRO_CHECK_EQ(format("{}{}{}", "a", "b", "c"), "abc");
    co_return;
}

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
