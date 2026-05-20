# tools_shared — helpers shared between dbtool, dbtool-gui, and the test
# suite. NOT installed and NOT part of the public Lightweight API:
# the core SQL framework has no dependency on these, so they live alongside
# the tools that actually use them.
#
# Mirrors the pattern of dbtool_lib (in ../dbtool/CMakeLists.txt), which is
# also a tools-private STATIC library shared between dbtool and dbtool-gui.

add_library(tools_shared STATIC
    Config/ProfileStore.cpp
    Config/ProfileStore.hpp

    PluginDiscovery.cpp
    PluginDiscovery.hpp

    PluginIngestion.cpp
    PluginIngestion.hpp

    PluginLoader.cpp
    PluginLoader.hpp

    Odbc/DataSourceEnumerator.cpp
    Odbc/DataSourceEnumerator.hpp

    Secrets/ISecretBackend.hpp
    Secrets/SecretResolver.cpp
    Secrets/SecretResolver.hpp
    Secrets/backends/EnvBackend.cpp
    Secrets/backends/EnvBackend.hpp
    Secrets/backends/FileBackend.cpp
    Secrets/backends/FileBackend.hpp
    Secrets/backends/StdinBackend.cpp
    Secrets/backends/StdinBackend.hpp

    CodeGen/SplitFileWriter.cpp
    CodeGen/SplitFileWriter.hpp
)

target_compile_features(tools_shared PUBLIC cxx_std_23)

# Consumers `#include "Config/ProfileStore.hpp"`, etc. — the `Lightweight/`
# prefix used by the public library is intentionally not part of this
# include path.
target_include_directories(tools_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# PUBLIC: ProfileStore returns SqlConnectInfo; DataSourceEnumerator uses ODBC
# headers that come from the Lightweight target.
target_link_libraries(tools_shared PUBLIC Lightweight::Lightweight)

# yaml-cpp is used only by Config/ProfileStore.cpp. Linking PRIVATE keeps it
# off downstream consumers and out of the installed Lightweight package.
target_link_libraries(tools_shared PRIVATE yaml-cpp::yaml-cpp)

# `dlopen` / `dlsym` for `PluginLoader.cpp` on POSIX. Windows links
# against `kernel32.lib` for `LoadLibraryW` / `GetProcAddress`, which is
# implicit in the MSVC toolchain — no entry needed.
if(UNIX)
    target_link_libraries(tools_shared PRIVATE ${CMAKE_DL_LIBS})
endif()

if(NOT WIN32)
    set_target_properties(tools_shared PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(CLANG_TIDY_EXE)
    set_target_properties(tools_shared PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

enable_coverage_for_target(tools_shared)
