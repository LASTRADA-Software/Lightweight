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

# Master key ring for profile-password encryption — see cmake/DbtoolKeyRing.cmake
# for the two ways CI can supply it (DBTOOL_MASTER_KEYS env var, or a key file
# via DBTOOL_MASTER_KEYS_FILE). The keys only land in a generated, never-installed
# header of this build tree; without them the public development key is used.
target_sources(tools_shared PRIVATE Secrets/KeyRing.cpp Secrets/KeyRing.hpp Secrets/ProfileCipher.cpp Secrets/ProfileCipher.hpp)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/DbtoolKeyRing.cmake")
set(DBTOOL_MASTER_KEYS_FILE "$ENV{DBTOOL_MASTER_KEYS_FILE}" CACHE FILEPATH
    "File holding the dbtool master key ring (id=<64 hex> per line); empty uses DBTOOL_MASTER_KEYS from the environment or the development key")
dbtool_resolve_key_ring("${DBTOOL_MASTER_KEYS_FILE}" "$ENV{DBTOOL_MASTER_KEYS}" DBTOOL_KEYRING_SPEC DBTOOL_KEYRING_IS_RELEASE _dbtool_keyring_error)
if(NOT _dbtool_keyring_error STREQUAL "")
    message(FATAL_ERROR "${_dbtool_keyring_error}")
endif()
if(NOT DBTOOL_MASTER_KEYS_FILE STREQUAL "")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DBTOOL_MASTER_KEYS_FILE}")
endif()
if(DBTOOL_KEYRING_IS_RELEASE)
    message(STATUS "dbtool: using the CI-provided master key ring")
else()
    message(STATUS "dbtool: no master key provided - using the public development key")
endif()
# The XOR mask is generated once per build tree so reconfiguring does not force a rebuild.
if(NOT DEFINED CACHE{DBTOOL_KEYRING_MASK})
    string(RANDOM LENGTH 64 ALPHABET "0123456789abcdef" _dbtool_mask)
    set(DBTOOL_KEYRING_MASK "${_dbtool_mask}" CACHE INTERNAL "XOR mask for the embedded dbtool key ring")
endif()
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
