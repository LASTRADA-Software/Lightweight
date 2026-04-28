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

    # Stamp file to track when generation was last run
    set(GENERATION_STAMP_FILE "${GENERATED_MIGRATIONS_DIR}/.generation_stamp")

    # Cold-start bootstrap: on the very first configure (or after a clean of the
    # generated directory) the `lup_*.cpp` files do not yet exist, so a configure-time
    # glob would yield nothing and the plugin would link without any migrations —
    # exactly the regression that produced "nothing to migrate" against an empty DB.
    #
    # Build lup2dbtool synchronously, run it, and populate the directory *during*
    # configure. The build-time custom command below still owns incremental updates
    # when individual SQL files change.
    # Re-entrancy guard: when ninja runs the build, it invokes
    # `cmake --regenerate-during-build` before each build. That re-runs configure,
    # which would re-enter this branch, recursively call `cmake --build`, and
    # deadlock. The sentinel file prevents the inner configure from trying to
    # bootstrap a second time.
    set(_lup_bootstrap_sentinel "${GENERATED_MIGRATIONS_DIR}/.bootstrap_in_progress")
    if(NOT EXISTS "${GENERATION_STAMP_FILE}" AND NOT EXISTS "${_lup_bootstrap_sentinel}")
        message(STATUS "LupMigrationsPlugin: Cold-start — bootstrapping lup2dbtool and generating sources")
        file(TOUCH "${_lup_bootstrap_sentinel}")
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
        # in the build tree.
        find_program(LUP2DBTOOL_BIN
            NAMES lup2dbtool
            PATHS "${CMAKE_BINARY_DIR}/src/tools/lup2dbtool"
            NO_DEFAULT_PATH
            NO_CACHE
        )
        if(NOT LUP2DBTOOL_BIN)
            file(REMOVE "${_lup_bootstrap_sentinel}")
            message(FATAL_ERROR "LupMigrationsPlugin: lup2dbtool binary not found after build "
                                "(expected under ${CMAKE_BINARY_DIR}/src/tools/lup2dbtool)")
        endif()
        execute_process(
            COMMAND "${LUP2DBTOOL_BIN}"
                --input-dir "${LUPMIGRATION_SQL_DIR}"
                --input-encoding "${LUPMIGRATION_INPUT_ENCODING}"
                --output "${GENERATED_MIGRATIONS_DIR}/lup_{version}.cpp"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            RESULT_VARIABLE _lup_bootstrap_gen_result
        )
        file(REMOVE "${_lup_bootstrap_sentinel}")
        if(NOT _lup_bootstrap_gen_result EQUAL 0)
            message(FATAL_ERROR "LupMigrationsPlugin: lup2dbtool failed during cold-start generation")
        endif()
        file(TOUCH "${GENERATION_STAMP_FILE}")
    endif()

    # Custom command to keep the generated sources fresh on incremental builds
    # (editing or adding an SQL file re-runs lup2dbtool). The cold-start bootstrap
    # above guarantees the outputs already exist before this is invoked the first time.
    add_custom_command(
        OUTPUT "${GENERATION_STAMP_FILE}"
        COMMAND lup2dbtool
            --input-dir "${LUPMIGRATION_SQL_DIR}"
            --input-encoding "${LUPMIGRATION_INPUT_ENCODING}"
            --output "${GENERATED_MIGRATIONS_DIR}/lup_{version}.cpp"
        COMMAND ${CMAKE_COMMAND} -E touch "${GENERATION_STAMP_FILE}"
        DEPENDS lup2dbtool ${LUPMIGRATION_SQL_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Generating C++ migrations from LUP SQL files (one file per version)"
        VERBATIM
    )

    add_custom_target(GenerateLupMigrations
        DEPENDS "${GENERATION_STAMP_FILE}"
    )

    # CONFIGURE_DEPENDS: when the build-time custom command emits new `lup_*.cpp`
    # files (e.g. a new SQL release adds a fresh version, or the splitter changes
    # the number of `_partNN` files), CMake automatically re-globs at the next
    # build invocation and the new sources enter the build without a manual
    # `cmake` reconfigure.
    file(GLOB GENERATED_MIGRATION_FILES CONFIGURE_DEPENDS
        "${GENERATED_MIGRATIONS_DIR}/lup_*.cpp"
    )
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

# Ensure lup2dbtool is built before the plugin if we're generating migrations
if(TARGET GenerateLupMigrations)
    add_dependencies(LupMigrationsPlugin GenerateLupMigrations)
endif()

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
