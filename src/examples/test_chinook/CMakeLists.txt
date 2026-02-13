add_executable(chinook main.cpp)
target_link_libraries(chinook PRIVATE Lightweight::Lightweight)
target_compile_features(chinook PUBLIC cxx_std_23)

if(LIGHTWEIGHT_BUILD_MODULES)
    set_target_properties(chinook PROPERTIES
        CXX_SCAN_FOR_MODULES ON
    )
endif()
