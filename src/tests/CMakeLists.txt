find_package(Catch2 REQUIRED)

add_executable(LightweightTest)
target_compile_features(LightweightTest PUBLIC cxx_std_23)

set(TEST_LIBRARIES Catch2::Catch2 Lightweight::Lightweight)
if(MSVC)
    target_compile_options(LightweightTest PRIVATE /MP)
else()
    set(TEST_LIBRARIES ${TEST_LIBRARIES} odbc ${SQLITE3_LIBRARY}) # FIXME: should be PkgConfig::ODBC in Lightweight target already
endif()

target_link_libraries(LightweightTest PRIVATE ${TEST_LIBRARIES})

set(SOURCE_FILES
    CoreTests.cpp
    DataBinderTests.cpp
    DataMapperTests.cpp
    MigrationTests.cpp
    QueryBuilderTests.cpp
    UnicodeConverterTests.cpp
)

target_sources(LightweightTest PRIVATE ${SOURCE_FILES})

enable_testing()
add_test(NAME LightweightTest COMMAND LightweightTest)

install(TARGETS LightweightTest DESTINATION bin)
