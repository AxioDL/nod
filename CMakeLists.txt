if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
cmake_minimum_required(VERSION 3.0)
project(NODLib)
endif()

if(NOT MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
endif()

if (NOT TARGET LogVisor)
    add_subdirectory(LogVisor)
    set(LOG_VISOR_INCLUDE_DIR LogVisor/include)
endif()

include_directories(include ${LOG_VISOR_INCLUDE_DIR})
file(GLOB NOD_HEADERS include/NOD/*.h*)

add_subdirectory(lib)
add_subdirectory(driver)
install(DIRECTORY include/NOD DESTINATION include/NOD)
