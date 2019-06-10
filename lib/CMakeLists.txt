if(NOT WIN32)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-multichar")
set(PLAT_SRCS FileIOFILE.cpp)
else()
set(PLAT_SRCS FileIOWin32.cpp)
endif()
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
            ${PLAT_SRCS}
            ${NOD_HEADERS})
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
