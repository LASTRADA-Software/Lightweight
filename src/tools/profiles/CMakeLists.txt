# LightweightProfiles — installed shared library giving any application the dbtool.yml profiles
# exactly as dbtool sees them. Public header: include/Lightweight/Profiles/ProfileConnection.hpp.
# Everything else (tools_shared, yaml-cpp, the key ring) is private and not exported.

add_library(LightweightProfiles SHARED
    Guarded.hpp
    ListProfiles.cpp
    Passwords.cpp
    ProfileLookup.cpp
    ProfileLookup.hpp
    ResolveProfileConnection.cpp
    SaveProfile.cpp
    include/Lightweight/Profiles/Api.hpp
    include/Lightweight/Profiles/ProfileConnection.hpp
)
add_library(Lightweight::Profiles ALIAS LightweightProfiles)

set_target_properties(LightweightProfiles PROPERTIES
    EXPORT_NAME Profiles
    OUTPUT_NAME LightweightProfiles
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(LightweightProfiles
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(LightweightProfiles PRIVATE BUILD_LIGHTWEIGHT_PROFILES=1)
target_compile_features(LightweightProfiles PUBLIC cxx_std_23)
target_link_libraries(LightweightProfiles PRIVATE tools_shared)

# On POSIX, tools_shared is linked in statically (and so is core Lightweight unless
# LIGHTWEIGHT_BUILD_SHARED=ON), and libstdc++ template instantiations pulled in by this library's
# own objects have default visibility: restrict the export table to the Lightweight::Profiles
# namespace with a linker-level filter (ELF version script / Mach-O symbol list).
if(APPLE)
    # _ZN11Lightweight8Profiles* covers functions; typeinfo/vtable use _ZTI/_ZTS/_ZTV prefixes.
    target_link_options(LightweightProfiles PRIVATE
        "LINKER:-exported_symbol,__ZN11Lightweight8Profiles*"
        "LINKER:-exported_symbol,__ZTIN11Lightweight8Profiles*"
        "LINKER:-exported_symbol,__ZTSN11Lightweight8Profiles*"
        "LINKER:-exported_symbol,__ZTVN11Lightweight8Profiles*")
elseif(UNIX)
    set(_profiles_version_script "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
    target_link_options(LightweightProfiles PRIVATE
        "LINKER:--exclude-libs,ALL"
        "LINKER:--version-script=${_profiles_version_script}")
    set_property(TARGET LightweightProfiles APPEND PROPERTY LINK_DEPENDS "${_profiles_version_script}")
endif()

if(CLANG_TIDY_EXE)
    set_target_properties(LightweightProfiles PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

enable_coverage_for_target(LightweightProfiles)

# ==================================================================================================
# Install: same component split and package as core Lightweight (LightweightConfig.cmake pulls in
# LightweightProfiles-targets.cmake when it was installed). Only built with LIGHTWEIGHT_BUILD_TOOLS,
# so a package installed without tools simply has no Lightweight::Profiles.

include(GNUInstallDirs)

if(NOT WIN32)
    set_target_properties(LightweightProfiles PROPERTIES
        VERSION "${CMAKE_PROJECT_VERSION}"
        SOVERSION "${CMAKE_PROJECT_VERSION_MAJOR}"
    )
    # Let the installed library find its sibling libLightweight.so (LIGHTWEIGHT_BUILD_SHARED=ON).
    if(APPLE)
        set_target_properties(LightweightProfiles PROPERTIES INSTALL_RPATH "@loader_path")
    else()
        set_target_properties(LightweightProfiles PROPERTIES INSTALL_RPATH "$ORIGIN")
    endif()
endif()

install(TARGETS LightweightProfiles
    EXPORT LightweightProfiles-targets
    RUNTIME_DEPENDENCY_SET profiles_deps
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}     COMPONENT Runtime      # DLL on Windows
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}     COMPONENT Runtime      # .so / .dylib and SONAME links
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}     COMPONENT Development  # Windows import library
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT Development
    FILES_MATCHING PATTERN "*.hpp"
)

install(EXPORT LightweightProfiles-targets
    NAMESPACE Lightweight::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Lightweight
    COMPONENT Development
)

if(WIN32)
    install(FILES $<TARGET_PDB_FILE:LightweightProfiles>
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        CONFIGURATIONS Debug RelWithDebInfo
        COMPONENT DebugSymbols
        OPTIONAL
    )
endif()

# Bundle the transitive runtime DLLs (Lightweight.dll, yaml-cpp, VC++ runtime, ...) the way dbtool
# does; off under a package manager that installs them from their own packages.
if(WIN32 AND LIGHTWEIGHT_INSTALL_RUNTIME_DEPENDENCIES)
    install(RUNTIME_DEPENDENCY_SET profiles_deps
        PRE_EXCLUDE_REGEXES  "api-ms-.*" "ext-ms-.*"
        POST_EXCLUDE_REGEXES ".*[Ss]ystem32[/\\\\].*\\.dll"
        DIRECTORIES "$<TARGET_FILE_DIR:LightweightProfiles>"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT Runtime
    )
endif()
