add_executable(todo main.cpp)

# The example pipes Lightweight coroutines into stdexec senders, so it includes
# <Lightweight/Async/Sender.hpp> (which pulls in the stdexec headers) and must link stdexec itself —
# Lightweight links stdexec PRIVATE and does not re-export it. STDEXEC::stdexec is resolved at the
# top-level CMakeLists and is visible project-wide.
target_link_libraries(todo PRIVATE Lightweight::Lightweight STDEXEC::stdexec)
target_compile_features(todo PUBLIC cxx_std_23)

if(LIGHTWEIGHT_BUILD_MODULES)
    set_target_properties(todo PROPERTIES
        CXX_SCAN_FOR_MODULES ON
    )
endif()
