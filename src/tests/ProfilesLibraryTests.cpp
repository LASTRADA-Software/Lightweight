// SPDX-License-Identifier: Apache-2.0
//
// Tests for the Lightweight::Profiles shared library.

#include <Lightweight/Profiles/ProfileConnection.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include <Secrets/ProfileCipher.hpp>

namespace Profiles = Lightweight::Profiles;
namespace Secrets = Lightweight::Secrets;

using namespace std::string_view_literals;

TEST_CASE("EncryptPassword and DecryptPassword round-trip", "[profiles-lib]")
{
    auto const encrypted = Profiles::EncryptPassword("s3cr3t-\xC3\x84");
    REQUIRE(encrypted.has_value());
    REQUIRE(Profiles::IsEncryptedPassword(*encrypted));
    CHECK(encrypted->find("s3cr3t") == std::string::npos);

    auto const decrypted = Profiles::DecryptPassword(*encrypted);
    REQUIRE(decrypted.has_value());
    CHECK(*decrypted == "s3cr3t-\xC3\x84");
}

TEST_CASE("DecryptPassword of a plaintext or garbled value is an error without the input", "[profiles-lib]")
{
    for (auto const input: { "plain"sv, "enc:dev:@@@"sv })
    {
        auto const result = Profiles::DecryptPassword(input);
        REQUIRE(!result.has_value());
        CHECK(result.error().find("@@@") == std::string::npos);
        CHECK(result.error().find("plain") == std::string::npos);
    }
}

TEST_CASE("IsEncryptedPassword is false for values without the enc: prefix", "[profiles-lib]")
{
    CHECK_FALSE(Profiles::IsEncryptedPassword(""));
    CHECK_FALSE(Profiles::IsEncryptedPassword("enc"));
    CHECK_FALSE(Profiles::IsEncryptedPassword("hunter2"));
}

TEST_CASE("Library output matches ProfileCipher::Builtin()", "[profiles-lib]")
{
    auto const encrypted = Profiles::EncryptPassword("x");
    REQUIRE(encrypted.has_value());

    auto const decrypted = Secrets::ProfileCipher::Builtin().Decrypt(*encrypted);
    REQUIRE(decrypted.has_value());
    CHECK(*decrypted == "x");
}
