add_executable(LightweightBenchmark benchmark.cpp)
target_compile_features(LightweightBenchmark PUBLIC cxx_std_23)
target_link_libraries(LightweightBenchmark Lightweight::Lightweight)

# When the entities were generated with `ddl2cpp --generate-instantiations`, the headers carry
# `extern template` declarations, so the heavy relation machinery must be linked from the generated
# instantiation library instead of being instantiated in benchmark.cpp.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/entities/CMakeLists.txt)
    add_subdirectory(entities)
    target_link_libraries(LightweightBenchmark LightweightEntities)
endif()

# Compilation-time profiling. When enabled (and building with Clang), emit a
# Chrome-trace (`benchmark.cpp.json`) next to the object file that
# measure_compile_time.py aggregates into per-template-instantiation timings.
option(LIGHTWEIGHT_BENCHMARK_TIME_TRACE "Emit -ftime-trace for the benchmark target" OFF)
if(LIGHTWEIGHT_BENCHMARK_TIME_TRACE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(LightweightBenchmark PRIVATE -ftime-trace -ftime-trace-granularity=100)
    else()
        message(WARNING "LIGHTWEIGHT_BENCHMARK_TIME_TRACE requires Clang; ignoring for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()
