option(LIGHTWEIGHT_BUILD_TESTS "Build Lightweight tests" ON)

if(NOT LIGHTWEIGHT_BUILD_TESTS)
    return()
endif()

find_package(Catch2 REQUIRED)

add_executable(LightweightTest)
target_compile_features(LightweightTest PUBLIC cxx_std_23)

set(TEST_LIBRARIES Catch2::Catch2 Lightweight::Lightweight Lightweight::Tools)
if(MSVC)
    # clang-cl does not support /MP
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(Lightweight PRIVATE /MP)
    endif()
else()
    set(TEST_LIBRARIES ${TEST_LIBRARIES} odbc ${SQLITE3_LIBRARY}) # FIXME: should be PkgConfig::ODBC in Lightweight target already
endif()

target_link_libraries(LightweightTest PRIVATE ${TEST_LIBRARIES})

if(MSVC)
    target_compile_options(LightweightTest PRIVATE /bigobj)
endif()

set(SOURCE_FILES
    CoreTests.cpp
    CxxModelPrinterTests.cpp
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
