if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR) # because of CMAKE_CXX_STANDARD
project(nod)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
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
