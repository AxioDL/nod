if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
cmake_minimum_required(VERSION 3.0)
project(nod)
endif()

if(NOT MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
endif()

if (NOT TARGET logvisor)
    add_subdirectory(logvisor)
    set(LOGVISOR_INCLUDE_DIR logvisor/include)
endif()

include_directories(include ${LOGVISOR_INCLUDE_DIR})
file(GLOB NOD_HEADERS include/nod/*.h*)

add_subdirectory(lib)
add_subdirectory(driver)
install(DIRECTORY include/nod DESTINATION include/nod)
