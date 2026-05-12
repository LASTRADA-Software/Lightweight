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

    /// Non-throwing variant: returns `nullptr` when the symbol is absent, throws only
    /// when the underlying OS API surfaces a different (non-not-found) error. Use this
    /// for optional plugin extension points (e.g. post-init hooks) where every plugin
    /// is allowed to omit the symbol.
    [[nodiscard]] GenericFunctionPointer TryGetSymbol(std::string const& symbolName) const noexcept;

    template <typename FunctionSignature>
    [[nodiscard]] FunctionSignature* GetFunction(std::string const& symbolName) const
    {
        return reinterpret_cast<FunctionSignature*>(GetSymbol(symbolName));
    }

    template <typename FunctionSignature>
    [[nodiscard]] FunctionSignature* TryGetFunction(std::string const& symbolName) const noexcept
    {
        return reinterpret_cast<FunctionSignature*>(TryGetSymbol(symbolName));
    }

    [[nodiscard]] void* GetNativeHandle() const
    {
        return _handle;
    }

  private:
    void* _handle { nullptr };
};

} // namespace Lightweight::Tools
