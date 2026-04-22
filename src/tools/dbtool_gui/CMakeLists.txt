find_package(Qt6 QUIET COMPONENTS Core Gui Qml Quick QuickControls2)

if(NOT Qt6_FOUND)
    message(STATUS "Qt6 (Core Gui Qml Quick QuickControls2) not found — skipping dbtool-gui")
    return()
endif()

# Explicit qt_wrap_cpp instead of AUTOMOC: AUTOMOC incorrectly prefixes the
# generated metaobject code with the enclosing target's header-search root
# (here: "Lightweight"), causing 'Lightweight::Backend' to be emitted for
# classes that live in the global namespace.
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC ON)

qt_wrap_cpp(dbtool-gui_moc_sources Backend.hpp)

add_executable(dbtool-gui
    main.cpp
    Backend.hpp
    qml.qrc
    ${dbtool-gui_moc_sources}
)

target_include_directories(dbtool-gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../dbtool
)

target_link_libraries(dbtool-gui PRIVATE
    dbtool_lib
    Lightweight::Lightweight
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

target_compile_features(dbtool-gui PRIVATE cxx_std_23)

set_target_properties(dbtool-gui PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

install(TARGETS dbtool-gui
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
)
