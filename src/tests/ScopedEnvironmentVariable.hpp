// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdlib>
#include <optional>
#include <string>

/// Sets an environment variable for the lifetime of the object and then restores its previous
/// value, or unsets it when it had none.
class ScopedEnvironmentVariable
{
  public:
    ScopedEnvironmentVariable(char const* name, std::string const& value):
        _name { name },
        _previousValue { Read(name) }
    {
        Write(name, value);
    }

    ScopedEnvironmentVariable(ScopedEnvironmentVariable const&) = delete;
    ScopedEnvironmentVariable(ScopedEnvironmentVariable&&) = delete;
    ScopedEnvironmentVariable& operator=(ScopedEnvironmentVariable const&) = delete;
    ScopedEnvironmentVariable& operator=(ScopedEnvironmentVariable&&) = delete;

    ~ScopedEnvironmentVariable()
    {
        Write(_name.c_str(), _previousValue.value_or(std::string {}));
    }

  private:
    static std::optional<std::string> Read(char const* name)
    {
#ifdef _WIN32
        auto size = std::size_t {};
        if (getenv_s(&size, nullptr, 0, name) != 0 || size == 0)
            return std::nullopt;
        auto value = std::string(size, '\0');
        if (getenv_s(&size, value.data(), value.size(), name) != 0)
            return std::nullopt;
        value.resize(size - 1); // size counts the terminating null
        return value;
#else
        if (auto const* const value = std::getenv(name); value != nullptr)
            return std::string { value };
        return std::nullopt;
#endif
    }

    /// Sets `name` to `value`; an empty value unsets it.
    static void Write(char const* name, std::string const& value)
    {
#ifdef _WIN32
        _putenv_s(name, value.c_str());
#else
        if (value.empty())
            unsetenv(name);
        else
            setenv(name, value.c_str(), 1);
#endif
    }

    std::string _name;
    std::optional<std::string> _previousValue;
};
