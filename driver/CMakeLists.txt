add_executable(nodtool
               main.cpp)

target_link_libraries(nodtool NOD LogVisor)
if (NOT WIN32)
    target_link_libraries(nodtool pthread)
endif()
