add_executable(chinook_modules main.cpp)
target_link_libraries(chinook_modules PRIVATE Lightweight::Lightweight)
target_compile_features(chinook_modules PUBLIC cxx_std_23)

if(LIGHTWEIGHT_BUILD_MODULES)
    message(STATUS "Building chinook_modules example with C++20 module support")

    set_target_properties(chinook_modules PROPERTIES
        CXX_SCAN_FOR_MODULES ON
    )
endif()
