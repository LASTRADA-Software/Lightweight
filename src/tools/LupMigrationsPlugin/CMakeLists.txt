# SPDX-License-Identifier: Apache-2.0

# LupMigrationsPlugin - Migration plugin containing LUP SQL migrations

# ============================================================================
# Configuration
# ============================================================================

# Path to directory containing LUP SQL migration files (init_m_*.sql, upd_m_*.sql)
# If set, lup2dbtool will be run during build to generate C++ migrations
# If empty, the plugin will be built with an empty placeholder
set(LUPMIGRATION_SQL_DIR "" CACHE PATH "Directory containing LUP SQL migration files")

# Input encoding for SQL files: auto (detect per file), windows-1252, or utf-8.
# In auto mode every file is classified individually. The explicit modes require all
# files to actually be in that encoding; mismatches abort the build with a clear error
# instead of silently double-encoding the contents.
set(LUPMIGRATION_INPUT_ENCODING "auto" CACHE STRING "Input encoding for LUP SQL files")
set_property(CACHE LUPMIGRATION_INPUT_ENCODING PROPERTY STRINGS "auto" "windows-1252" "utf-8")

# ============================================================================
# Static library for TransitionGlue
# ============================================================================

add_library(LupTransitionGlue STATIC
    TransitionGlue.cpp
)
target_include_directories(LupTransitionGlue PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(LupTransitionGlue PUBLIC Lightweight::Lightweight)
# Compile with -fPIC so the archive can be linked into the shared module
# `LupMigrationsPlugin.so` (and used directly by the test binary).
set_target_properties(LupTransitionGlue PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ============================================================================
# Generated migrations
# ============================================================================

set(GENERATED_MIGRATIONS_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Create the generated directory
file(MAKE_DIRECTORY "${GENERATED_MIGRATIONS_DIR}")

if(LUPMIGRATION_SQL_DIR AND EXISTS "${LUPMIGRATION_SQL_DIR}")
    message(STATUS "LupMigrationsPlugin: Generating migrations from ${LUPMIGRATION_SQL_DIR}")

    # Collect all SQL files to establish dependencies
    file(GLOB LUPMIGRATION_SQL_FILES
        "${LUPMIGRATION_SQL_DIR}/init_m_*.sql"
        "${LUPMIGRATION_SQL_DIR}/upd_m_*.sql"
    )
    list(LENGTH LUPMIGRATION_SQL_FILES LUPMIGRATION_SQL_FILE_COUNT)
    message(STATUS "LupMigrationsPlugin: Found ${LUPMIGRATION_SQL_FILE_COUNT} SQL files")

    # Manifest file: lup2dbtool emits the list of every source file it generated,
    # one path per line. The configure step below reads it to populate
    # `GENERATED_MIGRATION_FILES` and declare the custom command's OUTPUT set, so
    # ninja owns each `lup_*.cpp` as a real build artifact (no configure-time glob,
    # no `CONFIGURE_DEPENDS` reglob, no mass invalidation when one SQL file changes).
    set(LUP_MANIFEST_FILE "${GENERATED_MIGRATIONS_DIR}/lup_manifest.txt")

    # Cold-start bootstrap: on the very first configure (or after a clean of the
    # generated directory) the `lup_*.cpp` files do not yet exist, so the manifest
    # has nothing to read and the plugin would link without any migrations —
    # exactly the regression that produced "nothing to migrate" against an empty DB.
    #
    # Stale-manifest refresh: a brand-new SQL file added to LUPMIGRATION_SQL_DIR
    # is picked up by the GLOB above on reconfigure, but the manifest still
    # reflects the previous build's outputs — so the plugin's source list misses
    # the new `lup_<ver>.cpp` (since lup2dbtool only runs at build time). The
    # build *does* regenerate the manifest, but by then the source list is
    # already baked from the stale read. Fix: when any SQL file is newer than
    # the manifest, regenerate it during this configure too.
    #
    # Build lup2dbtool synchronously, run it, and populate the directory *during*
    # configure. The build-time custom command below still owns incremental updates
    # when individual SQL files change.
    #
    # Two cold-start variants must work:
    #
    #   * **True cold-start** — the build directory is fresh; the parent configure
    #     has not finished yet, so neither `build.ninja` nor `Makefile` exists.
    #     `cmake --build ${CMAKE_BINARY_DIR}` would fail with
    #     `ninja: error: loading 'build.ninja'`. Instead we configure + build
    #     `lup2dbtool` in a scratch sub-build that has its own complete configure.
    #
    #   * **Warm cold-start** — a previous configure already produced build files
    #     but the user (or `git clean`) wiped `${GENERATED_MIGRATIONS_DIR}`. We
    #     reuse the parent build by invoking `cmake --build` against it.
    #
    # Re-entrancy guard: when ninja runs the build, it invokes
    # `cmake --regenerate-during-build` before each build. That re-runs configure,
    # which would re-enter this branch, recursively call `cmake --build`, and
    # deadlock. The sentinel file prevents the inner configure from trying to
    # bootstrap a second time.
    set(_lup_bootstrap_sentinel "${GENERATED_MIGRATIONS_DIR}/.bootstrap_in_progress")

    # Decide whether the manifest needs (re)generation during this configure.
    # Cold-start: manifest absent. Stale-refresh: manifest exists but at least
    # one SQL file has been modified more recently than the manifest itself.
    set(_lup_manifest_needs_refresh FALSE)
    if(NOT EXISTS "${LUP_MANIFEST_FILE}")
        set(_lup_manifest_needs_refresh TRUE)
    else()
        foreach(_lup_sql_file IN LISTS LUPMIGRATION_SQL_FILES)
            if("${_lup_sql_file}" IS_NEWER_THAN "${LUP_MANIFEST_FILE}")
                set(_lup_manifest_needs_refresh TRUE)
                message(STATUS "LupMigrationsPlugin: SQL file newer than manifest, refreshing during configure: ${_lup_sql_file}")
                break()
            endif()
        endforeach()
        unset(_lup_sql_file)
    endif()

    if(_lup_manifest_needs_refresh AND NOT EXISTS "${_lup_bootstrap_sentinel}")
        # Treat the parent build as ready only when its native build script is
        # actually present. Checking just `CMakeCache.txt` is not enough — the
        # cache is written early in configure, well before `build.ninja` /
        # `Makefile` is generated.
        set(_lup_parent_build_ready FALSE)
        foreach(_lup_build_marker IN ITEMS
                "${CMAKE_BINARY_DIR}/build.ninja"
                "${CMAKE_BINARY_DIR}/Makefile")
            if(EXISTS "${_lup_build_marker}")
                set(_lup_parent_build_ready TRUE)
                break()
            endif()
        endforeach()
        unset(_lup_build_marker)

        file(TOUCH "${_lup_bootstrap_sentinel}")

        if(_lup_parent_build_ready)
            message(STATUS "LupMigrationsPlugin: Cold-start (warm) — building lup2dbtool in parent build")
            execute_process(
                COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target lup2dbtool
                RESULT_VARIABLE _lup_bootstrap_build_result
            )
            if(NOT _lup_bootstrap_build_result EQUAL 0)
                file(REMOVE "${_lup_bootstrap_sentinel}")
                message(FATAL_ERROR "LupMigrationsPlugin: failed to build lup2dbtool for cold-start generation")
            endif()
            # `$<TARGET_FILE:lup2dbtool>` cannot be used here — generator expressions are
            # not evaluated by `execute_process`. Resolve the just-built binary by name
            # in the build tree. Multi-config generators (Visual Studio) drop the binary
            # under `<config>/`, single-config (Ninja, Makefiles) directly in the target dir.
            find_program(LUP2DBTOOL_BIN
                NAMES lup2dbtool
                PATHS
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}"
                    "${CMAKE_BINARY_DIR}/src/tools/lup2dbtool"
                    "${CMAKE_BINARY_DIR}/src/tools/lup2dbtool/${CMAKE_BUILD_TYPE}"
                NO_DEFAULT_PATH
                NO_CACHE
            )
            if(NOT LUP2DBTOOL_BIN)
                file(REMOVE "${_lup_bootstrap_sentinel}")
                message(FATAL_ERROR "LupMigrationsPlugin: lup2dbtool binary not found after build "
                                    "(searched ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} and "
                                    "${CMAKE_BINARY_DIR}/src/tools/lup2dbtool)")
            endif()
        else()
            # True cold-start: parent build is mid-configure, so we cannot drive it.
            # Spin up a self-contained scratch project that builds only lup2dbtool.
            # The scratch dir persists across configures so subsequent cold-starts
            # (after `rm -rf generated/`) are incremental and fast.
            set(_lup_scratch_dir "${CMAKE_BINARY_DIR}/_lup_bootstrap")
            message(STATUS "LupMigrationsPlugin: Cold-start (fresh) — building lup2dbtool in scratch dir ${_lup_scratch_dir}")

            # Variables that must travel to the scratch configure verbatim. Anything
            # the parent picked up from the toolchain / preset has to be re-asserted
            # because the scratch dir starts from an empty cache.
            set(_lup_scratch_args
                "-S" "${CMAKE_SOURCE_DIR}"
                "-B" "${_lup_scratch_dir}"
                "-G" "${CMAKE_GENERATOR}"
                # Disable everything except the tool we need so the scratch build is minimal.
                "-DLIGHTWEIGHT_BUILD_TESTS=OFF"
                "-DLIGHTWEIGHT_BUILD_EXAMPLES=OFF"
                "-DLIGHTWEIGHT_BUILD_GUI=OFF"
                "-DLIGHTWEIGHT_BUILD_BENCHMARK=OFF"
                "-DLIGHTWEIGHT_BUILD_LARGEDB_TOOL=OFF"
                "-DLIGHTWEIGHT_BUILD_DOCUMENTATION=OFF"
                "-DLIGHTWEIGHT_BUILD_TOOLS=ON"
                "-DENABLE_TIDY=OFF"
                "-DPEDANTIC_COMPILER_WERROR=OFF"
                # Critical: empty SQL dir prevents the scratch configure from
                # re-entering this very bootstrap branch.
                "-DLUPMIGRATION_SQL_DIR="
            )
            if(CMAKE_TOOLCHAIN_FILE)
                list(APPEND _lup_scratch_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
            endif()
            if(CMAKE_C_COMPILER)
                list(APPEND _lup_scratch_args "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
            endif()
            if(CMAKE_CXX_COMPILER)
                list(APPEND _lup_scratch_args "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
            endif()
            if(CMAKE_BUILD_TYPE)
                list(APPEND _lup_scratch_args "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
            endif()
            if(VCPKG_TARGET_TRIPLET)
                list(APPEND _lup_scratch_args "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}")
            endif()
            if(CMAKE_MAKE_PROGRAM)
                list(APPEND _lup_scratch_args "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
            endif()

            execute_process(
                COMMAND ${CMAKE_COMMAND} ${_lup_scratch_args}
                RESULT_VARIABLE _lup_scratch_configure_result
            )
            if(NOT _lup_scratch_configure_result EQUAL 0)
                file(REMOVE "${_lup_bootstrap_sentinel}")
                message(FATAL_ERROR "LupMigrationsPlugin: scratch configure for lup2dbtool failed "
                                    "(see ${_lup_scratch_dir} for diagnostics)")
            endif()

            execute_process(
                COMMAND ${CMAKE_COMMAND} --build "${_lup_scratch_dir}" --target lup2dbtool
                                          --config "${CMAKE_BUILD_TYPE}"
                RESULT_VARIABLE _lup_scratch_build_result
            )
            if(NOT _lup_scratch_build_result EQUAL 0)
                file(REMOVE "${_lup_bootstrap_sentinel}")
                message(FATAL_ERROR "LupMigrationsPlugin: scratch build of lup2dbtool failed")
            endif()

            find_program(LUP2DBTOOL_BIN
                NAMES lup2dbtool
                PATHS
                    "${_lup_scratch_dir}/target"
                    "${_lup_scratch_dir}/target/${CMAKE_BUILD_TYPE}"
                    "${_lup_scratch_dir}/src/tools/lup2dbtool"
                    "${_lup_scratch_dir}/src/tools/lup2dbtool/${CMAKE_BUILD_TYPE}"
                NO_DEFAULT_PATH
                NO_CACHE
            )
            if(NOT LUP2DBTOOL_BIN)
                file(REMOVE "${_lup_bootstrap_sentinel}")
                message(FATAL_ERROR "LupMigrationsPlugin: lup2dbtool binary not found in scratch build "
                                    "(searched ${_lup_scratch_dir}/target and "
                                    "${_lup_scratch_dir}/src/tools/lup2dbtool)")
            endif()
            unset(_lup_scratch_args)
            unset(_lup_scratch_dir)
        endif()
        unset(_lup_parent_build_ready)

        execute_process(
            COMMAND "${LUP2DBTOOL_BIN}"
                --input-dir "${LUPMIGRATION_SQL_DIR}"
                --input-encoding "${LUPMIGRATION_INPUT_ENCODING}"
                --output "${GENERATED_MIGRATIONS_DIR}/lup_{version}.cpp"
                --manifest "${LUP_MANIFEST_FILE}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            RESULT_VARIABLE _lup_bootstrap_gen_result
        )
        file(REMOVE "${_lup_bootstrap_sentinel}")
        if(NOT _lup_bootstrap_gen_result EQUAL 0)
            message(FATAL_ERROR "LupMigrationsPlugin: lup2dbtool failed during cold-start generation")
        endif()

        # Bump the manifest mtime so the stale check above sees it as fresh on
        # the next reconfigure. lup2dbtool only rewrites the manifest when its
        # bytes change (content-addressed), so a SQL edit that doesn't add or
        # remove a generated file leaves the manifest mtime stale and would
        # re-trigger this synchronous bootstrap on every configure.
        file(TOUCH "${LUP_MANIFEST_FILE}")
    endif()
    unset(_lup_manifest_needs_refresh)

    # If we're inside a recursive reconfigure triggered by the bootstrap's inner
    # `cmake --build`, the sentinel exists and the manifest does not yet — skip the
    # post-bootstrap wiring entirely. The outer configure resumes once bootstrap
    # completes and will set up `GENERATED_MIGRATION_FILES` and the custom command.
    # The placeholder source keeps the (transient) inner-configure plugin target valid.
    if(EXISTS "${_lup_bootstrap_sentinel}")
        set(GENERATED_MIGRATION_FILES "${GENERATED_MIGRATIONS_DIR}/.bootstrap_placeholder.cpp")
        file(WRITE "${GENERATED_MIGRATION_FILES}"
            "// Transient placeholder during LupMigrationsPlugin cold-start bootstrap.\n"
        )
    else()
        # Read the manifest emitted by lup2dbtool to obtain the authoritative list
        # of generated source files. Used both as the source list for the plugin
        # target and as the OUTPUT set for the build-time regeneration custom command.
        if(NOT EXISTS "${LUP_MANIFEST_FILE}")
            message(FATAL_ERROR "LupMigrationsPlugin: manifest ${LUP_MANIFEST_FILE} missing after bootstrap")
        endif()
        file(STRINGS "${LUP_MANIFEST_FILE}" GENERATED_MIGRATION_FILES)
        list(LENGTH GENERATED_MIGRATION_FILES _lup_generated_count)
        if(_lup_generated_count EQUAL 0)
            message(FATAL_ERROR "LupMigrationsPlugin: manifest ${LUP_MANIFEST_FILE} is empty")
        endif()
        message(STATUS "LupMigrationsPlugin: ${_lup_generated_count} generated source file(s) listed in manifest")
        unset(_lup_generated_count)

        # Custom command to keep the generated sources fresh on incremental builds
        # (editing an SQL file re-runs lup2dbtool). Every generated file is declared
        # as an OUTPUT so ninja owns it as a build artifact: lup2dbtool's content-
        # addressed writes (see `WriteFileIfChanged` in main.cpp) only touch files
        # whose bytes actually change, so an SQL edit invalidates only the affected
        # `lup_*.cpp.o` translation unit instead of the whole plugin.
        #
        # Adding a brand-new SQL release adds a new generated file. A single
        # reconfigure (`cmake --preset <name>`) is sufficient: the stale-manifest
        # check above detects that the new SQL file is newer than the manifest
        # and runs lup2dbtool synchronously, so the manifest read here is current
        # and the new `lup_*.cpp` ends up in the plugin's source list.
        add_custom_command(
            OUTPUT ${GENERATED_MIGRATION_FILES} "${LUP_MANIFEST_FILE}"
            COMMAND lup2dbtool
                --input-dir "${LUPMIGRATION_SQL_DIR}"
                --input-encoding "${LUPMIGRATION_INPUT_ENCODING}"
                --output "${GENERATED_MIGRATIONS_DIR}/lup_{version}.cpp"
                --manifest "${LUP_MANIFEST_FILE}"
            DEPENDS lup2dbtool ${LUPMIGRATION_SQL_FILES}
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Generating C++ migrations from LUP SQL files (one file per version)"
            VERBATIM
        )
    endif()
else()
    if(LUPMIGRATION_SQL_DIR)
        message(WARNING "LupMigrationsPlugin: LUPMIGRATION_SQL_DIR is set but path does not exist: ${LUPMIGRATION_SQL_DIR}")
    else()
        message(STATUS "LupMigrationsPlugin: LUPMIGRATION_SQL_DIR not set, using placeholder migrations")
    endif()

    # Create a placeholder file
    set(PLACEHOLDER_FILE "${GENERATED_MIGRATIONS_DIR}/GeneratedMigrations.cpp")
    file(WRITE "${PLACEHOLDER_FILE}"
        "// Auto-generated placeholder - set LUPMIGRATION_SQL_DIR to generate actual migrations\n"
        "// Example: cmake -DLUPMIGRATION_SQL_DIR=/path/to/lup-sql ..\n"
    )
    set(GENERATED_MIGRATION_FILES "${PLACEHOLDER_FILE}")
endif()

# ============================================================================
# Shared library plugin for dbtool
# ============================================================================

add_library(LupMigrationsPlugin MODULE
    LupMigrationPlugin.cpp
    ${GENERATED_MIGRATION_FILES}
)

target_include_directories(LupMigrationsPlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GENERATED_MIGRATIONS_DIR}
)

target_link_libraries(LupMigrationsPlugin PRIVATE
    Lightweight::Lightweight
    LupTransitionGlue
)

# Disable clang-tidy for generated files as some range migrations
# (e.g., upd_m_2_3_0__2_3_6.sql) combine many versions into large functions
# that exceed readability-function-size thresholds
set_source_files_properties(${GENERATED_MIGRATION_FILES} PROPERTIES
    SKIP_LINTING TRUE
)

# Also set target property to skip clang-tidy on the entire plugin
# This is a workaround since set_source_files_properties SKIP_LINTING
# doesn't work with CMAKE_CXX_CLANG_TIDY
set_target_properties(LupMigrationsPlugin PROPERTIES
    CXX_CLANG_TIDY ""
)

# `add_custom_command(OUTPUT ${GENERATED_MIGRATION_FILES} ...)` above plus those
# files appearing in this target's source list is enough for ninja to wire the
# regeneration step into the plugin's build dependency chain — no explicit
# `add_dependencies` needed.

# Set output directory to plugins folder for easy discovery by dbtool
set_target_properties(LupMigrationsPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""  # Don't add 'lib' prefix on Unix
)

# Platform-specific settings
if(WIN32)
    set_target_properties(LupMigrationsPlugin PROPERTIES
        SUFFIX ".dll"
    )
elseif(APPLE)
    set_target_properties(LupMigrationsPlugin PROPERTIES
        SUFFIX ".dylib"
    )
else()
    set_target_properties(LupMigrationsPlugin PROPERTIES
        SUFFIX ".so"
    )
endif()
