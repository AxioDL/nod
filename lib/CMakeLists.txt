if(NOT WIN32)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-multichar")
endif()
add_library(NOD
            aes.cpp
            sha1.c
            DiscBase.cpp
            DiscGCN.cpp
            DiscIOISO.cpp
            DiscIOWBFS.cpp
            DiscWii.cpp
            FileIOFILE.cpp
            DirectoryEnumerator.cpp
            NOD.cpp
            ${NOD_HEADERS})
if(NOT WIN32)
set_source_files_properties(aes.cpp PROPERTIES COMPILE_FLAGS -maes)
endif()
