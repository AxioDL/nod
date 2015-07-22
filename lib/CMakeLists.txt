if(NOT WIN32)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-multichar")
endif()
add_library(NOD
            aes.cpp
            DiscBase.cpp
            DiscGCN.cpp
            DiscIOISO.cpp
            DiscIOWBFS.cpp
            DiscWii.cpp
            FileIOFILE.cpp
            FileIOMEM.cpp
            NOD.cpp
            WideStringConvert.cpp
            ${NOD_HEADERS})
if(NOT WIN32)
set_source_files_properties(aes.cpp PROPERTIES COMPILE_FLAGS -maes)
endif()
