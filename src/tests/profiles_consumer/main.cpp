// SPDX-License-Identifier: Apache-2.0
//
// Consumer of the installed Lightweight::Profiles package; includes nothing but its public header.

#include <Lightweight/Profiles/ProfileConnection.hpp>

#include <iostream>
#include <optional>

namespace Profiles = Lightweight::Profiles;

int main()
{
    constexpr auto Secret = "consumer-test-secret";

    auto const encrypted = Profiles::EncryptPassword(Secret);
    if (!encrypted || !Profiles::IsEncryptedPassword(*encrypted))
    {
        std::cerr << "EncryptPassword failed\n";
        return 1;
    }

    auto const decrypted = Profiles::DecryptPassword(*encrypted);
    if (!decrypted || *decrypted != Secret)
    {
        std::cerr << "DecryptPassword did not round-trip\n";
        return 1;
    }

    auto const list = Profiles::ListProfiles(std::nullopt);
    if (!list)
    {
        std::cerr << "ListProfiles failed: " << list.error() << '\n';
        return 1;
    }

    std::cout << "profiles: " << list->profiles.size() << '\n';
    return 0;
}
