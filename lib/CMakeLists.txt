add_library(nod
            aes.cpp
            sha1.c
            DiscBase.cpp
            DiscGCN.cpp
            DiscIOISO.cpp
            DiscIOWBFS.cpp
            DiscWii.cpp
            DirectoryEnumerator.cpp
            nod.cpp
            ${NOD_HEADERS})
target_link_libraries(nod PUBLIC logvisor)

if(WIN32)
  target_sources(nod PRIVATE FileIOWin32.cpp)
else()
  target_compile_options(nod PRIVATE -Wno-multichar)
  target_sources(nod PRIVATE FileIOFILE.cpp)
endif()

if(NOT MSVC AND NOT NX)
  set_source_files_properties(aes.cpp PROPERTIES COMPILE_FLAGS -maes)
endif()

# Associate target with export
install(
    TARGETS nod
    EXPORT nodTargets
    ARCHIVE DESTINATION "lib"
    INCLUDES DESTINATION include  # This sets the INTERFACE_INCLUDE_DIRECTORIES property of the target.
)
