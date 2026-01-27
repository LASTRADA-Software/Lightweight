# SPDX-License-Identifier: Apache-2.0

# LupMigrationsPlugin - Migration plugin containing LUP SQL migrations

# ============================================================================
# Configuration
# ============================================================================

# Path to directory containing LUP SQL migration files (init_m_*.sql, upd_m_*.sql)
# If set, lup2dbtool will be run during build to generate C++ migrations
# If empty, the plugin will be built with an empty placeholder
set(LUPMIGRATION_SQL_DIR "" CACHE PATH "Directory containing LUP SQL migration files")

# Input encoding for SQL files (windows-1252 or utf-8)
set(LUPMIGRATION_INPUT_ENCODING "windows-1252" CACHE STRING "Input encoding for LUP SQL files")
set_property(CACHE LUPMIGRATION_INPUT_ENCODING PROPERTY STRINGS "windows-1252" "utf-8")

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

    # Custom command to generate C++ migrations from SQL files (one file per migration)
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

    # Custom target to ensure generation happens before we try to glob for files
    add_custom_target(GenerateLupMigrations
        DEPENDS "${GENERATION_STAMP_FILE}"
    )

    # We need to run configure-time glob after the files exist
    # For the first configure, use a placeholder. After lup2dbtool runs, reconfigure will pick up all files.
    if(EXISTS "${GENERATION_STAMP_FILE}")
        file(GLOB GENERATED_MIGRATION_FILES "${GENERATED_MIGRATIONS_DIR}/lup_*.cpp")
    else()
        # First configure - files don't exist yet. Create a placeholder that will be replaced.
        set(PLACEHOLDER_FILE "${GENERATED_MIGRATIONS_DIR}/GeneratedMigrations.cpp")
        if(NOT EXISTS "${PLACEHOLDER_FILE}")
            file(WRITE "${PLACEHOLDER_FILE}"
                "// Placeholder - regenerate by running: cmake --build . --target GenerateLupMigrations\n"
                "// Then reconfigure CMake to pick up the generated files\n"
            )
        endif()
        set(GENERATED_MIGRATION_FILES "${PLACEHOLDER_FILE}")
        message(STATUS "LupMigrationsPlugin: Run 'cmake --build . --target GenerateLupMigrations' then reconfigure")
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
        "// Example: cmake -DLUPMIGRATION_SQL_DIR=/path/to/model4_JP ..\n"
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
