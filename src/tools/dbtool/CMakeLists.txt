add_executable(dbtool
    main.cpp
    PluginLoader.cpp
    PluginLoader.hpp
)

target_link_libraries(dbtool PRIVATE Lightweight yaml-cpp)

if(UNIX)
    target_link_libraries(dbtool PRIVATE ${CMAKE_DL_LIBS})
endif()

install(TARGETS dbtool DESTINATION bin)
