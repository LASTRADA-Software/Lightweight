// SPDX-License-Identifier: Apache-2.0

#include "StdinBackend.hpp"

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
    #include <conio.h>
    #include <io.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

namespace Lightweight::Secrets
{

namespace
{

    /// True when stdin (fd 0) is attached to a terminal. Used to decide whether
    /// to bother disabling echo and to print the "Password:" prompt.
    bool StdinIsTty() noexcept
    {
#ifdef _WIN32
        return _isatty(_fileno(stdin)) != 0;
#else
        return isatty(fileno(stdin)) != 0;
#endif
    }

#ifndef _WIN32
    /// POSIX: disable terminal echo for the duration of the read, then restore.
    class ScopedNoEcho
    {
      public:
        ScopedNoEcho() noexcept
        {
            if (tcgetattr(fileno(stdin), &_saved) != 0)
            {
                _active = false;
                return;
            }
            termios modified = _saved;
            modified.c_lflag &= ~static_cast<tcflag_t>(ECHO);
            _active = tcsetattr(fileno(stdin), TCSAFLUSH, &modified) == 0;
        }

        ~ScopedNoEcho()
        {
            if (_active)
                tcsetattr(fileno(stdin), TCSAFLUSH, &_saved);
        }

        ScopedNoEcho(ScopedNoEcho const&) = delete;
        ScopedNoEcho(ScopedNoEcho&&) = delete;
        ScopedNoEcho& operator=(ScopedNoEcho const&) = delete;
        ScopedNoEcho& operator=(ScopedNoEcho&&) = delete;

      private:
        termios _saved {};
        bool _active = false;
    };
#endif

#ifdef _WIN32
    /// Reads a password from a Windows console character-by-character with echo
    /// suppression via `_getch`.
    std::string ReadPasswordFromWindowsConsole()
    {
        std::string out;
        int ch = 0;
        while ((ch = _getch()) != '\r' && ch != '\n' && ch != EOF)
        {
            if (ch == '\b') // backspace
            {
                if (!out.empty())
                    out.pop_back();
                continue;
            }
            if (ch == 3) // ctrl-c
                throw std::runtime_error("stdin: password prompt cancelled");
            out.push_back(static_cast<char>(ch));
        }
        return out;
    }
#endif

} // namespace

std::optional<std::string> StdinBackend::Read(std::string_view key)
{
    bool const interactive = StdinIsTty();
    if (interactive)
        std::cerr << "Password for " << key << ": " << std::flush;

    std::string password;
#ifdef _WIN32
    if (interactive)
    {
        password = ReadPasswordFromWindowsConsole();
    }
    else
    {
        if (!std::getline(std::cin, password))
            return std::nullopt;
    }
#else
    if (interactive)
    {
        ScopedNoEcho guard;
        if (!std::getline(std::cin, password))
            return std::nullopt;
    }
    else
    {
        if (!std::getline(std::cin, password))
            return std::nullopt;
    }
#endif
    if (interactive)
        std::cerr << '\n';
    return password;
}

void StdinBackend::Write(std::string_view /*key*/, std::string_view /*value*/)
{
    throw std::runtime_error("stdin: secret backend is read-only");
}

void StdinBackend::Erase(std::string_view /*key*/)
{
    throw std::runtime_error("stdin: secret backend is read-only");
}

} // namespace Lightweight::Secrets
