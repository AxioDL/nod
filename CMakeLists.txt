if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.10 FATAL_ERROR) # because of c++17
  project(nod VERSION 0.1)

  if (MSVC)
    # Shaddup MSVC
    add_compile_definitions(UNICODE=1 _UNICODE=1 __SSE__=1
        _CRT_SECURE_NO_WARNINGS=1 D_SCL_SECURE_NO_WARNINGS=1
        _SCL_SECURE_NO_DEPRECATE=1 _CRT_NONSTDC_NO_WARNINGS=1
        _ENABLE_EXTENDED_ALIGNED_STORAGE=1 NOMINMAX=1)
    add_compile_options(/IGNORE:4221 /wd4018 /wd4800 /wd4005 /wd4311 /wd4068
        /wd4267 /wd4244 /wd4200 /wd4305 /wd4067 /wd4146 /wd4309 /wd4805 ${VS_OPTIONS})

    add_compile_options(
      # Disable exceptions
      $<$<COMPILE_LANGUAGE:CXX>:/EHsc->

      # Disable RTTI
      $<$<COMPILE_LANGUAGE:CXX>:/GR->

      # Enforce various standards compliant behavior.
      $<$<COMPILE_LANGUAGE:CXX>:/permissive->

      # Enable standard volatile semantics.
      $<$<COMPILE_LANGUAGE:CXX>:/volatile:iso>

      # Reports the proper value for the __cplusplus preprocessor macro.
      $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>

      # Use latest C++ standard.
      $<$<COMPILE_LANGUAGE:CXX>:/std:c++latest>
    )
    add_compile_definitions(FMT_EXCEPTIONS=0 _HAS_EXCEPTIONS=0)

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
      # Flags for MSVC (not clang-cl)
      add_compile_options(
        # Allow constexpr variables to have explicit external linkage.
        $<$<COMPILE_LANGUAGE:CXX>:/Zc:externConstexpr>

        # Assume that new throws exceptions, allowing better code generation.
        $<$<COMPILE_LANGUAGE:CXX>:/Zc:throwingNew>

        # Link-time Code Generation for Release builds
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/GL>
      )

      # Link-time Code Generation for Release builds
      set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "/LTCG")
      set(CMAKE_STATIC_LINKER_FLAGS_RELWITHDEBINFO "/LTCG")
      set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/RELEASE /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO")
      set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/DEBUG /RELEASE /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUGTYPE:cv,fixup")
    endif()
  else()
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
  endif()
endif()

include (CMakePackageConfigHelpers)

if (NOT TARGET logvisor)
  add_subdirectory(logvisor)
endif()

add_subdirectory(lib)
add_subdirectory(driver)

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
