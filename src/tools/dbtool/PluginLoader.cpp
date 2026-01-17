#include "PluginLoader.hpp"

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#include <format>
#include <stdexcept>

using namespace std::string_literals;

namespace Lightweight::Tools
{

PluginLoader::PluginLoader(std::filesystem::path const& libraryPath):
#if defined(_WIN32)
    _handle { LoadLibraryW(libraryPath.c_str()) }
#else
    _handle { dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL) }
#endif
{
    if (!_handle)
    {
#if defined(_WIN32)
        throw std::runtime_error(
            std::format("Failed to load library '{}': error code {}", libraryPath.string(), GetLastError()));
#else
        throw std::runtime_error(std::format("Failed to load library '{}': {}", libraryPath.string(), dlerror()));
#endif
    }
}

PluginLoader::~PluginLoader()
{
    if (!_handle)
        return;
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(_handle));
#else
    dlclose(_handle);
#endif
}

PluginLoader::PluginLoader(PluginLoader&& other) noexcept:
    _handle { other._handle }
{
    other._handle = nullptr;
}

PluginLoader& PluginLoader::operator=(PluginLoader&& other) noexcept
{
    if (this != &other)
    {
        if (_handle)
        {
#if defined(_WIN32)
            FreeLibrary(static_cast<HMODULE>(_handle));
#else
            dlclose(_handle);
#endif
        }
        _handle = other._handle;
        other._handle = nullptr;
    }
    return *this;
}

PluginLoader::GenericFunctionPointer PluginLoader::GetSymbol(std::string const& symbolName) const
{
#if defined(_WIN32)
    auto const symbol = GetProcAddress(static_cast<HMODULE>(_handle), symbolName.c_str());
    if (!symbol)
    {
        throw std::runtime_error(std::format("Failed to load symbol '{}': error code {}", symbolName, GetLastError()));
    }
    return reinterpret_cast<GenericFunctionPointer>(symbol);
#else
    auto* symbol = dlsym(_handle, symbolName.c_str());
    if (!symbol)
    {
        throw std::runtime_error(std::format("Failed to load symbol '{}': {}", symbolName, dlerror()));
    }
    return reinterpret_cast<GenericFunctionPointer>(symbol);
#endif
}

} // namespace Lightweight::Tools
