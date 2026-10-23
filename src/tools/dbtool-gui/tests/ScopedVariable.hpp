// SPDX-License-Identifier: Apache-2.0
//
// Sets an environment variable for the dbtool-gui tests and restores it afterwards.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include <QtCore/QByteArray>
#include <QtCore/QFile>
#include <QtCore/QString>
#include <QtCore/QtEnvironmentVariables>

namespace DbtoolGui::Tests
{

/// Sets an environment variable for its lifetime, then restores its previous value, or its absence.
class ScopedVariable
{
  public:
    /// @param name The variable.
    /// @param value Its value; a path is encoded as the file system expects, anything else as is.
    ScopedVariable(std::string name, std::filesystem::path const& value):
        _name { std::move(name) },
        _previous { qEnvironmentVariableIsSet(_name.c_str()) ? std::optional { qgetenv(_name.c_str()) } : std::nullopt }
    {
        qputenv(_name.c_str(), QFile::encodeName(QString::fromStdU16String(value.u16string())));
    }

    ScopedVariable(ScopedVariable const&) = delete;
    ScopedVariable(ScopedVariable&&) = delete;
    ScopedVariable& operator=(ScopedVariable const&) = delete;
    ScopedVariable& operator=(ScopedVariable&&) = delete;

    ~ScopedVariable()
    {
        if (_previous.has_value())
            qputenv(_name.c_str(), *_previous);
        else
            qunsetenv(_name.c_str());
    }

  private:
    std::string _name;
    std::optional<QByteArray> _previous;
};

} // namespace DbtoolGui::Tests
