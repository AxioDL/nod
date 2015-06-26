TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt
QT =

INCLUDEPATH += include

HEADERS += \
    include/NODLib.hpp \
    include/IDiscIO.hpp \
    include/IFileIO.hpp \
    include/DiscBase.hpp \
    include/DiscGCN.hpp \
    include/DiscWii.hpp

SOURCES += \
    lib/NODLib.cpp \
    lib/DiscIOFILE.cpp \
    lib/DiscIOWBFS.cpp \
    lib/FileIOFILE.cpp \
    lib/FileIOMEM.cpp \
    lib/DiscBase.cpp \
    lib/DiscGCN.cpp \
    lib/DiscWii.cpp \
    main.cpp
