if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
cmake_minimum_required(VERSION 3.10 FATAL_ERROR) # because of c++17
project(nod VERSION 0.1)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include (CMakePackageConfigHelpers)

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
install(DIRECTORY include/nod DESTINATION include)

set(version_config_file "${PROJECT_BINARY_DIR}/nodConfigVersion.cmake")
set(config_file "${PROJECT_BINARY_DIR}/nodConfig.cmake")
set(config_install_dir "lib/cmake/nod")

# Install the target config files
install(
    EXPORT nodTargets
    NAMESPACE "nod::"
    DESTINATION "${config_install_dir}"
)

# Generate version config file
write_basic_package_version_file(
    "${version_config_file}"
    COMPATIBILITY SameMajorVersion
)

# Generate config file
configure_package_config_file(
    "Config.cmake.in"
    "${config_file}"
    INSTALL_DESTINATION "lib/cmake/nod"
)

# Install the config files
install(
    FILES "${config_file}" "${version_config_file}"
    DESTINATION ${config_install_dir}
)
