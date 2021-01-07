add_library(nod
  aes.cpp
  sha1.c

  DirectoryEnumerator.cpp
  DiscBase.cpp
  DiscGCN.cpp
  DiscIOISO.cpp
  DiscIONFS.cpp
  DiscIOWBFS.cpp
  DiscWii.cpp
  nod.cpp

  ../include/nod/aes.hpp
  ../include/nod/DirectoryEnumerator.hpp
  ../include/nod/DiscBase.hpp
  ../include/nod/DiscGCN.hpp
  ../include/nod/DiscWii.hpp
  ../include/nod/IDiscIO.hpp
  ../include/nod/IFileIO.hpp
  ../include/nod/nod.hpp
  ../include/nod/sha1.h
  ../include/nod/Util.hpp
)

target_include_directories(nod PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
)

target_link_libraries(nod PUBLIC logvisor)

if(WIN32)
  target_sources(nod PRIVATE FileIOWin32.cpp)
else()
  target_compile_options(nod PRIVATE -Wno-multichar)
  target_sources(nod PRIVATE FileIOFILE.cpp)
endif()

if(NOT MSVC AND ${CMAKE_SYSTEM_PROCESSOR} STREQUAL x86_64)
  set_source_files_properties(aes.cpp PROPERTIES COMPILE_FLAGS -maes)
endif()

# Associate target with export
install(
    TARGETS nod
    EXPORT nodTargets
    ARCHIVE DESTINATION "lib"
    INCLUDES DESTINATION include  # This sets the INTERFACE_INCLUDE_DIRECTORIES property of the target.
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../include/nod DESTINATION include)
