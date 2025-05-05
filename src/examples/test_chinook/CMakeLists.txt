

add_executable(chinook main.cpp)
target_link_libraries(chinook PRIVATE Lightweight::Lightweight)
target_compile_features(chinook PUBLIC cxx_std_23)
