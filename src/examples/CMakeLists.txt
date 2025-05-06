
option(LIGHWEIGHT_EXAMPLE "Build example with example file generated from ddl2cpp" OFF)

if(LIGHWEIGHT_EXAMPLE)
  add_executable(example example.cpp)
  target_link_libraries(example PRIVATE Lightweight::Lightweight)
  target_compile_features(example PUBLIC cxx_std_23)
endif()

add_subdirectory(test_chinook)
