add_library(dbtool_lib STATIC
    BackupDiff.cpp
    BackupDiff.hpp
    StandardProgressManager.cpp
    StandardProgressManager.hpp
)

# BackupDiff reads zip archives directly, so dbtool_lib needs libzip. Kept PUBLIC so both the
# dbtool executable and the test target (which links dbtool_lib) resolve the dependency. The `zip`
# target itself is resolved once in the root CMakeLists.txt for every platform (find_package first,
# CPM fallback if not found — #543); re-resolving libzip here would hard-fail a CPM-fallback build.
target_link_libraries(dbtool_lib PUBLIC Lightweight Lightweight::Tools tools_shared zip)

add_executable(dbtool main.cpp)
target_link_libraries(dbtool PRIVATE dbtool_lib)

install(TARGETS dbtool
    RUNTIME_DEPENDENCY_SET dbtool_deps
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
)

# Collect transitive DLLs for the Windows MSI: Lightweight.dll, libzip,
# yaml-cpp, zlib, bz2, icu, and the VC++ runtime. PRE_EXCLUDE filters Windows
# API stubs that are never redistributable; POST_EXCLUDE drops anything that
# resolved to a system32 path (kernel32, ole32, etc.).
if(WIN32)
    install(RUNTIME_DEPENDENCY_SET dbtool_deps
        PRE_EXCLUDE_REGEXES  "api-ms-.*" "ext-ms-.*"
        POST_EXCLUDE_REGEXES ".*[Ss]ystem32[/\\\\].*\\.dll"
        DIRECTORIES "$<TARGET_FILE_DIR:dbtool>"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT Runtime
    )
endif()

# Enable coverage instrumentation
enable_coverage_for_target(dbtool_lib)
enable_coverage_for_target(dbtool)
