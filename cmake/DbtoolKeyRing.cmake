# SPDX-License-Identifier: Apache-2.0
#
# Resolves the master key ring used to encrypt dbtool.yml profile passwords.
#
# Official builds supply the ring in exactly one of two ways:
#
#   * DBTOOL_MASTER_KEYS (environment variable) — "id=<64 hex>[,id=<64 hex>...]".
#     Natural fit for GitHub Actions secrets.
#   * DBTOOL_MASTER_KEYS_FILE (CMake variable, defaulting to the environment
#     variable of the same name) — path to a file holding the same entries,
#     comma- or newline-separated. Natural fit for GitLab "File" variables and
#     for package ports (vcpkg/Conan), which strip the environment but can pass
#     -DDBTOOL_MASTER_KEYS_FILE=... through their CMake options.
#
# The newest key comes first: it encrypts, every key decrypts (rotation). Only
# the file *path* ever reaches CMakeCache.txt — the keys themselves never do.
# Without either input the public development key is used.

set(DBTOOL_DEV_KEY_HEX "ba456ca44382fdc8bfd9c84ac989779fa0da3754722e2691f1e0d7a735a3b232")

# Validates a comma-separated key ring spec. Sets <outError> to "" when valid.
# Messages never include key material.
function(dbtool_validate_key_ring spec outError)
    string(REPEAT "[0-9a-fA-F]" 64 hex64)
    string(REPLACE "," ";" entries "${spec}")
    set(seenIds "")
    foreach(entry IN LISTS entries)
        if(NOT entry MATCHES "^([a-z0-9]+)=${hex64}$")
            set(${outError} "the dbtool master key ring is malformed; expected id=<64 hex digits>[,id=<64 hex digits>...] with ids matching [a-z0-9]+" PARENT_SCOPE)
            return()
        endif()
        set(id "${CMAKE_MATCH_1}")
        string(LENGTH "${id}" idLength)
        if(idLength GREATER 16)
            set(${outError} "dbtool master key ids must be at most 16 characters" PARENT_SCOPE)
            return()
        endif()
        if(id STREQUAL "dev")
            set(${outError} "the dbtool master key id 'dev' is reserved for the public development key" PARENT_SCOPE)
            return()
        endif()
        if(id IN_LIST seenIds)
            set(${outError} "duplicate dbtool master key id '${id}'" PARENT_SCOPE)
            return()
        endif()
        list(APPEND seenIds "${id}")
    endforeach()
    set(${outError} "" PARENT_SCOPE)
endfunction()

# Resolves the key ring from a key file path and/or inline env value.
#   keyFile     - path from DBTOOL_MASTER_KEYS_FILE, or ""
#   envKeys     - value of $ENV{DBTOOL_MASTER_KEYS}, or ""
#   outSpec     - normalised "id=hex[,id=hex...]"
#   outRelease  - "true" for a CI-provided ring, "false" for the dev key
#   outError    - "" on success, otherwise a message free of key material
function(dbtool_resolve_key_ring keyFile envKeys outSpec outRelease outError)
    set(${outSpec} "" PARENT_SCOPE)
    set(${outRelease} "" PARENT_SCOPE)

    if(NOT "${keyFile}" STREQUAL "" AND NOT "${envKeys}" STREQUAL "")
        set(${outError} "both DBTOOL_MASTER_KEYS and DBTOOL_MASTER_KEYS_FILE are set; provide the dbtool master key ring only once" PARENT_SCOPE)
        return()
    endif()

    if(NOT "${keyFile}" STREQUAL "")
        if(NOT EXISTS "${keyFile}" OR IS_DIRECTORY "${keyFile}")
            set(${outError} "DBTOOL_MASTER_KEYS_FILE not found: ${keyFile}" PARENT_SCOPE)
            return()
        endif()
        file(READ "${keyFile}" raw)
        string(REGEX REPLACE "[ \t\r]" "" raw "${raw}")
        string(REGEX REPLACE "\n+" "," raw "${raw}")
        string(REGEX REPLACE "^,+|,+$" "" spec "${raw}")
        if(spec STREQUAL "")
            set(${outError} "DBTOOL_MASTER_KEYS_FILE is empty: ${keyFile}" PARENT_SCOPE)
            return()
        endif()
    elseif(NOT "${envKeys}" STREQUAL "")
        string(STRIP "${envKeys}" spec)
    else()
        set(${outSpec} "dev=${DBTOOL_DEV_KEY_HEX}" PARENT_SCOPE)
        set(${outRelease} "false" PARENT_SCOPE)
        set(${outError} "" PARENT_SCOPE)
        return()
    endif()

    dbtool_validate_key_ring("${spec}" validationError)
    if(NOT validationError STREQUAL "")
        set(${outError} "${validationError}" PARENT_SCOPE)
        return()
    endif()

    set(${outSpec} "${spec}" PARENT_SCOPE)
    set(${outRelease} "true" PARENT_SCOPE)
    set(${outError} "" PARENT_SCOPE)
endfunction()

# Guards a build tree that was once configured with a CI key ring against a
# later configure — e.g. an automatic re-run during `cmake --build` in a shell
# without DBTOOL_MASTER_KEYS — silently regenerating it with the public
# development key.
#   wasRelease  - cached flag from earlier configures ("" or TRUE)
#   isRelease   - "true"/"false" from dbtool_resolve_key_ring
#   outError    - "" when fine, otherwise the reason to stop
function(dbtool_check_key_ring_downgrade wasRelease isRelease outError)
    if(wasRelease AND NOT isRelease)
        set(${outError} "this build tree was configured with the CI master key ring, but neither DBTOOL_MASTER_KEYS nor DBTOOL_MASTER_KEYS_FILE is set now; refusing to fall back to the public development key. Provide the key ring again, or use a fresh build directory (or -U DBTOOL_KEYRING_WAS_RELEASE) for a development build" PARENT_SCOPE)
    else()
        set(${outError} "" PARENT_SCOPE)
    endif()
endfunction()
