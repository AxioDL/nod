add_executable(nodtool main.cpp)

target_link_libraries(nodtool nod logvisor)
if (NOT WIN32)
    target_link_libraries(nodtool pthread dl)
endif()
