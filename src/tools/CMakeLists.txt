add_executable(ddl2cpp ddl2cpp.cpp)
target_link_libraries(ddl2cpp PRIVATE Lightweight::Lightweight)
target_compile_features(ddl2cpp PUBLIC cxx_std_23)
install(TARGETS ddl2cpp DESTINATION bin)

add_executable(dbtool dbtool.cpp)
target_link_libraries(dbtool PRIVATE Lightweight::Lightweight)
target_compile_features(dbtool PUBLIC cxx_std_23)
install(TARGETS dbtool DESTINATION bin)
