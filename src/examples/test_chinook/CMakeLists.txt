add_executable(chinook main.cpp)
target_link_libraries(chinook PRIVATE Lightweight::Lightweight)
target_compile_features(chinook PUBLIC cxx_std_23)

# The generated Chinook records instantiate enough DataMapper templates in one TU to exceed
# MSVC's default COFF section limit in debug builds (C1128).
if(MSVC)
    target_compile_options(chinook PRIVATE /bigobj)
endif()

if(LIGHTWEIGHT_BUILD_MODULES)
    set_target_properties(chinook PROPERTIES
        CXX_SCAN_FOR_MODULES ON
    )
endif()
