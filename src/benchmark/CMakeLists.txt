
option(LIGHWEIGHT_BENCHMARK "Enable lightweight benchmark" OFF)

# Enable benchmark
if(LIGHWEIGHT_BENCHMARK)
  add_executable(LightweightBenchmark benchmark.cpp)
  target_compile_features(LightweightBenchmark PUBLIC cxx_std_23)
  target_link_libraries(LightweightBenchmark Lightweight::Lightweight)
endif()
