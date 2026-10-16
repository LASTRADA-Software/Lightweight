// SPDX-License-Identifier: Apache-2.0
//
// Manual triggers for `Lightweight::Tools::DisableErrorDialogs`, installed by the test `main`.
// Hidden: each one deliberately ends the process, so run one at a time by name, e.g.
// `LightweightTest "error dialogs: a failed CRT assert goes to stderr"`. Each must report on
// stderr and exit non-zero without a dialog.

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
    #include <crtdbg.h>
    #include <io.h>
#endif

TEST_CASE("error dialogs: a failed CRT assert goes to stderr", "[.][NoErrorDialogs]")
{
#if defined(_WIN32) && defined(_DEBUG)
    _ASSERTE(false && "deliberate assert from the NoErrorDialogs trigger");
#else
    std::fputs("deliberate abort from the NoErrorDialogs trigger (no CRT debug asserts here)\n", stderr);
    std::abort();
#endif
}

TEST_CASE("error dialogs: an invalid CRT parameter goes to stderr", "[.][NoErrorDialogs]")
{
#if defined(_WIN32)
    // The Debug CRT asserts first, so only a Release build reaches the invalid-parameter handler.
    static_cast<void>(_close(-1));
#else
    std::abort(); // only the Windows CRT validates parameters through a handler
#endif
}

TEST_CASE("error dialogs: abort goes to stderr", "[.][NoErrorDialogs]")
{
    std::abort();
}
