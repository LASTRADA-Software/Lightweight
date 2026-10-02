# tools_shared — helpers shared between dbtool, dbtool-gui, and the test
# suite. NOT installed and NOT part of the public Lightweight API:
# the core SQL framework has no dependency on these, so they live alongside
# the tools that actually use them.
#
# Mirrors the pattern of dbtool_lib (in ../dbtool/CMakeLists.txt), which is
# also a tools-private STATIC library shared between dbtool and dbtool-gui.

add_library(tools_shared STATIC
    Config/ConfigDiscovery.cpp
    Config/ConfigDiscovery.hpp
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

# Profile-password encryption uses only the OS's own cryptography (see
# Secrets/Crypto/CryptoPrimitives.hpp): exactly one backend per platform.
target_sources(tools_shared PRIVATE Secrets/Crypto/CryptoPrimitives.hpp)
if(WIN32)
    target_sources(tools_shared PRIVATE Secrets/Crypto/CryptoPrimitives_win.cpp)
    target_link_libraries(tools_shared PRIVATE bcrypt)
elseif(APPLE)
    target_sources(tools_shared PRIVATE Secrets/Crypto/CryptoPrimitives_apple.cpp)
    target_link_libraries(tools_shared PRIVATE "-framework Security")
else()
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    target_sources(tools_shared PRIVATE Secrets/Crypto/CryptoPrimitives_openssl.cpp)
    target_link_libraries(tools_shared PRIVATE OpenSSL::Crypto)
endif()

# Master key ring for profile-password encryption. Official builds get it from
# the DBTOOL_MASTER_KEYS environment variable (a CI secret: "id=<64 hex>[,...]",
# newest first). It is read from the environment on purpose — never cached in
# CMakeCache.txt — and only lands in a generated, never-installed header of this
# build tree. Without it the build uses the public development key.
target_sources(tools_shared PRIVATE Secrets/KeyRing.cpp Secrets/KeyRing.hpp Secrets/ProfileCipher.cpp Secrets/ProfileCipher.hpp)
set(DBTOOL_DEV_KEY_HEX "ba456ca44382fdc8bfd9c84ac989779fa0da3754722e2691f1e0d7a735a3b232")
if(NOT "$ENV{DBTOOL_MASTER_KEYS}" STREQUAL "")
    set(DBTOOL_KEYRING_SPEC "$ENV{DBTOOL_MASTER_KEYS}")
    set(DBTOOL_KEYRING_IS_RELEASE true)
    string(REPEAT "[0-9a-fA-F]" 64 _dbtool_hex64)
    string(REPLACE "," ";" _dbtool_key_entries "${DBTOOL_KEYRING_SPEC}")
    foreach(_dbtool_entry IN LISTS _dbtool_key_entries)
        # Deliberately does not echo the entry: it contains key material.
        if(NOT _dbtool_entry MATCHES "^[a-z0-9]+=${_dbtool_hex64}$")
            message(FATAL_ERROR "DBTOOL_MASTER_KEYS is malformed; expected id=<64 hex digits>[,id=<64 hex digits>...]")
        endif()
        string(FIND "${_dbtool_entry}" "=" _dbtool_equals)
        if(_dbtool_equals GREATER 16)
            message(FATAL_ERROR "DBTOOL_MASTER_KEYS: key ids must be at most 16 characters")
        endif()
    endforeach()
    message(STATUS "dbtool: using the master key ring from DBTOOL_MASTER_KEYS")
else()
    set(DBTOOL_KEYRING_SPEC "dev=${DBTOOL_DEV_KEY_HEX}")
    set(DBTOOL_KEYRING_IS_RELEASE false)
    message(STATUS "dbtool: no master key provided - using the public development key")
endif()
string(RANDOM LENGTH 64 ALPHABET "0123456789abcdef" DBTOOL_KEYRING_MASK)
configure_file(Secrets/DbtoolKeyRing.inc.in "${CMAKE_CURRENT_BINARY_DIR}/generated/DbtoolKeyRing.inc" @ONLY)
target_include_directories(tools_shared PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
unset(DBTOOL_KEYRING_SPEC)

if(NOT WIN32)
    set_target_properties(tools_shared PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(CLANG_TIDY_EXE)
    set_target_properties(tools_shared PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

enable_coverage_for_target(tools_shared)
