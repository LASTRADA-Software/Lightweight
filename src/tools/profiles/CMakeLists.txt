# LightweightProfiles — installed shared library giving any application the dbtool.yml profiles
# exactly as dbtool sees them. Public header: include/Lightweight/Profiles/ProfileConnection.hpp.
# Everything else (tools_shared, yaml-cpp, the key ring) is private and not exported.

add_library(LightweightProfiles SHARED
    Guarded.hpp
    Passwords.cpp
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

# tools_shared and core Lightweight are static here; keep their symbols out of the export table.
if(UNIX AND NOT APPLE)
    target_link_options(LightweightProfiles PRIVATE "LINKER:--exclude-libs,ALL")
endif()

if(CLANG_TIDY_EXE)
    set_target_properties(LightweightProfiles PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

enable_coverage_for_target(LightweightProfiles)
