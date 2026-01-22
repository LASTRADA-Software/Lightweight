// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>

namespace Lightweight::Tools
{

/// @brief Loads a plugin from a shared library.
/// @note This is a private helper class for dbtool.
class PluginLoader
{
  public:
    using GenericFunctionPointer = void (*)();

    explicit PluginLoader(std::filesystem::path const& libraryPath);
    ~PluginLoader();

    PluginLoader(PluginLoader&& /*other*/) noexcept;
    PluginLoader& operator=(PluginLoader&& /*other*/) noexcept;

    PluginLoader(PluginLoader const& /*other*/) = delete;
    PluginLoader& operator=(PluginLoader const& /*other*/) = delete;

    [[nodiscard]] GenericFunctionPointer GetSymbol(std::string const& symbolName) const;

    template <typename FunctionSignature>
    [[nodiscard]] FunctionSignature* GetFunction(std::string const& symbolName) const
    {
        return reinterpret_cast<FunctionSignature*>(GetSymbol(symbolName));
    }

    [[nodiscard]] void* GetNativeHandle() const
    {
        return _handle;
    }

  private:
    void* _handle { nullptr };
};

} // namespace Lightweight::Tools
