if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
cmake_minimum_required(VERSION 3.10 FATAL_ERROR) # because of c++17
project(nod)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if (MSVC)
    # Shaddup MSVC
    add_definitions(-DUNICODE=1 -D_UNICODE=1 -D__SSE__=1 -D_CRT_SECURE_NO_WARNINGS=1 -DD_SCL_SECURE_NO_WARNINGS=1
        /IGNORE:4221 /wd4018 /wd4800 /wd4005 /wd4311 /wd4267 /wd4244 /wd4200 /wd4305 /wd4067 /wd4146 ${VS_DEFINES})
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
