cmake_minimum_required(VERSION 3.0)
project(NODLib)

if (NOT TARGET LogVisor)
    add_subdirectory(LogVisor)
endif()

include_directories(include ${LOG_VISOR_INCLUDE_DIR})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -D_GLIBCXX_USE_CXX11_ABI=1")
file(GLOB NOD_HEADERS include/NOD/*.hpp)

add_subdirectory(lib)
add_subdirectory(driver)
install(DIRECTORY include/NOD DESTINATION include/NOD)
set(NOD_LIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include CACHE PATH "NODLib include dir" FORCE)
