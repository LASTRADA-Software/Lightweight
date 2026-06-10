add_library(dbtool_lib STATIC
    StandardProgressManager.cpp
    StandardProgressManager.hpp
)

target_link_libraries(dbtool_lib PUBLIC Lightweight tools_shared)

add_executable(dbtool main.cpp BackupDiff.cpp BackupDiff.hpp)
if(WIN32)
    find_package(libzip CONFIG REQUIRED)
    target_link_libraries(dbtool PRIVATE dbtool_lib libzip::zip)
else()
    target_link_libraries(dbtool PRIVATE dbtool_lib zip)
endif()

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
